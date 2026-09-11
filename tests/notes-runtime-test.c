/* Actual GTK allocation -> widget protocol -> Notes frame. No user data. */
#include "../ctlsthome/home-widget-runtime.c"
#include <gtk4-layer-shell.h>

int main(void)
{
	const char *helper = g_getenv("CTLST_TEST_NOTES_HELPER");
	g_assert_nonnull(helper);
	gtk_init();
	g_log_set_always_fatal(G_LOG_FATAL_MASK | G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL);
	struct home_widget_desc desc = {.kind=HOME_WIDGET_KIND_EXEC};
	g_strlcpy(desc.id, "notes", sizeof(desc.id));
	g_strlcpy(desc.name, "Notes", sizeof(desc.name));
	g_strlcpy(desc.protocol, "ctlst-widget-1", sizeof(desc.protocol));
	g_strlcpy(desc.interaction, "launch", sizeof(desc.interaction));
	g_strlcpy(desc.exec, helper, sizeof(desc.exec));
	struct home_widget_runtime *runtime = home_widget_runtime_new(&desc);
	g_assert_nonnull(runtime);
	GtkWindow *window = GTK_WINDOW(gtk_window_new());
	gtk_layer_init_for_window(window);
	gtk_layer_set_layer(window, GTK_LAYER_SHELL_LAYER_OVERLAY);
	GtkWidget *view = home_widget_runtime_view(runtime);
	gtk_window_set_child(window, view);
	const int sizes[][2] = {{200,240}, {360,110}, {160,280}};
	for (unsigned i = 0; i < G_N_ELEMENTS(sizes); i++) {
		gtk_window_set_default_size(window, sizes[i][0], sizes[i][1]);
		gtk_window_present(window);
		bool matched = false;
		int64_t deadline = g_get_monotonic_time() + 3000000;
		while (!matched && g_get_monotonic_time() < deadline) {
			g_main_context_iteration(NULL, FALSE);
			struct json_object *scene = NULL;
			double w = 0, h = 0;
			if (runtime->frame && json_object_object_get_ex(runtime->frame, "view", &scene)) {
				json_number(scene, "width", &w); json_number(scene, "height", &h);
				matched = w == sizes[i][0] && h == sizes[i][1];
			}
			g_usleep(1000);
		}
		g_assert_true(matched);
		g_print("PASS: Notes follows native allocation %dx%d\n", sizes[i][0], sizes[i][1]);
	}
	gtk_window_destroy(window);
	home_widget_runtime_free(runtime);
	return 0;
}
