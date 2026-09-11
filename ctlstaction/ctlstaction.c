#define _GNU_SOURCE

#include "../ctlst-runtime.h"
#include <errno.h>
#include <fcntl.h>
#include <glib-unix.h>
#include <gtk/gtk.h>
#include <gtk4-layer-shell.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#define APP_ID "dev.ctlst.Action"
#define ACTION_SOCKET "ctlst-action.sock"
#define ACTION_STATE "ctlstaction.visible"
#define MESSAGE_SIZE 96
#define PILL_WIDTH 44.0
#define PILL_HEIGHT 44.0
#define PILL_RADIUS 22.0

enum action_target {
	ACTION_NONE,
	ACTION_PAD,
	ACTION_KEYS,
	ACTION_CLOSE,
};

struct color {
	double red;
	double green;
	double blue;
	double alpha;
};

struct action_item {
	enum action_target target;
	double offset_x;
	double offset_y;
	struct color background;
	struct color foreground;
};

struct action_overlay {
	GtkWindow *window;
	GtkWidget *area;
	PangoLayout *labels[4];
	int control_fd;
	guint control_source;
	guint tick_source;
	char socket_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
	char state_path[256];
	bool mapped;
	double progress;
	double origin_x;
	double origin_y;
	double pointer_x;
	double pointer_y;
	enum action_target selected;
	struct color hub;
	struct color hub_text;
	struct color pad;
	struct color pad_text;
	struct color keys;
	struct color keys_text;
	struct color close;
	struct color close_text;
	struct color ring;
};

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
load_theme(struct action_overlay *overlay)
{
	char *path = ctlst_config_path("theme.env");
	char *contents = NULL;
	char **lines;

	overlay->hub = (struct color){1.0, 0.99, 0.97, 0.86};
	overlay->hub_text = (struct color){0.11, 0.11, 0.10, 1.0};
	overlay->pad = (struct color){0.92, 0.86, 0.94, 1.0};
	overlay->pad_text = (struct color){0.24, 0.18, 0.25, 1.0};
	overlay->keys = (struct color){0.85, 0.89, 1.0, 1.0};
	overlay->keys_text = (struct color){0.15, 0.19, 0.27, 1.0};
	overlay->close = (struct color){0.96, 0.85, 0.84, 1.0};
	overlay->close_text = (struct color){0.36, 0.13, 0.11, 1.0};
	overlay->ring = (struct color){0.21, 0.37, 0.44, 1.0};
	if (!g_file_get_contents(path, &contents, NULL, NULL)) {
		g_free(path);
		return;
	}
	lines = g_strsplit(contents, "\n", 0);
	for (int i = 0; lines[i] != NULL; i++) {
		char **pair = g_strsplit(lines[i], "=", 2);

		if (pair[0] == NULL || pair[1] == NULL) {
			g_strfreev(pair);
			continue;
		}
		if (strcmp(pair[0], "ACTION_HUB") == 0)
			overlay->hub = hex_color(pair[1], overlay->hub);
		else if (strcmp(pair[0], "ACTION_HUB_TEXT") == 0)
			overlay->hub_text = hex_color(pair[1], overlay->hub_text);
		else if (strcmp(pair[0], "ACTION_PAD") == 0)
			overlay->pad = hex_color(pair[1], overlay->pad);
		else if (strcmp(pair[0], "ACTION_PAD_TEXT") == 0)
			overlay->pad_text = hex_color(pair[1], overlay->pad_text);
		else if (strcmp(pair[0], "ACTION_KEYS") == 0)
			overlay->keys = hex_color(pair[1], overlay->keys);
		else if (strcmp(pair[0], "ACTION_KEYS_TEXT") == 0)
			overlay->keys_text = hex_color(pair[1], overlay->keys_text);
		else if (strcmp(pair[0], "ACTION_CLOSE") == 0)
			overlay->close = hex_color(pair[1], overlay->close);
		else if (strcmp(pair[0], "ACTION_CLOSE_TEXT") == 0)
			overlay->close_text = hex_color(pair[1], overlay->close_text);
		else if (strcmp(pair[0], "ACTION_RING") == 0)
			overlay->ring = hex_color(pair[1], overlay->ring);
		g_strfreev(pair);
	}
	g_strfreev(lines);
	g_free(contents);
	g_free(path);
}

static GdkRGBA
rgba(struct color color, double opacity)
{
	return (GdkRGBA){color.red, color.green, color.blue, color.alpha * opacity};
}

static void
rounded_panel(GtkSnapshot *snapshot, double x, double y, double width,
    double height, double radius, struct color color, double opacity)
{
	graphene_rect_t bounds = GRAPHENE_RECT_INIT(x, y, width, height);
	GskRoundedRect rounded;
	gsk_rounded_rect_init_from_rect(&rounded, &bounds, radius);
	GdkRGBA ink = rgba(color, opacity);
	gtk_snapshot_push_rounded_clip(snapshot, &rounded);
	gtk_snapshot_append_color(snapshot, &ink, &bounds);
	gtk_snapshot_pop(snapshot);
}

static void
line(GtkSnapshot *snapshot, double x1, double y1, double x2, double y2,
    double thickness, struct color color, double opacity)
{
	graphene_point_t origin = {x1, y1};
	graphene_rect_t bounds = GRAPHENE_RECT_INIT(0, -thickness / 2,
	    hypot(x2 - x1, y2 - y1), thickness);
	GdkRGBA ink = rgba(color, opacity);
	gtk_snapshot_save(snapshot);
	gtk_snapshot_translate(snapshot, &origin);
	gtk_snapshot_rotate(snapshot, atan2(y2 - y1, x2 - x1) * 180 / G_PI);
	gtk_snapshot_append_color(snapshot, &ink, &bounds);
	gtk_snapshot_restore(snapshot);
}

static void
draw_symbol(GtkSnapshot *snapshot, enum action_target target, double x,
    double y, struct color color, double alpha)
{
	if (target == ACTION_CLOSE) {
		line(snapshot, x - 6, y - 6, x + 6, y + 6, 2, color, alpha);
		line(snapshot, x - 6, y + 6, x + 6, y - 6, 2, color, alpha);
		return;
	}
	graphene_rect_t bounds = GRAPHENE_RECT_INIT(x - 10, y - 7, 20, 14);
	GskRoundedRect outline;
	gsk_rounded_rect_init_from_rect(&outline, &bounds, target == ACTION_PAD ? 5 : 3);
	GdkRGBA ink = rgba(color, alpha), edges[] = {ink, ink, ink, ink};
	float widths[] = {1.7, 1.7, 1.7, 1.7};
	gtk_snapshot_append_border(snapshot, &outline, widths, edges);
	if (target == ACTION_PAD) {
		line(snapshot, x - 7, y, x - 1, y, 1.6, color, alpha);
		line(snapshot, x - 4, y - 3, x - 4, y + 3, 1.6, color, alpha);
		rounded_panel(snapshot, x + 2, y - 3, 2.5, 2.5, 1.25, color, alpha);
		rounded_panel(snapshot, x + 5, y, 2.5, 2.5, 1.25, color, alpha);
	} else {
		for (int row = 0; row < 2; row++)
			for (int column = 0; column < 4; column++)
				rounded_panel(snapshot, x - 6.7 + column * 4,
				    y - 3.8 + row * 3.2, 1.5, 1.5, .5, color, alpha);
		line(snapshot, x - 5, y + 4, x + 5, y + 4, 1.4, color, alpha);
	}
}

static const char *action_name(enum action_target target)
{
	switch (target) {
	case ACTION_PAD: return "Gamepad";
	case ACTION_KEYS: return "Keyboard";
	case ACTION_CLOSE: return "Close window";
	default: return "Slide to choose";
	}
}

static void
draw_item(GtkSnapshot *snapshot, struct action_overlay *overlay,
    const struct action_item *item, double progress)
{
	double center_x = overlay->origin_x + item->offset_x * progress;
	double center_y = overlay->origin_y + item->offset_y * progress;
	double scale = item->target == overlay->selected ? 1.10 : 1.0;
	double width = PILL_WIDTH * scale, height = PILL_HEIGHT * scale;
	double alpha = fmin(1.0, progress * 1.35);
	if (item->target == overlay->selected) {
		width += 6.0;
		height += 6.0;
		rounded_panel(snapshot, center_x - width / 2, center_y - height / 2,
		    width, height, height / 2, overlay->ring, alpha);
		width -= 4;
		height -= 4;
	}
	rounded_panel(snapshot, center_x - width / 2, center_y - height / 2,
	    width, height, height / 2, item->background, alpha);
	draw_symbol(snapshot, item->target, center_x, center_y, item->foreground, alpha);
}

static void
draw(GtkWidget *widget, GtkSnapshot *snapshot, struct action_overlay *overlay)
{
	double eased = 1.0 - pow(1.0 - overlay->progress, 3.0);
	struct action_item items[] = {
		{ACTION_PAD, -118.0, -26.0, overlay->pad, overlay->pad_text},
		{ACTION_KEYS, -88.0, -75.0, overlay->keys, overlay->keys_text},
		{ACTION_CLOSE, -34.0, -108.0, overlay->close, overlay->close_text},
	};
	for (size_t i = 0; i < G_N_ELEMENTS(items); i++)
		draw_item(snapshot, overlay, &items[i], eased);
	rounded_panel(snapshot, overlay->origin_x - PILL_WIDTH / 2,
	    overlay->origin_y - PILL_HEIGHT / 2, PILL_WIDTH, PILL_HEIGHT,
	    PILL_RADIUS, overlay->hub, 1);
	for (int i = -1; i <= 1; i++)
		rounded_panel(snapshot, overlay->origin_x + i * 6 - 1.8,
		    overlay->origin_y - 1.8, 3.6, 3.6, 1.8, overlay->hub_text, 1);
	if (gtk_widget_get_height(widget) < 220 || eased < .5)
		return;
	/* The caption is visual only; it must not move the gesture hit targets. */
	double left = fmax(8, fmin(gtk_widget_get_width(widget) - 180, overlay->origin_x - 170));
	double top = fmax(8, overlay->origin_y - 180);
	rounded_panel(snapshot, left, top, 172, 36, 18, overlay->hub, eased);
	PangoLayout **label = &overlay->labels[overlay->selected];
	if (*label == NULL) {
		*label = gtk_widget_create_pango_layout(widget, action_name(overlay->selected));
		PangoFontDescription *font = pango_font_description_from_string("Noto Sans Medium");
		pango_font_description_set_absolute_size(font, 14 * PANGO_SCALE);
		pango_layout_set_font_description(*label, font);
		pango_font_description_free(font);
		pango_layout_set_width(*label, 156 * PANGO_SCALE);
		pango_layout_set_alignment(*label, PANGO_ALIGN_CENTER);
		pango_layout_set_ellipsize(*label, PANGO_ELLIPSIZE_END);
	}
	int label_height;
	pango_layout_get_pixel_size(*label, NULL, &label_height);
	graphene_point_t origin = {left + 8, top + (36 - label_height) / 2};
	GdkRGBA ink = rgba(overlay->hub_text, eased);
	gtk_snapshot_save(snapshot);
	gtk_snapshot_translate(snapshot, &origin);
	gtk_snapshot_append_layout(snapshot, *label, &ink);
	gtk_snapshot_restore(snapshot);
}

typedef struct { GtkWidget parent_instance; struct action_overlay *overlay; } ActionCanvas;
typedef GtkWidgetClass ActionCanvasClass;
G_DEFINE_TYPE(ActionCanvas, action_canvas, GTK_TYPE_WIDGET)

static void action_canvas_snapshot(GtkWidget *widget, GtkSnapshot *snapshot)
{
	draw(widget, snapshot, ((ActionCanvas *)widget)->overlay);
}

static void action_canvas_class_init(ActionCanvasClass *klass)
{
	GTK_WIDGET_CLASS(klass)->snapshot = action_canvas_snapshot;
	gtk_widget_class_set_accessible_role(GTK_WIDGET_CLASS(klass), GTK_ACCESSIBLE_ROLE_GROUP);
}

static void action_canvas_init(ActionCanvas *canvas) { (void)canvas; }

static gboolean
tick(gpointer data)
{
	struct action_overlay *overlay = data;

	if (!overlay->mapped) {
		overlay->tick_source = 0;
		return G_SOURCE_REMOVE;
	}
	overlay->progress += (1.0 - overlay->progress) * 0.32;
	if (overlay->progress >= 0.995) {
		overlay->progress = 1.0;
		overlay->tick_source = 0;
		gtk_widget_queue_draw(GTK_WIDGET(overlay->area));
		return G_SOURCE_REMOVE;
	}
	gtk_widget_queue_draw(GTK_WIDGET(overlay->area));
	return G_SOURCE_CONTINUE;
}

static void
ensure_tick(struct action_overlay *overlay)
{
	if (overlay->tick_source == 0)
		overlay->tick_source = g_timeout_add(16, tick, overlay);
}

static enum action_target
target_from_code(char code)
{
	switch (code) {
	case 'P':
		return ACTION_PAD;
	case 'K':
		return ACTION_KEYS;
	case 'X':
		return ACTION_CLOSE;
	default:
		return ACTION_NONE;
	}
}

static void
set_selection(struct action_overlay *overlay, double pointer_x,
    double pointer_y, char selected)
{
	overlay->pointer_x = pointer_x;
	overlay->pointer_y = pointer_y;
	overlay->selected = target_from_code(selected);
	gtk_accessible_update_property(GTK_ACCESSIBLE(overlay->area),
	    GTK_ACCESSIBLE_PROPERTY_LABEL, action_name(overlay->selected), -1);
	gtk_widget_queue_draw(GTK_WIDGET(overlay->area));
}

static void
set_visible(struct action_overlay *overlay, bool visible)
{
	overlay->mapped = visible;
	if (visible) {
		close(open(overlay->state_path,
		    O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600));
		gtk_widget_set_visible(GTK_WIDGET(overlay->window), TRUE);
		gtk_window_present(overlay->window);
	} else {
		unlink(overlay->state_path);
		gtk_widget_set_visible(GTK_WIDGET(overlay->window), FALSE);
	}
}

static gboolean
socket_ready(gint fd, GIOCondition condition, gpointer data)
{
	struct action_overlay *overlay = data;
	char message[MESSAGE_SIZE];
	double origin_x;
	double origin_y;
	double pointer_x;
	double pointer_y;
	char selected;
	ssize_t size;

	if ((condition & G_IO_IN) == 0)
		return G_SOURCE_CONTINUE;
	size = recv(fd, message, sizeof(message) - 1, 0);
	if (size <= 0)
		return G_SOURCE_CONTINUE;
	message[size] = '\0';
	if (sscanf(message, "S %lf %lf %lf %lf", &origin_x, &origin_y,
	    &pointer_x, &pointer_y) == 4) {
		load_theme(overlay);
		overlay->origin_x = origin_x;
		overlay->origin_y = origin_y;
		overlay->pointer_x = pointer_x;
		overlay->pointer_y = pointer_y;
		overlay->selected = ACTION_NONE;
		gtk_accessible_update_property(GTK_ACCESSIBLE(overlay->area),
		    GTK_ACCESSIBLE_PROPERTY_LABEL, action_name(ACTION_NONE), -1);
		overlay->progress = 0.0;
		set_visible(overlay, true);
		ensure_tick(overlay);
		gtk_widget_queue_draw(GTK_WIDGET(overlay->area));
	} else if (sscanf(message, "M %lf %lf %c", &pointer_x, &pointer_y,
	    &selected) == 3) {
		set_selection(overlay, pointer_x, pointer_y, selected);
	/* The parent owns haptics; V changes only the visual target. */
	} else if (sscanf(message, "V %c", &selected) == 1) {
		set_selection(overlay, overlay->pointer_x, overlay->pointer_y,
		    selected);
	} else if (message[0] == 'H') {
		set_visible(overlay, false);
	} else if (message[0] == 'R') {
		load_theme(overlay);
		gtk_widget_queue_draw(GTK_WIDGET(overlay->area));
	}
	return G_SOURCE_CONTINUE;
}

static bool
create_socket(struct action_overlay *overlay)
{
	struct sockaddr_un address = {0};

	overlay->control_fd = socket(AF_UNIX,
	    SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
	if (overlay->control_fd < 0)
		return false;
	address.sun_family = AF_UNIX;
	g_strlcpy(address.sun_path, overlay->socket_path,
	    sizeof(address.sun_path));
	unlink(overlay->socket_path);
	if (bind(overlay->control_fd, (struct sockaddr *)&address,
	    sizeof(address)) < 0)
		return false;
	chmod(overlay->socket_path, 0600);
	overlay->control_source = g_unix_fd_add(overlay->control_fd,
	    G_IO_IN, socket_ready, overlay);
	return true;
}

static void
make_input_transparent(GtkWidget *widget)
{
	GdkSurface *surface;
	cairo_region_t *empty;

	gtk_widget_realize(widget);
	surface = gtk_native_get_surface(GTK_NATIVE(widget));
	if (surface == NULL)
		return;
	empty = cairo_region_create();
	gdk_surface_set_input_region(surface, empty);
	cairo_region_destroy(empty);
}

static void
activate(GtkApplication *application, gpointer data)
{
	struct action_overlay *overlay = data;
	GtkCssProvider *provider;
	const char *runtime = g_get_user_runtime_dir();
	static const char *css =
	    "window#ctlstaction { background: transparent; }";

	if (overlay->window != NULL)
		return;
	g_snprintf(overlay->socket_path, sizeof(overlay->socket_path),
	    "%s/%s", runtime, ACTION_SOCKET);
	g_snprintf(overlay->state_path, sizeof(overlay->state_path),
	    "%s/%s", runtime, ACTION_STATE);
	if (!create_socket(overlay)) {
		g_warning("cannot create action socket: %s", g_strerror(errno));
		g_application_quit(G_APPLICATION(application));
		return;
	}
	load_theme(overlay);
	overlay->window = GTK_WINDOW(gtk_application_window_new(application));
	gtk_widget_set_name(GTK_WIDGET(overlay->window), "ctlstaction");
	gtk_window_set_title(overlay->window, "ctlstaction");
	gtk_window_set_decorated(overlay->window, FALSE);
	gtk_layer_init_for_window(overlay->window);
	gtk_layer_set_layer(overlay->window, GTK_LAYER_SHELL_LAYER_OVERLAY);
	gtk_layer_set_namespace(overlay->window, "ctlstaction");
	gtk_layer_set_anchor(overlay->window, GTK_LAYER_SHELL_EDGE_TOP, TRUE);
	gtk_layer_set_anchor(overlay->window, GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
	gtk_layer_set_anchor(overlay->window, GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
	gtk_layer_set_anchor(overlay->window, GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
	gtk_layer_set_exclusive_zone(overlay->window, -1);
	gtk_layer_set_keyboard_mode(overlay->window,
	    GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
	provider = gtk_css_provider_new();
	gtk_css_provider_load_from_string(provider, css);
	gtk_style_context_add_provider_for_display(gdk_display_get_default(),
	    GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref(provider);
	overlay->area = g_object_new(action_canvas_get_type(), NULL);
	((ActionCanvas *)overlay->area)->overlay = overlay;
	gtk_widget_set_hexpand(GTK_WIDGET(overlay->area), TRUE);
	gtk_widget_set_vexpand(GTK_WIDGET(overlay->area), TRUE);
	gtk_widget_set_can_target(GTK_WIDGET(overlay->area), FALSE);
	gtk_window_set_child(overlay->window, GTK_WIDGET(overlay->area));
	make_input_transparent(GTK_WIDGET(overlay->window));
	gtk_widget_set_visible(GTK_WIDGET(overlay->window), FALSE);
	g_application_hold(G_APPLICATION(application));
}

static void
shutdown_app(GApplication *application, gpointer data)
{
	struct action_overlay *overlay = data;

	(void)application;
	for (unsigned i = 0; i < G_N_ELEMENTS(overlay->labels); i++)
		g_clear_object(&overlay->labels[i]);
	unlink(overlay->socket_path);
	unlink(overlay->state_path);
	if (overlay->control_source != 0)
		g_source_remove(overlay->control_source);
	if (overlay->tick_source != 0)
		g_source_remove(overlay->tick_source);
	if (overlay->control_fd >= 0)
		close(overlay->control_fd);
}

int
main(int argc, char **argv)
{
	if (g_getenv("GSK_RENDERER") == NULL)
		g_setenv("GSK_RENDERER", "gl", FALSE);
	struct action_overlay overlay = {.control_fd = -1};
	GtkApplication *application = gtk_application_new(APP_ID,
	    G_APPLICATION_DEFAULT_FLAGS);
	int status;

	g_signal_connect(application, "activate", G_CALLBACK(activate), &overlay);
	g_signal_connect(application, "shutdown", G_CALLBACK(shutdown_app),
	    &overlay);
	status = g_application_run(G_APPLICATION(application), argc, argv);
	g_object_unref(application);
	return status;
}
