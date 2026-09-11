#define _GNU_SOURCE

#include "../ctlst-runtime.h"
#include <errno.h>
#include <fcntl.h>
#include <glib-unix.h>
#include <gtk/gtk.h>
#include <gtk4-layer-shell.h>
#include <glib/gstdio.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#define CONTROL_MESSAGE_SIZE 32
/* Full-height sheet; control padding belongs inside the painted surface. */
#define SHADE_BOTTOM_MARGIN 0
#define SHADE_SETTLE_DURATION_US 180000.0

static const char *component_css =
    "window#ctlstshade.gsk-motion { background: transparent;"
    " background-image: none; }"
    ".shade-root { background: alpha(@ctlst_bg, 0.94); padding: 8px 12px 14px;"
    " font-family: 'Noto Sans'; }"
    ".shade-page { padding: 2px 0 0; }"
    ".shade-header { margin: 2px 0 0; }"
    ".shade-handle { min-width: 46px; min-height: 5px; margin: 0 0 8px;"
    " border-radius: 3px; background: alpha(@ctlst_text, 0.18); }"
    ".shade-title { color: @ctlst_text; font-size: 24px; font-weight: 800; }"
    ".shade-subtitle { color: @ctlst_muted; font-size: 11px; }"
    ".shade-grid { margin: 6px 0 2px; }"
    ".shade-tile { min-height: 70px; padding: 10px 12px; border: 0;"
    " border-radius: 20px; color: @ctlst_text; background: alpha(@ctlst_panel, 0.82);"
    " box-shadow: 0 2px 7px alpha(@ctlst_text, 0.06); font-weight: 800; }"
    ".shade-tile.active { color: @ctlst_accent; border-color: @ctlst_accent;"
    " background: alpha(@ctlst_accent_soft, 0.92);"
    " box-shadow: inset 0 0 0 1px alpha(@ctlst_accent, 0.44); }"
    ".shade-settings-box { padding: 10px; border-radius: 22px;"
    " background: alpha(@ctlst_panel, 0.82);"
    " box-shadow: 0 3px 12px alpha(@ctlst_text, 0.08); }"
    ".shade-control-tile { min-height: 68px; padding: 7px 4px; border: 0;"
    " border-radius: 18px; color: @ctlst_text;"
    " background: alpha(@ctlst_raised, 0.62); box-shadow: none; }"
    ".shade-control-tile.active { color: @ctlst_panel; background: @ctlst_accent; }"
    ".shade-control-icon { color: @ctlst_accent; font-family: 'Symbols Nerd Font',"
    " 'Noto Sans Symbols 2', 'Noto Sans'; font-size: 22px; font-weight: 500; }"
    ".shade-control-tile.active .shade-control-icon { color: @ctlst_panel; }"
    ".shade-control-label { color: inherit; font-size: 10px; font-weight: 750; }"
    ".shade-primary > * { min-width: 120px; margin: 4px; }"
    ".shade-utilities > * { min-width: 120px; margin: 4px; }"
    ".shade-control-detail { min-width: 44px; min-height: 44px; padding: 0; }"
    ".shade-mini-control { min-height: 40px; padding: 0 8px; border: 0;"
    " border-radius: 14px; color: @ctlst_text;"
    " background: alpha(@ctlst_raised, 0.52); box-shadow: none; }"
    ".shade-mini-control.active { color: @ctlst_panel; background: @ctlst_accent; }"
    ".shade-mini-control .shade-control-icon { font-size: 17px; }"
    ".shade-toggle-row { min-height: 42px; padding: 0 4px; }"
    ".shade-toggle-title { color: @ctlst_text; font-size: 12px; font-weight: 750; }"
    ".shade-toggle-state { color: @ctlst_muted; font-size: 10px; }"
    ".shade-toggle-row switch { min-width: 42px; min-height: 24px;"
    " border-radius: 999px; background: alpha(@ctlst_muted, 0.30); }"
    ".shade-toggle-row switch slider { min-width: 20px; min-height: 20px;"
    " margin: 2px; border-radius: 999px; background: @ctlst_panel;"
    " box-shadow: 0 1px 3px alpha(@ctlst_text, 0.18); }"
    ".shade-toggle-row switch:checked { background: @ctlst_accent; }"
    ".shade-status-grid { margin-top: 2px; }"
    ".shade-status-grid .shade-card { padding: 8px 10px; border-radius: 15px; }"
    ".shade-status-grid .shade-network { font-size: 12px; }"
    ".shade-status-grid .shade-detail { font-size: 9px; }"
    ".shade-notification-header { margin: 8px 2px 4px; }"
    ".shade-notification-title { color: @ctlst_text; font-size: 17px; font-weight: 800; }"
    ".shade-card { padding: 14px; border: 0; border-radius: 20px;"
    " background: alpha(@ctlst_panel, 0.80);"
    " box-shadow: 0 2px 7px alpha(@ctlst_text, 0.05); }"
    ".shade-card-network { background: alpha(@ctlst_panel_alt, 0.78); }"
    ".shade-card-cellular { background: alpha(@ctlst_accent_soft, 0.52); }"
    ".shade-network { color: @ctlst_text; font-size: 16px; font-weight: 800; }"
    ".shade-detail { color: @ctlst_muted; font-size: 11px; }"
    ".shade-section { margin-top: 8px; color: @ctlst_muted;"
    " font-size: 10px; font-weight: 800; letter-spacing: 0.7px; }"
    ".shade-action { min-height: 46px; padding: 0 12px; border: 0;"
    " border-radius: 14px; color: @ctlst_text;"
    " background: alpha(@ctlst_panel, 0.64); font-weight: 800; }"
    ".shade-header-close { color: @ctlst_accent; background: alpha(@ctlst_accent_soft, 0.74); }"
    ".shade-action:hover, .shade-action:active { background: @ctlst_accent_soft; }"
    ".shade-chip { min-height: 40px; padding: 0 10px; border: 0;"
    " border-radius: 20px; color: @ctlst_text;"
    " background: alpha(@ctlst_panel, 0.72); font-size: 10px; font-weight: 800; }"
    ".shade-close { color: @ctlst_panel; background: @ctlst_danger;"
    " border-color: @ctlst_danger; }"
    ".wifi-row { min-height: 62px; margin: 0 0 7px; padding: 8px 12px; border: 0;"
    " border-radius: 16px; color: @ctlst_text; background: alpha(@ctlst_panel, 0.74); }"
    ".wifi-current { color: @ctlst_accent; font-weight: 700; }"
    ".device-row { min-height: 62px; margin: 0 0 7px; padding: 8px 12px; border: 0;"
    " border-radius: 16px; color: @ctlst_text; background: alpha(@ctlst_panel, 0.74); }"
    ".device-connected { color: @ctlst_accent; font-weight: 700; }"
    ".notification-row { min-height: 72px; margin: 0 0 7px; padding: 10px 12px; border: 0;"
    " border-radius: 16px; color: @ctlst_text; background: alpha(@ctlst_panel, 0.76); }"
    ".notification-row-dragging { background: @ctlst_accent_soft; }"
    ".notification-app { color: @ctlst_accent; font-size: 9px;"
    " font-weight: 700; }"
    ".notification-summary { color: @ctlst_text; font-size: 14px;"
    " font-weight: 700; }"
    ".notification-body { color: @ctlst_muted; font-size: 11px; }"
    ".shade-slider { color: @ctlst_accent; }"
    ".shade-slider trough { min-height: 8px; border-radius: 4px; background: @ctlst_raised; }"
    ".shade-slider highlight { background: @ctlst_accent; }"
    ".shade-slider slider { min-width: 22px; min-height: 22px;"
    " border-radius: 999px; background: @ctlst_accent; }"
    ".password-window { padding: 18px; color: @ctlst_text;"
    " background: @ctlst_panel; }"
    ".password-window entry { min-height: 44px; margin: 12px 0;"
    " color: @ctlst_text; background: @ctlst_raised;"
    " border: 1px solid @ctlst_line; }";

struct app {
	GtkApplication *application;
	GtkWindow *window;
	GtkWidget *panel;
	GtkStack *stack;
	GtkBox *network_list;
	GtkBox *bluetooth_list;
	GtkBox *notification_list;
	GtkWidget *notification_clear;
	GtkLabel *network_name;
	GtkLabel *network_detail;
	GtkLabel *cell_name;
	GtkLabel *cell_detail;
	GtkLabel *wifi_status;
	GtkLabel *bluetooth_status;
	GtkButton *wifi_button;
	GtkButton *bluetooth_button;
	GtkButton *sound_button;
	GtkButton *tailscale_button;
	GtkSwitch *rotation_switch;
	GtkButton *screen_idle_button;
	GtkButton *airplane_button;
	GtkScale *brightness;
	GtkLabel *rotation_label;
	GtkLabel *screen_idle_label;
	GtkLabel *tailscale_label;
	GtkLabel *airplane_label;
	GtkLabel *sound_status;
	GtkWidget *sound_profiles;
	GtkCssProvider *css;
	char runtime_dir[256];
	char control_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
	char gesture_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
	char visible_path[256];
	int control_fd;
	guint control_source;
	guint64 rotation_generation;
	bool visible;
	bool wifi_enabled;
	bool bluetooth_enabled;
	bool tailscale_connected;
	bool rotation_locked;
	bool rotation_action_running;
	bool sound_action_running;
	bool updating_rotation_switch;
	bool modem_enabled;
	bool updating_brightness;
	int brightness_target;
	guint brightness_write_source;
	bool brightness_write_running;
	guint64 brightness_generation;
	bool status_refresh_running;
	bool notification_refresh_running;
	bool notification_refresh_pending;
	bool gsk_motion;
	double close_drag_x;
	double close_drag_y;
	bool close_drag_allowed;
	bool pull_preview;
	int shell_height;
	double pull_progress;
	double settle_from;
	double settle_target;
	gint64 settle_start_us;
	guint settle_tick;
};

typedef struct _CtlsMotionPanel CtlsMotionPanel;
typedef struct _CtlsMotionPanelClass CtlsMotionPanelClass;

struct _CtlsMotionPanel {
	GtkWidget parent_instance;
	GtkWidget *child;
	double translation_y;
};

struct _CtlsMotionPanelClass {
	GtkWidgetClass parent_class;
};

G_DEFINE_TYPE(CtlsMotionPanel, ctlst_motion_panel, GTK_TYPE_WIDGET)

static void
ctlst_motion_panel_measure(GtkWidget *widget, GtkOrientation orientation,
	int for_size, int *minimum, int *natural, int *minimum_baseline,
	int *natural_baseline)
{
	CtlsMotionPanel *panel = (CtlsMotionPanel *)widget;

	if (panel->child != NULL) {
		gtk_widget_measure(panel->child, orientation, for_size, minimum,
		    natural, minimum_baseline, natural_baseline);
		return;
	}
	*minimum = 0;
	*natural = 0;
	*minimum_baseline = -1;
	*natural_baseline = -1;
}

static void
ctlst_motion_panel_size_allocate(GtkWidget *widget, int width, int height,
	int baseline)
{
	CtlsMotionPanel *panel = (CtlsMotionPanel *)widget;

	if (panel->child != NULL)
		gtk_widget_allocate(panel->child, width, height, baseline, NULL);
}

static void
ctlst_motion_panel_snapshot(GtkWidget *widget, GtkSnapshot *snapshot)
{
	CtlsMotionPanel *panel = (CtlsMotionPanel *)widget;
	graphene_point_t offset;

	if (panel->child == NULL)
		return;
	offset = GRAPHENE_POINT_INIT(0.0f, (float)panel->translation_y);
	gtk_snapshot_save(snapshot);
	gtk_snapshot_translate(snapshot, &offset);
	gtk_widget_snapshot_child(widget, panel->child, snapshot);
	gtk_snapshot_restore(snapshot);
}

static void
ctlst_motion_panel_dispose(GObject *object)
{
	CtlsMotionPanel *panel = (CtlsMotionPanel *)object;

	if (panel->child != NULL) {
		gtk_widget_unparent(panel->child);
		panel->child = NULL;
	}
	G_OBJECT_CLASS(ctlst_motion_panel_parent_class)->dispose(object);
}

static void
ctlst_motion_panel_class_init(CtlsMotionPanelClass *class)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(class);
	GObjectClass *object_class = G_OBJECT_CLASS(class);

	widget_class->measure = ctlst_motion_panel_measure;
	widget_class->size_allocate = ctlst_motion_panel_size_allocate;
	widget_class->snapshot = ctlst_motion_panel_snapshot;
	object_class->dispose = ctlst_motion_panel_dispose;
}

static void
ctlst_motion_panel_init(CtlsMotionPanel *panel)
{
	panel->translation_y = 0.0;
}

static GtkWidget *
ctlst_motion_panel_new(GtkWidget *child)
{
	CtlsMotionPanel *panel;

	panel = g_object_new(ctlst_motion_panel_get_type(), NULL);
	gtk_widget_set_parent(child, GTK_WIDGET(panel));
	panel->child = child;
	return GTK_WIDGET(panel);
}

static void
ctlst_motion_panel_set_translation(CtlsMotionPanel *panel, double y)
{
	panel->translation_y = y;
	gtk_widget_queue_draw(GTK_WIDGET(panel));
}

struct status_snapshot {
	struct app *app;
	char *ssid;
	char *network;
	char *cell_heading;
	char *cell_summary;
	char *screen_idle;
	char *tailscale_label;
	bool wifi_enabled;
	bool bluetooth_enabled;
	bool tailscale_connected;
	bool rotation_locked;
	bool modem_enabled;
	guint64 rotation_generation;
	int brightness;
	guint64 brightness_generation;
	bool brightness_valid;
};

struct status_request {
	struct app *app;
	guint64 rotation_generation;
	guint64 brightness_generation;
	bool brightness_valid;
};

struct rotation_request {
	struct app *app;
	bool locked;
};

static void show_quick_settings(struct app *app);
static gboolean refresh_notifications(gpointer data);
static gboolean refresh_notifications_later(gpointer data);

static double
clamp01(double value)
{
	if (value < 0.0)
		return 0.0;
	if (value > 1.0)
		return 1.0;
	return value;
}

static int
shade_shell_height(struct app *app)
{
	GdkDisplay *display;
	GListModel *monitors;
	GdkMonitor *monitor;
	GdkRectangle geometry;
	int height;

	display = gtk_widget_get_display(GTK_WIDGET(app->window));
	monitors = gdk_display_get_monitors(display);
	if (monitors == NULL || g_list_model_get_n_items(monitors) < 1)
		return 900 - SHADE_BOTTOM_MARGIN;
	monitor = GDK_MONITOR(g_list_model_get_item(monitors, 0));
	gdk_monitor_get_geometry(monitor, &geometry);
	height = geometry.height - SHADE_BOTTOM_MARGIN;
	return height > 120 ? height : 900 - SHADE_BOTTOM_MARGIN;
}

static void
apply_gsk_translation(struct app *app, double progress)
{
	double y;

	if (!app->gsk_motion || app->panel == NULL)
		return;
	if (app->shell_height <= 0)
		app->shell_height = shade_shell_height(app);
	y = -app->shell_height * (1.0 - clamp01(progress));
	ctlst_motion_panel_set_translation((CtlsMotionPanel *)app->panel, y);
}

static void
apply_shade_margin(struct app *app, double progress)
{
	int margin;

	app->pull_progress = clamp01(progress);
	if (app->shell_height <= 0)
		app->shell_height = shade_shell_height(app);
	if (app->gsk_motion) {
		apply_gsk_translation(app, app->pull_progress);
		return;
	}
	margin = (int)lround(app->shell_height * (1.0 - app->pull_progress));
	if (margin < 0)
		margin = 0;
	if (margin > app->shell_height)
		margin = app->shell_height;
	/* Keep the top edge fixed while the drawer grows down from it.  With both
	 * vertical edges anchored, moving the top margin makes the surface grow up
	 * from the bottom instead, which reverses the pull direction. */
	gtk_layer_set_margin(app->window, GTK_LAYER_SHELL_EDGE_BOTTOM,
	    SHADE_BOTTOM_MARGIN + margin);
}

static void
stop_shade_settle(struct app *app)
{
	if (app->settle_tick != 0) {
		gtk_widget_remove_tick_callback(GTK_WIDGET(app->window),
		    app->settle_tick);
		app->settle_tick = 0;
	}
	app->settle_start_us = 0;
}

static gboolean
shade_settle_frame(GtkWidget *widget, GdkFrameClock *clock, gpointer data)
{
	struct app *app = data;
	gint64 frame_time = gdk_frame_clock_get_frame_time(clock);
	double elapsed;
	double progress;
	double eased;
	double value;

	(void)widget;
	if (app->settle_start_us == 0)
		app->settle_start_us = frame_time;
	elapsed = (double)(frame_time - app->settle_start_us);
	progress = clamp01(elapsed / SHADE_SETTLE_DURATION_US);
	eased = 1.0 - pow(1.0 - progress, 3.0);
	value = app->settle_from +
	    (app->settle_target - app->settle_from) * eased;
	apply_shade_margin(app, value);
	if (progress >= 1.0) {
		app->settle_tick = 0;
		app->settle_start_us = 0;
		return G_SOURCE_REMOVE;
	}
	return G_SOURCE_CONTINUE;
}

static void
start_shade_settle(struct app *app, double target)
{
	stop_shade_settle(app);
	app->settle_from = app->pull_progress;
	app->settle_target = clamp01(target);
	if (fabs(app->settle_from - app->settle_target) < 0.001) {
		apply_shade_margin(app, app->settle_target);
		return;
	}
	app->settle_tick = gtk_widget_add_tick_callback(GTK_WIDGET(app->window),
	    shade_settle_frame, app, NULL);
}

static void
hide_shade_preview(struct app *app)
{
	stop_shade_settle(app);
	gtk_layer_set_keyboard_mode(app->window, GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
	app->pull_preview = false;
	gtk_widget_set_visible(GTK_WIDGET(app->window), FALSE);
	if (app->gsk_motion)
		apply_gsk_translation(app, 0.0);
	if (!app->gsk_motion && app->shell_height > 0)
		gtk_layer_set_margin(app->window, GTK_LAYER_SHELL_EDGE_BOTTOM,
		    SHADE_BOTTOM_MARGIN + app->shell_height);
}

static void
preview_shade_progress(struct app *app, double progress)
{
	stop_shade_settle(app);
	if (progress < 0.02) {
		if (app->pull_preview)
			hide_shade_preview(app);
		return;
	}
	if (!app->pull_preview) {
		show_quick_settings(app);
		app->pull_preview = true;
		gtk_layer_set_keyboard_mode(app->window, GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
	}
	gtk_widget_set_visible(GTK_WIDGET(app->window), TRUE);
	apply_shade_margin(app, progress);
}

static bool
script_available(const char *name)
{
	char *path = ctlst_script_path(name);
	bool available = g_file_test(path, G_FILE_TEST_IS_REGULAR) &&
	    access(path, X_OK) == 0;

	g_free(path);
	return available;
}

static char *
run_capture(char *argv[])
{
	char *output = NULL;
	char *error_output = NULL;
	int status = 0;
	GError *error = NULL;

	if (!g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL,
	    &output, &error_output, &status, &error)) {
		g_clear_error(&error);
		g_free(error_output);
		return g_strdup("");
	}
	g_free(error_output);
	if (!g_spawn_check_wait_status(status, NULL)) {
		g_free(output);
		return g_strdup("");
	}
	return output != NULL ? output : g_strdup("");
}

static void
run_detached(char *argv[])
{
	GError *error = NULL;

	if (!g_spawn_async(NULL, argv, NULL,
	    G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL |
	    G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, &error))
		g_clear_error(&error);
}

static void
notify_gestures(struct app *app, char command)
{
	struct sockaddr_un address = {0};
	int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);

	if (fd < 0)
		return;
	address.sun_family = AF_UNIX;
	snprintf(address.sun_path, sizeof(address.sun_path), "%s",
	    app->gesture_path);
	sendto(fd, &command, 1, 0, (struct sockaddr *)&address,
	    sizeof(address));
	close(fd);
}

static void
set_text_input_owner(bool owned)
{
	const char *runtime = g_get_user_runtime_dir();
	char *path = g_build_filename(runtime, "ctlst-text-input-owner", NULL);
	char *current = NULL;
	char owner[32];
	gsize length = 0;

	if (owned) {
		snprintf(owner, sizeof(owner), "%ld\n", (long)getpid());
		g_file_set_contents(path, owner, -1, NULL);
	} else if (g_file_get_contents(path, &current, &length, NULL)) {
		char *end = NULL;
		long pid = strtol(current, &end, 10);

		if (end != current && pid == (long)getpid())
			g_unlink(path);
	}
	g_free(current);
	g_free(path);
}

static void
text_input_focus_changed(GObject *object, GParamSpec *property, gpointer data)
{
	GtkWidget *widget = GTK_WIDGET(object);

	(void)property;
	(void)data;
	set_text_input_owner(gtk_widget_has_focus(widget));
}

static void
begin_text_input(struct app *app, GtkWidget *widget)
{
	/* Layer surfaces are absent from Sway's window tree. Acquire compositor
	 * keyboard focus before GTK focuses the entry so the shared focus daemon
	 * can safely attribute the AT-SPI event to this shade process. */
	set_text_input_owner(true);
	gtk_layer_set_keyboard_mode(app->window,
	    GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE);
	gtk_window_set_focus(app->window, widget);
	gtk_widget_grab_focus(widget);
}

static void
end_text_input(struct app *app)
{
	set_text_input_owner(false);
	gtk_layer_set_keyboard_mode(app->window,
	    app->visible && !app->pull_preview ? GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE :
	    GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
}

static void
set_visible_state(struct app *app, bool visible)
{
	int fd;

	app->visible = visible;
	if (!visible) {
		set_text_input_owner(false);
		unlink(app->visible_path);
		notify_gestures(app, 'q');
		return;
	}
	fd = open(app->visible_path, O_WRONLY | O_CREAT | O_CLOEXEC, 0600);
	if (getenv("CTLST_SHADE_DEBUG") != NULL)
		fprintf(stderr, "shade visible=%d marker=%s fd=%d errno=%d\n",
		    visible, app->visible_path, fd, fd < 0 ? errno : 0);
	if (fd >= 0)
		close(fd);
	notify_gestures(app, 's');
}

static void
hide_shade(struct app *app)
{
	stop_shade_settle(app);
	end_text_input(app);
	gtk_layer_set_keyboard_mode(app->window, GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
	if (app->pull_preview) {
		hide_shade_preview(app);
		return;
	}
	app->pull_preview = false;
	gtk_widget_set_visible(GTK_WIDGET(app->window), FALSE);
	apply_shade_margin(app, 0.0);
	set_visible_state(app, false);
}

static void
handle_clicked(GtkGestureClick *gesture, int press_count, double x, double y,
    gpointer data)
{
	(void)gesture;
	(void)press_count;
	(void)x;
	(void)y;
	hide_shade(data);
}

static gboolean
key_pressed(GtkEventControllerKey *controller, guint keyval, guint keycode,
    GdkModifierType state, gpointer data)
{
	struct app *app = data;

	(void)controller;
	(void)keycode;
	(void)state;
	if (keyval != GDK_KEY_Escape &&
	    !(keyval == GDK_KEY_Left && (state & GDK_ALT_MASK) != 0))
		return FALSE;
	if (strcmp(gtk_stack_get_visible_child_name(app->stack), "settings") != 0)
		gtk_stack_set_visible_child_name(app->stack, "settings");
	else
		hide_shade(app);
	return TRUE;
}

static void
show_dialog_page(struct app *app, GtkWidget *page)
{
	GtkWidget *old = gtk_stack_get_child_by_name(app->stack, "dialog");

	if (old != NULL) {
		gtk_stack_set_visible_child_name(app->stack, "home");
		gtk_stack_remove(app->stack, old);
	}
	gtk_stack_add_named(app->stack, page, "dialog");
	gtk_stack_set_visible_child_name(app->stack, "dialog");
}

static char *
active_theme_path(void)
{
	return ctlst_config_path("theme.css");
}

static void
load_theme(struct app *app)
{
	char *theme_path = active_theme_path();
	char *theme = NULL;
	char *combined;

	g_file_get_contents(theme_path, &theme, NULL, NULL);
	combined = g_strconcat(theme != NULL ? theme : "", component_css, NULL);
	gtk_css_provider_load_from_string(app->css, combined);
	g_free(combined);
	g_free(theme);
	g_free(theme_path);
}

static char *
current_ssid(void)
{
	char *argv[] = {"nmcli", "-g", "GENERAL.CONNECTION", "device", "show",
	    "wlan0", NULL};
	char *ssid = run_capture(argv);

	g_strstrip(ssid);
	if (ssid[0] == '\0' || strcmp(ssid, "--") == 0) {
		g_free(ssid);
		return g_strdup("Not connected");
	}
	return ssid;
}

static char *
network_summary(void)
{
	char *argv[] = {"nmcli", "-g", "IP4.ADDRESS,IP4.GATEWAY,IP4.DNS",
	    "device", "show", "wlan0", NULL};
	char *connectivity_argv[] = {"nmcli", "-t", "-f", "CONNECTIVITY",
	    "general", NULL};
	char *value = run_capture(argv);
	char *connectivity = run_capture(connectivity_argv);
	char **lines = g_strsplit(value, "\n", 0);
	char *state;
	char *result;

	g_strstrip(connectivity);
	state = strcmp(connectivity, "full") == 0 ? g_strdup("") :
	    (strcmp(connectivity, "portal") == 0 ? g_strdup("  Sign-in required") :
	    (strcmp(connectivity, "limited") == 0 ? g_strdup("  No internet") :
	    g_strdup("  Offline")));
	result = g_strdup_printf("%s  Gateway %s%s",
	    lines[0] != NULL && lines[0][0] != '\0' ? lines[0] : "No address",
	    lines[1] != NULL && lines[1][0] != '\0' ? lines[1] : "-", state);

	g_free(state);
	g_free(connectivity);
	g_strfreev(lines);
	g_free(value);
	return result;
}

static char *
key_value(const char *output, const char *key)
{
	char **lines = g_strsplit(output, "\n", 0);
	char *value = g_strdup("--");

	for (int index = 0; lines[index] != NULL; index++) {
		char *colon;
		char *name;

		colon = strchr(lines[index], ':');
		if (colon == NULL)
			continue;
		name = g_strndup(lines[index], (gsize)(colon - lines[index]));
		g_strstrip(name);
		if (strcmp(name, key) == 0) {
			g_free(value);
			value = g_strdup(colon + 1);
			g_strstrip(value);
			g_free(name);
			break;
		}
		g_free(name);
	}
	g_strfreev(lines);
	return value;
}

static int
current_brightness(void)
{
	char *current_argv[] = {"brightnessctl", "get", NULL};
	char *maximum_argv[] = {"brightnessctl", "max", NULL};
	char *current_text = run_capture(current_argv);
	char *maximum_text = run_capture(maximum_argv);
	long current = strtol(current_text, NULL, 10);
	long maximum = strtol(maximum_text, NULL, 10);
	int percent = maximum > 0 ? (int)(current * 100 / maximum) : 65;

	g_free(current_text);
	g_free(maximum_text);
	if (percent < 5)
		return 5;
	if (percent > 100)
		return 100;
	return percent;
}

static char *
current_screen_idle_label(void)
{
	char *script = ctlst_script_path("screen-idle");
	char *argv[] = {script, "status", NULL};
	char *label = run_capture(argv);

	g_strstrip(label);
	if (label[0] == '\0') {
		g_free(label);
		label = g_strdup("Screen 5 min");
	}
	g_free(script);
	return label;
}

static bool
current_rotation_locked(void)
{
	char *path = g_build_filename(g_get_user_state_dir(), "sway-touch",
	    "rotation-lock", NULL);
	char *content = NULL;
	bool locked = false;

	if (g_file_get_contents(path, &content, NULL, NULL)) {
		g_strstrip(content);
		locked = strcmp(content, "locked") == 0;
	}
	g_free(content);
	g_free(path);
	return locked;
}

static void
update_rotation_button(struct app *app)
{
	bool available = script_available("auto-rotate");

	app->updating_rotation_switch = true;
	gtk_switch_set_active(app->rotation_switch, available && app->rotation_locked);
	app->updating_rotation_switch = false;
	gtk_widget_set_sensitive(GTK_WIDGET(app->rotation_switch),
	    available && !app->rotation_action_running);
	gtk_label_set_text(app->rotation_label,
	    !available ? "Unavailable" :
	    (app->rotation_locked ? "Locked" : "Auto-rotate"));
}

static gboolean
apply_status_snapshot(gpointer data)
{
	struct status_snapshot *snapshot = data;
	struct app *app = snapshot->app;

	app->wifi_enabled = snapshot->wifi_enabled;
	app->bluetooth_enabled = snapshot->bluetooth_enabled;
	app->tailscale_connected = snapshot->tailscale_connected;
	app->modem_enabled = snapshot->modem_enabled;
	if (!app->rotation_action_running &&
	    snapshot->rotation_generation == app->rotation_generation)
		app->rotation_locked = snapshot->rotation_locked;
	gtk_label_set_text(app->network_name, snapshot->ssid);
	gtk_label_set_text(app->network_detail, snapshot->network);
	gtk_label_set_text(app->cell_name, snapshot->cell_heading);
	gtk_label_set_text(app->cell_detail, snapshot->cell_summary);
	gtk_label_set_text(app->wifi_status,
	    app->wifi_enabled ? "Wi-Fi is on" : "Wi-Fi is off");
	/* A status read started before a drag must not rewind its live value. */
	if (snapshot->brightness_valid &&
	    snapshot->brightness_generation == app->brightness_generation &&
	    !app->brightness_write_running && app->brightness_write_source == 0) {
		app->updating_brightness = true;
		gtk_range_set_value(GTK_RANGE(app->brightness), snapshot->brightness);
		app->updating_brightness = false;
	}
	gtk_label_set_text(app->screen_idle_label, snapshot->screen_idle);
	if (strcmp(snapshot->screen_idle, "Screen timeout off") == 0)
		gtk_widget_remove_css_class(
		    GTK_WIDGET(app->screen_idle_button), "active");
	else
		gtk_widget_add_css_class(
		    GTK_WIDGET(app->screen_idle_button), "active");
	if (app->wifi_enabled)
		gtk_widget_add_css_class(GTK_WIDGET(app->wifi_button), "active");
	else
		gtk_widget_remove_css_class(GTK_WIDGET(app->wifi_button), "active");
	if (app->bluetooth_enabled)
		gtk_widget_add_css_class(GTK_WIDGET(app->bluetooth_button), "active");
	else
		gtk_widget_remove_css_class(GTK_WIDGET(app->bluetooth_button),
		    "active");
	gtk_label_set_text(app->tailscale_label, snapshot->tailscale_label);
	if (app->tailscale_connected)
		gtk_widget_add_css_class(GTK_WIDGET(app->tailscale_button), "active");
	else
		gtk_widget_remove_css_class(GTK_WIDGET(app->tailscale_button),
		    "active");
	gtk_label_set_text(app->airplane_label,
	    app->modem_enabled ? "Airplane" : "Airplane on");
	if (app->modem_enabled)
		gtk_widget_remove_css_class(GTK_WIDGET(app->airplane_button),
		    "active");
	else
		gtk_widget_add_css_class(GTK_WIDGET(app->airplane_button), "active");
	if (!app->rotation_action_running &&
	    snapshot->rotation_generation == app->rotation_generation)
		update_rotation_button(app);
	app->status_refresh_running = false;
	g_free(snapshot->ssid);
	g_free(snapshot->network);
	g_free(snapshot->cell_heading);
	g_free(snapshot->cell_summary);
	g_free(snapshot->screen_idle);
	g_free(snapshot->tailscale_label);
	g_free(snapshot);
	return G_SOURCE_REMOVE;
}

static gpointer
load_status_snapshot(gpointer data)
{
	struct status_request *request = data;
	struct app *app = request->app;
	struct status_snapshot *snapshot = g_new0(struct status_snapshot, 1);
	char *wifi_argv[] = {"nmcli", "-t", "-f", "WIFI", "radio", NULL};
	char *bt_argv[] = {"bluetoothctl", "show", NULL};
	char *wifi = run_capture(wifi_argv);
	char *bluetooth = run_capture(bt_argv);
	char *tailscale_script = ctlst_script_path("tailscale-control");
	char *tailscale_argv[] = {tailscale_script, "status", NULL};
	char *tailscale = run_capture(tailscale_argv);
	char *modem_argv[] = {"mmcli", "-m", "0", "-K", NULL};
	char *modem = run_capture(modem_argv);
	char *operator = key_value(modem, "modem.3gpp.operator-name");
	char *registration = key_value(modem,
	    "modem.3gpp.registration-state");
	char *technology = key_value(modem,
	    "modem.generic.access-technologies.value[1]");
	char *signal = key_value(modem, "modem.generic.signal-quality.value");
	char *modem_state = key_value(modem, "modem.generic.state");

	snapshot->app = app;
	snapshot->rotation_generation = request->rotation_generation;
	snapshot->brightness_generation = request->brightness_generation;
	snapshot->brightness_valid = request->brightness_valid;
	g_free(request);
	snapshot->ssid = current_ssid();
	snapshot->network = network_summary();
	snapshot->cell_heading = g_strdup_printf("%s  %s",
	    strcmp(operator, "--") == 0 ? "Cellular" : operator,
	    strcmp(technology, "--") == 0 ? "" : technology);
	snapshot->cell_summary = g_strdup_printf("%s  /  signal %s%%",
	    strcmp(registration, "--") == 0 ? "not registered" : registration,
	    strcmp(signal, "--") == 0 ? "0" : signal);
	g_strstrip(wifi);
	snapshot->wifi_enabled = strcmp(wifi, "enabled") == 0;
	snapshot->bluetooth_enabled = strstr(bluetooth, "Powered: yes") != NULL;
	g_strstrip(tailscale);
	if (g_str_has_prefix(tailscale, "connected\t")) {
		const char *detail = tailscale + strlen("connected\t");

		snapshot->tailscale_connected = true;
		snapshot->tailscale_label = g_strdup_printf("Tailscale\n%s",
		    detail[0] == '\0' ? "Connected" : detail);
	} else if (strcmp(tailscale, "needs-login") == 0 ||
	    g_str_has_prefix(tailscale, "needs-login\t")) {
		snapshot->tailscale_label = g_strdup("Tailscale\nSign in");
	} else if (strcmp(tailscale, "stopped") == 0 ||
	    g_str_has_prefix(tailscale, "stopped\t")) {
		snapshot->tailscale_label = g_strdup("Tailscale\nOff");
	} else {
		snapshot->tailscale_label = g_strdup("Tailscale\nUnavailable");
	}
	snapshot->rotation_locked = current_rotation_locked();
	snapshot->modem_enabled = modem_state != NULL &&
	    strcmp(modem_state, "disabled") != 0;
	snapshot->brightness = current_brightness();
	snapshot->screen_idle = current_screen_idle_label();
	g_free(wifi);
	g_free(bluetooth);
	g_free(tailscale);
	g_free(tailscale_script);
	g_free(modem);
	g_free(operator);
	g_free(registration);
	g_free(technology);
	g_free(signal);
	g_free(modem_state);
	g_idle_add(apply_status_snapshot, snapshot);
	return NULL;
}

static gboolean
refresh_status(gpointer data)
{
	struct app *app = data;
	struct status_request *request;
	GThread *thread;

	if (app->status_refresh_running)
		return G_SOURCE_REMOVE;
	app->status_refresh_running = true;
	request = g_new0(struct status_request, 1);
	request->app = app;
	request->rotation_generation = app->rotation_generation;
	request->brightness_generation = app->brightness_generation;
	request->brightness_valid = !app->brightness_write_running &&
	    app->brightness_write_source == 0;
	thread = g_thread_new("shade-status", load_status_snapshot, request);
	g_thread_unref(thread);
	return G_SOURCE_REMOVE;
}

static void
clear_box(GtkBox *box)
{
	GtkWidget *child;

	while ((child = gtk_widget_get_first_child(GTK_WIDGET(box))) != NULL)
		gtk_box_remove(box, child);
}

static char *
decode_field(const char *encoded)
{
	gsize size = 0;
	guchar *decoded = g_base64_decode(encoded != NULL ? encoded : "", &size);
	char *text = g_strndup((const char *)decoded, size);

	g_free(decoded);
	return text;
}

static void
open_notification(GtkButton *button, gpointer data)
{
	struct app *app = data;
	char id[24];
	char *script = ctlst_script_path("notification-history");
	char *argv[] = {script, "open", id, NULL};

	snprintf(id, sizeof(id), "%d", GPOINTER_TO_INT(g_object_get_data(
	    G_OBJECT(button), "notification-id")));
	run_detached(argv);
	hide_shade(app);
	g_free(script);
}

static void
notification_drag_begin(GtkGestureDrag *gesture, double x, double y,
    gpointer data)
{
	GtkWidget *row = g_object_get_data(G_OBJECT(gesture),
	    "notification-row");

	(void)x;
	(void)y;
	(void)data;
	gtk_widget_add_css_class(row, "notification-row-dragging");
}

static void
reset_notification_drag(GtkGesture *gesture)
{
	GtkWidget *row = g_object_get_data(G_OBJECT(gesture),
	    "notification-row");

	gtk_widget_remove_css_class(row, "notification-row-dragging");
	gtk_widget_set_margin_start(row, 0);
	gtk_widget_set_margin_end(row, 0);
	gtk_widget_set_opacity(row, 1.0);
}

static void
notification_drag_cancel(GtkGesture *gesture, GdkEventSequence *sequence,
    gpointer data)
{
	(void)sequence;
	(void)data;
	reset_notification_drag(gesture);
}

static void
notification_drag_update(GtkGestureDrag *gesture, double x, double y,
    gpointer data)
{
	GtkWidget *row = g_object_get_data(G_OBJECT(gesture),
	    "notification-row");
	double distance = fabs(x);

	(void)y;
	(void)data;
	if (distance > 12)
		gtk_gesture_set_state(GTK_GESTURE(gesture),
		    GTK_EVENT_SEQUENCE_CLAIMED);
	gtk_widget_set_margin_start(row, x > 0 ? (int)MIN(x, 96) : 0);
	gtk_widget_set_margin_end(row, x < 0 ? (int)MIN(-x, 96) : 0);
	gtk_widget_set_opacity(row, MAX(0.25, 1.0 - distance / 140.0));
}

static void
notification_drag_end(GtkGestureDrag *gesture, double x, double y,
    gpointer data)
{
	struct app *app = data;
	GtkWidget *row = g_object_get_data(G_OBJECT(gesture),
	    "notification-row");
	int id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row),
	    "notification-id"));

	(void)y;
	reset_notification_drag(GTK_GESTURE(gesture));
	if (fabs(x) >= 72) {
		char id_text[24];
		char *script = ctlst_script_path("notification-history");
		char *argv[] = {script, "dismiss", id_text, NULL};

		snprintf(id_text, sizeof(id_text), "%d", id);
		gtk_widget_set_visible(row, FALSE);
		run_detached(argv);
		g_timeout_add(250, refresh_notifications_later, app);
		g_free(script);
		return;
	}
}

static void
render_notifications(struct app *app, const char *output)
{
	char **lines = g_strsplit(output, "\n", 0);
	int rows = 0;

	clear_box(app->notification_list);
	for (int index = 0; lines[index] != NULL && rows < 20; index++) {
		char **fields;
		int id;
		char *app_name;
		char *summary;
		char *body;
		GtkWidget *button;
		GtkWidget *box;
		GtkWidget *app_label;
		GtkWidget *summary_label;
		GtkWidget *body_label;
		GtkGesture *drag;

		if (lines[index][0] == '\0')
			continue;
		fields = g_strsplit(lines[index], "\t", 4);
		if (g_strv_length(fields) != 4) {
			g_strfreev(fields);
			continue;
		}
		id = (int)strtol(fields[0], NULL, 10);
		app_name = decode_field(fields[1]);
		summary = decode_field(fields[2]);
		body = decode_field(fields[3]);
		button = gtk_button_new();
		box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
		app_label = gtk_label_new(app_name[0] != '\0' ?
		    app_name : "Notification");
		summary_label = gtk_label_new(summary);
		body_label = gtk_label_new(body);
		gtk_widget_add_css_class(button, "notification-row");
		gtk_widget_add_css_class(app_label, "notification-app");
		gtk_widget_add_css_class(summary_label, "notification-summary");
		gtk_widget_add_css_class(body_label, "notification-body");
		gtk_label_set_xalign(GTK_LABEL(app_label), 0);
		gtk_label_set_xalign(GTK_LABEL(summary_label), 0);
		gtk_label_set_xalign(GTK_LABEL(body_label), 0);
		gtk_label_set_ellipsize(GTK_LABEL(summary_label),
		    PANGO_ELLIPSIZE_END);
		gtk_label_set_ellipsize(GTK_LABEL(body_label),
		    PANGO_ELLIPSIZE_END);
		gtk_label_set_max_width_chars(GTK_LABEL(body_label), 54);
		gtk_box_append(GTK_BOX(box), app_label);
		gtk_box_append(GTK_BOX(box), summary_label);
		if (body[0] != '\0')
			gtk_box_append(GTK_BOX(box), body_label);
		gtk_button_set_child(GTK_BUTTON(button), box);
		g_object_set_data(G_OBJECT(button), "notification-id",
		    GINT_TO_POINTER(id));
		g_signal_connect(button, "clicked",
		    G_CALLBACK(open_notification), app);
		drag = gtk_gesture_drag_new();
		gtk_gesture_single_set_exclusive(GTK_GESTURE_SINGLE(drag), TRUE);
		gtk_event_controller_set_propagation_phase(
		    GTK_EVENT_CONTROLLER(drag), GTK_PHASE_CAPTURE);
		g_object_set_data(G_OBJECT(drag), "notification-row", button);
		g_signal_connect(drag, "drag-begin",
		    G_CALLBACK(notification_drag_begin), app);
		g_signal_connect(drag, "drag-update",
		    G_CALLBACK(notification_drag_update), app);
		g_signal_connect(drag, "drag-end",
		    G_CALLBACK(notification_drag_end), app);
		g_signal_connect(drag, "cancel",
		    G_CALLBACK(notification_drag_cancel), app);
		gtk_widget_add_controller(button, GTK_EVENT_CONTROLLER(drag));
		gtk_box_append(app->notification_list, button);
		g_free(app_name);
		g_free(summary);
		g_free(body);
		g_strfreev(fields);
		rows++;
	}
	if (rows == 0) {
		GtkWidget *empty = gtk_label_new(
		    "No notifications\nNew alerts will appear here.");

		gtk_widget_add_css_class(empty, "shade-detail");
		gtk_widget_set_margin_top(empty, 60);
		gtk_label_set_justify(GTK_LABEL(empty), GTK_JUSTIFY_CENTER);
		gtk_box_append(app->notification_list, empty);
	}
	gtk_widget_set_visible(app->notification_clear, rows > 0);
	g_strfreev(lines);
}

struct notification_snapshot {
	struct app *app;
	char *output;
};

static gboolean
apply_notification_snapshot(gpointer data)
{
	struct notification_snapshot *snapshot = data;
	struct app *app = snapshot->app;
	bool refresh_again = app->notification_refresh_pending;

	app->notification_refresh_running = false;
	app->notification_refresh_pending = false;
	render_notifications(app, snapshot->output);
	g_free(snapshot->output);
	g_free(snapshot);
	if (refresh_again)
		refresh_notifications(app);
	return G_SOURCE_REMOVE;
}

static gpointer
load_notification_snapshot(gpointer data)
{
	struct notification_snapshot *snapshot = data;
	char *script = ctlst_script_path("notification-history");
	char *argv[] = {script, "cached-list", NULL};

	snapshot->output = run_capture(argv);
	g_free(script);
	g_idle_add(apply_notification_snapshot, snapshot);
	return NULL;
}

static gboolean
refresh_notifications(gpointer data)
{
	struct app *app = data;
	struct notification_snapshot *snapshot;
	GThread *thread;

	if (app->notification_refresh_running) {
		app->notification_refresh_pending = true;
		return G_SOURCE_REMOVE;
	}
	app->notification_refresh_running = true;
	snapshot = g_new0(struct notification_snapshot, 1);
	snapshot->app = app;
	thread = g_thread_new("shade-notifications",
	    load_notification_snapshot, snapshot);
	g_thread_unref(thread);
	return G_SOURCE_REMOVE;
}

static gboolean
refresh_notifications_later(gpointer data)
{
	return refresh_notifications(data);
}

static char **
split_nmcli_fields(const char *line)
{
	char **fields = g_new0(char *, 8);
	GString *field = g_string_new(NULL);
	int index = 0;
	bool escaped = false;

	for (const char *cursor = line; *cursor != '\0'; cursor++) {
		if (escaped) {
			g_string_append_c(field, *cursor);
			escaped = false;
		} else if (*cursor == '\\') {
			escaped = true;
		} else if (*cursor == ':' && index < 6) {
			fields[index++] = g_string_free(field, FALSE);
			field = g_string_new(NULL);
		} else {
			g_string_append_c(field, *cursor);
		}
	}
	if (escaped)
		g_string_append_c(field, '\\');
	fields[index] = g_string_free(field, FALSE);
	return fields;
}

static GHashTable *
bluetooth_device_set(const char *output)
{
	GHashTable *devices = g_hash_table_new_full(g_str_hash, g_str_equal,
	    g_free, NULL);
	char **lines = g_strsplit(output, "\n", 0);

	for (int index = 0; lines[index] != NULL; index++) {
		char **parts = g_strsplit(lines[index], " ", 3);

		if (parts[0] != NULL && parts[1] != NULL &&
		    strcmp(parts[0], "Device") == 0)
			g_hash_table_add(devices, g_strdup(parts[1]));
		g_strfreev(parts);
	}
	g_strfreev(lines);
	return devices;
}

static void refresh_bluetooth_devices(struct app *app);
static GtkWidget *make_button(const char *label, const char *style,
    GCallback callback, struct app *app);

static gboolean
refresh_bluetooth_later(gpointer data)
{
	struct app *app = data;
	char *path = g_build_filename(app->runtime_dir,
	    "ctlst-bluetooth-action.state", NULL);
	char *content = NULL;

	refresh_status(app);
	refresh_bluetooth_devices(app);
	if (g_file_get_contents(path, &content, NULL, NULL)) {
		char **fields = g_strsplit(content, "\t", 5);
		const char *result = fields[0] != NULL ? fields[0] : "failed";
		const char *action = fields[1] != NULL ? fields[1] : "update";
		const char *name = fields[3] != NULL ? fields[3] : "device";
		const char *message = fields[4] != NULL ? fields[4] : "Unknown error";
		char *status;

		g_strstrip((char *)message);
		if (strcmp(result, "success") == 0) {
			const char *verb = strcmp(action, "pair") == 0 ? "Paired" :
			    (strcmp(action, "connect") == 0 ? "Connected" :
			    (strcmp(action, "disconnect") == 0 ? "Disconnected" :
			    "Forgot"));
			status = g_strdup_printf("%s %s", verb, name);
		} else {
			status = g_strdup_printf("Could not %s %s: %s",
			    action, name, message);
		}
		gtk_label_set_text(app->bluetooth_status, status);
		g_free(status);
		g_strfreev(fields);
		g_unlink(path);
	}
	g_free(content);
	g_free(path);
	return G_SOURCE_REMOVE;
}

static void
bluetooth_action_cancel(GtkButton *button, gpointer data)
{
	struct app *app = data;

	(void)button;
	gtk_stack_set_visible_child_name(app->stack, "bluetooth");
}

static void
bluetooth_action_run(GtkButton *button, gpointer data)
{
	struct app *app = data;
	const char *address = g_object_get_data(G_OBJECT(button), "address");
	const char *name = g_object_get_data(G_OBJECT(button), "device-name");
	const char *action = g_object_get_data(G_OBJECT(button), "action");
	char *script = ctlst_script_path("bluetooth-device-action");
	char *argv[] = {script, (char *)action, (char *)address, (char *)name,
	    NULL};
	char *status;

	status = g_strdup_printf("%s %s...", action, name);
	gtk_label_set_text(app->bluetooth_status, status);
	run_detached(argv);
	gtk_stack_set_visible_child_name(app->stack, "bluetooth");
	g_timeout_add(strcmp(action, "pair") == 0 ? 5500 : 1800,
	    refresh_bluetooth_later, app);
	g_free(status);
	g_free(script);
}

static void
show_bluetooth_actions(struct app *app, const char *address,
    const char *name, bool connected)
{
	char *info_argv[] = {"bluetoothctl", "info", (char *)address, NULL};
	char *info = run_capture(info_argv);
	char *battery = key_value(info, "Battery Percentage");
	char *icon = key_value(info, "Icon");
	char **info_lines = g_strsplit(info, "\n", 0);
	GString *services = g_string_new(NULL);
	GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	GtkWidget *eyebrow = gtk_label_new("Bluetooth device");
	GtkWidget *title = gtk_label_new(name);
	GtkWidget *detail;
	GtkWidget *primary = make_button(connected ? "Disconnect" : "Connect",
	    "shade-action", G_CALLBACK(bluetooth_action_run), app);
	GtkWidget *forget = make_button("Forget device", "shade-action",
	    G_CALLBACK(bluetooth_action_run), app);
	GtkWidget *cancel = make_button("Cancel", "shade-action",
	    G_CALLBACK(bluetooth_action_cancel), app);
	char *detail_text;

	for (int index = 0; info_lines[index] != NULL; index++) {
		char *uuid = strstr(info_lines[index], "UUID:");
		char *end;

		if (uuid == NULL || services->len > 80)
			continue;
		uuid += 5;
		g_strstrip(uuid);
		end = strstr(uuid, " (0x");
		if (end != NULL)
			*end = '\0';
		if (uuid[0] == '\0')
			continue;
		if (services->len > 0)
			g_string_append(services, ", ");
		g_string_append(services, uuid);
	}
	detail_text = g_strdup_printf("%s\nBattery %s  /  Type %s\n%s",
	    connected ? "Connected and ready to use" : "Paired with this phone",
	    strcmp(battery, "--") == 0 ? "not reported" : battery,
	    strcmp(icon, "--") == 0 ? "unknown" : icon,
	    services->len > 0 ? services->str : "No profile details reported");
	detail = gtk_label_new(detail_text);

	gtk_widget_add_css_class(root, "password-window");
	gtk_widget_add_css_class(eyebrow, "shade-section");
	gtk_widget_add_css_class(title, "shade-title");
	gtk_widget_add_css_class(detail, "shade-detail");
	gtk_label_set_xalign(GTK_LABEL(eyebrow), 0);
	gtk_label_set_xalign(GTK_LABEL(title), 0);
	gtk_label_set_xalign(GTK_LABEL(detail), 0);
	gtk_label_set_wrap(GTK_LABEL(detail), TRUE);
	g_object_set_data(G_OBJECT(primary), "action",
	    connected ? "disconnect" : "connect");
	g_object_set_data(G_OBJECT(forget), "action", "forget");
	g_object_set_data_full(G_OBJECT(primary), "address",
	    g_strdup(address), g_free);
	g_object_set_data_full(G_OBJECT(primary), "device-name",
	    g_strdup(name), g_free);
	g_object_set_data_full(G_OBJECT(forget), "address",
	    g_strdup(address), g_free);
	g_object_set_data_full(G_OBJECT(forget), "device-name",
	    g_strdup(name), g_free);
	gtk_box_append(GTK_BOX(root), eyebrow);
	gtk_box_append(GTK_BOX(root), title);
	gtk_box_append(GTK_BOX(root), detail);
	gtk_box_append(GTK_BOX(root), primary);
	gtk_box_append(GTK_BOX(root), forget);
	gtk_box_append(GTK_BOX(root), cancel);
	show_dialog_page(app, root);
	g_free(detail_text);
	g_string_free(services, TRUE);
	g_strfreev(info_lines);
	g_free(battery);
	g_free(icon);
	g_free(info);
}

static void
show_bluetooth_pair_confirmation(struct app *app, const char *address,
    const char *name)
{
	GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	GtkWidget *eyebrow = gtk_label_new("New Bluetooth device");
	GtkWidget *title = gtk_label_new(name);
	GtkWidget *detail = gtk_label_new(
	    "Pairing allows this device to reconnect to the phone.");
	GtkWidget *pair = make_button("Pair and connect", "shade-action",
	    G_CALLBACK(bluetooth_action_run), app);
	GtkWidget *cancel = make_button("Cancel", "shade-action",
	    G_CALLBACK(bluetooth_action_cancel), app);

	gtk_widget_add_css_class(root, "password-window");
	gtk_widget_add_css_class(eyebrow, "shade-section");
	gtk_widget_add_css_class(title, "shade-title");
	gtk_widget_add_css_class(detail, "shade-detail");
	gtk_label_set_xalign(GTK_LABEL(eyebrow), 0);
	gtk_label_set_xalign(GTK_LABEL(title), 0);
	gtk_label_set_xalign(GTK_LABEL(detail), 0);
	gtk_label_set_wrap(GTK_LABEL(detail), TRUE);
	g_object_set_data(G_OBJECT(pair), "action", "pair");
	g_object_set_data_full(G_OBJECT(pair), "address", g_strdup(address),
	    g_free);
	g_object_set_data_full(G_OBJECT(pair), "device-name", g_strdup(name),
	    g_free);
	gtk_box_append(GTK_BOX(root), eyebrow);
	gtk_box_append(GTK_BOX(root), title);
	gtk_box_append(GTK_BOX(root), detail);
	gtk_box_append(GTK_BOX(root), pair);
	gtk_box_append(GTK_BOX(root), cancel);
	show_dialog_page(app, root);
}

static void
bluetooth_device_clicked(GtkButton *button, gpointer data)
{
	struct app *app = data;
	const char *address = g_object_get_data(G_OBJECT(button), "address");
	const char *name = g_object_get_data(G_OBJECT(button), "device-name");
	bool paired = GPOINTER_TO_INT(g_object_get_data(
	    G_OBJECT(button), "paired"));
	bool connected = GPOINTER_TO_INT(g_object_get_data(
	    G_OBJECT(button), "connected"));

	if (paired) {
		show_bluetooth_actions(app, address, name, connected);
		return;
	}
	show_bluetooth_pair_confirmation(app, address, name);
}

static void
refresh_bluetooth_devices(struct app *app)
{
	char *all_argv[] = {"bluetoothctl", "devices", NULL};
	char *paired_argv[] = {"bluetoothctl", "devices", "Paired", NULL};
	char *connected_argv[] = {"bluetoothctl", "devices", "Connected", NULL};
	char *all = run_capture(all_argv);
	char *paired_text = run_capture(paired_argv);
	char *connected_text = run_capture(connected_argv);
	GHashTable *paired = bluetooth_device_set(paired_text);
	GHashTable *connected = bluetooth_device_set(connected_text);
	char **lines = g_strsplit(all, "\n", 0);
	int rows = 0;

	clear_box(app->bluetooth_list);
	for (int index = 0; lines[index] != NULL && rows < 16; index++) {
		char **parts = g_strsplit(lines[index], " ", 3);
		bool is_paired;
		bool is_connected;
		const char *state;
		GtkWidget *button;
		GtkWidget *label_widget;
		char *label;

		if (parts[0] == NULL || parts[1] == NULL || parts[2] == NULL ||
		    strcmp(parts[0], "Device") != 0) {
			g_strfreev(parts);
			continue;
		}
		is_paired = g_hash_table_contains(paired, parts[1]);
		is_connected = g_hash_table_contains(connected, parts[1]);
		state = is_connected ? "Connected - tap to disconnect" :
		    (is_paired ? "Paired - tap to connect" :
		    "Available - tap to pair");
		label = g_strdup_printf("%s\n%s", parts[2], state);
		button = gtk_button_new_with_label(label);
		gtk_widget_add_css_class(button, "device-row");
		if (is_connected)
			gtk_widget_add_css_class(button, "device-connected");
		label_widget = gtk_button_get_child(GTK_BUTTON(button));
		if (GTK_IS_LABEL(label_widget))
			gtk_label_set_xalign(GTK_LABEL(label_widget), 0);
		g_object_set_data_full(G_OBJECT(button), "address",
		    g_strdup(parts[1]), g_free);
		g_object_set_data_full(G_OBJECT(button), "device-name",
		    g_strdup(parts[2]), g_free);
		g_object_set_data(G_OBJECT(button), "paired",
		    GINT_TO_POINTER(is_paired));
		g_object_set_data(G_OBJECT(button), "connected",
		    GINT_TO_POINTER(is_connected));
		g_signal_connect(button, "clicked",
		    G_CALLBACK(bluetooth_device_clicked), app);
		gtk_box_append(app->bluetooth_list, button);
		g_free(label);
		g_strfreev(parts);
		rows++;
	}
	if (rows == 0) {
		GtkWidget *empty = gtk_label_new(app->bluetooth_enabled ?
		    "No Bluetooth devices found.\nTap SCAN to look nearby." :
		    "Bluetooth is off.");

		gtk_widget_add_css_class(empty, "shade-detail");
		gtk_widget_set_margin_top(empty, 60);
		gtk_label_set_justify(GTK_LABEL(empty), GTK_JUSTIFY_CENTER);
		gtk_box_append(app->bluetooth_list, empty);
	}
	gtk_label_set_text(app->bluetooth_status,
	    app->bluetooth_enabled ? "Bluetooth is on" : "Bluetooth is off");
	g_strfreev(lines);
	g_hash_table_unref(paired);
	g_hash_table_unref(connected);
	g_free(all);
	g_free(paired_text);
	g_free(connected_text);
}

static void
show_first_bluetooth_device(struct app *app)
{
	char *paired_argv[] = {"bluetoothctl", "devices", "Paired", NULL};
	char *connected_argv[] = {"bluetoothctl", "devices", "Connected", NULL};
	char *paired_text = run_capture(paired_argv);
	char *connected_text = run_capture(connected_argv);
	GHashTable *connected = bluetooth_device_set(connected_text);
	char **lines = g_strsplit(paired_text, "\n", 0);

	for (int index = 0; lines[index] != NULL; index++) {
		char **parts = g_strsplit(lines[index], " ", 3);

		if (parts[0] != NULL && parts[1] != NULL && parts[2] != NULL &&
		    strcmp(parts[0], "Device") == 0) {
			show_bluetooth_actions(app, parts[1], parts[2],
			    g_hash_table_contains(connected, parts[1]));
			g_strfreev(parts);
			break;
		}
		g_strfreev(parts);
	}
	g_strfreev(lines);
	g_hash_table_unref(connected);
	g_free(paired_text);
	g_free(connected_text);
}

static void connect_network(GtkButton *button, gpointer data);
static GtkWidget *make_button(const char *label, const char *style,
    GCallback callback, struct app *app);

static void
refresh_networks(struct app *app)
{
	char *argv[] = {"nmcli", "-t", "--escape", "yes", "-f",
	    "IN-USE,SSID,SIGNAL,SECURITY,FREQ,CHAN,RATE", "device", "wifi", "list",
	    "--rescan", "no", NULL};
	char *output = run_capture(argv);
	char **lines = g_strsplit(output, "\n", 0);
	GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal,
	    g_free, NULL);
	int shown = 0;

	clear_box(app->network_list);
	for (int index = 0; lines[index] != NULL && shown < 12; index++) {
		char **fields;
		GtkWidget *button;
		char *label;

		if (lines[index][0] == '\0')
			continue;
		fields = split_nmcli_fields(lines[index]);
		if (fields[1] == NULL || fields[1][0] == '\0') {
			g_strfreev(fields);
			continue;
		}
		GtkWidget *existing = g_hash_table_lookup(seen, fields[1]);
		g_strstrip(fields[0]);
		if (existing != NULL) {
			if (strcmp(fields[0], "*") == 0)
				gtk_widget_add_css_class(existing, "wifi-current");
			g_strfreev(fields);
			continue;
		}
		label = g_strdup_printf("%s\n%s%%  %s  /  ch %s", fields[1],
		    fields[2] != NULL ? fields[2] : "0",
		    fields[3] != NULL && fields[3][0] != '\0' ?
		    fields[3] : "Open",
		    fields[5] != NULL && fields[5][0] != '\0' ? fields[5] : "-");
		button = gtk_button_new_with_label(label);
		gtk_widget_add_css_class(button, "wifi-row");
		gtk_widget_set_size_request(button, -1, 58);
		gtk_widget_set_vexpand(button, FALSE);
		GtkWidget *button_label = gtk_button_get_child(GTK_BUTTON(button));
		if (GTK_IS_LABEL(button_label))
			gtk_label_set_xalign(GTK_LABEL(button_label), 0);
		if (strcmp(fields[0], "*") == 0)
			gtk_widget_add_css_class(button, "wifi-current");
		g_hash_table_insert(seen, g_strdup(fields[1]), button);
		g_object_set_data_full(G_OBJECT(button), "ssid",
		    g_strdup(fields[1]), g_free);
		g_object_set_data(G_OBJECT(button), "secured",
		    GINT_TO_POINTER(fields[3] != NULL && fields[3][0] != '\0'));
		g_object_set_data(G_OBJECT(button), "current",
		    GINT_TO_POINTER(strcmp(fields[0], "*") == 0));
		g_signal_connect(button, "clicked", G_CALLBACK(connect_network), app);
		gtk_box_append(app->network_list, button);
		g_free(label);
		g_strfreev(fields);
		shown++;
	}
	g_hash_table_unref(seen);
	g_strfreev(lines);
	g_free(output);
}

static gboolean
refresh_after_action(gpointer data)
{
	struct app *app = data;

	refresh_status(app);
	refresh_networks(app);
	return G_SOURCE_REMOVE;
}

static void
show_quick_settings(struct app *app)
{
	refresh_status(app);
	gtk_stack_set_visible_child_name(app->stack, "settings");
}

static void
show_home(struct app *app)
{
	show_quick_settings(app);
}

static void
show_wifi(struct app *app)
{
	refresh_status(app);
	refresh_networks(app);
	gtk_stack_set_visible_child_name(app->stack, "wifi");
}

static void
show_notifications(struct app *app)
{
	show_quick_settings(app);
}

static void
show_bluetooth(struct app *app)
{
	refresh_status(app);
	refresh_bluetooth_devices(app);
	gtk_stack_set_visible_child_name(app->stack, "bluetooth");
}

static void
show_sound(struct app *app)
{
	gtk_stack_set_visible_child_name(app->stack, "sound");
}

static void
present_shade_page(struct app *app)
{
	stop_shade_settle(app);
	app->pull_preview = false;
	gtk_layer_set_keyboard_mode(app->window, GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE);
	gtk_window_present(app->window);
	set_visible_state(app, true);
	/* Direct detail-page opens bypass the main Quick Settings presenter. Restore the
	 * retained GSK panel after hide_shade() translated it fully offscreen. */
	apply_shade_margin(app, 1.0);
}

static void
show_shade(struct app *app, bool wifi)
{
	bool settle = app->pull_preview && app->pull_progress < 0.999;

	if (wifi)
		gtk_stack_set_visible_child_name(app->stack, "wifi");
	else
		show_quick_settings(app);
	app->pull_preview = false;
	gtk_layer_set_keyboard_mode(app->window, GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE);
	gtk_window_present(app->window);
	set_visible_state(app, true);
	if (settle)
		start_shade_settle(app, 1.0);
	else
		apply_shade_margin(app, 1.0);
	if (wifi)
		g_timeout_add(100, refresh_after_action, app);
	else
		g_timeout_add(100, refresh_status, app);
}

static void
action_close(GtkButton *button, gpointer data)
{
	(void)button;
	hide_shade(data);
}

static void
action_home(GtkButton *button, gpointer data)
{
	(void)button;
	show_home(data);
}

static void
action_settings_page(GtkButton *button, gpointer data)
{
	struct app *app = data;

	(void)button;
	refresh_status(app);
	gtk_stack_set_visible_child_name(app->stack, "settings");
}

static void
action_wifi_toggle(GtkButton *button, gpointer data)
{
	struct app *app = data;
	char *argv[] = {"nmcli", "radio", "wifi",
	    app->wifi_enabled ? "off" : "on", NULL};

	(void)button;
	run_detached(argv);
	g_timeout_add(700, refresh_after_action, app);
}

static void
action_bluetooth_toggle(GtkButton *button, gpointer data)
{
	struct app *app = data;
	char *argv[] = {"bluetoothctl", "power",
	    app->bluetooth_enabled ? "off" : "on", NULL};

	(void)button;
	run_detached(argv);
	gtk_label_set_text(app->bluetooth_status,
	    app->bluetooth_enabled ? "Turning Bluetooth off..." :
	    "Turning Bluetooth on...");
	g_timeout_add(800, refresh_bluetooth_later, app);
}

static void
action_tailscale_toggle(GtkButton *button, gpointer data)
{
	struct app *app = data;
	char *script = ctlst_script_path("tailscale-control");
	char *argv[] = {script, "toggle", NULL};

	(void)button;
	gtk_label_set_text(app->tailscale_label,
	    app->tailscale_connected ? "Tailscale\nDisconnecting..." :
	    "Tailscale\nConnecting...");
	run_detached(argv);
	g_timeout_add(2500, refresh_after_action, app);
	g_timeout_add(9000, refresh_after_action, app);
	g_free(script);
}

static void
action_bluetooth_scan(GtkButton *button, gpointer data)
{
	struct app *app = data;
	char *argv[] = {"bluetoothctl", "--timeout", "4", "scan", "on", NULL};

	(void)button;
	if (!app->bluetooth_enabled) {
		gtk_label_set_text(app->bluetooth_status,
		    "Turn Bluetooth on before scanning");
		return;
	}
	gtk_label_set_text(app->bluetooth_status, "Scanning nearby devices...");
	run_detached(argv);
	g_timeout_add(4500, refresh_bluetooth_later, app);
}

static void
finish_sound_action(GObject *source, GAsyncResult *result, gpointer data)
{
	struct app *app = data;
	GError *error = NULL;
	bool success = g_subprocess_wait_check_finish(G_SUBPROCESS(source),
	    result, &error);

	app->sound_action_running = false;
	gtk_widget_set_sensitive(app->sound_profiles,
	    script_available("sound-profile"));
	gtk_label_set_text(app->sound_status,
	    success ? "Sound profile updated" : "Could not change sound profile");
	g_clear_error(&error);
	g_object_unref(source);
}

static void
set_sound_profile(struct app *app, const char *profile)
{
	char *script;
	GError *error = NULL;
	GSubprocess *process;

	if (app->sound_action_running)
		return;
	script = ctlst_script_path("sound-profile");
	if (!script_available("sound-profile")) {
		gtk_label_set_text(app->sound_status, "Sound profiles unavailable");
		g_free(script);
		return;
	}
	process = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_SILENCE |
	    G_SUBPROCESS_FLAGS_STDERR_SILENCE, &error, script, profile, NULL);
	if (process != NULL) {
		app->sound_action_running = true;
		gtk_widget_set_sensitive(app->sound_profiles, FALSE);
		gtk_label_set_text(app->sound_status, "Applying sound profile...");
		g_subprocess_wait_check_async(process, NULL, finish_sound_action, app);
	} else {
		gtk_widget_set_sensitive(app->sound_profiles,
		    script_available("sound-profile"));
		gtk_label_set_text(app->sound_status, "Could not change sound profile");
		g_clear_error(&error);
	}
	g_free(script);
}

static void
action_sound(GtkButton *button, gpointer data)
{
	struct app *app = data;

	(void)button;
	if (!script_available("sound-profile")) {
		show_sound(app);
		return;
	}
	set_sound_profile(app, "cycle");
}

static void
action_sound_profile(GtkButton *button, gpointer data)
{
	struct app *app = data;
	const char *profile = g_object_get_data(G_OBJECT(button), "profile");
	set_sound_profile(app, profile);
}

static void
action_airplane_mode(GtkButton *button, gpointer data)
{
	struct app *app = data;
	char *argv_disable[] = {"mmcli", "-m", "any", "--disable", NULL};
	char *argv_enable[] = {"mmcli", "-m", "any", "--enable", NULL};

	(void)button;
	if (app->modem_enabled)
		run_detached(argv_disable);
	else
		run_detached(argv_enable);
	g_timeout_add(1200, refresh_status, app);
}

static gboolean
finish_rotation_action(gpointer data)
{
	struct rotation_request *request = data;
	struct app *app = request->app;

	app->rotation_generation++;
	app->rotation_locked = current_rotation_locked();
	app->rotation_action_running = false;
	gtk_widget_set_sensitive(GTK_WIDGET(app->rotation_switch), TRUE);
	update_rotation_button(app);
	g_free(request);
	return G_SOURCE_REMOVE;
}

static gpointer
run_rotation_action(gpointer data)
{
	struct rotation_request *request = data;
	char *script = ctlst_script_path("auto-rotate");
	char *argv[] = {script, request->locked ? "lock" : "unlock", NULL};
	char *output = run_capture(argv);

	g_free(output);
	g_free(script);
	g_idle_add(finish_rotation_action, request);
	return NULL;
}

static void
action_rotate(GtkSwitch *toggle, GParamSpec *pspec, gpointer data)
{
	struct app *app = data;
	struct rotation_request *request;
	GThread *thread;

	(void)pspec;
	if (app->updating_rotation_switch || app->rotation_action_running)
		return;
	if (!script_available("auto-rotate")) {
		update_rotation_button(app);
		return;
	}
	request = g_new0(struct rotation_request, 1);
	request->app = app;
	request->locked = gtk_switch_get_active(toggle);
	if (request->locked == current_rotation_locked()) {
		g_free(request);
		return;
	}
	app->rotation_generation++;
	app->rotation_locked = request->locked;
	app->rotation_action_running = true;
	update_rotation_button(app);
	gtk_label_set_text(app->rotation_label, "Applying...");
	gtk_widget_set_sensitive(GTK_WIDGET(app->rotation_switch), FALSE);
	thread = g_thread_new("shade-rotation", run_rotation_action, request);
	g_thread_unref(thread);
}

static gboolean
capture_screenshot(gpointer data)
{
	char *script = ctlst_script_path("screenshot");
	char *argv[] = {script, NULL};

	(void)data;
	run_detached(argv);
	g_free(script);
	return G_SOURCE_REMOVE;
}

static void
action_screenshot(GtkButton *button, gpointer data)
{
	struct app *app = data;

	(void)button;
	hide_shade(app);
	g_timeout_add(250, capture_screenshot, NULL);
}

static void
action_scan(GtkButton *button, gpointer data)
{
	struct app *app = data;
	char *argv[] = {"nmcli", "device", "wifi", "rescan", NULL};

	(void)button;
	run_detached(argv);
	g_timeout_add(1000, refresh_after_action, app);
}

static void
action_launch(GtkButton *button, gpointer data)
{
	struct app *app = data;
	const char *desktop = g_object_get_data(G_OBJECT(button), "desktop");
	char *app_run = ctlst_script_path("app-run");
	char *desktop_id = g_strdup_printf("%s.desktop", desktop);
	char *argv[] = {app_run, "desktop", desktop_id, NULL};

	run_detached(argv);
	g_free(desktop_id);
	g_free(app_run);
	hide_shade(app);
}

static void
action_clear_notifications(GtkButton *button, gpointer data)
{
	struct app *app = data;
	char *script = ctlst_script_path("notification-history");
	char *argv[] = {script, "clear", NULL};

	(void)button;
	run_detached(argv);
	g_timeout_add(700, refresh_notifications_later, app);
	g_free(script);
}

static void
action_screen_idle(GtkButton *button, gpointer data)
{
	struct app *app = data;
	char *script = ctlst_script_path("screen-idle");
	char *argv[] = {script, "cycle", NULL};
	char *label = run_capture(argv);

	(void)button;
	g_strstrip(label);
	if (label[0] != '\0')
		gtk_label_set_text(app->screen_idle_label, label);
	if (strcmp(label, "Screen timeout off") == 0)
		gtk_widget_remove_css_class(
		    GTK_WIDGET(app->screen_idle_button), "active");
	else
		gtk_widget_add_css_class(
		    GTK_WIDGET(app->screen_idle_button), "active");
	g_free(label);
	g_free(script);
}

struct brightness_request {
	struct app *app;
	int value;
	guint64 generation;
};

static gboolean dispatch_brightness(gpointer data);

static gboolean
brightness_write_done(gpointer data)
{
	struct brightness_request *request = data;
	struct app *app = request->app;

	app->brightness_write_running = false;
	if (request->generation != app->brightness_generation)
		app->brightness_write_source =
		    g_timeout_add(33, dispatch_brightness, app);
	g_free(request);
	return G_SOURCE_REMOVE;
}

static gpointer
write_brightness(gpointer data)
{
	struct brightness_request *request = data;
	char value[16];
	char *argv[] = {"brightnessctl", "set", value, NULL};
	GError *error = NULL;
	int status = 0;

	snprintf(value, sizeof(value), "%d%%", request->value);
	/* Spawn/wait off the GTK thread; serialize writes so the newest wins. */
	if (!g_spawn_sync(NULL, argv, NULL,
	    G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL |
	    G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, NULL, &status,
	    &error) || !g_spawn_check_wait_status(status, &error)) {
		g_warning("Could not set brightness: %s",
		    error != NULL ? error->message : "helper failed");
		g_clear_error(&error);
	}
	g_idle_add(brightness_write_done, request);
	return NULL;
}

static gboolean
dispatch_brightness(gpointer data)
{
	struct app *app = data;
	struct brightness_request *request = g_new0(struct brightness_request, 1);
	GThread *thread;

	app->brightness_write_source = 0;
	app->brightness_write_running = true;
	request->app = app;
	request->value = app->brightness_target;
	request->generation = app->brightness_generation;
	thread = g_thread_new("shade-brightness", write_brightness, request);
	g_thread_unref(thread);
	return G_SOURCE_REMOVE;
}

static void
brightness_changed(GtkRange *range, gpointer data)
{
	struct app *app = data;

	if (app->updating_brightness)
		return;
	/* GTK moves the thumb immediately. Coalesce only the hardware work. */
	app->brightness_target = (int)(gtk_range_get_value(range) + 0.5);
	app->brightness_generation++;
	if (!app->brightness_write_running && app->brightness_write_source == 0)
		app->brightness_write_source =
		    g_timeout_add(33, dispatch_brightness, app);
}

struct connect_request {
	struct app *app;
	GtkEditable *entry;
	char *ssid;
};

struct connection_check {
	struct app *app;
	char *ssid;
};

static gboolean
verify_connection(gpointer data)
{
	struct connection_check *check = data;
	char *ssid = current_ssid();
	char *connectivity_argv[] = {"nmcli", "-t", "-f", "CONNECTIVITY",
	    "general", NULL};
	char *connectivity = run_capture(connectivity_argv);
	char *status;

	g_strstrip(connectivity);
	refresh_status(check->app);
	refresh_networks(check->app);
	if (strcmp(ssid, check->ssid) != 0) {
		status = g_strdup_printf("Could not connect to %s - tap to retry",
		    check->ssid);
	} else if (strcmp(connectivity, "full") == 0) {
		status = g_strdup_printf("Connected to %s", check->ssid);
	} else if (strcmp(connectivity, "portal") == 0) {
		status = g_strdup_printf("%s needs browser sign-in", check->ssid);
	} else {
		status = g_strdup_printf("Connected to %s - no internet",
		    check->ssid);
	}
	gtk_label_set_text(check->app->wifi_status, status);
	g_free(status);
	g_free(connectivity);
	g_free(ssid);
	g_free(check->ssid);
	g_free(check);
	return G_SOURCE_REMOVE;
}

static void
schedule_connection_check(struct app *app, const char *ssid)
{
	struct connection_check *check = g_new0(struct connection_check, 1);

	check->app = app;
	check->ssid = g_strdup(ssid);
	g_timeout_add(4000, verify_connection, check);
}

static void
wifi_action_cancel(GtkButton *button, gpointer data)
{
	struct app *app = data;

	(void)button;
	end_text_input(app);
	gtk_stack_set_visible_child_name(app->stack, "wifi");
}

static void
wifi_action_run(GtkButton *button, gpointer data)
{
	struct app *app = data;
	const char *action = g_object_get_data(G_OBJECT(button), "action");
	const char *ssid = g_object_get_data(G_OBJECT(button), "ssid");
	char *disconnect_argv[] = {"nmcli", "device", "disconnect", "wlan0",
	    NULL};
	char *forget_argv[] = {"nmcli", "connection", "delete", "id", NULL,
	    NULL};
	char *autoconnect_argv[] = {"nmcli", "connection", "modify", NULL,
	    "connection.autoconnect", NULL, NULL};

	if (strcmp(action, "disconnect") == 0) {
		gtk_label_set_text(app->wifi_status, "Disconnecting...");
		run_detached(disconnect_argv);
	} else if (strcmp(action, "forget") == 0) {
		forget_argv[4] = (char *)ssid;
		gtk_label_set_text(app->wifi_status, "Forgetting network...");
		run_detached(forget_argv);
	} else {
		const char *enabled = g_object_get_data(G_OBJECT(button),
		    "autoconnect-enabled");

		autoconnect_argv[3] = (char *)ssid;
		autoconnect_argv[5] = (char *)enabled;
		gtk_label_set_text(app->wifi_status,
		    strcmp(enabled, "yes") == 0 ?
		    "Auto-connect enabled" : "Auto-connect disabled");
		run_detached(autoconnect_argv);
	}
	gtk_stack_set_visible_child_name(app->stack, "wifi");
	g_timeout_add(1200, refresh_after_action, app);
}

static void
show_wifi_actions(struct app *app, const char *ssid)
{
	char *device_argv[] = {"nmcli", "--escape", "no", "-g",
	    "GENERAL.HWADDR,IP4.ADDRESS,IP4.GATEWAY,IP4.DNS",
	    "device", "show", "wlan0", NULL};
	char *radio_argv[] = {"nmcli", "-t", "--escape", "yes", "-f",
	    "IN-USE,SSID,SIGNAL,SECURITY,FREQ,CHAN,RATE", "device", "wifi", "list",
	    "--rescan", "no", NULL};
	char *profile_argv[] = {"nmcli", "-g", "connection.autoconnect",
	    "connection", "show", (char *)ssid, NULL};
	char *iw_argv[] = {"iw", "dev", "wlan0", "link", NULL};
	char *device_output = run_capture(device_argv);
	char *radio_output = run_capture(radio_argv);
	char *profile_output = run_capture(profile_argv);
	char *iw_output = run_capture(iw_argv);
	char **device = g_strsplit(device_output, "\n", 0);
	char **radio_lines = g_strsplit(radio_output, "\n", 0);
	char *signal = g_strdup("-");
	char *security = g_strdup("Unknown");
	char *frequency = g_strdup("-");
	char *channel = g_strdup("-");
	char *rate = g_strdup("-");
	char *dbm = g_strdup("-");
	GString *dns = g_string_new(NULL);
	GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	GtkWidget *eyebrow = gtk_label_new("Connected network");
	GtkWidget *title = gtk_label_new(ssid);
	GtkWidget *detail;
	GtkWidget *disconnect = make_button("Disconnect", "shade-action",
	    G_CALLBACK(wifi_action_run), app);
	GtkWidget *forget = make_button("Forget network", "shade-action",
	    G_CALLBACK(wifi_action_run), app);
	GtkWidget *autoconnect;
	GtkWidget *cancel = make_button("Cancel", "shade-action",
	    G_CALLBACK(wifi_action_cancel), app);
	char *detail_text;

	g_strstrip(profile_output);
	autoconnect = make_button(
	    strcmp(profile_output, "yes") == 0 ?
	    "Auto-connect on" : "Auto-connect off",
	    "shade-action", G_CALLBACK(wifi_action_run), app);

	for (int index = 0; radio_lines[index] != NULL; index++) {
		char **fields = split_nmcli_fields(radio_lines[index]);

		if (fields[0] != NULL && strcmp(fields[0], "*") == 0) {
			g_free(signal);
			g_free(security);
			signal = g_strdup(fields[2] != NULL ? fields[2] : "-");
			security = g_strdup(fields[3] != NULL &&
			    fields[3][0] != '\0' ? fields[3] : "Open");
			g_free(frequency);
			g_free(channel);
			g_free(rate);
			frequency = g_strdup(fields[4] != NULL ? fields[4] : "-");
			channel = g_strdup(fields[5] != NULL ? fields[5] : "-");
			rate = g_strdup(fields[6] != NULL ? fields[6] : "-");
			g_strfreev(fields);
			break;
		}
		g_strfreev(fields);
	}
	char **iw_lines = g_strsplit(iw_output, "\n", 0);
	for (int index = 0; iw_lines[index] != NULL; index++) {
		char *signal_at = strstr(iw_lines[index], "signal:");

		if (signal_at != NULL) {
			char **parts = g_strsplit_set(signal_at + 7, " \t", -1);

			for (int part = 0; parts[part] != NULL; part++) {
				if (parts[part][0] != '\0') {
					g_free(dbm);
					dbm = g_strdup(parts[part]);
					break;
				}
			}
			g_strfreev(parts);
			break;
		}
	}
	for (int index = 3; device[index] != NULL &&
	    device[index][0] != '\0'; index++) {
		if (dns->len > 0)
			g_string_append(dns, ", ");
		g_string_append(dns, device[index]);
	}
	detail_text = g_strdup_printf(
	    "Signal %s%% / %s dBm  /  %s\n%s  ch %s  /  %s\n"
	    "IP %s\nGateway %s\nDNS %s\nDevice %s",
	    signal, dbm, security, frequency, channel, rate,
	    device[1] != NULL && device[1][0] != '\0' ? device[1] : "-",
	    device[2] != NULL && device[2][0] != '\0' ? device[2] : "-",
	    dns->len > 0 ? dns->str : "-",
	    device[0] != NULL && device[0][0] != '\0' ? device[0] : "-");
	detail = gtk_label_new(detail_text);

	gtk_widget_add_css_class(root, "password-window");
	gtk_widget_add_css_class(eyebrow, "shade-section");
	gtk_widget_add_css_class(title, "shade-title");
	gtk_widget_add_css_class(detail, "shade-detail");
	gtk_label_set_xalign(GTK_LABEL(eyebrow), 0);
	gtk_label_set_xalign(GTK_LABEL(title), 0);
	gtk_label_set_xalign(GTK_LABEL(detail), 0);
	gtk_label_set_wrap(GTK_LABEL(detail), TRUE);
	g_object_set_data(G_OBJECT(disconnect), "action", "disconnect");
	g_object_set_data(G_OBJECT(forget), "action", "forget");
	g_object_set_data(G_OBJECT(autoconnect), "action", "autoconnect");
	g_object_set_data(G_OBJECT(autoconnect), "autoconnect-enabled",
	    strcmp(profile_output, "yes") == 0 ? "no" : "yes");
	g_object_set_data_full(G_OBJECT(disconnect), "ssid",
	    g_strdup(ssid), g_free);
	g_object_set_data_full(G_OBJECT(forget), "ssid",
	    g_strdup(ssid), g_free);
	g_object_set_data_full(G_OBJECT(autoconnect), "ssid",
	    g_strdup(ssid), g_free);
	gtk_box_append(GTK_BOX(root), eyebrow);
	gtk_box_append(GTK_BOX(root), title);
	gtk_box_append(GTK_BOX(root), detail);
	gtk_box_append(GTK_BOX(root), disconnect);
	gtk_box_append(GTK_BOX(root), autoconnect);
	gtk_box_append(GTK_BOX(root), forget);
	gtk_box_append(GTK_BOX(root), cancel);
	show_dialog_page(app, root);
	g_free(detail_text);
	g_string_free(dns, TRUE);
	g_free(signal);
	g_free(security);
	g_free(frequency);
	g_free(channel);
	g_free(rate);
	g_free(dbm);
	g_strfreev(device);
	g_strfreev(radio_lines);
	g_strfreev(iw_lines);
	g_free(device_output);
	g_free(radio_output);
	g_free(profile_output);
	g_free(iw_output);
}

static void
show_current_wifi_details(struct app *app)
{
	char *ssid = current_ssid();

	if (strcmp(ssid, "Not connected") != 0)
		show_wifi_actions(app, ssid);
	else
		gtk_label_set_text(app->wifi_status, "Wi-Fi is not connected");
	g_free(ssid);
}

static void
finish_password(GtkButton *button, gpointer data)
{
	struct connect_request *request = data;
	const char *password = gtk_editable_get_text(request->entry);
	char *argv[] = {"nmcli", "device", "wifi", "connect", request->ssid,
	    "password", (char *)password, NULL};

	(void)button;
	gtk_label_set_text(request->app->wifi_status,
	    "Connecting to network...");
	end_text_input(request->app);
	run_detached(argv);
	gtk_stack_set_visible_child_name(request->app->stack, "wifi");
	schedule_connection_check(request->app, request->ssid);
	g_free(request->ssid);
	g_free(request);
}

static void
cancel_password(GtkButton *button, gpointer data)
{
	struct connect_request *request = data;

	(void)button;
	end_text_input(request->app);
	gtk_stack_set_visible_child_name(request->app->stack, "wifi");
	g_free(request->ssid);
	g_free(request);
}

static void
hidden_network_connect(GtkButton *button, gpointer data)
{
	struct app *app = data;
	GtkWidget *ssid_entry = g_object_get_data(
	    G_OBJECT(button), "ssid-entry");
	GtkWidget *password_entry = g_object_get_data(
	    G_OBJECT(button), "password-entry");
	const char *ssid;
	const char *password;
	char *secured_argv[] = {"nmcli", "device", "wifi", "connect", NULL,
	    "password", NULL, "hidden", "yes", NULL};
	char *open_argv[] = {"nmcli", "device", "wifi", "connect", NULL,
	    "hidden", "yes", NULL};

	ssid = gtk_editable_get_text(GTK_EDITABLE(ssid_entry));
	password = gtk_editable_get_text(GTK_EDITABLE(password_entry));
	if (ssid[0] == '\0')
		return;
	secured_argv[4] = (char *)ssid;
	open_argv[4] = (char *)ssid;
	gtk_label_set_text(app->wifi_status, "Connecting to hidden network...");
	end_text_input(app);
	if (password[0] == '\0') {
		run_detached(open_argv);
	} else {
		secured_argv[6] = (char *)password;
		run_detached(secured_argv);
	}
	gtk_stack_set_visible_child_name(app->stack, "wifi");
	schedule_connection_check(app, ssid);
}

static void
show_hidden_network(GtkButton *button, gpointer data)
{
	struct app *app = data;
	GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	GtkWidget *title = gtk_label_new("Hidden Wi-Fi network");
	GtkWidget *ssid = gtk_entry_new();
	GtkWidget *password = gtk_password_entry_new();
	GtkWidget *connect = make_button("Connect", "shade-action",
	    G_CALLBACK(hidden_network_connect), app);
	GtkWidget *cancel = make_button("Cancel", "shade-action",
	    G_CALLBACK(wifi_action_cancel), app);

	(void)button;
	gtk_widget_add_css_class(root, "password-window");
	gtk_widget_add_css_class(title, "shade-title");
	gtk_label_set_xalign(GTK_LABEL(title), 0);
	gtk_entry_set_placeholder_text(GTK_ENTRY(ssid), "Network name");
	g_object_set(password, "placeholder-text", "Password (optional)", NULL);
	g_signal_connect(ssid, "notify::has-focus",
	    G_CALLBACK(text_input_focus_changed), app);
	g_signal_connect(password, "notify::has-focus",
	    G_CALLBACK(text_input_focus_changed), app);
	g_object_set_data(G_OBJECT(connect), "ssid-entry", ssid);
	g_object_set_data(G_OBJECT(connect), "password-entry", password);
	gtk_box_append(GTK_BOX(root), title);
	gtk_box_append(GTK_BOX(root), ssid);
	gtk_box_append(GTK_BOX(root), password);
	gtk_box_append(GTK_BOX(root), connect);
	gtk_box_append(GTK_BOX(root), cancel);
	show_dialog_page(app, root);
	begin_text_input(app, ssid);
}

static void
show_password(struct app *app, const char *ssid)
{
	struct connect_request *request = g_new0(struct connect_request, 1);
	GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	GtkWidget *title = gtk_label_new(ssid);
	GtkWidget *entry = gtk_password_entry_new();
	GtkWidget *connect = gtk_button_new_with_label("Connect");
	GtkWidget *cancel = gtk_button_new_with_label("Cancel");

	request->app = app;
	request->ssid = g_strdup(ssid);
	request->entry = GTK_EDITABLE(entry);
	g_object_set(entry, "placeholder-text", "Password", NULL);
	g_signal_connect(entry, "notify::has-focus",
	    G_CALLBACK(text_input_focus_changed), app);
	gtk_widget_add_css_class(root, "password-window");
	gtk_widget_add_css_class(connect, "shade-action");
	gtk_widget_add_css_class(cancel, "shade-action");
	gtk_box_append(GTK_BOX(root), title);
	gtk_box_append(GTK_BOX(root), entry);
	gtk_box_append(GTK_BOX(root), connect);
	gtk_box_append(GTK_BOX(root), cancel);
	g_signal_connect(connect, "clicked", G_CALLBACK(finish_password), request);
	g_signal_connect(cancel, "clicked", G_CALLBACK(cancel_password), request);
	show_dialog_page(app, root);
	begin_text_input(app, entry);
}

static void
connect_network(GtkButton *button, gpointer data)
{
	struct app *app = data;
	const char *ssid = g_object_get_data(G_OBJECT(button), "ssid");
	bool secured = GPOINTER_TO_INT(g_object_get_data(
	    G_OBJECT(button), "secured"));
	bool current = GPOINTER_TO_INT(g_object_get_data(
	    G_OBJECT(button), "current"));
	char *argv[] = {"nmcli", "connection", "up", "id", (char *)ssid, NULL};
	char *result;

	if (current) {
		show_wifi_actions(app, ssid);
		return;
	}
	gtk_label_set_text(app->wifi_status, "Connecting...");
	result = run_capture(argv);
	if (result[0] == '\0' && secured) {
		show_password(app, ssid);
	} else if (result[0] == '\0') {
		char *connect_argv[] = {"nmcli", "device", "wifi", "connect",
		    (char *)ssid, NULL};
		run_detached(connect_argv);
		schedule_connection_check(app, ssid);
	} else {
		schedule_connection_check(app, ssid);
	}
	g_free(result);
}

static void
drag_begin(GtkGestureDrag *gesture, double x, double y, gpointer data)
{
	struct app *app = data;
	GtkWidget *root = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
	GtkWidget *picked = gtk_widget_pick(root, x, y, GTK_PICK_DEFAULT);

	app->close_drag_allowed = true;
	app->close_drag_x = 0;
	app->close_drag_y = 0;
	/* Decide ownership at touch-down: scroll while content remains below.
	 * At the bottom (including content that fits), an upward drag closes.
	 * Never steal a slider interaction or a drag that merely reaches bottom. */
	for (GtkWidget *node = picked; node != NULL && node != root;
	    node = gtk_widget_get_parent(node)) {
		if (GTK_IS_RANGE(node)) {
			app->close_drag_allowed = false;
			break;
		}
		if (GTK_IS_SCROLLED_WINDOW(node)) {
			GtkAdjustment *adjustment = gtk_scrolled_window_get_vadjustment(
			    GTK_SCROLLED_WINDOW(node));
			double bottom = gtk_adjustment_get_upper(adjustment) -
			    gtk_adjustment_get_page_size(adjustment);
			if (gtk_adjustment_get_value(adjustment) < bottom - 1.0)
				app->close_drag_allowed = false;
		}
	}
	if (!app->close_drag_allowed)
		gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_DENIED);
}

static void
drag_update(GtkGestureDrag *gesture, double x, double y, gpointer data)
{
	struct app *app = data;

	if (!app->close_drag_allowed)
		return;
	/* Claim only upward intent, before the child's kinetic scroller does.
	 * Horizontal notification dismissals and downward scrolling stay native. */
	if (fabs(x) > 8.0 || fabs(y) > 8.0) {
		if (y < 0 && -y > fabs(x) * 1.25)
			gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
		else {
			app->close_drag_allowed = false;
			gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_DENIED);
			return;
		}
	}
	app->close_drag_x = x;
	app->close_drag_y = y;
}

static void
drag_end(GtkGestureDrag *gesture, double x, double y, gpointer data)
{
	struct app *app = data;
	double dx = x;
	double dy = y;

	(void)gesture;
	if (!app->close_drag_allowed)
		return;
	if (app->close_drag_y < dy) {
		dx = app->close_drag_x;
		dy = app->close_drag_y;
	}
	if (dy < -54 && -dy > (dx < 0 ? -dx : dx) * 1.25)
		hide_shade(app);
}

static GtkWidget *
make_button(const char *label, const char *style,
    GCallback callback, struct app *app)
{
	GtkWidget *button = gtk_button_new_with_label(label);

	gtk_widget_add_css_class(button, style);
	g_signal_connect(button, "clicked", callback, app);
	return button;
}

static GtkWidget *
make_icon_button(const char *icon, const char *label, const char *style,
    bool vertical, GCallback callback, struct app *app, GtkLabel **label_out)
{
	GtkWidget *button = gtk_button_new();
	GtkWidget *content = gtk_box_new(vertical ? GTK_ORIENTATION_VERTICAL :
	    GTK_ORIENTATION_HORIZONTAL, vertical ? 3 : 6);
	GtkWidget *icon_label = gtk_label_new(icon);
	GtkWidget *text_label = gtk_label_new(label);

	gtk_widget_add_css_class(button, style);
	gtk_widget_add_css_class(icon_label, "shade-control-icon");
	gtk_widget_add_css_class(text_label, "shade-control-label");
	gtk_widget_set_halign(content, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(content, GTK_ALIGN_CENTER);
	gtk_label_set_ellipsize(GTK_LABEL(text_label), PANGO_ELLIPSIZE_END);
	gtk_box_append(GTK_BOX(content), icon_label);
	gtk_box_append(GTK_BOX(content), text_label);
	gtk_button_set_child(GTK_BUTTON(button), content);
	if (callback != NULL)
		g_signal_connect(button, "clicked", callback, app);
	if (label_out != NULL)
		*label_out = GTK_LABEL(text_label);
	return button;
}

typedef void (*button_action_func)(GtkButton *, gpointer);

struct hold_action {
	GtkButton *button;
	struct app *app;
	button_action_func tap;
	const char *detail;
	guint timeout;
	bool fired;
};

static void
free_hold_action(gpointer data)
{
	struct hold_action *action = data;

	if (action->timeout != 0)
		g_source_remove(action->timeout);
	g_free(action);
}

static gboolean
fire_hold_action(gpointer data)
{
	struct hold_action *action = data;

	action->timeout = 0;
	action->fired = true;
	if (strcmp(action->detail, "wifi") == 0)
		show_wifi(action->app);
	else if (strcmp(action->detail, "bluetooth") == 0)
		show_bluetooth(action->app);
	else
		show_sound(action->app);
	return G_SOURCE_REMOVE;
}

static void
hold_action_pressed(GtkGestureClick *gesture, int presses, double x, double y,
    gpointer data)
{
	struct hold_action *action = data;

	(void)presses;
	(void)x;
	(void)y;
	if (action->timeout != 0)
		g_source_remove(action->timeout);
	action->fired = false;
	gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
	action->timeout = g_timeout_add(420, fire_hold_action, action);
}

static void
hold_action_released(GtkGestureClick *gesture, int presses, double x, double y,
    gpointer data)
{
	struct hold_action *action = data;

	(void)gesture;
	(void)presses;
	(void)x;
	(void)y;
	if (action->timeout != 0) {
		g_source_remove(action->timeout);
		action->timeout = 0;
	}
	if (!action->fired)
		action->tap(action->button, action->app);
}

static void
add_hold_action(GtkWidget *widget, button_action_func tap, const char *detail,
    struct app *app)
{
	struct hold_action *action = g_new0(struct hold_action, 1);
	GtkGesture *gesture = gtk_gesture_click_new();

	action->button = GTK_BUTTON(widget);
	action->app = app;
	action->tap = tap;
	action->detail = detail;
	/* The captured touch hold owns pointer release; native activation remains
	 * available to keyboard and assistive technology users. */
	g_signal_connect(widget, "clicked", G_CALLBACK(tap), app);
	g_object_set_data_full(G_OBJECT(widget), "ctlst-hold-action", action,
	    free_hold_action);
	gtk_gesture_single_set_exclusive(GTK_GESTURE_SINGLE(gesture), TRUE);
	gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(gesture),
	    GTK_PHASE_CAPTURE);
	g_signal_connect(gesture, "pressed", G_CALLBACK(hold_action_pressed),
	    action);
	g_signal_connect(gesture, "released", G_CALLBACK(hold_action_released),
	    action);
	gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(gesture));
}

#if 0
static GtkWidget *
build_quick_settings(struct app *app)
{
	GtkWidget *scroller = gtk_scrolled_window_new();
	GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	GtkWidget *back = make_button("Close", "shade-action",
	    G_CALLBACK(action_close), app);
	GtkWidget *heading = gtk_label_new("Quick settings");
	GtkWidget *subtitle = gtk_label_new("Pixel 3a XL  /  Sway Touch");
	GtkWidget *grid = gtk_grid_new();
	GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
	GtkWidget *name = gtk_label_new("Network");
	GtkWidget *detail = gtk_label_new("Loading...");
	GtkWidget *cell_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
	GtkWidget *cell_name = gtk_label_new("Cellular");
	GtkWidget *cell_detail = gtk_label_new("Loading...");
	GtkWidget *brightness_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	GtkWidget *brightness_label = gtk_label_new("Brightness");
	GtkWidget *quick = gtk_grid_new();
	GtkWidget *phone;
	GtkWidget *messages;
	GtkWidget *theme;
	GtkWidget *settings;
	GtkWidget *close;
	GtkWidget *screenshot;

	gtk_widget_set_hexpand(heading, TRUE);
	gtk_label_set_xalign(GTK_LABEL(heading), 0);
	gtk_label_set_xalign(GTK_LABEL(subtitle), 0);
	gtk_widget_add_css_class(root, "shade-page");
	gtk_widget_add_css_class(root, "shade-quick-settings");
	gtk_widget_add_css_class(header, "shade-header");
	gtk_widget_add_css_class(back, "shade-header-close");
	gtk_widget_add_css_class(heading, "shade-title");
	gtk_widget_add_css_class(subtitle, "shade-subtitle");
	gtk_box_append(GTK_BOX(header), back);
	gtk_box_append(GTK_BOX(header), heading);
	gtk_box_append(GTK_BOX(root), header);
	gtk_box_append(GTK_BOX(root), subtitle);

	gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
	gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
	gtk_widget_set_hexpand(grid, TRUE);
	app->wifi_button = GTK_BUTTON(make_button("Wi-Fi", "shade-tile",
	    G_CALLBACK(action_wifi_page), app));
	app->sound_button = GTK_BUTTON(make_button("Sound", "shade-tile",
	    G_CALLBACK(action_sound), app));
	app->bluetooth_button = GTK_BUTTON(make_button("Bluetooth", "shade-tile",
	    G_CALLBACK(action_bluetooth_page), app));
	app->rotation_button = GTK_BUTTON(make_button("Rotation lock\nOff",
	    "shade-tile", G_CALLBACK(action_rotate), app));
	app->screen_idle_button = GTK_BUTTON(make_button("Screen 5 min",
	    "shade-tile", G_CALLBACK(action_screen_idle), app));
	app->notifications_button = GTK_BUTTON(make_button("Notifications",
	    "shade-tile", G_CALLBACK(action_notifications_page), app));
	app->airplane_button = GTK_BUTTON(make_button("Airplane\nOff",
	    "shade-tile", G_CALLBACK(action_airplane_mode), app));
	app->tailscale_button = GTK_BUTTON(make_button("Tailscale\nOff",
	    "shade-tile", G_CALLBACK(action_tailscale_toggle), app));
	screenshot = make_button("Screenshot", "shade-tile",
	    G_CALLBACK(action_screenshot), app);
	gtk_widget_set_hexpand(GTK_WIDGET(app->wifi_button), TRUE);
	gtk_widget_set_hexpand(GTK_WIDGET(app->sound_button), TRUE);
	gtk_widget_set_hexpand(GTK_WIDGET(app->bluetooth_button), TRUE);
	gtk_widget_set_hexpand(GTK_WIDGET(app->rotation_button), TRUE);
	gtk_widget_set_hexpand(GTK_WIDGET(app->screen_idle_button), TRUE);
	gtk_widget_set_hexpand(GTK_WIDGET(app->notifications_button), TRUE);
	gtk_widget_set_hexpand(GTK_WIDGET(app->airplane_button), TRUE);
	gtk_widget_set_hexpand(GTK_WIDGET(app->tailscale_button), TRUE);
	gtk_widget_set_hexpand(screenshot, TRUE);
	gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(app->wifi_button), 0, 0, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(app->sound_button), 1, 0, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(app->bluetooth_button), 0, 1, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(app->rotation_button),
	    1, 1, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(app->screen_idle_button),
	    0, 2, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(app->notifications_button),
	    1, 2, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), screenshot, 0, 3, 2, 1);
	gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(app->airplane_button),
	    0, 4, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(app->tailscale_button),
	    1, 4, 1, 1);
	gtk_box_append(GTK_BOX(root), grid);

	gtk_label_set_xalign(GTK_LABEL(name), 0);
	gtk_label_set_xalign(GTK_LABEL(detail), 0);
	gtk_label_set_ellipsize(GTK_LABEL(detail), PANGO_ELLIPSIZE_END);
	gtk_widget_add_css_class(card, "shade-card");
	gtk_widget_add_css_class(card, "shade-card-network");
	gtk_widget_add_css_class(name, "shade-network");
	gtk_widget_add_css_class(detail, "shade-detail");
	app->network_name = GTK_LABEL(name);
	app->network_detail = GTK_LABEL(detail);
	gtk_box_append(GTK_BOX(card), name);
	gtk_box_append(GTK_BOX(card), detail);
	g_signal_connect_swapped(card, "notify::visible",
	    G_CALLBACK(refresh_status), app);
	GtkGesture *network_click = gtk_gesture_click_new();
	g_signal_connect_swapped(network_click, "released",
	    G_CALLBACK(show_wifi), app);
	gtk_widget_add_controller(card, GTK_EVENT_CONTROLLER(network_click));
	gtk_box_append(GTK_BOX(root), card);

	gtk_label_set_xalign(GTK_LABEL(cell_name), 0);
	gtk_label_set_xalign(GTK_LABEL(cell_detail), 0);
	gtk_widget_add_css_class(cell_card, "shade-card");
	gtk_widget_add_css_class(cell_card, "shade-card-cellular");
	gtk_widget_add_css_class(cell_name, "shade-network");
	gtk_widget_add_css_class(cell_detail, "shade-detail");
	app->cell_name = GTK_LABEL(cell_name);
	app->cell_detail = GTK_LABEL(cell_detail);
	gtk_box_append(GTK_BOX(cell_card), cell_name);
	gtk_box_append(GTK_BOX(cell_card), cell_detail);
	gtk_box_append(GTK_BOX(root), cell_card);

	gtk_widget_add_css_class(brightness_label, "shade-section");
	app->brightness = GTK_SCALE(gtk_scale_new_with_range(
	    GTK_ORIENTATION_HORIZONTAL, 5, 100, 1));
	gtk_range_set_value(GTK_RANGE(app->brightness), 65);
	gtk_widget_set_hexpand(GTK_WIDGET(app->brightness), TRUE);
	gtk_widget_add_css_class(GTK_WIDGET(app->brightness), "shade-slider");
	g_signal_connect(app->brightness, "value-changed",
	    G_CALLBACK(brightness_changed), app);
	gtk_box_append(GTK_BOX(brightness_row), brightness_label);
	gtk_box_append(GTK_BOX(brightness_row), GTK_WIDGET(app->brightness));
	gtk_box_append(GTK_BOX(root), brightness_row);

	phone = make_button("Phone", "shade-action", G_CALLBACK(action_launch), app);
	messages = make_button("Messages", "shade-action",
	    G_CALLBACK(action_launch), app);
	theme = make_button("Theme", "shade-action", G_CALLBACK(action_theme), app);
	settings = make_button("Settings", "shade-action",
	    G_CALLBACK(action_launch), app);
	close = make_button("Close", "shade-action", G_CALLBACK(action_close), app);
	gtk_widget_add_css_class(close, "shade-close");
	g_object_set_data(G_OBJECT(phone), "desktop", "dev.ctlst.Dialer");
	g_object_set_data(G_OBJECT(messages), "desktop", "dev.ctlst.Messages");
	g_object_set_data(G_OBJECT(settings), "desktop", "dev.ctlst.Settings");
	gtk_grid_set_row_spacing(GTK_GRID(quick), 8);
	gtk_grid_set_column_spacing(GTK_GRID(quick), 8);
	for (GtkWidget *item = phone; item != NULL;) {
		gtk_widget_set_hexpand(item, TRUE);
		if (item == phone)
			item = messages;
		else if (item == messages)
			item = theme;
		else if (item == theme)
			item = settings;
		else if (item == settings)
			item = close;
		else
			item = NULL;
	}
	gtk_grid_attach(GTK_GRID(quick), phone, 0, 0, 1, 1);
	gtk_grid_attach(GTK_GRID(quick), messages, 1, 0, 1, 1);
	gtk_grid_attach(GTK_GRID(quick), theme, 2, 0, 1, 1);
	gtk_grid_attach(GTK_GRID(quick), settings, 0, 1, 2, 1);
	gtk_grid_attach(GTK_GRID(quick), close, 2, 1, 1, 1);
	gtk_box_append(GTK_BOX(root), quick);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
	    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_kinetic_scrolling(
	    GTK_SCROLLED_WINDOW(scroller), TRUE);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), root);
	gtk_widget_set_vexpand(scroller, TRUE);
	return scroller;
}

#endif

/* Balance four primary controls into 1, 2 or 4 columns. Natural child
 * requests include user CSS and font sizes; no orientation or device guess. */
typedef struct { GtkWidget parent_instance; } CtlsQuickControls;
typedef struct { GtkWidgetClass parent_class; } CtlsQuickControlsClass;
G_DEFINE_TYPE(CtlsQuickControls, ctlst_quick_controls, GTK_TYPE_WIDGET)

static int
quick_columns(GtkWidget *widget, int width, int *minimum, int *natural)
{
	int count = 0;
	*minimum = *natural = 0;
	for (GtkWidget *child = gtk_widget_get_first_child(widget); child;
	    child = gtk_widget_get_next_sibling(child)) {
		int child_min, child_nat;
		gtk_widget_measure(child, GTK_ORIENTATION_HORIZONTAL, -1,
		    &child_min, &child_nat, NULL, NULL);
		*minimum = MAX(*minimum, child_min);
		*natural = MAX(*natural, child_nat);
		count++;
	}
	int preferred = MAX(1, *natural);
	if (count >= 4 && (width < 0 || width / 4 >= preferred)) return 4;
	if (count >= 2 && (width < 0 || width / 2 >= preferred)) return 2;
	return 1;
}

static GtkSizeRequestMode
quick_request_mode(GtkWidget *widget)
{
	(void)widget;
	return GTK_SIZE_REQUEST_HEIGHT_FOR_WIDTH;
}

static void
quick_measure(GtkWidget *widget, GtkOrientation orientation, int for_size,
    int *minimum, int *natural, int *minimum_baseline, int *natural_baseline)
{
	int child_min, child_nat;
	int columns = quick_columns(widget, for_size, &child_min, &child_nat);
	*minimum_baseline = *natural_baseline = -1;
	if (orientation == GTK_ORIENTATION_HORIZONTAL) {
		*minimum = child_min;
		*natural = child_nat * 4;
		return;
	}
	int width = for_size < 0 ? child_nat : for_size / columns;
	int rows = 0, count = 0, row_min = 0, row_nat = 0;
	for (GtkWidget *child = gtk_widget_get_first_child(widget); child;
	    child = gtk_widget_get_next_sibling(child)) {
		gtk_widget_measure(child, GTK_ORIENTATION_VERTICAL, width,
		    &child_min, &child_nat, NULL, NULL);
		row_min = MAX(row_min, child_min);
		row_nat = MAX(row_nat, child_nat);
		count++;
	}
	rows = (count + columns - 1) / columns;
	*minimum = rows * row_min;
	*natural = rows * row_nat;
}

static void
quick_allocate(GtkWidget *widget, int width, int height, int baseline)
{
	(void)height; (void)baseline;
	int minimum, natural;
	int columns = quick_columns(widget, width, &minimum, &natural);
	int row_height = 0;
	for (GtkWidget *child = gtk_widget_get_first_child(widget); child;
	    child = gtk_widget_get_next_sibling(child)) {
		gtk_widget_measure(child, GTK_ORIENTATION_VERTICAL, width / columns,
		    &minimum, &natural, NULL, NULL);
		row_height = MAX(row_height, natural);
	}
	int index = 0;
	for (GtkWidget *child = gtk_widget_get_first_child(widget); child;
	    child = gtk_widget_get_next_sibling(child), index++) {
		int x = (index % columns) * width / columns;
		int right = ((index % columns) + 1) * width / columns;
		graphene_point_t point = GRAPHENE_POINT_INIT(x, (index / columns) * row_height);
		gtk_widget_allocate(child, right - x, row_height, -1,
		    gsk_transform_translate(NULL, &point));
	}
}

static void
quick_snapshot(GtkWidget *widget, GtkSnapshot *snapshot)
{
	for (GtkWidget *child = gtk_widget_get_first_child(widget); child;
	    child = gtk_widget_get_next_sibling(child))
		gtk_widget_snapshot_child(widget, child, snapshot);
}

static void
quick_dispose(GObject *object)
{
	GtkWidget *child;
	while ((child = gtk_widget_get_first_child(GTK_WIDGET(object))))
		gtk_widget_unparent(child);
	G_OBJECT_CLASS(ctlst_quick_controls_parent_class)->dispose(object);
}

static void
ctlst_quick_controls_class_init(CtlsQuickControlsClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
	widget_class->get_request_mode = quick_request_mode;
	widget_class->measure = quick_measure;
	widget_class->size_allocate = quick_allocate;
	widget_class->snapshot = quick_snapshot;
	gtk_widget_class_set_css_name(widget_class, "ctlstquickcontrols");
	gtk_widget_class_set_accessible_role(widget_class, GTK_ACCESSIBLE_ROLE_GROUP);
	G_OBJECT_CLASS(klass)->dispose = quick_dispose;
}

static void
ctlst_quick_controls_init(CtlsQuickControls *controls)
{
	(void)controls;
}

static void
prepare_wrapping_label(GtkLabel *label, int width_chars)
{
	gtk_label_set_ellipsize(label, PANGO_ELLIPSIZE_NONE);
	gtk_label_set_wrap(label, TRUE);
	gtk_label_set_wrap_mode(label, PANGO_WRAP_WORD_CHAR);
	gtk_label_set_width_chars(label, 1);
	gtk_label_set_max_width_chars(label, width_chars);
}

static void
prepare_control_label(GtkWidget *button)
{
	GtkWidget *content = gtk_button_get_child(GTK_BUTTON(button));
	prepare_wrapping_label(GTK_LABEL(gtk_widget_get_last_child(content)), 12);
}

static GtkWidget *
quick_control_group(GtkWidget *toggle, const char *detail, GCallback callback,
    struct app *app)
{
	GtkWidget *group = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	GtkWidget *open = gtk_button_new_from_icon_name("go-next-symbolic");
	gtk_widget_add_css_class(group, "shade-control-group");
	gtk_widget_add_css_class(open, "shade-control-detail");
	gtk_widget_set_hexpand(toggle, TRUE);
	gtk_widget_set_tooltip_text(open, detail);
	gtk_accessible_update_property(GTK_ACCESSIBLE(open),
	    GTK_ACCESSIBLE_PROPERTY_LABEL, detail, -1);
	g_signal_connect_swapped(open, "clicked", callback, app);
	gtk_box_append(GTK_BOX(group), toggle);
	gtk_box_append(GTK_BOX(group), open);
	return group;
}

static GtkWidget *
build_quick_settings(struct app *app)
{
	GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	GtkWidget *close = make_button("×", "shade-action",
	    G_CALLBACK(action_close), app);
	GtkWidget *heading = gtk_label_new("Quick settings");
	GtkWidget *settings_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	GtkWidget *primary = g_object_new(ctlst_quick_controls_get_type(), NULL);
	GtkWidget *rotation_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	GtkWidget *rotation_icon = gtk_label_new("↻");
	GtkWidget *rotation_text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	GtkWidget *rotation_title = gtk_label_new("Rotation lock");
	GtkWidget *utilities = g_object_new(ctlst_quick_controls_get_type(), NULL);
	GtkWidget *status_grid = gtk_grid_new();
	GtkWidget *network_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
	GtkWidget *network_name = gtk_label_new("Network");
	GtkWidget *network_detail = gtk_label_new("Loading...");
	GtkWidget *cell_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
	GtkWidget *cell_name = gtk_label_new("Cellular");
	GtkWidget *cell_detail = gtk_label_new("Loading...");
	GtkWidget *brightness_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	GtkWidget *brightness_icon = gtk_label_new("☀");
	GtkWidget *notification_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	GtkWidget *notification_heading = gtk_label_new("Notifications");
	GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
	GtkWidget *notification_scroller = gtk_scrolled_window_new();
	GtkWidget *settings;
	GtkWidget *screenshot;
	GtkGesture *network_click;

	gtk_widget_set_vexpand(root, TRUE);
	gtk_widget_set_hexpand(heading, TRUE);
	gtk_label_set_xalign(GTK_LABEL(heading), 0);
	gtk_widget_add_css_class(root, "shade-page");
	gtk_widget_add_css_class(root, "shade-quick-settings");
	gtk_widget_add_css_class(header, "shade-header");
	gtk_widget_add_css_class(close, "shade-header-close");
	gtk_widget_set_tooltip_text(close, "Close quick settings");
	gtk_widget_add_css_class(heading, "shade-title");
	prepare_wrapping_label(GTK_LABEL(heading), 24);
	gtk_box_append(GTK_BOX(header), heading);
	gtk_box_append(GTK_BOX(header), close);
	gtk_box_append(GTK_BOX(root), header);

	gtk_widget_add_css_class(settings_box, "shade-settings-box");
	gtk_widget_add_css_class(primary, "shade-primary");
	app->wifi_button = GTK_BUTTON(make_icon_button("󰤨", "Wi-Fi",
	    "shade-control-tile", true, NULL, app, NULL));
	app->bluetooth_button = GTK_BUTTON(make_icon_button("󰂯", "Bluetooth",
	    "shade-control-tile", true, NULL, app, NULL));
	app->sound_button = GTK_BUTTON(make_icon_button("󰕾", "Sound",
	    "shade-control-tile", true, NULL, app, NULL));
	app->airplane_button = GTK_BUTTON(make_icon_button("󰀝", "Airplane",
	    "shade-control-tile", true, G_CALLBACK(action_airplane_mode), app,
	    &app->airplane_label));
	prepare_control_label(GTK_WIDGET(app->wifi_button));
	prepare_control_label(GTK_WIDGET(app->bluetooth_button));
	prepare_control_label(GTK_WIDGET(app->sound_button));
	prepare_control_label(GTK_WIDGET(app->airplane_button));
	gtk_widget_set_parent(quick_control_group(
	    GTK_WIDGET(app->wifi_button), "Wi-Fi details", G_CALLBACK(show_wifi), app), primary);
	gtk_widget_set_parent(quick_control_group(
	    GTK_WIDGET(app->bluetooth_button), "Bluetooth details",
	    G_CALLBACK(show_bluetooth), app), primary);
	gtk_widget_set_parent(quick_control_group(
	    GTK_WIDGET(app->sound_button), "Sound details", G_CALLBACK(show_sound), app), primary);
	gtk_widget_set_parent(GTK_WIDGET(app->airplane_button), primary);
	add_hold_action(GTK_WIDGET(app->wifi_button), action_wifi_toggle,
	    "wifi", app);
	add_hold_action(GTK_WIDGET(app->bluetooth_button),
	    action_bluetooth_toggle, "bluetooth", app);
	add_hold_action(GTK_WIDGET(app->sound_button), action_sound, "sound", app);
	gtk_box_append(GTK_BOX(settings_box), primary);

	gtk_widget_add_css_class(rotation_row, "shade-toggle-row");
	gtk_widget_add_css_class(rotation_icon, "shade-control-icon");
	gtk_widget_add_css_class(rotation_title, "shade-toggle-title");
	gtk_label_set_xalign(GTK_LABEL(rotation_title), 0);
	app->rotation_label = GTK_LABEL(gtk_label_new("Auto-rotate"));
	prepare_wrapping_label(GTK_LABEL(rotation_title), 24);
	prepare_wrapping_label(app->rotation_label, 24);
	gtk_label_set_xalign(app->rotation_label, 0);
	gtk_widget_add_css_class(GTK_WIDGET(app->rotation_label),
	    "shade-toggle-state");
	gtk_widget_set_hexpand(rotation_text, TRUE);
	gtk_box_append(GTK_BOX(rotation_text), rotation_title);
	gtk_box_append(GTK_BOX(rotation_text), GTK_WIDGET(app->rotation_label));
	app->rotation_switch = GTK_SWITCH(gtk_switch_new());
	gtk_accessible_update_property(GTK_ACCESSIBLE(app->rotation_switch),
	    GTK_ACCESSIBLE_PROPERTY_LABEL, "Rotation lock", -1);
	update_rotation_button(app);
	gtk_widget_set_valign(GTK_WIDGET(app->rotation_switch), GTK_ALIGN_CENTER);
	g_signal_connect(app->rotation_switch, "notify::active",
	    G_CALLBACK(action_rotate), app);
	gtk_box_append(GTK_BOX(rotation_row), rotation_icon);
	gtk_box_append(GTK_BOX(rotation_row), rotation_text);
	gtk_box_append(GTK_BOX(rotation_row), GTK_WIDGET(app->rotation_switch));
	gtk_box_append(GTK_BOX(settings_box), rotation_row);

	gtk_widget_add_css_class(utilities, "shade-utilities");
	app->screen_idle_button = GTK_BUTTON(make_icon_button("◷", "Screen",
	    "shade-mini-control", false, G_CALLBACK(action_screen_idle), app,
	    &app->screen_idle_label));
	app->tailscale_button = GTK_BUTTON(make_icon_button("◇", "Tailscale",
	    "shade-mini-control", false, G_CALLBACK(action_tailscale_toggle), app,
	    &app->tailscale_label));
	screenshot = make_icon_button("▣", "Capture", "shade-mini-control",
	    false, G_CALLBACK(action_screenshot), app, NULL);
	settings = make_icon_button("⚙", "Settings", "shade-mini-control",
	    false, G_CALLBACK(action_launch), app, NULL);
	g_object_set_data(G_OBJECT(settings), "desktop", "dev.ctlst.Settings");
	prepare_control_label(GTK_WIDGET(app->screen_idle_button));
	prepare_control_label(GTK_WIDGET(app->tailscale_button));
	prepare_control_label(screenshot);
	prepare_control_label(settings);
	gtk_widget_set_parent(GTK_WIDGET(app->screen_idle_button), utilities);
	gtk_widget_set_parent(GTK_WIDGET(app->tailscale_button), utilities);
	gtk_widget_set_parent(screenshot, utilities);
	gtk_widget_set_parent(settings, utilities);
	gtk_box_append(GTK_BOX(settings_box), utilities);

	gtk_label_set_xalign(GTK_LABEL(network_name), 0);
	prepare_wrapping_label(GTK_LABEL(network_name), 24);
	gtk_label_set_xalign(GTK_LABEL(network_detail), 0);
	gtk_label_set_ellipsize(GTK_LABEL(network_detail), PANGO_ELLIPSIZE_END);
	gtk_widget_add_css_class(network_card, "shade-card");
	gtk_widget_add_css_class(network_card, "shade-card-network");
	gtk_widget_add_css_class(network_name, "shade-network");
	gtk_widget_add_css_class(network_detail, "shade-detail");
	app->network_name = GTK_LABEL(network_name);
	app->network_detail = GTK_LABEL(network_detail);
	gtk_box_append(GTK_BOX(network_card), network_name);
	gtk_box_append(GTK_BOX(network_card), network_detail);
	g_signal_connect_swapped(network_card, "notify::visible",
	    G_CALLBACK(refresh_status), app);
	network_click = gtk_gesture_click_new();
	g_signal_connect_swapped(network_click, "released",
	    G_CALLBACK(show_wifi), app);
	gtk_widget_add_controller(network_card,
	    GTK_EVENT_CONTROLLER(network_click));

	gtk_label_set_xalign(GTK_LABEL(cell_name), 0);
	gtk_label_set_xalign(GTK_LABEL(cell_detail), 0);
	prepare_wrapping_label(GTK_LABEL(cell_name), 24);
	gtk_label_set_ellipsize(GTK_LABEL(cell_detail), PANGO_ELLIPSIZE_END);
	gtk_widget_add_css_class(cell_card, "shade-card");
	gtk_widget_add_css_class(cell_card, "shade-card-cellular");
	gtk_widget_add_css_class(cell_name, "shade-network");
	gtk_widget_add_css_class(cell_detail, "shade-detail");
	app->cell_name = GTK_LABEL(cell_name);
	app->cell_detail = GTK_LABEL(cell_detail);
	gtk_box_append(GTK_BOX(cell_card), cell_name);
	gtk_box_append(GTK_BOX(cell_card), cell_detail);
	gtk_grid_set_column_homogeneous(GTK_GRID(status_grid), TRUE);
	gtk_grid_set_column_spacing(GTK_GRID(status_grid), 6);
	gtk_widget_add_css_class(status_grid, "shade-status-grid");
	gtk_grid_attach(GTK_GRID(status_grid), network_card, 0, 0, 1, 1);
	gtk_grid_attach(GTK_GRID(status_grid), cell_card, 1, 0, 1, 1);
	gtk_box_append(GTK_BOX(settings_box), status_grid);

	gtk_widget_add_css_class(brightness_icon, "shade-control-icon");
	app->brightness = GTK_SCALE(gtk_scale_new_with_range(
	    GTK_ORIENTATION_HORIZONTAL, 5, 100, 1));
	gtk_range_set_value(GTK_RANGE(app->brightness), 65);
	gtk_widget_set_hexpand(GTK_WIDGET(app->brightness), TRUE);
	gtk_widget_add_css_class(GTK_WIDGET(app->brightness), "shade-slider");
	gtk_accessible_update_property(GTK_ACCESSIBLE(app->brightness),
	    GTK_ACCESSIBLE_PROPERTY_LABEL, "Brightness", -1);
	g_signal_connect(app->brightness, "value-changed",
	    G_CALLBACK(brightness_changed), app);
	gtk_box_append(GTK_BOX(brightness_row), brightness_icon);
	gtk_box_append(GTK_BOX(brightness_row), GTK_WIDGET(app->brightness));
	/* Frequent adjustment precedes the secondary controls and diagnostics. */
	gtk_box_insert_child_after(GTK_BOX(settings_box), brightness_row, primary);
	/* One kinetic content viewport keeps every control reachable in landscape
	 * and avoids competing nested scroll regions. The close header stays put. */
	gtk_box_append(GTK_BOX(content), settings_box);

	gtk_widget_set_hexpand(notification_heading, TRUE);
	gtk_label_set_xalign(GTK_LABEL(notification_heading), 0);
	gtk_widget_add_css_class(notification_header, "shade-notification-header");
	gtk_widget_add_css_class(notification_heading, "shade-notification-title");
	prepare_wrapping_label(GTK_LABEL(notification_heading), 24);
	app->notification_clear = make_button("Clear all", "shade-chip",
	    G_CALLBACK(action_clear_notifications), app);
	gtk_box_append(GTK_BOX(notification_header), notification_heading);
	gtk_box_append(GTK_BOX(notification_header), app->notification_clear);
	gtk_box_append(GTK_BOX(content), notification_header);
	app->notification_list = GTK_BOX(
	    gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(notification_scroller),
	    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_kinetic_scrolling(
	    GTK_SCROLLED_WINDOW(notification_scroller), TRUE);
	gtk_box_append(GTK_BOX(content), GTK_WIDGET(app->notification_list));
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(notification_scroller),
	    content);
	gtk_widget_set_vexpand(notification_scroller, TRUE);
	gtk_box_append(GTK_BOX(root), notification_scroller);
	return root;
}

static GtkWidget *
build_sound(struct app *app)
{
	GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	GtkWidget *back = make_button("Back", "shade-action",
	    G_CALLBACK(action_settings_page), app);
	GtkWidget *heading = gtk_label_new("Sound");
	GtkWidget *profiles = gtk_grid_new();
	GtkWidget *sound;
	GtkWidget *vibrate;
	GtkWidget *silent;
	GtkWidget *settings;

	gtk_widget_add_css_class(root, "shade-page");
	gtk_widget_add_css_class(header, "shade-header");
	gtk_widget_set_hexpand(heading, TRUE);
	gtk_label_set_xalign(GTK_LABEL(heading), 0);
	gtk_widget_add_css_class(heading, "shade-title");
	gtk_box_append(GTK_BOX(header), back);
	gtk_box_append(GTK_BOX(header), heading);
	gtk_box_append(GTK_BOX(root), header);
	app->sound_status = GTK_LABEL(gtk_label_new(
	    script_available("sound-profile") ?
	    "Choose how calls and notifications alert you" :
	    "Sound profiles unavailable"));
	gtk_label_set_xalign(app->sound_status, 0);
	gtk_widget_add_css_class(GTK_WIDGET(app->sound_status), "shade-subtitle");
	gtk_box_append(GTK_BOX(root), GTK_WIDGET(app->sound_status));

	gtk_grid_set_column_homogeneous(GTK_GRID(profiles), TRUE);
	gtk_grid_set_column_spacing(GTK_GRID(profiles), 8);
	sound = make_icon_button("󰕾", "Sound", "shade-control-tile",
	    true, G_CALLBACK(action_sound_profile), app, NULL);
	vibrate = make_icon_button("󱄡", "Vibrate", "shade-control-tile",
	    true, G_CALLBACK(action_sound_profile), app, NULL);
	silent = make_icon_button("󰖁", "Silent", "shade-control-tile",
	    true, G_CALLBACK(action_sound_profile), app, NULL);
	g_object_set_data(G_OBJECT(sound), "profile", "sound");
	g_object_set_data(G_OBJECT(vibrate), "profile", "vibrate");
	g_object_set_data(G_OBJECT(silent), "profile", "silent");
	gtk_grid_attach(GTK_GRID(profiles), sound, 0, 0, 1, 1);
	gtk_grid_attach(GTK_GRID(profiles), vibrate, 1, 0, 1, 1);
	gtk_grid_attach(GTK_GRID(profiles), silent, 2, 0, 1, 1);
	app->sound_profiles = profiles;
	gtk_widget_set_sensitive(profiles, script_available("sound-profile"));
	gtk_box_append(GTK_BOX(root), profiles);

	settings = make_icon_button("⚙", "More sound settings",
	    "shade-mini-control", false, G_CALLBACK(action_launch), app, NULL);
	g_object_set_data(G_OBJECT(settings), "desktop", "dev.ctlst.Settings");
	gtk_box_append(GTK_BOX(root), settings);
	return root;
}

static GtkWidget *
build_bluetooth(struct app *app)
{
	GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	GtkWidget *back = make_button("Back", "shade-action",
	    G_CALLBACK(action_home), app);
	GtkWidget *heading = gtk_label_new("Bluetooth");
	GtkWidget *toggle = make_button("On / Off", "shade-action",
	    G_CALLBACK(action_bluetooth_toggle), app);
	GtkWidget *status = gtk_label_new("Loading...");
	GtkWidget *section = gtk_label_new("Devices");
	GtkWidget *scan = make_button("Scan", "shade-action",
	    G_CALLBACK(action_bluetooth_scan), app);
	GtkWidget *section_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	GtkWidget *scroller = gtk_scrolled_window_new();

	gtk_widget_add_css_class(root, "shade-page");
	gtk_widget_add_css_class(header, "shade-header");
	gtk_widget_set_hexpand(heading, TRUE);
	gtk_label_set_xalign(GTK_LABEL(heading), 0);
	gtk_widget_add_css_class(heading, "shade-title");
	gtk_box_append(GTK_BOX(header), back);
	gtk_box_append(GTK_BOX(header), heading);
	gtk_box_append(GTK_BOX(header), toggle);
	gtk_box_append(GTK_BOX(root), header);
	gtk_label_set_xalign(GTK_LABEL(status), 0);
	gtk_widget_add_css_class(status, "shade-subtitle");
	app->bluetooth_status = GTK_LABEL(status);
	gtk_box_append(GTK_BOX(root), status);
	gtk_widget_set_hexpand(section, TRUE);
	gtk_label_set_xalign(GTK_LABEL(section), 0);
	gtk_widget_add_css_class(section, "shade-section");
	gtk_box_append(GTK_BOX(section_row), section);
	gtk_box_append(GTK_BOX(section_row), scan);
	gtk_box_append(GTK_BOX(root), section_row);
	app->bluetooth_list = GTK_BOX(
	    gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
	    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_kinetic_scrolling(
	    GTK_SCROLLED_WINDOW(scroller), TRUE);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller),
	    GTK_WIDGET(app->bluetooth_list));
	gtk_widget_set_vexpand(scroller, TRUE);
	gtk_box_append(GTK_BOX(root), scroller);
	return root;
}

#if 0
static GtkWidget *
build_home(struct app *app)
{
	GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	GtkWidget *back = make_button("Back", "shade-action",
	    G_CALLBACK(action_settings_page), app);
	GtkWidget *heading = gtk_label_new("Notifications");
	GtkWidget *clear = make_button("Clear all", "shade-action",
	    G_CALLBACK(action_clear_notifications), app);
	GtkWidget *compact = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	GtkWidget *wifi = make_button("Wi-Fi", "shade-chip",
	    G_CALLBACK(action_wifi_page), app);
	GtkWidget *sound = make_button("Sound", "shade-chip",
	    G_CALLBACK(action_sound), app);
	GtkWidget *bluetooth = make_button("Bluetooth", "shade-chip",
	    G_CALLBACK(action_bluetooth_page), app);
	GtkWidget *controls = make_button("Controls", "shade-chip",
	    G_CALLBACK(action_settings_page), app);
	GtkWidget *subtitle = gtk_label_new(
	    "Tap to open  /  Swipe sideways to dismiss");
	GtkWidget *scroller = gtk_scrolled_window_new();

	gtk_widget_add_css_class(root, "shade-page");
	gtk_widget_add_css_class(header, "shade-header");
	gtk_widget_set_hexpand(heading, TRUE);
	app->notification_clear = clear;
	gtk_label_set_xalign(GTK_LABEL(heading), 0);
	gtk_widget_add_css_class(heading, "shade-title");
	gtk_box_append(GTK_BOX(header), back);
	gtk_box_append(GTK_BOX(header), heading);
	gtk_box_append(GTK_BOX(header), clear);
	gtk_box_append(GTK_BOX(root), header);
	for (GtkWidget *item = wifi; item != NULL;) {
		gtk_widget_set_hexpand(item, TRUE);
		if (item == wifi)
			item = sound;
		else if (item == sound)
			item = bluetooth;
		else if (item == bluetooth)
			item = controls;
		else
			item = NULL;
	}
	gtk_box_append(GTK_BOX(compact), wifi);
	gtk_box_append(GTK_BOX(compact), sound);
	gtk_box_append(GTK_BOX(compact), bluetooth);
	gtk_box_append(GTK_BOX(compact), controls);
	gtk_box_append(GTK_BOX(root), compact);
	gtk_label_set_xalign(GTK_LABEL(subtitle), 0);
	gtk_widget_add_css_class(subtitle, "shade-subtitle");
	gtk_box_append(GTK_BOX(root), subtitle);
	app->notification_list = GTK_BOX(
	    gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
	    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_kinetic_scrolling(
	    GTK_SCROLLED_WINDOW(scroller), TRUE);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller),
	    GTK_WIDGET(app->notification_list));
	gtk_widget_set_vexpand(scroller, TRUE);
	gtk_box_append(GTK_BOX(root), scroller);
	return root;
}
#endif

static GtkWidget *
build_wifi(struct app *app)
{
	GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	GtkWidget *back = make_button("Back", "shade-action",
	    G_CALLBACK(action_home), app);
	GtkWidget *heading = gtk_label_new("Wi-Fi");
	GtkWidget *toggle = make_button("On / Off", "shade-action",
	    G_CALLBACK(action_wifi_toggle), app);
	GtkWidget *status = gtk_label_new("Loading...");
	GtkWidget *section = gtk_label_new("Available networks");
	GtkWidget *scan = make_button("Scan", "shade-action",
	    G_CALLBACK(action_scan), app);
	GtkWidget *hidden = make_button("Hidden network", "shade-action",
	    G_CALLBACK(show_hidden_network), app);
	GtkWidget *section_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	GtkWidget *scroller = gtk_scrolled_window_new();

	gtk_widget_add_css_class(root, "shade-page");
	gtk_widget_add_css_class(header, "shade-header");
	gtk_widget_set_hexpand(heading, TRUE);
	gtk_label_set_xalign(GTK_LABEL(heading), 0);
	gtk_widget_add_css_class(heading, "shade-title");
	gtk_box_append(GTK_BOX(header), back);
	gtk_box_append(GTK_BOX(header), heading);
	gtk_box_append(GTK_BOX(header), toggle);
	gtk_box_append(GTK_BOX(root), header);
	gtk_label_set_xalign(GTK_LABEL(status), 0);
	gtk_widget_add_css_class(status, "shade-subtitle");
	app->wifi_status = GTK_LABEL(status);
	gtk_box_append(GTK_BOX(root), status);

	gtk_widget_set_hexpand(section, TRUE);
	gtk_label_set_xalign(GTK_LABEL(section), 0);
	gtk_widget_add_css_class(section, "shade-section");
	gtk_box_append(GTK_BOX(section_row), section);
	gtk_box_append(GTK_BOX(section_row), hidden);
	gtk_box_append(GTK_BOX(section_row), scan);
	gtk_box_append(GTK_BOX(root), section_row);

	app->network_list = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
	    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller),
	    GTK_WIDGET(app->network_list));
	gtk_widget_set_vexpand(scroller, TRUE);
	gtk_box_append(GTK_BOX(root), scroller);
	return root;
}

static gboolean
socket_ready(gint fd, GIOCondition condition, gpointer data)
{
	struct app *app = data;
	char message[CONTROL_MESSAGE_SIZE];
	ssize_t size;

	if ((condition & G_IO_IN) == 0)
		return G_SOURCE_CONTINUE;
	while ((size = recv(fd, message, sizeof(message) - 1,
	    MSG_DONTWAIT)) > 0) {
		message[size] = '\0';
		if (getenv("CTLST_SHADE_DEBUG") != NULL)
			fprintf(stderr, "shade received=%s size=%zd\n",
			    message, size);
		if (message[0] == 'P' &&
		    (message[1] == '.' ||
		    (message[1] >= '0' && message[1] <= '9')))
			preview_shade_progress(app,
			    g_ascii_strtod(message + 1, NULL));
		else if (message[0] == 'S')
			show_shade(app, false);
		else if (message[0] == 'W')
			show_shade(app, true);
		else if (message[0] == 'B') {
			show_bluetooth(app);
			present_shade_page(app);
		} else if (message[0] == 'D') {
			show_shade(app, true);
			show_current_wifi_details(app);
		} else if (message[0] == 'P') {
			show_bluetooth(app);
			present_shade_page(app);
			show_first_bluetooth_device(app);
		} else if (message[0] == 'N') {
			show_notifications(app);
			present_shade_page(app);
		} else if (message[0] == 'R') {
			refresh_notifications(app);
		} else if (message[0] == 'H')
			hide_shade(app);
		else if (message[0] == 'T')
			load_theme(app);
	}
	return G_SOURCE_CONTINUE;
}

static bool
create_socket(struct app *app)
{
	struct sockaddr_un address = {0};

	app->control_fd = socket(AF_UNIX,
	    SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
	if (app->control_fd < 0)
		return false;
	address.sun_family = AF_UNIX;
	snprintf(address.sun_path, sizeof(address.sun_path), "%s",
	    app->control_path);
	unlink(app->control_path);
	if (bind(app->control_fd, (struct sockaddr *)&address,
	    sizeof(address)) < 0)
		return false;
	chmod(app->control_path, 0600);
	app->control_source = g_unix_fd_add(app->control_fd,
	    G_IO_IN, socket_ready, app);
	return true;
}

static void
activate(GtkApplication *application, gpointer data)
{
	struct app *app = data;
	GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	GtkWidget *handle = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	GtkGesture *drag = gtk_gesture_drag_new();
	GtkGesture *handle_click = gtk_gesture_click_new();
	GtkEventController *keys = gtk_event_controller_key_new();
	const char *runtime = g_get_user_runtime_dir();

	app->application = application;
	app->gsk_motion = g_strcmp0(g_getenv("CTLST_SHADE_MOTION"), "gsk") == 0;
	snprintf(app->runtime_dir, sizeof(app->runtime_dir), "%s", runtime);
	snprintf(app->control_path, sizeof(app->control_path),
	    "%s/ctlst-shade.sock", runtime);
	snprintf(app->gesture_path, sizeof(app->gesture_path),
	    "%s/ctlst-gestures.sock", runtime);
	snprintf(app->visible_path, sizeof(app->visible_path),
	    "%s/ctlstshade.visible", runtime);
	unlink(app->visible_path);
	if (getenv("CTLST_SHADE_DEBUG") != NULL)
		fprintf(stderr, "shade socket=%s marker=%s\n", app->control_path,
		    app->visible_path);

	app->window = GTK_WINDOW(gtk_application_window_new(application));
	gtk_widget_set_name(GTK_WIDGET(app->window), "ctlstshade");
	if (app->gsk_motion)
		gtk_widget_add_css_class(GTK_WIDGET(app->window), "gsk-motion");
	gtk_window_set_title(app->window, "ctlstshade");
	gtk_window_set_decorated(app->window, FALSE);
	gtk_layer_init_for_window(app->window);
	gtk_layer_set_layer(app->window, GTK_LAYER_SHELL_LAYER_OVERLAY);
	gtk_layer_set_namespace(app->window, "ctlstshade");
	gtk_layer_set_anchor(app->window, GTK_LAYER_SHELL_EDGE_TOP, TRUE);
	gtk_layer_set_anchor(app->window, GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
	gtk_layer_set_anchor(app->window, GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
	gtk_layer_set_anchor(app->window, GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
	gtk_layer_set_margin(app->window, GTK_LAYER_SHELL_EDGE_BOTTOM,
	    SHADE_BOTTOM_MARGIN);
	gtk_layer_set_exclusive_zone(app->window, -1);
	gtk_layer_set_keyboard_mode(app->window,
	    GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
	/* Start fully collapsed too, so the first progressive pull is top-down. */
	apply_shade_margin(app, 0.0);

	app->css = gtk_css_provider_new();
	load_theme(app);
	gtk_style_context_add_provider_for_display(gdk_display_get_default(),
	    GTK_STYLE_PROVIDER(app->css), GTK_STYLE_PROVIDER_PRIORITY_USER);

	gtk_widget_add_css_class(root, "shade-root");
	gtk_widget_add_css_class(handle, "shade-handle");
	gtk_widget_set_halign(handle, GTK_ALIGN_CENTER);
	gtk_widget_add_controller(handle,
	    GTK_EVENT_CONTROLLER(handle_click));
	g_signal_connect(handle_click, "released",
	    G_CALLBACK(handle_clicked), app);
	gtk_widget_add_controller(GTK_WIDGET(app->window), keys);
	g_signal_connect(keys, "key-pressed", G_CALLBACK(key_pressed), app);
	gtk_box_append(GTK_BOX(root), handle);
	app->stack = GTK_STACK(gtk_stack_new());
	gtk_stack_set_transition_type(app->stack,
	    GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
	gtk_stack_add_named(app->stack, build_quick_settings(app), "settings");
	gtk_stack_add_named(app->stack, build_wifi(app), "wifi");
	gtk_stack_add_named(app->stack, build_bluetooth(app), "bluetooth");
	gtk_stack_add_named(app->stack, build_sound(app), "sound");
	refresh_notifications(app);
	gtk_widget_set_vexpand(GTK_WIDGET(app->stack), TRUE);
	gtk_box_append(GTK_BOX(root), GTK_WIDGET(app->stack));
	if (app->gsk_motion) {
		app->panel = ctlst_motion_panel_new(root);
		gtk_window_set_child(app->window, app->panel);
	} else {
		app->panel = root;
		gtk_window_set_child(app->window, root);
	}

	g_signal_connect(drag, "drag-begin", G_CALLBACK(drag_begin), app);
	g_signal_connect(drag, "drag-update", G_CALLBACK(drag_update), app);
	g_signal_connect(drag, "drag-end", G_CALLBACK(drag_end), app);
	gtk_gesture_single_set_exclusive(GTK_GESTURE_SINGLE(drag), FALSE);
	gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(drag),
	    GTK_PHASE_CAPTURE);
	gtk_widget_add_controller(root, GTK_EVENT_CONTROLLER(drag));
	create_socket(app);
	gtk_widget_set_visible(GTK_WIDGET(app->window), FALSE);
	g_application_hold(G_APPLICATION(application));
}

static void
shutdown_app(GApplication *application, gpointer data)
{
	struct app *app = data;

	(void)application;
	if (app->control_source != 0)
		g_source_remove(app->control_source);
	if (app->brightness_write_source != 0)
		g_source_remove(app->brightness_write_source);
	if (app->control_fd >= 0)
		close(app->control_fd);
	unlink(app->control_path);
	unlink(app->visible_path);
	set_text_input_owner(false);
}

int
main(int argc, char **argv)
{
	struct app app = {.control_fd = -1};
	GtkApplication *application = gtk_application_new(
	    "dev.ctlst.Shade", G_APPLICATION_DEFAULT_FLAGS);
	int status;

	g_signal_connect(application, "activate", G_CALLBACK(activate), &app);
	g_signal_connect(application, "shutdown", G_CALLBACK(shutdown_app), &app);
	status = g_application_run(G_APPLICATION(application), argc, argv);
	g_object_unref(application);
	return status;
}
