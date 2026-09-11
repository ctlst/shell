#define _POSIX_C_SOURCE 200809L

#include "../ctlst-runtime.h"
#include <gtk/gtk.h>
#include <gtk4-layer-shell.h>
#include <glib-unix.h>

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define CONTROL_MESSAGE_SIZE 16
#define UNLOCK_DISTANCE 150.0
#define LOCK_RELEASE_SETTLE_MS 80
#define LOCK_STARTUP_TIMEOUT_SECONDS 10

struct color {
	double red;
	double green;
	double blue;
	double alpha;
};

struct lock_theme {
	struct color bg_top;
	struct color bg_bottom;
	struct color bg;
	struct color raised;
	struct color panel;
	struct color panel_alt;
	struct color line;
	struct color line_soft;
	struct color text;
	struct color muted;
	struct color accent;
	struct color accent_soft;
	struct color warm;
	struct color danger;
	struct color danger_active;
	struct color selected_text;
	struct color overlay;
};

enum lock_label {
	LOCK_TITLE, LOCK_NETWORK, LOCK_BATTERY, LOCK_CLOCK, LOCK_DATE, LOCK_HINT,
	LOCK_LABEL_COUNT
};

struct app {
	GtkApplication *application;
	GtkWindow *window;
	GtkWidget *canvas;
	GtkWidget *sheet;
	GtkCssProvider *chrome_css;
	PangoLayout *labels[LOCK_LABEL_COUNT];
	int cache_width, cache_height;
	char last_clock[32], last_date[96];
	int control_fd;
	guint control_source;
	guint clock_source;
	guint status_source;
	guint hide_source;
	guint startup_source;
	char control_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
	char visible_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
	char network[128];
	char battery[32];
	struct lock_theme theme;
	double drag_offset;
	double drag_origin;
	double settle_from;
	gint64 settle_started;
	guint settle_tick;
	bool dismissing;
	bool drag_active;
	bool visible;
};

static void draw_lock(GtkWidget *widget, GtkSnapshot *snapshot, struct app *app);
static void apply_chrome_style(struct app *app);
static void stop_settle(struct app *app);

typedef struct {
	GtkWidget parent_instance;
	struct app *app;
} LockCanvas;
typedef GtkWidgetClass LockCanvasClass;
G_DEFINE_TYPE(LockCanvas, lock_canvas, GTK_TYPE_WIDGET)

static void
lock_canvas_snapshot(GtkWidget *widget, GtkSnapshot *snapshot)
{
	draw_lock(widget, snapshot, ((LockCanvas *)widget)->app);
}

static void
lock_canvas_measure(GtkWidget *widget, GtkOrientation orientation, int for_size,
    int *minimum, int *natural, int *minimum_baseline, int *natural_baseline)
{
	(void)widget;
	(void)orientation;
	(void)for_size;
	*minimum = *natural = 1;
	*minimum_baseline = *natural_baseline = -1;
}

static void
lock_canvas_class_init(LockCanvasClass *klass)
{
	gtk_widget_class_set_accessible_role(GTK_WIDGET_CLASS(klass), GTK_ACCESSIBLE_ROLE_GROUP);
	klass->snapshot = lock_canvas_snapshot;
	klass->measure = lock_canvas_measure;
}

static void lock_canvas_init(LockCanvas *canvas) { (void)canvas; }

/* One retained sheet moves as a whole, including its opaque background and
 * native Continue button. The full-window host keeps input until dismissal. */
typedef struct {
	GtkWidget parent_instance;
	GtkWidget *child;
	struct app *app;
	int width, height;
} LockSheet;
typedef GtkWidgetClass LockSheetClass;
G_DEFINE_TYPE(LockSheet, lock_sheet, GTK_TYPE_WIDGET)

static void
lock_sheet_allocate(GtkWidget *widget, int width, int height, int baseline)
{
	LockSheet *sheet = (LockSheet *)widget;
	struct app *app = sheet->app;
	if (sheet->height && (height != sheet->height || width != sheet->width)) {
		/* A new orientation cancels the old finger coordinate system. */
		stop_settle(app);
		app->drag_active = app->dismissing = false;
		app->drag_offset = 0;
	}
	sheet->width = width;
	sheet->height = height;
	graphene_point_t point = GRAPHENE_POINT_INIT(0, app->drag_offset);
	gtk_widget_allocate(sheet->child, width, height, baseline,
	    gsk_transform_translate(NULL, &point));
}

static void
lock_sheet_snapshot(GtkWidget *widget, GtkSnapshot *snapshot)
{
	gtk_widget_snapshot_child(widget, ((LockSheet *)widget)->child, snapshot);
}

static void
lock_sheet_dispose(GObject *object)
{
	LockSheet *sheet = (LockSheet *)object;
	g_clear_pointer(&sheet->child, gtk_widget_unparent);
	G_OBJECT_CLASS(lock_sheet_parent_class)->dispose(object);
}

static void
lock_sheet_class_init(LockSheetClass *klass)
{
	klass->measure = lock_canvas_measure;
	klass->size_allocate = lock_sheet_allocate;
	klass->snapshot = lock_sheet_snapshot;
	G_OBJECT_CLASS(klass)->dispose = lock_sheet_dispose;
}

static void
lock_sheet_init(LockSheet *sheet)
{
	gtk_widget_set_overflow(GTK_WIDGET(sheet), GTK_OVERFLOW_HIDDEN);
}

static GdkRGBA rgba(struct color color)
{
	return (GdkRGBA){color.red, color.green, color.blue, color.alpha};
}

static void
clear_labels(struct app *app)
{
	for (int i = 0; i < LOCK_LABEL_COUNT; i++)
		g_clear_object(&app->labels[i]);
}

static bool
parse_hex_color(const char *value, struct color *color)
{
	char *end;
	unsigned long parsed;
	size_t length;

	if (value == NULL)
		return false;
	while (g_ascii_isspace(*value))
		value++;
	if (*value == '#')
		value++;
	length = strlen(value);
	if (length != 6 && length != 8)
		return false;
	parsed = g_ascii_strtoull(value, &end, 16);
	if (*end != '\0')
		return false;
	color->red = ((parsed >> (length == 8 ? 24 : 16)) & 0xff) / 255.0;
	color->green = ((parsed >> (length == 8 ? 16 : 8)) & 0xff) / 255.0;
	color->blue = ((parsed >> (length == 8 ? 8 : 0)) & 0xff) / 255.0;
	color->alpha = (length == 8 ? parsed & 0xff : 0xff) / 255.0;
	return true;
}

static void
set_default_color(struct color *color, unsigned int red, unsigned int green,
	unsigned int blue, unsigned int alpha)
{
	color->red = red / 255.0;
	color->green = green / 255.0;
	color->blue = blue / 255.0;
	color->alpha = alpha / 255.0;
}

static void
set_theme_color(struct lock_theme *theme, const char *key, const char *value)
{
	struct color *color = NULL;

	if (strcmp(key, "LOCK_BG_TOP") == 0)
		color = &theme->bg_top;
	else if (strcmp(key, "LOCK_BG_BOTTOM") == 0)
		color = &theme->bg_bottom;
	else if (strcmp(key, "SHELL_BG") == 0)
		color = &theme->bg;
	else if (strcmp(key, "SHELL_RAISED") == 0)
		color = &theme->raised;
	else if (strcmp(key, "SHELL_PANEL") == 0)
		color = &theme->panel;
	else if (strcmp(key, "SHELL_PANEL_ALT") == 0)
		color = &theme->panel_alt;
	else if (strcmp(key, "SHELL_LINE") == 0)
		color = &theme->line;
	else if (strcmp(key, "SHELL_LINE_SOFT") == 0)
		color = &theme->line_soft;
	else if (strcmp(key, "SHELL_TEXT") == 0)
		color = &theme->text;
	else if (strcmp(key, "SHELL_MUTED") == 0)
		color = &theme->muted;
	else if (strcmp(key, "SHELL_ACCENT") == 0)
		color = &theme->accent;
	else if (strcmp(key, "SHELL_ACCENT_SOFT") == 0)
		color = &theme->accent_soft;
	else if (strcmp(key, "SHELL_WARM") == 0)
		color = &theme->warm;
	else if (strcmp(key, "SHELL_DANGER") == 0)
		color = &theme->danger;
	else if (strcmp(key, "SHELL_DANGER_ACTIVE") == 0)
		color = &theme->danger_active;
	else if (strcmp(key, "SHELL_SELECTED_TEXT") == 0)
		color = &theme->selected_text;
	else if (strcmp(key, "SHELL_OVERLAY") == 0)
		color = &theme->overlay;
	if (color != NULL)
		parse_hex_color(value, color);
}

static void
init_theme(struct lock_theme *theme)
{
	set_default_color(&theme->bg_top, 7, 19, 28, 255);
	set_default_color(&theme->bg_bottom, 11, 36, 37, 255);
	set_default_color(&theme->bg, 11, 25, 23, 255);
	set_default_color(&theme->raised, 27, 39, 36, 255);
	set_default_color(&theme->panel, 23, 39, 36, 255);
	set_default_color(&theme->panel_alt, 33, 52, 49, 255);
	set_default_color(&theme->line, 49, 95, 88, 255);
	set_default_color(&theme->line_soft, 95, 113, 107, 66);
	set_default_color(&theme->text, 244, 237, 227, 255);
	set_default_color(&theme->muted, 164, 176, 170, 255);
	set_default_color(&theme->accent, 101, 183, 167, 255);
	set_default_color(&theme->accent_soft, 36, 73, 65, 255);
	set_default_color(&theme->warm, 231, 169, 59, 255);
	set_default_color(&theme->danger, 182, 74, 55, 255);
	set_default_color(&theme->danger_active, 228, 94, 71, 255);
	set_default_color(&theme->selected_text, 17, 23, 19, 255);
	set_default_color(&theme->overlay, 11, 18, 16, 232);
}

static void
load_theme(struct app *app)
{
	char *path = ctlst_config_path("theme.env");
	char *contents = NULL;
	char **lines;
	char **parts;
	size_t index;

	init_theme(&app->theme);
	if (!g_file_get_contents(path, &contents, NULL, NULL)) {
		g_free(path);
		apply_chrome_style(app);
		return;
	}
	lines = g_strsplit(contents, "\n", -1);
	for (index = 0; lines[index] != NULL; index++) {
		parts = g_strsplit(lines[index], "=", 2);
		if (parts[0] != NULL && parts[1] != NULL)
			set_theme_color(&app->theme, parts[0], parts[1]);
		g_strfreev(parts);
	}
	g_strfreev(lines);
	g_free(contents);
	g_free(path);
	apply_chrome_style(app);
}

static void
apply_chrome_style(struct app *app)
{
	char *css = g_strdup_printf(
	    "window#ctlstlock { background: transparent; }"
	    "window#ctlstlock button { min-height: 48px; min-width: 120px;"
	    " padding: 0 18px; border: 0; border-radius: 24px; box-shadow: none;"
	    " font-family: 'Noto Sans'; font-size: 16px; font-weight: 500;"
	    " background: rgb(%d,%d,%d); color: rgb(%d,%d,%d); }"
	    "window#ctlstlock button:focus-visible { outline: 2px solid rgb(%d,%d,%d);"
	    " outline-offset: -4px; }",
	    (int)(app->theme.accent.red * 255), (int)(app->theme.accent.green * 255),
	    (int)(app->theme.accent.blue * 255),
	    (int)(app->theme.selected_text.red * 255), (int)(app->theme.selected_text.green * 255),
	    (int)(app->theme.selected_text.blue * 255),
	    (int)(app->theme.selected_text.red * 255), (int)(app->theme.selected_text.green * 255),
	    (int)(app->theme.selected_text.blue * 255));
	if (app->chrome_css == NULL) {
		app->chrome_css = gtk_css_provider_new();
		gtk_style_context_add_provider_for_display(gdk_display_get_default(),
		    GTK_STYLE_PROVIDER(app->chrome_css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	}
	gtk_css_provider_load_from_string(app->chrome_css, css);
	g_free(css);
}

static int
draw_label(GtkWidget *widget, GtkSnapshot *snapshot, PangoLayout **cached,
    const char *text, double x, double y, double width, double size,
    PangoWeight weight, PangoAlignment alignment, struct color color)
{
	if (*cached == NULL) {
		PangoFontDescription *font = pango_font_description_new();
		char *valid = g_utf8_make_valid(text, -1);
		*cached = gtk_widget_create_pango_layout(widget, valid);
		g_free(valid);
		pango_font_description_set_family(font, "Noto Sans");
		pango_font_description_set_absolute_size(font, size * PANGO_SCALE);
		pango_font_description_set_weight(font, weight);
		pango_layout_set_font_description(*cached, font);
		pango_font_description_free(font);
		pango_layout_set_width(*cached, (int)(fmax(1, width) * PANGO_SCALE));
		pango_layout_set_ellipsize(*cached, PANGO_ELLIPSIZE_END);
		pango_layout_set_single_paragraph_mode(*cached, TRUE);
		pango_layout_set_alignment(*cached, alignment);
	}
	GdkRGBA ink = rgba(color);
	graphene_point_t origin = {(float)x, (float)y};
	int height;
	pango_layout_get_pixel_size(*cached, NULL, &height);
	gtk_snapshot_save(snapshot);
	gtk_snapshot_translate(snapshot, &origin);
	gtk_snapshot_append_layout(snapshot, *cached, &ink);
	gtk_snapshot_restore(snapshot);
	return height;
}

static void
format_clock(char *clock_text, size_t clock_size, char *date_text,
    size_t date_size)
{
	time_t now = time(NULL);
	struct tm local;
	char raw_clock[32];

	localtime_r(&now, &local);
	strftime(raw_clock, sizeof(raw_clock), "%I:%M", &local);
	while (raw_clock[0] == '0')
		memmove(raw_clock, raw_clock + 1, strlen(raw_clock));
	snprintf(clock_text, clock_size, "%s", raw_clock);
	strftime(date_text, date_size, "%A, %B %e", &local);
}

static void
draw_lock(GtkWidget *widget, GtkSnapshot *snapshot, struct app *app)
{
	int width = gtk_widget_get_width(widget), height = gtk_widget_get_height(widget);
	char clock_text[32], date_text[96];
	double padding = width < 360 ? 16 : 24;
	double content_width = width - padding * 2;
	double clock_size = fmax(40, fmin(96, fmin(width * 0.18, height * 0.16)));
	double clock_top = fmax(82, height * 0.23);
	if (width < 1 || height < 1)
		return;
	if (app->cache_width != width || app->cache_height != height) {
		clear_labels(app);
		app->cache_width = width;
		app->cache_height = height;
	}
	format_clock(clock_text, sizeof(clock_text), date_text, sizeof(date_text));
	if (strcmp(clock_text, app->last_clock) || strcmp(date_text, app->last_date)) {
		g_clear_object(&app->labels[LOCK_CLOCK]);
		g_clear_object(&app->labels[LOCK_DATE]);
		g_strlcpy(app->last_clock, clock_text, sizeof(app->last_clock));
		g_strlcpy(app->last_date, date_text, sizeof(app->last_date));
	}

	graphene_rect_t bounds = GRAPHENE_RECT_INIT(0, 0, width, height);
	graphene_point_t start = {0, 0}, end = {(float)width, (float)height};
	GskColorStop stops[] = {
		{0, rgba(app->theme.bg_top)}, {0.52, rgba(app->theme.bg)},
		{1, rgba(app->theme.bg_bottom)}
	};
	/* A translucent palette must never expose the covered applications. */
	for (unsigned i = 0; i < G_N_ELEMENTS(stops); i++)
		stops[i].color.alpha = 1;
	gtk_snapshot_append_linear_gradient(snapshot, &bounds, &start, &end,
	    stops, G_N_ELEMENTS(stops));

	draw_label(widget, snapshot, &app->labels[LOCK_TITLE], "Privacy screen",
	    padding, 22, content_width * 0.65, 14, PANGO_WEIGHT_MEDIUM,
	    PANGO_ALIGN_LEFT, app->theme.text);
	draw_label(widget, snapshot, &app->labels[LOCK_BATTERY], app->battery,
	    width * 0.70, 22, width * 0.30 - padding, 13, PANGO_WEIGHT_NORMAL,
	    PANGO_ALIGN_RIGHT, app->theme.text);
	draw_label(widget, snapshot, &app->labels[LOCK_NETWORK], app->network,
	    padding, 48, content_width, 13, PANGO_WEIGHT_NORMAL,
	    PANGO_ALIGN_LEFT, app->theme.muted);
	int clock_height = draw_label(widget, snapshot, &app->labels[LOCK_CLOCK],
	    clock_text, padding, clock_top, content_width, clock_size,
	    PANGO_WEIGHT_NORMAL, PANGO_ALIGN_CENTER, app->theme.text);
	draw_label(widget, snapshot, &app->labels[LOCK_DATE], date_text,
	    padding, clock_top + clock_height + 8, content_width, 15,
	    PANGO_WEIGHT_NORMAL, PANGO_ALIGN_CENTER, app->theme.muted);
	if (height >= 420)
		draw_label(widget, snapshot, &app->labels[LOCK_HINT],
		    "Swipe up to continue", padding, height - 138, content_width,
		    14, PANGO_WEIGHT_NORMAL, PANGO_ALIGN_CENTER, app->theme.muted);
	GdkRGBA ink = rgba(app->theme.muted);
	graphene_rect_t handle = GRAPHENE_RECT_INIT(width / 2.0 - 28, height - 24, 56, 4);
	gtk_snapshot_append_color(snapshot, &ink, &handle);
}

static void
read_battery(struct app *app)
{
	char *capacity = NULL;
	char *status = NULL;
	char *capacity_path = g_strdup(
	    "/sys/class/power_supply/qcom-battery/capacity");
	char *status_path = g_strdup(
	    "/sys/class/power_supply/qcom-battery/status");

	if (g_file_get_contents(capacity_path, &capacity, NULL, NULL)) {
		g_strstrip(capacity);
		g_file_get_contents(status_path, &status, NULL, NULL);
		if (status != NULL)
			g_strstrip(status);
		snprintf(app->battery, sizeof(app->battery), "%s%s%%",
		    status != NULL && strcmp(status, "Charging") == 0 ? "+" : "",
		    capacity);
	} else {
		snprintf(app->battery, sizeof(app->battery), "--%%");
	}
	g_free(capacity);
	g_free(status);
	g_free(capacity_path);
	g_free(status_path);
}

static void
read_network(struct app *app)
{
	char *output = NULL;
	char *marker;
	char *line;
	bool mobile = false;

	snprintf(app->network, sizeof(app->network), "Wi-Fi disconnected");
	if (!g_spawn_command_line_sync(
	    "nmcli -t -f NAME,TYPE connection show --active",
	    &output, NULL, NULL, NULL) || output == NULL)
		return;
	for (line = strtok(output, "\n"); line != NULL;
	    line = strtok(NULL, "\n")) {
		marker = strstr(line, ":802-11-wireless");
		if (marker != NULL) {
			*marker = '\0';
			snprintf(app->network, sizeof(app->network), "Wi-Fi  |  %s",
			    line);
			g_free(output);
			return;
		}
		if (strstr(line, ":gsm") != NULL)
			mobile = true;
	}
	if (mobile)
		snprintf(app->network, sizeof(app->network), "Mobile network");
	g_free(output);
}

static void
update_accessible_status(struct app *app)
{
	char clock_text[32], date_text[96];
	format_clock(clock_text, sizeof(clock_text), date_text, sizeof(date_text));
	char *raw = g_strdup_printf("Privacy screen. %s, %s. %s. Battery %s.",
	    clock_text, date_text, app->network, app->battery);
	char *valid = g_utf8_make_valid(raw, -1);
	gtk_accessible_update_property(GTK_ACCESSIBLE(app->canvas),
	    GTK_ACCESSIBLE_PROPERTY_LABEL, valid,
	    GTK_ACCESSIBLE_PROPERTY_DESCRIPTION, "Swipe up or activate Continue. This is not an authenticated lock.",
	    -1);
	g_free(valid);
	g_free(raw);
}

static gboolean
refresh_clock(gpointer data)
{
	struct app *app = data;
	char clock_text[32], date_text[96];

	format_clock(clock_text, sizeof(clock_text), date_text, sizeof(date_text));
	if (app->visible && (strcmp(clock_text, app->last_clock) ||
	    strcmp(date_text, app->last_date))) {
		update_accessible_status(app);
		gtk_widget_queue_draw(app->canvas);
	}
	return G_SOURCE_CONTINUE;
}

static gboolean
refresh_status(gpointer data)
{
	struct app *app = data;
	char network[sizeof(app->network)], battery[sizeof(app->battery)];

	g_strlcpy(network, app->network, sizeof(network));
	g_strlcpy(battery, app->battery, sizeof(battery));
	read_network(app);
	read_battery(app);
	if (strcmp(network, app->network))
		g_clear_object(&app->labels[LOCK_NETWORK]);
	if (strcmp(battery, app->battery))
		g_clear_object(&app->labels[LOCK_BATTERY]);
	update_accessible_status(app);
	if (app->visible)
		gtk_widget_queue_draw(app->canvas);
	return G_SOURCE_CONTINUE;
}

static void
restart_gestures(void)
{
	char *script = ctlst_script_path("touch-gestures");
	if (!g_file_test(script, G_FILE_TEST_IS_EXECUTABLE)) {
		g_free(script);
		return;
	}
	char *quoted = g_shell_quote(script);
	char *command = g_strdup_printf("%s >/dev/null 2>&1", quoted);

	g_spawn_command_line_async(command, NULL);
	g_free(command);
	g_free(quoted);
	g_free(script);
}

static gboolean
finish_hide(gpointer data)
{
	struct app *app = data;

	app->hide_source = 0;
	if (app->visible)
		return G_SOURCE_REMOVE;
	unlink(app->visible_path);
	restart_gestures();
	g_application_quit(G_APPLICATION(app->application));
	return G_SOURCE_REMOVE;
}

static gboolean
quit_if_never_shown(gpointer data)
{
	struct app *app = data;

	app->startup_source = 0;
	if (!app->visible)
		g_application_quit(G_APPLICATION(app->application));
	return G_SOURCE_REMOVE;
}

static void
lock_surface_mapped(GtkWidget *widget, gpointer data)
{
	struct app *app = data;

	(void)widget;
	if (app->visible)
		g_file_set_contents(app->visible_path, "", 0, NULL);
}

static void
set_visible_state(struct app *app, bool visible)
{
	stop_settle(app);
	app->dismissing = false;
	if (app->window == NULL)
		return;
	if (app->hide_source != 0) {
		g_source_remove(app->hide_source);
		app->hide_source = 0;
	}
	app->visible = visible;
	app->drag_active = false;
	app->drag_offset = 0;
	if (visible) {
		if (app->startup_source != 0) {
			g_source_remove(app->startup_source);
			app->startup_source = 0;
		}
		gtk_layer_set_keyboard_mode(app->window,
		    GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE);
		load_theme(app);
		refresh_status(app);
		gtk_window_present(app->window);
		gtk_widget_grab_focus(app->canvas);
	} else {
		gtk_window_set_focus(app->window, NULL);
		gtk_layer_set_keyboard_mode(app->window,
		    GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
		/*
		 * Destroy the exclusive layer surface instead of parking it hidden.
		 * Some wlroots releases retain its keyboard ownership until the
		 * surface role is destroyed, leaving Home unable to accept input.
		 * The ctlstlock launcher starts a fresh process for the next lock.
		 */
		gtk_window_destroy(app->window);
		app->window = NULL;
		app->canvas = NULL;
		app->sheet = NULL;
		app->hide_source = g_timeout_add(LOCK_RELEASE_SETTLE_MS,
		    finish_hide, app);
	}
	if (app->canvas != NULL)
		gtk_widget_queue_draw(app->canvas);
}

static void
stop_settle(struct app *app)
{
	if (app->settle_tick && app->sheet)
		gtk_widget_remove_tick_callback(app->sheet, app->settle_tick);
	app->settle_tick = 0;
}

static gboolean
settle_frame(GtkWidget *widget, GdkFrameClock *clock, gpointer data)
{
	struct app *app = data;
	double duration = app->dismissing ? 220000.0 : 180000.0;
	double progress = fmin(1, (gdk_frame_clock_get_frame_time(clock) - app->settle_started) / duration);
	double target = app->dismissing ? -gtk_widget_get_height(widget) : 0;
	double eased = 1 - pow(1 - fmax(0, progress), 3);
	app->drag_offset = app->settle_from + (target - app->settle_from) * eased;
	gtk_widget_queue_allocate(widget);
	if (progress < 1)
		return G_SOURCE_CONTINUE;
	app->settle_tick = 0;
	if (app->dismissing)
		set_visible_state(app, false);
	return G_SOURCE_REMOVE;
}

static void
settle_sheet(struct app *app, bool dismiss)
{
	if (!app->visible || app->sheet == NULL || app->dismissing)
		return;
	stop_settle(app);
	app->drag_active = false;
	app->dismissing = dismiss;
	gboolean animations = TRUE;
	g_object_get(gtk_widget_get_settings(app->sheet), "gtk-enable-animations", &animations, NULL);
	if (!animations || (!dismiss && app->drag_offset == 0)) {
		app->drag_offset = 0;
		if (dismiss)
			set_visible_state(app, false);
		else
			gtk_widget_queue_allocate(app->sheet);
		return;
	}
	app->settle_from = app->drag_offset;
	app->settle_started = g_get_monotonic_time();
	app->settle_tick = gtk_widget_add_tick_callback(app->sheet, settle_frame, app, NULL);
}

static void
drag_begin(GtkGestureDrag *gesture, double x, double y, gpointer data)
{
	struct app *app = data;

	(void)x;
	(void)y;
	app->drag_active = app->visible && !app->dismissing;
	if (app->drag_active)
		stop_settle(app);
	app->drag_origin = app->drag_offset;
	if (!app->drag_active)
		gtk_gesture_set_state(GTK_GESTURE(gesture),
		    GTK_EVENT_SEQUENCE_DENIED);
}

static void
drag_update(GtkGestureDrag *gesture, double offset_x, double offset_y,
    gpointer data)
{
	struct app *app = data;

	if (!app->drag_active)
		return;
	if (offset_y > 8 || (fabs(offset_x) > 8 && fabs(offset_x) > fabs(offset_y) * 1.35)) {
		gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_DENIED);
		settle_sheet(app, false);
		return;
	}
	if (offset_y < -8)
		gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
	app->drag_offset = fmax(-gtk_widget_get_height(app->sheet), fmin(0, app->drag_origin + offset_y));
	gtk_widget_queue_allocate(app->sheet);
}

static void
drag_end(GtkGestureDrag *gesture, double offset_x, double offset_y,
    gpointer data)
{
	struct app *app = data;
	if (!app->drag_active)
		return;
	double threshold = fmin(UNLOCK_DISTANCE, gtk_widget_get_height(app->sheet) * .28);
	bool unlock = offset_y <= -threshold &&
	    fabs(offset_y) >= fabs(offset_x) * 1.20;

	(void)gesture;
	app->drag_active = false;
	settle_sheet(app, unlock);
}

static void
drag_cancel(GtkGesture *gesture, GdkEventSequence *sequence, gpointer data)
{
	(void)gesture;
	(void)sequence;
	settle_sheet(data, false);
}

static gboolean
socket_ready(gint fd, GIOCondition condition, gpointer data)
{
	struct app *app = data;
	char message[CONTROL_MESSAGE_SIZE];
	ssize_t size;

	if ((condition & G_IO_IN) == 0)
		return G_SOURCE_CONTINUE;
	size = recv(fd, message, sizeof(message), 0);
	if (size <= 0)
		return G_SOURCE_CONTINUE;
	if (message[0] == 'L')
		set_visible_state(app, true);
	else if (message[0] == 'H')
		set_visible_state(app, false);
	else if (message[0] == 'T') {
		load_theme(app);
		if (app->canvas != NULL)
			gtk_widget_queue_draw(app->canvas);
	}
	return G_SOURCE_CONTINUE;
}

static gboolean
unlock_key(GtkEventControllerKey *controller, guint keyval, guint keycode,
    GdkModifierType modifiers, gpointer data)
{
	(void)controller;
	(void)keycode;
	(void)modifiers;
	if (keyval == GDK_KEY_Return || keyval == GDK_KEY_space) {
		settle_sheet(data, true);
		return TRUE;
	}
	return FALSE;
}

static void
continue_clicked(GtkButton *button, gpointer data)
{
	(void)button;
	settle_sheet(data, true);
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
	GtkGesture *drag = gtk_gesture_drag_new();
	GtkEventController *keys = gtk_event_controller_key_new();
	const char *runtime = g_get_user_runtime_dir();

	app->application = application;
	snprintf(app->control_path, sizeof(app->control_path),
	    "%s/ctlst-lock.sock", runtime);
	snprintf(app->visible_path, sizeof(app->visible_path),
	    "%s/ctlstlock.visible", runtime);
	app->window = GTK_WINDOW(gtk_application_window_new(application));
	gtk_widget_set_name(GTK_WIDGET(app->window), "ctlstlock");
	gtk_window_set_title(app->window, "ctlstlock");
	gtk_window_set_decorated(app->window, FALSE);
	gtk_layer_init_for_window(app->window);
	/*
	 * A lock surface must cover normal windows, bars, and shell panels.
	 * Call notifications opened after the gate can still map above it in the
	 * overlay layer.
	 */
	gtk_layer_set_layer(app->window, GTK_LAYER_SHELL_LAYER_OVERLAY);
	gtk_layer_set_namespace(app->window, "ctlstlock");
	gtk_layer_set_anchor(app->window, GTK_LAYER_SHELL_EDGE_TOP, TRUE);
	gtk_layer_set_anchor(app->window, GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
	gtk_layer_set_anchor(app->window, GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
	gtk_layer_set_anchor(app->window, GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
	gtk_layer_set_exclusive_zone(app->window, -1);
	gtk_layer_set_keyboard_mode(app->window,
	    GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);

	app->canvas = g_object_new(lock_canvas_get_type(), NULL);
	((LockCanvas *)app->canvas)->app = app;
	gtk_widget_set_focusable(app->canvas, TRUE);
	gtk_widget_set_hexpand(app->canvas, TRUE);
	gtk_widget_set_vexpand(app->canvas, TRUE);
	GtkWidget *overlay = gtk_overlay_new();
	gtk_overlay_set_child(GTK_OVERLAY(overlay), app->canvas);
	GtkWidget *button = gtk_button_new_with_label("Continue");
	gtk_widget_set_halign(button, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(button, GTK_ALIGN_END);
	gtk_widget_set_margin_bottom(button, 54);
	g_signal_connect(button, "clicked", G_CALLBACK(continue_clicked), app);
	gtk_overlay_add_overlay(GTK_OVERLAY(overlay), button);
	LockSheet *sheet = g_object_new(lock_sheet_get_type(), NULL);
	sheet->app = app;
	sheet->child = overlay;
	app->sheet = GTK_WIDGET(sheet);
	gtk_widget_set_parent(overlay, app->sheet);
	gtk_window_set_child(app->window, app->sheet);
	g_signal_connect(app->window, "map",
	    G_CALLBACK(lock_surface_mapped), app);
	g_signal_connect(drag, "drag-begin", G_CALLBACK(drag_begin), app);
	g_signal_connect(drag, "drag-update", G_CALLBACK(drag_update), app);
	g_signal_connect(drag, "drag-end", G_CALLBACK(drag_end), app);
	g_signal_connect(drag, "cancel", G_CALLBACK(drag_cancel), app);
	gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(drag), GTK_PHASE_CAPTURE);
	gtk_widget_add_controller(app->sheet, GTK_EVENT_CONTROLLER(drag));
	g_signal_connect(keys, "key-pressed", G_CALLBACK(unlock_key), app);
	gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
	gtk_widget_add_controller(GTK_WIDGET(app->window), keys);

	read_network(app);
	read_battery(app);
	create_socket(app);
	update_accessible_status(app);
	app->clock_source = g_timeout_add_seconds(1, refresh_clock, app);
	app->status_source = g_timeout_add_seconds(15, refresh_status, app);
	app->startup_source = g_timeout_add_seconds(
	    LOCK_STARTUP_TIMEOUT_SECONDS, quit_if_never_shown, app);
	gtk_widget_set_visible(GTK_WIDGET(app->window), FALSE);
	g_application_hold(G_APPLICATION(application));
}

static void
shutdown_app(GApplication *application, gpointer data)
{
	struct app *app = data;

	(void)application;
	stop_settle(app);
	if (app->clock_source != 0)
		g_source_remove(app->clock_source);
	if (app->status_source != 0)
		g_source_remove(app->status_source);
	if (app->hide_source != 0)
		g_source_remove(app->hide_source);
	if (app->startup_source != 0)
		g_source_remove(app->startup_source);
	if (app->control_source != 0)
		g_source_remove(app->control_source);
	if (app->control_fd >= 0)
		close(app->control_fd);
	unlink(app->control_path);
	unlink(app->visible_path);
	clear_labels(app);
	g_clear_object(&app->chrome_css);
}

int
main(int argc, char **argv)
{
	struct app app = {.control_fd = -1};
	if (g_getenv("GSK_RENDERER") == NULL)
		g_setenv("GSK_RENDERER", "gl", FALSE);
	GtkApplication *application = gtk_application_new(
	    "dev.ctlst.Lock", G_APPLICATION_DEFAULT_FLAGS);
	int status;

	g_signal_connect(application, "activate", G_CALLBACK(activate), &app);
	g_signal_connect(application, "shutdown", G_CALLBACK(shutdown_app), &app);
	status = g_application_run(G_APPLICATION(application), argc, argv);
	g_object_unref(application);
	return status;
}
