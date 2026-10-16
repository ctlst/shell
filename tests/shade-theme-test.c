/* Theme reload uses the production watcher without starting hardware providers. */
#define main shade_application_main
#include "../ctlstshade/ctlstshade.c"
#undef main

int main(int argc, char **argv)
{
	g_assert_cmpint(argc, ==, 2);
	char *original = NULL;
	g_assert_true(g_file_get_contents(argv[1], &original, NULL, NULL));
	char *home = g_dir_make_tmp("ctlst-shade-theme-XXXXXX", NULL);
	g_assert_nonnull(home);
	g_setenv("HOME", home, TRUE);
	g_setenv("XDG_CONFIG_HOME", home, TRUE);
	g_setenv("CTLST_GENERATED_DIR", home, TRUE);
	g_assert_cmpstr(g_get_home_dir(), ==, home);
	char *path = active_theme_path();
	char *directory = g_path_get_dirname(path);
	g_assert_cmpint(g_mkdir_with_parents(directory, 0700), ==, 0);
	g_assert_true(g_file_set_contents(path, original, -1, NULL));
	gtk_init();
	struct app app = {0};
	setup_theme(&app);
	g_assert_nonnull(app.theme_monitor);
	g_assert_cmpstr(app.theme_source, ==, original);
	char *cached = app.theme_source;
	load_theme(&app);
	g_assert_true(cached == app.theme_source);

	char *changed = g_strconcat(original,
	    "\n.shade-title { color: #123456; font-size: 31px; }\n", NULL);
	char *staged = g_strconcat(path, ".new", NULL);
	g_assert_true(g_file_set_contents(staged, changed, -1, NULL));
	g_assert_cmpint(g_rename(staged, path), ==, 0);
	gint64 deadline = g_get_monotonic_time() + 3000000;
	while (g_strcmp0(app.theme_source, changed) != 0 &&
	    g_get_monotonic_time() < deadline) {
		while (g_main_context_iteration(NULL, FALSE)) {}
		g_usleep(1000);
	}
	g_assert_cmpstr(app.theme_source, ==, changed);
	GtkWidget *label = gtk_label_new("Theme override");
	g_object_ref_sink(label);
	gtk_widget_add_css_class(label, "shade-title");
	GdkRGBA color;
	gtk_widget_get_color(label, &color);
	g_assert_cmpfloat_with_epsilon(color.red, 0x12 / 255.0, 0.001);
	g_assert_cmpfloat_with_epsilon(color.green, 0x34 / 255.0, 0.001);
	g_assert_cmpfloat_with_epsilon(color.blue, 0x56 / 255.0, 0.001);
	g_print("PASS: atomic theme replacement reloads without socket message; variant wins; duplicate reload is skipped\n");
	g_clear_object(&app.theme_monitor);
	gtk_style_context_remove_provider_for_display(gdk_display_get_default(),
	    GTK_STYLE_PROVIDER(app.css));
	g_object_unref(app.css);
	g_object_unref(label);
	g_free(app.theme_source);
	g_unlink(path);
	g_free(staged);
	g_free(changed);
	g_free(original);
	g_free(path);
	g_free(directory);
	g_free(home);
	return 0;
}
