#define _GNU_SOURCE

#include "../ctlst-runtime.h"
#include <cairo.h>
#include <cairo/cairo.h>
#include <errno.h>
#include <fcntl.h>
#include <gio/gdesktopappinfo.h>
#include <gio/gio.h>
#include <glib-unix.h>
#include <gtk/gtk.h>
#include <gtk4-layer-shell.h>
#include <json-c/json.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/time.h>
#include <unistd.h>

#define MAX_TASKS 64
#define MAX_ICONS 8
#define MAX_VISIBLE_TASKS 3
#define MAX_TEXT 128
#define MAX_REPLY 65536
#define POLL_MS 50
#define REVEAL_FRAME_MS 16
#define COLLAPSE_DELAY_MS 420
#define DOCK_SURFACE_WIDTH 184
#define DOCK_SURFACE_HEIGHT 56
#define DOCK_DASH_WIDTH 72
#define DOCK_DASH_HEIGHT 6
#define DOCK_POINTER_TARGET_WIDTH 96
#define DOCK_POINTER_TARGET_HEIGHT 24
#define DOCK_SCROLL_DETENT 0.8
#define DOCK_SCROLL_RATE_LIMIT_US 90000

struct color {
	double red;
	double green;
	double blue;
	double alpha;
};

struct theme {
	struct color panel;
	struct color line;
	struct color accent;
	struct color accent_soft;
	struct color danger;
	struct color text;
	struct color selected_text;
};

struct task {
	char workspace[MAX_TEXT];
	char layout[16];
	char icons[MAX_ICONS][MAX_TEXT];
	unsigned int icon_count;
	unsigned int window_count;
	bool focused;
	bool urgent;
};

struct dock {
	GtkApplication *application;
	GtkWindow *window;
	GtkOverlay *overlay;
	GtkWidget *surface;
	GtkBox *root;
	GtkCssProvider *css;
	struct theme theme;
	char home[MAX_TEXT];
	char focused[MAX_TEXT];
	struct task task_data[MAX_TASKS];
	unsigned int task_count;
	unsigned int focused_index;
	uint64_t snapshot_hash;
	bool camera_focused;
	bool shell_visible;
	double reveal;
	double reveal_target;
	guint reveal_source;
	guint collapse_source;
	double scroll_accumulator;
	gint64 last_scroll_us;
	int control_fd;
	guint control_source;
	char control_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
};

static const struct theme default_theme = {
	.panel = {0.090, 0.153, 0.141, 0.96},
	.line = {0.373, 0.443, 0.420, 0.55},
	.accent = {0.396, 0.718, 0.655, 1.0},
	.accent_soft = {0.141, 0.286, 0.255, 1.0},
	.danger = {0.714, 0.290, 0.216, 1.0},
	.text = {0.957, 0.929, 0.890, 1.0},
	.selected_text = {0.067, 0.090, 0.075, 1.0},
};

static void
color_from_hex(struct color *color, const char *value)
{
	char *end = NULL;
	unsigned long parsed;
	size_t length;

	if (value == NULL)
		return;
	while (*value == '#' || g_ascii_isspace(*value))
		value++;
	length = strlen(value);
	if (length != 6 && length != 8)
		return;
	parsed = strtoul(value, &end, 16);
	if (end == NULL || *end != '\0')
		return;
	color->red = ((parsed >> (length == 8 ? 24 : 16)) & 0xff) / 255.0;
	color->green = ((parsed >> (length == 8 ? 16 : 8)) & 0xff) / 255.0;
	color->blue = ((parsed >> (length == 8 ? 8 : 0)) & 0xff) / 255.0;
	color->alpha = length == 8 ? (parsed & 0xff) / 255.0 : 1.0;
}

static void
color_css(const struct color *color, char *buffer, size_t length)
{
	g_snprintf(buffer, length, "rgba(%.0f, %.0f, %.0f, %.3f)",
	    color->red * 255.0, color->green * 255.0, color->blue * 255.0,
	    color->alpha);
}

static void
load_theme(struct theme *theme)
{
	char *path;
	char *contents = NULL;
	char **lines;
	const char *keys[] = {
		"SHELL_PANEL", "SHELL_LINE", "SHELL_ACCENT", "SHELL_ACCENT_SOFT",
		"SHELL_DANGER", "SHELL_TEXT", "SHELL_SELECTED_TEXT",
	};
	struct color *colors[] = {
		&theme->panel, &theme->line, &theme->accent, &theme->accent_soft,
		&theme->danger, &theme->text, &theme->selected_text,
	};
	gsize length;

	*theme = default_theme;
	path = ctlst_config_path("theme.env");
	(void)g_file_get_contents(path, &contents, &length, NULL);
	g_free(path);
	if (contents == NULL)
		return;
	lines = g_strsplit(contents, "\n", -1);
	for (gsize i = 0; lines[i] != NULL; i++) {
		char **pair = g_strsplit(lines[i], "=", 2);

		if (pair[0] == NULL || pair[1] == NULL) {
			g_strfreev(pair);
			continue;
		}
		g_strstrip(pair[0]);
		g_strstrip(pair[1]);
		for (size_t key = 0; key < G_N_ELEMENTS(keys); key++) {
			if (strcmp(pair[0], keys[key]) != 0)
				continue;
			color_from_hex(colors[key], pair[1]);
			break;
		}
		g_strfreev(pair);
	}
	g_strfreev(lines);
	g_free(contents);
}

static void
install_css(struct dock *dock)
{
	char panel[64], line[64], accent[64], accent_soft[64], danger[64], text[64];
	char selected_text[64];
	char *css;

	color_css(&dock->theme.panel, panel, sizeof(panel));
	color_css(&dock->theme.line, line, sizeof(line));
	color_css(&dock->theme.accent, accent, sizeof(accent));
	color_css(&dock->theme.accent_soft, accent_soft, sizeof(accent_soft));
	color_css(&dock->theme.danger, danger, sizeof(danger));
	color_css(&dock->theme.text, text, sizeof(text));
	color_css(&dock->theme.selected_text, selected_text, sizeof(selected_text));
	if (dock->css != NULL) {
		gtk_style_context_remove_provider_for_display(gdk_display_get_default(),
		    GTK_STYLE_PROVIDER(dock->css));
		g_clear_object(&dock->css);
	}
	dock->css = gtk_css_provider_new();
	css = g_strdup_printf(
	    "window#ctlstdock { background: transparent; }"
	    ".dock-root { background: transparent; padding: 6px 10px; }"
	    ".dock-task, .dock-home { min-width: 44px; min-height: 44px; padding: 0; border-radius: 22px; background: transparent; color: %s; border: 0; box-shadow: none; }"
	    ".dock-task:hover, .dock-home:hover { background: %s; }"
	    ".dock-task-focused, .dock-home-focused { background: %s; color: %s; box-shadow: none; }"
	    ".dock-task-urgent { background: %s; color: %s; }"
	    ".dock-count { min-width: 17px; min-height: 17px; padding: 0 3px; border-radius: 9px; background: %s; color: %s; font-size: 11px; font-weight: 700; }",
	    text, accent_soft, accent_soft, text,
	    danger, selected_text, accent, selected_text);
	gtk_css_provider_load_from_string(dock->css, css);
	gtk_style_context_add_provider_for_display(gdk_display_get_default(),
	    GTK_STYLE_PROVIDER(dock->css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_free(css);
}

static double
clamp_unit(double value)
{
	return MAX(0.0, MIN(1.0, value));
}

/* Keep a small task set compact without shrinking the collapsed pointer lane.
 * GTK measurement includes the native controls and the root's CSS padding. */
static int
dock_pill_width(struct dock *dock)
{
	int minimum = 0, natural = 0;

	if (dock->task_count >= 2)
		return DOCK_SURFACE_WIDTH;
	gtk_widget_measure(GTK_WIDGET(dock->root), GTK_ORIENTATION_HORIZONTAL, -1,
	    &minimum, &natural, NULL, NULL);
	return CLAMP(natural, 96, DOCK_SURFACE_WIDTH);
}

static cairo_rectangle_int_t
dock_expanded_target(struct dock *dock, int width, int height)
{
	const int pill_width = MIN(width, dock_pill_width(dock));
	return (cairo_rectangle_int_t){
		.x = (width - pill_width) / 2,
		.y = MAX(0, height - DOCK_SURFACE_HEIGHT),
		.width = pill_width,
		.height = MIN(height, DOCK_SURFACE_HEIGHT),
	};
}

static void
draw_surface(GtkWidget *widget, GtkSnapshot *snapshot, struct dock *dock)
{
	int width = gtk_widget_get_width(widget), height = gtk_widget_get_height(widget);
	double progress = clamp_unit(dock->reveal);
	double eased = progress;
	double shell_width = DOCK_DASH_WIDTH +
	    (dock_pill_width(dock) - DOCK_DASH_WIDTH) * eased;
	double shell_height = DOCK_DASH_HEIGHT +
	    (DOCK_SURFACE_HEIGHT - DOCK_DASH_HEIGHT) * eased;
	double x = (width - shell_width) / 2.0;
	double y = height - shell_height;
	double radius = shell_height / 2.0;
	double panel_mix = pow(progress, 0.72);
	struct color fill = {
		.red = dock->theme.accent.red * (1.0 - panel_mix) +
		    dock->theme.panel.red * panel_mix,
		.green = dock->theme.accent.green * (1.0 - panel_mix) +
		    dock->theme.panel.green * panel_mix,
		.blue = dock->theme.accent.blue * (1.0 - panel_mix) +
		    dock->theme.panel.blue * panel_mix,
		.alpha = dock->theme.accent.alpha * (1.0 - panel_mix) +
		    dock->theme.panel.alpha * panel_mix,
	};

	graphene_rect_t bounds = GRAPHENE_RECT_INIT(x, y, shell_width, shell_height);
	GskRoundedRect outline;
	gsk_rounded_rect_init_from_rect(&outline, &bounds, radius);
	GdkRGBA ink = {fill.red, fill.green, fill.blue, fill.alpha};
	gtk_snapshot_push_rounded_clip(snapshot, &outline);
	gtk_snapshot_append_color(snapshot, &ink, &bounds);
	gtk_snapshot_pop(snapshot);
	GdkRGBA line = {dock->theme.line.red, dock->theme.line.green,
	    dock->theme.line.blue, dock->theme.line.alpha * progress};
	GdkRGBA edges[] = {line, line, line, line};
	float widths[] = {progress, progress, progress, progress};
	gtk_snapshot_append_border(snapshot, &outline, widths, edges);
}

typedef struct { GtkWidget parent_instance; struct dock *dock; } DockSurface;
typedef GtkWidgetClass DockSurfaceClass;
G_DEFINE_TYPE(DockSurface, dock_surface, GTK_TYPE_WIDGET)

static void dock_surface_snapshot(GtkWidget *widget, GtkSnapshot *snapshot)
{
	draw_surface(widget, snapshot, ((DockSurface *)widget)->dock);
}

static void dock_surface_measure(GtkWidget *widget, GtkOrientation orientation,
    int for_size, int *minimum, int *natural, int *min_baseline, int *nat_baseline)
{
	(void)widget;
	(void)for_size;
	*minimum = *natural = orientation == GTK_ORIENTATION_HORIZONTAL ?
	    DOCK_SURFACE_WIDTH : DOCK_SURFACE_HEIGHT;
	*min_baseline = *nat_baseline = -1;
}

static void dock_surface_class_init(DockSurfaceClass *klass)
{
	GTK_WIDGET_CLASS(klass)->snapshot = dock_surface_snapshot;
	GTK_WIDGET_CLASS(klass)->measure = dock_surface_measure;
}

static void dock_surface_init(DockSurface *surface) { (void)surface; }

static void
update_dock_input_region(struct dock *dock)
{
	GtkNative *native;
	GdkSurface *surface;
	cairo_region_t *region;

	if (dock->window == NULL)
		return;
	native = gtk_widget_get_native(GTK_WIDGET(dock->window));
	if (native == NULL)
		return;
	surface = gtk_native_get_surface(native);
	if (surface == NULL)
		return;

	/*
	 * Keep collapsed ownership bounded to a small accessible target around the
	 * painted dash. This lets a pointer wheel switch tasks without claiming the
	 * full 184x56 layer surface; ctlst-gestured still owns touch navigation.
	 */
	if (!dock->shell_visible) {
		region = cairo_region_create();
		gdk_surface_set_input_region(surface, region);
		cairo_region_destroy(region);
		return;
	}
	if (dock->reveal < 0.82) {
		const int width = gdk_surface_get_width(surface);
		const int height = gdk_surface_get_height(surface);
		const cairo_rectangle_int_t target = {
			.x = (width - DOCK_POINTER_TARGET_WIDTH) / 2,
			.y = height - DOCK_POINTER_TARGET_HEIGHT,
			.width = DOCK_POINTER_TARGET_WIDTH,
			.height = DOCK_POINTER_TARGET_HEIGHT,
		};

		region = cairo_region_create_rectangle(&target);
		gdk_surface_set_input_region(surface, region);
		cairo_region_destroy(region);
		return;
	}
	const cairo_rectangle_int_t target = dock_expanded_target(dock,
	    gdk_surface_get_width(surface), gdk_surface_get_height(surface));
	region = cairo_region_create_rectangle(&target);
	gdk_surface_set_input_region(surface, region);
	cairo_region_destroy(region);
}

static void
apply_reveal(struct dock *dock, double reveal)
{
	double icon_opacity;

	dock->reveal = clamp_unit(reveal);
	icon_opacity = clamp_unit((dock->reveal - 0.68) / 0.32);
	gtk_widget_set_opacity(GTK_WIDGET(dock->root), icon_opacity);
	gtk_widget_set_can_target(GTK_WIDGET(dock->root),
	    dock->shell_visible && dock->reveal >= 0.82);
	gtk_widget_set_can_target(GTK_WIDGET(dock->window),
	    dock->shell_visible);
	gtk_widget_set_can_target(GTK_WIDGET(dock->surface),
	    dock->shell_visible);
	update_dock_input_region(dock);
	gtk_widget_queue_draw(GTK_WIDGET(dock->surface));
}

static gboolean
animate_reveal(gpointer data)
{
	struct dock *dock = data;
	double delta = dock->reveal_target - dock->reveal;

	if (fabs(delta) <= 0.008) {
		apply_reveal(dock, dock->reveal_target);
		dock->reveal_source = 0;
		return G_SOURCE_REMOVE;
	}
	apply_reveal(dock, dock->reveal + delta * 0.28);
	return G_SOURCE_CONTINUE;
}

static void
set_reveal_target(struct dock *dock, double target)
{
	dock->reveal_target = clamp_unit(target);
	if (dock->collapse_source != 0 && dock->reveal_target > 0.0) {
		g_source_remove(dock->collapse_source);
		dock->collapse_source = 0;
	}
	if (dock->reveal_source == 0)
		dock->reveal_source = g_timeout_add(REVEAL_FRAME_MS,
		    animate_reveal, dock);
}

static gboolean
collapse_dock(gpointer data)
{
	struct dock *dock = data;

	dock->collapse_source = 0;
	set_reveal_target(dock, 0.0);
	return G_SOURCE_REMOVE;
}

static void
schedule_collapse(struct dock *dock)
{
	if (dock->collapse_source != 0)
		g_source_remove(dock->collapse_source);
	dock->collapse_source = g_timeout_add(COLLAPSE_DELAY_MS,
	    collapse_dock, dock);
}

static gboolean
control_ready(gint fd, GIOCondition condition, gpointer data)
{
	struct dock *dock = data;
	char message[64];
	ssize_t length;

	if ((condition & (G_IO_ERR | G_IO_HUP | G_IO_NVAL)) != 0)
		return G_SOURCE_CONTINUE;
	length = recv(fd, message, sizeof(message) - 1, 0);
	if (length <= 0)
		return G_SOURCE_CONTINUE;
	message[length] = '\0';
	if (message[0] == 'S') {
		/* Compatibility soft morph; hold inflate prefers P 0..1000. */
		set_reveal_target(dock, 0.12);
	} else if (message[0] == 'P') {
		char *end = NULL;
		long value = strtol(message + 1, &end, 10);

		if (end != message + 1)
			set_reveal_target(dock, clamp_unit(value / 1000.0));
	} else if (message[0] == 'E') {
		set_reveal_target(dock, 1.0);
	} else if (message[0] == 'H') {
		schedule_collapse(dock);
	} else if (message[0] == 'T') {
		load_theme(&dock->theme);
		install_css(dock);
		gtk_widget_queue_draw(GTK_WIDGET(dock->surface));
	}
	return G_SOURCE_CONTINUE;
}

static bool
create_control_socket(struct dock *dock)
{
	struct sockaddr_un address = {0};

	if (g_snprintf(dock->control_path, sizeof(dock->control_path),
	    "%s/ctlst-dock.sock", g_get_user_runtime_dir()) >=
	    (int)sizeof(dock->control_path))
		return false;
	dock->control_fd = socket(AF_UNIX,
	    SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
	if (dock->control_fd < 0)
		return false;
	address.sun_family = AF_UNIX;
	strcpy(address.sun_path, dock->control_path);
	unlink(dock->control_path);
	if (bind(dock->control_fd, (struct sockaddr *)&address,
	    sizeof(address)) < 0) {
		close(dock->control_fd);
		dock->control_fd = -1;
		return false;
	}
	chmod(dock->control_path, 0600);
	dock->control_source = g_unix_fd_add(dock->control_fd,
	    G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL, control_ready, dock);
	return true;
}

static void
draw_fallback(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data)
{
	const char *name = data != NULL ? data : "";
	char *lower_name = g_ascii_strdown(name, -1);
	double center_x = width / 2.0;
	double center_y = height / 2.0;
	double radius = MIN(width, height) * 0.43;

	(void)area;
	cairo_set_source_rgb(cr, 0.88, 0.93, 0.91);
	cairo_arc(cr, center_x, center_y, radius, 0, 2 * G_PI);
	cairo_fill(cr);
	cairo_set_source_rgb(cr, 0.09, 0.41, 0.37);
	cairo_set_line_width(cr, MAX(2.0, width / 12.0));
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	if (g_strrstr(lower_name, "home") != NULL) {
		cairo_move_to(cr, width * 0.24, height * 0.48);
		cairo_line_to(cr, center_x, height * 0.27);
		cairo_line_to(cr, width * 0.76, height * 0.48);
		cairo_move_to(cr, width * 0.32, height * 0.44);
		cairo_line_to(cr, width * 0.32, height * 0.72);
		cairo_line_to(cr, width * 0.68, height * 0.72);
		cairo_line_to(cr, width * 0.68, height * 0.44);
	} else if (g_strrstr(lower_name, "term") != NULL ||
	    g_strrstr(lower_name, "foot") != NULL) {
		cairo_move_to(cr, width * 0.30, height * 0.42);
		cairo_line_to(cr, width * 0.45, center_y);
		cairo_line_to(cr, width * 0.30, height * 0.58);
		cairo_move_to(cr, width * 0.52, height * 0.60);
		cairo_line_to(cr, width * 0.70, height * 0.60);
	} else if (g_strrstr(lower_name, "browser") != NULL ||
	    g_strrstr(lower_name, "firefox") != NULL) {
		cairo_arc(cr, center_x, center_y, radius * 0.52, 0, 2 * G_PI);
		cairo_move_to(cr, center_x, center_y - radius * 0.52);
		cairo_line_to(cr, center_x, center_y + radius * 0.52);
		cairo_move_to(cr, center_x - radius * 0.52, center_y);
		cairo_line_to(cr, center_x + radius * 0.52, center_y);
	} else if (width < 24 || height < 24) {
		cairo_arc(cr, center_x, center_y, radius * 0.22, 0, 2 * G_PI);
		cairo_fill(cr);
	} else {
		cairo_rectangle(cr, width * 0.28, height * 0.28, width * 0.18,
		    height * 0.18);
		cairo_rectangle(cr, width * 0.54, height * 0.28, width * 0.18,
		    height * 0.18);
		cairo_rectangle(cr, width * 0.28, height * 0.54, width * 0.18,
		    height * 0.18);
		cairo_rectangle(cr, width * 0.54, height * 0.54, width * 0.18,
		    height * 0.18);
	}
	cairo_stroke(cr);
	g_free(lower_name);
}

static bool
app_identity_matches(const char *app_id, const char *candidate)
{
	char *normalized;
	bool matches;

	if (candidate == NULL || candidate[0] == '\0')
		return false;
	normalized = g_strdup(candidate);
	if (g_str_has_suffix(normalized, ".desktop"))
		normalized[strlen(normalized) - strlen(".desktop")] = '\0';
	matches = g_ascii_strcasecmp(app_id, normalized) == 0;
	g_free(normalized);
	return matches;
}

static GDesktopAppInfo *
desktop_for_app_id(const char *app_id)
{
	static GHashTable *resolved;
	char *desktop_id = g_str_has_suffix(app_id, ".desktop") ?
	    g_strdup(app_id) : g_strdup_printf("%s.desktop", app_id);
	GDesktopAppInfo *desktop = g_desktop_app_info_new(desktop_id);
	GList *applications;
	const char *cached;

	g_free(desktop_id);
	if (desktop != NULL)
		return desktop;
	if (resolved == NULL)
		resolved = g_hash_table_new_full(g_str_hash, g_str_equal,
		    g_free, g_free);
	cached = g_hash_table_lookup(resolved, app_id);
	if (cached != NULL)
		return cached[0] != '\0' ? g_desktop_app_info_new(cached) : NULL;
	applications = g_app_info_get_all();
	for (GList *item = applications; item != NULL; item = item->next) {
		GAppInfo *info = G_APP_INFO(item->data);
		const char *startup_class;

		if (!G_IS_DESKTOP_APP_INFO(info))
			continue;
		startup_class = g_desktop_app_info_get_startup_wm_class(
		    G_DESKTOP_APP_INFO(info));
		if (!app_identity_matches(app_id, g_app_info_get_id(info)) &&
		    !app_identity_matches(app_id, startup_class))
			continue;
		desktop = g_object_ref(G_DESKTOP_APP_INFO(info));
		g_hash_table_insert(resolved, g_strdup(app_id),
		    g_strdup(g_app_info_get_id(info)));
		break;
	}
	g_list_free_full(applications, g_object_unref);
	if (desktop == NULL)
		g_hash_table_insert(resolved, g_strdup(app_id), g_strdup(""));
	return desktop;
}

static void
add_flatpak_icon_paths(GtkIconTheme *theme)
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

static GtkWidget *icon_widget(const char *name, int size)
{
	GdkDisplay *display = gdk_display_get_default();
	GtkIconTheme *theme = gtk_icon_theme_get_for_display(display);
	GDesktopAppInfo *desktop = desktop_for_app_id(name);
	GIcon *icon = desktop != NULL && g_app_info_get_icon(G_APP_INFO(desktop)) != NULL ?
	    g_object_ref(g_app_info_get_icon(G_APP_INFO(desktop))) :
	    g_themed_icon_new_with_default_fallbacks(name);
	GtkIconPaintable *paintable = NULL;
	GtkWidget *widget;
	bool available = G_IS_FILE_ICON(icon) || G_IS_LOADABLE_ICON(icon);

	add_flatpak_icon_paths(theme);
	if (!available && G_IS_THEMED_ICON(icon)) {
		const char *const *names =
		    g_themed_icon_get_names(G_THEMED_ICON(icon));

		for (size_t index = 0; names[index] != NULL; index++) {
			if (gtk_icon_theme_has_icon(theme, names[index])) {
				available = true;
				break;
			}
		}
	}
	if (available)
		paintable = gtk_icon_theme_lookup_by_gicon(theme, icon, size, 1,
		    GTK_TEXT_DIR_NONE, 0);

	if (paintable != NULL) {
		widget = gtk_image_new_from_paintable(GDK_PAINTABLE(paintable));
		gtk_image_set_pixel_size(GTK_IMAGE(widget), size);
	} else {
		GtkDrawingArea *fallback = GTK_DRAWING_AREA(gtk_drawing_area_new());
		gtk_drawing_area_set_content_width(fallback, size);
		gtk_drawing_area_set_content_height(fallback, size);
		gtk_drawing_area_set_draw_func(fallback, draw_fallback, g_strdup(name), g_free);
		widget = GTK_WIDGET(fallback);
	}
	g_clear_object(&paintable);
	g_object_unref(icon);
	g_clear_object(&desktop);
	gtk_widget_set_size_request(widget, size, size);
	return widget;
}

static GtkWidget *icon_group(const struct task *task)
{
	GtkWidget *container;
	unsigned int count = task->window_count;

	if (count <= 1) {
		const char *name = task->icon_count > 0 ? task->icons[0] : "application-x-executable";
		return icon_widget(name, 30);
	}
	if (count == 2) {
		GtkOrientation orientation = strcmp(task->layout, "splitv") == 0
		    ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL;
		GtkWidget *box = gtk_box_new(orientation, 2);
		for (unsigned int i = 0; i < 2; i++) {
			const char *name = task->icon_count > i ? task->icons[i] : task->icons[0];
			GtkWidget *icon = icon_widget(name, 19);
			gtk_box_append(GTK_BOX(box), icon);
		}
		return box;
	}

	container = gtk_overlay_new();
	gtk_widget_set_size_request(container, 34, 34);
	/* One recognizable identity plus a count beats a stack of tiny icons. */
	gtk_overlay_set_child(GTK_OVERLAY(container), icon_widget(
	    task->icon_count ? task->icons[0] : "application-x-executable", 30));
	{
		char count[16];
		GtkWidget *badge = gtk_label_new(NULL);
		g_snprintf(count, sizeof(count), "%u", task->window_count);
		gtk_label_set_text(GTK_LABEL(badge), count);
		gtk_widget_add_css_class(badge, "dock-count");
		gtk_widget_set_halign(badge, GTK_ALIGN_END);
		gtk_widget_set_valign(badge, GTK_ALIGN_START);
		gtk_overlay_add_overlay(GTK_OVERLAY(container), badge);
	}
	return container;
}

static void
send_command(struct dock *dock, const char *command, const char *workspace)
{
	const char *runtime = g_get_user_runtime_dir();
	char *path = g_build_filename(runtime, "ctlst-workspaces.sock", NULL);
	char payload[256];
	struct sockaddr_un address = {0};
	int fd;
	int written;

	if (workspace == NULL)
		written = g_snprintf(payload, sizeof(payload), "{\"command\":\"%s\"}\n", command);
	else
		written = g_snprintf(payload, sizeof(payload),
		    "{\"command\":\"%s\",\"workspace\":\"%s\"}\n", command,
		    workspace);
	if (written < 0 || (size_t)written >= sizeof(payload)) {
		g_free(path);
		return;
	}
	fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0)
		goto out;
	address.sun_family = AF_UNIX;
	if (g_strlcpy(address.sun_path, path, sizeof(address.sun_path)) >=
	    sizeof(address.sun_path))
		goto close_fd;
	if (connect(fd, (struct sockaddr *)&address, sizeof(address)) == 0) {
		ssize_t sent = send(fd, payload, (size_t)written, MSG_NOSIGNAL);

		if (sent != (ssize_t)written)
			goto close_fd;
	}
close_fd:
	close(fd);
out:
	g_free(path);
	(void)dock;
}

static void home_clicked(GtkButton *button, gpointer data)
{
	struct dock *dock = data;

	(void)button;
	send_command(dock, "home", NULL);
	schedule_collapse(dock);
}

static void task_clicked(GtkButton *button, gpointer data)
{
	struct dock *dock = data;
	const char *workspace = g_object_get_data(G_OBJECT(button), "workspace");

	send_command(dock, "focus", workspace);
	schedule_collapse(dock);
}

static gboolean
dock_scrolled(GtkEventControllerScroll *controller, double dx, double dy,
    gpointer data)
{
	struct dock *dock = data;
	const unsigned int workspace_count = dock->task_count + 1;
	const double delta = fabs(dy) >= fabs(dx) ? dy : dx;
	const gint64 now = g_get_monotonic_time();
	int direction;
	unsigned int target;

	(void)controller;
	if (!dock->shell_visible || delta == 0.0 || workspace_count < 2)
		return GDK_EVENT_STOP;
	dock->scroll_accumulator += delta;
	const char *detent_value = g_getenv("CTLST_DOCK_SCROLL_DETENT");
	const char *rate_value = g_getenv("CTLST_DOCK_SCROLL_RATE_LIMIT_MS");
	const double detent = detent_value ? g_ascii_strtod(detent_value, NULL) : DOCK_SCROLL_DETENT;
	const gint64 rate_us = rate_value ? g_ascii_strtoll(rate_value, NULL, 10) * 1000 : DOCK_SCROLL_RATE_LIMIT_US;
	if (fabs(dock->scroll_accumulator) < detent)
		return GDK_EVENT_STOP;
	if (now - dock->last_scroll_us < rate_us)
		return GDK_EVENT_STOP;
	direction = dock->scroll_accumulator > 0.0 ? 1 : -1;
	dock->scroll_accumulator = 0.0;
	dock->last_scroll_us = now;
	target = (dock->focused_index + workspace_count + direction) %
	    workspace_count;
	dock->focused_index = target;
	if (target == 0)
		send_command(dock, "home", NULL);
	else
		send_command(dock, "focus",
		    dock->task_data[target - 1].workspace);
	set_reveal_target(dock, 1.0);
	schedule_collapse(dock);
	return GDK_EVENT_STOP;
}

static void
dock_pointer_enter(GtkEventControllerMotion *controller, double x, double y,
    gpointer data)
{
	(void)controller;
	(void)x;
	(void)y;
	struct dock *dock = data;
	if (dock->shell_visible)
		set_reveal_target(dock, 1.0);
}

static void
dock_pointer_leave(GtkEventControllerMotion *controller, gpointer data)
{
	(void)controller;
	schedule_collapse(data);
}

static void
clear_children(GtkWidget *widget)
{
	GtkWidget *child = gtk_widget_get_first_child(widget);
	while (child != NULL) {
		GtkWidget *next = gtk_widget_get_next_sibling(child);
		gtk_box_remove(GTK_BOX(widget), child);
		child = next;
	}
}

static void
rebuild(struct dock *dock)
{
	unsigned int workspace_count = dock->task_count + 1;
	unsigned int selected = 0;
	GtkWidget *button;

	clear_children(GTK_WIDGET(dock->root));
	for (unsigned int i = 0; i < dock->task_count; i++) {
		if (dock->task_data[i].focused) {
			selected = i + 1;
			break;
		}
	}
	dock->focused_index = selected;
	/* With fewer than three destinations, don't wrap the same target twice.
	 * Stable Home/app ordering keeps a focus change from moving the buttons. */
	const unsigned int visible_count = MIN(workspace_count, MAX_VISIBLE_TASKS);
	for (unsigned int slot = 0; slot < visible_count; slot++) {
		int offset = (int)slot - (int)(MAX_VISIBLE_TASKS / 2);
		unsigned int index = workspace_count < MAX_VISIBLE_TASKS ? slot :
		    (selected + workspace_count + offset) % workspace_count;
		button = gtk_button_new();

		if (index == 0) {
			gtk_button_set_child(GTK_BUTTON(button),
			    icon_widget("go-home-symbolic", 27));
			gtk_widget_add_css_class(button, "dock-home");
			gtk_widget_set_tooltip_text(button, "Home");
			gtk_accessible_update_property(GTK_ACCESSIBLE(button),
			    GTK_ACCESSIBLE_PROPERTY_LABEL, "Home", -1);
			g_signal_connect(button, "clicked", G_CALLBACK(home_clicked),
			    dock);
		} else {
			struct task *task = &dock->task_data[index - 1];

			gtk_button_set_child(GTK_BUTTON(button), icon_group(task));
			gtk_widget_add_css_class(button, "dock-task");
			GDesktopAppInfo *desktop = task->icon_count ?
			    desktop_for_app_id(task->icons[0]) : NULL;
			const char *identity = desktop ?
			    g_app_info_get_display_name(G_APP_INFO(desktop)) : "Task";
			char *raw = g_strdup_printf("%s · %u %s", identity, task->window_count,
			    task->window_count == 1 ? "window" : "windows");
			char *label = g_utf8_make_valid(raw, -1);
			gtk_widget_set_tooltip_text(button, label);
			gtk_accessible_update_property(GTK_ACCESSIBLE(button),
			    GTK_ACCESSIBLE_PROPERTY_LABEL, label, -1);
			g_free(label);
			g_free(raw);
			g_clear_object(&desktop);
			if (task->urgent)
				gtk_widget_add_css_class(button, "dock-task-urgent");
			g_object_set_data_full(G_OBJECT(button), "workspace",
			    g_strdup(task->workspace), g_free);
			g_signal_connect(button, "clicked", G_CALLBACK(task_clicked),
			    dock);
		}
		if (index == selected)
			gtk_widget_add_css_class(button,
			    index == 0 ? "dock-home-focused" : "dock-task-focused");
		gtk_box_append(dock->root, button);
	}
	update_dock_input_region(dock);
	gtk_widget_queue_draw(GTK_WIDGET(dock->surface));
}

static bool json_string(json_object *object, const char *key, char *buffer, size_t size)
{
	json_object *value;
	const char *text;
	if (!json_object_object_get_ex(object, key, &value) ||
	    !json_object_is_type(value, json_type_string))
		return false;
	text = json_object_get_string(value);
	g_strlcpy(buffer, text != NULL ? text : "", size);
	return true;
}

static unsigned int
json_uint(json_object *object, const char *key, unsigned int fallback,
    unsigned int maximum)
{
	json_object *value;
	if (!json_object_object_get_ex(object, key, &value) ||
	    !json_object_is_type(value, json_type_int))
		return fallback;
	return MIN((unsigned int)json_object_get_int64(value), maximum);
}

static bool json_boolean(json_object *object, const char *key)
{
	json_object *value;
	return json_object_object_get_ex(object, key, &value) &&
	    json_object_is_type(value, json_type_boolean) && json_object_get_boolean(value);
}

static void
parse_snapshot(struct dock *dock, const char *buffer, size_t length)
{
	json_object *root = NULL;
	json_object *value;
	json_object *tasks;
	uint64_t hash = UINT64_C(1469598103934665603);

	for (size_t i = 0; i < length; i++) {
		hash ^= (unsigned char)buffer[i];
		hash *= UINT64_C(1099511628211);
	}
	if (hash == dock->snapshot_hash)
		return;

	root = json_tokener_parse(buffer);
	if (root == NULL || !json_object_is_type(root, json_type_object))
		goto out;
	if (!json_object_object_get_ex(root, "ok", &value) ||
	    !json_object_get_boolean(value))
		goto out;
	if (!json_object_object_get_ex(root, "version", &value) ||
	    json_object_get_int(value) != 1)
		goto out;
	if (json_string(root, "home", dock->home, sizeof(dock->home)) == false)
		g_strlcpy(dock->home, "1", sizeof(dock->home));
	if (json_string(root, "focused", dock->focused, sizeof(dock->focused)) == false)
		g_strlcpy(dock->focused, dock->home, sizeof(dock->focused));
	if (!json_object_object_get_ex(root, "tasks", &tasks) ||
	    !json_object_is_type(tasks, json_type_array))
		goto out;
	dock->camera_focused = false;
	dock->task_count = MIN((unsigned int)json_object_array_length(tasks), MAX_TASKS);
	for (unsigned int i = 0; i < dock->task_count; i++) {
		json_object *task = json_object_array_get_idx(tasks, i);
		json_object *icons;
		json_object *windows;
		dock->task_data[i] = (struct task){0};
		if (!json_object_is_type(task, json_type_object)) {
			dock->task_count = i;
			break;
		}
		(void)json_string(task, "workspace", dock->task_data[i].workspace,
		    sizeof(dock->task_data[i].workspace));
		(void)json_string(task, "layout", dock->task_data[i].layout,
		    sizeof(dock->task_data[i].layout));
		if (dock->task_data[i].layout[0] == '\0')
			g_strlcpy(dock->task_data[i].layout, "single",
			    sizeof(dock->task_data[i].layout));
		dock->task_data[i].focused = json_boolean(task, "focused");
		dock->task_data[i].urgent = json_boolean(task, "urgent");
		dock->task_data[i].window_count = json_uint(task, "window_count", 1,
		    999);
		if (dock->task_data[i].window_count == 0)
			dock->task_data[i].window_count = 1;
		if (json_object_object_get_ex(task, "windows", &windows) &&
		    json_object_is_type(windows, json_type_array)) {
			const unsigned int window_count =
			    (unsigned int)json_object_array_length(windows);
			for (unsigned int j = 0; j < window_count; j++) {
				json_object *window =
				    json_object_array_get_idx(windows, j);
				char app_id[MAX_TEXT] = {0};
				if (!json_object_is_type(window, json_type_object))
					continue;
				(void)json_string(window, "app_id", app_id,
				    sizeof(app_id));
				const bool window_focused =
				    json_boolean(window, "focused");
				dock->task_data[i].focused |= window_focused;
				if (window_focused &&
				    strcmp(app_id, "dev.ctlst.Camera") == 0) {
					dock->camera_focused = true;
					break;
				}
			}
		}
		if (json_object_object_get_ex(task, "icons", &icons) &&
		    json_object_is_type(icons, json_type_array)) {
			unsigned int count = MIN((unsigned int)json_object_array_length(icons), MAX_ICONS);
			for (unsigned int j = 0; j < count; j++) {
				json_object *icon = json_object_array_get_idx(icons, j);
				if (json_object_is_type(icon, json_type_string)) {
					g_strlcpy(dock->task_data[i].icons[dock->task_data[i].icon_count],
					    json_object_get_string(icon), MAX_TEXT);
					dock->task_data[i].icon_count++;
				}
			}
		}
	}
	dock->snapshot_hash = hash;
rebuild(dock);
out:
	if (root != NULL)
		json_object_put(root);
}

static bool
lock_visible(void)
{
	char *path = g_build_filename(g_get_user_runtime_dir(),
	    "ctlstlock.visible", NULL);
	bool visible = g_file_test(path, G_FILE_TEST_EXISTS);

	g_free(path);
	return visible;
}

static void
set_dock_visible(struct dock *dock, bool visible)
{
	/*
	 * Disable targeting before hiding. This closes the small polling window
	 * where a camera shutter touch could otherwise reach the overlay dock.
	 */
	dock->shell_visible = visible;
	gtk_widget_set_can_target(GTK_WIDGET(dock->window),
	    visible);
	gtk_widget_set_can_target(GTK_WIDGET(dock->root),
	    visible && dock->reveal >= 0.82);
	gtk_widget_set_can_target(GTK_WIDGET(dock->surface),
	    visible);
	gtk_widget_set_sensitive(GTK_WIDGET(dock->window), visible);
	gtk_widget_set_visible(GTK_WIDGET(dock->window), visible);
	update_dock_input_region(dock);
}

static gboolean
poll_snapshot(gpointer data)
{
	struct dock *dock = data;
	const char *runtime = g_get_user_runtime_dir();
	char *path = g_build_filename(runtime, "ctlst-workspaces.sock", NULL);
	char buffer[MAX_REPLY];
	const char request[] = "{\"command\":\"list\"}\n";
	struct sockaddr_un address = {0};
	struct timeval timeout = {.tv_sec = 0, .tv_usec = 120000};
	ssize_t received;
	size_t offset = 0;
	int fd;

	if (lock_visible()) {
		set_dock_visible(dock, false);
		goto out;
	}
	fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0)
		goto out;
	(void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
	address.sun_family = AF_UNIX;
	if (g_strlcpy(address.sun_path, path, sizeof(address.sun_path)) >=
	    sizeof(address.sun_path))
		goto close_fd;
	if (connect(fd, (struct sockaddr *)&address, sizeof(address)) != 0)
		goto close_fd;
	if (send(fd, request, sizeof(request) - 1, MSG_NOSIGNAL) < 0)
		goto close_fd;
	while (offset + 1 < sizeof(buffer)) {
		received = recv(fd, buffer + offset, sizeof(buffer) - offset - 1, 0);
		if (received <= 0)
			break;
		offset += (size_t)received;
		if (memchr(buffer, '\n', offset) != NULL)
			break;
	}
	if (offset > 0) {
		buffer[offset] = '\0';
		parse_snapshot(dock, buffer, offset);
	}
close_fd:
	close(fd);
out:
	if (!lock_visible()) {
		set_dock_visible(dock, !dock->camera_focused);
	}
	g_free(path);
	return G_SOURCE_CONTINUE;
}

static void
activate(GtkApplication *application, gpointer data)
{
	struct dock *dock = data;
	GtkEventController *scroll;
	GtkEventController *motion;

	dock->application = application;
	dock->window = GTK_WINDOW(gtk_application_window_new(application));
	gtk_widget_set_name(GTK_WIDGET(dock->window), "ctlstdock");
	gtk_window_set_title(dock->window, "ctlstdock");
	gtk_window_set_decorated(dock->window, FALSE);
	gtk_layer_init_for_window(dock->window);
	gtk_layer_set_namespace(dock->window, "ctlstdock");
	gtk_layer_set_layer(dock->window, GTK_LAYER_SHELL_LAYER_OVERLAY);
	gtk_layer_set_anchor(dock->window, GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
	/* Sit on the physical bottom edge; no layout exclusive zone for apps. */
	gtk_layer_set_margin(dock->window, GTK_LAYER_SHELL_EDGE_BOTTOM, 0);
	gtk_layer_set_exclusive_zone(dock->window, 0);
	gtk_layer_set_keyboard_mode(dock->window, GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);

	dock->overlay = GTK_OVERLAY(gtk_overlay_new());
	gtk_widget_set_size_request(GTK_WIDGET(dock->overlay),
	    DOCK_SURFACE_WIDTH, DOCK_SURFACE_HEIGHT);
	dock->surface = g_object_new(dock_surface_get_type(), NULL);
	((DockSurface *)dock->surface)->dock = dock;
	scroll = gtk_event_controller_scroll_new(
	    GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES |
	    GTK_EVENT_CONTROLLER_SCROLL_DISCRETE);
	g_signal_connect(scroll, "scroll", G_CALLBACK(dock_scrolled), dock);
	gtk_event_controller_set_propagation_phase(scroll, GTK_PHASE_CAPTURE);
	gtk_widget_add_controller(GTK_WIDGET(dock->window), scroll);
	motion = gtk_event_controller_motion_new();
	g_signal_connect(motion, "enter", G_CALLBACK(dock_pointer_enter), dock);
	g_signal_connect(motion, "leave", G_CALLBACK(dock_pointer_leave), dock);
	gtk_widget_add_controller(GTK_WIDGET(dock->window), motion);
	gtk_overlay_set_child(dock->overlay, GTK_WIDGET(dock->surface));

	dock->root = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4));
	gtk_widget_add_css_class(GTK_WIDGET(dock->root), "dock-root");
	gtk_widget_set_halign(GTK_WIDGET(dock->root), GTK_ALIGN_CENTER);
	gtk_widget_set_valign(GTK_WIDGET(dock->root), GTK_ALIGN_CENTER);
	gtk_overlay_add_overlay(dock->overlay, GTK_WIDGET(dock->root));
	gtk_window_set_child(dock->window, GTK_WIDGET(dock->overlay));
	install_css(dock);
	dock->shell_visible = true;
	apply_reveal(dock, 0.0);
	g_strlcpy(dock->home, "1", sizeof(dock->home));
	g_strlcpy(dock->focused, "1", sizeof(dock->focused));
	rebuild(dock);
	gtk_window_present(dock->window);
	/* Surface exists after present; re-apply empty input region. */
	update_dock_input_region(dock);
	(void)create_control_socket(dock);
	if (lock_visible())
		set_dock_visible(dock, false);
	g_timeout_add(POLL_MS, poll_snapshot, dock);
}

int
main(int argc, char **argv)
{
	struct dock dock = {
		.control_fd = -1,
	};
	GtkApplication *application;
	int status;

	(void)argv;
	if (g_getenv("GSK_RENDERER") == NULL)
		g_setenv("GSK_RENDERER", "gl", FALSE);
	load_theme(&dock.theme);
	application = gtk_application_new("com.ctlst.Dock", G_APPLICATION_NON_UNIQUE);
	g_signal_connect(application, "activate", G_CALLBACK(activate), &dock);
	status = g_application_run(G_APPLICATION(application), argc, argv);
	if (dock.reveal_source != 0)
		g_source_remove(dock.reveal_source);
	if (dock.collapse_source != 0)
		g_source_remove(dock.collapse_source);
	if (dock.control_source != 0)
		g_source_remove(dock.control_source);
	if (dock.control_fd >= 0)
		close(dock.control_fd);
	if (dock.control_path[0] != '\0')
		unlink(dock.control_path);
	if (dock.css != NULL) {
		gtk_style_context_remove_provider_for_display(gdk_display_get_default(),
		    GTK_STYLE_PROVIDER(dock.css));
		g_clear_object(&dock.css);
	}
	g_object_unref(application);
	return status;
}
