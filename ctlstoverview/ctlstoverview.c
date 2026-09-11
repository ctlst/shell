#define _GNU_SOURCE

#include "../ctlst-runtime.h"
#include <errno.h>
#include <fcntl.h>
#include <glib-unix.h>
#include <gio/gdesktopappinfo.h>
#include <gtk/gtk.h>
#include <gtk4-layer-shell.h>
#include <inttypes.h>
#include <json-c/json.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#define APP_ID "dev.ctlst.Overview"
#define MAX_WORKSPACE_CARDS 64
#define MAX_CONS_PER_CARD 64
#define MAX_LEAF_WINDOWS 128
#define CONTROL_MESSAGE_SIZE 64
#define OVERVIEW_SOCKET "ctlst-overview.sock"
#define OVERVIEW_STATE "ctlstoverview.visible"
#define OVERVIEW_CAPTURE_STATE "ctlstoverview.capture"
#define GESTURE_SOCKET "ctlst-gestures.sock"
#define DOCK_SOCKET "ctlst-dock.sock"
/* Keep cards and scrim clear of the bottom task dock (56px surface + margin). */
#define DOCK_CLEARANCE 72.0
#define OVERVIEW_CONTROLS_HEIGHT 56.0
/* Cards only fade for the first slice of progress; after that they stay opaque. */
#define CARD_FADE_IN_PROGRESS 0.18
/* Committed overview: drag down this far (and mostly vertical) to dismiss. */
#define OVERVIEW_DISMISS_DRAG_PX 88.0
#define OVERVIEW_DISMISS_DRAG_FRACTION 0.22
/* A card starts its deliberate upward dismissal after this much travel. */
#define CARD_DISMISS_DRAG_PX 110.0
/* Release settling mirrors the retained drawer/shade 180ms ease-out. */
#define CARD_DISMISS_SETTLE_US 180000

struct color {
	double red;
	double green;
	double blue;
	double alpha;
};

struct leaf_window {
	int64_t id;
	char app_id[128];
	char title[256];
	char workspace[128];
	bool focused;
};

struct workspace_card {
	char workspace[128];
	char cache_key[128];
	char summary[192];
	char label[160];
	int64_t ids[MAX_CONS_PER_CARD];
	int id_count;
	int app_count;
	bool focused;
	cairo_surface_t *thumb;
};

struct label_cache {
	PangoLayout *layout;
	char text[256];
	char font[48];
	int width;
};

struct overview {
	GtkApplication *application;
	GtkWindow *window;
	GtkWidget *area;
	GtkWidget *controls, *done_button, *previous_button, *next_button;
	GtkWidget *open_button, *close_button;
	GtkCssProvider *chrome_css;
	GdkTexture *card_textures[MAX_WORKSPACE_CARDS];
	int texture_scale;
	struct label_cache empty_label;
	struct workspace_card cards[MAX_WORKSPACE_CARDS];
	int card_count;
	int selected;
	int width;
	int height;
	int control_fd;
	guint control_source;
	guint tick_source;
	gint64 last_tick_us;
	char socket_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
	char state_path[256];
	char capture_state_path[256];
	guint64 capture_generation;
	char gesture_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
	char dock_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
	char cache_dir[512];
	double target_progress;
	double display_progress;
	double target_offset;
	double display_offset;
	double drag_origin;
	double drag_dx;
	double drag_dy;
	double drag_start_x;
	double drag_start_y;
	/* The focused card itself follows an upward dismissal pull 1:1. */
	double card_dismiss_target_y;
	double card_dismiss_display_y;
	double card_dismiss_start_y;
	gint64 card_dismiss_started_us;
	int drag_card_index;
	int card_dismiss_index;
	bool card_dismiss_active;
	bool card_dismiss_settling;
	bool card_dismiss_pending;
	bool mapped;
	bool committed;
	bool hide_pending;
	bool refresh_scheduled;
	bool data_ready;
	bool refresh_failed;
	bool refresh_running;
	bool drag_active;
	bool drag_cancelled;
	guint refresh_idle_source;
	guint refresh_delay_source;
	guint64 refresh_serial;
	GCancellable *refresh_cancel;
	GObject *refresh_owner;
	struct color overlay;
	struct color panel;
	struct color panel_alt;
	struct color line;
	struct color line_soft;
	struct color accent;
	struct color accent_soft;
	struct color warm;
	struct color danger;
	struct color text;
	struct color selected_text;
	struct color muted;
	struct label_cache header_title;
	struct label_cache header_count;
	struct label_cache card_titles[MAX_WORKSPACE_CARDS];
	struct label_cache card_summaries[MAX_WORKSPACE_CARDS];
	struct label_cache card_badges[MAX_WORKSPACE_CARDS];
	char header_count_text[64];
	char fps_path[256];
	gint64 fps_window_us;
	int fps_frames;
	double fps;
	gint64 last_draw_us;
	double max_draw_ms;
};

typedef struct { GtkWidget parent_instance; struct overview *overview; } OverviewCanvas;
typedef struct { GtkWidgetClass parent_class; } OverviewCanvasClass;
static void draw(GtkWidget *widget, GtkSnapshot *snapshot);
static void overview_canvas_size_allocate(GtkWidget *widget, int width, int height, int baseline);
G_DEFINE_TYPE(OverviewCanvas, overview_canvas, GTK_TYPE_WIDGET)
static void overview_canvas_class_init(OverviewCanvasClass *klass)
{
	GTK_WIDGET_CLASS(klass)->snapshot = draw;
	GTK_WIDGET_CLASS(klass)->size_allocate = overview_canvas_size_allocate;
	gtk_widget_class_set_accessible_role(GTK_WIDGET_CLASS(klass), GTK_ACCESSIBLE_ROLE_GROUP);
}
static void overview_canvas_init(OverviewCanvas *canvas) { (void)canvas; }

static void ensure_tick(struct overview *overview);
static void free_card_thumbs(struct overview *overview);
static void clear_label_cache(struct overview *overview);
static void schedule_refresh_windows(struct overview *overview);
static double card_width(const struct overview *overview);
static double card_height(const struct overview *overview);
static double card_spacing(const struct overview *overview);
static void close_card(struct overview *overview, int selected);
static void reset_card_dismissal(struct overview *overview);
static void run_sway_argv(char *argv[]);

static void
record_capture_state(struct overview *overview, bool active)
{
	if (!overview->capture_state_path[0])
		return; /* Display-free/native fixtures do not publish session state. */
	char value[128];
	g_snprintf(value, sizeof(value), "%s %ld:%" G_GINT64_FORMAT ":%" G_GUINT64_FORMAT "\n",
	    active ? "active" : "idle", (long)getpid(), g_get_monotonic_time(),
	    ++overview->capture_generation);
	/* Runtime-only state: no fsync in the input path. A partial read is blocked
	 * by the cache; it must observe an unchanged complete idle generation. */
	GError *error = NULL;
	if (!g_file_set_contents_full(overview->capture_state_path, value, -1,
	    G_FILE_SET_CONTENTS_NONE, 0600, &error)) {
		g_warning("cannot record Overview capture state: %s", error->message);
		g_clear_error(&error);
	}
}

static void
notify_capture_hidden(void)
{
	/* Existing Sway IPC wakes the existing cache after unmap, including a
	 * cancelled partial pull that never acquired keyboard focus. */
	char *argv[] = {"swaymsg", "-t", "send_tick", "ctlst-overview-hidden", NULL};
	run_sway_argv(argv);
}

static bool
landscape_controls(const struct overview *overview)
{
	return overview->width >= 640 && overview->width >= overview->height;
}

static double
controls_clearance(const struct overview *overview)
{
	return landscape_controls(overview) ? 0 : OVERVIEW_CONTROLS_HEIGHT;
}

static double
content_top(const struct overview *overview)
{
	/* A short display needs one compact heading band, not portrait margins. */
	return landscape_controls(overview) ? 72 : 108;
}

static void
layout_overview_controls(struct overview *overview)
{
	if (overview->controls == NULL)
		return;
	bool landscape = landscape_controls(overview);
	/* On wide outputs, share the header band instead of squeezing the
	 * preview between two toolbars. Keep the real dock's area untouched. */
	gtk_widget_set_halign(overview->controls, landscape ? GTK_ALIGN_END : GTK_ALIGN_CENTER);
	gtk_widget_set_valign(overview->controls, landscape ? GTK_ALIGN_START : GTK_ALIGN_END);
	gtk_widget_set_margin_top(overview->controls, landscape ? 12 : 0);
	if (overview->done_button != NULL)
		gtk_widget_set_margin_top(overview->done_button, landscape ? 12 : 34);
	gtk_widget_set_margin_bottom(overview->controls, landscape ? 0 : (int)DOCK_CLEARANCE);
	gtk_widget_set_margin_end(overview->controls, landscape ? 104 : 0);
}

static double
clamp(double value, double minimum, double maximum)
{
	if (value < minimum)
		return minimum;
	if (value > maximum)
		return maximum;
	return value;
}

static struct color
hex_color(const char *value, struct color fallback)
{
	char hex[9] = {0};
	char *end = NULL;
	unsigned long rgba;
	size_t length;

	if (value == NULL)
		return fallback;
	while (*value == '#')
		value++;
	length = strlen(value);
	if (length != 6 && length != 8)
		return fallback;
	memcpy(hex, value, length);
	rgba = strtoul(hex, &end, 16);
	if (end == hex || *end != '\0')
		return fallback;
	if (length == 6)
		rgba = (rgba << 8) | 0xff;
	return (struct color){
		.red = ((rgba >> 24) & 0xff) / 255.0,
		.green = ((rgba >> 16) & 0xff) / 255.0,
		.blue = ((rgba >> 8) & 0xff) / 255.0,
		.alpha = (rgba & 0xff) / 255.0,
	};
}

static void
update_overview_controls(struct overview *overview)
{
	char label[48], description[256];
	bool available = overview->data_ready && overview->card_count > 0;
	if (overview->open_button == NULL)
		return;
	gtk_widget_set_visible(overview->controls, overview->committed && available);
	gtk_widget_set_visible(overview->done_button, overview->committed);
	gtk_accessible_update_property(GTK_ACCESSIBLE(overview->area),
	    GTK_ACCESSIBLE_PROPERTY_LABEL, "Recent apps",
	    GTK_ACCESSIBLE_PROPERTY_DESCRIPTION, available ?
	    overview->cards[overview->selected].summary :
	    (overview->refresh_failed ? "Couldn't load apps. Close and reopen to retry." :
	    overview->data_ready ? "No open apps" : "Loading apps"), -1);
	bool interactive = available && !overview->card_dismiss_active;
	gtk_widget_set_sensitive(overview->previous_button, interactive && overview->selected > 0);
	gtk_widget_set_sensitive(overview->next_button,
	    interactive && overview->selected + 1 < overview->card_count);
	gtk_widget_set_sensitive(overview->open_button, interactive);
	gtk_widget_set_sensitive(overview->close_button, interactive);
	g_snprintf(label, sizeof(label), available ? "Open %d of %d" : "No apps",
	    overview->selected + 1, overview->card_count);
	gtk_button_set_label(GTK_BUTTON(overview->open_button), label);
	g_snprintf(description, sizeof(description), "Open %s", available ?
	    overview->cards[overview->selected].summary : "selected task");
	gtk_accessible_update_property(GTK_ACCESSIBLE(overview->open_button),
	    GTK_ACCESSIBLE_PROPERTY_LABEL, label,
	    GTK_ACCESSIBLE_PROPERTY_DESCRIPTION, description, -1);
	g_snprintf(description, sizeof(description), "Close task (%d windows)",
	    available ? overview->cards[overview->selected].id_count : 0);
	gtk_widget_set_tooltip_text(overview->close_button, description);
	gtk_accessible_update_property(GTK_ACCESSIBLE(overview->close_button),
	    GTK_ACCESSIBLE_PROPERTY_LABEL, description, -1);
}

static void
update_chrome_style(struct overview *overview)
{
	char *css = g_strdup_printf(
	    "window#ctlstoverview button { min-width: 44px; min-height: 44px;"
	    " padding: 0; border: 0; border-radius: 22px; font-size: 14px;"
	    " font-weight: 500; box-shadow: none; background: rgb(%d,%d,%d);"
	    " color: rgb(%d,%d,%d); }"
	    "window#ctlstoverview button.text-button { padding: 0 10px; }"
	    "window#ctlstoverview button:hover { background: rgb(%d,%d,%d); }"
	    "window#ctlstoverview button:disabled { opacity: 0.35; }"
	    "window#ctlstoverview button:focus-visible { outline: 2px solid rgb(%d,%d,%d);"
	    " outline-offset: -3px; }",
	    (int)(overview->panel_alt.red * 255), (int)(overview->panel_alt.green * 255),
	    (int)(overview->panel_alt.blue * 255),
	    (int)(overview->text.red * 255), (int)(overview->text.green * 255),
	    (int)(overview->text.blue * 255),
	    (int)(overview->accent_soft.red * 255), (int)(overview->accent_soft.green * 255),
	    (int)(overview->accent_soft.blue * 255),
	    (int)(overview->accent.red * 255), (int)(overview->accent.green * 255),
	    (int)(overview->accent.blue * 255));
	if (overview->chrome_css == NULL) {
		overview->chrome_css = gtk_css_provider_new();
		gtk_style_context_add_provider_for_display(gdk_display_get_default(),
		    GTK_STYLE_PROVIDER(overview->chrome_css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	}
	gtk_css_provider_load_from_string(overview->chrome_css, css);
	g_free(css);
}

static void
load_theme(struct overview *overview)
{
	char *path = ctlst_config_path("theme.env");
	char *contents = NULL;
	char **lines;

	overview->overlay = (struct color){0.05, 0.06, 0.08, 0.52};
	overview->panel = (struct color){1.0, 0.992, 0.969, 0.98};
	overview->panel_alt = (struct color){0.91, 0.914, 0.890, 1.0};
	overview->line = (struct color){0.78, 0.788, 0.761, 1.0};
	overview->line_soft = (struct color){0.894, 0.898, 0.875, 1.0};
	overview->accent = (struct color){0.208, 0.373, 0.443, 1.0};
	overview->accent_soft = (struct color){0.80, 0.898, 0.941, 1.0};
	overview->warm = (struct color){0.482, 0.369, 0.482, 1.0};
	overview->danger = (struct color){0.729, 0.102, 0.102, 0.95};
	overview->text = (struct color){0.106, 0.110, 0.102, 1.0};
	overview->selected_text = (struct color){1.0, 0.992, 0.969, 1.0};
	overview->muted = (struct color){0.271, 0.278, 0.275, 1.0};
	if (!g_file_get_contents(path, &contents, NULL, NULL)) {
		g_free(path);
		update_chrome_style(overview);
		return;
	}
	lines = g_strsplit(contents, "\n", 0);
	for (int i = 0; lines[i] != NULL; i++) {
		char **pair = g_strsplit(lines[i], "=", 2);

		if (pair[0] == NULL || pair[1] == NULL) {
			g_strfreev(pair);
			continue;
		}
		if (strcmp(pair[0], "SHELL_OVERLAY") == 0)
			overview->overlay = hex_color(pair[1], overview->overlay);
		else if (strcmp(pair[0], "SHELL_PANEL") == 0)
			overview->panel = hex_color(pair[1], overview->panel);
		else if (strcmp(pair[0], "SHELL_LINE") == 0)
			overview->line = hex_color(pair[1], overview->line);
		else if (strcmp(pair[0], "SHELL_LINE_SOFT") == 0)
			overview->line_soft = hex_color(pair[1], overview->line_soft);
		else if (strcmp(pair[0], "SHELL_PANEL_ALT") == 0)
			overview->panel_alt = hex_color(pair[1], overview->panel_alt);
		else if (strcmp(pair[0], "SHELL_ACCENT") == 0)
			overview->accent = hex_color(pair[1], overview->accent);
		else if (strcmp(pair[0], "SHELL_ACCENT_SOFT") == 0)
			overview->accent_soft = hex_color(pair[1],
			    overview->accent_soft);
		else if (strcmp(pair[0], "SHELL_WARM") == 0)
			overview->warm = hex_color(pair[1], overview->warm);
		else if (strcmp(pair[0], "SHELL_DANGER") == 0)
			overview->danger = hex_color(pair[1], overview->danger);
		else if (strcmp(pair[0], "SHELL_TEXT") == 0)
			overview->text = hex_color(pair[1], overview->text);
		else if (strcmp(pair[0], "SHELL_SELECTED_TEXT") == 0)
			overview->selected_text = hex_color(pair[1],
			    overview->selected_text);
		else if (strcmp(pair[0], "SHELL_MUTED") == 0)
			overview->muted = hex_color(pair[1], overview->muted);
		g_strfreev(pair);
	}
	g_strfreev(lines);
	g_free(contents);
	g_free(path);
	update_chrome_style(overview);
}

static void
set_source(cairo_t *cr, struct color color, double alpha)
{
	cairo_set_source_rgba(cr, color.red, color.green, color.blue,
	    color.alpha * alpha);
}

static void
rounded_rectangle(cairo_t *cr, double x, double y, double width,
    double height, double radius)
{
	double right = x + width;
	double bottom = y + height;

	cairo_new_sub_path(cr);
	cairo_arc(cr, right - radius, y + radius, radius, -G_PI_2, 0);
	cairo_arc(cr, right - radius, bottom - radius, radius, 0, G_PI_2);
	cairo_arc(cr, x + radius, bottom - radius, radius, G_PI_2, G_PI);
	cairo_arc(cr, x + radius, y + radius, radius, G_PI, 3 * G_PI_2);
	cairo_close_path(cr);
}

static void
draw_text(cairo_t *cr, const char *text, double x, double y, double width,
    const char *font, struct color color, double alpha,
    PangoEllipsizeMode ellipsize)
{
	PangoLayout *layout = pango_cairo_create_layout(cr);
	PangoFontDescription *description = pango_font_description_from_string(font);

	pango_layout_set_font_description(layout, description);
	pango_layout_set_text(layout, text != NULL ? text : "", -1);
	pango_layout_set_width(layout, (int)(width * PANGO_SCALE));
	pango_layout_set_ellipsize(layout, ellipsize);
	pango_layout_set_single_paragraph_mode(layout, TRUE);
	set_source(cr, color, alpha);
	cairo_move_to(cr, x, y);
	pango_cairo_show_layout(cr, layout);
	pango_font_description_free(description);
	g_object_unref(layout);
}

static void
release_label_cache(struct label_cache *cache)
{
	if (cache->layout != NULL) {
		g_object_unref(cache->layout);
		cache->layout = NULL;
	}
	cache->text[0] = '\0';
	cache->font[0] = '\0';
	cache->width = 0;
}

static void
clear_label_cache(struct overview *overview)
{
	release_label_cache(&overview->empty_label);
	for (int i = 0; i < MAX_WORKSPACE_CARDS; i++)
		g_clear_object(&overview->card_textures[i]);
	release_label_cache(&overview->header_title);
	release_label_cache(&overview->header_count);
	for (int i = 0; i < MAX_WORKSPACE_CARDS; i++) {
		release_label_cache(&overview->card_titles[i]);
		release_label_cache(&overview->card_summaries[i]);
		release_label_cache(&overview->card_badges[i]);
	}
	overview->header_count_text[0] = '\0';
}

static void
ensure_label_cache(cairo_t *cr, struct label_cache *cache, const char *text,
    const char *font, int width, PangoEllipsizeMode ellipsize)
{
	const char *safe = text != NULL ? text : "";

	if (cache->layout != NULL && strcmp(cache->text, safe) == 0 &&
	    strcmp(cache->font, font) == 0 && cache->width == width)
		return;
	release_label_cache(cache);
	g_strlcpy(cache->text, safe, sizeof(cache->text));
	g_strlcpy(cache->font, font, sizeof(cache->font));
	cache->width = width;
	cache->layout = pango_cairo_create_layout(cr);
	{
		PangoFontDescription *description =
		    pango_font_description_from_string(font);

		pango_layout_set_font_description(cache->layout, description);
		pango_font_description_free(description);
	}
	pango_layout_set_text(cache->layout, safe, -1);
	pango_layout_set_width(cache->layout, width * PANGO_SCALE);
	pango_layout_set_ellipsize(cache->layout, ellipsize);
	pango_layout_set_single_paragraph_mode(cache->layout, TRUE);
}

static void
draw_cached_label(cairo_t *cr, struct label_cache *cache, double x, double y,
    struct color color, double alpha)
{
	if (cache->layout == NULL)
		return;
	set_source(cr, color, alpha);
	cairo_move_to(cr, x, y);
	pango_cairo_show_layout(cr, cache->layout);
}

static void
rebuild_label_cache(struct overview *overview, cairo_t *cr)
{
	int header_width = overview->width - (landscape_controls(overview) ? 440 : 136);

	clear_label_cache(overview);
	ensure_label_cache(cr, &overview->header_title, "Recent apps",
	    landscape_controls(overview) ? "Noto Sans Semi-Bold 18" : "Noto Sans Semi-Bold 22",
	    header_width, PANGO_ELLIPSIZE_END);
	g_snprintf(overview->header_count_text, sizeof(overview->header_count_text),
	    "%d task%s", overview->card_count, overview->card_count == 1 ? "" : "s");
	if (!overview->data_ready)
		g_strlcpy(overview->header_count_text,
		    overview->refresh_failed ? "Reopen to retry" : "Loading…",
		    sizeof(overview->header_count_text));
	ensure_label_cache(cr, &overview->header_count, overview->header_count_text,
	    "Noto Sans 11", header_width, PANGO_ELLIPSIZE_END);
	ensure_label_cache(cr, &overview->empty_label,
	    overview->refresh_failed ? "Couldn't load apps" :
	    overview->data_ready ? "No open apps" : "Loading apps…",
	    "Noto Sans 14", header_width, PANGO_ELLIPSIZE_END);
	for (int i = 0; i < overview->card_count; i++) {
		struct workspace_card *card = &overview->cards[i];
		char count[32];
		int card_w = (int)card_width(overview);

		ensure_label_cache(cr, &overview->card_titles[i], card->summary,
		    "Noto Sans Medium 13", card_w - 72, PANGO_ELLIPSIZE_END);
		g_snprintf(count, sizeof(count), "%d window%s",
		    card->id_count, card->id_count == 1 ? "" : "s");
		ensure_label_cache(cr, &overview->card_summaries[i], count,
		    "Noto Sans 9", card_w - 72, PANGO_ELLIPSIZE_END);
	}
}

static const char *
json_string(struct json_object *object, const char *name)
{
	struct json_object *value;

	if (!json_object_object_get_ex(object, name, &value) ||
	    json_object_is_type(value, json_type_null))
		return "";
	return json_object_get_string(value);
}

static bool
shell_window(const char *app_id)
{
	static const char *excluded[] = {
		"dev.ctlst.Drawer", "dev.ctlst.Overview", "dev.ctlst.Pad",
		"dev.ctlst.Lock", "dev.ctlst.Action", "dev.ctlst.Shade",
		"waybar", "wvkbd", "ctlstshade", "window-drop-overlay",
		"ctlsthome",
	};

	for (size_t i = 0; i < G_N_ELEMENTS(excluded); i++) {
		if (strstr(app_id, excluded[i]) != NULL)
			return true;
	}
	return false;
}

static bool
is_home_workspace(const char *name)
{
	char *end = NULL;
	long number;

	if (name == NULL || name[0] == '\0')
		return false;
	if (g_ascii_strcasecmp(name, "home") == 0)
		return true;
	if (strncmp(name, "__", 2) == 0)
		return true;
	number = strtol(name, &end, 10);
	return end != name && *end == '\0' && number == 1;
}

static void
workspace_cache_key(const char *workspace, char *out, size_t out_size)
{
	const unsigned char *bytes =
	    (const unsigned char *)(workspace != NULL ? workspace : "");
	uint64_t hash = UINT64_C(1469598103934665603);
	size_t o = 0;

	if (out_size == 0)
		return;
	for (const unsigned char *p = bytes; *p != '\0'; p++) {
		hash ^= *p;
		hash *= UINT64_C(1099511628211);
	}
	if (o + 3 < out_size) {
		out[o++] = 'w';
		out[o++] = 's';
		out[o++] = '-';
	}
	for (const char *p = workspace != NULL ? workspace : "";
	    *p != '\0' && o < 56 && o + 1 < out_size; p++) {
		unsigned char ch = (unsigned char)*p;

		if (g_ascii_isalnum(ch) || ch == '.' || ch == '_' || ch == '-')
			out[o++] = (char)ch;
		else
			out[o++] = '_';
	}
	if (o == 3 && o + 7 < out_size) {
		memcpy(out + o, "unknown", 7);
		o += 7;
	}
	out[o] = '\0';
	g_snprintf(out + o, out_size - o, "-%016" PRIx64, hash);
}

static void
copy_app_label(char *out, size_t out_size, const char *text)
{
	if (out_size == 0)
		return;
	char *valid = g_utf8_make_valid(text, -1);
	/* Card headings stay single-line, including user-authored desktop names. */
	for (char *p = valid; *p; p++)
		if ((unsigned char)*p < 0x20 || *p == 0x7f)
			*p = ' ';
	size_t length = MIN(strlen(valid), out_size - 1);
	const char *end = NULL;
	/* The byte budget must never cut a translated name inside a codepoint. */
	if (!g_utf8_validate(valid, (gssize)length, &end))
		length = (size_t)(end - valid);
	memcpy(out, valid, length);
	out[length] = '\0';
	g_free(valid);
}

static void
short_app_label(const char *app_id, char *out, size_t out_size)
{
	if (out_size == 0)
		return;
	if (app_id == NULL || app_id[0] == '\0') {
		copy_app_label(out, out_size, "App");
		return;
	}
	/* Match the drawer's localized display name and XDG user overrides.
	 * This runs only while rebuilding cards, never in the render/motion path.
	 * Window IDs are identifiers, not arbitrary desktop-file paths. */
	if (g_utf8_validate(app_id, -1, NULL) &&
	    strchr(app_id, '/') == NULL && strchr(app_id, '\\') == NULL) {
		char *desktop_id = g_str_has_suffix(app_id, ".desktop") ?
		    g_strdup(app_id) : g_strconcat(app_id, ".desktop", NULL);
		GDesktopAppInfo *desktop = g_desktop_app_info_new(desktop_id);
		g_free(desktop_id);
		if (desktop != NULL) {
			const char *name = g_app_info_get_display_name(G_APP_INFO(desktop));
			if (name != NULL && name[0] != '\0') {
				copy_app_label(out, out_size, name);
				g_object_unref(desktop);
				return;
			}
			g_object_unref(desktop);
		}
	}
	/* Unregistered apps keep a readable ID fallback, without ".desktop". */
	char *identifier = g_strdup(app_id);
	if (g_str_has_suffix(identifier, ".desktop"))
		identifier[strlen(identifier) - strlen(".desktop")] = '\0';
	const char *base = strrchr(identifier, '.');
	base = base != NULL && base[1] != '\0' ? base + 1 : identifier;
	copy_app_label(out, out_size, *base ? base : "App");
	if (out[0] != '\0')
		out[0] = (char)g_ascii_toupper((guchar)out[0]);
	g_free(identifier);
}

static void
build_summary(struct workspace_card *card, const struct leaf_window *leaves,
    int leaf_count, const char *workspace)
{
	char seen[MAX_CONS_PER_CARD][128];
	int seen_count = 0;
	int used = 0;

	card->summary[0] = '\0';
	card->app_count = 0;
	for (int i = 0; i < leaf_count; i++) {
		char label[64];
		bool already = false;

		if (strcmp(leaves[i].workspace, workspace) != 0)
			continue;
		for (int s = 0; s < seen_count; s++) {
			if (strcmp(seen[s], leaves[i].app_id) == 0) {
				already = true;
				break;
			}
		}
		if (already)
			continue;
		if (seen_count < MAX_CONS_PER_CARD)
			g_strlcpy(seen[seen_count++], leaves[i].app_id, sizeof(seen[0]));
		card->app_count++;
		if (used >= 3)
			continue;
		short_app_label(leaves[i].app_id, label, sizeof(label));
		if (used > 0) {
			size_t offset = strlen(card->summary);
			copy_app_label(card->summary + offset, sizeof(card->summary) - offset, " · ");
		}
		size_t offset = strlen(card->summary);
		copy_app_label(card->summary + offset, sizeof(card->summary) - offset, label);
		used++;
	}
	if (card->app_count > 3) {
		char more[32];

		g_snprintf(more, sizeof(more), " +%d", card->app_count - 3);
		copy_app_label(card->summary, sizeof(card->summary) - strlen(more), card->summary);
		g_strlcat(card->summary, more, sizeof(card->summary));
	}
	if (card->summary[0] == '\0')
		g_strlcpy(card->summary, "Apps", sizeof(card->summary));
}

static void
workspace_display_label(const char *workspace, char *out, size_t out_size)
{
	char *end = NULL;
	long number = strtol(workspace, &end, 10);

	if (end != workspace && *end == '\0' && number >= 2 && number <= 5)
		g_snprintf(out, out_size, "Workspace %ld", number - 1);
	else if (end != workspace && *end == '\0')
		g_snprintf(out, out_size, "Workspace %ld", number);
	else
		g_snprintf(out, out_size, "Workspace %s", workspace);
}

static void
collect_leaves(struct leaf_window *leaves, int *leaf_count,
    struct json_object *node, const char *workspace)
{
	static const char *groups[] = {"nodes", "floating_nodes"};
	struct json_object *children;
	const char *type = json_string(node, "type");
	const char *current_workspace = workspace;
	const char *app_id = json_string(node, "app_id");
	const char *title = json_string(node, "name");
	struct json_object *properties;
	struct json_object *value;
	bool has_children = false;

	if (strcmp(type, "workspace") == 0)
		current_workspace = title;
	for (size_t group = 0; group < G_N_ELEMENTS(groups); group++) {
		if (json_object_object_get_ex(node, groups[group], &children) &&
		    json_object_is_type(children, json_type_array) &&
		    json_object_array_length(children) > 0)
			has_children = true;
	}
	/* Sway's floating app leaves use floating_con, not a con wrapper. */
	if ((strcmp(type, "con") == 0 || strcmp(type, "floating_con") == 0) &&
	    !has_children &&
	    *leaf_count < MAX_LEAF_WINDOWS) {
		if (app_id[0] == '\0' && json_object_object_get_ex(node,
		    "window_properties", &properties)) {
			if (json_object_object_get_ex(properties, "class", &value))
				app_id = json_object_get_string(value);
		}
		if ((app_id[0] != '\0' || title[0] != '\0') &&
		    !shell_window(app_id) &&
		    !is_home_workspace(current_workspace)) {
			struct leaf_window *leaf = &leaves[(*leaf_count)++];
			struct json_object *focused;
			struct json_object *id;

			memset(leaf, 0, sizeof(*leaf));
			if (json_object_object_get_ex(node, "id", &id))
				leaf->id = json_object_get_int64(id);
			if (json_object_object_get_ex(node, "focused", &focused))
				leaf->focused = json_object_get_boolean(focused);
			g_strlcpy(leaf->app_id,
			    app_id[0] != '\0' ? app_id : "Application",
			    sizeof(leaf->app_id));
			g_strlcpy(leaf->title,
			    title[0] != '\0' ? title : leaf->app_id,
			    sizeof(leaf->title));
			g_strlcpy(leaf->workspace,
			    current_workspace != NULL ? current_workspace : "?",
			    sizeof(leaf->workspace));
		}
	}
	for (size_t group = 0; group < G_N_ELEMENTS(groups); group++) {
		if (!json_object_object_get_ex(node, groups[group], &children) ||
		    !json_object_is_type(children, json_type_array))
			continue;
		for (size_t i = 0; i < json_object_array_length(children); i++)
			collect_leaves(leaves, leaf_count,
			    json_object_array_get_idx(children, i),
			    current_workspace);
	}
}

static void
free_card_thumbs(struct overview *overview)
{
	for (int i = 0; i < MAX_WORKSPACE_CARDS; i++) {
		if (overview->cards[i].thumb != NULL) {
			cairo_surface_destroy(overview->cards[i].thumb);
			overview->cards[i].thumb = NULL;
		}
	}
}

static void
load_card_thumb(struct overview *overview, struct workspace_card *card)
{
	char path[768];
	cairo_surface_t *surface;

	if (overview->cache_dir[0] == '\0' || card->cache_key[0] == '\0')
		return;
	g_snprintf(path, sizeof(path), "%s/%s.png", overview->cache_dir,
	    card->cache_key);
	if (access(path, R_OK) != 0)
		return;
	surface = cairo_image_surface_create_from_png(path);
	if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(surface);
		return;
	}
	card->thumb = surface;
}

static void
build_workspace_cards(struct overview *overview, struct leaf_window *leaves,
    int leaf_count)
{
	overview->card_count = 0;
	for (int i = 0; i < leaf_count; i++) {
		struct workspace_card *card = NULL;
		const char *ws = leaves[i].workspace;

		if (is_home_workspace(ws))
			continue;
		for (int c = 0; c < overview->card_count; c++) {
			if (strcmp(overview->cards[c].workspace, ws) == 0) {
				card = &overview->cards[c];
				break;
			}
		}
		if (card == NULL) {
			if (overview->card_count >= MAX_WORKSPACE_CARDS)
				continue;
			card = &overview->cards[overview->card_count++];
			memset(card, 0, sizeof(*card));
			g_strlcpy(card->workspace, ws, sizeof(card->workspace));
			workspace_cache_key(ws, card->cache_key,
			    sizeof(card->cache_key));
			workspace_display_label(ws, card->label,
			    sizeof(card->label));
		}
		if (card->id_count < MAX_CONS_PER_CARD)
			card->ids[card->id_count++] = leaves[i].id;
		if (leaves[i].focused)
			card->focused = true;
	}
	for (int c = 0; c < overview->card_count; c++) {
		build_summary(&overview->cards[c], leaves, leaf_count,
		    overview->cards[c].workspace);
		load_card_thumb(overview, &overview->cards[c]);
	}
	/* Focused workspace card first for carousel orientation */
	for (int i = 0; i < overview->card_count; i++) {
		if (!overview->cards[i].focused)
			continue;
		if (i != 0) {
			struct workspace_card temporary = overview->cards[0];

			overview->cards[0] = overview->cards[i];
			overview->cards[i] = temporary;
		}
		break;
	}
}

/* A worker owns only its snapshot's cards/cache path. It never accesses live
 * GTK, Pango or GdkTexture objects. Cairo image ownership transfers on completion. */
struct refresh_job {
	struct overview snapshot;
	guint64 serial;
};

static gboolean refresh_windows_idle(gpointer data);

static void
queue_refresh_windows(struct overview *overview)
{
	if (overview->refresh_scheduled && !overview->refresh_running &&
	    !overview->refresh_idle_source && overview->mapped &&
	    !overview->hide_pending && !overview->card_dismiss_active &&
	    !overview->drag_active)
		overview->refresh_idle_source = g_idle_add(refresh_windows_idle, overview);
}

static void
cancel_refresh_windows(struct overview *overview)
{
	overview->refresh_serial++;
	overview->refresh_scheduled = false;
	if (overview->refresh_idle_source) {
		g_source_remove(overview->refresh_idle_source);
		overview->refresh_idle_source = 0;
	}
	if (overview->refresh_delay_source) {
		g_source_remove(overview->refresh_delay_source);
		overview->refresh_delay_source = 0;
	}
	if (overview->refresh_cancel)
		g_cancellable_cancel(overview->refresh_cancel);
}

static void
free_refresh_job(gpointer data)
{
	struct refresh_job *job = data;
	free_card_thumbs(&job->snapshot);
	g_free(job);
}

static void
refresh_windows_worker(GTask *task, gpointer source, gpointer data,
    GCancellable *cancellable)
{
	struct refresh_job *job = data;
	GError *error = NULL;
	char *output = NULL;
	(void)source;
	if (g_task_return_error_if_cancelled(task))
		return;
	GSubprocess *process = g_subprocess_new(
	    G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE,
	    &error, "swaymsg", "-r", "-t", "get_tree", NULL);
	if (!process) {
		g_task_return_error(task, error);
		return;
	}
	bool ok = g_subprocess_communicate_utf8(process, NULL, cancellable,
	    &output, NULL, &error);
	if (!ok) {
		/* communicate cancellation does not terminate the child itself. */
		g_subprocess_force_exit(process);
		g_subprocess_wait(process, NULL, NULL);
	} else if (!g_subprocess_get_successful(process)) {
		g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_FAILED,
		    "Sway tree query failed");
		ok = false;
	}
	g_object_unref(process);
	if (ok && !g_cancellable_is_cancelled(cancellable)) {
		struct json_object *tree = json_tokener_parse(output ? output : "");
		if (tree && json_object_is_type(tree, json_type_object) &&
		    strcmp(json_string(tree, "type"), "root") == 0) {
			struct leaf_window leaves[MAX_LEAF_WINDOWS];
			int count = 0;
			collect_leaves(leaves, &count, tree, "?");
			build_workspace_cards(&job->snapshot, leaves, count);
		} else {
			g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
			    "Invalid Sway tree response");
		}
		if (tree)
			json_object_put(tree);
	}
	g_free(output);
	if (error)
		g_task_return_error(task, error);
	else
		g_task_return_boolean(task, TRUE);
}

static void
refresh_windows_finished(GObject *owner, GAsyncResult *result, gpointer data)
{
	struct overview *overview = g_object_get_data(owner, "overview");
	GTask *task = G_TASK(result);
	struct refresh_job *job = g_task_get_task_data(task);
	GError *error = NULL;
	bool ok = g_task_propagate_boolean(task, &error);
	(void)data;
	g_clear_error(&error);
	/* Shutdown detaches the stack-owned UI; late callbacks just free the job. */
	if (!overview)
		return;
	overview->refresh_running = false;
	g_clear_object(&overview->refresh_cancel);
	if (job->serial == overview->refresh_serial && overview->mapped &&
	    !overview->hide_pending) {
		if (overview->card_dismiss_active || overview->drag_active) {
			/* Never replace a card's close targets beneath an active gesture. */
			overview->refresh_scheduled = true;
		} else {
			free_card_thumbs(overview);
			clear_label_cache(overview);
			memcpy(overview->cards, job->snapshot.cards, sizeof(overview->cards));
			memset(job->snapshot.cards, 0, sizeof(job->snapshot.cards));
			overview->card_count = job->snapshot.card_count;
			overview->selected = 0;
			overview->target_offset = overview->display_offset = 0;
			overview->data_ready = ok;
			overview->refresh_failed = !ok;
			update_overview_controls(overview);
			gtk_widget_queue_draw(overview->area);
		}
	}
	queue_refresh_windows(overview);
}

static gboolean
refresh_windows_idle(gpointer data)
{
	struct overview *overview = data;
	overview->refresh_idle_source = 0;
	if (!overview->mapped || overview->hide_pending || overview->drag_active ||
	    overview->card_dismiss_active)
		return G_SOURCE_REMOVE;
	overview->refresh_scheduled = false;
	overview->refresh_running = true;
	overview->data_ready = false;
	overview->refresh_failed = false;
	clear_label_cache(overview);
	update_overview_controls(overview);
	gtk_widget_queue_draw(overview->area);
	if (!overview->refresh_owner) {
		overview->refresh_owner = g_object_new(G_TYPE_OBJECT, NULL);
		g_object_set_data(overview->refresh_owner, "overview", overview);
	}
	overview->refresh_cancel = g_cancellable_new();
	struct refresh_job *job = g_new0(struct refresh_job, 1);
	job->serial = overview->refresh_serial;
	g_strlcpy(job->snapshot.cache_dir, overview->cache_dir, sizeof(job->snapshot.cache_dir));
	GTask *task = g_task_new(overview->refresh_owner, overview->refresh_cancel,
	    refresh_windows_finished, NULL);
	g_task_set_task_data(task, job, free_refresh_job);
	g_task_run_in_thread(task, refresh_windows_worker);
	g_object_unref(task);
	return G_SOURCE_REMOVE;
}

static void
schedule_refresh_windows(struct overview *overview)
{
	if (overview->refresh_scheduled)
		return;
	overview->refresh_serial++;
	overview->refresh_scheduled = true;
	if (overview->refresh_cancel)
		g_cancellable_cancel(overview->refresh_cancel);
	queue_refresh_windows(overview);
}

static gboolean
refresh_windows_once(gpointer data)
{
	struct overview *overview = data;
	overview->refresh_delay_source = 0;
	schedule_refresh_windows(overview);
	return G_SOURCE_REMOVE;
}

static double
card_width(const struct overview *overview)
{
	if (landscape_controls(overview))
		return clamp(overview->width * 0.36, 220, 420);
	return overview->width < overview->height ?
	    overview->width * 0.82 : overview->width * 0.44;
}

static double
card_height(const struct overview *overview)
{
	double available = fmax(64, overview->height - DOCK_CLEARANCE - controls_clearance(overview) - content_top(overview) - 4);

	return overview->width < overview->height ? available * 0.90 : available * 0.94;
}

static double
content_center_y(const struct overview *overview)
{
	double top = content_top(overview);
	return top + (overview->height - DOCK_CLEARANCE - controls_clearance(overview) - top) / 2.0;
}

static struct color
card_panel_color(const struct overview *overview)
{
	struct color panel = overview->panel;

	panel.alpha = 1.0;
	return panel;
}

static double
card_spacing(const struct overview *overview)
{
	return card_width(overview) + 20;
}

static void
paint_thumb(cairo_t *cr, cairo_surface_t *thumb, double x, double y,
    double width, double height, double radius, double alpha)
{
	double sw = cairo_image_surface_get_width(thumb);
	double sh = cairo_image_surface_get_height(thumb);
	double scale;
	double dw;
	double dh;
	double ox;
	double oy;

	if (sw < 1 || sh < 1)
		return;
	scale = fmin(width / sw, height / sh);
	dw = sw * scale;
	dh = sh * scale;
	ox = x + (width - dw) / 2.0;
	oy = y + (height - dh) / 2.0;
	cairo_save(cr);
	rounded_rectangle(cr, x, y, width, height, radius);
	cairo_clip(cr);
	cairo_translate(cr, ox, oy);
	cairo_scale(cr, scale, scale);
	cairo_set_source_surface(cr, thumb, 0, 0);
	cairo_paint_with_alpha(cr, alpha);
	cairo_restore(cr);
}

static GdkTexture *
bake_card(struct overview *overview, int index)
{
	struct workspace_card *card = &overview->cards[index];
	double width = card_width(overview), height = card_height(overview);
	double preview_y = card->id_count > 1 ? 60 : 48;
	int scale = gtk_widget_get_scale_factor(overview->area);
	cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
	    (int)ceil(width * scale), (int)ceil(height * scale));
	cairo_t *cr = cairo_create(surface);
	GdkTexture *texture;
	GBytes *bytes;
	int stride;
	char initial[8] = {0};

	/* Rasterize static chrome/preview once; animation only moves the texture. */
	cairo_scale(cr, scale, scale);
	set_source(cr, card_panel_color(overview), 1);
	rounded_rectangle(cr, 0, 0, width, height, 22);
	cairo_fill(cr);
	set_source(cr, overview->line, 0.45);
	rounded_rectangle(cr, 0.5, 0.5, width - 1, height - 1, 22);
	cairo_set_line_width(cr, 1);
	cairo_stroke(cr);
	draw_cached_label(cr, &overview->card_titles[index], 16, card->id_count > 1 ? 10 : 15,
	    overview->text, 1);
	if (card->id_count > 1)
		draw_cached_label(cr, &overview->card_summaries[index], 16, 33,
		    overview->muted, 1);

	/* Quiet, large close target; keep its 52x48 hitbox out of the preview. */
	set_source(cr, overview->panel_alt, 1);
	cairo_arc(cr, width - 26, 26, 16, 0, 2 * G_PI);
	cairo_fill(cr);
	set_source(cr, overview->muted, 1);
	cairo_set_line_width(cr, 1.8);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_move_to(cr, width - 30, 22);
	cairo_line_to(cr, width - 22, 30);
	cairo_move_to(cr, width - 22, 22);
	cairo_line_to(cr, width - 30, 30);
	cairo_stroke(cr);

	set_source(cr, overview->panel_alt, 1);
	rounded_rectangle(cr, 8, preview_y, width - 16, height - preview_y - 8, 16);
	cairo_fill(cr);
	if (card->thumb != NULL) {
		paint_thumb(cr, card->thumb, 8, preview_y, width - 16,
		    height - preview_y - 8, 16, 1);
	} else {
		gunichar first = g_utf8_get_char_validated(card->summary, -1);
		if (first == (gunichar)-1 || first == (gunichar)-2 || first == 0)
			first = 'A';
		g_unichar_to_utf8(g_unichar_toupper(first), initial);
		draw_text(cr, initial, width / 2 - 16, height / 2, 40,
		    "Noto Sans Bold 24", overview->muted, 1, PANGO_ELLIPSIZE_NONE);
	}
	cairo_destroy(cr);
	cairo_surface_flush(surface);
	stride = cairo_image_surface_get_stride(surface);
	bytes = g_bytes_new(cairo_image_surface_get_data(surface),
	    (gsize)stride * cairo_image_surface_get_height(surface));
	/* GDK_MEMORY_DEFAULT matches native-endian premultiplied Cairo ARGB32. */
	texture = gdk_memory_texture_new(cairo_image_surface_get_width(surface),
	    cairo_image_surface_get_height(surface), GDK_MEMORY_DEFAULT, bytes, stride);
	g_bytes_unref(bytes);
	cairo_surface_destroy(surface);
	return texture;
}

static void
draw_card(GtkSnapshot *snapshot, struct overview *overview, int index,
    double center_x, double center_y, double position, double progress)
{
	double distance = fabs(position);
	double scale = clamp(1.0 - distance * 0.08, 0.84, 1.0);
	double width = card_width(overview) * scale;
	double height = card_height(overview) * scale;
	double x = center_x + position * card_spacing(overview) - width / 2.0;
	double dismiss_y = overview->card_dismiss_active &&
	    overview->card_dismiss_index == index ?
	    overview->card_dismiss_display_y : 0;
	double dismiss_amount = clamp(-dismiss_y / fmax(overview->height, 1), 0, 1);
	double rise = (1.0 - progress) * overview->height * 0.22;
	double y = center_y - height / 2.0 + rise + distance * 6 + dismiss_y;
	double alpha = clamp(progress / CARD_FADE_IN_PROGRESS, 0, 1);

	alpha *= 1.0 - clamp((dismiss_amount - 0.12) / 0.76, 0, 1);
	if (x + width < -20 || x > overview->width + 20)
		return;
	if (overview->card_textures[index] == NULL)
		overview->card_textures[index] = bake_card(overview, index);
	gtk_snapshot_push_opacity(snapshot, alpha);
	gtk_snapshot_append_texture(snapshot, overview->card_textures[index],
	    &GRAPHENE_RECT_INIT(x, y, width, height));
	gtk_snapshot_pop(snapshot);
}

static void
note_fps_sample(struct overview *overview, gint64 draw_us)
{
	gint64 now = g_get_monotonic_time();
	double draw_ms = draw_us / 1000.0;
	char line[96];
	int fd;

	overview->last_draw_us = draw_us;
	if (draw_ms > overview->max_draw_ms)
		overview->max_draw_ms = draw_ms;
	overview->fps_frames += 1;
	if (overview->fps_window_us == 0)
		overview->fps_window_us = now;
	if (now - overview->fps_window_us < 400000)
		return;
	overview->fps = overview->fps_frames /
	    ((now - overview->fps_window_us) / 1000000.0);
	overview->fps_frames = 0;
	overview->fps_window_us = now;
	if (overview->fps_path[0] == '\0')
		return;
	g_snprintf(line, sizeof(line),
	    "fps=%.1f draw_ms=%.2f max_draw_ms=%.2f progress=%.2f cards=%d\n",
	    overview->fps, draw_ms, overview->max_draw_ms,
	    overview->display_progress, overview->card_count);
	fd = open(overview->fps_path,
	    O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	if (fd >= 0) {
		size_t length = strlen(line);
		ssize_t written = write(fd, line, length);

		if (written != (ssize_t)length)
			unlink(overview->fps_path);
		close(fd);
	}
	overview->max_draw_ms = 0;
}

static void
snapshot_label(GtkSnapshot *snapshot, struct label_cache *label,
    double x, double y, struct color color)
{
	GdkRGBA rgba = {color.red, color.green, color.blue, color.alpha};

	if (label->layout == NULL)
		return;
	gtk_snapshot_save(snapshot);
	gtk_snapshot_translate(snapshot, &GRAPHENE_POINT_INIT(x, y));
	gtk_snapshot_append_layout(snapshot, label->layout, &rgba);
	gtk_snapshot_restore(snapshot);
}

static void
overview_canvas_size_allocate(GtkWidget *widget, int width, int height, int baseline)
{
	struct overview *overview = ((OverviewCanvas *)widget)->overview;
	(void)baseline;
	if (overview == NULL || (overview->width == width && overview->height == height))
		return;
	overview->width = width;
	overview->height = height;
	/* The overlay allocates its controls after this main child. Never change
	 * their margins during snapshot, which invalidates an in-flight frame. */
	layout_overview_controls(overview);
	clear_label_cache(overview);
	overview->target_offset = -overview->selected * card_spacing(overview);
	overview->display_offset = overview->target_offset;
}

static void
draw(GtkWidget *widget, GtkSnapshot *snapshot)
{
	struct overview *overview = ((OverviewCanvas *)widget)->overview;
	int width = gtk_widget_get_width(widget), height = gtk_widget_get_height(widget);
	int scale = gtk_widget_get_scale_factor(widget);
	double progress = overview->display_progress;
	double spacing, center_y;
	GdkRGBA backdrop = {overview->panel.red, overview->panel.green,
	    overview->panel.blue, progress};
	gint64 draw_started = g_get_monotonic_time();

	if (overview->width != width || overview->height != height ||
	    overview->texture_scale != scale) {
		overview->width = width;
		overview->height = height;
		overview->texture_scale = scale;
		clear_label_cache(overview);
		/* A rotation changes spacing; retain the selected workspace. */
		overview->target_offset = -overview->selected * card_spacing(overview);
		overview->display_offset = overview->target_offset;
	}
	if (overview->header_title.layout == NULL) {
		cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
		cairo_t *cr = cairo_create(surface);
		rebuild_label_cache(overview, cr);
		cairo_destroy(cr);
		cairo_surface_destroy(surface);
	}
	spacing = card_spacing(overview);
	center_y = content_center_y(overview);
	gtk_snapshot_append_color(snapshot, &backdrop, &GRAPHENE_RECT_INIT(0, 0, width, height));
	gtk_snapshot_push_opacity(snapshot, clamp(progress / CARD_FADE_IN_PROGRESS, 0, 1));
	snapshot_label(snapshot, &overview->header_title, 24,
	    landscape_controls(overview) ? 12 : 38, overview->text);
	snapshot_label(snapshot, &overview->header_count, 24,
	    landscape_controls(overview) ? 42 : 72, overview->muted);
	if (!overview->data_ready || overview->card_count == 0) {
		int label_width = 0;
		pango_layout_get_pixel_size(overview->empty_label.layout, &label_width, NULL);
		snapshot_label(snapshot, &overview->empty_label, (width - label_width) / 2.0,
		    center_y, overview->muted);
	} else {
		for (int i = 0; i < overview->card_count; i++) {
			double position = i + overview->display_offset / spacing;

			/* Bound GPU memory to the visible cards and immediate neighbours. */
			if (fabs(position) > 2.0)
				g_clear_object(&overview->card_textures[i]);
			else
				draw_card(snapshot, overview, i, width / 2.0, center_y, position, progress);
		}
	}
	gtk_snapshot_pop(snapshot);
	note_fps_sample(overview, g_get_monotonic_time() - draw_started);
}

static void
notify_gestures(struct overview *overview, char command)
{
	struct sockaddr_un address = {0};
	int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);

	if (fd < 0)
		return;
	address.sun_family = AF_UNIX;
	g_strlcpy(address.sun_path, overview->gesture_path,
	    sizeof(address.sun_path));
	sendto(fd, &command, 1, 0, (struct sockaddr *)&address,
	    sizeof(address));
	close(fd);
}

static void
notify_dock(struct overview *overview, char command)
{
	struct sockaddr_un address = {0};
	int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);

	if (fd < 0 || overview->dock_path[0] == '\0')
		return;
	address.sun_family = AF_UNIX;
	g_strlcpy(address.sun_path, overview->dock_path,
	    sizeof(address.sun_path));
	sendto(fd, &command, 1, 0, (struct sockaddr *)&address,
	    sizeof(address));
	close(fd);
}

static void
set_committed(struct overview *overview, bool committed)
{
	if (overview->committed == committed)
		return;
	overview->committed = committed;
	update_overview_controls(overview);
	if (overview->window != NULL) {
		gtk_layer_set_keyboard_mode(overview->window, committed ?
		    GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE : GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
		if (committed)
			gtk_widget_grab_focus(overview->area);
	}
	if (committed) {
		close(open(overview->state_path,
		    O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600));
		notify_gestures(overview, 'o');
		/* Fully reveal the real dock (home + tasks); no in-overview home. */
		notify_dock(overview, 'E');
	} else {
		unlink(overview->state_path);
		notify_gestures(overview, 'c');
		notify_dock(overview, 'H');
	}
}

static void
ensure_mapped(struct overview *overview)
{
	if (overview->mapped)
		return;
	load_theme(overview);
	overview->mapped = true;
	overview->hide_pending = false;
	record_capture_state(overview, true);
	gtk_widget_set_visible(GTK_WIDGET(overview->window), TRUE);
	gtk_window_present(overview->window);
	schedule_refresh_windows(overview);
}

static void
preview_progress(struct overview *overview, double progress)
{
	bool resuming = overview->hide_pending;
	ensure_mapped(overview);
	overview->target_progress = clamp(progress, 0.06, 1.0);
	overview->hide_pending = false;
	if (resuming)
		schedule_refresh_windows(overview);
	/*
	 * Apply progress immediately for the live pull so batched socket reads
	 * still produce a draw this frame, not only after a timeout tick.
	 */
	if (!overview->committed) {
		overview->display_progress = overview->target_progress;
		if (overview->mapped)
			gtk_widget_queue_draw(GTK_WIDGET(overview->area));
	}
	ensure_tick(overview);
}

static void
show_overview(struct overview *overview)
{
	ensure_mapped(overview);
	overview->hide_pending = false;
	schedule_refresh_windows(overview);
	reset_card_dismissal(overview);
	overview->target_progress = 1.0;
	overview->hide_pending = false;
	set_committed(overview, true);
	ensure_tick(overview);
}

static void
hide_overview(struct overview *overview)
{
	cancel_refresh_windows(overview);
	overview->drag_active = false;
	overview->target_progress = 0;
	overview->hide_pending = true;
	overview->card_dismiss_active = false;
	overview->card_dismiss_settling = false;
	overview->card_dismiss_pending = false;
	overview->card_dismiss_display_y = 0;
	overview->card_dismiss_target_y = 0;
	set_committed(overview, false);
	ensure_tick(overview);
}

static void
reset_card_dismissal(struct overview *overview)
{
	overview->card_dismiss_active = false;
	overview->card_dismiss_settling = false;
	overview->card_dismiss_pending = false;
	overview->card_dismiss_display_y = 0;
	overview->card_dismiss_target_y = 0;
	overview->card_dismiss_start_y = 0;
	overview->card_dismiss_started_us = 0;
	overview->card_dismiss_index = -1;
	update_overview_controls(overview);
	queue_refresh_windows(overview);
}

static void
settle_card_dismissal(struct overview *overview, bool commit)
{
	double offscreen;

	if (!overview->card_dismiss_active || overview->card_dismiss_index < 0)
		return;
	offscreen = -(overview->height > 1 ? overview->height * 1.10 :
	    CARD_DISMISS_DRAG_PX * 4.0);
	overview->card_dismiss_start_y = overview->card_dismiss_display_y;
	overview->card_dismiss_target_y = commit ? offscreen : 0;
	overview->card_dismiss_pending = commit;
	overview->card_dismiss_settling = true;
	overview->card_dismiss_started_us = g_get_monotonic_time();
	update_overview_controls(overview);
	ensure_tick(overview);
}

/* Keep release motion separate from the frame callback's lifetime/backend
 * work. GtkSettings is the shared preference, not a per-component switch. */
static bool
advance_motion(struct overview *overview, gint64 now)
{
	gboolean animations = TRUE;
	GtkSettings *settings = gtk_widget_get_settings(overview->area);
	g_object_get(settings,
	    "gtk-enable-animations", &animations, NULL);
	/* GTK 4.22 adds the system reduced-motion preference. Runtime discovery
	 * keeps older GTK installations usable without probing an absent property.
	 * GtkReducedMotion's zero value is NO_PREFERENCE; nonzero requests reduction. */
	if (g_object_class_find_property(G_OBJECT_GET_CLASS(settings),
	    "gtk-interface-reduced-motion") != NULL) {
		int reduced_motion = 0;
		g_object_get(settings, "gtk-interface-reduced-motion", &reduced_motion, NULL);
		animations = animations && reduced_motion == 0;
	}
	double dt = overview->last_tick_us > 0 ?
	    (now - overview->last_tick_us) / 1000000.0 : 0.016;
	double progress_delta = overview->target_progress -
	    overview->display_progress;
	double offset_delta = overview->target_offset -
	    overview->display_offset;
	/*
	 * Progressive bottom-edge open must track the finger 1:1. Easing during
	 * the live pull is what made multitasking feel a frame behind the thumb.
	 * Only ease after commit (snap open) or hide settle.
	 */
	bool progressive_pull = overview->mapped && !overview->committed &&
	    !overview->hide_pending;
	bool card_dismiss_finished = false;
	overview->last_tick_us = now;
	if (progressive_pull || !animations) {
		overview->display_progress = overview->target_progress;
		overview->display_offset = overview->target_offset;
	} else {
		/* ~faster than the old exp(-14) settle so open/close snap. */
		double progress_factor = 1.0 - exp(-26.0 * dt);
		double offset_factor = 1.0 - exp(-28.0 * dt);

		overview->display_progress += progress_delta * progress_factor;
		overview->display_offset += offset_delta * offset_factor;
		if (fabs(progress_delta) < 0.002)
			overview->display_progress = overview->target_progress;
		if (fabs(offset_delta) < 0.25)
			overview->display_offset = overview->target_offset;
	}
	if (overview->card_dismiss_settling) {
		double elapsed = now - overview->card_dismiss_started_us;
		double t = animations ? clamp(elapsed / CARD_DISMISS_SETTLE_US, 0, 1) : 1;
		/* Cubic ease-out: velocity is zero at the end, never a bounce. */
		double eased = 1.0 - pow(1.0 - t, 3.0);

		overview->card_dismiss_display_y =
		    overview->card_dismiss_start_y +
		    (overview->card_dismiss_target_y - overview->card_dismiss_start_y) *
		    eased;
		if (t >= 1.0) {
			overview->card_dismiss_display_y =
			    overview->card_dismiss_target_y;
			overview->card_dismiss_settling = false;
			card_dismiss_finished = true;
		}
	}
	return card_dismiss_finished;
}

static gboolean
tick(GtkWidget *widget, GdkFrameClock *clock, gpointer data)
{
	struct overview *overview = data;
	bool card_dismiss_finished = advance_motion(overview,
	    gdk_frame_clock_get_frame_time(clock));
	bool settled;

	(void)widget;
	if (overview->hide_pending && overview->display_progress <= 0.01) {
		overview->display_progress = 0;
		overview->mapped = false;
		clear_label_cache(overview);
		overview->hide_pending = false;
		overview->last_tick_us = 0;
		gtk_widget_set_visible(GTK_WIDGET(overview->window), FALSE);
		record_capture_state(overview, false);
		notify_capture_hidden();
	}
	if (overview->mapped)
		gtk_widget_queue_draw(GTK_WIDGET(overview->area));
	if (card_dismiss_finished) {
		int selected = overview->card_dismiss_index;
		bool close_workspace = overview->card_dismiss_pending;

		reset_card_dismissal(overview);
		if (close_workspace)
			close_card(overview, selected);
		return G_SOURCE_CONTINUE;
	}
	settled = fabs(overview->target_progress -
	    overview->display_progress) < 0.002 &&
	    fabs(overview->target_offset - overview->display_offset) < 0.25 &&
	    !overview->card_dismiss_settling;
	if (settled) {
		overview->tick_source = 0;
		overview->last_tick_us = 0;
		return G_SOURCE_REMOVE;
	}
	return G_SOURCE_CONTINUE;
}

static void
ensure_tick(struct overview *overview)
{
	if (overview->tick_source == 0) {
		overview->last_tick_us = 0;
		overview->tick_source = gtk_widget_add_tick_callback(overview->area,
		    tick, overview, NULL);
	}
}

static void
run_sway_argv(char *argv[])
{
	GError *error = NULL;

	if (!g_spawn_async(NULL, argv, NULL,
	    G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL |
	    G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, &error))
		g_clear_error(&error);
}

static void
run_sway_action(int64_t id, const char *action)
{
	char selector[64];
	char *argv[] = {"swaymsg", selector, (char *)action, NULL};

	g_snprintf(selector, sizeof(selector), "[con_id=%" PRId64 "]", id);
	run_sway_argv(argv);
}

static bool
request_workspace_close(const char *workspace)
{
	const char *runtime = g_get_user_runtime_dir();
	char *path = g_build_filename(runtime, "ctlst-workspaces.sock", NULL);
	struct sockaddr_un address = {0};
	struct json_object *request = json_object_new_object();
	const char *payload;
	size_t length;
	ssize_t sent;
	int fd;
	bool ok = false;

	json_object_object_add(request, "command",
	    json_object_new_string("close"));
	json_object_object_add(request, "workspace",
	    json_object_new_string(workspace));
	payload = json_object_to_json_string_ext(request,
	    JSON_C_TO_STRING_PLAIN);
	length = strlen(payload);
	fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0)
		goto out;
	address.sun_family = AF_UNIX;
	if (g_strlcpy(address.sun_path, path, sizeof(address.sun_path)) >=
	    sizeof(address.sun_path))
		goto close_fd;
	if (connect(fd, (struct sockaddr *)&address, sizeof(address)) != 0)
		goto close_fd;
	sent = send(fd, payload, length, MSG_NOSIGNAL);
	ok = sent == (ssize_t)length;
close_fd:
	close(fd);
out:
	json_object_put(request);
	g_free(path);
	return ok;
}

static void
focus_workspace_card(struct overview *overview, int selected)
{
	struct workspace_card *card;
	char *argv[4];

	if (selected < 0 || selected >= overview->card_count)
		return;
	card = &overview->cards[selected];
	/* Fixed argv: workspace name is a single argv element, never shell-interpolated */
	argv[0] = "swaymsg";
	argv[1] = "workspace";
	argv[2] = card->workspace;
	argv[3] = NULL;
	run_sway_argv(argv);
}

static void
close_card(struct overview *overview, int selected)
{
	struct workspace_card *card;
	double spacing = card_spacing(overview);

	if (selected < 0 || selected >= overview->card_count)
		return;
	card = &overview->cards[selected];
	if (!request_workspace_close(card->workspace)) {
		for (int i = 0; i < card->id_count; i++)
			run_sway_action(card->ids[i], "kill");
	}
	if (card->thumb != NULL) {
		cairo_surface_destroy(card->thumb);
		card->thumb = NULL;
	}
	if (selected + 1 < overview->card_count) {
		memmove(&overview->cards[selected],
		    &overview->cards[selected + 1],
		    (size_t)(overview->card_count - selected - 1) *
		    sizeof(overview->cards[0]));
	}
	overview->card_count--;
	/* Drop the duplicate tail slot left by memmove (avoids double-free). */
	memset(&overview->cards[overview->card_count], 0,
	    sizeof(overview->cards[0]));
	if (overview->card_count == 0) {
		overview->selected = 0;
		overview->target_offset = 0;
	} else {
		overview->selected = selected < overview->card_count ? selected :
		    overview->card_count - 1;
		overview->target_offset = -overview->selected * spacing;
	}
	clear_label_cache(overview);
	/* A pre-close snapshot must never resurrect the removed workspace. */
	cancel_refresh_windows(overview);
	ensure_tick(overview);
	update_overview_controls(overview);
	gtk_widget_queue_draw(GTK_WIDGET(overview->area));
	overview->refresh_delay_source = g_timeout_add(180, refresh_windows_once, overview);
}

static bool
drag_is_vertical(double dx, double dy, double ratio)
{
	return fabs(dy) > fabs(dx) * ratio;
}

static void
drag_begin(GtkGestureDrag *gesture, double x, double y, gpointer data)
{
	struct overview *overview = data;
	double spacing = card_spacing(overview);

	(void)gesture;
	overview->drag_active = true;
	overview->drag_cancelled = false;
	overview->drag_origin = overview->target_offset;
	overview->drag_dx = 0;
	overview->drag_dy = 0;
	overview->drag_start_x = x;
	overview->drag_start_y = y;
	if (overview->data_ready && overview->card_count > 0) {
		overview->drag_card_index = (int)llround(
		    -overview->display_offset / spacing);
		overview->drag_card_index = (int)clamp(overview->drag_card_index,
		    0, overview->card_count - 1);
	} else {
		overview->drag_card_index = -1;
	}
}

static void
drag_update(GtkGestureDrag *gesture, double dx, double dy, gpointer data)
{
	struct overview *overview = data;
	double minimum;

	(void)gesture;
	overview->drag_dx = dx;
	overview->drag_dy = dy;
	if (!overview->data_ready && dy <= 0)
		return;
	if (fabs(dx) >= fabs(dy)) {
		/* Horizontal: scrub workspace cards. */
		if (overview->card_dismiss_active)
			reset_card_dismissal(overview);
		minimum = -(overview->card_count - 1) * card_spacing(overview);
		overview->target_offset = clamp(overview->drag_origin + dx,
		    minimum - 70, 70);
		overview->display_offset = overview->target_offset;
		gtk_widget_queue_draw(overview->area);
		/* Cancel any in-progress pull-down if intent becomes horizontal. */
		if (overview->committed && overview->target_progress < 1.0) {
			overview->target_progress = 1.0;
			overview->display_progress = 1.0;
		}
		ensure_tick(overview);
		return;
	}
	/*
	 * A card drag is a retained-canvas translation, not a GTK resize or a
	 * delayed action.  The focused card's top edge therefore stays glued to
	 * the finger for the whole upward pull.
	 */
	if (overview->committed && dy < 0 && overview->drag_card_index >= 0) {
		double offscreen = overview->height > 1 ? overview->height * 1.10 :
		    CARD_DISMISS_DRAG_PX * 4.0;

		overview->card_dismiss_active = true;
		overview->card_dismiss_settling = false;
		overview->card_dismiss_pending = false;
		overview->card_dismiss_index = overview->drag_card_index;
		overview->card_dismiss_target_y = clamp(dy, -offscreen, 0);
		overview->card_dismiss_display_y =
		    overview->card_dismiss_target_y;
		gtk_widget_queue_draw(GTK_WIDGET(overview->area));
		ensure_tick(overview);
		return;
	}
	/*
	 * Vertical down while overview is open: track dismiss progress 1:1 so
	 * sliding down feels like closing multitasking (Android-style).
	 */
	if (overview->committed && dy > 0) {
		double full = overview->height > 1 ?
		    overview->height * OVERVIEW_DISMISS_DRAG_FRACTION :
		    OVERVIEW_DISMISS_DRAG_PX * 2.0;
		double progress = clamp(1.0 - dy / full, 0.08, 1.0);

		if (overview->card_dismiss_active)
			reset_card_dismissal(overview);
		overview->target_progress = progress;
		overview->display_progress = progress;
		ensure_tick(overview);
	}
}

static void
drag_end(GtkGestureDrag *gesture, double dx, double dy, gpointer data)
{
	struct overview *overview = data;
	double spacing = card_spacing(overview);
	int selected;
	double dismiss_threshold;

	(void)gesture;
	overview->drag_active = false;
	queue_refresh_windows(overview);
	if (overview->drag_cancelled)
		return;
	/* Click release can schedule Close before GTK emits the same press's
	 * zero-distance drag-end. Do not turn that confirmed close into a cancel. */
	if (overview->card_dismiss_settling)
		return;
	dismiss_threshold = overview->height > 1 ?
	    overview->height * OVERVIEW_DISMISS_DRAG_FRACTION * 0.55 :
	    OVERVIEW_DISMISS_DRAG_PX;
	if (dismiss_threshold < OVERVIEW_DISMISS_DRAG_PX)
		dismiss_threshold = OVERVIEW_DISMISS_DRAG_PX;

	/* Slide down to leave multitasking. */
	if (overview->committed && dy > dismiss_threshold &&
	    drag_is_vertical(dx, dy, 1.10)) {
		hide_overview(overview);
		return;
	}
	/* Incomplete pull-down: snap overview fully open again. */
	if (overview->committed && overview->target_progress < 1.0 && dy >= 0) {
		overview->target_progress = 1.0;
		ensure_tick(overview);
	}

	if (overview->card_count == 0) {
		/* Empty overview: upward flick also dismisses. */
		if (dy < -70 && drag_is_vertical(dx, dy, 1.15))
			hide_overview(overview);
		return;
	}
	/*
	 * Finish the visual card dismissal before deleting the workspace.  A
	 * short pull uses the same 180ms ease-out to return precisely to center.
	 */
	if (overview->committed && dy < 0 &&
	    drag_is_vertical(dx, dy, 1.15) && overview->card_dismiss_active) {
		settle_card_dismissal(overview, dy <= -CARD_DISMISS_DRAG_PX);
		return;
	}
	/* A late diagonal change of intent must never leave a card stranded. */
	if (overview->card_dismiss_active) {
		settle_card_dismissal(overview, false);
		return;
	}
	selected = (int)llround(-overview->target_offset / spacing);
	if (fabs(dx) > spacing * 0.16)
		selected += dx < 0 ? 1 : -1;
	selected = (int)clamp(selected, 0, overview->card_count - 1);
	overview->selected = selected;
	overview->target_offset = -selected * spacing;
	update_overview_controls(overview);
	ensure_tick(overview);
}

static void
drag_cancel(GtkGesture *gesture, GdkEventSequence *sequence, gpointer data)
{
	struct overview *overview = data;
	(void)gesture;
	(void)sequence;
	overview->drag_active = false;
	overview->drag_cancelled = true;
	/* A cancelled input sequence cannot close a task or keep refresh deferred.
	 * Preserve an explicit click-close already committed by release ordering. */
	if (!overview->card_dismiss_pending) {
		reset_card_dismissal(overview);
		overview->target_offset = -overview->selected * card_spacing(overview);
		if (overview->committed && !overview->hide_pending)
			overview->target_progress = 1;
		ensure_tick(overview);
	}
	queue_refresh_windows(overview);
}

static void
select_overview_card(struct overview *overview, int index)
{
	if (!overview->committed || !overview->data_ready ||
	    overview->card_count == 0 || overview->card_dismiss_active)
		return;
	overview->selected = (int)clamp(index, 0, overview->card_count - 1);
	overview->target_offset = -overview->selected * card_spacing(overview);
	update_overview_controls(overview);
	ensure_tick(overview);
}

static void
previous_clicked(GtkButton *button, gpointer data)
{
	(void)button;
	struct overview *overview = data;
	select_overview_card(overview, overview->selected - 1);
}

static void
next_clicked(GtkButton *button, gpointer data)
{
	(void)button;
	struct overview *overview = data;
	select_overview_card(overview, overview->selected + 1);
}

static void
done_clicked(GtkButton *button, gpointer data)
{
	(void)button;
	hide_overview(data);
}

static void
open_clicked(GtkButton *button, gpointer data)
{
	(void)button;
	struct overview *overview = data;
	if (!overview->committed || !overview->data_ready || overview->card_count == 0 ||
	    overview->card_dismiss_active)
		return;
	focus_workspace_card(overview, overview->selected);
	hide_overview(overview);
}

static void
close_clicked(GtkButton *button, gpointer data)
{
	(void)button;
	struct overview *overview = data;
	if (!overview->committed || !overview->data_ready || overview->card_count == 0 ||
	    overview->card_dismiss_active)
		return;
	overview->card_dismiss_active = true;
	overview->card_dismiss_index = overview->selected;
	overview->card_dismiss_display_y = 0;
	settle_card_dismissal(overview, true);
}

static gboolean
overview_key_pressed(GtkEventControllerKey *controller, guint keyval,
    guint keycode, GdkModifierType state, gpointer data)
{
	struct overview *overview = data;
	(void)controller;
	(void)keycode;
	if (!overview->committed)
		return FALSE;
	if (keyval == GDK_KEY_Escape) {
		hide_overview(overview);
		return TRUE;
	}
	if (state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SUPER_MASK))
		return FALSE;
	if (keyval == GDK_KEY_Left || keyval == GDK_KEY_Right) {
		select_overview_card(overview, overview->selected +
		    (keyval == GDK_KEY_Right ? 1 : -1));
		return TRUE;
	}
	if ((keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) &&
	    gtk_window_get_focus(overview->window) == overview->area) {
		open_clicked(NULL, overview);
		return TRUE;
	}
	return FALSE;
}

static gboolean
overview_scroll(GtkEventControllerScroll *controller, double dx, double dy,
    gpointer data)
{
	struct overview *overview = data;
	(void)controller;
	if (!overview->committed)
		return FALSE;
	/* Discrete wheel/trackpad steps move the same selection as touch/keys. */
	if (fabs(dx) + fabs(dy) > 0.01)
		select_overview_card(overview, overview->selected + ((fabs(dx) > fabs(dy) ? dx : dy) > 0 ? 1 : -1));
	return TRUE;
}

static GtkWidget *
overview_button(const char *icon, const char *label, GCallback callback,
    struct overview *overview)
{
	GtkWidget *button = icon != NULL ? gtk_button_new_from_icon_name(icon) :
	    gtk_button_new_with_label(label);
	gtk_widget_set_tooltip_text(button, label);
	gtk_accessible_update_property(GTK_ACCESSIBLE(button),
	    GTK_ACCESSIBLE_PROPERTY_LABEL, label, -1);
	g_signal_connect(button, "clicked", callback, overview);
	return button;
}

static void
click_pressed(GtkGestureClick *gesture, int presses, double x, double y,
    gpointer data)
{
	struct overview *overview = data;
	(void)gesture;
	(void)presses;
	(void)x;
	(void)y;
	/* A click can follow a completed touch swipe without a new drag-begin.
	 * Movement suppression belongs to this press, not the previous gesture. */
	overview->drag_dx = overview->drag_dy = 0;
}

static void
click_released(GtkGestureClick *gesture, int presses, double x, double y,
    gpointer data)
{
	struct overview *overview = data;
	double spacing = card_spacing(overview);
	int selected;
	double width;
	double height;
	double card_x;
	double card_y;

	(void)gesture;
	(void)presses;
	if (!overview->committed || !overview->data_ready ||
	    overview->card_dismiss_active || overview->card_count == 0 ||
	    fabs(overview->drag_dx) > 14 ||
	    fabs(overview->drag_dy) > 14)
		return;
	selected = (int)llround(-overview->display_offset / spacing);
	selected = (int)clamp(selected, 0, overview->card_count - 1);
	width = card_width(overview);
	height = card_height(overview);
	card_x = overview->width / 2.0 - width / 2.0;
	card_y = content_center_y(overview) - height / 2.0;
	if (x < card_x || x > card_x + width ||
	    y < card_y || y > card_y + height) {
		hide_overview(overview);
		return;
	}
	if (x >= card_x + width - 52 && y <= card_y + 48) {
		overview->selected = selected;
		close_clicked(NULL, overview);
		return;
	}
	focus_workspace_card(overview, selected);
	hide_overview(overview);
}

static gboolean
socket_ready(gint fd, GIOCondition condition, gpointer data)
{
	struct overview *overview = data;
	char message[CONTROL_MESSAGE_SIZE];
	ssize_t size;

	if ((condition & G_IO_IN) == 0)
		return G_SOURCE_CONTINUE;
	while ((size = recv(fd, message, sizeof(message) - 1,
	    MSG_DONTWAIT)) > 0) {
		message[size] = '\0';
		if (message[0] == 'P')
			preview_progress(overview, g_ascii_strtod(message + 1, NULL));
		else if (message[0] == 'S')
			show_overview(overview);
		else if (message[0] == 'H')
			hide_overview(overview);
		else if (message[0] == 'R') {
			load_theme(overview);
			schedule_refresh_windows(overview);
			gtk_widget_queue_draw(GTK_WIDGET(overview->area));
		}
	}
	return G_SOURCE_CONTINUE;
}

static bool
create_socket(struct overview *overview)
{
	struct sockaddr_un address = {0};

	overview->control_fd = socket(AF_UNIX,
	    SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
	if (overview->control_fd < 0)
		return false;
	address.sun_family = AF_UNIX;
	g_strlcpy(address.sun_path, overview->socket_path,
	    sizeof(address.sun_path));
	unlink(overview->socket_path);
	if (bind(overview->control_fd, (struct sockaddr *)&address,
	    sizeof(address)) < 0)
		return false;
	chmod(overview->socket_path, 0600);
	overview->control_source = g_unix_fd_add(overview->control_fd,
	    G_IO_IN, socket_ready, overview);
	return true;
}

static void
activate(GtkApplication *application, gpointer data)
{
	struct overview *overview = data;
	GtkGesture *drag;
	GtkGesture *click;
	GtkWidget *overlay;
	GtkEventController *keys, *scroll;
	const char *runtime = g_get_user_runtime_dir();
	const char *cache = g_get_user_cache_dir();
	static const char *css =
	    "window#ctlstoverview { background: transparent; }";
	GtkCssProvider *provider;

	if (overview->window != NULL)
		return;
	overview->application = application;
	g_snprintf(overview->socket_path, sizeof(overview->socket_path),
	    "%s/%s", runtime, OVERVIEW_SOCKET);
	g_snprintf(overview->state_path, sizeof(overview->state_path),
	    "%s/%s", runtime, OVERVIEW_STATE);
	g_snprintf(overview->capture_state_path, sizeof(overview->capture_state_path),
	    "%s/%s", runtime, OVERVIEW_CAPTURE_STATE);
	g_snprintf(overview->gesture_path, sizeof(overview->gesture_path),
	    "%s/%s", runtime, GESTURE_SOCKET);
	g_snprintf(overview->dock_path, sizeof(overview->dock_path),
	    "%s/%s", runtime, DOCK_SOCKET);
	g_snprintf(overview->cache_dir, sizeof(overview->cache_dir),
	    "%s/sway-touch/workspaces", cache != NULL ? cache : "/tmp");
	g_snprintf(overview->fps_path, sizeof(overview->fps_path),
	    "%s/ctlstoverview.fps", runtime);
	unlink(overview->fps_path);
	if (!create_socket(overview)) {
		g_warning("cannot create overview socket: %s", g_strerror(errno));
		g_application_quit(G_APPLICATION(application));
		return;
	}
	load_theme(overview);
	overview->window = GTK_WINDOW(gtk_application_window_new(application));
	record_capture_state(overview, false);
	gtk_widget_set_name(GTK_WIDGET(overview->window), "ctlstoverview");
	gtk_window_set_title(overview->window, "ctlstoverview");
	gtk_window_set_decorated(overview->window, FALSE);
	gtk_layer_init_for_window(overview->window);
	/*
	 * TOP sits under OVERLAY. The task dock is OVERLAY, so it stays visible
	 * and tappable (including its Home control) while multitasking is open.
	 */
	gtk_layer_set_layer(overview->window, GTK_LAYER_SHELL_LAYER_TOP);
	gtk_layer_set_namespace(overview->window, "ctlstoverview");
	gtk_layer_set_anchor(overview->window, GTK_LAYER_SHELL_EDGE_TOP, TRUE);
	gtk_layer_set_anchor(overview->window, GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
	gtk_layer_set_anchor(overview->window, GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
	gtk_layer_set_anchor(overview->window, GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
	gtk_layer_set_exclusive_zone(overview->window, -1);
	gtk_layer_set_keyboard_mode(overview->window,
	    GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
	provider = gtk_css_provider_new();
	gtk_css_provider_load_from_string(provider, css);
	gtk_style_context_add_provider_for_display(gdk_display_get_default(),
	    GTK_STYLE_PROVIDER(provider),
	    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref(provider);
	overview->area = g_object_new(overview_canvas_get_type(), NULL);
	((OverviewCanvas *)overview->area)->overview = overview;
	gtk_widget_set_hexpand(GTK_WIDGET(overview->area), TRUE);
	gtk_widget_set_vexpand(GTK_WIDGET(overview->area), TRUE);
	gtk_widget_set_focusable(overview->area, TRUE);
	drag = gtk_gesture_drag_new();
	g_signal_connect(drag, "drag-begin", G_CALLBACK(drag_begin), overview);
	g_signal_connect(drag, "drag-update", G_CALLBACK(drag_update), overview);
	g_signal_connect(drag, "drag-end", G_CALLBACK(drag_end), overview);
	g_signal_connect(drag, "cancel", G_CALLBACK(drag_cancel), overview);
	gtk_widget_add_controller(GTK_WIDGET(overview->area),
	    GTK_EVENT_CONTROLLER(drag));
	click = gtk_gesture_click_new();
	g_signal_connect(click, "pressed", G_CALLBACK(click_pressed), overview);
	g_signal_connect(click, "released", G_CALLBACK(click_released), overview);
	gtk_widget_add_controller(GTK_WIDGET(overview->area),
	    GTK_EVENT_CONTROLLER(click));
	keys = gtk_event_controller_key_new();
	g_signal_connect(keys, "key-pressed", G_CALLBACK(overview_key_pressed), overview);
	gtk_widget_add_controller(GTK_WIDGET(overview->window), keys);
	scroll = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES |
	    GTK_EVENT_CONTROLLER_SCROLL_DISCRETE);
	g_signal_connect(scroll, "scroll", G_CALLBACK(overview_scroll), overview);
	gtk_widget_add_controller(overview->area, scroll);
	overlay = gtk_overlay_new();
	gtk_overlay_set_child(GTK_OVERLAY(overlay), overview->area);
	overview->done_button = overview_button(NULL, "Done", G_CALLBACK(done_clicked), overview);
	gtk_widget_set_halign(overview->done_button, GTK_ALIGN_END);
	gtk_widget_set_valign(overview->done_button, GTK_ALIGN_START);
	gtk_widget_set_margin_top(overview->done_button, 34);
	gtk_widget_set_margin_end(overview->done_button, 20);
	gtk_overlay_add_overlay(GTK_OVERLAY(overlay), overview->done_button);
	overview->controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_halign(overview->controls, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(overview->controls, GTK_ALIGN_END);
	gtk_widget_set_margin_bottom(overview->controls, (int)DOCK_CLEARANCE);
	overview->previous_button = overview_button("go-previous-symbolic", "Previous task",
	    G_CALLBACK(previous_clicked), overview);
	overview->next_button = overview_button("go-next-symbolic", "Next task",
	    G_CALLBACK(next_clicked), overview);
	overview->open_button = overview_button(NULL, "Open", G_CALLBACK(open_clicked), overview);
	overview->close_button = overview_button("window-close-symbolic", "Close task",
	    G_CALLBACK(close_clicked), overview);
	gtk_box_append(GTK_BOX(overview->controls), overview->previous_button);
	gtk_box_append(GTK_BOX(overview->controls), overview->open_button);
	gtk_box_append(GTK_BOX(overview->controls), overview->close_button);
	gtk_box_append(GTK_BOX(overview->controls), overview->next_button);
	gtk_overlay_add_overlay(GTK_OVERLAY(overlay), overview->controls);
	update_overview_controls(overview);
	gtk_window_set_child(overview->window, overlay);
	gtk_widget_set_visible(GTK_WIDGET(overview->window), FALSE);
	g_application_hold(G_APPLICATION(application));
}

static void
shutdown_app(GApplication *application, gpointer data)
{
	struct overview *overview = data;

	(void)application;
	if (overview->refresh_owner)
		g_object_set_data(overview->refresh_owner, "overview", NULL);
	cancel_refresh_windows(overview);
	g_clear_object(&overview->refresh_cancel);
	g_clear_object(&overview->refresh_owner);
	if (overview->window)
		gtk_widget_set_visible(GTK_WIDGET(overview->window), FALSE);
	record_capture_state(overview, false);
	if (overview->capture_state_path[0])
		notify_capture_hidden();
	unlink(overview->state_path);
	unlink(overview->socket_path);
	if (overview->control_source != 0)
		g_source_remove(overview->control_source);
	if (overview->tick_source != 0)
		gtk_widget_remove_tick_callback(overview->area, overview->tick_source);
	if (overview->control_fd >= 0)
		close(overview->control_fd);
	clear_label_cache(overview);
	free_card_thumbs(overview);
	g_clear_object(&overview->chrome_css);
}

int
main(int argc, char **argv)
{
	struct overview overview = {
		.control_fd = -1,
	};
	GtkApplication *application;
	int status;

	/* Honour explicit software/testing overrides; normal sessions use GL. */
	if (g_getenv("GSK_RENDERER") == NULL)
		g_setenv("GSK_RENDERER", "gl", FALSE);
	application = gtk_application_new(APP_ID, G_APPLICATION_DEFAULT_FLAGS);

	g_signal_connect(application, "activate", G_CALLBACK(activate),
	    &overview);
	g_signal_connect(application, "shutdown", G_CALLBACK(shutdown_app),
	    &overview);
	status = g_application_run(G_APPLICATION(application), argc, argv);
	g_object_unref(application);
	return status;
}
