#define _GNU_SOURCE

#include "../ctlst-runtime.h"
#include <errno.h>
#include <fcntl.h>
#include <glib-unix.h>
#include <gtk/gtk.h>
#include <gtk4-layer-shell.h>
#include <epoxy/gl.h>
#include <cairo.h>
#include <json-c/json.h>
#include <linux/input.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "home-config.h"
#include "home-allocation-host.h"
#include "home-edit-frame.h"
#include "home-picker-host.h"
#include "home-launcher-host.h"
#include "home-pager.h"
#include "home-rotation.h"
#include "home-widget-desc.h"
#include "home-widget-runtime.h"

#define APP_ID "dev.ctlst.Home"
#define HOME_SOCKET "ctlst-home.sock"
#define HOME_STATE "ctlsthome.visible"
/* Ignore brief GTK focus churn while typing; end the session only if leave sticks. */
#define PROMPT_LEAVE_GRACE_MS 180
/* Collapse rapid prompt taps into one Wayland keyboard-seat handoff. */
#define PROMPT_RECLAIM_DELAY_MS 80
#define HOME_GRID_BUILTINS 4
#define HOME_GRID_WIDGET_MAX (HOME_GRID_BUILTINS + HOME_WIDGET_DESC_MAX)
/* Handle geometry stays compile-time; grid/drag/page tunables live in home_config. */
#define HOME_WIDGET_EDGE_SIZE 28.0
/* Preserve the legacy 61x39 resize region independently of the smaller CSS mark. */
#define HOME_WIDGET_HANDLE_MARGIN 7.0
#define HOME_WIDGET_HANDLE_BORDER 2.0
#define HOME_WIDGET_HANDLE_PAD 7.0
#define HOME_WIDGET_HANDLE_MIN_W 36.0
#define HOME_WIDGET_HANDLE_MIN_H 28.0
#define HOME_WIDGET_HANDLE_HIT_W (HOME_WIDGET_HANDLE_MARGIN + \
    2.0 * HOME_WIDGET_HANDLE_BORDER + 2.0 * HOME_WIDGET_HANDLE_PAD + \
    HOME_WIDGET_HANDLE_MIN_W)
#define HOME_WIDGET_HANDLE_HIT_H (HOME_WIDGET_HANDLE_MARGIN + \
    2.0 * HOME_WIDGET_HANDLE_BORDER + HOME_WIDGET_HANDLE_MIN_H)
#define HOME_RESIZE_LEFT (1U << 0)
#define HOME_RESIZE_RIGHT (1U << 1)
#define HOME_RESIZE_TOP (1U << 2)
#define HOME_RESIZE_BOTTOM (1U << 3)

#define home_cols(h) ((h)->config.grid_columns)
#define home_rows(h) ((h)->config.grid_rows)
#define home_spacing(h) ((h)->config.grid_spacing_px)
#define home_cluster_rows(h) ((h)->config.grid_cluster_rows)
#define home_strip_rows(h) ((h)->config.grid_rows - (h)->config.grid_cluster_rows)
#define HOME_PAGE_PANELS_MAX 8
/* Keep neighboring card surfaces and shadows outside the settled viewport. */
#define HOME_PAGER_SEAM_GUARD_PX 8
/* Home actions are release-only taps, never the tail of a shell pull. */
#define HOME_TAP_SLOP 12.0

struct home;
struct home_drag_snapshot;


/* Retain all pages and move their render tree with GTK/GSK, like Shade. */
typedef struct {
	GtkWidget parent_instance;
	GtkWidget *child;
	double translation_x;
	int pages;
	int page_gap;
	HomeAllocationCallback prepare;
	gpointer prepare_data;
} HomePagerMotion;

typedef GtkWidgetClass HomePagerMotionClass;

G_DEFINE_TYPE(HomePagerMotion, home_pager_motion, GTK_TYPE_WIDGET)

static void
home_pager_motion_measure(GtkWidget *widget, GtkOrientation orientation,
    int for_size, int *minimum, int *natural, int *minimum_baseline,
    int *natural_baseline)
{
	(void)widget;
	(void)orientation;
	(void)for_size;
	*minimum = 0;
	*natural = 0;
	*minimum_baseline = -1;
	*natural_baseline = -1;
}

static void
home_pager_motion_size_allocate(GtkWidget *widget, int width, int height,
    int baseline)
{
	HomePagerMotion *motion = (HomePagerMotion *)widget;
	int child_width;

	if (motion->child == NULL)
		return;
	if (motion->prepare != NULL)
		motion->prepare(motion->prepare_data, width, height);
	child_width = width * MAX(motion->pages, 1) +
	    motion->page_gap * MAX(motion->pages - 1, 0);
	/* Keep child geometry stable, but give GTK the translation as well as
	 * GSK. Snapshot-only motion leaves picking/compute_point on page zero.
	 * Transform-only allocations reuse the child's unchanged layout. */
	graphene_point_t offset = GRAPHENE_POINT_INIT((float)motion->translation_x, 0.0f);
	gtk_widget_allocate(motion->child, child_width, height, baseline,
	    gsk_transform_translate(NULL, &offset));
}

static void
home_pager_motion_snapshot(GtkWidget *widget, GtkSnapshot *snapshot)
{
	HomePagerMotion *motion = (HomePagerMotion *)widget;
	graphene_rect_t viewport;
	int height;
	int width;

	if (motion->child != NULL) {
		width = gtk_widget_get_width(widget);
		height = gtk_widget_get_height(widget);
		if (width <= 0 || height <= 0)
			return;
		viewport = GRAPHENE_RECT_INIT(0.0f, 0.0f,
		    (float)width, (float)height);
		gtk_snapshot_push_clip(snapshot, &viewport);
		gtk_widget_snapshot_child(widget, motion->child, snapshot);
		gtk_snapshot_pop(snapshot);
	}
}

static void
home_pager_motion_dispose(GObject *object)
{
	HomePagerMotion *motion = (HomePagerMotion *)object;

	if (motion->child != NULL) {
		gtk_widget_unparent(motion->child);
		motion->child = NULL;
	}
	G_OBJECT_CLASS(home_pager_motion_parent_class)->dispose(object);
}

static void
home_pager_motion_class_init(HomePagerMotionClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	widget_class->measure = home_pager_motion_measure;
	widget_class->size_allocate = home_pager_motion_size_allocate;
	widget_class->snapshot = home_pager_motion_snapshot;
	object_class->dispose = home_pager_motion_dispose;
}

static void
home_pager_motion_init(HomePagerMotion *motion)
{
	motion->pages = 1;
	gtk_widget_set_overflow(GTK_WIDGET(motion), GTK_OVERFLOW_HIDDEN);
}

static HomePagerMotion *
home_pager_motion_new(GtkWidget *child)
{
	HomePagerMotion *motion = g_object_new(home_pager_motion_get_type(), NULL);

	gtk_widget_set_parent(child, GTK_WIDGET(motion));
	motion->child = child;
	return motion;
}

static void
home_pager_motion_set_geometry(HomePagerMotion *motion, int pages, int page_gap)
{
	pages = MAX(pages, 1);
	page_gap = MAX(page_gap, 0);
	if (motion->pages != pages || motion->page_gap != page_gap) {
		motion->pages = pages;
		motion->page_gap = page_gap;
		gtk_widget_queue_allocate(GTK_WIDGET(motion));
	}
}

static void
home_pager_motion_set_translation(HomePagerMotion *motion, double x)
{
	if (fabs(motion->translation_x - x) >= 0.01) {
		motion->translation_x = x;
		gtk_widget_queue_allocate(GTK_WIDGET(motion));
	}
}

struct home_grid_item {
	struct home *home;
	const char *id;
	GtkWidget *container;
	GtkWidget *card;
	GtkWidget *handle;
	GtkWidget *remove_button;
	const struct home_widget_desc *descriptor;
	struct home_widget_runtime *runtime;
	bool tap_moved;
	int column;
	int row;
	int column_span;
	int row_span;
	int page;
	/* enabled on layout when true; hidden keeps geometry for restore. */
	bool hidden;
};

struct cpu_times {
	unsigned long long total;
	unsigned long long idle;
	bool present;
};

struct status_snapshot {
	char *capacity;
	char *battery_status;
	char *network;
	char *calls;
};

struct home_launcher_icon {
	struct home *home;
	char *desktop_id;
	int page;
	int column;
	int row;
	GtkWidget *button;
};

struct home {
	GtkApplication *application;
	GtkWindow *window;
	GtkEntry *prompt;
	GtkLabel *clock;
	GtkLabel *date;
	GtkLabel *system;
	GtkLabel *agent_status;
	GtkLabel *agent_query;
	GtkLabel *proposed_action;
	GtkSpinner *agent_spinner;
	GtkWidget *action_controls;
	GtkWidget *cancel_action_button;
	GtkWidget *agent_scroller;
	char *pending_action_id;
	GtkLabel *response;
	GtkLabel *network_context;
	GtkLabel *battery_context;
	GtkLabel *cpu_context;
	GtkLabel *memory_context;
	GtkProgressBar *memory_meter;
	GtkButton *ask_button;
	GtkButton *action_button;
	GtkButton *visual_button;
	GtkGLArea *visual;
	GtkWidget *root;
	GtkOverlay *rotation_host;
	GtkWidget *header;
	GtkWidget *agent;
	GtkWidget *suggestions;
	GtkWidget *context;
	GtkOverlay *context_overlay;
	HomeEditFrame *edit_frame;
	GtkWidget *edit_controls;
	GtkGesture *layout_drag;
	GtkOverlay *edit_scene;
	GtkWidget *context_body;
	GtkWidget *page_viewport;
	HomePagerMotion *page_motion;
	GtkWidget *page_strip;
	GtkWidget *page_viewport_pad;
	GtkWidget *page_panels[HOME_PAGE_PANELS_MAX];
	GtkGrid *page_grids[HOME_PAGE_PANELS_MAX];
	int page_panel_count;
	int pager_layout_width;
	int pager_layout_height;
	GtkGrid *context_grid;
	GtkGrid *strip_grid;
	GtkWidget *cluster_host;
	GtkWidget *strip_host;
	struct home_widget_desc widget_descs[HOME_WIDGET_DESC_MAX];
	int widget_desc_count;
	GtkDrawingArea *grid_overlay;
	GtkWidget *grid_anchor;
	GtkWidget *calendar_month;
	GtkWidget *calendar_compact;
	GtkLabel *calendar_compact_date;
	GtkLabel *calendar_compact_weekday;
	GtkWidget *clock_compact;
	GtkLabel *clock_compact_time;
	GtkLabel *clock_compact_date;
	GtkWidget *weather_icon;
	GtkLabel *weather_condition;
	GtkLabel *weather_range;
	GtkLabel *weather_temperature;
	GtkWidget *glance_title;
	guint weather_source;
	struct home_grid_item grid_items[HOME_GRID_WIDGET_MAX];
	size_t grid_item_count;
	struct home_grid_item *selected_grid_item;
	struct home_grid_item *active_drag_item;
	int drag_start_column;
	int drag_start_row;
	int drag_start_column_span;
	int drag_start_row_span;
	int drag_preview_column;
	int drag_preview_row;
	int drag_preview_column_span;
	int drag_preview_row_span;
	double launcher_drag_anchor_gx;
	double launcher_drag_anchor_gy;
	double drag_start_x;
	double drag_start_y;
	double drag_overlay_press_x;
	double drag_overlay_press_y;
	double last_drag_overlay_x;
	double last_drag_overlay_y;
	double launcher_drag_offset_x;
	double launcher_drag_offset_y;
	unsigned int resize_edges;
	bool resize_drag;
	bool layout_editing;
	GtkDrawingArea *analog_clock;
	GPtrArray *launcher_icons;
	GtkWidget *launcher_page_bar;
	GtkLabel *launcher_page_label;
	GtkButton *launcher_page_add;
	GtkButton *launcher_page_remove;
	GtkWidget *edit_toolbar;
	GtkWidget *widget_picker;
	GtkWidget *widget_picker_list;
	GtkLabel *widget_picker_status;
	int launcher_page_count;
	int launcher_current_page;
	bool page_drag_active;
	bool page_drag_pending; /* waiting for horizontal hysteresis */
	bool page_drag_owned; /* suppress swipe until settle finishes */
	bool page_release_suppress; /* never launch the swipe's start target */
	double page_drag_dx;
	double page_drag_start_x;
	double page_drag_start_y;
	double page_strip_x; /* absolute translateX of the page strip */
	guint page_settle_source;
	int64_t page_settle_started_us;
	double page_settle_from;
	double page_settle_to;
	/* >=0: land on this page index when settle completes. */
	int page_settle_commit;
	GtkCssProvider *pager_css;
	bool pager_gsk;
	struct home_launcher_icon *active_launcher_drag;
	struct home_launcher_icon *pending_launcher_press;
	int launcher_drag_start_column;
	int launcher_drag_start_row;
	double launcher_press_x;
	double launcher_press_y;
	bool launcher_drag_armed;
	bool launcher_suppress_click;
	bool launcher_remove_armed;
	guint launcher_drag_arm_source;
	guint launcher_layout_source;
	GtkWidget *launcher_remove_banner;
	GtkWidget *call_card;
	GtkCssProvider *theme_provider;
	GFileMonitor *theme_monitor;
	int control_fd;
	guint control_source;
	guint socket_watch_source;
	guint clock_source;
	guint analog_clock_source;
	guint status_source;
	guint activity_source;
	guint prompt_leave_source;
	guint prompt_reclaim_source;
	guint prompt_post_map_delay_ms;
	GLuint visual_program;
	GLuint visual_vbo;
	GLint visual_time;
	GLint visual_mode_uniform;
	GLint visual_palette_uniforms[4];
	GLfloat visual_palette[4][3];
	GLfloat accent_rgb[3];
	GLfloat text_rgb[3];
	GLfloat line_rgb[3];
	gint64 visual_started;
	unsigned long long previous_cpu_total;
	unsigned long long previous_cpu_idle;
	char socket_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
	dev_t socket_device;
	ino_t socket_inode;
	char state_path[256];
	char focus_path[256];
	char layout_edit_path[256];
	char keyboard_path[256];
	char lock_path[256];
	char prompt_length_path[256];
	char rotation_ready_path[256];
	bool visible;
	bool agent_busy;
	bool landscape;
	bool landscape_known;
	int layout_width;
	int layout_height;
	bool prompt_session;
	bool prompt_focus_pending;
	bool prompt_post_map;
	bool prompt_lock_recovery;
	guint launcher_push_dwell_source;
	int launcher_push_target_column;
	int launcher_push_target_row;
	bool launcher_push_dwell_ready;
	guint page_edge_dwell_source;
	int page_edge_direction;
	bool page_edge_haptic_sent;
	bool cpu_sample_ready;
	bool status_refresh_pending;
	unsigned int prompt_settle_attempts;
	unsigned int visual_mode;
	guint push_dwell_source;
	struct home_grid_item *push_dwell_item;
	int push_dwell_column;
	int push_dwell_row;
	int push_dwell_column_span;
	int push_dwell_row_span;
	bool push_dwell_ready;
	struct home_drag_snapshot *drag_snapshot;
	struct home_config config;
	int haptic_fd;
	int haptic_effect_id;
};

static void ask_agent(GtkWidget *widget, gpointer data);
static gboolean focus_prompt(gpointer data);
static void soft_reclaim_prompt(struct home *home);
static void schedule_prompt_reclaim(struct home *home, guint delay_ms);
static void refresh_launcher_icons(struct home *home);
static void render_launcher_page(struct home *home);
static void relayout_launcher_icons(struct home *home);
static void apply_home_grid(struct home *home);
static void attach_launcher_icon_view(struct home *home,
    struct home_launcher_icon *icon);
static void apply_page_drag_offset(struct home *home, double drag_dx);
static void start_page_settle(struct home *home, double from_dx, double to_dx);
static void abort_page_settle(struct home *home);
static void ensure_page_panels(struct home *home);
static void sync_page_panel_widths(struct home *home);
static int pager_viewport_width(struct home *home);
static int pager_viewport_height(struct home *home);
static int pager_page_stride(struct home *home);
static void schedule_launcher_icon_layout(struct home *home);
static void refresh_launcher_icon_editing(struct home *home);
static void refresh_launcher_page_bar(struct home *home);
static void refresh_edit_controls(struct home *home);
static void set_launcher_page(struct home *home, int page);
static void set_launcher_page_animated(struct home *home, int page,
    bool animate);
static void cancel_launcher_push_dwell(struct home *home);
static void cancel_page_edge_dwell(struct home *home);
static void check_drag_page_edge(struct home *home, double ox, double oy);
static void launcher_icon_clicked(GtkButton *button, gpointer data);
static void launcher_remove_badge_clicked(GtkGestureClick *gesture,
    int n_press, double x, double y, gpointer data);
static void home_launcher_long_pressed(GtkGestureLongPress *gesture, double x,
    double y, gpointer data);
static void home_context_pressed(GtkGestureClick *gesture, int n_press,
    double x, double y, gpointer data);
static void home_context_icon_clicked(GtkGestureClick *gesture, int n_press,
    double x, double y, gpointer data);
static struct home_launcher_icon *home_launcher_icon_at(struct home *home,
    double x, double y);
static struct home_launcher_icon *launcher_remove_badge_at(struct home *home,
    double x, double y);
static struct home_grid_item *home_grid_item_at(struct home *home, double x,
    double y, double *local_x, double *local_y);
static void cancel_launcher_drag_arm(struct home *home);
static gboolean launcher_drag_arm_fire(gpointer data);
static void start_launcher_icon_drag(struct home *home,
    struct home_launcher_icon *icon);
static void update_launcher_icon_drag(struct home *home, double offset_x,
    double offset_y);
static void end_launcher_icon_drag(struct home *home, double offset_x,
    double offset_y);
static void set_launcher_remove_armed(struct home *home, bool armed);
static bool launcher_point_is_remove(struct home *home, double ox, double oy);
static void remove_favorite(struct home *home, const char *favorite);
static void launch_launcher_icon(struct home *home,
    struct home_launcher_icon *icon);
static bool overlay_to_grid_point(struct home *home, double ox, double oy,
    double *gx, double *gy);
static bool point_in_edit_controls(struct home *home, double ox, double oy);
static bool grid_point_to_cell(struct home *home, double gx, double gy,
    int *column_out, int *row_out);
static void cancel_push_dwell(struct home *home);
static void save_drag_layout_snapshot(struct home *home);
static void restore_drag_layout_snapshot(struct home *home);
static void clear_drag_layout_snapshot(struct home *home);
static void reset_drag_preview(struct home *home);
static int grid_index_with_hysteresis(double offset, double step,
    int start_index, int current_index, int min_index, int max_index,
    double margin);
static bool home_widget_target_from_drag(struct home *home,
    struct home_grid_item *item, double offset_x, double offset_y,
    int *column, int *row, int *column_span, int *row_span);
static bool try_place_widget(struct home *home, struct home_grid_item *item,
    int column, int row, int column_span, int row_span, bool allow_push);
static void evacuate_launcher_icons_under_widgets(struct home *home);
static void apply_home_grid(struct home *home);
static void refresh_grid_editing(struct home *home);
static GtkIconPaintable *favorite_icon_paintable(GIcon *icon, int size);
static void start_refresh_sources(struct home *home);
static void stop_refresh_sources(struct home *home);
static gboolean refresh_weather(gpointer data);
static void request_weather_fetch(void);
static bool grid_item_fits(struct home *home, struct home_grid_item *item,
    int column, int row, int column_span, int row_span);
static bool grid_rect_overlaps_launcher_icon(struct home *home, int page,
    int column, int row, int column_span, int row_span,
    const struct home_launcher_icon *skip);
static bool find_available_grid_slot(struct home *home,
    struct home_grid_item *item, int desired_column, int desired_row,
    int column_span, int row_span, int *column_out, int *row_out);
static void reconcile_widgets_with_launcher_icons(struct home *home);
static bool favorite_list_contains(struct json_object *favorites,
    const char *favorite);
static struct home_launcher_icon *
launcher_icon_for_desktop_id(struct home *home, const char *desktop_id);
static bool overlay_to_grid_point(struct home *home, double ox, double oy,
    double *gx, double *gy);
static bool grid_point_to_cell(struct home *home, double gx, double gy,
    int *column_out, int *row_out);
static void apply_launcher_icon_move(struct home *home,
    struct home_launcher_icon *icon, int target_column, int target_row);

static int
open_haptic_device(void)
{
	const char *configured = getenv("CTLST_HAPTIC_DEVICE");
	char name_path[96];
	char event_path[32];
	char name[80];
	int fd;

	if (configured != NULL && configured[0] != '\0')
		return open(configured, O_RDWR | O_NONBLOCK | O_CLOEXEC);
	for (int index = 0; index < 32; index++) {
		ssize_t length;

		snprintf(name_path, sizeof(name_path),
		    "/sys/class/input/event%d/device/name", index);
		fd = open(name_path, O_RDONLY | O_CLOEXEC);
		if (fd < 0)
			continue;
		length = read(fd, name, sizeof(name) - 1);
		close(fd);
		if (length <= 0)
			continue;
		name[length] = '\0';
		name[strcspn(name, "\r\n")] = '\0';
		if (strcmp(name, "drv2624:haptics") != 0)
			continue;
		snprintf(event_path, sizeof(event_path), "/dev/input/event%d", index);
		return open(event_path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
	}
	return -1;
}

static void
init_haptic(struct home *home)
{
	struct ff_effect effect = {0};

	home->haptic_fd = open_haptic_device();
	home->haptic_effect_id = -1;
	if (home->haptic_fd < 0)
		return;
	effect.type = FF_RUMBLE;
	effect.id = -1;
	effect.u.rumble.strong_magnitude = 0x7fff;
	effect.replay.length = 15;
	if (ioctl(home->haptic_fd, EVIOCSFF, &effect) < 0) {
		close(home->haptic_fd);
		home->haptic_fd = -1;
		return;
	}
	home->haptic_effect_id = effect.id;
}

static void
pulse_layout_edit_haptic(const struct home *home)
{
	struct input_event event = {
		.type = EV_FF,
		.code = (uint16_t)home->haptic_effect_id,
		.value = 1,
	};

	if (home->haptic_fd >= 0 && home->haptic_effect_id >= 0) {
		ssize_t written = write(home->haptic_fd, &event, sizeof(event));

		if (written != (ssize_t)sizeof(event))
			return;
	}
}

static bool
legacy_agent_enabled(void)
{
	const char *value = g_getenv("CTLST_LEGACY_AGENT");

	return value != NULL && (g_str_equal(value, "1") ||
	    g_ascii_strcasecmp(value, "true") == 0 ||
	    g_ascii_strcasecmp(value, "yes") == 0);
}

static bool
home_demo_enabled(void)
{
	const char *value = g_getenv("CTLST_HOME_DEMO");

	return value != NULL && (g_str_equal(value, "1") ||
	    g_ascii_strcasecmp(value, "true") == 0 ||
	    g_ascii_strcasecmp(value, "yes") == 0);
}

static bool
home_pager_gsk_enabled(void)
{
	const char *value = g_getenv("CTLST_HOME_PAGER_MOTION");

	return value == NULL || value[0] == '\0' ||
	    g_ascii_strcasecmp(value, "gsk") == 0 ||
	    g_ascii_strcasecmp(value, "1") == 0 ||
	    g_ascii_strcasecmp(value, "true") == 0;
}

static const char *style_css =
	"@define-color ctlst_bg #f7f7f2; @define-color ctlst_raised #e8e9e3;"
	"@define-color ctlst_panel #fffdf7; @define-color ctlst_line #c7c9c2;"
	"@define-color ctlst_line_soft #e4e5df; @define-color ctlst_text #1b1c1a;"
	"@define-color ctlst_muted #454746; @define-color ctlst_accent #355f71;"
	"@define-color ctlst_accent_soft #cce5f0;"
	"@define-color ctlst_warm #7b5e7b; @define-color ctlst_danger #ba1a1a;"
	"window#ctlsthome { color: @ctlst_text; background: transparent; }"
	".home-root { padding: 8px 10px 6px; background: transparent; }"
	".home-clock { color: @ctlst_panel; font-size: 38px; font-weight: 500; }"
	".home-date { color: @ctlst_panel; font-size: 14px; font-weight: 600; }"
	".home-system { color: @ctlst_panel; font-size: 11px; }"
	".home-agent { padding: 12px; border: 0; border-radius: 24px;"
		" background: alpha(@ctlst_panel, .94); }"
		".home-visual { min-height: 220px; border-radius: 30px;"
		" background: transparent; }"
	"button.home-visual-mode { min-height: 44px; padding: 0 12px; border: 0;"
	" border-radius: 15px; color: @ctlst_text;"
	" background: alpha(@ctlst_panel, 0.90); font-size: 12px; font-weight: 500; }"
	".home-agent-title { color: @ctlst_text; font-size: 18px; font-weight: 650; }"
	".home-agent-state { color: @ctlst_muted; font-size: 12px; font-weight: 500; }"
	"entry.home-prompt { min-height: 44px; min-width: 0; padding: 0 12px; border: 0;"
	" border-radius: 22px; color: @ctlst_text; background: alpha(@ctlst_panel, .76);"
	" caret-color: @ctlst_accent; }"
	"entry.home-prompt:focus { outline: 2px solid @ctlst_accent; }"
	"button.home-ask { min-width: 44px; min-height: 44px; padding: 0 10px;"
	" border: 0; border-radius: 22px; color: @ctlst_panel;"
	" background: @ctlst_accent; font-size: 14px; font-weight: 500; }"
	".home-agent button.home-action, .home-agent button.home-agent-cancel {"
	" min-height: 44px; padding: 0 10px; border-radius: 16px; font-size: 13px; }"
	".home-agent button.home-agent-cancel { color: @ctlst_text; background: @ctlst_panel; }"
	".home-suggestions > flowboxchild { padding: 0; }"
	".home-agent button:focus-visible { outline: 2px solid @ctlst_accent; outline-offset: -2px; }"
	".home-response { padding: 2px 2px 0; color: @ctlst_text; font-size: 14px; }"
	".home-query { padding: 7px 10px; border-radius: 13px; color: @ctlst_text;"
	" background: alpha(@ctlst_panel, 0.52); font-size: 10px; }"
	".home-context { padding: 0; background: transparent; }"
	".home-grid { background: transparent; }"
	".home-page-viewport { background: transparent; }"
	".home-page-strip { background: transparent; }"
	".home-page-panel { background: transparent; }"
	".home-cluster, .home-strip { background: transparent; }"
	".home-body-landscape .home-strip { min-width: 72px; }"
	".home-grid-editing { background: alpha(@ctlst_text, .06); }"
	"window#ctlsthome button.home-widget-remove { min-width: 44px; min-height: 44px;"
	" padding: 0; margin: 0; border: 0; border-radius: 0; box-shadow: none;"
	" background: transparent; background-image: none; color: #ffffff; }"
	"window#ctlsthome button.home-widget-remove > label { min-width: 22px; min-height: 22px;"
	" padding: 0; border: 0; border-radius: 11px; background: @ctlst_danger;"
	" color: #ffffff; font-size: 15px; font-weight: 700;"
	" box-shadow: 0 2px 7px alpha(#000000, .25); }"
	"window#ctlsthome button.home-widget-remove:hover > label {"
	" background: shade(@ctlst_danger, .85); }"
	".home-edit-controls { padding-bottom: 28px; }"
	".home-launcher-page-bar button { min-width: 44px; min-height: 44px;"
	"  padding: 0; border: 0; border-radius: 22px; box-shadow: none;"
	"  background-image: none; background: alpha(@ctlst_text, .08); color: @ctlst_text; }"
	".home-launcher-page-bar button:disabled { opacity: .38; }"
	".home-edit-toolbar { padding: 7px; border-radius: 24px;"
	"  background: alpha(@ctlst_panel, .96);"
	"  box-shadow: 0 3px 12px alpha(#000000, .18); }"
	"button.home-edit-action { min-height: 44px; padding: 0 10px;"
	"  border: 0; border-radius: 18px; color: @ctlst_text;"
	"  background: alpha(@ctlst_text, .08); font-size: 14px; font-weight: 500; }"
	"button.home-edit-done { color: @ctlst_selected_text; background: @ctlst_accent; }"
	".home-widget-picker { min-width: 0; min-height: 0;"
	"  padding: 14px; border-radius: 24px; color: @ctlst_text;"
	"  background: alpha(@ctlst_panel, .98);"
	"  box-shadow: 0 4px 18px alpha(#000000, .22); }"
	".home-widget-picker-title { font-size: 20px; font-weight: 600; }"
	".home-widget-picker-status { color: @ctlst_muted; font-size: 12px; }"
	"button.home-widget-choice { min-height: 52px; padding: 0 16px;"
	"  border-radius: 18px; background: alpha(@ctlst_text, .08);"
	"  color: @ctlst_text; font-weight: 650; }"
	".home-widget { min-width: 0; min-height: 0; border-radius: 24px; }"
	".home-widget-clip { min-width: 0; min-height: 0; }"
	".home-widget-text { min-width: 0; }"
	".home-widget-selected { outline: 2px solid @ctlst_accent;"
	" outline-offset: -2px; }"
	"window#ctlsthome .home-widget-handle { min-width: 22px; min-height: 22px;"
	" padding: 0; border: 1px solid @ctlst_line; border-radius: 12px;"
	" background: @ctlst_panel; color: @ctlst_text; font-size: 15px;"
	" font-weight: 500; box-shadow: 0 2px 7px alpha(#000000, .20); }"
	".home-weather-card { min-width: 0; min-height: 0; padding: 12px 16px;"
	" border-radius: 24px; background: @ctlst_panel_alt; color: @ctlst_text;"
	" box-shadow: 0 5px 20px alpha(#080612, .18); }"
	/* Freedesktop weather icons are available across Adwaita and Papirus. */
	".home-weather-icon { -gtk-icon-size: 48px; }"
	".home-weather-temp { color: @ctlst_text; font-size: 38px; font-weight: 500; }"
	".home-weather-card.home-widget-compact { padding: 4px 12px; }"
	".home-widget-compact .home-weather-icon { -gtk-icon-size: 28px; }"
	".home-widget-compact .home-weather-temp { font-size: 24px; }"
	".home-weather-detail { color: @ctlst_muted; font-size: 12px;"
	" min-width: 0; }"
	".home-calendar-card { min-width: 0; min-height: 0;"
	" padding: 14px 16px; border-radius: 24px;"
	" background: @ctlst_panel; color: @ctlst_text;"
	" box-shadow: 0 4px 14px alpha(#000000, .14); }"
	".home-calendar-title { color: @ctlst_text; font-size: 13px; font-weight: 700;"
	" min-width: 0; }"
	".home-calendar-month { min-width: 0; }"
	".home-calendar-day { min-width: 0; min-height: 0;"
	" color: alpha(@ctlst_text, .90); font-size: 10px; }"
	".home-calendar-weekday { color: @ctlst_muted; font-size: 8px;"
	" font-weight: 700; min-width: 0; }"
	".home-calendar-today { border-radius: 11px; background: @ctlst_accent;"
	" color: @ctlst_panel; font-weight: 700; }"
	".home-calendar-compact-date { color: @ctlst_text; font-size: 27px; font-weight: 700; }"
	".home-calendar-compact-weekday { color: @ctlst_muted; font-size: 10px;"
	" font-weight: 700; }"
	".home-clock-card { min-width: 0; min-height: 0; padding: 0;"
	" background: transparent; box-shadow: none; }"
	/* Clock face scales to the grid cell; hard mins blow homogeneous rows and
	 * clip later widgets under the context overflow. */
	".home-analog-clock { min-width: 0; min-height: 0; }"
	".home-clock-compact-time { color: @ctlst_text; font-size: 28px; font-weight: 650; }"
	".home-clock-compact-date { color: @ctlst_muted; font-size: 10px;"
	" font-weight: 650; }"
	".home-glance-card { min-width: 0; min-height: 0; padding: 12px 14px;"
	" border-radius: 24px; background: @ctlst_panel; color: @ctlst_text;"
	" box-shadow: 0 5px 20px alpha(#080612, .16); }"
	".home-glance-title { color: @ctlst_muted; font-size: 10px;"
	" font-weight: 600; min-width: 0; }"
	".home-glance-event { color: @ctlst_text; font-size: 12px; font-weight: 600;"
	" min-width: 0; }"
	".home-glance-meta { color: @ctlst_muted; font-size: 9px;"
	" min-width: 0; }"
	".home-glance-dot { min-width: 34px; min-height: 34px;"
	" border-radius: 17px; background: @ctlst_accent; color: @ctlst_selected_text;"
	" font-size: 17px; font-weight: 700; }"
	".home-glance-card.home-widget-compact { padding: 4px 12px; }"
	".home-widget-compact .home-glance-dot { min-width: 24px; min-height: 24px;"
	" font-size: 14px; }"
	".home-context-tile { min-height: 82px; padding: 13px 15px;"
	" border: 1px solid alpha(@ctlst_line, .56); border-radius: 24px;"
	" color: @ctlst_text; }"
	".home-context-network { background: @ctlst_panel_alt; }"
	".home-context-battery { background: @ctlst_accent_soft; }"
	".home-context-performance { background: alpha(@ctlst_panel, .92); }"
	".home-context-title { color: @ctlst_muted; font-size: 10px; font-weight: 600; }"
	".home-context-dot { min-width: 9px; min-height: 9px; border-radius: 5px; }"
	".home-context-network .home-context-dot { background: @ctlst_accent; }"
	".home-context-battery .home-context-dot { background: @ctlst_warm; }"
	".home-context-performance .home-context-dot { background: @ctlst_accent; }"
	".home-context-value { color: @ctlst_text; font-size: 15px; font-weight: 650; }"
	".home-activity-value { color: @ctlst_text; font-size: 11px; font-weight: 650; }"
	"progressbar.home-activity-meter trough { min-height: 5px; border: 0;"
	" border-radius: 3px; background: alpha(@ctlst_line, .42); }"
	"progressbar.home-activity-meter progress { min-height: 5px; border: 0;"
	" border-radius: 3px; background: @ctlst_accent; }"
	"progressbar.home-memory-meter progress { background: @ctlst_warm; }"
	".home-core-graph { min-height: 25px; }"
	".home-suggestions { padding: 0; background: transparent; }"
	"button.home-suggestion { min-height: 44px; padding: 0 10px; border: 0;"
	" border-radius: 15px; color: @ctlst_text;"
	" background: alpha(@ctlst_panel, 0.50); font-size: 9px; }"
	"button.home-action { min-height: 36px; border: 0; border-radius: 18px;"
	" color: @ctlst_panel; background: @ctlst_accent; font-weight: 650; }"
	"button.home-launcher-icon { min-height: 0; min-width: 0; padding: 0;"
		" border: 0; border-radius: 0; color: @ctlst_text;"
		" background: transparent; }"
	"button.home-launcher-icon:hover { background: transparent; }"
	"button.home-launcher-icon:focus-visible { outline: none; }"
	"button.home-launcher-icon:focus-visible .home-launcher-icon-disc {"
		" background: alpha(@ctlst_accent, .13); }"
	".home-launcher-icon-disc { border-radius: 999px; border: 0;"
		" padding: 0; background: transparent; box-shadow: none; }"
	"button.home-launcher-icon:hover .home-launcher-icon-disc {"
		" background: alpha(@ctlst_accent, .13); }"
	"button.home-launcher-icon.editing .home-launcher-icon-disc {"
		" background: alpha(@ctlst_danger, .14); }"
	"button.home-launcher-icon.removing .home-launcher-icon-disc {"
		" background: alpha(@ctlst_danger, .28); opacity: 0.85; }"
	".home-launcher-remove-badge { min-width: 22px; min-height: 22px;"
		" padding: 0; border-radius: 11px; color: #ffffff;"
		" background: @ctlst_danger; font-size: 15px; font-weight: 800;"
		" box-shadow: 0 2px 7px alpha(#000000, .38); }"
	".home-launcher-remove { padding: 10px 18px; border-radius: 18px;"
		" color: #ffffff; background: alpha(@ctlst_danger, .92);"
		" font-size: 13px; font-weight: 700; }"
	".home-launcher-page-bar { padding: 6px 10px; border-radius: 16px;"
		" background: alpha(@ctlst_panel, .92); }"
	".home-launcher-page-label { color: @ctlst_muted; font-size: 11px;"
		" font-weight: 650; }"
	"button.home-call { min-height: 44px; border: 0; border-radius: 22px;"
	" color: @ctlst_text; background: @ctlst_accent_soft; font-weight: 650; }"
	".home-root.landscape { padding: 12px; }"
	".home-root.landscape .home-clock { font-size: 30px; }"
	".home-root.landscape .home-agent { padding: 12px; }"
	".home-root.landscape .home-calendar-card { margin-left: 0; }"
	/* A 4-row landscape cluster has shorter cells than portrait. Keep all
	 * three At a Glance rows inside its allocation instead of clipping meta. */
	".home-root.landscape .home-glance-card { padding: 4px 12px; }";

static void
home_clip_text(GtkLabel *label)
{
	gtk_label_set_wrap(label, FALSE);
	gtk_label_set_ellipsize(label, PANGO_ELLIPSIZE_END);
	gtk_widget_set_hexpand(GTK_WIDGET(label), TRUE);
	gtk_widget_add_css_class(GTK_WIDGET(label), "home-widget-text");
}

static void
home_clip_box(GtkWidget *widget)
{
	gtk_widget_set_overflow(widget, GTK_OVERFLOW_HIDDEN);
	gtk_widget_add_css_class(widget, "home-widget-clip");
	gtk_widget_set_hexpand(widget, TRUE);
}

static char *
run_capture(char *const argv[])
{
	char *output = NULL;
	char *errors = NULL;
	int status = 0;
	GError *error = NULL;

	if (!g_spawn_sync(NULL, (char **)argv, NULL, G_SPAWN_SEARCH_PATH,
	    NULL, NULL, &output, &errors, &status, &error)) {
		g_clear_error(&error);
		g_free(errors);
		return g_strdup("");
	}
	g_free(errors);
	return output != NULL ? output : g_strdup("");
}

static void
run_detached(char *const argv[])
{
	GError *error = NULL;

	if (!g_spawn_async(NULL, (char **)argv, NULL,
	    G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL |
	    G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, &error))
		g_clear_error(&error);
}

/* Launch via app-run so lifecycle/scopes match drawer favorites. */
static void
launch_desktop_id(const char *desktop_id)
{
	char *scripts;
	char *app_run;
	char *argv[4];

	if (desktop_id == NULL || desktop_id[0] == '\0')
		return;
	scripts = ctlst_script_path(NULL);
	app_run = g_build_filename(scripts, "app-run", NULL);
	argv[0] = app_run;
	argv[1] = "desktop";
	argv[2] = (char *)desktop_id;
	argv[3] = NULL;
	run_detached(argv);
	g_free(app_run);
	g_free(scripts);
}

/*
 * Linux defaults: MIME handlers via xdg-mime / GAppInfo (e.g. text/calendar).
 * Sway has no separate "default clock app"; clocks use a desktop-id fallback
 * (override with CTLST_CLOCK_DESKTOP=org.example.Clock.desktop).
 */
static char *
default_app_desktop_for_type(const char *content_type)
{
	GAppInfo *info;
	const char *id;
	char *copy;

	info = g_app_info_get_default_for_type(content_type, FALSE);
	if (info == NULL)
		return NULL;
	id = g_app_info_get_id(info);
	copy = id != NULL ? g_strdup(id) : NULL;
	g_object_unref(info);
	return copy;
}

static void
activate_home_widget(struct home_grid_item *item)
{
	struct home *home = item->home;

	if (home->layout_editing)
		return;
	if (item->descriptor != NULL) {
		if (strcmp(item->descriptor->interaction, "launch") == 0 &&
		    item->descriptor->desktop[0] != '\0')
			launch_desktop_id(item->descriptor->desktop);
	} else if (g_str_equal(item->id, "calendar")) {
		char *desktop = default_app_desktop_for_type("text/calendar");

		launch_desktop_id(desktop != NULL ? desktop :
		    "org.gnome.Calendar.desktop");
		g_free(desktop);
	} else if (g_str_equal(item->id, "clock")) {
		const char *env = g_getenv("CTLST_CLOCK_DESKTOP");

		launch_desktop_id(env != NULL && env[0] != '\0' ? env :
		    "org.gnome.clocks.desktop");
	} else if (g_str_equal(item->id, "weather")) {
		const char *env = g_getenv("CTLST_WEATHER_DESKTOP");

		/* CTLST Weather: curated location dropdown + Open-Meteo. */
		launch_desktop_id(env != NULL && env[0] != '\0' ? env :
		    "dev.ctlst.Weather.desktop");
	}
}

static void
home_widget_clicked(GtkGestureClick *gesture, int n_press, double x, double y,
    gpointer data)
{
	struct home_grid_item *item = data;

	(void)gesture;
	(void)n_press;
	(void)x;
	(void)y;
	if (item->tap_moved || item->home->page_release_suppress)
		return;
	activate_home_widget(item);
}

static void
home_widget_tap_drag_begin(GtkGestureDrag *gesture, double start_x,
    double start_y, gpointer data)
{
	struct home_grid_item *item = data;

	(void)gesture;
	(void)start_x;
	(void)start_y;
	item->tap_moved = false;
}

static void
home_widget_tap_drag_update(GtkGestureDrag *gesture, double offset_x,
    double offset_y, gpointer data)
{
	struct home_grid_item *item = data;

	(void)gesture;
	if (hypot(offset_x, offset_y) >= HOME_TAP_SLOP)
		item->tap_moved = true;
}

static void
keyboard_control(const char *command)
{
	char *control = ctlst_script_path("keyboard-control");
	char *argv[] = {control, (char *)command, NULL};

	run_detached(argv);
	g_free(control);
}

static bool
home_is_locked(const struct home *home)
{
	return access(home->lock_path, F_OK) == 0;
}

static bool
parse_rgb(const char *value, GLfloat color[3])
{
	int high;
	int low;

	if (value == NULL || strlen(value) < 6)
		return false;
	for (size_t i = 0; i < 3; i++) {
		high = g_ascii_xdigit_value(value[i * 2]);
		low = g_ascii_xdigit_value(value[i * 2 + 1]);
		if (high < 0 || low < 0)
			return false;
		color[i] = (GLfloat)((high << 4) | low) / 255.0f;
	}
	return true;
}

static bool
find_theme_color(const char *contents, const char *key, GLfloat color[3])
{
	const char *line = contents;
	const char *end;
	size_t key_length = strlen(key);
	size_t line_length;

	while (line != NULL && *line != '\0') {
		end = strchr(line, '\n');
		line_length = end == NULL ? strlen(line) : (size_t)(end - line);
		if (line_length >= key_length + 7 &&
		    strncmp(line, key, key_length) == 0 &&
		    line[key_length] == '=' &&
		    parse_rgb(line + key_length + 1, color))
			return true;
		line = end == NULL ? NULL : end + 1;
	}
	return false;
}

static void
load_visual_palette(struct home *home)
{
	static const GLfloat fallback[4][3] = {
		{1.000f, 0.992f, 0.969f},
		{0.863f, 0.910f, 0.937f},
		{0.800f, 0.898f, 0.941f},
		{0.482f, 0.369f, 0.482f},
	};
	static const GLfloat fallback_accent[3] = {
		0.435f, 0.141f, 0.812f,
	};
	static const GLfloat fallback_text[3] = {
		0.145f, 0.137f, 0.192f,
	};
	static const GLfloat fallback_line[3] = {
		0.753f, 0.765f, 0.812f,
	};
	static const char *keys[] = {
		"SHELL_PANEL",
		"SHELL_PANEL_ALT",
		"SHELL_ACCENT_SOFT",
		"SHELL_WARM",
	};
	char *path;
	char *contents = NULL;

	memcpy(home->visual_palette, fallback, sizeof(fallback));
	memcpy(home->accent_rgb, fallback_accent, sizeof(fallback_accent));
	memcpy(home->text_rgb, fallback_text, sizeof(fallback_text));
	memcpy(home->line_rgb, fallback_line, sizeof(fallback_line));
	path = ctlst_config_path("theme.env");
	if (g_file_get_contents(path, &contents, NULL, NULL)) {
		for (size_t i = 0; i < G_N_ELEMENTS(keys); i++)
			find_theme_color(contents, keys[i],
			    home->visual_palette[i]);
		find_theme_color(contents, "SHELL_ACCENT", home->accent_rgb);
		find_theme_color(contents, "SHELL_TEXT", home->text_rgb);
		find_theme_color(contents, "SHELL_LINE", home->line_rgb);
	}
	g_free(contents);
	g_free(path);
}

static bool
hardware_needs_post_map_settle(void)
{
	char *compatible = NULL;
	gsize length = 0;
	bool needs_settle = false;

	if (g_file_get_contents("/proc/device-tree/compatible", &compatible,
	    &length, NULL))
		needs_settle = g_strstr_len(compatible, (gssize)length,
		    "google,b4s4-sdm670") != NULL;
	g_free(compatible);
	return needs_settle;
}

static void
cancel_prompt_leave(struct home *home)
{
	if (home->prompt_leave_source == 0)
		return;
	g_source_remove(home->prompt_leave_source);
	home->prompt_leave_source = 0;
}

static void
mark_prompt_focused(struct home *home)
{
	close(open(home->focus_path,
	    O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600));
}

static void
cancel_prompt_reclaim(struct home *home)
{
	if (home->prompt_reclaim_source == 0)
		return;
	g_source_remove(home->prompt_reclaim_source);
	home->prompt_reclaim_source = 0;
}

static void
end_prompt_session(struct home *home, const char *keyboard_command)
{
	cancel_prompt_leave(home);
	cancel_prompt_reclaim(home);
	home->prompt_session = false;
	home->prompt_focus_pending = false;
	home->prompt_post_map = false;
	home->prompt_post_map_delay_ms = 0;
	home->prompt_settle_attempts = 0;
	unlink(home->focus_path);
	if (keyboard_command != NULL)
		keyboard_control(keyboard_command);
	if (home->window == NULL)
		return;
	gtk_window_set_focus(home->window, NULL);
	gtk_layer_set_keyboard_mode(home->window,
	    GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
}

static void
release_prompt_for_lock(struct home *home)
{
	/* Keep suppress semantics of hide during lock rather than blur. */
	end_prompt_session(home, "hide");
	home->prompt_lock_recovery = true;
}

static gboolean
prompt_leave_timeout(gpointer data)
{
	struct home *home = data;

	home->prompt_leave_source = 0;
	if (home->window == NULL || !home->visible || home_is_locked(home)) {
		end_prompt_session(home, "hide");
		return G_SOURCE_REMOVE;
	}
	/* Focus returned to the prompt during the grace window. */
	if (gtk_window_get_focus(home->window) == GTK_WIDGET(home->prompt) ||
	    gtk_widget_has_focus(GTK_WIDGET(home->prompt))) {
		home->prompt_session = true;
		mark_prompt_focused(home);
		return G_SOURCE_REMOVE;
	}
	GtkWidget *focus = gtk_window_get_focus(home->window);
	if (focus && home->agent && gtk_widget_is_ancestor(focus, home->agent)) {
		/* Moving to response/confirmation controls ends text input, not navigation. */
		end_prompt_session(home, "blur");
		gtk_layer_set_keyboard_mode(home->window, GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE);
		gtk_widget_grab_focus(focus);
		return G_SOURCE_REMOVE;
	}
	end_prompt_session(home, "blur");
	return G_SOURCE_REMOVE;
}

static GLuint
compile_shader(GLenum type, const char *source)
{
	GLuint shader = glCreateShader(type);
	GLint compiled = GL_FALSE;

	glShaderSource(shader, 1, &source, NULL);
	glCompileShader(shader);
	glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
	if (compiled == GL_FALSE) {
		glDeleteShader(shader);
		return 0;
	}
	return shader;
}

static void
visual_realize(GtkGLArea *area, gpointer data)
{
	static const GLfloat vertices[] = {
		-1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f,
		-1.0f, 1.0f, 1.0f, -1.0f, 1.0f, 1.0f,
	};
	static const char *vertex_source =
		"attribute vec2 position;"
		"varying vec2 uv;"
		"void main(){uv=position*.5+.5;gl_Position=vec4(position,0.,1.);}";
	static const char *fragment_source =
		"precision mediump float; varying vec2 uv; uniform float time;"
		"uniform float mode; uniform vec3 palette0; uniform vec3 palette1;"
		"uniform vec3 palette2; uniform vec3 palette3;"
		"float orb(vec2 p,vec2 c,float r){return smoothstep(r,r-.28,length(p-c));}"
		"void main(){vec2 p=uv; float t=time*.16; vec3 c;"
		"if(mode<.5){c=mix(palette0,palette1,.22+.18*p.y);"
		"c=mix(c,palette2,.36*orb(p,vec2(.82+.04*sin(t),.22),.42));"
		"c=mix(c,palette3,.15*orb(p,vec2(.12,.88+.03*cos(t)),.38));}"
		"else if(mode<1.5){c=mix(palette0,palette2,.34);"
		"float rings=.5+.5*sin(length(p-vec2(.78,.28))*38.-time*.8);"
		"c=mix(c,palette1,rings*.14);"
		"c=mix(c,palette3,.25*orb(p,vec2(.77+.04*sin(t),.30),.28));}"
		"else{c=mix(palette1,palette2,p.x);"
		"float wave=.5+.5*sin((p.x+p.y)*7.+time*.45);"
		"c=mix(c,palette3,wave*.12);"
		"c=mix(c,palette0,.30*orb(p,vec2(.84+.03*cos(t),.34),.30));}"
		/* Keep the horizontal antialiasing inside the GL texture boundary. */
		"vec2 q=abs(p-vec2(.5))-vec2(.43,.44);"
		"float d=length(max(q,vec2(0.)))+min(max(q.x,q.y),0.)-.06;"
		"float alpha=1.-smoothstep(-.0025,.0025,d);"
		"gl_FragColor=vec4(c*alpha,alpha);}";
	struct home *home = data;
	GLuint vertex;
	GLuint fragment;

	gtk_gl_area_make_current(area);
	if (gtk_gl_area_get_error(area) != NULL)
		return;
	vertex = compile_shader(GL_VERTEX_SHADER, vertex_source);
	fragment = compile_shader(GL_FRAGMENT_SHADER, fragment_source);
	if (vertex == 0 || fragment == 0)
		goto fail;
	home->visual_program = glCreateProgram();
	glAttachShader(home->visual_program, vertex);
	glAttachShader(home->visual_program, fragment);
	glBindAttribLocation(home->visual_program, 0, "position");
	glLinkProgram(home->visual_program);
	glDeleteShader(vertex);
	glDeleteShader(fragment);
	glGenBuffers(1, &home->visual_vbo);
	glBindBuffer(GL_ARRAY_BUFFER, home->visual_vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
	home->visual_time = glGetUniformLocation(home->visual_program, "time");
	home->visual_mode_uniform = glGetUniformLocation(home->visual_program,
	    "mode");
	for (size_t i = 0; i < G_N_ELEMENTS(home->visual_palette_uniforms); i++) {
		char name[16];

		g_snprintf(name, sizeof(name), "palette%zu", i);
		home->visual_palette_uniforms[i] = glGetUniformLocation(
		    home->visual_program, name);
	}
	home->visual_started = g_get_monotonic_time();
	return;
fail:
	if (vertex != 0)
		glDeleteShader(vertex);
	if (fragment != 0)
		glDeleteShader(fragment);
}

static void
visual_unrealize(GtkGLArea *area, gpointer data)
{
	struct home *home = data;

	gtk_gl_area_make_current(area);
	if (gtk_gl_area_get_error(area) == NULL) {
		if (home->visual_vbo != 0)
			glDeleteBuffers(1, &home->visual_vbo);
		if (home->visual_program != 0)
			glDeleteProgram(home->visual_program);
	}
	home->visual_vbo = 0;
	home->visual_program = 0;
}

static gboolean
visual_render(GtkGLArea *area, GdkGLContext *context, gpointer data)
{
	struct home *home = data;
	float elapsed;
	int scale;

	(void)context;
	if (home->visual_program == 0)
		return FALSE;
	elapsed = (float)(g_get_monotonic_time() - home->visual_started) / 1000000.0f;
	scale = gtk_widget_get_scale_factor(GTK_WIDGET(area));
	glViewport(0, 0, gtk_widget_get_width(GTK_WIDGET(area)) * scale,
	    gtk_widget_get_height(GTK_WIDGET(area)) * scale);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glUseProgram(home->visual_program);
	glUniform1f(home->visual_time, elapsed);
	glUniform1f(home->visual_mode_uniform, (float)home->visual_mode);
	for (size_t i = 0; i < G_N_ELEMENTS(home->visual_palette_uniforms); i++)
		glUniform3fv(home->visual_palette_uniforms[i], 1,
		    home->visual_palette[i]);
	glBindBuffer(GL_ARRAY_BUFFER, home->visual_vbo);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
	glDrawArrays(GL_TRIANGLES, 0, 6);
	glDisableVertexAttribArray(0);
	return TRUE;
}

static void
cycle_visual(GtkButton *button, gpointer data)
{
	static const char *labels[] = {"At a glance", "Signal", "Bloom"};
	struct home *home = data;
	char *directory = ctlst_config_path(NULL);
	char *path = g_build_filename(directory, "agent-visual", NULL);
	char value[8];

	home->visual_mode = (home->visual_mode + 1) % G_N_ELEMENTS(labels);
	gtk_button_set_label(button, labels[home->visual_mode]);
	g_snprintf(value, sizeof(value), "%u\n", home->visual_mode);
	g_mkdir_with_parents(directory, 0700);
	g_file_set_contents(path, value, -1, NULL);
	gtk_gl_area_queue_render(home->visual);
	g_free(path);
	g_free(directory);
}

static void
prompt_focus_enter(GtkEventControllerFocus *controller, gpointer data)
{
	struct home *home = data;

	(void)controller;
	if (home_is_locked(home)) {
		release_prompt_for_lock(home);
		return;
	}
	cancel_prompt_leave(home);
	home->prompt_session = true;
	mark_prompt_focused(home);
	/* Show only if hidden. Never remap or restart an already-visible keyboard. */
	if (access(home->keyboard_path, F_OK) != 0)
		keyboard_control("focus");
}

static void
prompt_focus_leave(GtkEventControllerFocus *controller, gpointer data)
{
	struct home *home = data;

	(void)controller;
	if (home_is_locked(home)) {
		release_prompt_for_lock(home);
		return;
	}
	/*
	 * The focus dance deliberately clears focus, and text commits can churn
	 * GTK focus for a frame. Immediate blur caused per-letter keyboard
	 * flicker; keep the session and marker until leave sticks.
	 */
	if (home->prompt_focus_pending || home->prompt_post_map)
		return;
	if (!home->prompt_session)
		return;
	if (home->prompt_leave_source != 0)
		return;
	home->prompt_leave_source = g_timeout_add(PROMPT_LEAVE_GRACE_MS,
	    prompt_leave_timeout, home);
}

static void
prompt_changed(GtkEditable *editable, gpointer data)
{
	struct home *home = data;
	const char *text = gtk_editable_get_text(editable);
	char count[32];
	int fd;
	int length;

	length = g_snprintf(count, sizeof(count), "%ld\n",
	    (long)g_utf8_strlen(text, -1));
	fd = open(home->prompt_length_path,
	    O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	if (fd >= 0) {
		size_t count_length = (size_t)length;
		ssize_t written = write(fd, count, count_length);

		if (written != (ssize_t)count_length)
			unlink(home->prompt_length_path);
		close(fd);
	}
}

static gboolean
ensure_prompt_keyboard(gpointer data)
{
	struct home *home = data;

	if (home->window == NULL || !home->visible)
		return G_SOURCE_REMOVE;
	if (home_is_locked(home)) {
		release_prompt_for_lock(home);
		return G_SOURCE_REMOVE;
	}
	if (access(home->keyboard_path, F_OK) == 0) {
		if (home->prompt_post_map) {
			guint delay = home->prompt_post_map_delay_ms;

			home->prompt_post_map = false;
			home->prompt_post_map_delay_ms = 0;
			home->prompt_focus_pending = true;
			/*
			 * Let the compositor commit the new keyboard layer before
			 * reclaiming text focus. Physical Pixel startup needs a longer
			 * settle than the headless VM.
			 */
			g_timeout_add(delay, focus_prompt, home);
		} else {
			home->prompt_focus_pending = false;
			home->prompt_session = true;
			mark_prompt_focused(home);
		}
		return G_SOURCE_REMOVE;
	}
	if (home->prompt_settle_attempts >= 3) {
		home->prompt_focus_pending = false;
		return G_SOURCE_REMOVE;
	}
	home->prompt_settle_attempts++;
	gtk_layer_set_keyboard_mode(home->window,
	    GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE);
	gtk_window_present(home->window);
	gtk_window_set_focus(home->window, GTK_WIDGET(home->prompt));
	gtk_widget_grab_focus(GTK_WIDGET(home->prompt));
	mark_prompt_focused(home);
	keyboard_control("focus");
	g_timeout_add(120, ensure_prompt_keyboard, home);
	return G_SOURCE_REMOVE;
}

static gboolean
commit_prompt_focus(gpointer data)
{
	struct home *home = data;

	if (home->window == NULL || !home->visible)
		return G_SOURCE_REMOVE;
	if (home_is_locked(home)) {
		release_prompt_for_lock(home);
		return G_SOURCE_REMOVE;
	}
	home->prompt_session = true;
	gtk_layer_set_keyboard_mode(home->window,
	    GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE);
	gtk_window_present(home->window);
	gtk_window_set_focus(home->window, GTK_WIDGET(home->prompt));
	gtk_widget_grab_focus(GTK_WIDGET(home->prompt));
	gtk_editable_select_region(GTK_EDITABLE(home->prompt), -1, -1);
	gtk_editable_set_position(GTK_EDITABLE(home->prompt), -1);
	mark_prompt_focused(home);
	g_timeout_add(120, ensure_prompt_keyboard, home);
	return G_SOURCE_REMOVE;
}

static gboolean
focus_prompt(gpointer data)
{
	struct home *home = data;

	if (home->window == NULL || !home->visible)
		return G_SOURCE_REMOVE;
	if (home_is_locked(home)) {
		release_prompt_for_lock(home);
		return G_SOURCE_REMOVE;
	}
	home->prompt_focus_pending = true;
	gtk_window_set_focus(home->window, NULL);
	gtk_layer_set_keyboard_mode(home->window,
	    GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE);
	gtk_window_present(home->window);
	g_timeout_add(35, commit_prompt_focus, home);
	return G_SOURCE_REMOVE;
}

static gboolean
reclaim_prompt_focus(gpointer data)
{
	struct home *home = data;

	home->prompt_reclaim_source = 0;
	return focus_prompt(home);
}

static void
schedule_prompt_reclaim(struct home *home, guint delay_ms)
{
	if (home->window == NULL || !home->visible || home_is_locked(home))
		return;
	cancel_prompt_leave(home);
	cancel_prompt_reclaim(home);
	home->prompt_session = true;
	home->prompt_focus_pending = true;
	home->prompt_post_map = false;
	home->prompt_post_map_delay_ms = 0;
	mark_prompt_focused(home);

	/*
	 * Re-applying EXCLUSIVE is a no-op once wvkbd owns the seat. Commit NONE
	 * for one main-loop turn, then reclaim below. Keeping wvkbd mapped avoids
	 * the resize/flicker caused by hide/show while restoring key delivery.
	 */
	gtk_window_set_focus(home->window, NULL);
	gtk_layer_set_keyboard_mode(home->window,
	    GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
	home->prompt_reclaim_source = g_timeout_add(delay_ms,
	    reclaim_prompt_focus, home);
}

static void
soft_reclaim_prompt(struct home *home)
{
	schedule_prompt_reclaim(home, PROMPT_RECLAIM_DELAY_MS);
}

static gboolean
finish_prompt_lock_remap(gpointer data)
{
	struct home *home = data;

	if (home->window == NULL || !home->visible)
		return G_SOURCE_REMOVE;
	if (home_is_locked(home)) {
		release_prompt_for_lock(home);
		return G_SOURCE_REMOVE;
	}
	gtk_layer_set_keyboard_mode(home->window,
	    GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE);
	gtk_widget_set_visible(GTK_WIDGET(home->window), TRUE);
	gtk_window_present(home->window);
	/*
	 * Showing in a later main-loop turn gives Wayland a real unmap/remap
	 * boundary after the lock surface releases keyboard exclusivity. Set
	 * EXCLUSIVE before mapping so the first layer-surface commit owns the
	 * seat rather than relying on a later interactivity update.
	 */
	g_timeout_add(80, commit_prompt_focus, home);
	return G_SOURCE_REMOVE;
}

static void
request_prompt_focus(struct home *home)
{
	bool keyboard_visible;
	bool lock_recovery;

	if (home_is_locked(home))
		return;
	cancel_prompt_leave(home);
	keyboard_visible = access(home->keyboard_path, F_OK) == 0;

	/*
	 * Active or keyboard-already-up sessions only soft-reclaim seat focus.
	 * The old "both markers exist → return" path left typing dead when GTK
	 * reported focus but the seat did not. Soft reclaim repairs that without
	 * the clear-focus / remap dance that flickered the keyboard per letter.
	 */
	if (keyboard_visible) {
		soft_reclaim_prompt(home);
		return;
	}

	cancel_prompt_reclaim(home);
	lock_recovery = home->prompt_lock_recovery;
	if (lock_recovery) {
		gtk_window_set_focus(home->window, NULL);
		gtk_layer_set_keyboard_mode(home->window,
		    GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
		gtk_widget_set_visible(GTK_WIDGET(home->window), FALSE);
	}
	home->prompt_session = true;
	home->prompt_focus_pending = true;
	home->prompt_settle_attempts = 0;
	home->prompt_post_map = hardware_needs_post_map_settle();
	home->prompt_post_map_delay_ms = 260;
	home->prompt_lock_recovery = false;
	if (lock_recovery)
		g_timeout_add(80, finish_prompt_lock_remap, home);
	else
		g_timeout_add(40, focus_prompt, home);
}

static gboolean
prompt_event(GtkEventControllerLegacy *controller, GdkEvent *event,
    gpointer data)
{
	struct home *home = data;
	GdkEventType type = gdk_event_get_event_type(event);

	(void)controller;
	if (type == GDK_TOUCH_END || type == GDK_BUTTON_RELEASE)
		request_prompt_focus(home);
	return GDK_EVENT_PROPAGATE;
}

static void
reload_theme(struct home *home)
{
	char *path = ctlst_config_path("theme.css");

	load_visual_palette(home);
	gtk_css_provider_load_from_path(home->theme_provider, path);
	if (home->visual != NULL)
		gtk_gl_area_queue_render(home->visual);
	if (home->analog_clock != NULL)
		gtk_widget_queue_draw(GTK_WIDGET(home->analog_clock));
	g_free(path);
}

static void
reload_home_config(struct home *home)
{
	int old_cols = home->config.grid_columns;
	int old_rows = home->config.grid_rows;

	home_config_set_defaults(&home->config);
	home_config_load_user(&home->config);
	if (home->edit_frame != NULL)
		home_edit_frame_configure(home->edit_frame, home->layout_editing,
		    home->config.edit_inset_px, home->config.edit_controls_gap_px);
	/* Descriptor/runtime membership is fixed for this Home process. Restart
	 * Home after installing or enabling an external widget; keeping descriptor
	 * storage stable avoids invalidating active runtime pointers on reload. */
	if (home->context_grid != NULL) {
		gtk_grid_set_column_spacing(home->context_grid, home_spacing(home));
		gtk_grid_set_row_spacing(home->context_grid, home_spacing(home));
	}
	if (home->strip_grid != NULL) {
		gtk_grid_set_column_spacing(home->strip_grid, home_spacing(home));
		gtk_grid_set_row_spacing(home->strip_grid, home_spacing(home));
	}
	(void)old_cols;
	(void)old_rows;
	render_launcher_page(home);
	schedule_launcher_icon_layout(home);
}

static void
theme_css_changed(GFileMonitor *monitor, GFile *file, GFile *other,
    GFileMonitorEvent event, gpointer data)
{
	char *file_name = file != NULL ? g_file_get_basename(file) : NULL;
	char *other_name = other != NULL ? g_file_get_basename(other) : NULL;
	bool is_theme = g_strcmp0(file_name, "theme.css") == 0 ||
	    g_strcmp0(other_name, "theme.css") == 0;

	(void)monitor;
	if (is_theme && (event == G_FILE_MONITOR_EVENT_CHANGED ||
	    event == G_FILE_MONITOR_EVENT_CREATED ||
	    event == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT ||
	    event == G_FILE_MONITOR_EVENT_MOVED_IN ||
	    event == G_FILE_MONITOR_EVENT_RENAMED))
		reload_theme(data);
	g_free(file_name);
	g_free(other_name);
}

static void
theme_changed(GFileMonitor *monitor, GFile *file, GFile *other,
    GFileMonitorEvent event, gpointer data)
{
	theme_css_changed(monitor, file, other, event, data);
}

static void
setup_theme(struct home *home)
{
	GtkCssProvider *base = gtk_css_provider_new();
	char *path = g_build_filename(g_get_home_dir(), ".config", "sway-touch",
	    NULL);
	GFile *file = g_file_new_for_path(path);

	gtk_css_provider_load_from_string(base, style_css);
	gtk_style_context_add_provider_for_display(gdk_display_get_default(),
	    GTK_STYLE_PROVIDER(base), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref(base);
	home->theme_provider = gtk_css_provider_new();
	reload_theme(home);
	gtk_style_context_add_provider_for_display(gdk_display_get_default(),
	    GTK_STYLE_PROVIDER(home->theme_provider),
	    GTK_STYLE_PROVIDER_PRIORITY_USER);
	home->theme_monitor = g_file_monitor_directory(file,
	    G_FILE_MONITOR_NONE, NULL, NULL);
	if (home->theme_monitor != NULL)
		g_signal_connect(home->theme_monitor, "changed",
		    G_CALLBACK(theme_changed), home);
	g_object_unref(file);
	g_free(path);
}

static char *
home_layout_path(void)
{
	return g_build_filename(g_get_user_config_dir(), "ctlst", "home-layout.json",
	    NULL);
}

static void save_home_layout(struct home *home);
static bool find_available_grid_slot(struct home *home,
    struct home_grid_item *item, int column, int row, int column_span,
    int row_span, int *column_out, int *row_out);

static int
home_grid_min_columns(const struct home_grid_item *item)
{
	if (item->descriptor != NULL)
		return MAX(item->descriptor->min_columns, 1);
	return g_str_equal(item->id, "weather") ? 2 : 1;
}

static int
home_grid_min_rows(const struct home_grid_item *item)
{
	if (item->descriptor != NULL)
		return MAX(item->descriptor->min_rows, 1);
	return 1;
}

static bool
grid_item_fits(struct home *home, struct home_grid_item *item, int column,
    int row, int column_span, int row_span)
{
	int right = column + column_span;
	int bottom = row + row_span;
	int cluster = home_cluster_rows(home);

	if (column < 0 || row < 0 ||
	    column_span < home_grid_min_columns(item) ||
	    row_span < home_grid_min_rows(item) ||
	    right > home_cols(home) || bottom > cluster)
		return false;
	if (item != NULL && item->hidden)
		return true;
	for (size_t i = 0; i < home->grid_item_count; i++) {
		struct home_grid_item *other = &home->grid_items[i];
		int other_right;
		int other_bottom;

		if (other == item || other->container == NULL || other->hidden ||
		    (item != NULL && other->page != item->page))
			continue;
		other_right = other->column + other->column_span;
		other_bottom = other->row + other->row_span;
		if (column < other_right && right > other->column &&
		    row < other_bottom && bottom > other->row)
			return false;
	}
	if (grid_rect_overlaps_launcher_icon(home, item != NULL ? item->page : 0,
	    column, row, column_span,
	    row_span, NULL))
		return false;
	return true;
}

static bool
grid_rects_overlap(int column_a, int row_a, int column_span_a, int row_span_a,
    int column_b, int row_b, int column_span_b, int row_span_b)
{
	return column_a < column_b + column_span_b &&
	    column_a + column_span_a > column_b &&
	    row_a < row_b + row_span_b && row_a + row_span_a > row_b;
}

static bool
grid_rect_overlaps_launcher_icon(struct home *home, int page, int column,
    int row, int column_span, int row_span,
    const struct home_launcher_icon *skip)
{
	if (home->launcher_icons == NULL)
		return false;
	for (size_t i = 0; i < home->launcher_icons->len; i++) {
		struct home_launcher_icon *icon =
		    g_ptr_array_index(home->launcher_icons, i);

		if (icon == skip || icon->desktop_id == NULL)
			continue;
		if (icon->page != page)
			continue;
		if (grid_rects_overlap(column, row, column_span, row_span,
		    icon->column, icon->row, 1, 1))
			return true;
	}
	return false;
}

static bool
grid_item_in_bounds(const struct home_grid_item *item)
{
	struct home *home = item->home;

	if (home == NULL)
		return false;
	return item->column >= 0 && item->row >= 0 &&
	    item->column_span >= home_grid_min_columns(item) &&
	    item->row_span >= home_grid_min_rows(item) &&
	    item->column + item->column_span <= home_cols(home) &&
	    item->row + item->row_span <= home_rows(home);
}

static bool
grid_items_overlap(const struct home_grid_item *a,
    const struct home_grid_item *b)
{
	return a->page == b->page &&
	    grid_rects_overlap(a->column, a->row, a->column_span, a->row_span,
	    b->column, b->row, b->column_span, b->row_span);
}

static bool
layout_is_valid(struct home *home)
{
	for (size_t i = 0; i < home->grid_item_count; i++) {
		struct home_grid_item *a = &home->grid_items[i];

		if (a->container == NULL || a->hidden)
			continue;
		if (!grid_item_in_bounds(a))
			return false;
		for (size_t j = i + 1; j < home->grid_item_count; j++) {
			struct home_grid_item *b = &home->grid_items[j];

			if (b->container == NULL || b->hidden)
				continue;
			if (grid_items_overlap(a, b))
				return false;
		}
		if (grid_rect_overlaps_launcher_icon(home, a->page, a->column, a->row,
		    a->column_span, a->row_span, NULL))
			return false;
	}
	return true;
}

struct grid_layout_snapshot {
	int column[HOME_GRID_WIDGET_MAX];
	int row[HOME_GRID_WIDGET_MAX];
	int column_span[HOME_GRID_WIDGET_MAX];
	int row_span[HOME_GRID_WIDGET_MAX];
};

struct home_icon_layout_snap {
	int column;
	int row;
	int page;
};

struct home_drag_snapshot {
	struct grid_layout_snapshot grid;
	struct home_icon_layout_snap *icons;
	size_t icon_count;
};

static void
save_grid_layout(struct home *home, struct grid_layout_snapshot *snap)
{
	for (size_t i = 0; i < home->grid_item_count; i++) {
		struct home_grid_item *item = &home->grid_items[i];

		snap->column[i] = item->column;
		snap->row[i] = item->row;
		snap->column_span[i] = item->column_span;
		snap->row_span[i] = item->row_span;
	}
}

static void
restore_grid_layout(struct home *home, const struct grid_layout_snapshot *snap)
{
	for (size_t i = 0; i < home->grid_item_count; i++) {
		struct home_grid_item *item = &home->grid_items[i];

		item->column = snap->column[i];
		item->row = snap->row[i];
		item->column_span = snap->column_span[i];
		item->row_span = snap->row_span[i];
	}
}

static void
clear_drag_layout_snapshot(struct home *home)
{
	if (home->drag_snapshot == NULL)
		return;
	g_free(home->drag_snapshot->icons);
	g_free(home->drag_snapshot);
	home->drag_snapshot = NULL;
}

static void
save_drag_layout_snapshot(struct home *home)
{
	size_t icon_count = 0;

	clear_drag_layout_snapshot(home);
	home->drag_snapshot = g_new0(struct home_drag_snapshot, 1);
	save_grid_layout(home, &home->drag_snapshot->grid);
	if (home->launcher_icons == NULL)
		return;
	icon_count = home->launcher_icons->len;
	if (icon_count == 0)
		return;
	home->drag_snapshot->icon_count = icon_count;
	home->drag_snapshot->icons = g_new(struct home_icon_layout_snap, icon_count);
	for (size_t i = 0; i < icon_count; i++) {
		struct home_launcher_icon *icon =
		    g_ptr_array_index(home->launcher_icons, i);

		home->drag_snapshot->icons[i].column = icon->column;
		home->drag_snapshot->icons[i].row = icon->row;
		home->drag_snapshot->icons[i].page = icon->page;
	}
}

static void
restore_drag_layout_data(struct home *home)
{
	struct home_drag_snapshot *snap = home->drag_snapshot;

	if (snap == NULL)
		return;
	restore_grid_layout(home, &snap->grid);
	if (snap->icons != NULL && home->launcher_icons != NULL) {
		size_t count = MIN(snap->icon_count, home->launcher_icons->len);

		for (size_t i = 0; i < count; i++) {
			struct home_launcher_icon *icon =
			    g_ptr_array_index(home->launcher_icons, i);

			if (icon == NULL)
				continue;
			icon->column = snap->icons[i].column;
			icon->row = snap->icons[i].row;
			icon->page = snap->icons[i].page;
		}
	}
}

static void
restore_drag_layout_snapshot(struct home *home)
{
	restore_drag_layout_data(home);
	apply_home_grid(home);
	/* Icon attach only — full pager relayout is too heavy/crashy inside
	 * drag motion handlers. */
	relayout_launcher_icons(home);
}

static void
reset_drag_preview(struct home *home)
{
	home->drag_preview_column = -1;
	home->drag_preview_row = -1;
	home->drag_preview_column_span = -1;
	home->drag_preview_row_span = -1;
}

static int
grid_index_with_hysteresis(double offset, double step, int start_index,
    int current_index, int min_index, int max_index, double margin)
{
	int n;
	double f;

	if (step <= 0.0)
		return CLAMP(start_index, min_index, max_index);
	if (current_index < min_index || current_index > max_index)
		current_index = start_index;
	n = current_index - start_index;
	f = offset / step;
	while (n < max_index - start_index && f >= n + 0.5 + margin)
		n++;
	while (n > min_index - start_index && f <= n - 0.5 - margin)
		n--;
	return CLAMP(start_index + n, min_index, max_index);
}

/*
 * Make room at the requested origin by pushing obstructing widgets down one row
 * at a time, cascading until the layout is collision-free or the grid overflows.
 */
static bool
push_obstructing_widgets_down(struct home *home, struct home_grid_item *item,
    int column, int row, int column_span, int row_span)
{
	struct grid_layout_snapshot snap;
	const int max_passes = home_rows(home) * (int)home->grid_item_count;

	save_grid_layout(home, &snap);
	item->column = column;
	item->row = row;
	item->column_span = column_span;
	item->row_span = row_span;
	if (!grid_item_in_bounds(item)) {
		restore_grid_layout(home, &snap);
		return false;
	}
	if (grid_item_fits(home, item, column, row, column_span, row_span))
		return true;

	for (int pass = 0; pass < max_passes; pass++) {
		bool moved = false;

		for (size_t i = 0; i < home->grid_item_count; i++) {
			struct home_grid_item *other = &home->grid_items[i];

			if (other == item || other->container == NULL)
				continue;
			if (!grid_rects_overlap(column, row, column_span, row_span,
			    other->column, other->row, other->column_span,
			    other->row_span))
				continue;
			if (other->row + other->row_span >= home_rows(home)) {
				restore_grid_layout(home, &snap);
				return false;
			}
			other->row += 1;
			moved = true;
		}
		if (layout_is_valid(home))
			return true;
		if (!moved) {
			for (size_t i = 0; i < home->grid_item_count; i++) {
				struct home_grid_item *a = &home->grid_items[i];

				if (a->container == NULL)
					continue;
				for (size_t j = i + 1; j < home->grid_item_count; j++) {
					struct home_grid_item *b =
					    &home->grid_items[j];
					struct home_grid_item *push = NULL;

					if (b->container == NULL)
						continue;
					if (!grid_items_overlap(a, b))
						continue;
					if (a != item && b != item) {
						push = b->row > a->row ||
						    (b->row == a->row &&
						    b->column > a->column) ? b : a;
					} else if (a != item) {
						push = a;
					} else {
						push = b;
					}
					if (push->row + push->row_span >=
					    home_rows(home)) {
						restore_grid_layout(home, &snap);
						return false;
					}
					push->row += 1;
					moved = true;
					break;
				}
				if (moved)
					break;
			}
		}
		if (!moved) {
			restore_grid_layout(home, &snap);
			return false;
		}
	}
	restore_grid_layout(home, &snap);
	return false;
}

static void
cancel_push_dwell(struct home *home)
{
	if (home->push_dwell_source != 0) {
		g_source_remove(home->push_dwell_source);
		home->push_dwell_source = 0;
	}
	home->push_dwell_item = NULL;
	home->push_dwell_ready = false;
}

static gboolean
push_dwell_fire(gpointer data)
{
	struct home *home = data;
	struct home_grid_item *item = home->push_dwell_item;

	home->push_dwell_source = 0;
	home->push_dwell_ready = true;
	/* Re-apply placement if the finger is still over the blocked slot. */
	if (item != NULL && home->active_drag_item == item &&
	    !home->resize_drag) {
		restore_drag_layout_snapshot(home);
		try_place_widget(home, item, home->push_dwell_column,
		    home->push_dwell_row, home->push_dwell_column_span,
		    home->push_dwell_row_span, true);
	}
	return G_SOURCE_REMOVE;
}

static void
schedule_push_dwell(struct home *home, struct home_grid_item *item,
    int column, int row, int column_span, int row_span)
{
	if (home->push_dwell_item == item && home->push_dwell_column == column &&
	    home->push_dwell_row == row &&
	    home->push_dwell_column_span == column_span &&
	    home->push_dwell_row_span == row_span && home->push_dwell_source != 0)
		return;
	cancel_push_dwell(home);
	home->push_dwell_item = item;
	home->push_dwell_column = column;
	home->push_dwell_row = row;
	home->push_dwell_column_span = column_span;
	home->push_dwell_row_span = row_span;
	home->push_dwell_source = g_timeout_add(home->config.drag_push_dwell_ms,
	    push_dwell_fire, home);
}

static void
apply_widget_placement(struct home *home, struct home_grid_item *item,
    int column, int row, int column_span, int row_span)
{
	item->column = column;
	item->row = row;
	item->column_span = column_span;
	item->row_span = row_span;
	apply_home_grid(home);
	/* Widgets claim cells; shove icons to the next free slot down/up. */
	evacuate_launcher_icons_under_widgets(home);
	refresh_grid_editing(home);
}

static bool
try_place_widget(struct home *home, struct home_grid_item *item,
    int column, int row, int column_span, int row_span, bool allow_push)
{
	int resolved_column;
	int resolved_row;

	if (grid_item_fits(home, item, column, row, column_span, row_span)) {
		cancel_push_dwell(home);
		apply_widget_placement(home, item, column, row, column_span,
		    row_span);
		return true;
	}
	if (allow_push && home->push_dwell_ready &&
	    home->push_dwell_item == item &&
	    home->push_dwell_column == column && home->push_dwell_row == row &&
	    home->push_dwell_column_span == column_span &&
	    home->push_dwell_row_span == row_span &&
	    push_obstructing_widgets_down(home, item, column, row, column_span,
	    row_span)) {
		cancel_push_dwell(home);
		apply_home_grid(home);
		evacuate_launcher_icons_under_widgets(home);
		refresh_grid_editing(home);
		return true;
	}
	if (!allow_push)
		return false;
	schedule_push_dwell(home, item, column, row, column_span, row_span);
	if (find_available_grid_slot(home, item, column, row, column_span,
	    row_span, &resolved_column, &resolved_row)) {
		apply_widget_placement(home, item, resolved_column, resolved_row,
		    column_span, row_span);
		return true;
	}
	return false;
}

static bool
find_available_grid_slot(struct home *home, struct home_grid_item *item,
    int desired_column, int desired_row, int column_span, int row_span,
    int *column_out, int *row_out)
{
	int best_column = -1;
	int best_row = -1;
	int best_distance = home_cols(home) + home_rows(home) + 1;

	desired_column = CLAMP(desired_column, 0,
	    home_cols(home) - column_span);
	desired_row = CLAMP(desired_row, 0, home_rows(home) - row_span);
	if (grid_item_fits(home, item, desired_column, desired_row,
	    column_span, row_span)) {
		*column_out = desired_column;
		*row_out = desired_row;
		return true;
	}

	/* Keep a moved widget in its column when possible, like a phone launcher. */
	for (int distance = 1; distance < home_rows(home); distance++) {
		int rows[] = { desired_row - distance, desired_row + distance };

		for (size_t i = 0; i < G_N_ELEMENTS(rows); i++) {
			if (rows[i] < 0 || rows[i] > home_rows(home) - row_span)
				continue;
			if (grid_item_fits(home, item, desired_column, rows[i],
			    column_span, row_span)) {
				*column_out = desired_column;
				*row_out = rows[i];
				return true;
			}
		}
	}

	/* Then try neighboring columns before the full nearest-slot search. */
	for (int distance = 1; distance < home_cols(home); distance++) {
		int columns[] = { desired_column - distance,
		    desired_column + distance };

		for (size_t i = 0; i < G_N_ELEMENTS(columns); i++) {
			if (columns[i] < 0 || columns[i] > home_cols(home) - column_span)
				continue;
			if (grid_item_fits(home, item, columns[i], desired_row,
			    column_span, row_span)) {
				*column_out = columns[i];
				*row_out = desired_row;
				return true;
			}
		}
	}

	for (int row = 0; row <= home_rows(home) - row_span; row++) {
		for (int column = 0; column <= home_cols(home) - column_span;
		    column++) {
			int distance = abs(column - desired_column) +
			    abs(row - desired_row);

			if (distance >= best_distance ||
			    !grid_item_fits(home, item, column, row, column_span,
			    row_span))
				continue;
			best_column = column;
			best_row = row;
			best_distance = distance;
		}
	}
	if (best_column < 0)
		return false;
	*column_out = best_column;
	*row_out = best_row;
	return true;
}

static void
reconcile_widgets_with_launcher_icons(struct home *home)
{
	for (size_t i = 0; i < home->grid_item_count; i++) {
		struct home_grid_item *item = &home->grid_items[i];
		int column;
		int row;

		if (item->container == NULL)
			continue;
		if (grid_item_fits(home, item, item->column, item->row,
		    item->column_span, item->row_span))
			continue;
		if (!find_available_grid_slot(home, item, item->column, item->row,
		    item->column_span, item->row_span, &column, &row)) {
			if (item->descriptor != NULL) {
				g_warning("home widget %s has no free cluster slot; hiding",
				    item->id);
				item->hidden = true;
				home_widget_runtime_set_visible(item->runtime, false);
			}
			continue;
		}
		apply_widget_placement(home, item, column, row, item->column_span,
		    item->row_span);
	}
}

static void
add_favorite_icon_paths(GtkIconTheme *theme)
{
	static bool configured;
	char *user_path;

	if (configured)
		return;
	configured = true;
	user_path = g_build_filename(g_get_home_dir(), ".local", "share",
	    "flatpak", "exports", "share", "icons", NULL);
	if (g_file_test(user_path, G_FILE_TEST_IS_DIR))
		gtk_icon_theme_add_search_path(theme, user_path);
	g_free(user_path);
	if (g_file_test("/var/lib/flatpak/exports/share/icons",
	    G_FILE_TEST_IS_DIR))
		gtk_icon_theme_add_search_path(theme,
		    "/var/lib/flatpak/exports/share/icons");
}

static GtkIconPaintable *
favorite_icon_paintable(GIcon *icon, int size)
{
	GtkIconTheme *theme;

	if (icon == NULL)
		return NULL;
	theme = gtk_icon_theme_get_for_display(gdk_display_get_default());
	add_favorite_icon_paths(theme);
	return gtk_icon_theme_lookup_by_gicon(theme, icon, size, 1,
	    GTK_TEXT_DIR_NONE, GTK_ICON_LOOKUP_FORCE_REGULAR);
}

static GtkWidget *
favorite_icon_image(GIcon *icon, int pixel_size)
{
	GtkIconPaintable *paintable;
	GtkWidget *image;

	paintable = favorite_icon_paintable(icon, pixel_size);
	image = gtk_image_new_from_paintable(GDK_PAINTABLE(paintable));
	gtk_image_set_pixel_size(GTK_IMAGE(image), pixel_size);
	g_clear_object(&paintable);
	return image;
}

static void
layout_launcher_icon_button(GtkWidget *button, int icon_px)
{
	GtkWidget *icon_host;
	GtkWidget *disc;
	GtkWidget *icon;
	int prev_px;
	int glyph_px;

	if (button == NULL || !GTK_IS_BUTTON(button))
		return;
	prev_px = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button),
	    "launcher-icon-px"));
	if (prev_px == icon_px)
		return;
	g_object_set_data(G_OBJECT(button), "launcher-icon-px",
	    GINT_TO_POINTER(icon_px));
	gtk_widget_set_hexpand(button, TRUE);
	gtk_widget_set_vexpand(button, TRUE);
	gtk_widget_set_halign(button, GTK_ALIGN_FILL);
	gtk_widget_set_valign(button, GTK_ALIGN_FILL);
	icon_host = gtk_button_get_child(GTK_BUTTON(button));
	if (icon_host == NULL || !G_TYPE_CHECK_INSTANCE_TYPE(icon_host, home_launcher_host_get_type()))
		return;
	disc = ((HomeLauncherHost *)icon_host)->disc;
	if (disc == NULL)
		return;
	/* Match app drawer: a 44px glyph in a 56px touch host. */
	glyph_px = MIN(icon_px, MAX(16, (icon_px * 44) / 56));
	gtk_widget_set_size_request(disc, icon_px, icon_px);
	gtk_widget_set_halign(disc, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(disc, GTK_ALIGN_CENTER);
	icon = gtk_widget_get_first_child(disc);
	if (icon == NULL || !GTK_IS_IMAGE(icon))
		return;
	{
		GIcon *app_icon = g_object_get_data(G_OBJECT(button), "favorite-icon");
		GtkIconPaintable *paintable;

		if (app_icon != NULL) {
			paintable = favorite_icon_paintable(app_icon, glyph_px);
			if (paintable != NULL) {
				gtk_image_set_from_paintable(GTK_IMAGE(icon),
				    GDK_PAINTABLE(paintable));
				g_object_unref(paintable);
			}
		}
		gtk_image_set_pixel_size(GTK_IMAGE(icon), glyph_px);
	}
}

static void
launcher_unparent_button(GtkWidget *button)
{
	GtkWidget *parent;

	if (button == NULL || !GTK_IS_WIDGET(button))
		return;
	parent = gtk_widget_get_parent(button);
	if (parent == NULL)
		return;
	/*
	 * Hold a ref across unparent. Callers that keep the widget (icon
	 * table, grid items) already own a durable ref; this guards the
	 * window where the parent was the last floating owner.
	 */
	g_object_ref(button);
	gtk_widget_unparent(button);
	g_object_unref(button);
}

static void
launcher_icon_free(gpointer data)
{
	struct home_launcher_icon *icon = data;

	if (icon == NULL)
		return;
	launcher_unparent_button(icon->button);
	g_free(icon->desktop_id);
	g_free(icon);
}

static bool
launcher_cell_blocked_by_widget(struct home *home, int page, int column,
    int row)
{
	for (size_t i = 0; i < home->grid_item_count; i++) {
		struct home_grid_item *item = &home->grid_items[i];

		if (item->container == NULL || item->hidden || item->page != page)
			continue;
		if (grid_rects_overlap(column, row, 1, 1, item->column,
		    item->row, item->column_span, item->row_span))
			return true;
	}
	return false;
}

static bool
launcher_cell_occupied(struct home *home, int page, int column, int row,
    const struct home_launcher_icon *skip)
{
	if (launcher_cell_blocked_by_widget(home, page, column, row))
		return true;
	if (home->launcher_icons == NULL)
		return false;
	for (size_t i = 0; i < home->launcher_icons->len; i++) {
		struct home_launcher_icon *icon =
		    g_ptr_array_index(home->launcher_icons, i);

		if (icon == skip || icon->page != page)
			continue;
		if (icon->column == column && icon->row == row)
			return true;
	}
	return false;
}

static bool
launcher_cell_occupied_except(struct home *home, int page, int column, int row,
    const struct home_launcher_icon *skip_a,
    const struct home_launcher_icon *skip_b)
{
	if (launcher_cell_blocked_by_widget(home, page, column, row))
		return true;
	if (home->launcher_icons == NULL)
		return false;
	for (size_t i = 0; i < home->launcher_icons->len; i++) {
		struct home_launcher_icon *icon =
		    g_ptr_array_index(home->launcher_icons, i);

		if (icon == skip_a || icon == skip_b || icon->page != page)
			continue;
		if (icon->column == column && icon->row == row)
			return true;
	}
	return false;
}

static struct home_launcher_icon *
launcher_icon_at_cell(struct home *home, int page, int column, int row,
    const struct home_launcher_icon *skip)
{
	if (home->launcher_icons == NULL)
		return NULL;
	for (size_t i = 0; i < home->launcher_icons->len; i++) {
		struct home_launcher_icon *icon =
		    g_ptr_array_index(home->launcher_icons, i);

		if (icon == skip || icon->page != page)
			continue;
		if (icon->column == column && icon->row == row)
			return icon;
	}
	return NULL;
}

/*
 * Next free cell for a displaced icon. Prefer same column down, then other
 * columns on lower rows, then upward — so a push "flows" down the home grid.
 */
static bool
launcher_find_push_cell(struct home *home, int page, int prefer_column,
    int prefer_row, const struct home_launcher_icon *skip_a,
    const struct home_launcher_icon *skip_b, int *column_out, int *row_out)
{
	prefer_column = CLAMP(prefer_column, 0, home_cols(home) - 1);
	prefer_row = CLAMP(prefer_row, 0, home_rows(home) - 1);

	for (int row = prefer_row + 1; row < home_rows(home); row++) {
		if (!launcher_cell_occupied_except(home, page, prefer_column, row,
		    skip_a, skip_b)) {
			*column_out = prefer_column;
			*row_out = row;
			return true;
		}
		for (int column = 0; column < home_cols(home); column++) {
			if (column == prefer_column)
				continue;
			if (!launcher_cell_occupied_except(home, page, column, row,
			    skip_a, skip_b)) {
				*column_out = column;
				*row_out = row;
				return true;
			}
		}
	}
	for (int row = prefer_row - 1; row >= 0; row--) {
		if (!launcher_cell_occupied_except(home, page, prefer_column, row,
		    skip_a, skip_b)) {
			*column_out = prefer_column;
			*row_out = row;
			return true;
		}
		for (int column = 0; column < home_cols(home); column++) {
			if (column == prefer_column)
				continue;
			if (!launcher_cell_occupied_except(home, page, column, row,
			    skip_a, skip_b)) {
				*column_out = column;
				*row_out = row;
				return true;
			}
		}
	}
	/* Same row left/right as last resort. */
	for (int column = 0; column < home_cols(home); column++) {
		if (column == prefer_column)
			continue;
		if (!launcher_cell_occupied_except(home, page, column, prefer_row,
		    skip_a, skip_b)) {
			*column_out = column;
			*row_out = prefer_row;
			return true;
		}
	}
	return false;
}

static bool
launcher_find_free_cell(struct home *home, int page, int *column_out,
    int *row_out)
{
	for (int row = 0; row < home_rows(home); row++) {
		for (int column = 0; column < home_cols(home); column++) {
			if (!launcher_cell_occupied(home, page, column, row, NULL)) {
				*column_out = column;
				*row_out = row;
				return true;
			}
		}
	}
	return false;
}

/* Move icon to free cell preferring down then up from its current seat. */
static bool
launcher_push_icon_aside(struct home *home, struct home_launcher_icon *icon,
    const struct home_launcher_icon *also_skip)
{
	int column;
	int row;

	if (icon == NULL)
		return false;
	if (!launcher_find_push_cell(home, icon->page, icon->column, icon->row,
	    icon, also_skip, &column, &row))
		return false;
	icon->column = column;
	icon->row = row;
	return true;
}

static void
evacuate_launcher_icons_under_widgets(struct home *home)
{
	bool moved = false;

	if (home->launcher_icons == NULL)
		return;
	/* Multiple passes: cascading widget coverage after each push. */
	for (int pass = 0; pass < home_rows(home) * home_cols(home); pass++) {
		bool pass_moved = false;

		for (size_t i = 0; i < home->launcher_icons->len; i++) {
			struct home_launcher_icon *icon =
			    g_ptr_array_index(home->launcher_icons, i);

			if (icon == NULL)
				continue;
			if (!launcher_cell_blocked_by_widget(home, icon->page, icon->column,
			    icon->row))
				continue;
			if (launcher_push_icon_aside(home, icon, NULL)) {
				pass_moved = true;
				moved = true;
			}
		}
		if (!pass_moved)
			break;
	}
	if (moved)
		render_launcher_page(home);
}

static void
layout_launcher_icons(struct home *home)
{
	int width;
	int height;
	int cell_w;
	int cell_h;
	int icon_px;

	if (home->launcher_icons == NULL)
		return;
	/*
	 * Size icons from the full page panel (5×7), not the cluster grid
	 * alone. Using context_grid height with home_rows made cells ~5/7
	 * too short, so strip icons looked tiny with a dead gap under them.
	 */
	width = pager_viewport_width(home);
	height = pager_viewport_height(home);
	if (width <= 0 || height <= 0)
		return;
	cell_w = (width - home_spacing(home) * (home_cols(home) - 1)) /
	    home_cols(home);
	cell_h = (height - home_spacing(home) * (home_rows(home) - 1)) /
	    home_rows(home);
	if (cell_w <= 0 || cell_h <= 0)
		return;
	icon_px = (int)((double)MIN(cell_w, cell_h) * home->config.launcher_icon_scale);
	icon_px = CLAMP(icon_px, home->config.launcher_icon_min_px, home->config.launcher_icon_max_px);
	/* A preferred minimum must never force an icon beyond its cell. */
	icon_px = MIN(icon_px, MAX(1, MIN(cell_w, cell_h)));
	for (size_t i = 0; i < home->launcher_icons->len; i++) {
		struct home_launcher_icon *icon =
		    g_ptr_array_index(home->launcher_icons, i);

		if (icon == NULL || icon->button == NULL ||
		    icon->page != home->launcher_current_page)
			continue;
		layout_launcher_icon_button(icon->button, icon_px);
	}
}

static void update_home_widget_density(struct home *home, struct home_grid_item *item);

static gboolean
launcher_layout_idle(gpointer data)
{
	struct home *home = data;

	home->launcher_layout_source = 0;
	layout_launcher_icons(home);
	for (size_t i = 0; i < home->grid_item_count; i++) {
		if (!home->grid_items[i].hidden)
			update_home_widget_density(home, &home->grid_items[i]);
	}
	return G_SOURCE_REMOVE;
}

static void
schedule_launcher_icon_layout(struct home *home)
{
	if (home->launcher_layout_source != 0)
		return;
	home->launcher_layout_source = g_idle_add(launcher_layout_idle, home);
}

static void
prepare_home_pager_allocation(gpointer data, int width, int height)
{
	struct home *home = data;

	if (width == home->pager_layout_width &&
	    height == home->pager_layout_height)
		return;
	home->pager_layout_width = width;
	home->pager_layout_height = height;
	sync_page_panel_widths(home);
	home->page_strip_x =
	    -(double)home->launcher_current_page *
	    (double)pager_page_stride(home);
	apply_page_drag_offset(home, home->page_drag_dx);
	/* Use the new viewport before GTK measures/allocates the retained strip. */
	layout_launcher_icons(home);
	for (size_t i = 0; i < home->grid_item_count; i++) {
		if (!home->grid_items[i].hidden)
			update_home_widget_density(home, &home->grid_items[i]);
	}
}

static void
seed_page_grid_anchors(GtkGrid *grid, int columns, int rows)
{
	GtkWidget *child;
	GtkWidget *span = gtk_label_new(" ");

	/*
	 * Anchors describe the grid's logical tracks.  The page-zero strip
	 * changes from 5x2 below the cluster to 2x5 beside it, so discard only
	 * our old sentinels before reseeding it for the new orientation.
	 */
	child = gtk_widget_get_first_child(GTK_WIDGET(grid));
	while (child != NULL) {
		GtkWidget *next = gtk_widget_get_next_sibling(child);

		if (g_object_get_data(G_OBJECT(child), "home-grid-anchor") != NULL)
			gtk_grid_remove(grid, child);
		child = next;
	}

	gtk_widget_set_size_request(span, 1, 1);
	gtk_widget_set_hexpand(span, TRUE);
	gtk_widget_set_vexpand(span, TRUE);
	gtk_widget_set_opacity(span, 0.0);
	gtk_widget_set_can_target(span, false);
	g_object_set_data(G_OBJECT(span), "home-grid-anchor",
	    GINT_TO_POINTER(1));
	gtk_grid_attach(grid, span, 0, 0, columns, rows);
	for (int column = 0; column < columns; column++) {
		GtkWidget *anchor = gtk_label_new(" ");

		gtk_widget_set_size_request(anchor, 1, 1);
		gtk_widget_set_opacity(anchor, 0.0);
		gtk_widget_set_can_target(anchor, false);
		g_object_set_data(G_OBJECT(anchor), "home-grid-anchor",
		    GINT_TO_POINTER(1));
		gtk_grid_attach(grid, anchor, column, 0, 1, 1);
	}
	for (int row = 1; row < rows; row++) {
		GtkWidget *anchor = gtk_label_new(" ");

		gtk_widget_set_size_request(anchor, 1, 1);
		gtk_widget_set_opacity(anchor, 0.0);
		gtk_widget_set_can_target(anchor, false);
		g_object_set_data(G_OBJECT(anchor), "home-grid-anchor",
		    GINT_TO_POINTER(1));
		gtk_grid_attach(grid, anchor, 0, row, 1, 1);
	}
}

static void
seed_strip_grid_anchors(struct home *home)
{
	int columns = home->landscape ? home_strip_rows(home) : home_cols(home);
	int rows = home->landscape ? home_cols(home) : home_strip_rows(home);

	seed_page_grid_anchors(home->strip_grid, MAX(columns, 1), MAX(rows, 1));
}

static GtkWidget *
make_icon_page_panel(struct home *home)
{
	GtkWidget *panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	GtkGrid *grid = GTK_GRID(gtk_grid_new());

	gtk_widget_add_css_class(panel, "home-page-panel");
	gtk_widget_set_hexpand(panel, FALSE);
	gtk_widget_set_vexpand(panel, TRUE);
	gtk_widget_set_overflow(panel, GTK_OVERFLOW_HIDDEN);
	gtk_grid_set_column_homogeneous(grid, TRUE);
	gtk_grid_set_row_homogeneous(grid, TRUE);
	gtk_grid_set_column_spacing(grid, home_spacing(home));
	gtk_grid_set_row_spacing(grid, home_spacing(home));
	gtk_widget_set_hexpand(GTK_WIDGET(grid), TRUE);
	gtk_widget_set_vexpand(GTK_WIDGET(grid), TRUE);
	seed_page_grid_anchors(grid, home_cols(home), home_rows(home));
	gtk_box_append(GTK_BOX(panel), GTK_WIDGET(grid));
	g_object_set_data(G_OBJECT(panel), "home-page-grid", grid);
	return panel;
}

static int
pager_viewport_width(struct home *home)
{
	int width = 0;

	/* GSK allocates the retained strip from page_motion. At fractional output
	 * scales its overlay allocation can differ from the sibling pad by a few
	 * logical pixels; using the pad then leaves a sliver of the previous page
	 * after the full-output settle. Use the widget that actually moves. */
	if (home->pager_gsk && home->page_motion != NULL)
		width = gtk_widget_get_width(GTK_WIDGET(home->page_motion));
	if (width <= 0 && home->page_viewport_pad != NULL)
		width = gtk_widget_get_width(home->page_viewport_pad);
	if (width <= 0 && home->page_viewport != NULL)
		width = gtk_widget_get_width(home->page_viewport);
	if (width <= 0 && home->context_overlay != NULL)
		width = gtk_widget_get_width(GTK_WIDGET(home->context_overlay));
	if (width <= 0)
		width = 1;
	return width;
}

static int
pager_page_stride(struct home *home)
{
	int viewport = pager_viewport_width(home);
	int output = 0;

	if (!home->pager_gsk)
		return viewport;
	/* The page contents live inside .home-root's horizontal padding, but a
	 * page transition crosses the physical output. Put that gutter between
	 * retained pages and advance by the full output width. The seam guard
	 * keeps rounded cards and shadows from touching the next page's clip. */
	if (home->rotation_host != NULL)
		output = gtk_widget_get_width(GTK_WIDGET(home->rotation_host));
	return MAX(viewport, output) + HOME_PAGER_SEAM_GUARD_PX;
}

static int
pager_viewport_height(struct home *home)
{
	int height = 0;

	if (home->pager_gsk && home->page_motion != NULL)
		height = gtk_widget_get_height(GTK_WIDGET(home->page_motion));
	if (height <= 0 && home->page_viewport_pad != NULL)
		height = gtk_widget_get_height(home->page_viewport_pad);
	if (height <= 0 && home->page_viewport != NULL)
		height = gtk_widget_get_height(home->page_viewport);
	if (height <= 0 && home->context_overlay != NULL)
		height = gtk_widget_get_height(GTK_WIDGET(home->context_overlay));
	return height;
}

static void
sync_page_panel_widths(struct home *home)
{
	int width = pager_viewport_width(home);
	int height = pager_viewport_height(home);
	int pages = MAX(home->page_panel_count, 1);
	int stride = home->pager_gsk ? pager_page_stride(home) : width;
	int page_gap = MAX(stride - width, 0);
	int strip_width = width * pages + page_gap * MAX(pages - 1, 0);
	int request_height = home->pager_gsk ? -1 : height;
	char css[256];

	if (width <= 1)
		return;
	if (height <= 1)
		return;
	/*
	 * Overlay children ignore expand flags for their request size. Lock
	 * each page to the pad allocation and make the strip N pages wide so
	 * pages cannot collapse to content width (was ~300 + ~60).
	 * GSK already allocates the exact current height. Do not turn the old
	 * pad height into a minimum that prevents shrinking before idle sync.
	 * The CSS fallback still needs its explicit overlay height request.
	 */
	if (home->page_strip != NULL)
		gtk_box_set_homogeneous(GTK_BOX(home->page_strip), TRUE);
	if (home->page_strip != NULL)
		gtk_box_set_spacing(GTK_BOX(home->page_strip), page_gap);
	for (int i = 0; i < home->page_panel_count; i++) {
		GtkWidget *panel = home->page_panels[i];

		if (panel == NULL)
			continue;
		gtk_widget_set_hexpand(panel, TRUE);
		gtk_widget_set_vexpand(panel, TRUE);
		gtk_widget_set_halign(panel, GTK_ALIGN_FILL);
		gtk_widget_set_valign(panel, GTK_ALIGN_FILL);
		gtk_widget_set_size_request(panel, width, request_height);
	}
	if (home->page_strip != NULL) {
		gtk_widget_set_hexpand(home->page_strip, FALSE);
		gtk_widget_set_vexpand(home->page_strip, TRUE);
		gtk_widget_set_halign(home->page_strip, GTK_ALIGN_START);
		gtk_widget_set_valign(home->page_strip, GTK_ALIGN_FILL);
		gtk_widget_set_size_request(home->page_strip, strip_width,
		    request_height);
	}
	if (home->pager_gsk && home->page_motion != NULL) {
		home_pager_motion_set_geometry(home->page_motion, pages, page_gap);
		home_pager_motion_set_translation(home->page_motion,
		    home->page_strip_x);
	} else if (home->pager_css == NULL) {
		home->pager_css = gtk_css_provider_new();
		gtk_style_context_add_provider_for_display(
		    gdk_display_get_default(),
		    GTK_STYLE_PROVIDER(home->pager_css),
		    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 5);
	}
	if (!home->pager_gsk) {
		g_snprintf(css, sizeof(css),
		    ".home-page-panel { min-width: %dpx; min-height: %dpx; }"
		    ".home-page-strip { min-width: %dpx; min-height: %dpx;"
		    " transform: translateX(%.2fpx); }",
		    width, height, width * pages, height, home->page_strip_x);
		gtk_css_provider_load_from_string(home->pager_css, css);
	}
	/*
	 * Keep the icon strip at strip_rows/grid_rows of the page height so the
	 * cluster stays 5×5 and the strip 2×5 instead of the cluster eating the
	 * leftover after a content-sized strip.
	 */
	if (home->strip_host != NULL && height > 1 && home_rows(home) > 0 &&
	    !home->landscape) {
		int spacing = home_spacing(home);
		int strip_h = (height - spacing) * home_strip_rows(home) /
		    home_rows(home);

		if (strip_h < 1)
			strip_h = 1;
		gtk_widget_set_size_request(home->strip_host, -1, strip_h);
		gtk_widget_set_hexpand(home->strip_host, TRUE);
		gtk_widget_set_vexpand(home->strip_host, FALSE);
		gtk_widget_set_valign(home->strip_host, GTK_ALIGN_FILL);
	} else if (home->strip_host != NULL && home->landscape) {
		int spacing = home_spacing(home);
		int strip_w = (width - spacing) * home_strip_rows(home) /
		    (home_cols(home) + home_strip_rows(home));

		/*
		 * Landscape keeps the five-column widget composition upright, then
		 * transposes the portrait app rows into side-dock columns. Leaving
		 * the dock at its 72px CSS minimum let the widget cluster take
		 * almost the whole page and pushed both app columns past the edge.
		 */
		if (strip_w < 1)
			strip_w = 1;
		gtk_widget_set_size_request(home->strip_host, strip_w, -1);
		gtk_widget_set_hexpand(home->strip_host, FALSE);
		gtk_widget_set_vexpand(home->strip_host, TRUE);
		gtk_widget_set_halign(home->strip_host, GTK_ALIGN_FILL);
	}
}

static void
ensure_page_panels(struct home *home)
{
	int want = CLAMP(home->launcher_page_count, 1, HOME_PAGE_PANELS_MAX);

	if (want > home->config.page_max_pages)
		want = home->config.page_max_pages;
	if (want < 1)
		want = 1;
	while (home->page_panel_count < want) {
		int index = home->page_panel_count;
		GtkWidget *panel;

		if (index == 0) {
			panel = home->context_body;
			home->page_grids[0] = home->context_grid;
		} else {
			panel = make_icon_page_panel(home);
			home->page_grids[index] = GTK_GRID(
			    g_object_get_data(G_OBJECT(panel), "home-page-grid"));
		}
		home->page_panels[index] = panel;
		if (gtk_widget_get_parent(panel) == NULL &&
		    home->page_strip != NULL)
			gtk_box_append(GTK_BOX(home->page_strip), panel);
		home->page_panel_count++;
	}
	while (home->page_panel_count > want) {
		int index = home->page_panel_count - 1;
		GtkWidget *panel = home->page_panels[index];

		if (index > 0 && panel != NULL) {
			GtkWidget *parent = gtk_widget_get_parent(panel);

			if (parent != NULL)
				gtk_box_remove(GTK_BOX(parent), panel);
		}
		home->page_panels[index] = NULL;
		home->page_grids[index] = NULL;
		home->page_panel_count--;
	}
	sync_page_panel_widths(home);
}

static void
attach_launcher_icon_to_page(struct home *home, struct home_launcher_icon *icon)
{
	if (icon == NULL || icon->button == NULL)
		return;
	if (icon->page <= 0) {
		attach_launcher_icon_view(home, icon);
		return;
	}
	if (icon->page >= home->page_panel_count ||
	    home->page_grids[icon->page] == NULL)
		return;
	gtk_grid_attach(home->page_grids[icon->page], icon->button,
	    icon->column, icon->row, 1, 1);
}

/*
 * Reattach icons to their cells without touching widget cards or pager layout.
 * Used during icon drag so we do not unparent mid-gesture through the full
 * render path (that was crashing ctlsthome while rearranging).
 */
static void
relayout_launcher_icons(struct home *home)
{
	if (home->launcher_icons == NULL)
		return;
	ensure_page_panels(home);
	for (size_t i = 0; i < home->launcher_icons->len; i++) {
		struct home_launcher_icon *icon =
		    g_ptr_array_index(home->launcher_icons, i);

		if (icon == NULL || icon->button == NULL ||
		    !GTK_IS_WIDGET(icon->button))
			continue;
		/* Durable ref: unparent must not finalize a gesture target. */
		g_object_ref(icon->button);
		launcher_unparent_button(icon->button);
		gtk_widget_set_visible(icon->button, FALSE);
	}
	for (size_t i = 0; i < home->launcher_icons->len; i++) {
		struct home_launcher_icon *icon =
		    g_ptr_array_index(home->launcher_icons, i);

		if (icon == NULL || icon->button == NULL ||
		    !GTK_IS_WIDGET(icon->button))
			continue;
		if (icon->page >= 0 && icon->page < home->launcher_page_count) {
			attach_launcher_icon_to_page(home, icon);
			gtk_widget_set_visible(icon->button, TRUE);
		}
		g_object_unref(icon->button);
	}
	schedule_launcher_icon_layout(home);
	refresh_launcher_icon_editing(home);
}

static void
render_launcher_page(struct home *home)
{
	if (home->launcher_icons == NULL)
		return;
	relayout_launcher_icons(home);
	apply_home_grid(home);
	refresh_launcher_page_bar(home);
	/* Avoid pager geometry churn while an icon drag is live. */
	if (home->active_launcher_drag == NULL) {
		sync_page_panel_widths(home);
		home->page_strip_x =
		    -(double)home->launcher_current_page *
		    (double)pager_page_stride(home);
		apply_page_drag_offset(home, 0.0);
	}
}

static void
set_launcher_page_animated(struct home *home, int page, bool animate)
{
	int from;
	int width;

	if (page < 0 || page >= home->launcher_page_count)
		return;
	if (home->launcher_current_page == page)
		return;
	from = home->launcher_current_page;
	width = pager_page_stride(home);
	ensure_page_panels(home);
	if (!animate) {
		home->launcher_current_page = page;
		home->page_strip_x = -(double)page * (double)width;
		apply_page_drag_offset(home, 0.0);
		refresh_launcher_page_bar(home);
		save_home_layout(home);
		return;
	}
	home->page_settle_commit = page;
	home->page_drag_owned = true;
	start_page_settle(home,
	    -(double)from * (double)width,
	    -(double)page * (double)width);
}

static void
set_launcher_page(struct home *home, int page)
{
	set_launcher_page_animated(home, page, false);
}

static void
refresh_launcher_page_bar(struct home *home)
{
	char label[48];

	if (home->launcher_page_bar == NULL)
		return;
	gtk_widget_set_visible(home->launcher_page_bar, home->layout_editing);
	if (!home->layout_editing)
		return;
	snprintf(label, sizeof(label), "Screen %d of %d",
	    home->launcher_current_page + 1, home->launcher_page_count);
	gtk_label_set_text(home->launcher_page_label, label);
	gtk_widget_set_sensitive(GTK_WIDGET(home->launcher_page_remove),
	    home->launcher_page_count > 1);
	gtk_widget_set_sensitive(GTK_WIDGET(home->launcher_page_add),
	    home->launcher_page_count < home->config.page_max_pages);
}

static void
launcher_page_add_clicked(GtkButton *button, gpointer data)
{
	struct home *home = data;

	(void)button;
	if (home->launcher_page_count >= home->config.page_max_pages)
		return;
	home->launcher_page_count += 1;
	ensure_page_panels(home);
	render_launcher_page(home);
	set_launcher_page(home, home->launcher_page_count - 1);
	save_home_layout(home);
}

static void
launcher_page_remove_clicked(GtkButton *button, gpointer data)
{
	struct home *home = data;
	int removed = home->launcher_current_page;

	(void)button;
	if (home->launcher_page_count <= 1)
		return;
	for (ssize_t i = (ssize_t)home->launcher_icons->len - 1; i >= 0; i--) {
		struct home_launcher_icon *icon =
		    g_ptr_array_index(home->launcher_icons, (size_t)i);

		if (icon->page == removed) {
			g_ptr_array_remove_index(home->launcher_icons, (size_t)i);
			continue;
		}
		if (icon->page > removed)
			icon->page -= 1;
	}
	for (size_t i = 0; i < home->grid_item_count; i++) {
		struct home_grid_item *item = &home->grid_items[i];

		if (item->page == removed)
			item->hidden = true;
		else if (item->page > removed)
			item->page -= 1;
	}
	home->launcher_page_count -= 1;
	if (home->launcher_current_page >= home->launcher_page_count)
		home->launcher_current_page = home->launcher_page_count - 1;
	render_launcher_page(home);
	save_home_layout(home);
}

static void
queue_home_card_resize(GtkWidget *widget)
{
	/* A compact-class change alters descendant font/icon sizes too. During
	 * allocation GTK can still have cached child requests from the full card.
	 * Invalidate those requests before negotiating the new grid dimensions. */
	gtk_widget_queue_resize(widget);
	for (GtkWidget *child = gtk_widget_get_first_child(widget); child != NULL;
	    child = gtk_widget_get_next_sibling(child))
		queue_home_card_resize(child);
}

static void
update_home_widget_density(struct home *home, struct home_grid_item *item)
{
	/* Compact when the cell is too small for full chrome (1x1, or only one
	 * row/column). Keeps analog face out of tiny allocations. */
	bool compact = item->column_span <= 1 || item->row_span <= 1;
	int rows = item->page == 0 ? home_cluster_rows(home) : home_rows(home);
	int available = pager_viewport_height(home);
	if (item->page == 0 && !home->landscape)
		available -= MAX(1, (available - home_spacing(home)) *
		    home_strip_rows(home) / home_rows(home)) + home_spacing(home);
	int slot_height = MAX(1, (available - home_spacing(home) * (rows - 1)) /
	    rows) * item->row_span + home_spacing(home) * (item->row_span - 1);
	bool short_card = slot_height < home->config.widget_compact_height_px;
	compact = compact || short_card;
	if (item->card != NULL) {
		bool was_short = gtk_widget_has_css_class(item->card, "home-widget-compact");
		if (short_card)
			gtk_widget_add_css_class(item->card, "home-widget-compact");
		else
			gtk_widget_remove_css_class(item->card, "home-widget-compact");
		if (was_short != short_card)
			queue_home_card_resize(item->card);
	}

	if (g_str_equal(item->id, "clock") && home->analog_clock != NULL) {
		gtk_widget_set_visible(GTK_WIDGET(home->analog_clock), !compact);
		gtk_widget_set_visible(home->clock_compact, compact);
		if (!compact) {
			/* Fill the cell; draw_analog_clock scales to allocation. */
			gtk_widget_set_hexpand(GTK_WIDGET(home->analog_clock), TRUE);
			gtk_widget_set_vexpand(GTK_WIDGET(home->analog_clock), TRUE);
			gtk_widget_set_halign(GTK_WIDGET(home->analog_clock),
			    GTK_ALIGN_FILL);
			gtk_widget_set_valign(GTK_WIDGET(home->analog_clock),
			    GTK_ALIGN_FILL);
		}
	} else if (g_str_equal(item->id, "calendar") &&
	    home->calendar_month != NULL) {
		bool month_fits = item->column_span >= home->config.calendar_month_min_columns &&
		    item->row_span >= home->config.calendar_month_min_rows &&
		    slot_height >= home->config.calendar_month_min_height_px;

		gtk_widget_set_visible(home->calendar_month, month_fits);
		gtk_widget_set_visible(home->calendar_compact, !month_fits);
	} else if (g_str_equal(item->id, "weather") && home->weather_range != NULL) {
		gtk_widget_set_visible(GTK_WIDGET(home->weather_range), !short_card);
	} else if (g_str_equal(item->id, "glance") && home->network_context != NULL) {
		GtkWidget *device = gtk_widget_get_parent(GTK_WIDGET(home->network_context));
		if (home->glance_title != NULL)
			gtk_widget_set_visible(home->glance_title, !short_card);
		if (device != NULL)
			gtk_widget_set_visible(device, !short_card);
	}
}

static void
map_widget_view_rect(struct home *home, int column, int row, int column_span,
    int row_span, int *view_col, int *view_row, int *view_cs, int *view_rs)
{
	(void)home;
	/* Keep the widget composition upright in both orientations. */
	*view_col = column;
	*view_row = row;
	*view_cs = column_span;
	*view_rs = row_span;
}

static void
attach_launcher_icon_view(struct home *home, struct home_launcher_icon *icon)
{
	int view_col = icon->column;
	int view_row = icon->row;
	bool in_strip = false;
	GtkGrid *grid;

	if (!home_rotation_map_cell(home->landscape, home_cols(home),
	    home_rows(home), home_cluster_rows(home), icon->column, icon->row,
	    &view_col, &view_row, &in_strip))
		return;
	grid = in_strip ? home->strip_grid : home->context_grid;
	if (grid == NULL)
		return;
	gtk_grid_attach(grid, icon->button, view_col, view_row, 1, 1);
}

static bool
widget_id_is_hidden(struct home *home, const char *id)
{
	for (int i = 0; i < home->config.widgets_hidden_count; i++) {
		if (strcmp(home->config.widgets_hidden[i], id) == 0)
			return true;
	}
	return false;
}

static void
apply_home_grid(struct home *home)
{
	ensure_page_panels(home);
	for (size_t i = 0; i < home->grid_item_count; i++) {
		struct home_grid_item *item = &home->grid_items[i];
		GtkGrid *target_grid;
		int view_col;
		int view_row;
		int view_cs;
		int view_rs;
		bool show;

		if (item->container == NULL)
			continue;
		/* Dangling pointers crash in g_object_ref (seen in coredumps:
		 * finish_page_settle → render_launcher_page → apply_home_grid). */
		if (!G_IS_OBJECT(item->container) ||
		    !GTK_IS_WIDGET(item->container)) {
			g_warning("home: dropping stale widget container for %s",
			    item->id != NULL ? item->id : "?");
			item->container = NULL;
			item->card = NULL;
			item->handle = NULL;
			item->remove_button = NULL;
			continue;
		}
		home_rotation_clamp_widget(home_cols(home), home_cluster_rows(home),
		    &item->column, &item->row, &item->column_span, &item->row_span);
		if (widget_id_is_hidden(home, item->id))
			item->hidden = true;
		show = !item->hidden;
		if (item->runtime != NULL)
			home_widget_runtime_set_visible(item->runtime, show);

		/*
		 * Always keep widgets parented in the cluster grid. The old
		 * unparent-on-other-pages path dropped the last GObject ref and
		 * destroyed the widget; the next page flip then crashed in
		 * g_object_ref (see finish_page_settle → apply_home_grid).
		 */
		item->page = CLAMP(item->page, 0, home->launcher_page_count - 1);
		target_grid = home->page_grids[item->page];
		if (item->page == 0)
			map_widget_view_rect(home, item->column, item->row,
			    item->column_span, item->row_span, &view_col, &view_row,
			    &view_cs, &view_rs);
		else {
			view_col = item->column;
			view_row = item->row;
			view_cs = item->column_span;
			view_rs = item->row_span;
		}
		g_object_ref(item->container);
		if (gtk_widget_get_parent(item->container) != NULL)
			gtk_widget_unparent(item->container);
		if (target_grid != NULL)
			gtk_grid_attach(target_grid, item->container,
			    view_col, view_row, view_cs, view_rs);
		gtk_widget_set_size_request(item->container, -1, -1);
		gtk_widget_set_hexpand(item->container, TRUE);
		gtk_widget_set_vexpand(item->container, TRUE);
		gtk_widget_set_halign(item->container, GTK_ALIGN_FILL);
		gtk_widget_set_valign(item->container, GTK_ALIGN_FILL);
		gtk_widget_set_visible(item->container, show);
		if (item->card != NULL && G_IS_OBJECT(item->card) &&
		    GTK_IS_WIDGET(item->card)) {
			gtk_widget_set_hexpand(item->card, TRUE);
			gtk_widget_set_vexpand(item->card, TRUE);
			gtk_widget_set_halign(item->card, GTK_ALIGN_FILL);
			gtk_widget_set_valign(item->card, GTK_ALIGN_FILL);
			gtk_widget_set_size_request(item->card, -1, -1);
		}
		if (show)
			update_home_widget_density(home, item);
		g_object_unref(item->container);
	}
	for (int page = 0; page < home->page_panel_count; page++) {
		if (home->page_grids[page] == NULL)
			continue;
		gtk_widget_queue_resize(GTK_WIDGET(home->page_grids[page]));
		gtk_widget_queue_allocate(GTK_WIDGET(home->page_grids[page]));
	}
	if (home->strip_grid != NULL) {
		gtk_widget_queue_resize(GTK_WIDGET(home->strip_grid));
		gtk_widget_queue_allocate(GTK_WIDGET(home->strip_grid));
	}
}

static void
load_home_layout(struct home *home)
{
	char *path = home_layout_path();
	char *contents = NULL;
	struct json_object *root;
	bool legacy_grid = true;

	if (!g_file_get_contents(path, &contents, NULL, NULL)) {
		g_free(path);
		return;
	}
	root = json_tokener_parse(contents);
	if (root != NULL && json_object_is_type(root, json_type_object)) {
		struct json_object *saved_columns;
		struct json_object *saved_rows;
		struct json_object *saved_pages;

		if (json_object_object_get_ex(root, "grid_columns", &saved_columns) &&
		    json_object_object_get_ex(root, "grid_rows", &saved_rows) &&
		    json_object_get_int(saved_columns) == home_cols(home) &&
		    json_object_get_int(saved_rows) == home_rows(home))
			legacy_grid = false;
		if (json_object_object_get_ex(root, "home_pages", &saved_pages) &&
		    json_object_is_type(saved_pages, json_type_int))
			home->launcher_page_count = CLAMP(json_object_get_int(saved_pages),
			    1, home->config.page_max_pages);
		for (size_t i = 0; i < home->grid_item_count; i++) {
			struct json_object *value;
			struct json_object *column;
			struct json_object *row;
			struct json_object *column_span;
			struct json_object *row_span;
			struct json_object *page_value;
			struct home_grid_item *item = &home->grid_items[i];

			if (!json_object_object_get_ex(root, item->id, &value) ||
			    !json_object_is_type(value, json_type_object) ||
			    !json_object_object_get_ex(value, "column", &column) ||
			    !json_object_object_get_ex(value, "row", &row) ||
			    !json_object_object_get_ex(value, "column_span", &column_span) ||
			    !json_object_object_get_ex(value, "row_span", &row_span))
				continue;
			item->page = 0;
			if (json_object_object_get_ex(value, "page", &page_value) &&
			    json_object_is_type(page_value, json_type_int))
				item->page = CLAMP(json_object_get_int(page_value), 0,
				    home->launcher_page_count - 1);
			{
				struct json_object *visibility;

				item->hidden = false;
				if (json_object_object_get_ex(value, "visibility",
				    &visibility) &&
				    json_object_is_type(visibility, json_type_string) &&
				    strcmp(json_object_get_string(visibility),
				    "hidden") == 0)
					item->hidden = true;
			}
			if (grid_item_fits(home, item, json_object_get_int(column),
			    json_object_get_int(row), json_object_get_int(column_span),
			    json_object_get_int(row_span))) {
				item->column = json_object_get_int(column);
				item->row = json_object_get_int(row);
				item->column_span = json_object_get_int(column_span);
				item->row_span = json_object_get_int(row_span);
				home_rotation_clamp_widget(home_cols(home),
				    home_cluster_rows(home), &item->column, &item->row,
				    &item->column_span, &item->row_span);
				/* Preserve the old launcher layout while widening its full-width cards. */
				if (legacy_grid && item->column == 0 && item->column_span == 4 &&
				    (g_str_equal(item->id, "weather") ||
				     g_str_equal(item->id, "glance")))
					item->column_span = home_cols(home);
			}
		}
		if (legacy_grid)
			save_home_layout(home);
		{
			struct json_object *pages_value;
			struct json_object *icons_value;
			struct json_object *order_value;
			bool have_icons = false;

			if (json_object_object_get_ex(root, "home_pages", &pages_value) &&
			    json_object_is_type(pages_value, json_type_int))
				home->launcher_page_count = CLAMP(
				    json_object_get_int(pages_value), 1,
				    home->config.page_max_pages);
			if (json_object_object_get_ex(root, "home_icons", &icons_value) &&
			    json_object_is_type(icons_value, json_type_array)) {
				for (size_t i = 0;
				    i < json_object_array_length(icons_value); i++) {
					struct json_object *entry =
					    json_object_array_get_idx(icons_value, i);
					struct json_object *id_value;
					struct json_object *page_value;
					struct json_object *column_value;
					struct json_object *row_value;
					struct home_launcher_icon *icon;

					if (!json_object_is_type(entry, json_type_object))
						continue;
					if (!json_object_object_get_ex(entry, "id",
					    &id_value) ||
					    !json_object_is_type(id_value,
					    json_type_string))
						continue;
					icon = g_new0(struct home_launcher_icon, 1);
					icon->home = home;
					icon->desktop_id =
					    g_strdup(json_object_get_string(id_value));
					icon->page = 0;
					icon->column = 0;
					icon->row = 0;
					if (json_object_object_get_ex(entry, "page",
					    &page_value) &&
					    json_object_is_type(page_value,
					    json_type_int))
						icon->page = json_object_get_int(page_value);
					if (json_object_object_get_ex(entry, "column",
					    &column_value) &&
					    json_object_is_type(column_value,
					    json_type_int))
						icon->column =
						    json_object_get_int(column_value);
					if (json_object_object_get_ex(entry, "row",
					    &row_value) &&
					    json_object_is_type(row_value, json_type_int))
						icon->row = json_object_get_int(row_value);
					icon->page = CLAMP(icon->page, 0,
					    home->launcher_page_count - 1);
					icon->column = CLAMP(icon->column, 0,
					    home_cols(home) - 1);
					icon->row = CLAMP(icon->row, 0,
					    home_rows(home) - 1);
					if (home->launcher_icons != NULL)
						g_ptr_array_add(home->launcher_icons, icon);
					else
						launcher_icon_free(icon);
					have_icons = true;
				}
			}
			if (!have_icons &&
			    json_object_object_get_ex(root, "favorites_order",
			    &order_value) &&
			    json_object_is_type(order_value, json_type_array) &&
			    home->launcher_icons != NULL) {
				for (size_t i = 0;
				    i < json_object_array_length(order_value); i++) {
					struct json_object *entry =
					    json_object_array_get_idx(order_value, i);
					struct home_launcher_icon *icon;
					int column = 0;
					int row = 0;

					if (!json_object_is_type(entry, json_type_string))
						continue;
					if (!launcher_find_free_cell(home, 0, &column,
					    &row))
						break;
					icon = g_new0(struct home_launcher_icon, 1);
					icon->home = home;
					icon->desktop_id =
					    g_strdup(json_object_get_string(entry));
					icon->page = 0;
					icon->column = column;
					icon->row = row;
					g_ptr_array_add(home->launcher_icons, icon);
				}
			}
		}
		reconcile_widgets_with_launcher_icons(home);
	}
	if (root != NULL)
		json_object_put(root);
	g_free(contents);
	g_free(path);
}

static void
save_home_layout(struct home *home)
{
	char *path = home_layout_path();
	char *directory = g_path_get_dirname(path);
	struct json_object *root = json_object_new_object();
	struct json_object *previous = NULL;
	char *previous_contents = NULL;
	const char *serialized;
	GError *error = NULL;

	g_mkdir_with_parents(directory, 0700);
	if (g_file_get_contents(path, &previous_contents, NULL, NULL))
		previous = json_tokener_parse(previous_contents);
	if (previous != NULL && json_object_is_type(previous, json_type_object)) {
		json_object_object_foreach(previous, key, value) {
			bool managed = strcmp(key, "version") == 0 ||
			    strcmp(key, "grid_columns") == 0 ||
			    strcmp(key, "grid_rows") == 0 ||
			    strcmp(key, "cluster_rows") == 0 ||
			    strcmp(key, "home_pages") == 0 ||
			    strcmp(key, "home_icons") == 0 ||
			    strcmp(key, "favorites_order") == 0;

			for (size_t i = 0; !managed && i < home->grid_item_count; i++)
				managed = strcmp(key, home->grid_items[i].id) == 0;
			/* Preserve the stable layout of a temporarily unavailable widget.
			 * Current widgets and all metadata are rewritten below. */
			if (!managed && json_object_is_type(value, json_type_object))
				json_object_object_add(root, key, json_object_get(value));
		}
	}
	json_object_object_add(root, "version", json_object_new_int(3));
	json_object_object_add(root, "grid_columns",
	    json_object_new_int(home_cols(home)));
	json_object_object_add(root, "grid_rows",
	    json_object_new_int(home_rows(home)));
	json_object_object_add(root, "cluster_rows",
	    json_object_new_int(home_cluster_rows(home)));
	for (size_t i = 0; i < home->grid_item_count; i++) {
		struct home_grid_item *item = &home->grid_items[i];
		struct json_object *value = json_object_new_object();

		json_object_object_add(value, "page", json_object_new_int(item->page));
		json_object_object_add(value, "column", json_object_new_int(item->column));
		json_object_object_add(value, "row", json_object_new_int(item->row));
		json_object_object_add(value, "column_span",
		    json_object_new_int(item->column_span));
		json_object_object_add(value, "row_span",
		    json_object_new_int(item->row_span));
		json_object_object_add(value, "visibility",
		    json_object_new_string(item->hidden ? "hidden" : "enabled"));
		json_object_object_add(root, item->id, value);
	}
	json_object_object_add(root, "home_pages",
	    json_object_new_int(home->launcher_page_count));
	if (home->launcher_icons != NULL && home->launcher_icons->len > 0) {
		struct json_object *icons = json_object_new_array();

		for (size_t i = 0; i < home->launcher_icons->len; i++) {
			struct home_launcher_icon *icon =
			    g_ptr_array_index(home->launcher_icons, i);
			struct json_object *entry = json_object_new_object();

			if (icon == NULL || icon->desktop_id == NULL)
				continue;
			json_object_object_add(entry, "id",
			    json_object_new_string(icon->desktop_id));
			json_object_object_add(entry, "page",
			    json_object_new_int(icon->page));
			json_object_object_add(entry, "column",
			    json_object_new_int(icon->column));
			json_object_object_add(entry, "row",
			    json_object_new_int(icon->row));
			json_object_array_add(icons, entry);
		}
		json_object_object_add(root, "home_icons", icons);
	}
	serialized = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PRETTY);
	if (!g_file_set_contents_full(path, serialized, -1,
	    G_FILE_SET_CONTENTS_CONSISTENT, 0600, &error)) {
		g_warning("home: cannot save layout %s: %s", path,
		    error != NULL ? error->message : "unknown error");
		g_clear_error(&error);
	}
	json_object_put(root);
	if (previous != NULL)
		json_object_put(previous);
	g_free(previous_contents);
	g_free(directory);
	g_free(path);
}

static bool
grid_cell_size(struct home *home, double *cell_width, double *cell_height)
{
	int column_spacing;
	int row_spacing;
	int width;
	int height;
	GtkWidget *measure = home->context_body != NULL ?
	    home->context_body : GTK_WIDGET(home->context_grid);

	width = gtk_widget_get_width(measure);
	height = gtk_widget_get_height(measure);
	column_spacing = gtk_grid_get_column_spacing(home->context_grid);
	row_spacing = gtk_grid_get_row_spacing(home->context_grid);
	if (width <= 0 || height <= 0)
		return false;
	*cell_width = ((double)width -
	    (double)column_spacing * (home_cols(home) - 1)) /
	    home_cols(home);
	*cell_height = ((double)height -
	    (double)row_spacing * (home_rows(home) - 1)) / home_rows(home);
	return *cell_width > 0.0 && *cell_height > 0.0;
}

static void
draw_edit_grid(GtkDrawingArea *area, cairo_t *cr, GtkGrid *grid,
    int columns, int rows)
{
	graphene_point_t origin = GRAPHENE_POINT_INIT(0, 0), offset;
	if (grid == NULL || columns < 1 || rows < 1 ||
	    !gtk_widget_compute_point(GTK_WIDGET(grid), GTK_WIDGET(area),
	    &origin, &offset))
		return;
	double sx = gtk_grid_get_column_spacing(grid);
	double sy = gtk_grid_get_row_spacing(grid);
	double cw = (gtk_widget_get_width(GTK_WIDGET(grid)) - sx * (columns - 1)) / columns;
	double ch = (gtk_widget_get_height(GTK_WIDGET(grid)) - sy * (rows - 1)) / rows;
	for (int row = 0; row < rows; row++) {
		for (int col = 0; col < columns; col++) {
			cairo_rectangle(cr, offset.x + col * (cw + sx) + 0.5,
			    offset.y + row * (ch + sy) + 0.5, cw - 1.0, ch - 1.0);
		}
	}
}

static void
draw_grid_overlay(GtkDrawingArea *area, cairo_t *cr, int width, int height,
    gpointer data)
{
	struct home *home = data;
	if (!home->layout_editing)
		return;
	cairo_save(cr);
	cairo_set_source_rgba(cr, home->accent_rgb[0], home->accent_rgb[1],
	    home->accent_rgb[2], 0.22);
	cairo_set_line_width(cr, 1.0);
	if (home->launcher_current_page == 0) {
		draw_edit_grid(area, cr, home->context_grid, home_cols(home), home_cluster_rows(home));
		draw_edit_grid(area, cr, home->strip_grid,
		    home->landscape ? home_strip_rows(home) : home_cols(home),
		    home->landscape ? home_cols(home) : home_strip_rows(home));
	} else {
		draw_edit_grid(area, cr, home->page_grids[home->launcher_current_page],
		    home_cols(home), home_rows(home));
	}
	cairo_stroke(cr);
	(void)width;
	(void)height;
	cairo_restore(cr);
}

static void
refresh_grid_editing(struct home *home)
{
	for (size_t i = 0; i < home->grid_item_count; i++) {
		struct home_grid_item *item = &home->grid_items[i];
		bool selected = home->layout_editing && home->selected_grid_item == item;

		if (item->container == NULL || item->hidden)
			continue;
		if (selected)
			gtk_widget_add_css_class(item->container, "home-widget-selected");
		else
			gtk_widget_remove_css_class(item->container, "home-widget-selected");
		gtk_widget_set_visible(item->handle, home->layout_editing);
		/* The icon is visual-only; the card owns the full drag sequence. */
		gtk_widget_set_can_target(item->handle, false);
		if (item->remove_button != NULL) {
			gtk_widget_set_visible(item->remove_button,
			    home->layout_editing);
			gtk_widget_set_can_target(item->remove_button,
			    home->layout_editing);
		}
	}
	gtk_widget_set_visible(GTK_WIDGET(home->grid_overlay), home->layout_editing);
	if (home->context_body != NULL) {
		if (home->layout_editing)
			gtk_widget_add_css_class(home->context_body,
			    "home-grid-editing");
		else
			gtk_widget_remove_css_class(home->context_body,
			    "home-grid-editing");
	}
	gtk_widget_queue_draw(GTK_WIDGET(home->grid_overlay));
}

static void
set_layout_editing_marker(struct home *home, bool editing)
{
	if (editing) {
		int fd = open(home->layout_edit_path,
		    O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
		if (fd >= 0)
			close(fd);
	} else {
		unlink(home->layout_edit_path);
	}
}

static void
set_layout_editing(struct home *home, bool editing)
{
	if (home->layout_editing == editing) {
		if (editing)
			set_layout_editing_marker(home, true);
		return;
	}
	home->layout_editing = editing;
	if (home->layout_drag != NULL)
		gtk_event_controller_set_propagation_phase(
		    GTK_EVENT_CONTROLLER(home->layout_drag),
		    editing ? GTK_PHASE_CAPTURE : GTK_PHASE_BUBBLE);
	set_layout_editing_marker(home, editing);
	if (editing)
		pulse_layout_edit_haptic(home);
	if (!editing) {
		home->selected_grid_item = NULL;
		home->active_drag_item = NULL;
		home->active_launcher_drag = NULL;
		cancel_launcher_drag_arm(home);
		cancel_launcher_push_dwell(home);
		cancel_page_edge_dwell(home);
		cancel_push_dwell(home);
		clear_drag_layout_snapshot(home);
		reset_drag_preview(home);
		save_home_layout(home);
	}
	refresh_grid_editing(home);
	refresh_launcher_page_bar(home);
	refresh_launcher_icon_editing(home);
	refresh_edit_controls(home);
}

static void
select_grid_item(struct home *home, struct home_grid_item *item)
{
	if (!home->layout_editing)
		set_layout_editing(home, true);
	home->selected_grid_item = item;
	refresh_grid_editing(home);
}

static void
home_widget_long_pressed(GtkGestureLongPress *gesture, double x, double y,
    gpointer data)
{
	struct home_grid_item *item = data;

	(void)x;
	(void)y;
	home_widget_runtime_cancel_pointer(item->runtime);
	gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
	select_grid_item(item->home, item);
}

static void
start_home_widget_drag(struct home *home, struct home_grid_item *item,
    unsigned int resize_edges, double start_x, double start_y)
{
	select_grid_item(home, item);
	home->active_drag_item = item;
	home->resize_edges = resize_edges;
	home->resize_drag = resize_edges != 0;
	home->drag_start_column = item->column;
	home->drag_start_row = item->row;
	home->drag_start_column_span = item->column_span;
	home->drag_start_row_span = item->row_span;
	home->drag_start_x = start_x;
	home->drag_start_y = start_y;
	save_drag_layout_snapshot(home);
	reset_drag_preview(home);
	home->drag_preview_column = item->column;
	home->drag_preview_row = item->row;
	home->drag_preview_column_span = item->column_span;
	home->drag_preview_row_span = item->row_span;
}

static bool
home_widget_target_from_drag(struct home *home, struct home_grid_item *item,
    double offset_x, double offset_y, int *column, int *row,
    int *column_span, int *row_span)
{
	double cell_width;
	double cell_height;
	double x_step;
	double y_step;
	int right;
	int bottom;
	int min_column;
	int min_row;

	if (!grid_cell_size(home, &cell_width, &cell_height))
		return false;
	x_step = cell_width + gtk_grid_get_column_spacing(home->context_grid);
	y_step = cell_height + gtk_grid_get_row_spacing(home->context_grid);
	if (item->page == 0) {
		x_step = (gtk_widget_get_width(GTK_WIDGET(home->context_grid)) +
		    gtk_grid_get_column_spacing(home->context_grid)) / (double)home_cols(home);
		y_step = (gtk_widget_get_height(GTK_WIDGET(home->context_grid)) +
		    gtk_grid_get_row_spacing(home->context_grid)) / (double)home_cluster_rows(home);
	}
	/* Gesture deltas belong to the unscaled outer overlay. */
	if (home->edit_frame != NULL && home->edit_frame->scale > 0.0) {
		offset_x /= home->edit_frame->scale;
		offset_y /= home->edit_frame->scale;
	}
	*column = home->drag_preview_column;
	*row = home->drag_preview_row;
	*column_span = home->drag_preview_column_span;
	*row_span = home->drag_preview_row_span;
	if (*column < 0 || *row < 0 || *column_span < 0 || *row_span < 0) {
		*column = home->drag_start_column;
		*row = home->drag_start_row;
		*column_span = home->drag_start_column_span;
		*row_span = home->drag_start_row_span;
	}
	if (home->resize_drag) {
		right = *column + *column_span;
		bottom = *row + *row_span;
		if (home->resize_edges & HOME_RESIZE_LEFT) {
			min_column = 0;
			*column = grid_index_with_hysteresis(offset_x, x_step,
			    home->drag_start_column, *column, min_column,
			    home->drag_start_column + home->drag_start_column_span -
			    home_grid_min_columns(item),
			    home->config.drag_cell_switch_fraction);
		}
		if (home->resize_edges & HOME_RESIZE_RIGHT) {
			right = grid_index_with_hysteresis(offset_x, x_step,
			    home->drag_start_column + home->drag_start_column_span,
			    right, *column + home_grid_min_columns(item),
			    home_cols(home),
			    home->config.drag_cell_switch_fraction);
		}
		if (home->resize_edges & HOME_RESIZE_TOP) {
			min_row = 0;
			*row = grid_index_with_hysteresis(offset_y, y_step,
			    home->drag_start_row, *row, min_row,
			    home->drag_start_row + home->drag_start_row_span -
			    home_grid_min_rows(item),
			    home->config.drag_cell_switch_fraction);
		}
		if (home->resize_edges & HOME_RESIZE_BOTTOM) {
			bottom = grid_index_with_hysteresis(offset_y, y_step,
			    home->drag_start_row + home->drag_start_row_span,
			    bottom, *row + home_grid_min_rows(item),
			    home_rows(home),
			    home->config.drag_cell_switch_fraction);
		}
		*column_span = MAX(home_grid_min_columns(item), right - *column);
		*row_span = MAX(home_grid_min_rows(item), bottom - *row);
		if (!(home->resize_edges & HOME_RESIZE_LEFT))
			*column = home->drag_start_column;
		if (!(home->resize_edges & HOME_RESIZE_TOP))
			*row = home->drag_start_row;
		if (*column + *column_span > home_cols(home))
			*column_span = home_cols(home) - *column;
		if (*row + *row_span > home_rows(home))
			*row_span = home_rows(home) - *row;
	} else {
		*column = grid_index_with_hysteresis(offset_x, x_step,
		    home->drag_start_column, *column, 0,
		    home_cols(home) - *column_span,
		    home->config.drag_cell_switch_fraction);
		*row = grid_index_with_hysteresis(offset_y, y_step,
		    home->drag_start_row, *row, 0, home_rows(home) - *row_span,
		    home->config.drag_cell_switch_fraction);
	}
	home->drag_preview_column = *column;
	home->drag_preview_row = *row;
	home->drag_preview_column_span = *column_span;
	home->drag_preview_row_span = *row_span;
	return true;
}

static void
home_resize_trace(struct home *home, struct home_grid_item *item,
    const char *stage, double x, double y)
{
	const char *runtime;
	char path[256];
	int fd;

	if (g_getenv("CTLSTHOME_RESIZE_DEBUG") == NULL)
		return;
	runtime = g_get_user_runtime_dir();
	if (runtime == NULL)
		return;
	if (g_snprintf(path, sizeof(path), "%s/ctlsthome-resize-debug.log",
	    runtime) < 0)
		return;
	fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
	if (fd < 0)
		return;
	dprintf(fd, "%s item=%s x=%.1f y=%.1f edges=0x%x resize=%d "
	    "layout=%d span=%dx%d box=%dx%d grid=%dx%d context=%dx%d\n",
	    stage, item->id, x, y,
	    home->resize_edges, home->resize_drag, home->layout_editing,
	    item->column_span, item->row_span,
	    gtk_widget_get_width(item->container),
	    gtk_widget_get_height(item->container),
	    gtk_widget_get_width(GTK_WIDGET(home->context_grid)),
	    gtk_widget_get_height(GTK_WIDGET(home->context_grid)),
	    gtk_widget_get_width(home->context),
	    gtk_widget_get_height(home->context));
	close(fd);
}

static struct home_grid_item *
home_grid_item_at(struct home *home, double x, double y,
    double *local_x, double *local_y)
{
	GtkWidget *picked;

	picked = gtk_widget_pick(GTK_WIDGET(home->context_overlay), x, y,
	    GTK_PICK_DEFAULT);
	for (size_t i = 0; i < home->grid_item_count; i++) {
		struct home_grid_item *item = &home->grid_items[i];
		GtkWidget *ancestor;
		graphene_point_t point = { (float)x, (float)y };
		graphene_point_t local;

		for (ancestor = picked; ancestor != NULL;
		    ancestor = gtk_widget_get_parent(ancestor)) {
			if (ancestor != item->container)
				continue;
			if (!gtk_widget_compute_point(GTK_WIDGET(home->context_overlay),
			    item->container, &point, &local))
				return NULL;
			*local_x = local.x;
			*local_y = local.y;
			return item;
		}
	}
	return NULL;
}

static void
home_widget_resize_hit_extents(int width, int height, double *hit_w, double *hit_h)
{
	const double target_w = HOME_WIDGET_HANDLE_HIT_W;
	const double target_h = HOME_WIDGET_HANDLE_HIT_H;
	const double max_w = width * 0.46;
	const double max_h = height * 0.46;

	*hit_w = MIN(target_w, max_w);
	*hit_h = MIN(target_h, max_h);
	/* Use the full handle target whenever the cell can still fit a drag body. */
	if (width >= target_w + 16.0)
		*hit_w = target_w;
	if (height >= target_h + 16.0)
		*hit_h = target_h;
}

static void
home_widget_drag_begin(GtkGestureDrag *gesture, double start_x, double start_y,
    gpointer data)
{
	struct home *home = data;
	struct home_grid_item *item;
	struct home_launcher_icon *icon;
	double local_x;
	double local_y;

	cancel_launcher_drag_arm(home);
	item = home_grid_item_at(home, start_x, start_y, &local_x, &local_y);
	if (item != NULL && item->remove_button != NULL) {
		GtkWidget *target = gtk_widget_pick(GTK_WIDGET(home->context_overlay),
		    start_x, start_y, GTK_PICK_DEFAULT);
		if (target == item->remove_button || (target != NULL &&
		    gtk_widget_is_ancestor(target, item->remove_button))) {
			gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_DENIED);
			return;
		}
	}
	if (home->layout_editing && item != NULL) {
		home->resize_edges = 0;
		if (gtk_widget_get_width(item->container) > 0 &&
		    gtk_widget_get_height(item->container) > 0) {
			const int width = gtk_widget_get_width(item->container);
			const int height = gtk_widget_get_height(item->container);
			double hit_w;
			double hit_h;
			bool near_right;
			bool near_bottom;

			home_widget_resize_hit_extents(width, height, &hit_w, &hit_h);
			near_right = local_x >= width - hit_w;
			near_bottom = local_y >= height - hit_h;

			if (near_right && near_bottom)
				home->resize_edges =
				    HOME_RESIZE_RIGHT | HOME_RESIZE_BOTTOM;
		}
		if (home->resize_edges != 0) {
			start_home_widget_drag(home, item, home->resize_edges,
			    local_x, local_y);
			home->drag_overlay_press_x = start_x;
			home->drag_overlay_press_y = start_y;
			home_resize_trace(home, item, "parent-begin", local_x,
			    local_y);
			gtk_gesture_set_state(GTK_GESTURE(gesture),
			    GTK_EVENT_SEQUENCE_CLAIMED);
			return;
		}
		start_home_widget_drag(home, item, 0, local_x, local_y);
		home->drag_overlay_press_x = start_x;
		home->drag_overlay_press_y = start_y;
		home_resize_trace(home, item, "drag-begin", local_x, local_y);
		gtk_gesture_set_state(GTK_GESTURE(gesture),
		    GTK_EVENT_SEQUENCE_CLAIMED);
		return;
	}
	/*
	 * Icons: claim the press but do not move yet. Short release launches;
	 * holding ~350ms arms rearrange. Immediate drag-to-move made taps feel
	 * like failed launches and fought click.
	 */
	icon = home_launcher_icon_at(home, start_x, start_y);
	if (icon != NULL) {
		home->pending_launcher_press = icon;
		home->launcher_press_x = start_x;
		home->launcher_press_y = start_y;
		/* Edit mode is already explicit intent: move immediately instead of
		 * making every rearrange wait through the normal launch hold timer. */
		if (home->layout_editing) {
			/* A small movement while tapping the remove badge must not turn
			 * into a rearrange. The release handler owns that target. */
			if (launcher_remove_badge_at(home, start_x, start_y) == icon) {
				home->launcher_drag_armed = false;
				return;
			}
			home->launcher_drag_armed = true;
			home->selected_grid_item = NULL;
			start_launcher_icon_drag(home, icon);
			gtk_gesture_set_state(GTK_GESTURE(gesture),
			    GTK_EVENT_SEQUENCE_CLAIMED);
			return;
		}
		home->launcher_drag_armed = false;
		home->launcher_drag_arm_source = g_timeout_add(
		    home->config.drag_icon_arm_ms, launcher_drag_arm_fire, home);
		/* Keep this shared until motion resolves. A horizontal drag belongs
		 * to the capture-phase page controller; a stationary hold/tap stays
		 * here. The page controller cancels this timer at tap slop. */
		return;
	}
	gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_DENIED);
}

static void
home_widget_drag_update(GtkGestureDrag *gesture, double offset_x,
    double offset_y, gpointer data)
{
	struct home *home = data;
	struct home_grid_item *item = home->active_drag_item;
	int column;
	int row;
	int column_span;
	int row_span;

	(void)gesture;
	if (home->pending_launcher_press != NULL && !home->launcher_drag_armed)
		return;
	if (home->active_launcher_drag != NULL) {
		bool remove_zone;

		home->last_drag_overlay_x = home->launcher_press_x + offset_x;
		home->last_drag_overlay_y = home->launcher_press_y + offset_y;
		home->launcher_drag_offset_x = offset_x;
		home->launcher_drag_offset_y = offset_y;
		remove_zone = launcher_point_is_remove(home,
		    home->last_drag_overlay_x, home->last_drag_overlay_y);
		set_launcher_remove_armed(home, remove_zone);
		if (remove_zone) {
			restore_drag_layout_snapshot(home);
			return;
		}
		check_drag_page_edge(home, home->last_drag_overlay_x,
		    home->last_drag_overlay_y);
		update_launcher_icon_drag(home, offset_x, offset_y);
		return;
	}
	if (item == NULL || home->active_drag_item != item)
		return;
	if (!home_widget_target_from_drag(home, item, offset_x, offset_y,
	    &column, &row, &column_span, &row_span))
		return;
	restore_drag_layout_snapshot(home);
	if (home->resize_drag) {
		if (grid_item_fits(home, item, column, row, column_span, row_span) ||
		    push_obstructing_widgets_down(home, item, column, row,
		    column_span, row_span)) {
			apply_widget_placement(home, item, column, row, column_span,
			    row_span);
			home_resize_trace(home, item, "resize-update", offset_x,
			    offset_y);
		}
	} else if (try_place_widget(home, item, column, row, column_span, row_span,
	    true)) {
		home_resize_trace(home, item, "update", offset_x, offset_y);
	}
	home->last_drag_overlay_x = home->drag_overlay_press_x + offset_x;
	home->last_drag_overlay_y = home->drag_overlay_press_y + offset_y;
	check_drag_page_edge(home, home->last_drag_overlay_x,
	    home->last_drag_overlay_y);
}

static void
home_widget_drag_end(GtkGestureDrag *gesture, double offset_x, double offset_y,
    gpointer data)
{
	struct home *home = data;
	struct home_grid_item *item = home->active_drag_item;
	struct home_launcher_icon *pending = home->pending_launcher_press;
	bool armed = home->launcher_drag_armed;
	bool remove_badge = pending != NULL && home->layout_editing &&
	    launcher_remove_badge_at(home, home->launcher_press_x + offset_x,
	    home->launcher_press_y + offset_y) == pending &&
	    hypot(offset_x, offset_y) < HOME_TAP_SLOP;
	int column;
	int row;
	int column_span;
	int row_span;

	(void)gesture;
	cancel_launcher_drag_arm(home);
	if (remove_badge) {
		char *desktop_id = g_strdup(pending->desktop_id);

		home->pending_launcher_press = NULL;
		home->launcher_suppress_click = true;
		remove_favorite(home, desktop_id);
		pulse_layout_edit_haptic(home);
		g_free(desktop_id);
		return;
	}
	if (pending != NULL && !armed) {
		/* A launcher opens only on a stationary release, never a shell pull. */
		if (hypot(offset_x, offset_y) < HOME_TAP_SLOP)
			launch_launcher_icon(home, pending);
		home->pending_launcher_press = NULL;
		return;
	}
	if (home->active_launcher_drag != NULL) {
		end_launcher_icon_drag(home, offset_x, offset_y);
		home->pending_launcher_press = NULL;
		return;
	}
	home->pending_launcher_press = NULL;
	if (item == NULL)
		return;
	cancel_push_dwell(home);
	cancel_launcher_push_dwell(home);
	cancel_page_edge_dwell(home);
	home_resize_trace(home, item, "end", offset_x, offset_y);
	restore_drag_layout_snapshot(home);
	if (!point_in_edit_controls(home, home->drag_overlay_press_x + offset_x,
	    home->drag_overlay_press_y + offset_y) &&
	    home_widget_target_from_drag(home, item, offset_x, offset_y,
	    &column, &row, &column_span, &row_span)) {
		if (home->resize_drag) {
			if (grid_item_fits(home, item, column, row, column_span,
			    row_span) ||
			    push_obstructing_widgets_down(home, item, column, row,
			    column_span, row_span))
				apply_widget_placement(home, item, column, row,
				    column_span, row_span);
		} else {
			try_place_widget(home, item, column, row, column_span,
			    row_span, true);
		}
	}
	clear_drag_layout_snapshot(home);
	reset_drag_preview(home);
	home->active_drag_item = NULL;
	home->resize_edges = 0;
	home->resize_drag = false;
	save_home_layout(home);
}

static void
remove_home_widget(struct home_grid_item *item)
{
	struct home *home;

	if (item == NULL || item->home == NULL)
		return;
	home = item->home;
	item->hidden = true;
	if (item->runtime != NULL)
		home_widget_runtime_set_visible(item->runtime, false);
	if (item->container != NULL)
		gtk_widget_set_visible(item->container, false);
	if (home->selected_grid_item == item)
		home->selected_grid_item = NULL;
	render_launcher_page(home);
	refresh_grid_editing(home);
	refresh_edit_controls(home);
	save_home_layout(home);
	pulse_layout_edit_haptic(home);
}

static void
home_widget_remove_clicked(GtkGestureClick *gesture, int presses, double x,
    double y, gpointer data)
{
	struct home_grid_item *item = data;
	(void)presses;
	if (item == NULL || item->home == NULL || !item->home->layout_editing ||
	    item->remove_button == NULL || !gtk_widget_get_mapped(item->remove_button) ||
	    x < 0 || y < 0 || x >= gtk_widget_get_width(item->remove_button) ||
	    y >= gtk_widget_get_height(item->remove_button))
		return;
	/* A widget is removed only on release. Claim here so the card's drag and
	 * activation handlers cannot reinterpret the same touch. */
	gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
	remove_home_widget(item);
}

static const char *home_widget_display_name(const struct home_grid_item *item);

static GtkWidget *
home_widget_container(struct home *home, struct home_grid_item *item,
	GtkWidget *card)
{
	GtkWidget *container = gtk_overlay_new();
	GtkWidget *handle = gtk_label_new("↘");
	GtkWidget *remove = gtk_button_new_with_label("×");
	GtkWidget *remove_mark = gtk_button_get_child(GTK_BUTTON(remove));
	GtkGesture *long_press = gtk_gesture_long_press_new();
	GtkGesture *click = gtk_gesture_click_new();
	GtkGesture *tap_drag = gtk_gesture_drag_new();
	GtkGesture *remove_click = gtk_gesture_click_new();

	item->home = home;
	item->card = card;
	item->container = container;
	item->handle = handle;
	item->remove_button = remove;
	item->hidden = false;
	/* Durable owner ref so page-flip unparent/attach cycles cannot destroy
	 * the widget when it briefly has no parent. */
	g_object_ref_sink(container);
	gtk_widget_add_css_class(container, "home-widget");
	/* Clip card chrome to the cell; do not let content force the grid taller. */
	gtk_widget_set_overflow(container, GTK_OVERFLOW_HIDDEN);
	home_clip_box(card);
	gtk_widget_set_hexpand(container, TRUE);
	gtk_widget_set_vexpand(container, TRUE);
	gtk_widget_set_hexpand(card, TRUE);
	gtk_widget_set_vexpand(card, TRUE);
	gtk_widget_set_halign(card, GTK_ALIGN_FILL);
	gtk_widget_set_valign(card, GTK_ALIGN_FILL);
	gtk_overlay_set_child(GTK_OVERLAY(container), card);
	gtk_widget_add_css_class(handle, "home-widget-handle");
	gtk_widget_set_halign(handle, GTK_ALIGN_END);
	gtk_widget_set_valign(handle, GTK_ALIGN_END);
	gtk_widget_set_margin_end(handle, 6);
	gtk_widget_set_margin_bottom(handle, 6);
	gtk_widget_set_visible(handle, false);
	gtk_widget_set_can_target(handle, false);
	gtk_overlay_add_overlay(GTK_OVERLAY(container), handle);
	gtk_widget_add_css_class(remove, "home-widget-remove");
	gtk_widget_set_halign(remove_mark, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(remove_mark, GTK_ALIGN_CENTER);
	char *remove_name = g_strdup_printf("Remove %s widget", home_widget_display_name(item));
	gtk_accessible_reset_relation(GTK_ACCESSIBLE(remove), GTK_ACCESSIBLE_RELATION_LABELLED_BY);
	gtk_accessible_update_property(GTK_ACCESSIBLE(remove),
	    GTK_ACCESSIBLE_PROPERTY_LABEL, remove_name,
	    GTK_ACCESSIBLE_PROPERTY_DESCRIPTION, "Remove from Home; restore with Add widget.", -1);
	gtk_widget_set_tooltip_text(remove, remove_name);
	g_free(remove_name);
	char *resize_name = g_strdup_printf("Resize %s widget", home_widget_display_name(item));
	gtk_accessible_update_property(GTK_ACCESSIBLE(handle),
	    GTK_ACCESSIBLE_PROPERTY_LABEL, resize_name, -1);
	g_free(resize_name);
	gtk_widget_set_halign(remove, GTK_ALIGN_START);
	gtk_widget_set_valign(remove, GTK_ALIGN_START);
	gtk_widget_set_visible(remove, false);
	gtk_widget_set_can_target(remove, false);
	gtk_widget_set_focusable(remove, FALSE);
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(remove_click),
	    GDK_BUTTON_PRIMARY);
	gtk_gesture_single_set_exclusive(GTK_GESTURE_SINGLE(remove_click), true);
	gtk_event_controller_set_propagation_phase(
	    GTK_EVENT_CONTROLLER(remove_click), GTK_PHASE_CAPTURE);
	g_signal_connect(remove_click, "released",
	    G_CALLBACK(home_widget_remove_clicked), item);
	gtk_widget_add_controller(remove, GTK_EVENT_CONTROLLER(remove_click));
	gtk_overlay_add_overlay(GTK_OVERLAY(container), remove);
	gtk_widget_set_can_target(container, true);
	gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(long_press),
	    GTK_PHASE_CAPTURE);
	g_signal_connect(long_press, "pressed",
	    G_CALLBACK(home_widget_long_pressed), item);
	gtk_widget_add_controller(container, GTK_EVENT_CONTROLLER(long_press));
	/* GtkGestureClick receives release after a drag, so record movement before
	 * dispatching the release-only widget action. */
	gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(tap_drag),
	    GTK_PHASE_CAPTURE);
	g_signal_connect(tap_drag, "drag-begin",
	    G_CALLBACK(home_widget_tap_drag_begin), item);
	g_signal_connect(tap_drag, "drag-update",
	    G_CALLBACK(home_widget_tap_drag_update), item);
	gtk_widget_add_controller(container, GTK_EVENT_CONTROLLER(tap_drag));
	/* Short tap opens the linked app; ignored while layout editing. */
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_PRIMARY);
	g_signal_connect(click, "released", G_CALLBACK(home_widget_clicked), item);
	gtk_widget_add_controller(container, GTK_EVENT_CONTROLLER(click));
	return container;
}

static bool
external_widget_is_enabled(struct home *home, const char *id)
{
	if (!home->config.widgets_enabled_set)
		return true;
	for (int i = 0; i < home->config.widgets_enabled_count; i++) {
		if (strcmp(home->config.widgets_enabled[i], id) == 0)
			return true;
	}
	return false;
}

static const char *
home_widget_display_name(const struct home_grid_item *item)
{
	if (item->descriptor != NULL && item->descriptor->name[0] != '\0')
		return item->descriptor->name;
	if (g_str_equal(item->id, "weather"))
		return "Weather";
	if (g_str_equal(item->id, "clock"))
		return "Clock";
	if (g_str_equal(item->id, "calendar"))
		return "Calendar";
	if (g_str_equal(item->id, "glance"))
		return "At a glance";
	return item->id;
}

static GtkWidget *
external_widget_placeholder(const struct home_widget_desc *desc)
{
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	GtkWidget *label = gtk_label_new(desc->name);
	GtkWidget *detail = gtk_label_new("External Home widget");

	gtk_widget_add_css_class(box, "home-external-widget");
	gtk_widget_add_css_class(label, "home-widget-picker-title");
	gtk_widget_add_css_class(detail, "home-widget-picker-status");
	gtk_widget_set_halign(label, GTK_ALIGN_START);
	gtk_widget_set_halign(detail, GTK_ALIGN_START);
	gtk_box_append(GTK_BOX(box), label);
	gtk_box_append(GTK_BOX(box), detail);
	return box;
}

static bool
ensure_external_widget_runtime(struct home_grid_item *item)
{
	GtkWidget *view;

	if (item->descriptor == NULL)
		return true;
	if (item->runtime != NULL)
		return home_widget_runtime_view(item->runtime) != NULL;
	item->runtime = home_widget_runtime_new(item->descriptor);
	view = home_widget_runtime_view(item->runtime);
	if (view == NULL)
		return false;
	home_clip_box(view);
	gtk_widget_add_css_class(view, "home-external-widget");
	gtk_widget_set_hexpand(view, TRUE);
	gtk_widget_set_vexpand(view, TRUE);
	gtk_widget_set_halign(view, GTK_ALIGN_FILL);
	gtk_widget_set_valign(view, GTK_ALIGN_FILL);
	gtk_overlay_set_child(GTK_OVERLAY(item->container), view);
	item->card = view;
	return true;
}

static void
add_external_widgets(struct home *home)
{
	for (int i = 0; i < home->widget_desc_count &&
	    home->grid_item_count < HOME_GRID_WIDGET_MAX; i++) {
		const struct home_widget_desc *desc = &home->widget_descs[i];
		struct home_grid_item *item;
		GtkWidget *placeholder;

		if (desc->kind != HOME_WIDGET_KIND_EXEC)
			continue;
		item = &home->grid_items[home->grid_item_count++];
		*item = (struct home_grid_item){
			.home = home,
			.id = desc->id,
			.descriptor = desc,
			.column = 0,
			.row = 0,
			.column_span = MIN(desc->default_columns, home_cols(home)),
			.row_span = MIN(desc->default_rows, home_cluster_rows(home)),
			.hidden = !external_widget_is_enabled(home, desc->id) ||
			    widget_id_is_hidden(home, desc->id),
		};
		placeholder = external_widget_placeholder(desc);
		home_widget_container(home, item, placeholder);
		item->hidden = !external_widget_is_enabled(home, desc->id) ||
		    widget_id_is_hidden(home, desc->id);
	}
}

static void
hide_widget_picker(struct home *home)
{
	if (home->widget_picker != NULL)
		gtk_widget_set_visible(home->widget_picker, false);
}

static void
widget_picker_close_clicked(GtkButton *button, gpointer data)
{
	(void)button;
	hide_widget_picker(data);
}

static void
widget_picker_add_clicked(GtkButton *button, gpointer data)
{
	struct home *home = data;
	struct home_grid_item *item = g_object_get_data(G_OBJECT(button),
	    "home-widget-item");
	int column;
	int row;
	int column_span;
	int row_span;

	if (item == NULL || !item->hidden)
		return;
	column_span = CLAMP(item->column_span, home_grid_min_columns(item),
	    home_cols(home));
	row_span = CLAMP(item->row_span, home_grid_min_rows(item),
	    home_cluster_rows(home));
	/* Hidden items are deliberately ignored by normal collision checks. Make
	 * this one live temporarily so the picker finds a genuinely open slot. */
	item->page = home->launcher_current_page;
	item->hidden = false;
	if (!find_available_grid_slot(home, item, item->column, item->row,
	    column_span, row_span, &column, &row)) {
		column_span = home_grid_min_columns(item);
		row_span = home_grid_min_rows(item);
		if (!find_available_grid_slot(home, item, 0, 0, column_span,
		    row_span, &column, &row)) {
			item->hidden = true;
			gtk_label_set_text(home->widget_picker_status,
			    "No open space. Remove or resize something first.");
			return;
		}
	}
	if (!ensure_external_widget_runtime(item)) {
		item->hidden = true;
		gtk_label_set_text(home->widget_picker_status,
		    "That widget could not be started.");
		return;
	}
	item->column = column;
	item->row = row;
	item->column_span = column_span;
	item->row_span = row_span;
	home->selected_grid_item = item;
	apply_home_grid(home);
	render_launcher_page(home);
	refresh_grid_editing(home);
	save_home_layout(home);
	pulse_layout_edit_haptic(home);
	hide_widget_picker(home);
}

static void
rebuild_widget_picker(struct home *home)
{
	GtkWidget *child;
	int available = 0;

	if (home->widget_picker_list == NULL)
		return;
	while ((child = gtk_widget_get_first_child(home->widget_picker_list)) != NULL)
		gtk_box_remove(GTK_BOX(home->widget_picker_list), child);
	for (size_t i = 0; i < home->grid_item_count; i++) {
		struct home_grid_item *item = &home->grid_items[i];
		GtkWidget *choice;
		char *label;

		if (!item->hidden || widget_id_is_hidden(home, item->id))
			continue;
		label = g_strdup_printf("+  %s", home_widget_display_name(item));
		choice = gtk_button_new_with_label(label);
		GtkLabel *choice_label = GTK_LABEL(gtk_button_get_child(GTK_BUTTON(choice)));
		gtk_label_set_ellipsize(choice_label, PANGO_ELLIPSIZE_END);
		gtk_label_set_width_chars(choice_label, 1);
		g_free(label);
		gtk_widget_add_css_class(choice, "home-widget-choice");
		gtk_widget_set_focusable(choice, false);
		g_object_set_data(G_OBJECT(choice), "home-widget-item", item);
		g_signal_connect(choice, "clicked",
		    G_CALLBACK(widget_picker_add_clicked), home);
		gtk_box_append(GTK_BOX(home->widget_picker_list), choice);
		available++;
	}
	gtk_label_set_text(home->widget_picker_status,
	    available > 0 ? "Choose a widget to place on this screen."
	    : "Every available widget is already on Home.");
}

static void
widget_picker_open_clicked(GtkButton *button, gpointer data)
{
	struct home *home = data;

	(void)button;
	rebuild_widget_picker(home);
	gtk_widget_set_visible(home->widget_picker, true);
	gtk_widget_grab_focus(home->widget_picker);
}

static void
wallpaper_edit_clicked(GtkButton *button, gpointer data)
{
	struct home *home = data;

	(void)button;
	hide_widget_picker(home);
	set_layout_editing(home, false);
	launch_desktop_id("dev.ctlst.Settings.desktop");
}

static void
edit_done_clicked(GtkButton *button, gpointer data)
{
	(void)button;
	set_layout_editing(data, false);
}

static void
refresh_edit_controls(struct home *home)
{
	if (home->edit_frame != NULL)
		home_edit_frame_configure(home->edit_frame, home->layout_editing,
		    home->config.edit_inset_px, home->config.edit_controls_gap_px);
	if (home->edit_toolbar != NULL)
		gtk_widget_set_visible(home->edit_toolbar, home->layout_editing);
	if (!home->layout_editing)
		hide_widget_picker(home);
}

/* Acknowledge old helpers without hiding or animating the live scene. */
static void
signal_rotation_ready(struct home *home)
{
	int fd = open(home->rotation_ready_path,
	    O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	if (fd >= 0)
		close(fd);
}

static gboolean
update_layout_size(struct home *home, int width, int height)
{
	bool landscape;

	if (home->window == NULL || home->context == NULL)
		return G_SOURCE_CONTINUE;
	if (width <= 0 || height <= 0)
		return G_SOURCE_CONTINUE;
	landscape = width > height;
	/* First successful measure must apply even when still portrait
	 * (landscape defaults false and would otherwise early-return). */
	if (home->landscape_known && landscape == home->landscape &&
	    width == home->layout_width && height == home->layout_height)
		return G_SOURCE_CONTINUE;
	home->landscape_known = true;
	home->layout_width = width;
	home->layout_height = height;

	/*
	 * V2 responsive layout: cluster (rows 0..cluster_rows-1) and icon strip
	 * are separate grids. Portrait stacks them; landscape keeps the widget
	 * composition upright and docks the transposed app strip beside it.
	 */
	if (home->context_body != NULL) {
		gtk_orientable_set_orientation(GTK_ORIENTABLE(home->context_body),
		    landscape ? GTK_ORIENTATION_HORIZONTAL :
		    GTK_ORIENTATION_VERTICAL);
	}
	if (home->strip_host != NULL) {
		gtk_widget_set_hexpand(home->strip_host, !landscape);
		gtk_widget_set_vexpand(home->strip_host, landscape);
	}
	if (home->cluster_host != NULL) {
		gtk_widget_set_hexpand(home->cluster_host, TRUE);
		gtk_widget_set_vexpand(home->cluster_host, TRUE);
	}
	gtk_orientable_set_orientation(GTK_ORIENTABLE(home->root),
	    GTK_ORIENTATION_VERTICAL);
	gtk_orientable_set_orientation(GTK_ORIENTABLE(home->header),
	    GTK_ORIENTATION_HORIZONTAL);
	gtk_label_set_xalign(home->system, 1);
	gtk_widget_set_valign(GTK_WIDGET(home->system), GTK_ALIGN_END);
	gtk_widget_set_size_request(home->header, -1, -1);
	gtk_widget_set_size_request(home->context, -1, -1);
	gtk_widget_set_visible(home->context, TRUE);
	gtk_widget_set_hexpand(home->context, TRUE);
	gtk_widget_set_vexpand(home->context, TRUE);
	if (home->agent != NULL)
		gtk_widget_set_visible(home->agent, !landscape);
	if (landscape)
		gtk_widget_add_css_class(home->root, "landscape");
	else
		gtk_widget_remove_css_class(home->root, "landscape");
	if (home->context_body != NULL) {
		if (landscape)
			gtk_widget_add_css_class(home->context_body,
			    "home-body-landscape");
		else
			gtk_widget_remove_css_class(home->context_body,
			    "home-body-landscape");
	}
	home->landscape = landscape;
	seed_strip_grid_anchors(home);
	render_launcher_page(home);
	sync_page_panel_widths(home);
	schedule_launcher_icon_layout(home);
	return G_SOURCE_CONTINUE;
}

static gboolean
update_layout(struct home *home)
{
	if (home->window == NULL)
		return G_SOURCE_CONTINUE;
	return update_layout_size(home,
	    gtk_widget_get_width(GTK_WIDGET(home->window)),
	    gtk_widget_get_height(GTK_WIDGET(home->window)));
}

static void
prepare_home_allocation(gpointer data, int width, int height)
{
	update_layout_size(data, width, height);
}

static void
finish_home_allocation(gpointer data, int width, int height)
{
	struct home *home = data;
	(void)width;
	(void)height;
	/* The non-GSK recovery path uses CSS requests after the pad is allocated. */
	if (!home->pager_gsk)
		prepare_home_pager_allocation(home, pager_viewport_width(home),
		    pager_viewport_height(home));
}

static void
draw_analog_clock(GtkDrawingArea *area, cairo_t *cr, int width, int height,
    gpointer data)
{
	struct home *home = data;
	struct timeval now;
	struct tm local;
	double fraction;
	double seconds;
	double minutes;
	double hours;
	double size;
	double radius;
	double center_x;
	double center_y;
	double angle;

	(void)area;
	gettimeofday(&now, NULL);
	localtime_r(&now.tv_sec, &local);
	fraction = (double)now.tv_usec / 1000000.0;
	seconds = (double)local.tm_sec + fraction;
	minutes = (double)local.tm_min + seconds / 60.0;
	hours = (double)(local.tm_hour % 12) + minutes / 60.0;
	size = MIN((double)width, (double)height);
	radius = size * 0.45;
	center_x = (double)width / 2.0;
	center_y = (double)height / 2.0;

	if (size <= 0.0)
		return;

	cairo_save(cr);
	/* Soft shadow so the semantic face reads on any wallpaper. */
	cairo_arc(cr, center_x, center_y + size * 0.018, radius, 0.0,
	    2.0 * G_PI);
	cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.14);
	cairo_fill(cr);

	cairo_arc(cr, center_x, center_y, radius, 0.0, 2.0 * G_PI);
	cairo_set_source_rgba(cr, home->visual_palette[0][0],
	    home->visual_palette[0][1], home->visual_palette[0][2], 0.98);
	cairo_fill(cr);
	cairo_arc(cr, center_x, center_y, radius - 0.5, 0.0, 2.0 * G_PI);
	cairo_set_source_rgba(cr, home->line_rgb[0], home->line_rgb[1],
	    home->line_rgb[2], 0.72);
	cairo_set_line_width(cr, MAX(1.0, size * 0.008));
	cairo_stroke(cr);

	angle = hours * G_PI / 6.0 - G_PI / 2.0;
	cairo_move_to(cr, center_x, center_y);
	cairo_line_to(cr, center_x + cos(angle) * radius * 0.56,
	    center_y + sin(angle) * radius * 0.56);
	cairo_set_source_rgb(cr, home->text_rgb[0], home->text_rgb[1],
	    home->text_rgb[2]);
	cairo_set_line_width(cr, MAX(4.0, size * 0.035));
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_stroke(cr);

	angle = minutes * G_PI / 30.0 - G_PI / 2.0;
	cairo_move_to(cr, center_x, center_y);
	cairo_line_to(cr, center_x + cos(angle) * radius * 0.78,
	    center_y + sin(angle) * radius * 0.78);
	cairo_set_source_rgb(cr, home->text_rgb[0], home->text_rgb[1],
	    home->text_rgb[2]);
	cairo_set_line_width(cr, MAX(3.0, size * 0.025));
	cairo_stroke(cr);

	angle = seconds * G_PI / 30.0 - G_PI / 2.0;
	cairo_move_to(cr, center_x, center_y);
	cairo_line_to(cr, center_x + cos(angle) * radius * 0.92,
	    center_y + sin(angle) * radius * 0.92);
	cairo_set_source_rgb(cr, home->accent_rgb[0], home->accent_rgb[1],
	    home->accent_rgb[2]);
	cairo_set_line_width(cr, MAX(1.5, size * 0.012));
	cairo_stroke(cr);

	cairo_arc(cr, center_x, center_y, MAX(3.0, size * 0.035), 0.0,
	    2.0 * G_PI);
	cairo_set_source_rgb(cr, home->accent_rgb[0], home->accent_rgb[1],
	    home->accent_rgb[2]);
	cairo_fill(cr);
	cairo_restore(cr);
}

static gboolean
refresh_analog_clock(gpointer data)
{
	struct home *home = data;

	if (home->analog_clock != NULL)
		gtk_widget_queue_draw(GTK_WIDGET(home->analog_clock));
	return G_SOURCE_CONTINUE;
}

static gboolean
refresh_clock(gpointer data)
{
	struct home *home = data;
	time_t now = time(NULL);
	struct tm local;
	char clock_text[32];
	char date_text[64];

	localtime_r(&now, &local);
	strftime(clock_text, sizeof(clock_text), "%I:%M", &local);
	if (clock_text[0] == '0')
		memmove(clock_text, clock_text + 1, strlen(clock_text));
	strftime(date_text, sizeof(date_text), "%A, %B %e", &local);
	gtk_label_set_text(home->clock, clock_text);
	gtk_label_set_text(home->date, date_text);
	if (home->clock_compact_time != NULL) {
		gtk_label_set_text(home->clock_compact_time, clock_text);
		gtk_label_set_text(home->clock_compact_date, date_text);
	}
	if (home->calendar_compact_date != NULL) {
		char compact_date[32];
		char compact_weekday[32];

		strftime(compact_date, sizeof(compact_date), "%b %e", &local);
		strftime(compact_weekday, sizeof(compact_weekday), "%A", &local);
		gtk_label_set_text(home->calendar_compact_date, compact_date);
		gtk_label_set_text(home->calendar_compact_weekday, compact_weekday);
	}
	update_layout(home);
	return G_SOURCE_CONTINUE;
}

static char *
read_first_line(const char *path)
{
	char *contents = NULL;
	char **lines;
	char *result;

	if (!g_file_get_contents(path, &contents, NULL, NULL))
		return g_strdup("?");
	lines = g_strsplit(contents, "\n", 2);
	result = g_strdup(lines[0]);
	g_strfreev(lines);
	g_free(contents);
	return result;
}

static void
free_status_snapshot(gpointer data)
{
	struct status_snapshot *snapshot = data;

	if (snapshot == NULL)
		return;
	g_free(snapshot->capacity);
	g_free(snapshot->battery_status);
	g_free(snapshot->network);
	g_free(snapshot->calls);
	g_free(snapshot);
}

static void
load_status_snapshot(GTask *task, gpointer source_object,
    gpointer task_data, GCancellable *cancellable)
{
	struct status_snapshot *snapshot = g_new0(struct status_snapshot, 1);
	char *network_argv[] = {"sh", "-c",
	    "nmcli -t -f TYPE,STATE,CONNECTION device status 2>/dev/null | "
	    "sed -n 's/^wifi:connected://p' | head -n1", NULL};
	char *call_argv[] = {"mmcli", "-m", "any", "--voice-list-calls", NULL};

	(void)source_object;
	(void)task_data;
	(void)cancellable;
	snapshot->capacity = read_first_line(
	    "/sys/class/power_supply/qcom-battery/capacity");
	snapshot->battery_status = read_first_line(
	    "/sys/class/power_supply/qcom-battery/status");
	snapshot->network = run_capture(network_argv);
	snapshot->calls = run_capture(call_argv);
	g_task_return_pointer(task, snapshot, free_status_snapshot);
}

static void
apply_status_snapshot(GObject *source_object, GAsyncResult *result,
    gpointer data)
{
	struct home *home = data;
	struct status_snapshot *snapshot;
	char *trimmed;
	char *summary;
	char *network_chip;
	char *battery_chip;

	(void)source_object;
	home->status_refresh_pending = false;
	snapshot = g_task_propagate_pointer(G_TASK(result), NULL);
	if (snapshot == NULL)
		return;
	trimmed = g_strstrip(snapshot->network);
	summary = g_strdup_printf("%s  ·  %s%%",
	    trimmed[0] != '\0' ? trimmed : "Offline", snapshot->capacity);
	network_chip = g_strdup(
	    trimmed[0] != '\0' ? trimmed : "Offline");
	battery_chip = g_strdup_printf("%s%% · %s",
	    snapshot->capacity, snapshot->battery_status);

	gtk_label_set_text(home->system, summary);
	gtk_label_set_text(home->network_context, network_chip);
	gtk_label_set_text(home->battery_context, battery_chip);
	gtk_widget_set_visible(home->call_card,
	    strstr(snapshot->calls, "/Call/") != NULL);
	g_free(summary);
	g_free(network_chip);
	g_free(battery_chip);
	free_status_snapshot(snapshot);
}

static gboolean
refresh_status(gpointer data)
{
	struct home *home = data;
	GTask *task;

	if (home->status_refresh_pending)
		return G_SOURCE_CONTINUE;
	home->status_refresh_pending = true;
	task = g_task_new(NULL, NULL, apply_status_snapshot, home);
	g_task_run_in_thread(task, load_status_snapshot);
	g_object_unref(task);
	return G_SOURCE_CONTINUE;
}

static void
set_cpu_times(struct cpu_times *times, unsigned long long user,
    unsigned long long nice, unsigned long long system,
    unsigned long long idle, unsigned long long iowait,
    unsigned long long irq, unsigned long long softirq,
    unsigned long long steal)
{
	times->idle = idle + iowait;
	times->total = user + nice + system + idle + iowait + irq + softirq +
	    steal;
	times->present = true;
}

static bool
read_cpu_times(struct cpu_times *aggregate)
{
	char line[256];
	char name[16];
	unsigned long long user;
	unsigned long long nice;
	unsigned long long system;
	unsigned long long idle_time;
	unsigned long long iowait;
	unsigned long long irq;
	unsigned long long softirq;
	unsigned long long steal;
	FILE *file = fopen("/proc/stat", "r");

	if (file == NULL)
		return false;
	memset(aggregate, 0, sizeof(*aggregate));
	while (fgets(line, sizeof(line), file) != NULL) {
		int fields;

		if (strncmp(line, "cpu", 3) != 0)
			break;
		fields = sscanf(line,
		    "%15s %llu %llu %llu %llu %llu %llu %llu %llu",
		    name, &user, &nice, &system, &idle_time, &iowait, &irq,
		    &softirq, &steal);
		if (fields != 9)
			continue;
		if (strcmp(name, "cpu") == 0) {
			set_cpu_times(aggregate, user, nice, system, idle_time,
			    iowait, irq, softirq, steal);
			break;
		}
	}
	fclose(file);
	return aggregate->present;
}

static bool
read_memory_kib(unsigned long long *total, unsigned long long *used,
    unsigned long long *available)
{
	char line[256];
	char key[64];
	char unit[16];
	unsigned long long value;
	unsigned long long free_kib = 0;
	unsigned long long buffers = 0;
	unsigned long long cached = 0;
	unsigned long long reclaimable = 0;
	FILE *file = fopen("/proc/meminfo", "r");
	bool have_total = false;
	bool have_free = false;
	bool have_available = false;

	*total = 0;
	*used = 0;
	*available = 0;
	if (file == NULL)
		return false;
	while (fgets(line, sizeof(line), file) != NULL) {
		if (sscanf(line, "%63s %llu %15s", key, &value, unit) < 2)
			continue;
		if (strcmp(key, "MemTotal:") == 0) {
			*total = value;
			have_total = true;
		} else if (strcmp(key, "MemFree:") == 0) {
			free_kib = value;
			have_free = true;
		} else if (strcmp(key, "MemAvailable:") == 0) {
			*available = value;
			have_available = true;
		} else if (strcmp(key, "Buffers:") == 0) {
			buffers = value;
		} else if (strcmp(key, "Cached:") == 0) {
			cached = value;
		} else if (strcmp(key, "SReclaimable:") == 0) {
			reclaimable = value;
		}
	}
	fclose(file);
	if (!have_total || !have_free || !have_available || *total == 0 ||
	    *available > *total)
		return false;
	value = free_kib + buffers + cached + reclaimable;
	*used = value < *total ? *total - value : 0;
	return true;
}

static gboolean
refresh_activity(gpointer data)
{
	struct home *home = data;
	struct cpu_times aggregate;
	unsigned long long memory_total;
	unsigned long long memory_used;
	unsigned long long memory_available;
	long online_cores;

	if (!home->visible)
		return G_SOURCE_CONTINUE;
	online_cores = sysconf(_SC_NPROCESSORS_ONLN);
	if (read_cpu_times(&aggregate)) {
		char *text;

		if (home->cpu_sample_ready &&
		    aggregate.total > home->previous_cpu_total) {
			unsigned long long total_delta =
			    aggregate.total - home->previous_cpu_total;
			unsigned long long idle_delta =
			    aggregate.idle - home->previous_cpu_idle;
			double usage = total_delta > idle_delta ?
			    (double)(total_delta - idle_delta) / (double)total_delta :
			    0.0;

			text = g_strdup_printf("CPU %.0f%%  ·  %ld %s", usage * 100.0,
			    online_cores > 0 ? online_cores : 0,
			    online_cores == 1 ? "core" : "cores");
		} else {
			text = g_strdup_printf("CPU sampling  ·  %ld %s",
			    online_cores > 0 ? online_cores : 0,
			    online_cores == 1 ? "core" : "cores");
		}
		gtk_label_set_text(home->cpu_context, text);
		g_free(text);
		home->previous_cpu_total = aggregate.total;
		home->previous_cpu_idle = aggregate.idle;
		home->cpu_sample_ready = true;
	} else {
		gtk_label_set_text(home->cpu_context, "CPU unavailable");
		home->cpu_sample_ready = false;
	}
	if (read_memory_kib(&memory_total, &memory_used, &memory_available)) {
		double fraction = (double)memory_used / (double)memory_total;
		char *text = g_strdup_printf(
		    "RAM %.2f / %.1f GiB  ·  %.1f avail",
		    (double)memory_used / (1024.0 * 1024.0),
		    (double)memory_total / (1024.0 * 1024.0),
		    (double)memory_available / (1024.0 * 1024.0));

		gtk_label_set_text(home->memory_context, text);
		gtk_progress_bar_set_fraction(home->memory_meter, fraction);
		g_free(text);
	} else {
		gtk_label_set_text(home->memory_context, "RAM unavailable");
		gtk_progress_bar_set_fraction(home->memory_meter, 0.0);
	}
	return G_SOURCE_CONTINUE;
}

static gboolean
finish_show_focus(gpointer data)
{
	struct home *home = data;

	if (gtk_window_get_focus(home->window) == home->root) {
		gtk_window_set_focus(home->window, NULL);
		gtk_layer_set_keyboard_mode(home->window,
		    GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
	}
	return G_SOURCE_REMOVE;
}

static gboolean
finish_show(gpointer data)
{
	struct home *home = data;

	if (!home->visible)
		return G_SOURCE_REMOVE;
	gtk_widget_set_visible(GTK_WIDGET(home->window), TRUE);
	gtk_window_present(home->window);
	gtk_widget_queue_resize(home->root);
	gtk_widget_queue_draw(home->root);
	if (home_is_locked(home)) {
		release_prompt_for_lock(home);
		return G_SOURCE_REMOVE;
	}
	gtk_layer_set_keyboard_mode(home->window,
	    GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND);
	gtk_window_set_focus(home->window, home->root);
	gtk_widget_grab_focus(home->root);
	if (home->visual != NULL)
		gtk_gl_area_queue_render(home->visual);
	refresh_activity(home);
	g_timeout_add(260, finish_show_focus, home);
	return G_SOURCE_REMOVE;
}

static void
stop_refresh_sources(struct home *home)
{
	if (home->clock_source != 0) {
		g_source_remove(home->clock_source);
		home->clock_source = 0;
	}
	if (home->status_source != 0) {
		g_source_remove(home->status_source);
		home->status_source = 0;
	}
	if (home->activity_source != 0) {
		g_source_remove(home->activity_source);
		home->activity_source = 0;
	}
	if (home->analog_clock_source != 0) {
		g_source_remove(home->analog_clock_source);
		home->analog_clock_source = 0;
	}
	if (home->weather_source != 0) {
		g_source_remove(home->weather_source);
		home->weather_source = 0;
	}
}

static void
start_refresh_sources(struct home *home)
{
	if (home->clock_source == 0)
		home->clock_source = g_timeout_add_seconds(1, refresh_clock, home);
	if (home->status_source == 0)
		home->status_source = g_timeout_add_seconds(8, refresh_status, home);
	if (home->activity_source == 0)
		home->activity_source = g_timeout_add(1000, refresh_activity, home);
	if (home->analog_clock_source == 0)
		home->analog_clock_source = g_timeout_add(16,
	    refresh_analog_clock, home);
	if (home->weather_source == 0)
		home->weather_source = g_timeout_add_seconds(60,
		    refresh_weather, home);
}

static void
set_visible(struct home *home, bool visible)
{
	if (home->visible == visible)
		return;
	home->visible = visible;
	if (visible) {
		start_refresh_sources(home);
		close(open(home->state_path,
		    O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600));
		refresh_clock(home);
		refresh_status(home);
		refresh_weather(home);
		request_weather_fetch();
		gtk_widget_set_visible(GTK_WIDGET(home->window), TRUE);
		gtk_window_present(home->window);
		finish_show(home);
	} else {
		set_layout_editing(home, false);
		if (home->prompt != NULL)
			end_prompt_session(home, "blur");
		stop_refresh_sources(home);
		home->cpu_sample_ready = false;
		unlink(home->state_path);
		gtk_widget_set_visible(GTK_WIDGET(home->window), FALSE);
	}
}

static void
open_agent(GtkWidget *widget, gpointer data)
{
	struct home *home = data;

	(void)widget;
	if (home->prompt == NULL)
		return;
	if (home->agent != NULL)
		gtk_widget_set_visible(home->agent, TRUE);
	request_prompt_focus(home);
}

static void
suggestion_clicked(GtkButton *button, gpointer data)
{
	struct home *home = data;
	const char *prompt = g_object_get_data(G_OBJECT(button), "prompt");

	gtk_editable_set_text(GTK_EDITABLE(home->prompt), prompt);
	ask_agent(GTK_WIDGET(button), home);
}

static GtkWidget *
make_button(const char *label, const char *style, GCallback callback,
    gpointer data)
{
	GtkWidget *button = gtk_button_new_with_label(label);

	gtk_widget_add_css_class(button, style);
	g_signal_connect(button, "clicked", callback, data);
	return button;
}

static void
quick_action(GtkButton *button, gpointer data)
{
	const char *action = g_object_get_data(G_OBJECT(button), "action");
	char *scripts = ctlst_script_path(NULL);
	char *drawer = g_build_filename(scripts, "ctlstdrawer", NULL);
	char *app_run = g_build_filename(scripts, "app-run", NULL);
	char *terminal = g_build_filename(scripts, "terminal-shell", NULL);
	char *foot_config = ctlst_config_path("foot.ini");
	char *foot_option = g_strdup_printf("--config=%s", foot_config);
	char *apps_argv[] = {drawer, "show", NULL};
	char *phone_argv[] = {app_run, "desktop", "dev.ctlst.Dialer.desktop", NULL};
	char *messages_argv[] = {app_run, "desktop", "dev.ctlst.Messages.desktop", NULL};
	char *terminal_argv[] = {app_run, "command", "foot", "foot",
	    foot_option, terminal, NULL};

	(void)data;
	if (g_strcmp0(action, "apps") == 0)
		run_detached(apps_argv);
	else if (g_str_has_prefix(action, "desktop:")) {
		char *desktop_argv[] = {app_run, "desktop", (char *)action + 8, NULL};

		run_detached(desktop_argv);
	}
	else if (g_strcmp0(action, "phone") == 0)
		run_detached(phone_argv);
	else if (g_strcmp0(action, "messages") == 0)
		run_detached(messages_argv);
	else if (g_strcmp0(action, "terminal") == 0)
		run_detached(terminal_argv);
	g_free(foot_option);
	g_free(foot_config);
	g_free(terminal);
	g_free(app_run);
	g_free(drawer);
	g_free(scripts);
}

static void
request_weather_fetch(void)
{
	char *scripts;
	char *weather;
	char *argv[3];

	scripts = ctlst_script_path(NULL);
	weather = g_build_filename(scripts, "ctlst-weather", NULL);
	argv[0] = weather;
	argv[1] = "fetch";
	argv[2] = NULL;
	run_detached(argv);
	g_free(weather);
	g_free(scripts);
}

static void
format_weather_temp(char *buffer, size_t size, struct json_object *value)
{
	double number;

	if (value == NULL ||
	    (!json_object_is_type(value, json_type_double) &&
	    !json_object_is_type(value, json_type_int))) {
		g_strlcpy(buffer, "--", size);
		return;
	}
	number = json_object_get_double(value);
	snprintf(buffer, size, "%.0f", number);
}

static gboolean
refresh_weather(gpointer data)
{
	struct home *home = data;
	char *cache_path;
	struct json_object *root = NULL;
	struct json_object *current;
	struct json_object *daily;
	struct json_object *location;
	struct json_object *value;
	char temp_text[16];
	char high_text[16];
	char low_text[16];
	char range_text[48];
	char condition_buf[96];
	const char *condition = "Tap to set location";
	const char *icon_name = "weather-overcast";
	const bool demo = home_demo_enabled();

	if (home->weather_temperature == NULL)
		return G_SOURCE_CONTINUE;
	if (demo) {
		gtk_image_set_from_icon_name(GTK_IMAGE(home->weather_icon),
		    "weather-few-clouds");
		gtk_label_set_text(home->weather_condition, "Partly cloudy");
		gtk_label_set_text(home->weather_range, "H:22°  L:13°");
		gtk_label_set_text(home->weather_temperature, "18°");
		return G_SOURCE_CONTINUE;
	}

	cache_path = g_build_filename(g_get_user_cache_dir(), "ctlst",
	    "weather.json", NULL);
	root = json_object_from_file(cache_path);
	g_free(cache_path);
	if (root != NULL &&
	    json_object_object_get_ex(root, "current", &current) &&
	    json_object_is_type(current, json_type_object)) {
		if (json_object_object_get_ex(current, "temperature", &value))
			format_weather_temp(temp_text, sizeof(temp_text), value);
		else
			g_strlcpy(temp_text, "--", sizeof(temp_text));
		if (json_object_object_get_ex(current, "condition", &value) &&
		    json_object_is_type(value, json_type_string))
			condition = json_object_get_string(value);
		if (json_object_object_get_ex(current, "icon", &value) &&
		    json_object_is_type(value, json_type_string))
			icon_name = json_object_get_string(value);
		if (json_object_object_get_ex(root, "daily", &daily) &&
		    json_object_is_type(daily, json_type_object)) {
			if (json_object_object_get_ex(daily, "high", &value))
				format_weather_temp(high_text,
				    sizeof(high_text), value);
			else
				g_strlcpy(high_text, "--", sizeof(high_text));
			if (json_object_object_get_ex(daily, "low", &value))
				format_weather_temp(low_text, sizeof(low_text),
				    value);
			else
				g_strlcpy(low_text, "--", sizeof(low_text));
		} else {
			g_strlcpy(high_text, "--", sizeof(high_text));
			g_strlcpy(low_text, "--", sizeof(low_text));
		}
		snprintf(range_text, sizeof(range_text), "H:%s°  L:%s°",
		    high_text, low_text);
		if (json_object_object_get_ex(root, "location", &location) &&
		    json_object_is_type(location, json_type_object) &&
		    json_object_object_get_ex(location, "name", &value) &&
		    json_object_is_type(value, json_type_string)) {
			snprintf(condition_buf, sizeof(condition_buf), "%s · %s",
			    json_object_get_string(value), condition);
			condition = condition_buf;
		}
		gtk_image_set_from_icon_name(GTK_IMAGE(home->weather_icon),
		    icon_name);
		gtk_label_set_text(home->weather_condition, condition);
		gtk_label_set_text(home->weather_range, range_text);
		{
			char temperature_label[24];

			snprintf(temperature_label, sizeof(temperature_label),
			    "%s°", temp_text);
			gtk_label_set_text(home->weather_temperature,
			    temperature_label);
		}
	} else {
		gtk_image_set_from_icon_name(GTK_IMAGE(home->weather_icon),
		    "weather-overcast");
		gtk_label_set_text(home->weather_condition,
		    "Tap to set location");
		gtk_label_set_text(home->weather_range, "Open Weather");
		gtk_label_set_text(home->weather_temperature, "--°");
	}
	if (root != NULL)
		json_object_put(root);
	return G_SOURCE_CONTINUE;
}

static GtkWidget *
weather_card(struct home *home)
{
	GtkWidget *card = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	GtkWidget *icon = gtk_image_new_from_icon_name(
	    "weather-overcast");
	GtkWidget *description = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
	GtkWidget *condition = gtk_label_new("Tap to set location");
	GtkWidget *range = gtk_label_new("Open Weather");
	GtkWidget *temperature = gtk_label_new("--°");

	home->weather_icon = icon;
	home->weather_condition = GTK_LABEL(condition);
	home->weather_range = GTK_LABEL(range);
	home->weather_temperature = GTK_LABEL(temperature);
	/* CSS owns full/compact icon sizing and ordinary theme overrides. */
	gtk_image_set_pixel_size(GTK_IMAGE(icon), -1);
	gtk_widget_add_css_class(card, "home-weather-card");
	gtk_widget_add_css_class(icon, "home-weather-icon");
	gtk_widget_add_css_class(condition, "home-weather-detail");
	gtk_widget_add_css_class(range, "home-weather-detail");
	gtk_widget_add_css_class(temperature, "home-weather-temp");
	gtk_label_set_xalign(GTK_LABEL(condition), 0);
	gtk_label_set_xalign(GTK_LABEL(range), 0);
	home_clip_text(GTK_LABEL(condition));
	home_clip_text(GTK_LABEL(range));
	home_clip_box(description);
	gtk_widget_set_hexpand(description, TRUE);
	gtk_widget_set_valign(description, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(temperature, GTK_ALIGN_CENTER);
	gtk_box_append(GTK_BOX(description), condition);
	gtk_box_append(GTK_BOX(description), range);
	gtk_box_append(GTK_BOX(card), icon);
	gtk_box_append(GTK_BOX(card), description);
	gtk_box_append(GTK_BOX(card), temperature);
	home_clip_box(card);
	return card;
}

static GtkWidget *
calendar_card(struct home *home)
{
	static const char *weekday_names[] = {"M", "T", "W", "T", "F", "S", "S"};
	time_t now = time(NULL);
	struct tm today;
	struct tm first;
	char title_text[64];
	GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 7);
	GtkWidget *title;
	GtkWidget *grid = gtk_grid_new();
	GtkWidget *compact;
	GtkWidget *compact_date;
	GtkWidget *compact_weekday;
	int month_days;
	int start_column;

	localtime_r(&now, &today);
	first = today;
	first.tm_mday = 1;
	mktime(&first);
	strftime(title_text, sizeof(title_text), "%B %Y", &today);
	title = gtk_label_new(title_text);
	gtk_label_set_xalign(GTK_LABEL(title), 0);
	gtk_widget_add_css_class(card, "home-calendar-card");
	gtk_widget_add_css_class(title, "home-calendar-title");
	home_clip_text(GTK_LABEL(title));
	home_clip_box(card);
	gtk_box_append(GTK_BOX(card), title);
	home->calendar_month = grid;
	gtk_widget_add_css_class(grid, "home-calendar-month");
	gtk_widget_set_hexpand(grid, TRUE);
	gtk_widget_set_halign(grid, GTK_ALIGN_FILL);
	gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
	gtk_grid_set_row_spacing(GTK_GRID(grid), 2);
	for (int column = 0; column < 7; column++) {
		GtkWidget *label = gtk_label_new(weekday_names[column]);

		gtk_widget_add_css_class(label, "home-calendar-weekday");
		gtk_label_set_xalign(GTK_LABEL(label), 0.5);
		gtk_grid_attach(GTK_GRID(grid), label, column, 0, 1, 1);
	}
	start_column = (first.tm_wday + 6) % 7;
	month_days = 31;
	if (today.tm_mon == 3 || today.tm_mon == 5 || today.tm_mon == 8 ||
	    today.tm_mon == 10)
		month_days = 30;
	else if (today.tm_mon == 1) {
		const int year = today.tm_year + 1900;

		month_days = (year % 4 == 0 &&
		    (year % 100 != 0 || year % 400 == 0)) ? 29 : 28;
	}
	for (int day = 1; day <= month_days; day++) {
		char text[4];
		const int position = start_column + day - 1;
		GtkWidget *label;

		g_snprintf(text, sizeof(text), "%d", day);
		label = gtk_label_new(text);
		gtk_widget_add_css_class(label, "home-calendar-day");
		gtk_label_set_xalign(GTK_LABEL(label), 0.5);
		if (day == today.tm_mday)
			gtk_widget_add_css_class(label, "home-calendar-today");
		gtk_grid_attach(GTK_GRID(grid), label, position % 7,
		    1 + position / 7, 1, 1);
	}
	gtk_box_append(GTK_BOX(card), grid);
	compact = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
	compact_date = gtk_label_new("");
	compact_weekday = gtk_label_new("");
	home->calendar_compact = compact;
	home->calendar_compact_date = GTK_LABEL(compact_date);
	home->calendar_compact_weekday = GTK_LABEL(compact_weekday);
	gtk_widget_add_css_class(compact_date, "home-calendar-compact-date");
	gtk_widget_add_css_class(compact_weekday, "home-calendar-compact-weekday");
	gtk_label_set_xalign(GTK_LABEL(compact_date), 0);
	gtk_label_set_xalign(GTK_LABEL(compact_weekday), 0);
	home_clip_text(home->calendar_compact_date);
	home_clip_text(home->calendar_compact_weekday);
	gtk_box_append(GTK_BOX(compact), compact_date);
	gtk_box_append(GTK_BOX(compact), compact_weekday);
	gtk_widget_set_valign(compact, GTK_ALIGN_CENTER);
	gtk_widget_set_visible(compact, FALSE);
	gtk_box_append(GTK_BOX(card), compact);
	return card;
}

static GtkWidget *
analog_clock_card(struct home *home)
{
	GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	GtkWidget *compact;
	GtkWidget *compact_time;
	GtkWidget *compact_date;

	gtk_widget_add_css_class(card, "home-clock-card");
	home_clip_box(card);
	home->analog_clock = GTK_DRAWING_AREA(gtk_drawing_area_new());
	gtk_widget_add_css_class(GTK_WIDGET(home->analog_clock),
	    "home-analog-clock");
	gtk_drawing_area_set_content_width(home->analog_clock, 96);
	gtk_drawing_area_set_content_height(home->analog_clock, 96);
	gtk_widget_set_hexpand(GTK_WIDGET(home->analog_clock), TRUE);
	gtk_widget_set_vexpand(GTK_WIDGET(home->analog_clock), TRUE);
	gtk_widget_set_halign(GTK_WIDGET(home->analog_clock), GTK_ALIGN_FILL);
	gtk_widget_set_valign(GTK_WIDGET(home->analog_clock), GTK_ALIGN_FILL);
	gtk_drawing_area_set_draw_func(home->analog_clock, draw_analog_clock,
	    home, NULL);
	gtk_box_append(GTK_BOX(card), GTK_WIDGET(home->analog_clock));
	compact = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
	compact_time = gtk_label_new("--:--");
	compact_date = gtk_label_new("");
	home->clock_compact = compact;
	home->clock_compact_time = GTK_LABEL(compact_time);
	home->clock_compact_date = GTK_LABEL(compact_date);
	gtk_widget_add_css_class(compact_time, "home-clock-compact-time");
	gtk_widget_add_css_class(compact_date, "home-clock-compact-date");
	gtk_label_set_xalign(GTK_LABEL(compact_time), 0);
	gtk_label_set_xalign(GTK_LABEL(compact_date), 0);
	home_clip_text(home->clock_compact_time);
	home_clip_text(home->clock_compact_date);
	gtk_box_append(GTK_BOX(compact), compact_time);
	gtk_box_append(GTK_BOX(compact), compact_date);
	gtk_widget_set_valign(compact, GTK_ALIGN_CENTER);
	gtk_widget_set_visible(compact, FALSE);
	gtk_box_append(GTK_BOX(card), compact);
	return card;
}

static GtkWidget *
glance_card(struct home *home)
{
	const bool demo = home_demo_enabled();
	GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
	GtkWidget *title = gtk_label_new("At a Glance");
	GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	GtkWidget *dot = gtk_label_new("◇");
	GtkWidget *copy = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	GtkWidget *event = gtk_label_new(demo ? "Team sync" :
	    "Calendar not connected");
	GtkWidget *when = gtk_label_new(demo ? "10:00 AM" :
	    "Event feed is not configured");
	GtkWidget *device = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

	home->glance_title = title;
	home->network_context = GTK_LABEL(gtk_label_new("Offline"));
	home->battery_context = GTK_LABEL(gtk_label_new("--%"));
	gtk_widget_add_css_class(card, "home-glance-card");
	gtk_widget_add_css_class(title, "home-glance-title");
	gtk_widget_add_css_class(dot, "home-glance-dot");
	gtk_widget_add_css_class(event, "home-glance-event");
	gtk_widget_add_css_class(when, "home-glance-meta");
	gtk_widget_add_css_class(GTK_WIDGET(home->network_context),
	    "home-glance-meta");
	gtk_widget_add_css_class(GTK_WIDGET(home->battery_context),
	    "home-glance-meta");
	gtk_label_set_xalign(GTK_LABEL(title), 0);
	gtk_label_set_xalign(GTK_LABEL(event), 0);
	gtk_label_set_xalign(GTK_LABEL(when), 0);
	home_clip_text(GTK_LABEL(title));
	home_clip_text(GTK_LABEL(event));
	home_clip_text(GTK_LABEL(when));
	home_clip_text(home->network_context);
	home_clip_text(home->battery_context);
	gtk_widget_set_valign(dot, GTK_ALIGN_CENTER);
	gtk_widget_set_halign(dot, GTK_ALIGN_CENTER);
	home_clip_box(copy);
	gtk_widget_set_hexpand(copy, TRUE);
	gtk_box_append(GTK_BOX(copy), event);
	gtk_box_append(GTK_BOX(copy), when);
	gtk_box_append(GTK_BOX(row), dot);
	gtk_box_append(GTK_BOX(row), copy);
	home_clip_box(row);
	gtk_box_append(GTK_BOX(device), GTK_WIDGET(home->network_context));
	gtk_box_append(GTK_BOX(device), GTK_WIDGET(home->battery_context));
	home_clip_box(device);
	home_clip_box(card);
	gtk_box_append(GTK_BOX(card), title);
	gtk_box_append(GTK_BOX(card), row);
	gtk_box_append(GTK_BOX(card), device);
	return card;
}

static GtkWidget *
activity_row(const char *initial, GtkLabel **value, GtkProgressBar **meter,
    const char *meter_style)
{
	GtkWidget *row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);

	*value = GTK_LABEL(gtk_label_new(initial));
	gtk_label_set_xalign(*value, 0);
	gtk_widget_add_css_class(GTK_WIDGET(*value), "home-activity-value");
	*meter = GTK_PROGRESS_BAR(gtk_progress_bar_new());
	gtk_widget_add_css_class(GTK_WIDGET(*meter), "home-activity-meter");
	if (meter_style != NULL)
		gtk_widget_add_css_class(GTK_WIDGET(*meter), meter_style);
	gtk_widget_set_hexpand(GTK_WIDGET(*meter), TRUE);
	gtk_box_append(GTK_BOX(row), GTK_WIDGET(*value));
	gtk_box_append(GTK_BOX(row), GTK_WIDGET(*meter));
	return row;
}

static GtkWidget *
performance_tile(struct home *home)
{
	GtkWidget *tile = gtk_box_new(GTK_ORIENTATION_VERTICAL, 7);
	GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 7);
	GtkWidget *dot = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	GtkWidget *heading = gtk_label_new("Performance");
	GtkWidget *metrics = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
	GtkWidget *cpu = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	GtkWidget *memory;

	gtk_widget_add_css_class(tile, "home-context-tile");
	gtk_widget_add_css_class(tile, "home-context-performance");
	gtk_widget_add_css_class(dot, "home-context-dot");
	gtk_widget_set_valign(dot, GTK_ALIGN_CENTER);
	gtk_widget_add_css_class(heading, "home-context-title");
	gtk_label_set_xalign(GTK_LABEL(heading), 0);
	gtk_box_append(GTK_BOX(header), dot);
	gtk_box_append(GTK_BOX(header), heading);
	home->cpu_context = GTK_LABEL(gtk_label_new("CPU sampling"));
	gtk_label_set_xalign(home->cpu_context, 0);
	gtk_widget_add_css_class(GTK_WIDGET(home->cpu_context),
	    "home-activity-value");
	gtk_box_append(GTK_BOX(cpu), GTK_WIDGET(home->cpu_context));
	memory = activity_row("RAM -- / --", &home->memory_context,
	    &home->memory_meter, "home-memory-meter");
	gtk_widget_set_hexpand(cpu, TRUE);
	gtk_widget_set_hexpand(memory, TRUE);
	gtk_box_append(GTK_BOX(metrics), cpu);
	gtk_box_append(GTK_BOX(metrics), memory);
	gtk_box_append(GTK_BOX(tile), header);
	gtk_box_append(GTK_BOX(tile), metrics);
	gtk_widget_set_hexpand(tile, TRUE);
	return tile;
}

static void
cancel_launcher_push_dwell(struct home *home)
{
	if (home->launcher_push_dwell_source != 0) {
		g_source_remove(home->launcher_push_dwell_source);
		home->launcher_push_dwell_source = 0;
	}
	home->launcher_push_target_column = -1;
	home->launcher_push_target_row = -1;
	home->launcher_push_dwell_ready = false;
}

static void
cancel_page_edge_dwell(struct home *home)
{
	if (home->page_edge_dwell_source != 0) {
		g_source_remove(home->page_edge_dwell_source);
		home->page_edge_dwell_source = 0;
	}
	home->page_edge_direction = 0;
	home->page_edge_haptic_sent = false;
}

static gboolean
page_edge_dwell_fire(gpointer data)
{
	struct home *home = data;
	int direction = home->page_edge_direction;
	int new_page;

	home->page_edge_dwell_source = 0;
	home->page_edge_direction = 0;
	home->page_edge_haptic_sent = false;
	if (!home->layout_editing || direction == 0 ||
	    home->active_launcher_drag == NULL)
		return G_SOURCE_REMOVE;
	new_page = home->launcher_current_page + direction;
	if (new_page < 0 || new_page >= home->launcher_page_count)
		return G_SOURCE_REMOVE;
	{
		struct home_launcher_icon *icon = home->active_launcher_drag;
		struct home_launcher_icon *occupant;
		int target_column = direction < 0 ? home_cols(home) - 1 : 0;
		int target_row = CLAMP(icon->row, 0, home_rows(home) - 1);
		double cell_width;
		double cell_height;

		/*
		 * Instant page switch while dragging: CSS-settle leaves hit
		 * testing on page 0 and made drops look like removes.
		 */
		abort_page_settle(home);
		set_launcher_page(home, new_page);
		icon->page = new_page;
		occupant = launcher_icon_at_cell(home, new_page, target_column,
		    target_row, icon);
		if (occupant != NULL &&
		    !launcher_push_icon_aside(home, occupant, icon)) {
			if (!launcher_find_free_cell(home, new_page,
			    &target_column, &target_row)) {
				target_column = icon->column;
				target_row = icon->row;
			}
		}
		icon->column = target_column;
		icon->row = target_row;
		home->drag_preview_column = target_column;
		home->drag_preview_row = target_row;
		home->launcher_drag_start_column = target_column;
		home->launcher_drag_start_row = target_row;
		if (grid_cell_size(home, &cell_width, &cell_height)) {
			int column_spacing =
			    gtk_grid_get_column_spacing(home->context_grid);
			int row_spacing =
			    gtk_grid_get_row_spacing(home->context_grid);

			home->launcher_drag_anchor_gx = target_column *
			    (cell_width + column_spacing) + cell_width * 0.5;
			home->launcher_drag_anchor_gy = target_row *
			    (cell_height + row_spacing) + cell_height * 0.5;
		}
		/* Page changed even when column/row stayed put — must reattach. */
		relayout_launcher_icons(home);
		save_drag_layout_snapshot(home);
		home->launcher_press_x = home->last_drag_overlay_x -
		    home->launcher_drag_offset_x;
		home->launcher_press_y = home->last_drag_overlay_y -
		    home->launcher_drag_offset_y;
		set_launcher_remove_armed(home, false);
	}
	return G_SOURCE_REMOVE;
}

static void
check_drag_page_edge(struct home *home, double ox, double oy)
{
	int width;
	int direction = 0;

	(void)oy;

	if (!home->layout_editing || home->resize_drag)
		return;
	/* Widgets stay on page 0; only launcher icons cross home screens. */
	if (home->active_launcher_drag == NULL)
		return;
	width = gtk_widget_get_width(GTK_WIDGET(home->context_overlay));
	if (width <= 0)
		return;
	if (ox <= home->config.page_edge_px &&
	    home->launcher_current_page > 0)
		direction = -1;
	else if (ox >= width - home->config.page_edge_px &&
	    home->launcher_current_page < home->launcher_page_count - 1)
		direction = 1;
	if (direction == 0) {
		cancel_page_edge_dwell(home);
		return;
	}
	if (home->page_edge_direction != direction) {
		cancel_page_edge_dwell(home);
		home->page_edge_direction = direction;
		pulse_layout_edit_haptic(home);
		home->page_edge_haptic_sent = true;
		home->page_edge_dwell_source = g_timeout_add(
		    home->config.page_edge_dwell_ms, page_edge_dwell_fire, home);
	}
}

static void
apply_launcher_icon_move(struct home *home, struct home_launcher_icon *icon,
    int target_column, int target_row)
{
	struct home_launcher_icon *occupant;
	int page;

	if (icon == NULL)
		return;
	if (target_column == icon->column && target_row == icon->row)
		return;
	page = icon->page;
	/* Widgets own their cells; icons never land under a card. */
	if (launcher_cell_blocked_by_widget(home, page, target_column,
	    target_row))
		return;
	occupant = launcher_icon_at_cell(home, page, target_column, target_row,
	    icon);
	if (occupant != NULL) {
		/*
		 * Instant push: displace the occupant to the next free seat
		 * (prefer down, then up). The dragged icon keeps its origin
		 * for offset math; only the occupant relocates.
		 */
		if (!launcher_push_icon_aside(home, occupant, icon))
			return;
	}
	icon->column = target_column;
	icon->row = target_row;
	cancel_launcher_push_dwell(home);
	relayout_launcher_icons(home);
}

static void
layout_outside_released(GtkGestureClick *gesture, int presses, double x, double y,
    gpointer data)
{
	struct home *home = data;
	GtkWidget *target;

	(void)gesture;
	(void)presses;
	if (!home->layout_editing)
		return;
	target = gtk_widget_pick(home->root, x, y, GTK_PICK_DEFAULT);
	for (size_t i = 0; i < home->grid_item_count; i++) {
		if (target == home->grid_items[i].container ||
		    (target != NULL && gtk_widget_is_ancestor(target,
		    home->grid_items[i].container)))
			return;
	}
	if (home->launcher_page_bar != NULL &&
	    (target == home->launcher_page_bar ||
	    (target != NULL && gtk_widget_is_ancestor(target,
	    home->launcher_page_bar))))
		return;
	if (home->edit_toolbar != NULL &&
	    (target == home->edit_toolbar ||
	    (target != NULL && gtk_widget_is_ancestor(target,
	    home->edit_toolbar))))
		return;
	if (home->widget_picker != NULL &&
	    (target == home->widget_picker ||
	    (target != NULL && gtk_widget_is_ancestor(target,
	    home->widget_picker))))
		return;
	if (home->launcher_icons != NULL) {
		for (size_t i = 0; i < home->launcher_icons->len; i++) {
			struct home_launcher_icon *icon =
			    g_ptr_array_index(home->launcher_icons, i);

			if (icon == NULL || icon->button == NULL)
				continue;
			if (target == icon->button ||
			    (target != NULL &&
			    gtk_widget_is_ancestor(target, icon->button)))
				return;
		}
	}
	set_layout_editing(home, false);
}

static void
remove_favorite(struct home *home, const char *favorite)
{
	char *preferences = ctlst_script_path("ctlst-preferences");
	char *argv[] = {preferences, "favorites", "remove", (char *)favorite, NULL};
	char *output = run_capture(argv);

	g_free(output);
	g_free(preferences);
	refresh_launcher_icons(home);
	save_home_layout(home);
}

static bool
point_in_edit_controls(struct home *home, double ox, double oy)
{
	graphene_point_t in = GRAPHENE_POINT_INIT(ox, oy), out;
	if (!home->layout_editing || home->page_viewport == NULL)
		return false;
	return gtk_widget_compute_point(GTK_WIDGET(home->context_overlay),
	    home->page_viewport, &in, &out) &&
	    out.y > gtk_widget_get_height(home->page_viewport);
}

static bool
launcher_point_is_remove(struct home *home, double ox, double oy)
{
	int height;
	int width;

	if (home->context_overlay == NULL)
		return false;
	/* The edit toolbar is not a removal target. */
	if (point_in_edit_controls(home, ox, oy))
		return false;
	height = gtk_widget_get_height(GTK_WIDGET(home->context_overlay));
	width = gtk_widget_get_width(GTK_WIDGET(home->context_overlay));
	if (height <= 0)
		return false;
	/* Top strip: drag up to uninstall from Home (iOS-style). */
	if (oy < (double)height * home->config.drag_remove_zone_fraction)
		return true;
	/*
	 * Horizontal page-edge holds move icons between home screens. The old
	 * "outside page-0 grid ⇒ remove" check treated those edge holds (and
	 * any drop on page ≥ 1) as uninstall and deleted the favorite.
	 */
	if (width > 0 && (ox <= home->config.page_edge_px ||
	    ox >= (double)width - home->config.page_edge_px))
		return false;
	/* Fling below the pager toward the dock still removes. */
	if (home->page_viewport != NULL) {
		graphene_point_t in = { (float)ox, (float)oy };
		graphene_point_t out;
		int viewport_height;

		if (gtk_widget_compute_point(GTK_WIDGET(home->context_overlay),
		    home->page_viewport, &in, &out)) {
			viewport_height = gtk_widget_get_height(home->page_viewport);
			if (viewport_height > 0 && out.y > (double)viewport_height + 24.0)
				return true;
			if (out.y >= -24.0 && out.y <= (double)viewport_height + 24.0)
				return false;
		}
	}
	return false;
}

static void
set_launcher_remove_armed(struct home *home, bool armed)
{
	struct home_launcher_icon *icon = home->active_launcher_drag;

	if (home->launcher_remove_armed == armed)
		return;
	home->launcher_remove_armed = armed;
	if (home->launcher_remove_banner != NULL) {
		gtk_label_set_text(GTK_LABEL(home->launcher_remove_banner),
		    armed ? "Release to remove" : "Drag here to remove");
		gtk_widget_set_visible(home->launcher_remove_banner, armed);
	}
	if (icon != NULL && icon->button != NULL) {
		if (armed)
			gtk_widget_add_css_class(icon->button, "removing");
		else
			gtk_widget_remove_css_class(icon->button, "removing");
	}
	if (armed) {
		cancel_page_edge_dwell(home);
		pulse_layout_edit_haptic(home);
	}
}

static bool
preferences_add_favorite(const char *desktop_id)
{
	char *preferences = ctlst_script_path("ctlst-preferences");
	char *argv[] = {preferences, "favorites", "add", (char *)desktop_id, NULL};
	char *output = run_capture(argv);
	bool ok = output != NULL;

	g_free(output);
	g_free(preferences);
	return ok;
}

static bool
native_to_overlay_point(struct home *home, double nx, double ny, double *ox,
    double *oy)
{
	GtkNative *native;
	graphene_point_t in = { (float)nx, (float)ny };
	graphene_point_t out;

	if (home->root == NULL || home->context_overlay == NULL)
		return false;
	native = gtk_widget_get_native(home->root);
	if (native == NULL)
		return false;
	if (!gtk_widget_compute_point(GTK_WIDGET(native),
	    GTK_WIDGET(home->context_overlay), &in, &out))
		return false;
	*ox = out.x;
	*oy = out.y;
	return true;
}

static bool
find_place_cell(struct home *home, int *page_out, int *column_out, int *row_out)
{
	int page;
	int column;
	int row;

	page = home->launcher_current_page;
	if (launcher_find_free_cell(home, page, &column, &row)) {
		*page_out = page;
		*column_out = column;
		*row_out = row;
		return true;
	}
	for (page = 0; page < home->launcher_page_count; page++) {
		if (launcher_find_free_cell(home, page, &column, &row)) {
			*page_out = page;
			*column_out = column;
			*row_out = row;
			return true;
		}
	}
	if (home->launcher_page_count < home->config.page_max_pages) {
		home->launcher_page_count += 1;
		*page_out = home->launcher_page_count - 1;
		*column_out = 0;
		*row_out = 0;
		return true;
	}
	return false;
}

static void
place_launcher_icon_from_drop(struct home *home, const char *desktop_id,
    int page, int column, int row)
{
	struct home_launcher_icon *icon;

	if (desktop_id == NULL || desktop_id[0] == '\0')
		return;
	if (g_strcmp0(desktop_id, "ctlst:apps") == 0)
		return;
	if (launcher_icon_for_desktop_id(home, desktop_id) == NULL) {
		char *preferences = ctlst_script_path("ctlst-preferences");
		char *argv[] = {preferences, "favorites", "list", NULL};
		char *output = run_capture(argv);
		struct json_object *favorites = json_tokener_parse(output);
		bool listed = favorites != NULL &&
		    json_object_is_type(favorites, json_type_array);

		if (!listed || !favorite_list_contains(favorites, desktop_id))
			(void)preferences_add_favorite(desktop_id);
		if (favorites != NULL)
			json_object_put(favorites);
		g_free(output);
		g_free(preferences);
	}
	refresh_launcher_icons(home);
	icon = launcher_icon_for_desktop_id(home, desktop_id);
	if (icon == NULL)
		return;
	page = CLAMP(page, 0, home->launcher_page_count - 1);
	icon->page = page;
	if (!launcher_cell_occupied_except(home, page, column, row, icon, NULL)) {
		icon->column = column;
		icon->row = row;
	} else if (launcher_find_free_cell(home, page, &column, &row)) {
		icon->column = column;
		icon->row = row;
	} else if (launcher_find_free_cell(home, home->launcher_current_page,
	    &column, &row)) {
		icon->page = home->launcher_current_page;
		icon->column = column;
		icon->row = row;
	} else {
		int free_page = page;

		if (!find_place_cell(home, &free_page, &column, &row))
			return;
		icon->page = free_page;
		icon->column = column;
		icon->row = row;
	}
	if (!home->visible)
		set_visible(home, true);
	set_launcher_page(home, icon->page);
	render_launcher_page(home);
	save_home_layout(home);
}

static void
handle_place_drop(struct home *home, const char *message)
{
	double nx;
	double ny;
	char desktop_id[256];
	double ox;
	double oy;
	double gx;
	double gy;
	int page;
	int column = 0;
	int row = 0;
	bool have_cell = false;

	/*
	 * Drawer drop messages use the drawer's surface coordinates. Dragging
	 * down to dismiss often lands outside Home's icon grid, so treat the
	 * point as a hint and always fall back to a free cell.
	 */
	if (sscanf(message, "P%lf,%lf,%255s", &nx, &ny, desktop_id) != 3)
		return;
	page = home->launcher_current_page;
	if (native_to_overlay_point(home, nx, ny, &ox, &oy) &&
	    overlay_to_grid_point(home, ox, oy, &gx, &gy) &&
	    grid_point_to_cell(home, gx, gy, &column, &row))
		have_cell = true;
	if (!have_cell && !find_place_cell(home, &page, &column, &row))
		return;
	place_launcher_icon_from_drop(home, desktop_id, page, column, row);
}

static char *
favorite_action(const char *favorite)
{
	if (g_strcmp0(favorite, "ctlst:apps") == 0)
		return g_strdup("apps");
	if (g_strcmp0(favorite, "dev.ctlst.Dialer.desktop") == 0)
		return g_strdup("phone");
	if (g_strcmp0(favorite, "dev.ctlst.Messages.desktop") == 0)
		return g_strdup("messages");
	if (g_strcmp0(favorite, "foot.desktop") == 0 ||
	    g_strcmp0(favorite, "dev.ctlst.Terminal.desktop") == 0)
		return g_strdup("terminal");
	return g_strdup_printf("desktop:%s", favorite);
}

static GAppInfo *
find_favorite_app(const char *desktop_id)
{
	GList *apps = g_app_info_get_all();
	GAppInfo *match = NULL;

	for (GList *item = apps; item != NULL; item = item->next) {
		GAppInfo *app = item->data;

		if (g_strcmp0(g_app_info_get_id(app), desktop_id) == 0) {
			match = g_object_ref(app);
			break;
		}
	}
	g_list_free_full(apps, g_object_unref);
	return match;
}

static GIcon *
favorite_icon_for_id(const char *favorite)
{
	GAppInfo *app;
	GIcon *icon;

	if (g_strcmp0(favorite, "ctlst:apps") == 0) {
		GtkIconTheme *theme = gtk_icon_theme_get_for_display(gdk_display_get_default());
		/* Check the full theme inheritance chain first: a generic fallback
		 * in the selected theme must not displace our preferred hicolor icon. */
		return g_themed_icon_new(gtk_icon_theme_has_icon(theme, "dev.ctlst.Apps")
		    ? "dev.ctlst.Apps" : "view-app-grid-symbolic");
	}
	app = find_favorite_app(favorite);
	if (app == NULL)
		return g_themed_icon_new("application-x-executable-symbolic");
	icon = g_object_ref(g_app_info_get_icon(app));
	g_object_unref(app);
	return icon;
}

static struct home_launcher_icon *
launcher_icon_for_desktop_id(struct home *home, const char *desktop_id)
{
	for (size_t i = 0; i < home->launcher_icons->len; i++) {
		struct home_launcher_icon *icon =
		    g_ptr_array_index(home->launcher_icons, i);

		if (icon != NULL && icon->desktop_id != NULL &&
		    g_strcmp0(icon->desktop_id, desktop_id) == 0)
			return icon;
	}
	return NULL;
}

static bool
favorite_list_contains(struct json_object *favorites, const char *favorite)
{
	for (size_t i = 0; i < json_object_array_length(favorites); i++) {
		struct json_object *entry = json_object_array_get_idx(favorites, i);

		if (json_object_is_type(entry, json_type_string) &&
		    g_strcmp0(json_object_get_string(entry), favorite) == 0)
			return true;
	}
	return false;
}

static GtkWidget *
launcher_icon_button(struct home *home, GIcon *app_icon, const char *desktop_id)
{
	GtkWidget *button = gtk_button_new();
	GtkWidget *disc = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	GtkWidget *icon = favorite_icon_image(app_icon, 28);
	GtkWidget *remove_badge = gtk_label_new("×");
	GtkWidget *icon_host = home_launcher_host_new(disc, remove_badge);
	GtkGesture *remove_click = gtk_gesture_click_new();
	char *action = favorite_action(desktop_id);

	gtk_widget_add_css_class(button, "home-launcher-icon");
	gtk_widget_add_css_class(disc, "home-launcher-icon-disc");
	/*
	 * Touch/click only. Keep icons out of GTK directional keynav so arrow
	 * keys never walk the launcher grid — especially while the prompt has
	 * EXCLUSIVE keyboard, and so a stray focus cannot steal arrows when
	 * Home is not the active typing surface (KEYBOARD_MODE_NONE).
	 */
	gtk_widget_set_focusable(button, FALSE);
	gtk_widget_set_halign(disc, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(disc, GTK_ALIGN_CENTER);
	gtk_widget_set_hexpand(disc, FALSE);
	gtk_widget_set_vexpand(disc, FALSE);
	gtk_widget_set_halign(icon, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
	gtk_widget_set_hexpand(icon, TRUE);
	gtk_widget_set_vexpand(icon, TRUE);
	gtk_box_append(GTK_BOX(disc), icon);
	/* The cell owns the remove corner; artwork fits the remaining edit area. */
	gtk_widget_set_hexpand(icon_host, TRUE);
	gtk_widget_set_vexpand(icon_host, TRUE);
	gtk_widget_set_halign(icon_host, GTK_ALIGN_FILL);
	gtk_widget_set_valign(icon_host, GTK_ALIGN_FILL);
	gtk_widget_add_css_class(remove_badge, "home-launcher-remove-badge");
	gtk_widget_set_halign(remove_badge, GTK_ALIGN_END);
	gtk_widget_set_valign(remove_badge, GTK_ALIGN_START);
	gtk_widget_set_can_target(remove_badge, false);
	gtk_widget_set_visible(remove_badge, false);
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(remove_click),
	    GDK_BUTTON_PRIMARY);
	gtk_gesture_single_set_exclusive(GTK_GESTURE_SINGLE(remove_click), true);
	gtk_event_controller_set_propagation_phase(
	    GTK_EVENT_CONTROLLER(remove_click), GTK_PHASE_CAPTURE);
	g_signal_connect(remove_click, "released",
	    G_CALLBACK(launcher_remove_badge_clicked), button);
	gtk_widget_add_controller(remove_badge,
	    GTK_EVENT_CONTROLLER(remove_click));
	gtk_button_set_child(GTK_BUTTON(button), icon_host);
	g_signal_connect(button, "clicked", G_CALLBACK(launcher_icon_clicked), home);
	g_object_set_data_full(G_OBJECT(button), "favorite-icon",
	    g_object_ref(app_icon), g_object_unref);
	g_object_set_data_full(G_OBJECT(button), "action", action, g_free);
	g_object_set_data_full(G_OBJECT(button), "favorite-id",
	    g_strdup(desktop_id), g_free);
	g_object_set_data(G_OBJECT(button), "launcher-remove-badge", remove_badge);
	gtk_widget_set_hexpand(button, TRUE);
	gtk_widget_set_vexpand(button, TRUE);
	gtk_widget_set_halign(button, GTK_ALIGN_FILL);
	gtk_widget_set_valign(button, GTK_ALIGN_FILL);
	return button;
}

static void
refresh_launcher_icon_editing(struct home *home)
{
	if (home->launcher_icons == NULL)
		return;
	for (size_t i = 0; i < home->launcher_icons->len; i++) {
		struct home_launcher_icon *icon =
		    g_ptr_array_index(home->launcher_icons, i);

		if (icon == NULL || icon->button == NULL)
			continue;
		{
			GtkWidget *remove_badge = g_object_get_data(
			    G_OBJECT(icon->button), "launcher-remove-badge");

			if (remove_badge != NULL)
				gtk_widget_set_visible(remove_badge,
				    home->layout_editing &&
				    icon != home->active_launcher_drag);
			if (remove_badge != NULL)
				gtk_widget_set_can_target(remove_badge,
				    home->layout_editing &&
				    icon != home->active_launcher_drag);
		}
		/*
		 * Keep buttons non-targetable while a rearrange is armed so the
		 * overlay drag owns the sequence; otherwise leave them targetable
		 * as a launch fallback when the drag gesture does not claim.
		 */
		gtk_widget_set_can_target(icon->button,
		    !home->layout_editing || home->active_launcher_drag == NULL);
		if (home->layout_editing && icon == home->active_launcher_drag)
			gtk_widget_add_css_class(icon->button, "editing");
		else
			gtk_widget_remove_css_class(icon->button, "editing");
	}
}

static void
launch_launcher_icon(struct home *home, struct home_launcher_icon *icon)
{
	if (icon == NULL || icon->desktop_id == NULL || icon->button == NULL)
		return;
	if (home->launcher_suppress_click ||
	    home->active_launcher_drag != NULL || home->launcher_drag_armed)
		return;
	home->launcher_suppress_click = true;
	/* Short tap always launches, even if a prior rearrange left edit on. */
	if (home->layout_editing)
		set_layout_editing(home, false);
	quick_action(GTK_BUTTON(icon->button), NULL);
}

static void
launcher_icon_clicked(GtkButton *button, gpointer data)
{
	struct home *home = data;
	const char *favorite = g_object_get_data(G_OBJECT(button), "favorite-id");

	/* Drag path launches short presses; this covers pure clicks. */
	if (favorite == NULL || home->page_release_suppress ||
	    home->launcher_suppress_click || home->layout_editing ||
	    home->launcher_drag_armed || home->active_launcher_drag != NULL)
		return;
	launch_launcher_icon(home,
	    launcher_icon_for_desktop_id(home, favorite));
}

static void
launcher_remove_badge_clicked(GtkGestureClick *gesture, int n_press, double x,
    double y, gpointer data)
{
	GtkWidget *button = data;
	struct home_launcher_icon *icon = g_object_get_data(G_OBJECT(button),
	    "launcher-icon");
	struct home *home = icon != NULL ? icon->home : NULL;
	char *desktop_id;

	(void)n_press;
	(void)x;
	(void)y;
	if (home == NULL || !home->layout_editing || icon->desktop_id == NULL)
		return;
	/* Own the badge tap before the enclosing launcher button can turn it into
	 * activation. The duplicated id survives refresh destroying this view. */
	gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
	desktop_id = g_strdup(icon->desktop_id);
	home->launcher_suppress_click = true;
	home->pending_launcher_press = NULL;
	remove_favorite(home, desktop_id);
	pulse_layout_edit_haptic(home);
	g_free(desktop_id);
}

static void
cancel_launcher_drag_arm(struct home *home)
{
	if (home->launcher_drag_arm_source != 0) {
		g_source_remove(home->launcher_drag_arm_source);
		home->launcher_drag_arm_source = 0;
	}
	if (!home->launcher_drag_armed)
		home->pending_launcher_press = NULL;
	home->launcher_drag_armed = false;
}

static gboolean
launcher_drag_arm_fire(gpointer data)
{
	struct home *home = data;
	struct home_launcher_icon *icon = home->pending_launcher_press;

	home->launcher_drag_arm_source = 0;
	if (icon == NULL)
		return G_SOURCE_REMOVE;
	home->launcher_drag_armed = true;
	home->selected_grid_item = NULL;
	if (!home->layout_editing)
		set_layout_editing(home, true);
	start_launcher_icon_drag(home, icon);
	return G_SOURCE_REMOVE;
}

static void
home_launcher_long_pressed(GtkGestureLongPress *gesture, double x, double y,
    gpointer data)
{
	struct home *home = data;
	struct home_launcher_icon *icon;
	double local_x;
	double local_y;

	/* Backup path if long-press wins over drag arm timer. */
	icon = home_launcher_icon_at(home, x, y);
	if (icon == NULL) {
		/* Empty wallpaper/cell holds enter edit mode too. Widget cards own
		 * their own long-press controller, so do not steal those sequences. */
		if (home_grid_item_at(home, x, y, &local_x, &local_y) != NULL)
			return;
		cancel_launcher_drag_arm(home);
		home->launcher_suppress_click = true;
		home->selected_grid_item = NULL;
		set_layout_editing(home, true);
		gtk_gesture_set_state(GTK_GESTURE(gesture),
		    GTK_EVENT_SEQUENCE_CLAIMED);
		return;
	}
	cancel_launcher_drag_arm(home);
	home->pending_launcher_press = icon;
	home->launcher_press_x = x;
	home->launcher_press_y = y;
	home->launcher_drag_armed = true;
	home->selected_grid_item = NULL;
	if (!home->layout_editing)
		set_layout_editing(home, true);
	start_launcher_icon_drag(home, icon);
	home->launcher_suppress_click = true;
	/* Claiming synchronously ends grouped gestures, so publish all drag/edit
	 * state first or their drag-end path mistakes this hold for a short tap. */
	gtk_gesture_set_state(GTK_GESTURE(gesture),
	    GTK_EVENT_SEQUENCE_CLAIMED);
}

static void
home_context_pressed(GtkGestureClick *gesture, int n_press, double x, double y,
    gpointer data)
{
	struct home *home = data;
	struct home_launcher_icon *icon;

	(void)gesture;
	(void)n_press;
	/* Claimed gestures can prevent or reorder release callbacks. Clear their
	 * one-sequence launch suppression only at the next independent press. */
	home->page_release_suppress = false;
	home->launcher_suppress_click = false;
	/*
	 * GtkGestureDrag only emits drag-begin after movement, so a still
	 * long-press never armed edit. Start the arm timer on button press.
	 */
	if (home->layout_editing || home->active_launcher_drag != NULL)
		return;
	icon = home_launcher_icon_at(home, x, y);
	if (icon == NULL)
		return;
	cancel_launcher_drag_arm(home);
	home->pending_launcher_press = icon;
	home->launcher_press_x = x;
	home->launcher_press_y = y;
	home->launcher_drag_armed = false;
	home->launcher_drag_arm_source = g_timeout_add(
	    home->config.drag_icon_arm_ms, launcher_drag_arm_fire, home);
}

static void
home_context_icon_clicked(GtkGestureClick *gesture, int n_press, double x,
    double y, gpointer data)
{
	struct home *home = data;
	struct home_launcher_icon *icon;
	struct home_launcher_icon *pending = home->pending_launcher_press;
	bool armed = home->launcher_drag_armed;

	(void)gesture;
	(void)n_press;
	if (home->page_release_suppress) {
		home->page_release_suppress = false;
		return;
	}
	if (home->active_launcher_drag != NULL) {
		end_launcher_icon_drag(home, x - home->launcher_press_x,
		    y - home->launcher_press_y);
		home->pending_launcher_press = NULL;
		return;
	}
	if (home->layout_editing) {
		icon = launcher_remove_badge_at(home, x, y);
		if (icon != NULL) {
			char *desktop_id = g_strdup(icon->desktop_id);

			cancel_launcher_drag_arm(home);
			home->pending_launcher_press = NULL;
			home->launcher_suppress_click = true;
			remove_favorite(home, desktop_id);
			pulse_layout_edit_haptic(home);
			g_free(desktop_id);
		}
		return;
	}
	cancel_launcher_drag_arm(home);
	home->pending_launcher_press = NULL;
	if (home->launcher_suppress_click)
		return;
	if (pending != NULL && hypot(x - home->launcher_press_x,
	    y - home->launcher_press_y) >= HOME_TAP_SLOP)
		return;
	/* Short-tap remove-in-edit was fighting launch; launch only. */
	if (home->layout_editing || armed)
		return;
	icon = pending != NULL ? pending : home_launcher_icon_at(home, x, y);
	if (icon == NULL)
		return;
	launch_launcher_icon(home, icon);
}

static bool
overlay_to_grid_point(struct home *home, double ox, double oy, double *gx,
    double *gy)
{
	graphene_point_t in = { (float)ox, (float)oy };
	graphene_point_t out;
	GtkWidget *panel;
	int page;

	if (home->context_overlay == NULL)
		return false;
	/*
	 * Map into the visible page panel. Page 0's panel is context_body
	 * (cluster + strip); later pages are full 5×7 icon grids. Mapping to
	 * context_grid alone broke drops on page ≥ 1.
	 */
	page = home->launcher_current_page;
	if (page < 0 || page >= home->page_panel_count)
		page = 0;
	panel = home->page_panels[page];
	if (panel == NULL)
		panel = home->context_body != NULL ? home->context_body :
		    GTK_WIDGET(home->context_grid);
	if (panel == NULL)
		return false;
	if (!gtk_widget_compute_point(GTK_WIDGET(home->context_overlay),
	    panel, &in, &out))
		return false;
	*gx = out.x;
	*gy = out.y;
	if (page == 0) {
		/* Convert the displayed cluster/side strip to portrait-logical space.
		 * This also handles fractional edit zoom through compute_point. */
		double cw, ch;
		if (!grid_cell_size(home, &cw, &ch))
			return false;
		for (int strip = 0; strip <= 1; strip++) {
			GtkGrid *grid = strip ? home->strip_grid : home->context_grid;
			int cols = strip && home->landscape ? home_strip_rows(home) : home_cols(home);
			int rows = strip ? (home->landscape ? home_cols(home) : home_strip_rows(home)) : home_cluster_rows(home);
			graphene_point_t local;
			if (!gtk_widget_compute_point(GTK_WIDGET(home->context_overlay),
			    GTK_WIDGET(grid), &in, &local))
				continue;
			int w = gtk_widget_get_width(GTK_WIDGET(grid));
			int h = gtk_widget_get_height(GTK_WIDGET(grid));
			if (local.x < 0 || local.y < 0 || local.x >= w || local.y >= h)
				continue;
			double vx = local.x / ((w + gtk_grid_get_column_spacing(grid)) / (double)cols);
			double vy = local.y / ((h + gtk_grid_get_row_spacing(grid)) / (double)rows);
			*gx = (strip && home->landscape ? vy : vx) *
			    (cw + gtk_grid_get_column_spacing(home->context_grid));
			*gy = ((strip ? home_cluster_rows(home) : 0) +
			    (strip && home->landscape ? vx : vy)) *
			    (ch + gtk_grid_get_row_spacing(home->context_grid));
			return true;
		}
		return false;
	}
	return true;
}

static bool
grid_point_to_cell(struct home *home, double gx, double gy, int *column_out,
    int *row_out)
{
	double cell_width;
	double cell_height;
	int column_spacing;
	int row_spacing;
	double step_x;
	double step_y;
	int column;
	int row;
	double local_x;
	double local_y;

	if (!grid_cell_size(home, &cell_width, &cell_height))
		return false;
	column_spacing = gtk_grid_get_column_spacing(home->context_grid);
	row_spacing = gtk_grid_get_row_spacing(home->context_grid);
	step_x = cell_width + column_spacing;
	step_y = cell_height + row_spacing;
	if (step_x <= 0.0 || step_y <= 0.0 || gx < 0.0 || gy < 0.0)
		return false;
	column = (int)floor(gx / step_x);
	row = (int)floor(gy / step_y);
	if (column < 0 || column >= home_cols(home) || row < 0 ||
	    row >= home_rows(home))
		return false;
	/* Reject touches that land in the inter-cell gutter. */
	local_x = gx - column * step_x;
	local_y = gy - row * step_y;
	if (local_x > cell_width || local_y > cell_height)
		return false;
	*column_out = column;
	*row_out = row;
	return true;
}

static struct home_launcher_icon *
home_launcher_icon_at(struct home *home, double x, double y)
{
	GtkWidget *picked;
	double gx;
	double gy;
	int column;
	int row;

	if (home->launcher_icons == NULL || home->context_overlay == NULL)
		return NULL;
	/* The modal picker owns its input. Grid-cell fallback must not select
	 * an app underneath Close or a widget choice. */
	if (home->widget_picker != NULL && gtk_widget_get_visible(home->widget_picker))
		return NULL;
	/* Prefer real widget hit-testing (matches painted icon bounds). */
	picked = gtk_widget_pick(GTK_WIDGET(home->context_overlay), x, y,
	    GTK_PICK_DEFAULT);
	for (GtkWidget *ancestor = picked; ancestor != NULL;
	    ancestor = gtk_widget_get_parent(ancestor)) {
		for (size_t i = 0; i < home->launcher_icons->len; i++) {
			struct home_launcher_icon *icon =
			    g_ptr_array_index(home->launcher_icons, i);

			if (icon == NULL || icon->button == NULL ||
			    icon->page != home->launcher_current_page)
				continue;
			if (ancestor == icon->button)
				return icon;
		}
	}
	/*
	 * Fallback: map overlay coords into the grid's allocation. Using overlay
	 * x/y as if they were grid-local made cells look shifted when the
	 * overlay and grid origins differed.
	 */
	if (!overlay_to_grid_point(home, x, y, &gx, &gy))
		return NULL;
	if (!grid_point_to_cell(home, gx, gy, &column, &row))
		return NULL;
	return launcher_icon_at_cell(home, home->launcher_current_page, column,
	    row, NULL);
}

static struct home_launcher_icon *
launcher_remove_badge_at(struct home *home, double x, double y)
{
	graphene_point_t in = { (float)x, (float)y };

	if (!home->layout_editing || home->launcher_icons == NULL ||
	    home->context_overlay == NULL)
		return NULL;
	if (home->widget_picker != NULL && gtk_widget_get_visible(home->widget_picker))
		return NULL;
	for (size_t i = 0; i < home->launcher_icons->len; i++) {
		struct home_launcher_icon *icon =
		    g_ptr_array_index(home->launcher_icons, i);
		GtkWidget *badge;
		graphene_point_t out;
		int width;
		int height;

		if (icon == NULL || icon->button == NULL ||
		    icon->page != home->launcher_current_page)
			continue;
		badge = g_object_get_data(G_OBJECT(icon->button),
		    "launcher-remove-badge");
		if (badge == NULL || !gtk_widget_get_visible(badge) ||
		    !gtk_widget_compute_point(GTK_WIDGET(home->context_overlay),
		    badge, &in, &out))
			continue;
		width = gtk_widget_get_width(badge);
		height = gtk_widget_get_height(badge);
		if (out.x >= 0.0 && out.y >= 0.0 && out.x < width &&
		    out.y < height)
			return icon;
	}
	return NULL;
}

static void
start_launcher_icon_drag(struct home *home, struct home_launcher_icon *icon)
{
	double cell_width;
	double cell_height;
	int column_spacing;
	int row_spacing;

	if (icon == NULL || icon->button == NULL)
		return;
	home->selected_grid_item = NULL;
	refresh_grid_editing(home);
	/* Extra ref for the drag lifetime — survives unparent during relayout. */
	if (home->active_launcher_drag != NULL &&
	    home->active_launcher_drag->button != NULL &&
	    home->active_launcher_drag != icon)
		g_object_unref(home->active_launcher_drag->button);
	if (home->active_launcher_drag != icon)
		g_object_ref(icon->button);
	home->active_launcher_drag = icon;
	home->launcher_drag_start_column = icon->column;
	home->launcher_drag_start_row = icon->row;
	home->launcher_remove_armed = false;
	cancel_launcher_push_dwell(home);
	save_drag_layout_snapshot(home);
	reset_drag_preview(home);
	home->drag_preview_column = icon->column;
	home->drag_preview_row = icon->row;
	if (grid_cell_size(home, &cell_width, &cell_height)) {
		column_spacing = gtk_grid_get_column_spacing(home->context_grid);
		row_spacing = gtk_grid_get_row_spacing(home->context_grid);
		home->launcher_drag_anchor_gx = icon->column *
		    (cell_width + column_spacing) + cell_width * 0.5;
		home->launcher_drag_anchor_gy = icon->row *
		    (cell_height + row_spacing) + cell_height * 0.5;
	}
	home->launcher_suppress_click = true;
	refresh_launcher_icon_editing(home);
	if (home->launcher_remove_banner != NULL) {
		gtk_label_set_text(GTK_LABEL(home->launcher_remove_banner),
		    "Drag here to remove");
		gtk_widget_set_visible(home->launcher_remove_banner, FALSE);
	}
}

static void
update_launcher_icon_drag(struct home *home, double offset_x, double offset_y)
{
	struct home_launcher_icon *icon = home->active_launcher_drag;
	double cell_width;
	double cell_height;
	double gx;
	double gy;
	int column_spacing;
	int row_spacing;
	double step_x;
	double step_y;
	int target_column;
	int target_row;

	if (icon == NULL || home->context_grid == NULL)
		return;
	/* While armed for remove, leave the icon on its last valid cell. */
	if (home->launcher_remove_armed)
		return;
	/* Absolute finger position in grid space — avoids cumulative offset skew. */
	if (!overlay_to_grid_point(home, home->launcher_press_x + offset_x,
	    home->launcher_press_y + offset_y, &gx, &gy))
		return;
	if (!grid_cell_size(home, &cell_width, &cell_height))
		return;
	column_spacing = gtk_grid_get_column_spacing(home->context_grid);
	row_spacing = gtk_grid_get_row_spacing(home->context_grid);
	step_x = cell_width + column_spacing;
	step_y = cell_height + row_spacing;
	target_column = grid_index_with_hysteresis(gx - home->launcher_drag_anchor_gx,
	    step_x, home->launcher_drag_start_column, home->drag_preview_column,
	    0, home_cols(home) - 1,
	    home->config.drag_cell_switch_fraction);
	target_row = grid_index_with_hysteresis(gy - home->launcher_drag_anchor_gy,
	    step_y, home->launcher_drag_start_row, home->drag_preview_row, 0,
	    home_rows(home) - 1,
	    home->config.drag_cell_switch_fraction);
	home->drag_preview_column = target_column;
	home->drag_preview_row = target_row;
	if (target_column == icon->column && target_row == icon->row)
		return;
	/*
	 * Restore snapshot data only, then one light icon relayout inside
	 * apply_launcher_icon_move. The old path called restore (full render)
	 * + apply (full render) every motion sample and unparented the live
	 * drag target — that crashed the home process.
	 */
	restore_drag_layout_data(home);
	apply_launcher_icon_move(home, icon, target_column, target_row);
}

static void
end_launcher_icon_drag(struct home *home, double offset_x, double offset_y)
{
	struct home_launcher_icon *icon = home->active_launcher_drag;
	char *desktop_id = NULL;
	bool remove = false;
	double ox = home->launcher_press_x + offset_x;
	double oy = home->launcher_press_y + offset_y;

	if (icon != NULL && icon->desktop_id != NULL) {
		remove = home->launcher_remove_armed ||
		    launcher_point_is_remove(home, ox, oy);
		if (remove)
			desktop_id = g_strdup(icon->desktop_id);
	}
	restore_drag_layout_data(home);
	if (icon != NULL && !remove) {
		if (!point_in_edit_controls(home, ox, oy) &&
		    home->drag_preview_column >= 0 && home->drag_preview_row >= 0)
			apply_launcher_icon_move(home, icon,
			    home->drag_preview_column, home->drag_preview_row);
		/* Reattach even when the cell was unchanged after a page hop. */
		relayout_launcher_icons(home);
		if (icon->page != home->launcher_current_page)
			set_launcher_page(home, icon->page);
	} else {
		relayout_launcher_icons(home);
	}
	clear_drag_layout_snapshot(home);
	reset_drag_preview(home);
	cancel_launcher_push_dwell(home);
	cancel_page_edge_dwell(home);
	set_launcher_remove_armed(home, false);
	if (icon != NULL && icon->button != NULL) {
		gtk_widget_remove_css_class(icon->button, "removing");
		g_object_unref(icon->button);
	}
	home->active_launcher_drag = NULL;
	home->launcher_drag_armed = false;
	home->pending_launcher_press = NULL;
	refresh_launcher_icon_editing(home);
	if (remove && desktop_id != NULL) {
		remove_favorite(home, desktop_id);
		pulse_layout_edit_haptic(home);
		g_free(desktop_id);
		return;
	}
	g_free(desktop_id);
	save_home_layout(home);
}

static void
set_page_strip_x(struct home *home, double x)
{
	int width = pager_viewport_width(home);
	int height = pager_viewport_height(home);
	int pages = MAX(home->page_panel_count, 1);
	char css[256];

	home->page_strip_x = x;
	if (home->layout_editing && home->grid_overlay != NULL)
		gtk_widget_queue_draw(GTK_WIDGET(home->grid_overlay));
	if (home->pager_gsk && home->page_motion != NULL) {
		home_pager_motion_set_translation(home->page_motion, x);
		return;
	}
	if (home->pager_css == NULL) {
		home->pager_css = gtk_css_provider_new();
		gtk_style_context_add_provider_for_display(
		    gdk_display_get_default(),
		    GTK_STYLE_PROVIDER(home->pager_css),
		    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 5);
	}
	if (width <= 1)
		width = 1;
	if (height <= 1)
		height = 1;
	/* Keep geometry locks; transform alone would drop min-width/height. */
	g_snprintf(css, sizeof(css),
	    ".home-page-panel { min-width: %dpx; min-height: %dpx; }"
	    ".home-page-strip { min-width: %dpx; min-height: %dpx;"
	    " transform: translateX(%.2fpx); }",
	    width, height, width * pages, height, x);
	gtk_css_provider_load_from_string(home->pager_css, css);
}

static void
apply_page_drag_offset(struct home *home, double drag_dx)
{
	int width = pager_page_stride(home);

	home->page_drag_dx = drag_dx;
	set_page_strip_x(home,
	    -(double)home->launcher_current_page * (double)width + drag_dx);
	if (home->page_strip != NULL)
		gtk_widget_add_css_class(home->page_strip, "paging");
}

static void
clear_page_drag_offset(struct home *home)
{
	home->page_drag_dx = 0.0;
	set_page_strip_x(home,
	    -(double)home->launcher_current_page *
	    (double)pager_page_stride(home));
	if (home->page_strip != NULL)
		gtk_widget_remove_css_class(home->page_strip, "paging");
}

static void
finish_page_settle(struct home *home)
{
	int commit = home->page_settle_commit;

	home->page_settle_source = 0;
	home->page_settle_commit = -1;
	if (commit >= 0 && commit < home->launcher_page_count)
		home->launcher_current_page = commit;
	home->page_drag_dx = 0.0;
	clear_page_drag_offset(home);
	refresh_launcher_page_bar(home);
	if (commit >= 0)
		save_home_layout(home);
	home->page_drag_owned = false;
}

static gboolean
page_settle_tick(GtkWidget *widget, GdkFrameClock *frame_clock, gpointer data)
{
	struct home *home = data;
	bool done = false;
	double progress;
	double x;
	int64_t now = gdk_frame_clock_get_frame_time(frame_clock);

	(void)widget;
	if (home->page_settle_started_us == 0)
		home->page_settle_started_us = now;

	progress = home_pager_settle_progress(home->page_settle_started_us, now,
	    home->config.page_animation_ms, &done);
	x = home->page_settle_from +
	    (home->page_settle_to - home->page_settle_from) * progress;
	set_page_strip_x(home, x);
	if (!done)
		return G_SOURCE_CONTINUE;
	finish_page_settle(home);
	return G_SOURCE_REMOVE;
}

static void
cancel_page_settle(struct home *home)
{
	if (home->page_settle_source == 0)
		return;
	gtk_widget_remove_tick_callback(GTK_WIDGET(home->context_overlay),
	    home->page_settle_source);
	home->page_settle_source = 0;
}

static void
abort_page_settle(struct home *home)
{
	cancel_page_settle(home);
	/* Flush a pending page commit / transform before a new gesture. */
	if (home->page_settle_commit >= 0 || home->page_drag_dx != 0.0 ||
	    home->page_drag_owned)
		finish_page_settle(home);
}

static void
start_page_settle(struct home *home, double from_dx, double to_dx)
{
	cancel_page_settle(home);
	home->page_settle_from = from_dx;
	home->page_settle_to = to_dx;
	home->page_settle_started_us = 0;
	if (home->config.page_animation_ms <= 0 ||
	    fabs(from_dx - to_dx) < 1.0) {
		finish_page_settle(home);
		return;
	}
	set_page_strip_x(home, from_dx);
	if (home->page_strip != NULL)
		gtk_widget_add_css_class(home->page_strip, "paging");
	home->page_settle_source = gtk_widget_add_tick_callback(
	    GTK_WIDGET(home->context_overlay), page_settle_tick, home, NULL);
}

static void
home_page_drag_begin(GtkGestureDrag *gesture, double start_x, double start_y,
    gpointer data)
{
	struct home *home = data;

	(void)gesture;
	home->page_drag_pending = false;
	home->page_drag_active = false;
	if (home->layout_editing || home->active_drag_item != NULL ||
	    home->active_launcher_drag != NULL ||
	    home->launcher_drag_armed)
		return;
	if (home->launcher_page_count <= 1)
		return;
	/*
	 * Observe every Home target without claiming yet. Stationary release and
	 * hold remain owned by icons/widgets; horizontal intent promotes this
	 * capture-phase controller to the sequence owner.
	 */
	abort_page_settle(home);
	home->page_settle_commit = -1;
	home->page_drag_pending = true;
	home->page_drag_start_x = start_x;
	home->page_drag_start_y = start_y;
	home->page_drag_dx = 0.0;
}

static void
home_page_drag_update(GtkGestureDrag *gesture, double offset_x, double offset_y,
    gpointer data)
{
	struct home *home = data;
	int width;
	double max_dx;
	double threshold;
	double distance = hypot(offset_x, offset_y);

	/* Movement must never wait long enough to mature into icon rearrangement.
	 * Cancel the custom hold at tap slop even if this later resolves as a
	 * vertical shell pull instead of a page swipe. */
	if (!home->page_drag_active && distance >= HOME_TAP_SLOP)
		cancel_launcher_drag_arm(home);

	if (home->page_drag_pending && !home->page_drag_active) {
		if (home->layout_editing || home->active_drag_item != NULL ||
		    home->active_launcher_drag != NULL ||
		    home->launcher_drag_armed) {
			home->page_drag_pending = false;
			return;
		}
		/* Resolve intent quickly without turning tap jitter into paging. */
		threshold = MAX(HOME_TAP_SLOP,
		    (double)pager_page_stride(home) * 0.04);
		if (fabs(offset_x) < threshold && fabs(offset_y) < threshold)
			return;
		if (fabs(offset_x) <= fabs(offset_y) * 1.15) {
			/* Vertical / diagonal — let other gestures keep it. */
			home->page_drag_pending = false;
			gtk_gesture_set_state(GTK_GESTURE(gesture),
			    GTK_EVENT_SEQUENCE_DENIED);
			return;
		}
		home->page_drag_pending = false;
		home->page_drag_active = true;
		home->page_drag_owned = true;
		home->page_release_suppress = true;
		cancel_launcher_drag_arm(home);
		gtk_gesture_set_state(GTK_GESTURE(gesture),
		    GTK_EVENT_SEQUENCE_CLAIMED);
	}
	if (!home->page_drag_active)
		return;
	width = pager_page_stride(home);
	if (width <= 0)
		return;
	max_dx = (double)width;
	/* Rubber-band at ends. */
	if ((home->launcher_current_page <= 0 && offset_x > 0.0) ||
	    (home->launcher_current_page >= home->launcher_page_count - 1 &&
	    offset_x < 0.0))
		offset_x *= 0.35;
	offset_x = CLAMP(offset_x, -max_dx, max_dx);
	apply_page_drag_offset(home, offset_x);
}

static void
home_page_drag_end(GtkGestureDrag *gesture, double offset_x, double offset_y,
    gpointer data)
{
	struct home *home = data;
	double velocity_x = 0.0;
	int width;
	double fraction;
	int target;
	double settle_from;

	(void)gesture;
	(void)offset_y;
	if (!home->page_drag_active) {
		home->page_drag_pending = false;
		return;
	}
	home->page_drag_active = false;
	home->page_drag_pending = false;
	width = pager_page_stride(home);
	if (width <= 0) {
		home->page_settle_commit = -1;
		finish_page_settle(home);
		return;
	}
	/*
	 * Approximate fling from drag distance over a short window: treat a
	 * large offset as velocity in that direction when past ~30% width.
	 */
	if (fabs(offset_x) > (double)width * 0.30)
		velocity_x = offset_x > 0.0 ?
		    home->config.page_fling_velocity :
		    -home->config.page_fling_velocity;
	fraction = offset_x / (double)width;
	target = home_pager_commit_page(home->launcher_current_page,
	    home->launcher_page_count, fraction, velocity_x,
	    home->config.page_snap_fraction, home->config.page_fling_velocity);
	settle_from = -(double)home->launcher_current_page * (double)width +
	    offset_x;
	home->page_settle_commit = target;
	start_page_settle(home, settle_from,
	    -(double)target * (double)width);
}

static void
launcher_swiped(GtkGestureSwipe *gesture, double velocity_x, double velocity_y,
    gpointer data)
{
	struct home *home = data;
	int target;

	(void)gesture;
	(void)velocity_y;
	if (home->layout_editing || home->active_launcher_drag != NULL ||
	    home->page_drag_active || home->page_drag_owned ||
	    home->page_settle_source != 0)
		return;
	/* GtkGestureSwipe still observes a vertical sequence that page_drag denied.
	 * Never reinterpret its incidental X velocity as a Home-page fling: the
	 * top Shade and lower Drawer pulls must remain vertically owned 1:1. */
	if (fabs(velocity_x) <= fabs(velocity_y) * 1.15)
		return;
	target = home_pager_commit_page(home->launcher_current_page,
	    home->launcher_page_count, 0.0, velocity_x,
	    home->config.page_snap_fraction, home->config.page_fling_velocity);
	if (target != home->launcher_current_page)
		set_launcher_page_animated(home, target, true);
}

static void
ensure_launcher_button(struct home *home, struct home_launcher_icon *icon)
{
	GIcon *app_icon;

	if (icon->button != NULL)
		return;
	app_icon = favorite_icon_for_id(icon->desktop_id);
	icon->button = launcher_icon_button(home, app_icon, icon->desktop_id);
	g_object_unref(app_icon);
	g_object_ref_sink(icon->button);
}

static void
refresh_launcher_icons(struct home *home)
{
	char *preferences = ctlst_script_path("ctlst-preferences");
	char *argv[] = {preferences, "favorites", "list", NULL};
	char *output = run_capture(argv);
	struct json_object *favorites = json_tokener_parse(output);
	bool listed = favorites != NULL &&
	    json_object_is_type(favorites, json_type_array);

	if (home->launcher_icons == NULL)
		home->launcher_icons =
		    g_ptr_array_new_with_free_func(launcher_icon_free);
	for (ssize_t i = (ssize_t)home->launcher_icons->len - 1; i >= 0; i--) {
		struct home_launcher_icon *icon =
		    g_ptr_array_index(home->launcher_icons, (size_t)i);

		if (!listed || !favorite_list_contains(favorites,
		    icon->desktop_id))
			g_ptr_array_remove_index(home->launcher_icons, (size_t)i);
	}
	if (listed) {
		for (size_t i = 0; i < json_object_array_length(favorites); i++) {
			struct json_object *entry =
			    json_object_array_get_idx(favorites, i);
			struct home_launcher_icon *icon;
			const char *desktop_id;
			int column = 0;
			int row = 0;
			int page = home->launcher_current_page;

			if (!json_object_is_type(entry, json_type_string))
				continue;
			desktop_id = json_object_get_string(entry);
			if (launcher_icon_for_desktop_id(home, desktop_id) != NULL)
				continue;
			if (!launcher_find_free_cell(home, page, &column, &row) &&
			    !launcher_find_free_cell(home, 0, &column, &row)) {
				for (page = 0; page < home->launcher_page_count;
				    page++) {
					if (launcher_find_free_cell(home, page,
					    &column, &row))
						break;
				}
				if (page >= home->launcher_page_count &&
				    home->launcher_page_count <
				    home->config.page_max_pages) {
					home->launcher_page_count += 1;
					page = home->launcher_page_count - 1;
					launcher_find_free_cell(home, page, &column,
					    &row);
				}
			}
			icon = g_new0(struct home_launcher_icon, 1);
			icon->home = home;
			icon->desktop_id = g_strdup(desktop_id);
			icon->page = page;
			icon->column = column;
			icon->row = row;
			g_ptr_array_add(home->launcher_icons, icon);
		}
	} else {
		struct home_launcher_icon *icon;
		int column = 0;
		int row = 0;

		if (launcher_icon_for_desktop_id(home, "ctlst:apps") == NULL &&
		    launcher_find_free_cell(home, 0, &column, &row)) {
			icon = g_new0(struct home_launcher_icon, 1);
			icon->home = home;
			icon->desktop_id = g_strdup("ctlst:apps");
			icon->page = 0;
			icon->column = column;
			icon->row = row;
			g_ptr_array_add(home->launcher_icons, icon);
		}
	}
	for (size_t i = 0; i < home->launcher_icons->len; i++) {
		struct home_launcher_icon *icon =
		    g_ptr_array_index(home->launcher_icons, i);

		ensure_launcher_button(home, icon);
		g_object_set_data(G_OBJECT(icon->button), "launcher-icon", icon);
	}
	render_launcher_page(home);
	refresh_launcher_icon_editing(home);
	if (favorites != NULL)
		json_object_put(favorites);
	g_free(output);
	g_free(preferences);
}

static void
set_agent_busy(struct home *home, bool busy)
{
	home->agent_busy = busy;
	gtk_widget_set_sensitive(GTK_WIDGET(home->ask_button), !busy);
	gtk_widget_set_sensitive(GTK_WIDGET(home->prompt), !busy);
	gtk_widget_set_sensitive(home->suggestions, !busy);
	gtk_widget_set_sensitive(home->action_controls, !busy);
	if (busy) gtk_spinner_start(home->agent_spinner);
	else gtk_spinner_stop(home->agent_spinner);
}

static void
clear_agent_action(struct home *home)
{
	g_clear_pointer(&home->pending_action_id, g_free);
	gtk_widget_set_visible(home->action_controls, FALSE);
}

static const char *
agent_string(struct json_object *root, const char *key)
{
	struct json_object *value;
	return root && json_object_is_type(root, json_type_object) &&
	    json_object_object_get_ex(root, key, &value) &&
	    json_object_is_type(value, json_type_string) ? json_object_get_string(value) : "";
}

static void
agent_finished(GObject *source, GAsyncResult *result, gpointer data)
{
	struct home *home = data;
	GSubprocess *process = G_SUBPROCESS(source);
	char *output = NULL, *errors = NULL;
	GError *error = NULL;
	struct json_object *root = NULL, *value;
	const char *reply = "Agent did not return a response.";
	bool ok = false;
	bool asked = g_strcmp0(g_object_get_data(source, "agent-command"), "ask") == 0;

	if (g_subprocess_communicate_utf8_finish(process, result,
	    &output, &errors, &error) && output != NULL)
		root = json_tokener_parse(output);
	if (root && json_object_is_type(root, json_type_object)) {
		ok = g_subprocess_get_successful(process) &&
		    json_object_object_get_ex(root, "ok", &value) &&
		    json_object_is_type(value, json_type_boolean) &&
		    json_object_get_boolean(value);
		if (*agent_string(root, "reply"))
			reply = agent_string(root, "reply");
	} else if (error != NULL) {
		reply = error->message;
	} else if (errors != NULL && errors[0] != '\0') {
		reply = errors;
	}
	gtk_label_set_text(home->response, reply);
	gtk_adjustment_set_value(gtk_scrolled_window_get_vadjustment(
	    GTK_SCROLLED_WINDOW(home->agent_scroller)), 0);
	gtk_label_set_text(home->agent_status, ok ? "Ready" : "Needs attention");
	clear_agent_action(home);
	const char *action_id = agent_string(root, "action_id");
	const char *action_label = agent_string(root, "action_label");
	if (ok && *action_id && *action_label) {
		home->pending_action_id = g_strdup(action_id);
		gtk_label_set_text(home->proposed_action, action_label);
		gtk_widget_set_visible(home->action_controls, TRUE);
	} else if (ok && *action_label) {
		ok = false;
		gtk_label_set_text(home->agent_status, "Action has no confirmation token");
	}
	set_agent_busy(home, false);
	/* A failed request leaves the original prompt available for correction/retry. */
	if (ok && asked)
		gtk_editable_set_text(GTK_EDITABLE(home->prompt), "");
	g_clear_error(&error);
	if (root != NULL) json_object_put(root);
	g_free(errors);
	g_free(output);
	g_object_unref(process);
}

static void
run_agent(struct home *home, const char *command, const char *argument)
{
	char *agent = ctlst_script_path("ctlst-agent");
	const char *argv[5] = {agent, command, argument, NULL, NULL};
	GError *error = NULL;
	GSubprocess *process = g_subprocess_newv(argv,
	    G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE, &error);
	g_free(agent);
	if (process == NULL) {
		gtk_label_set_text(home->response,
		    "The agent helper could not start. Check the optional agent installation, then try again.");
		gtk_label_set_text(home->agent_status, "Needs attention");
		g_clear_error(&error);
		set_agent_busy(home, false);
		return;
	}
	g_object_set_data_full(G_OBJECT(process), "agent-command", g_strdup(command), g_free);
	g_subprocess_communicate_utf8_async(process, NULL, NULL, agent_finished, home);
}

static void
ask_agent(GtkWidget *widget, gpointer data)
{
	struct home *home = data;
	const char *prompt = gtk_editable_get_text(GTK_EDITABLE(home->prompt));
	(void)widget;
	if (home->agent_busy || prompt[0] == '\0')
		return;
	clear_agent_action(home);
	set_agent_busy(home, true);
	char *query = g_strdup_printf("You: %s", prompt);
	gtk_label_set_text(home->agent_query, query);
	gtk_widget_set_visible(GTK_WIDGET(home->agent_query), TRUE);
	gtk_widget_set_visible(home->suggestions, FALSE);
	g_free(query);
	gtk_label_set_text(home->agent_status, "Thinking…");
	gtk_label_set_text(home->response, "Waiting for the optional agent…");
	run_agent(home, "ask", prompt);
}

static void
finish_agent_action(struct home *home, const char *command)
{
	if (home->agent_busy || home->pending_action_id == NULL)
		return;
	char *token = g_strdup(home->pending_action_id);
	clear_agent_action(home);
	set_agent_busy(home, true);
	gtk_label_set_text(home->agent_status,
	    strcmp(command, "confirm") == 0 ? "Running…" : "Cancelling…");
	/* Never consult another client's mutable global pending-action file. */
	run_agent(home, command, token);
	g_free(token);
}

static void
confirm_action(GtkButton *button, gpointer data)
{
	(void)button;
	finish_agent_action(data, "confirm");
}

static void
cancel_agent_action(GtkButton *button, gpointer data)
{
	(void)button;
	finish_agent_action(data, "cancel");
}

static gboolean
socket_ready(gint fd, GIOCondition condition, gpointer data)
{
	struct home *home = data;
	char buffer[512];
	ssize_t length;

	if ((condition & G_IO_IN) == 0)
		return G_SOURCE_CONTINUE;
	length = recv(fd, buffer, sizeof(buffer) - 1, MSG_DONTWAIT);
	if (length < 0) {
		if (errno != EAGAIN && errno != EWOULDBLOCK)
			g_warning("home socket read failed: %s",
			    g_strerror(errno));
		return G_SOURCE_CONTINUE;
	}
	if (length == 0)
		return G_SOURCE_CONTINUE;
	buffer[length] = '\0';
	if (buffer[0] == 'P') {
		handle_place_drop(home, buffer);
		return G_SOURCE_CONTINUE;
	}
	if (buffer[0] == 'O') {
		signal_rotation_ready(home);
		return G_SOURCE_CONTINUE;
	}
	if (buffer[0] == 'C') {
		update_layout(home);
		return G_SOURCE_CONTINUE;
	}
	for (ssize_t i = 0; i < length; i++) {
		char command = buffer[i];

		if (command == 'E') {
			/* Go Home: leave edit mode and return to the first page. */
			set_layout_editing(home, false);
			set_launcher_page(home, 0);
		} else if (command == 'S')
			set_visible(home, true);
		else if (command == 'H')
			set_visible(home, false);
		else if (command == 'T') {
			reload_theme(home);
			reload_home_config(home);
		} else if (command == 'R')
			refresh_launcher_icons(home);
		else if (command == 'A' && home->visible && home->prompt != NULL)
			open_agent(NULL, home);
		else if (command == 'V' && home->visible &&
		    home->visual_button != NULL)
			cycle_visual(home->visual_button, home);
	}
	return G_SOURCE_CONTINUE;
}

static bool
owns_socket_path(struct home *home)
{
	struct stat status;

	return home->socket_inode != 0 &&
	    lstat(home->socket_path, &status) == 0 &&
	    status.st_dev == home->socket_device &&
	    status.st_ino == home->socket_inode;
}

static bool
create_socket(struct home *home)
{
	struct sockaddr_un address = {0};
	const char *runtime = g_get_user_runtime_dir();
	struct stat status;
	guint source;
	int fd;

	g_snprintf(home->socket_path, sizeof(home->socket_path), "%s/%s",
	    runtime, HOME_SOCKET);
	g_snprintf(home->state_path, sizeof(home->state_path), "%s/%s",
	    runtime, HOME_STATE);
	g_snprintf(home->focus_path, sizeof(home->focus_path),
	    "%s/ctlsthome.prompt-focused", runtime);
	g_snprintf(home->layout_edit_path, sizeof(home->layout_edit_path),
	    "%s/ctlsthome.layout-editing", runtime);
	unlink(home->layout_edit_path);
	g_snprintf(home->keyboard_path, sizeof(home->keyboard_path),
	    "%s/ctlstkeyboard.visible", runtime);
	g_snprintf(home->lock_path, sizeof(home->lock_path),
	    "%s/ctlstlock.visible", runtime);
	g_snprintf(home->prompt_length_path, sizeof(home->prompt_length_path),
	    "%s/ctlsthome.prompt-length", runtime);
	g_snprintf(home->rotation_ready_path, sizeof(home->rotation_ready_path),
	    "%s/ctlsthome.rotation-ready", runtime);
	unlink(home->rotation_ready_path);
	fd = socket(AF_UNIX,
	    SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
	if (fd < 0)
		return false;
	address.sun_family = AF_UNIX;
	g_strlcpy(address.sun_path, home->socket_path, sizeof(address.sun_path));
	unlink(home->socket_path);
	if (bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
		close(fd);
		return false;
	}
	chmod(home->socket_path, 0600);
	if (lstat(home->socket_path, &status) < 0) {
		unlink(home->socket_path);
		close(fd);
		return false;
	}
	source = g_unix_fd_add(fd, G_IO_IN, socket_ready, home);
	if (source == 0) {
		unlink(home->socket_path);
		close(fd);
		errno = EIO;
		return false;
	}
	if (home->control_source != 0)
		g_source_remove(home->control_source);
	if (home->control_fd >= 0)
		close(home->control_fd);
	home->control_fd = fd;
	home->control_source = source;
	home->socket_device = status.st_dev;
	home->socket_inode = status.st_ino;
	return true;
}

static gboolean
ensure_socket(gpointer data)
{
	struct home *home = data;
	struct stat status;

	if (owns_socket_path(home))
		return G_SOURCE_CONTINUE;
	/* Never replace a path currently owned by another live instance. */
	if (lstat(home->socket_path, &status) == 0)
		return G_SOURCE_CONTINUE;
	if (!create_socket(home))
		g_warning("cannot restore Home socket: %s", g_strerror(errno));
	return G_SOURCE_CONTINUE;
}

static void
agent_interaction_started(GtkGestureClick *gesture, int count, double x, double y, gpointer data)
{
	struct home *home = data;
	(void)gesture; (void)count; (void)x; (void)y;
	if (!home_is_locked(home))
		gtk_layer_set_keyboard_mode(home->window, GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE);
}

static GtkWidget *
make_agent_content(struct home *home)
{
	GtkWidget *agent = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_widget_add_css_class(agent, "home-agent");
	gtk_widget_set_halign(agent, GTK_ALIGN_FILL);
	gtk_widget_set_valign(agent, GTK_ALIGN_FILL);
	GtkGesture *press = gtk_gesture_click_new();
	gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(press), GTK_PHASE_CAPTURE);
	g_signal_connect(press, "pressed", G_CALLBACK(agent_interaction_started), home);
	gtk_widget_add_controller(agent, GTK_EVENT_CONTROLLER(press));
	GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	GtkWidget *identity = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
	GtkWidget *title = gtk_label_new("Pocket Agent");
	gtk_label_set_xalign(GTK_LABEL(title), 0);
	gtk_widget_add_css_class(title, "home-agent-title");
	gtk_box_append(GTK_BOX(identity), title);
	home->agent_status = GTK_LABEL(gtk_label_new("Ready"));
	gtk_label_set_xalign(home->agent_status, 0);
	gtk_label_set_ellipsize(home->agent_status, PANGO_ELLIPSIZE_END);
	gtk_label_set_width_chars(home->agent_status, 1);
	gtk_widget_add_css_class(GTK_WIDGET(home->agent_status), "home-agent-state");
	gtk_box_append(GTK_BOX(identity), GTK_WIDGET(home->agent_status));
	gtk_widget_set_hexpand(identity, TRUE);
	gtk_box_append(GTK_BOX(header), identity);
	home->agent_spinner = GTK_SPINNER(gtk_spinner_new());
	gtk_box_append(GTK_BOX(header), GTK_WIDGET(home->agent_spinner));
	home->visual_button = GTK_BUTTON(make_button(
	    (const char *[]){"At a glance", "Signal", "Bloom"}[home->visual_mode],
	    "home-visual-mode", G_CALLBACK(cycle_visual), home));
	gtk_box_append(GTK_BOX(header), GTK_WIDGET(home->visual_button));
	gtk_box_append(GTK_BOX(agent), header);

	GtkWidget *body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
	home->agent_query = GTK_LABEL(gtk_label_new(""));
	gtk_label_set_xalign(home->agent_query, 0);
	gtk_label_set_wrap(home->agent_query, TRUE);
	gtk_label_set_wrap_mode(home->agent_query, PANGO_WRAP_WORD_CHAR);
	gtk_label_set_width_chars(home->agent_query, 1);
	gtk_widget_add_css_class(GTK_WIDGET(home->agent_query), "home-query");
	gtk_widget_set_visible(GTK_WIDGET(home->agent_query), FALSE);
	gtk_box_append(GTK_BOX(body), GTK_WIDGET(home->agent_query));
	home->response = GTK_LABEL(gtk_label_new("Ask a question about this phone."));
	gtk_label_set_xalign(home->response, 0);
	gtk_label_set_yalign(home->response, 0);
	gtk_label_set_wrap(home->response, TRUE);
	gtk_label_set_wrap_mode(home->response, PANGO_WRAP_WORD_CHAR);
	gtk_label_set_width_chars(home->response, 1);
	gtk_label_set_selectable(home->response, TRUE);
	gtk_widget_add_css_class(GTK_WIDGET(home->response), "home-response");
	gtk_box_append(GTK_BOX(body), GTK_WIDGET(home->response));

	home->action_controls = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	GtkWidget *proposed = gtk_label_new("Proposed action");
	gtk_label_set_xalign(GTK_LABEL(proposed), 0);
	gtk_widget_add_css_class(proposed, "home-agent-state");
	gtk_box_append(GTK_BOX(home->action_controls), proposed);
	home->proposed_action = GTK_LABEL(gtk_label_new(""));
	gtk_label_set_xalign(home->proposed_action, 0);
	gtk_label_set_wrap(home->proposed_action, TRUE);
	gtk_label_set_wrap_mode(home->proposed_action, PANGO_WRAP_WORD_CHAR);
	gtk_label_set_width_chars(home->proposed_action, 1);
	gtk_box_append(GTK_BOX(home->action_controls), GTK_WIDGET(home->proposed_action));
	GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	home->cancel_action_button = make_button("Cancel action", "home-agent-cancel",
	    G_CALLBACK(cancel_agent_action), home);
	home->action_button = GTK_BUTTON(make_button("Run action", "home-action",
	    G_CALLBACK(confirm_action), home));
	gtk_widget_set_hexpand(home->cancel_action_button, TRUE);
	gtk_widget_set_hexpand(GTK_WIDGET(home->action_button), TRUE);
	gtk_box_append(GTK_BOX(actions), home->cancel_action_button);
	gtk_box_append(GTK_BOX(actions), GTK_WIDGET(home->action_button));
	gtk_box_append(GTK_BOX(home->action_controls), actions);
	gtk_widget_set_visible(home->action_controls, FALSE);
	gtk_box_append(GTK_BOX(body), home->action_controls);

	home->suggestions = gtk_flow_box_new();
	gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(home->suggestions), GTK_SELECTION_NONE);
	gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(home->suggestions), 1);
	gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(home->suggestions), 3);
	gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(home->suggestions), 6);
	gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(home->suggestions), 6);
	gtk_widget_add_css_class(home->suggestions, "home-suggestions");
	const char *labels[] = {"Phone health", "Network", "Next step"};
	const char *prompts[] = {
	    "Give me a concise health snapshot of this phone.",
	    "Summarize my current network and connectivity.",
	    "What is the most useful next improvement for this shell?"};
	for (unsigned i = 0; i < G_N_ELEMENTS(labels); i++) {
		GtkWidget *suggestion = make_button(labels[i], "home-suggestion",
		    G_CALLBACK(suggestion_clicked), home);
		g_object_set_data_full(G_OBJECT(suggestion), "prompt", g_strdup(prompts[i]), g_free);
		gtk_flow_box_append(GTK_FLOW_BOX(home->suggestions), suggestion);
	}
	gtk_box_append(GTK_BOX(body), home->suggestions);
	home->agent_scroller = gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(home->agent_scroller),
	    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_kinetic_scrolling(GTK_SCROLLED_WINDOW(home->agent_scroller), TRUE);
	gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(home->agent_scroller), 48);
	gtk_widget_set_vexpand(home->agent_scroller, TRUE);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(home->agent_scroller), body);
	gtk_box_append(GTK_BOX(agent), home->agent_scroller);

	GtkWidget *prompt_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	home->prompt = GTK_ENTRY(gtk_entry_new());
	gtk_entry_set_placeholder_text(home->prompt, "Ask about this phone…");
	gtk_editable_set_width_chars(GTK_EDITABLE(home->prompt), 1);
	gtk_widget_add_css_class(GTK_WIDGET(home->prompt), "home-prompt");
	gtk_widget_set_hexpand(GTK_WIDGET(home->prompt), TRUE);
	gtk_accessible_update_property(GTK_ACCESSIBLE(home->prompt),
	    GTK_ACCESSIBLE_PROPERTY_LABEL, "Ask Pocket Agent", -1);
	g_signal_connect(home->prompt, "activate", G_CALLBACK(ask_agent), home);
	g_signal_connect(home->prompt, "changed", G_CALLBACK(prompt_changed), home);
	GtkEventController *focus = gtk_event_controller_focus_new();
	g_signal_connect(focus, "enter", G_CALLBACK(prompt_focus_enter), home);
	g_signal_connect(focus, "leave", G_CALLBACK(prompt_focus_leave), home);
	gtk_widget_add_controller(GTK_WIDGET(home->prompt), focus);
	GtkEventController *events = gtk_event_controller_legacy_new();
	gtk_event_controller_set_propagation_phase(events, GTK_PHASE_CAPTURE);
	g_signal_connect(events, "event", G_CALLBACK(prompt_event), home);
	gtk_widget_add_controller(GTK_WIDGET(home->prompt), events);
	home->ask_button = GTK_BUTTON(make_button("Ask", "home-ask", G_CALLBACK(ask_agent), home));
	gtk_box_append(GTK_BOX(prompt_row), GTK_WIDGET(home->prompt));
	gtk_box_append(GTK_BOX(prompt_row), GTK_WIDGET(home->ask_button));
	gtk_box_append(GTK_BOX(agent), prompt_row);
	return agent;
}

static void
activate(GtkApplication *application, gpointer data)
{
	struct home *home = data;
	GtkWidget *root;
	GtkWidget *header;
	GtkWidget *clock_box;
	GtkWidget *agent;
	GtkWidget *visual_overlay;
	GtkWidget *performance;
	GtkWidget *page_bar;
	GtkGesture *layout_outside;
	GtkGesture *page_swipe;
	char *visual_path;
	char *visual_value = NULL;
	bool agent_enabled;

	if (home->window != NULL)
		return;
	home_config_set_defaults(&home->config);
	home_config_load_user(&home->config);
	home->pager_gsk = home_pager_gsk_enabled();
	home->application = application;
	home->launcher_icons =
	    g_ptr_array_new_with_free_func(launcher_icon_free);
	home->launcher_page_count = 1;
	home->launcher_current_page = 0;
	home->launcher_push_target_column = -1;
	home->launcher_push_target_row = -1;
	init_haptic(home);
	home->window = GTK_WINDOW(gtk_application_window_new(application));
	gtk_widget_set_name(GTK_WIDGET(home->window), "ctlsthome");
	gtk_window_set_title(home->window, "Home");
	gtk_window_set_decorated(home->window, FALSE);
	gtk_layer_init_for_window(home->window);
	/* The task manager scopes Home to workspace 1, so keep it above swaybg. */
	gtk_layer_set_layer(home->window, GTK_LAYER_SHELL_LAYER_TOP);
	gtk_layer_set_namespace(home->window, "ctlsthome");
	gtk_layer_set_anchor(home->window, GTK_LAYER_SHELL_EDGE_TOP, TRUE);
	gtk_layer_set_anchor(home->window, GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
	gtk_layer_set_anchor(home->window, GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
	gtk_layer_set_anchor(home->window, GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
	gtk_layer_set_margin(home->window, GTK_LAYER_SHELL_EDGE_TOP, 30);
	/* Keep clear of the gesture pill, but do not leave a large dead band. */
	gtk_layer_set_margin(home->window, GTK_LAYER_SHELL_EDGE_BOTTOM, 28);
	gtk_layer_set_exclusive_zone(home->window, -1);
	gtk_layer_set_keyboard_mode(home->window,
	    GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
	setup_theme(home);
	agent_enabled = legacy_agent_enabled();
	if (agent_enabled) {
		visual_path = ctlst_config_path("agent-visual");
		if (g_file_get_contents(visual_path, &visual_value, NULL, NULL))
			home->visual_mode = (unsigned int)g_ascii_strtoull(visual_value,
			    NULL, 10) % 3;
		g_free(visual_value);
		g_free(visual_path);
	}

	root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
	home->root = root;
	gtk_widget_set_name(root, "ctlsthome-root");
	gtk_widget_set_focusable(root, TRUE);
	gtk_widget_add_css_class(root, "home-root");
	layout_outside = gtk_gesture_click_new();
	gtk_gesture_single_set_exclusive(
	    GTK_GESTURE_SINGLE(layout_outside), false);
	/* Let widget drags claim their sequence before tap-away dismissal runs. */
	gtk_event_controller_set_propagation_phase(
	    GTK_EVENT_CONTROLLER(layout_outside), GTK_PHASE_BUBBLE);
	g_signal_connect(layout_outside, "released",
	    G_CALLBACK(layout_outside_released), home);
	gtk_widget_add_controller(root,
	    GTK_EVENT_CONTROLLER(layout_outside));
	home->rotation_host = GTK_OVERLAY(gtk_overlay_new());
	gtk_widget_set_hexpand(GTK_WIDGET(home->rotation_host), TRUE);
	gtk_widget_set_vexpand(GTK_WIDGET(home->rotation_host), TRUE);
	gtk_widget_set_overflow(GTK_WIDGET(home->rotation_host),
	    GTK_OVERFLOW_HIDDEN);
	gtk_overlay_set_child(home->rotation_host, root);
	gtk_window_set_child(home->window, home_allocation_host_new(
	    GTK_WIDGET(home->rotation_host), prepare_home_allocation,
	    finish_home_allocation, home));

	header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	home->header = header;
	clock_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	home->clock = GTK_LABEL(gtk_label_new("--:--"));
	home->date = GTK_LABEL(gtk_label_new(""));
	home->system = GTK_LABEL(gtk_label_new("Offline  ·  --%"));
	gtk_label_set_xalign(home->clock, 0);
	gtk_label_set_xalign(home->date, 0);
	gtk_label_set_xalign(home->system, 1);
	gtk_widget_add_css_class(GTK_WIDGET(home->clock), "home-clock");
	gtk_widget_add_css_class(GTK_WIDGET(home->date), "home-date");
	gtk_widget_add_css_class(GTK_WIDGET(home->system), "home-system");
	gtk_box_append(GTK_BOX(clock_box), GTK_WIDGET(home->clock));
	gtk_box_append(GTK_BOX(clock_box), GTK_WIDGET(home->date));
	gtk_widget_set_hexpand(clock_box, TRUE);
	gtk_widget_set_valign(GTK_WIDGET(home->system), GTK_ALIGN_END);
	gtk_box_append(GTK_BOX(header), clock_box);
	gtk_box_append(GTK_BOX(header), GTK_WIDGET(home->system));
	gtk_box_append(GTK_BOX(root), header);
	gtk_widget_set_visible(header, FALSE);

	home->call_card = make_button("CALL ACTIVE  /  RETURN TO PHONE",
	    "home-call", G_CALLBACK(quick_action), NULL);
	g_object_set_data_full(G_OBJECT(home->call_card), "action",
	    g_strdup("phone"), g_free);
	gtk_widget_set_visible(home->call_card, FALSE);
	gtk_box_append(GTK_BOX(root), home->call_card);

	if (agent_enabled) {
		visual_overlay = gtk_overlay_new();
		home->agent = visual_overlay;
		home->visual = GTK_GL_AREA(gtk_gl_area_new());
		gtk_gl_area_set_allowed_apis(home->visual, GDK_GL_API_GLES);
		gtk_gl_area_set_required_version(home->visual, 2, 0);
		gtk_gl_area_set_auto_render(home->visual, FALSE);
		gtk_widget_add_css_class(GTK_WIDGET(home->visual), "home-visual");
		gtk_widget_set_size_request(GTK_WIDGET(home->visual), -1, 220);
		g_signal_connect(home->visual, "realize",
		    G_CALLBACK(visual_realize), home);
		g_signal_connect(home->visual, "unrealize",
		    G_CALLBACK(visual_unrealize), home);
		g_signal_connect(home->visual, "render",
		    G_CALLBACK(visual_render), home);
		gtk_overlay_set_child(GTK_OVERLAY(visual_overlay),
		    GTK_WIDGET(home->visual));

		agent = make_agent_content(home);
		gtk_widget_set_vexpand(agent, FALSE);
		gtk_overlay_add_overlay(GTK_OVERLAY(visual_overlay), agent);
		gtk_box_append(GTK_BOX(root), visual_overlay);
		if (home_demo_enabled())
			gtk_widget_set_visible(visual_overlay, FALSE);
	}

	home->context_overlay = GTK_OVERLAY(gtk_overlay_new());
	home->context = GTK_WIDGET(home->context_overlay);
	gtk_widget_add_css_class(home->context, "home-context");
	gtk_widget_set_hexpand(home->context, TRUE);
	gtk_widget_set_vexpand(home->context, TRUE);
	gtk_widget_set_overflow(home->context, home->pager_gsk ?
	    GTK_OVERFLOW_VISIBLE : GTK_OVERFLOW_HIDDEN);
	home->context_body = gtk_box_new(GTK_ORIENTATION_VERTICAL,
	    home_spacing(home));
	gtk_widget_add_css_class(home->context_body, "home-grid");
	gtk_widget_set_hexpand(home->context_body, TRUE);
	gtk_widget_set_vexpand(home->context_body, TRUE);
	gtk_widget_set_overflow(home->context_body, GTK_OVERFLOW_HIDDEN);

	home->cluster_host = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_add_css_class(home->cluster_host, "home-cluster");
	gtk_widget_set_hexpand(home->cluster_host, TRUE);
	gtk_widget_set_vexpand(home->cluster_host, TRUE);
	home->context_grid = GTK_GRID(gtk_grid_new());
	gtk_widget_set_halign(GTK_WIDGET(home->context_grid), GTK_ALIGN_FILL);
	gtk_grid_set_column_homogeneous(home->context_grid, TRUE);
	gtk_grid_set_row_homogeneous(home->context_grid, TRUE);
	gtk_grid_set_column_spacing(home->context_grid, home_spacing(home));
	gtk_grid_set_row_spacing(home->context_grid, home_spacing(home));
	gtk_widget_set_hexpand(GTK_WIDGET(home->context_grid), TRUE);
	gtk_widget_set_vexpand(GTK_WIDGET(home->context_grid), TRUE);
	gtk_widget_set_overflow(GTK_WIDGET(home->context_grid),
	    GTK_OVERFLOW_HIDDEN);
	/* Keep explicit, non-targetable sentinels in every cluster track. */
	home->grid_anchor = gtk_label_new(" ");
	gtk_widget_set_size_request(home->grid_anchor, 1, 1);
	gtk_widget_set_hexpand(home->grid_anchor, TRUE);
	gtk_widget_set_vexpand(home->grid_anchor, TRUE);
	gtk_widget_set_opacity(home->grid_anchor, 0.0);
	gtk_widget_set_can_target(home->grid_anchor, false);
	gtk_grid_attach(home->context_grid, home->grid_anchor, 0, 0,
	    home_cols(home), home_cluster_rows(home));
	for (int column = 0; column < home_cols(home); column++) {
		GtkWidget *anchor = gtk_label_new(" ");

		gtk_widget_set_size_request(anchor, 1, 1);
		gtk_widget_set_opacity(anchor, 0.0);
		gtk_widget_set_can_target(anchor, false);
		gtk_grid_attach(home->context_grid, anchor, column, 0, 1, 1);
	}
	for (int row = 1; row < home_cluster_rows(home); row++) {
		GtkWidget *anchor = gtk_label_new(" ");

		gtk_widget_set_size_request(anchor, 1, 1);
		gtk_widget_set_opacity(anchor, 0.0);
		gtk_widget_set_can_target(anchor, false);
		gtk_grid_attach(home->context_grid, anchor, 0, row, 1, 1);
	}
	gtk_box_append(GTK_BOX(home->cluster_host),
	    GTK_WIDGET(home->context_grid));

	home->strip_host = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_add_css_class(home->strip_host, "home-strip");
	gtk_widget_set_hexpand(home->strip_host, TRUE);
	gtk_widget_set_vexpand(home->strip_host, FALSE);
	home->strip_grid = GTK_GRID(gtk_grid_new());
	gtk_grid_set_column_homogeneous(home->strip_grid, TRUE);
	gtk_grid_set_row_homogeneous(home->strip_grid, TRUE);
	gtk_grid_set_column_spacing(home->strip_grid, home_spacing(home));
	gtk_grid_set_row_spacing(home->strip_grid, home_spacing(home));
	gtk_widget_set_hexpand(GTK_WIDGET(home->strip_grid), TRUE);
	gtk_widget_set_vexpand(GTK_WIDGET(home->strip_grid), TRUE);
	seed_strip_grid_anchors(home);
	gtk_box_append(GTK_BOX(home->strip_host), GTK_WIDGET(home->strip_grid));
	gtk_box_append(GTK_BOX(home->context_body), home->cluster_host);
	gtk_box_append(GTK_BOX(home->context_body), home->strip_host);
	gtk_widget_add_css_class(home->context_body, "home-page-panel");
	gtk_widget_set_hexpand(home->context_body, FALSE);
	gtk_widget_set_vexpand(home->context_body, TRUE);

	/*
	 * Viewport is an overlay whose main child is an empty pad that takes
	 * the normal allocation. The page strip is an overlay child so it can
	 * be N×viewport wide without GTK compressing pages into one width.
	 */
	home->page_viewport = gtk_overlay_new();
	gtk_widget_add_css_class(home->page_viewport, "home-page-viewport");
	gtk_widget_set_hexpand(home->page_viewport, TRUE);
	gtk_widget_set_vexpand(home->page_viewport, TRUE);
	/* GSK pages include the root gutter in their stride and are clipped only
	 * by rotation_host at the physical output edge. */
	gtk_widget_set_overflow(home->page_viewport, home->pager_gsk ?
	    GTK_OVERFLOW_VISIBLE : GTK_OVERFLOW_HIDDEN);
	home->page_viewport_pad = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_hexpand(home->page_viewport_pad, TRUE);
	gtk_widget_set_vexpand(home->page_viewport_pad, TRUE);
	gtk_overlay_set_child(GTK_OVERLAY(home->page_viewport),
	    home->page_viewport_pad);
	home->page_strip = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_add_css_class(home->page_strip, "home-page-strip");
	gtk_widget_set_hexpand(home->page_strip, FALSE);
	gtk_widget_set_vexpand(home->page_strip, TRUE);
	gtk_widget_set_halign(home->page_strip, GTK_ALIGN_START);
	gtk_widget_set_valign(home->page_strip, GTK_ALIGN_FILL);
	if (home->pager_gsk) {
		home->page_motion = home_pager_motion_new(home->page_strip);
		home->page_motion->prepare = prepare_home_pager_allocation;
		home->page_motion->prepare_data = home;
		gtk_widget_set_hexpand(GTK_WIDGET(home->page_motion), TRUE);
		gtk_widget_set_vexpand(GTK_WIDGET(home->page_motion), TRUE);
		gtk_widget_set_halign(GTK_WIDGET(home->page_motion), GTK_ALIGN_FILL);
		gtk_widget_set_valign(GTK_WIDGET(home->page_motion), GTK_ALIGN_FILL);
		gtk_overlay_add_overlay(GTK_OVERLAY(home->page_viewport),
		    GTK_WIDGET(home->page_motion));
	} else {
		gtk_overlay_add_overlay(GTK_OVERLAY(home->page_viewport),
		    home->page_strip);
	}
	home->page_panel_count = 0;
	home->page_panels[0] = home->context_body;
	home->page_grids[0] = home->context_grid;
	gtk_box_append(GTK_BOX(home->page_strip), home->context_body);
	home->page_panel_count = 1;
	home->edit_scene = GTK_OVERLAY(gtk_overlay_new());
	gtk_overlay_set_child(home->edit_scene, home->page_viewport);
	home->edit_controls = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	gtk_widget_add_css_class(home->edit_controls, "home-edit-controls");
	home->edit_frame = home_edit_frame_new(GTK_WIDGET(home->edit_scene),
	    home->edit_controls);
	gtk_widget_set_hexpand(GTK_WIDGET(home->edit_frame), true);
	gtk_widget_set_vexpand(GTK_WIDGET(home->edit_frame), true);
	gtk_overlay_set_child(home->context_overlay, GTK_WIDGET(home->edit_frame));
	home->grid_overlay = GTK_DRAWING_AREA(gtk_drawing_area_new());
	gtk_widget_set_hexpand(GTK_WIDGET(home->grid_overlay), TRUE);
	gtk_widget_set_vexpand(GTK_WIDGET(home->grid_overlay), TRUE);
	gtk_widget_set_can_target(GTK_WIDGET(home->grid_overlay), false);
	gtk_drawing_area_set_draw_func(home->grid_overlay, draw_grid_overlay, home,
	    NULL);
	gtk_overlay_add_overlay(home->edit_scene,
	    GTK_WIDGET(home->grid_overlay));
	home->launcher_remove_banner = gtk_label_new("Drag here to remove");
	gtk_widget_add_css_class(home->launcher_remove_banner,
	    "home-launcher-remove");
	gtk_widget_set_halign(home->launcher_remove_banner, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(home->launcher_remove_banner, GTK_ALIGN_START);
	gtk_widget_set_margin_top(home->launcher_remove_banner, 10);
	gtk_widget_set_can_target(home->launcher_remove_banner, false);
	gtk_widget_set_visible(home->launcher_remove_banner, FALSE);
	gtk_overlay_add_overlay(home->context_overlay,
	    home->launcher_remove_banner);
	{
		GtkGesture *grid_drag = gtk_gesture_drag_new();
		GtkGesture *launcher_long_press = gtk_gesture_long_press_new();
		GtkGesture *launcher_click = gtk_gesture_click_new();

		home->layout_drag = grid_drag;
		gtk_gesture_single_set_exclusive(GTK_GESTURE_SINGLE(grid_drag), true);
		gtk_event_controller_set_propagation_phase(
		    GTK_EVENT_CONTROLLER(grid_drag), GTK_PHASE_BUBBLE);
		g_signal_connect(grid_drag, "drag-begin",
		    G_CALLBACK(home_widget_drag_begin), home);
		g_signal_connect(grid_drag, "drag-update",
		    G_CALLBACK(home_widget_drag_update), home);
		g_signal_connect(grid_drag, "drag-end",
		    G_CALLBACK(home_widget_drag_end), home);
		/* Match drag_icon_arm_ms (~350ms; default long-press is 500ms). */
		gtk_gesture_long_press_set_delay_factor(
		    GTK_GESTURE_LONG_PRESS(launcher_long_press),
		    (double)home->config.drag_icon_arm_ms / 500.0);
		gtk_event_controller_set_propagation_phase(
		    GTK_EVENT_CONTROLLER(launcher_long_press), GTK_PHASE_CAPTURE);
		g_signal_connect(launcher_long_press, "pressed",
		    G_CALLBACK(home_launcher_long_pressed), home);
		gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(launcher_click),
		    GDK_BUTTON_PRIMARY);
		gtk_event_controller_set_propagation_phase(
		    GTK_EVENT_CONTROLLER(launcher_click), GTK_PHASE_BUBBLE);
		g_signal_connect(launcher_click, "pressed",
		    G_CALLBACK(home_context_pressed), home);
		g_signal_connect(launcher_click, "released",
		    G_CALLBACK(home_context_icon_clicked), home);
		gtk_widget_add_controller(GTK_WIDGET(home->context_overlay),
		    GTK_EVENT_CONTROLLER(grid_drag));
		gtk_widget_add_controller(GTK_WIDGET(home->context_overlay),
		    GTK_EVENT_CONTROLLER(launcher_long_press));
		gtk_widget_add_controller(GTK_WIDGET(home->context_overlay),
		    GTK_EVENT_CONTROLLER(launcher_click));
		/* Keep long-press out of the drag group so a still hold can
		 * claim without waiting for drag-begin movement. */
		gtk_gesture_group(GTK_GESTURE(grid_drag),
		    GTK_GESTURE(launcher_click));
	}
	page_swipe = gtk_gesture_swipe_new();
	g_signal_connect(page_swipe, "swipe", G_CALLBACK(launcher_swiped), home);
	gtk_event_controller_set_propagation_phase(
	    GTK_EVENT_CONTROLLER(page_swipe), GTK_PHASE_CAPTURE);
	gtk_widget_add_controller(GTK_WIDGET(home->context_overlay),
	    GTK_EVENT_CONTROLLER(page_swipe));
	{
		GtkGesture *page_drag = gtk_gesture_drag_new();

		gtk_gesture_single_set_exclusive(GTK_GESTURE_SINGLE(page_drag),
		    false);
		gtk_event_controller_set_propagation_phase(
		    GTK_EVENT_CONTROLLER(page_drag), GTK_PHASE_CAPTURE);
		g_signal_connect(page_drag, "drag-begin",
		    G_CALLBACK(home_page_drag_begin), home);
		g_signal_connect(page_drag, "drag-update",
		    G_CALLBACK(home_page_drag_update), home);
		g_signal_connect(page_drag, "drag-end",
		    G_CALLBACK(home_page_drag_end), home);
		gtk_widget_add_controller(GTK_WIDGET(home->context_overlay),
		    GTK_EVENT_CONTROLLER(page_drag));
	}
	page_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	home->launcher_page_bar = page_bar;
	gtk_widget_add_css_class(page_bar, "home-launcher-page-bar");
	gtk_widget_set_halign(page_bar, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(page_bar, GTK_ALIGN_END);
	gtk_widget_set_margin_bottom(page_bar, 0);
	gtk_widget_set_visible(page_bar, FALSE);
	home->launcher_page_remove = GTK_BUTTON(
	    gtk_button_new_with_label("−"));
	gtk_widget_set_focusable(GTK_WIDGET(home->launcher_page_remove), FALSE);
	home->launcher_page_label = GTK_LABEL(gtk_label_new("Screen 1 of 1"));
	gtk_widget_add_css_class(GTK_WIDGET(home->launcher_page_label),
	    "home-launcher-page-label");
	home->launcher_page_add = GTK_BUTTON(gtk_button_new_with_label("+"));
	gtk_widget_set_focusable(GTK_WIDGET(home->launcher_page_add), FALSE);
	gtk_widget_set_tooltip_text(GTK_WIDGET(home->launcher_page_add), "Add Home page");
	/* GtkButton's automatic label relation would otherwise expose only "+"/"−". */
	gtk_accessible_reset_relation(GTK_ACCESSIBLE(home->launcher_page_add),
	    GTK_ACCESSIBLE_RELATION_LABELLED_BY);
	gtk_accessible_update_property(GTK_ACCESSIBLE(home->launcher_page_add),
	    GTK_ACCESSIBLE_PROPERTY_LABEL, "Add Home page", -1);
	gtk_widget_set_tooltip_text(GTK_WIDGET(home->launcher_page_remove), "Remove current Home page");
	gtk_accessible_reset_relation(GTK_ACCESSIBLE(home->launcher_page_remove),
	    GTK_ACCESSIBLE_RELATION_LABELLED_BY);
	gtk_accessible_update_property(GTK_ACCESSIBLE(home->launcher_page_remove),
	    GTK_ACCESSIBLE_PROPERTY_LABEL, "Remove current Home page", -1);
	g_signal_connect(home->launcher_page_remove, "clicked",
	    G_CALLBACK(launcher_page_remove_clicked), home);
	g_signal_connect(home->launcher_page_add, "clicked",
	    G_CALLBACK(launcher_page_add_clicked), home);
	gtk_box_append(GTK_BOX(page_bar),
	    GTK_WIDGET(home->launcher_page_remove));
	gtk_box_append(GTK_BOX(page_bar), GTK_WIDGET(home->launcher_page_label));
	gtk_box_append(GTK_BOX(page_bar), GTK_WIDGET(home->launcher_page_add));
	{
		GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 7);
		GtkWidget *add_widget = gtk_button_new_with_label("Add widget");
		GtkWidget *wallpaper = gtk_button_new_with_label("Wallpaper");
		GtkWidget *done = gtk_button_new_with_label("Done");
		GtkWidget *picker = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
		GtkWidget *picker_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
		GtkWidget *picker_title = gtk_label_new("Add widget");
		GtkWidget *picker_close = gtk_button_new_with_label("Close");
		GtkWidget *scroller = gtk_scrolled_window_new();
		GtkWidget *list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);

		home->edit_toolbar = toolbar;
		gtk_widget_add_css_class(toolbar, "home-edit-toolbar");
		gtk_widget_set_halign(toolbar, GTK_ALIGN_CENTER);
		gtk_widget_set_valign(toolbar, GTK_ALIGN_END);
		gtk_widget_set_margin_bottom(toolbar, 0);
		gtk_widget_set_visible(toolbar, false);
		GtkWidget *actions[] = { add_widget, wallpaper, done };
		for (size_t i = 0; i < G_N_ELEMENTS(actions); i++) {
			gtk_widget_add_css_class(actions[i], "home-edit-action");
			gtk_widget_set_focusable(actions[i], false);
			gtk_box_append(GTK_BOX(toolbar), actions[i]);
		}
		gtk_widget_add_css_class(done, "home-edit-done");
		g_signal_connect(add_widget, "clicked",
		    G_CALLBACK(widget_picker_open_clicked), home);
		g_signal_connect(wallpaper, "clicked",
		    G_CALLBACK(wallpaper_edit_clicked), home);
		g_signal_connect(done, "clicked", G_CALLBACK(edit_done_clicked), home);
		gtk_box_append(GTK_BOX(home->edit_controls), toolbar);
		gtk_box_append(GTK_BOX(home->edit_controls), page_bar);

		GtkWidget *picker_host = home_picker_host_new(picker);
		home->widget_picker = picker_host;
		home->widget_picker_list = list;
		home->widget_picker_status = GTK_LABEL(gtk_label_new(""));
		gtk_widget_add_css_class(picker, "home-widget-picker");
		gtk_widget_set_focusable(picker_host, true);
		gtk_widget_set_visible(picker_host, false);
		gtk_widget_add_css_class(picker_title, "home-widget-picker-title");
		gtk_label_set_ellipsize(GTK_LABEL(picker_title), PANGO_ELLIPSIZE_END);
		gtk_label_set_width_chars(GTK_LABEL(picker_title), 1);
		gtk_widget_set_hexpand(picker_title, true);
		gtk_widget_set_halign(picker_title, GTK_ALIGN_START);
		gtk_widget_add_css_class(picker_close, "home-edit-action");
		gtk_widget_set_focusable(picker_close, false);
		g_signal_connect(picker_close, "clicked",
		    G_CALLBACK(widget_picker_close_clicked), home);
		gtk_box_append(GTK_BOX(picker_header), picker_title);
		gtk_box_append(GTK_BOX(picker_header), picker_close);
		gtk_box_append(GTK_BOX(picker), picker_header);
		gtk_widget_add_css_class(GTK_WIDGET(home->widget_picker_status),
		    "home-widget-picker-status");
		gtk_label_set_wrap(home->widget_picker_status, true);
		gtk_widget_set_halign(GTK_WIDGET(home->widget_picker_status),
		    GTK_ALIGN_START);
		gtk_box_append(GTK_BOX(picker),
		    GTK_WIDGET(home->widget_picker_status));
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
		    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
		gtk_widget_set_vexpand(scroller, true);
		gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroller), true);
		gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroller), 240);
		gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), list);
		gtk_box_append(GTK_BOX(picker), scroller);
		gtk_overlay_add_overlay(home->context_overlay, picker_host);
	}
	performance = performance_tile(home);
	gtk_widget_set_visible(performance, FALSE);
	home->grid_items[0] = (struct home_grid_item){
		.home = home, .id = "weather", .column = 0, .row = 0,
		.column_span = home_cols(home), .row_span = 1,
	};
	home->grid_items[1] = (struct home_grid_item){
		.home = home, .id = "clock", .column = 0, .row = 1,
		.column_span = 2, .row_span = 2,
	};
	home->grid_items[2] = (struct home_grid_item){
		.home = home, .id = "calendar", .column = 2, .row = 1,
		.column_span = home_cols(home) - 2, .row_span = 2,
	};
	home->grid_items[3] = (struct home_grid_item){
		.home = home, .id = "glance", .column = 0, .row = 3,
		.column_span = home_cols(home), .row_span = 1,
	};
	home->grid_item_count = HOME_GRID_BUILTINS;
	home_widget_container(home, &home->grid_items[0], weather_card(home));
	home_widget_container(home, &home->grid_items[1], analog_clock_card(home));
	home_widget_container(home, &home->grid_items[2], calendar_card(home));
	home_widget_container(home, &home->grid_items[3], glance_card(home));
	{
		char *user_widgets = g_build_filename(g_get_home_dir(), ".config",
		    "ctlst", "widgets", NULL);
		char *share_widgets = g_build_filename(g_get_home_dir(),
		    ".local", "share", "sway-touch", "widgets", NULL);
		const char *system_widgets = "/usr/share/ctlst/widgets";

		home->widget_desc_count = home_widget_desc_scan_dir(
		    home->widget_descs, HOME_WIDGET_DESC_MAX, 0, user_widgets);
		home->widget_desc_count = home_widget_desc_scan_dir(
		    home->widget_descs, HOME_WIDGET_DESC_MAX,
		    home->widget_desc_count, share_widgets);
		home->widget_desc_count = home_widget_desc_scan_dir(
		    home->widget_descs, HOME_WIDGET_DESC_MAX,
		    home->widget_desc_count, system_widgets);
		g_free(user_widgets);
		g_free(share_widgets);
	}
	/* Descriptor intake happens before layout restore so discovered widgets,
	 * including currently removed ones, remain available to the edit picker. */
	for (size_t i = 0; i < home->grid_item_count; i++) {
		if (widget_id_is_hidden(home, home->grid_items[i].id))
			home->grid_items[i].hidden = true;
	}
	add_external_widgets(home);
	load_home_layout(home);
	/* Explicit home.yaml hides win over persisted layout visibility and free
	 * those cells before placing a newly enabled external widget. */
	for (size_t i = 0; i < home->grid_item_count; i++) {
		if (widget_id_is_hidden(home, home->grid_items[i].id))
			home->grid_items[i].hidden = true;
		if (!home->grid_items[i].hidden &&
		    !ensure_external_widget_runtime(&home->grid_items[i]))
			home->grid_items[i].hidden = true;
	}
	reconcile_widgets_with_launcher_icons(home);
	apply_home_grid(home);
	gtk_box_append(GTK_BOX(root), home->context);

	if (!create_socket(home)) {
		g_warning("cannot create Home socket: %s", g_strerror(errno));
		g_application_quit(G_APPLICATION(application));
		return;
	}
	home->socket_watch_source = g_timeout_add_seconds(1,
	    ensure_socket, home);
	refresh_launcher_icons(home);
	home->clock_source = g_timeout_add_seconds(1, refresh_clock, home);
	home->status_source = g_timeout_add_seconds(8, refresh_status, home);
	home->activity_source = g_timeout_add(1000, refresh_activity, home);
	home->weather_source = g_timeout_add_seconds(60, refresh_weather, home);
	refresh_clock(home);
	refresh_status(home);
	refresh_weather(home);
	request_weather_fetch();
	set_visible(home, true);
	g_application_hold(G_APPLICATION(application));
}

static void
shutdown_app(GApplication *application, gpointer data)
{
	struct home *home = data;

	(void)application;
	g_clear_pointer(&home->pending_action_id, g_free);
	unlink(home->state_path);
	unlink(home->focus_path);
	unlink(home->layout_edit_path);
	unlink(home->prompt_length_path);
	unlink(home->rotation_ready_path);
	if (owns_socket_path(home))
		unlink(home->socket_path);
	if (home->socket_watch_source != 0)
		g_source_remove(home->socket_watch_source);
	if (home->control_source != 0)
		g_source_remove(home->control_source);
	if (home->clock_source != 0)
		g_source_remove(home->clock_source);
	if (home->analog_clock_source != 0)
		g_source_remove(home->analog_clock_source);
	if (home->status_source != 0)
		g_source_remove(home->status_source);
	if (home->activity_source != 0)
		g_source_remove(home->activity_source);
	if (home->weather_source != 0)
		g_source_remove(home->weather_source);
	if (home->launcher_layout_source != 0)
		g_source_remove(home->launcher_layout_source);
	if (home->launcher_drag_arm_source != 0)
		g_source_remove(home->launcher_drag_arm_source);
	if (home->launcher_push_dwell_source != 0)
		g_source_remove(home->launcher_push_dwell_source);
	if (home->page_edge_dwell_source != 0)
		g_source_remove(home->page_edge_dwell_source);
	if (home->control_fd >= 0)
		close(home->control_fd);
	if (home->haptic_fd >= 0) {
		if (home->haptic_effect_id >= 0)
			(void)ioctl(home->haptic_fd, EVIOCRMFF,
			    home->haptic_effect_id);
		close(home->haptic_fd);
	}
	g_clear_object(&home->theme_monitor);
	g_clear_object(&home->theme_provider);
}

int
main(int argc, char **argv)
{
	struct home home = {
		.control_fd = -1,
		.haptic_fd = -1,
		.haptic_effect_id = -1,
		.page_settle_commit = -1,
	};
	GtkApplication *application = gtk_application_new(APP_ID,
	    G_APPLICATION_DEFAULT_FLAGS);
	int status;

	g_signal_connect(application, "activate", G_CALLBACK(activate), &home);
	g_signal_connect(application, "shutdown", G_CALLBACK(shutdown_app), &home);
	status = g_application_run(G_APPLICATION(application), argc, argv);
	g_object_unref(application);
	return status;
}
