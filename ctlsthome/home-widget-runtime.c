#include "../ctlst-runtime.h"
#include "home-widget-runtime.h"

#include <math.h>
#include <pango/pangocairo.h>
#include <json-c/json.h>

#define WIDGET_LINE_MAX (256 * 1024)
#define WIDGET_NODE_MAX 512
#define WIDGET_THEME_TOKEN_MAX 16

struct widget_theme_token {
	const char *name;
	const char *env_name;
	const char *fallback;
	char value[WIDGET_THEME_TOKEN_MAX];
};

static const struct widget_theme_token k_theme_defaults[] = {
	{ "@ctlst_bg", "SHELL_BG", "#171721ff", "" },
	{ "@ctlst_panel", "SHELL_PANEL", "#252535ff", "" },
	{ "@ctlst_panel_alt", "SHELL_PANEL_ALT", "#35354aff", "" },
	{ "@ctlst_raised", "SHELL_RAISED", "#2d2d40ff", "" },
	{ "@ctlst_overlay", "SHELL_OVERLAY", "#252535dd", "" },
	{ "@ctlst_text", "SHELL_TEXT", "#ffffffff", "" },
	{ "@ctlst_muted", "SHELL_MUTED", "#a0a0b0ff", "" },
	{ "@ctlst_line", "SHELL_LINE", "#606078ff", "" },
	{ "@ctlst_line_soft", "SHELL_LINE_SOFT", "#60607880", "" },
	{ "@ctlst_accent", "SHELL_ACCENT", "#6f24cfff", "" },
	{ "@ctlst_accent_soft", "SHELL_ACCENT_SOFT", "#eadbffff", "" },
	{ "@ctlst_warm", "SHELL_WARM", "#e7a93bff", "" },
	{ "@ctlst_danger", "SHELL_DANGER", "#ba1a1ae8", "" },
	{ "@ctlst_danger_active", "SHELL_DANGER_ACTIVE", "#d73329fc", "" },
	{ "@ctlst_selected_text", "SHELL_SELECTED_TEXT", "#ffffffff", "" },
};

struct home_widget_runtime {
	struct home_widget_desc desc;
	GtkDrawingArea *view;
	GSubprocess *process;
	GDataInputStream *output;
	GOutputStream *input;
	GCancellable *cancel;
	GFileMonitor *theme_monitor;
	struct widget_theme_token theme_tokens[G_N_ELEMENTS(k_theme_defaults)];
	struct json_object *frame;
	double pointer_x;
	double pointer_y;
	bool visible;
	bool stopped;
};

static void widget_read_next(struct home_widget_runtime *runtime);
static void load_theme_tokens(struct home_widget_runtime *runtime);

static bool
theme_file_changed(GFile *file)
{
	char *basename;
	bool changed;

	if (file == NULL)
		return false;
	basename = g_file_get_basename(file);
	changed = g_strcmp0(basename, "theme.env") == 0;
	g_free(basename);
	return changed;
}

static void
widget_theme_changed(GFileMonitor *monitor, GFile *file, GFile *other,
    GFileMonitorEvent event, gpointer data)
{
	struct home_widget_runtime *runtime = data;

	(void)monitor;
	if (!theme_file_changed(file) && !theme_file_changed(other))
		return;
	if (event == G_FILE_MONITOR_EVENT_CHANGED ||
	    event == G_FILE_MONITOR_EVENT_CREATED ||
	    event == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT ||
	    event == G_FILE_MONITOR_EVENT_MOVED_IN ||
	    event == G_FILE_MONITOR_EVENT_RENAMED) {
		load_theme_tokens(runtime);
		gtk_widget_queue_draw(GTK_WIDGET(runtime->view));
	}
}

static bool
json_number(struct json_object *object, const char *key, double *value)
{
	struct json_object *entry;

	if (!json_object_object_get_ex(object, key, &entry) ||
	    !(json_object_is_type(entry, json_type_double) ||
	    json_object_is_type(entry, json_type_int)))
		return false;
	*value = json_object_get_double(entry);
	return isfinite(*value);
}

static const char *
json_string(struct json_object *object, const char *key, const char *fallback)
{
	struct json_object *entry;

	if (!json_object_object_get_ex(object, key, &entry) ||
	    !json_object_is_type(entry, json_type_string))
		return fallback;
	return json_object_get_string(entry);
}

static bool
valid_theme_value(const char *value)
{
	size_t length;

	if (value == NULL)
		return false;
	length = strlen(value);
	if (length != 6 && length != 8)
		return false;
	for (size_t i = 0; i < length; i++)
		if (!g_ascii_isxdigit(value[i]))
			return false;
	return true;
}

static void
copy_theme_value(char destination[WIDGET_THEME_TOKEN_MAX], const char *value)
{
	if (valid_theme_value(value))
		g_snprintf(destination, WIDGET_THEME_TOKEN_MAX, "#%s%s", value,
		    strlen(value) == 6 ? "ff" : "");
}

static void
load_theme_tokens(struct home_widget_runtime *runtime)
{
	char *path;
	char *contents = NULL;
	char **lines;

	for (size_t i = 0; i < G_N_ELEMENTS(k_theme_defaults); i++) {
		runtime->theme_tokens[i] = k_theme_defaults[i];
		g_strlcpy(runtime->theme_tokens[i].value,
		    k_theme_defaults[i].fallback,
		    sizeof(runtime->theme_tokens[i].value));
	}
	path = ctlst_config_path("theme.env");
	if (!g_file_get_contents(path, &contents, NULL, NULL)) {
		g_free(path);
		return;
	}
	lines = g_strsplit(contents, "\n", -1);
	for (size_t i = 0; i < G_N_ELEMENTS(k_theme_defaults); i++) {
		for (int line_index = 0; lines[line_index] != NULL; line_index++) {
			char *line = g_strstrip(lines[line_index]);
			char *equals;

			equals = strchr(line, '=');
			if (equals == NULL)
				continue;
			/* Every token scans these same lines; keep delimiters intact. */
			if ((size_t)(equals - line) ==
			    strlen(runtime->theme_tokens[i].env_name) &&
			    strncmp(line, runtime->theme_tokens[i].env_name,
			    (size_t)(equals - line)) == 0) {
				copy_theme_value(runtime->theme_tokens[i].value, equals + 1);
				break;
			}
		}
	}
	g_strfreev(lines);
	g_free(contents);
	g_free(path);
}

static const char *
resolve_color(struct home_widget_runtime *runtime, const char *text,
    const char *fallback)
{
	if (text != NULL && text[0] == '@') {
		for (size_t i = 0; i < G_N_ELEMENTS(k_theme_defaults); i++)
			if (strcmp(text, runtime->theme_tokens[i].name) == 0)
				return runtime->theme_tokens[i].value;
	}
	return text != NULL ? text : fallback;
}

static void
set_source(struct home_widget_runtime *runtime, cairo_t *cr,
    const char *text, const char *fallback)
{
	GdkRGBA color;
	const char *resolved = resolve_color(runtime, text, fallback);

	if (!gdk_rgba_parse(&color, resolved) &&
	    !gdk_rgba_parse(&color, fallback))
		gdk_rgba_parse(&color, "#ffffff");
	cairo_set_source_rgba(cr, color.red, color.green, color.blue, color.alpha);
}

static void
rounded_rect(cairo_t *cr, double x, double y, double width, double height,
    double radius)
{
	double r = CLAMP(radius, 0.0, MIN(width, height) / 2.0);

	cairo_new_sub_path(cr);
	cairo_arc(cr, x + width - r, y + r, r, -G_PI / 2.0, 0.0);
	cairo_arc(cr, x + width - r, y + height - r, r, 0.0, G_PI / 2.0);
	cairo_arc(cr, x + r, y + height - r, r, G_PI / 2.0, G_PI);
	cairo_arc(cr, x + r, y + r, r, G_PI, 3.0 * G_PI / 2.0);
	cairo_close_path(cr);
}

static void
draw_text_node(struct home_widget_runtime *runtime, cairo_t *cr,
    struct json_object *node)
{
	const char *text = json_string(node, "text", "");
	const char *align = json_string(node, "align", "left");
	const char *weight = json_string(node, "weight", "normal");
	double x = 0.0, y = 0.0, size = 12.0;
	PangoLayout *layout;
	PangoFontDescription *font;
	int width;

	json_number(node, "x", &x);
	json_number(node, "y", &y);
	json_number(node, "size", &size);
	size = CLAMP(size, 4.0, 96.0);
	layout = pango_cairo_create_layout(cr);
	pango_layout_set_text(layout, text, 4096);
	font = pango_font_description_new();
	pango_font_description_set_family(font, "sans");
	pango_font_description_set_absolute_size(font, size * PANGO_SCALE);
	if (strcmp(weight, "bold") == 0)
		pango_font_description_set_weight(font, PANGO_WEIGHT_BOLD);
	pango_layout_set_font_description(layout, font);
	pango_layout_get_pixel_size(layout, &width, NULL);
	if (strcmp(align, "center") == 0)
		x -= width / 2.0;
	else if (strcmp(align, "right") == 0)
		x -= width;
	set_source(runtime, cr, json_string(node, "fill", "#ffffff"), "#ffffff");
	cairo_move_to(cr, x, y);
	pango_cairo_show_layout(cr, layout);
	pango_font_description_free(font);
	g_object_unref(layout);
}

static void
draw_node(struct home_widget_runtime *runtime, cairo_t *cr,
    struct json_object *node)
{
	const char *type = json_string(node, "type", "");
	double x = 0.0, y = 0.0, width = 0.0, height = 0.0;
	double radius = 0.0, x2 = 0.0, y2 = 0.0, line_width = 1.0;

	if (strcmp(type, "text") == 0) {
		draw_text_node(runtime, cr, node);
		return;
	}
	json_number(node, "x", &x);
	json_number(node, "y", &y);
	json_number(node, "width", &width);
	json_number(node, "height", &height);
	if (strcmp(type, "rect") == 0) {
		json_number(node, "radius", &radius);
		rounded_rect(cr, x, y, MAX(width, 0.0), MAX(height, 0.0), radius);
		set_source(runtime, cr, json_string(node, "fill", "#ffffff"), "#ffffff");
		cairo_fill(cr);
	} else if (strcmp(type, "circle") == 0) {
		json_number(node, "radius", &radius);
		cairo_arc(cr, x, y, MAX(radius, 0.0), 0.0, 2.0 * G_PI);
		set_source(runtime, cr, json_string(node, "fill", "#ffffff"), "#ffffff");
		cairo_fill(cr);
	} else if (strcmp(type, "line") == 0) {
		json_number(node, "x2", &x2);
		json_number(node, "y2", &y2);
		json_number(node, "line_width", &line_width);
		set_source(runtime, cr, json_string(node, "stroke", "#ffffff"), "#ffffff");
		cairo_set_line_width(cr, CLAMP(line_width, 0.5, 32.0));
		cairo_move_to(cr, x, y);
		cairo_line_to(cr, x2, y2);
		cairo_stroke(cr);
	}
}

static void
draw_widget(GtkDrawingArea *area, cairo_t *cr, int width, int height,
    gpointer data)
{
	struct home_widget_runtime *runtime = data;
	struct json_object *view;
	struct json_object *nodes;
	double scene_width = 100.0, scene_height = 100.0;
	double scale;

	(void)area;
	if (runtime->frame == NULL) {
		set_source(runtime, cr, "#252535", "#252535");
		cairo_paint(cr);
		return;
	}
	if (!json_object_object_get_ex(runtime->frame, "view", &view) ||
	    !json_object_is_type(view, json_type_object))
		return;
	json_number(view, "width", &scene_width);
	json_number(view, "height", &scene_height);
	scene_width = CLAMP(scene_width, 1.0, 4096.0);
	scene_height = CLAMP(scene_height, 1.0, 4096.0);
	set_source(runtime, cr, json_string(view, "background", "#252535"), "#252535");
	cairo_paint(cr);
	scale = MIN((double)width / scene_width, (double)height / scene_height);
	cairo_translate(cr, ((double)width - scene_width * scale) / 2.0,
	    ((double)height - scene_height * scale) / 2.0);
	cairo_scale(cr, scale, scale);
	if (!json_object_object_get_ex(runtime->frame, "nodes", &nodes) ||
	    !json_object_is_type(nodes, json_type_array))
		return;
	for (size_t i = 0; i < MIN(json_object_array_length(nodes),
	    (size_t)WIDGET_NODE_MAX); i++) {
		struct json_object *node = json_object_array_get_idx(nodes, i);

		if (json_object_is_type(node, json_type_object))
			draw_node(runtime, cr, node);
	}
}

static void
widget_send(struct home_widget_runtime *runtime, struct json_object *message)
{
	const char *serialized;
	gsize written = 0;
	GError *error = NULL;
	char *line;

	if (runtime->stopped || runtime->input == NULL)
		return;
	serialized = json_object_to_json_string_ext(message, JSON_C_TO_STRING_PLAIN);
	line = g_strconcat(serialized, "\n", NULL);
	if (!g_output_stream_write_all(runtime->input, line, strlen(line), &written,
	    runtime->cancel, &error)) {
		g_clear_error(&error);
		runtime->stopped = true;
	}
	g_free(line);
}

static void
send_geometry(struct home_widget_runtime *runtime, const char *type)
{
	struct json_object *message = json_object_new_object();
	int width = gtk_widget_get_width(GTK_WIDGET(runtime->view));
	int height = gtk_widget_get_height(GTK_WIDGET(runtime->view));

	json_object_object_add(message, "ctlst_widget", json_object_new_int(1));
	json_object_object_add(message, "type", json_object_new_string(type));
	json_object_object_add(message, "id",
	    json_object_new_string(runtime->desc.id));
	json_object_object_add(message, "width", json_object_new_int(MAX(width, 1)));
	json_object_object_add(message, "height", json_object_new_int(MAX(height, 1)));
	json_object_object_add(message, "orientation", json_object_new_string(
	    width > height ? "landscape" : "portrait"));
	widget_send(runtime, message);
	json_object_put(message);
}

static void
widget_size_changed(GtkDrawingArea *area, int width, int height, gpointer data)
{
	(void)area;
	(void)width;
	(void)height;
	send_geometry(data, "configure");
}

static void
send_pointer(struct home_widget_runtime *runtime, const char *phase,
    double x, double y)
{
	struct json_object *message = json_object_new_object();
	struct json_object *view;
	int width = MAX(gtk_widget_get_width(GTK_WIDGET(runtime->view)), 1);
	int height = MAX(gtk_widget_get_height(GTK_WIDGET(runtime->view)), 1);
	double scene_width = 100.0, scene_height = 100.0;
	double scale;
	double view_x;
	double view_y;
	bool inside;

	if (runtime->frame != NULL &&
	    json_object_object_get_ex(runtime->frame, "view", &view) &&
	    json_object_is_type(view, json_type_object)) {
		json_number(view, "width", &scene_width);
		json_number(view, "height", &scene_height);
	}
	scene_width = CLAMP(scene_width, 1.0, 4096.0);
	scene_height = CLAMP(scene_height, 1.0, 4096.0);
	scale = MIN((double)width / scene_width, (double)height / scene_height);
	view_x = (x - ((double)width - scene_width * scale) / 2.0) / scale;
	view_y = (y - ((double)height - scene_height * scale) / 2.0) / scale;
	inside = view_x >= 0.0 && view_y >= 0.0 &&
	    view_x <= scene_width && view_y <= scene_height;

	json_object_object_add(message, "ctlst_widget", json_object_new_int(1));
	json_object_object_add(message, "type", json_object_new_string("pointer"));
	json_object_object_add(message, "phase", json_object_new_string(phase));
	json_object_object_add(message, "x", json_object_new_double(x));
	json_object_object_add(message, "y", json_object_new_double(y));
	json_object_object_add(message, "nx", json_object_new_double(x / width));
	json_object_object_add(message, "ny", json_object_new_double(y / height));
	json_object_object_add(message, "view_x", json_object_new_double(view_x));
	json_object_object_add(message, "view_y", json_object_new_double(view_y));
	json_object_object_add(message, "inside", json_object_new_boolean(inside));
	widget_send(runtime, message);
	json_object_put(message);
}

static void
pointer_begin(GtkGestureDrag *gesture, double x, double y, gpointer data)
{
	struct home_widget_runtime *runtime = data;

	(void)gesture;
	runtime->pointer_x = x;
	runtime->pointer_y = y;
	send_pointer(runtime, "begin", x, y);
}

static void
pointer_update(GtkGestureDrag *gesture, double offset_x, double offset_y,
    gpointer data)
{
	struct home_widget_runtime *runtime = data;

	(void)gesture;
	send_pointer(runtime, "move", runtime->pointer_x + offset_x,
	    runtime->pointer_y + offset_y);
}

static void
pointer_end(GtkGestureDrag *gesture, double offset_x, double offset_y,
    gpointer data)
{
	struct home_widget_runtime *runtime = data;

	(void)gesture;
	send_pointer(runtime, "end", runtime->pointer_x + offset_x,
	    runtime->pointer_y + offset_y);
}

static bool
valid_frame(struct json_object *frame)
{
	struct json_object *version;
	struct json_object *type;

	return json_object_is_type(frame, json_type_object) &&
	    json_object_object_get_ex(frame, "ctlst_widget", &version) &&
	    json_object_get_int(version) == 1 &&
	    json_object_object_get_ex(frame, "type", &type) &&
	    json_object_is_type(type, json_type_string) &&
	    strcmp(json_object_get_string(type), "frame") == 0;
}

static void
widget_line_ready(GObject *source, GAsyncResult *result, gpointer data)
{
	struct home_widget_runtime *runtime = data;
	gsize length = 0;
	GError *error = NULL;
	char *line;
	struct json_object *frame;

	(void)source;
	line = g_data_input_stream_read_line_finish(runtime->output, result,
	    &length, &error);
	if (line == NULL) {
		g_clear_error(&error);
		runtime->stopped = true;
		return;
	}
	if (length <= WIDGET_LINE_MAX) {
		frame = json_tokener_parse(line);
		if (frame != NULL && valid_frame(frame)) {
			if (runtime->frame != NULL)
				json_object_put(runtime->frame);
			runtime->frame = frame;
			gtk_widget_queue_draw(GTK_WIDGET(runtime->view));
		} else if (frame != NULL) {
			json_object_put(frame);
		}
	}
	g_free(line);
	widget_read_next(runtime);
}

static void
widget_read_next(struct home_widget_runtime *runtime)
{
	if (runtime->stopped || runtime->output == NULL)
		return;
	g_data_input_stream_read_line_async(runtime->output, G_PRIORITY_DEFAULT,
	    runtime->cancel, widget_line_ready, runtime);
}

struct home_widget_runtime *
home_widget_runtime_new(const struct home_widget_desc *desc)
{
	struct home_widget_runtime *runtime;
	GSubprocessLauncher *launcher;
	GError *error = NULL;
	const char *argv[HOME_WIDGET_DESC_ARGS_MAX + 2] = {0};
	GtkGesture *drag;

	if (desc == NULL || desc->kind != HOME_WIDGET_KIND_EXEC ||
	    strcmp(desc->protocol, "ctlst-widget-1") != 0)
		return NULL;
	runtime = g_new0(struct home_widget_runtime, 1);
	runtime->desc = *desc;
	runtime->visible = true;
	runtime->cancel = g_cancellable_new();
	load_theme_tokens(runtime);
	runtime->view = GTK_DRAWING_AREA(gtk_drawing_area_new());
	gtk_drawing_area_set_draw_func(runtime->view, draw_widget, runtime, NULL);
	gtk_widget_set_hexpand(GTK_WIDGET(runtime->view), TRUE);
	gtk_widget_set_vexpand(GTK_WIDGET(runtime->view), TRUE);
	gtk_widget_set_overflow(GTK_WIDGET(runtime->view), GTK_OVERFLOW_HIDDEN);
	drag = gtk_gesture_drag_new();
	g_signal_connect(drag, "drag-begin", G_CALLBACK(pointer_begin), runtime);
	g_signal_connect(drag, "drag-update", G_CALLBACK(pointer_update), runtime);
	g_signal_connect(drag, "drag-end", G_CALLBACK(pointer_end), runtime);
	gtk_widget_add_controller(GTK_WIDGET(runtime->view),
	    GTK_EVENT_CONTROLLER(drag));
	{
		char *theme_dir_path = ctlst_config_path(NULL);
		GFile *theme_dir = g_file_new_for_path(theme_dir_path);

		runtime->theme_monitor = g_file_monitor_directory(theme_dir,
		    G_FILE_MONITOR_NONE, NULL, NULL);
		if (runtime->theme_monitor != NULL)
			g_signal_connect(runtime->theme_monitor, "changed",
			    G_CALLBACK(widget_theme_changed), runtime);
		g_object_unref(theme_dir);
		g_free(theme_dir_path);
	}
	/* GTK4 dimensions are not GObject properties. The drawing area's
	 * resize signal is the actual allocation notification. */
	g_signal_connect(runtime->view, "resize",
	    G_CALLBACK(widget_size_changed), runtime);
	argv[0] = desc->exec;
	for (int i = 0; i < desc->args_count; i++)
		argv[i + 1] = desc->args[i];
	launcher = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDIN_PIPE |
	    G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE);
	g_subprocess_launcher_setenv(launcher, "CTLST_WIDGET_ID", desc->id, TRUE);
	runtime->process = g_subprocess_launcher_spawnv(launcher, argv, &error);
	g_object_unref(launcher);
	if (runtime->process == NULL) {
		g_warning("home widget %s failed to start: %s", desc->id,
		    error != NULL ? error->message : "unknown error");
		g_clear_error(&error);
		runtime->stopped = true;
		return runtime;
	}
	runtime->input = g_object_ref(
	    g_subprocess_get_stdin_pipe(runtime->process));
	runtime->output = g_data_input_stream_new(
	    g_subprocess_get_stdout_pipe(runtime->process));
	g_data_input_stream_set_newline_type(runtime->output,
	    G_DATA_STREAM_NEWLINE_TYPE_LF);
	send_geometry(runtime, "hello");
	widget_read_next(runtime);
	return runtime;
}

GtkWidget *
home_widget_runtime_view(struct home_widget_runtime *runtime)
{
	return runtime != NULL ? GTK_WIDGET(runtime->view) : NULL;
}

void
home_widget_runtime_set_visible(struct home_widget_runtime *runtime,
    bool visible)
{
	struct json_object *message;

	if (runtime == NULL || runtime->visible == visible)
		return;
	runtime->visible = visible;
	message = json_object_new_object();
	json_object_object_add(message, "ctlst_widget", json_object_new_int(1));
	json_object_object_add(message, "type", json_object_new_string("visibility"));
	json_object_object_add(message, "visible", json_object_new_boolean(visible));
	widget_send(runtime, message);
	json_object_put(message);
}

void
home_widget_runtime_cancel_pointer(struct home_widget_runtime *runtime)
{
	if (runtime == NULL)
		return;
	send_pointer(runtime, "cancel", runtime->pointer_x, runtime->pointer_y);
}

void
home_widget_runtime_free(struct home_widget_runtime *runtime)
{
	if (runtime == NULL)
		return;
	if (!runtime->stopped && runtime->input != NULL) {
		struct json_object *message = json_object_new_object();

		json_object_object_add(message, "ctlst_widget", json_object_new_int(1));
		json_object_object_add(message, "type", json_object_new_string("shutdown"));
		widget_send(runtime, message);
		json_object_put(message);
	}
	runtime->stopped = true;
	g_cancellable_cancel(runtime->cancel);
	if (runtime->process != NULL)
		g_subprocess_force_exit(runtime->process);
	if (runtime->frame != NULL)
		json_object_put(runtime->frame);
	g_clear_object(&runtime->output);
	g_clear_object(&runtime->input);
	g_clear_object(&runtime->process);
	g_clear_object(&runtime->theme_monitor);
	g_clear_object(&runtime->cancel);
	g_free(runtime);
}
