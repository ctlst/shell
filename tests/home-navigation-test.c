/* Isolated GTK navigation fixture; no providers or real applications. */
#define main home_application_main
#include "../ctlsthome/ctlsthome.c"
#undef main

static void activated(GtkButton *button, gpointer data)
{
	(void)button;
	(*(int *)data)++;
}

static void settle(void)
{
	gint64 deadline = g_get_monotonic_time() + 300000;
	while (g_get_monotonic_time() < deadline) {
		g_main_context_iteration(NULL, FALSE);
		g_usleep(1000);
	}
}

int main(void)
{
	gtk_init();
	struct home home = {.visible = true, .launcher_current_page = 0};
	home.window = GTK_WINDOW(gtk_window_new());
	home.root = gtk_fixed_new();
	gtk_widget_set_focusable(home.root, TRUE);
	gtk_window_set_child(home.window, home.root);
	gtk_window_set_default_size(home.window, 400, 400);
	home.launcher_icons = g_ptr_array_new();
	struct home_launcher_icon icons[3] = {0};
	int activations = 0;
	for (int i = 0; i < 3; i++) {
		icons[i].button = gtk_button_new_with_label("Fixture");
		icons[i].page = i == 2 ? 1 : 0;
		gtk_widget_set_size_request(icons[i].button, 80, 80);
		gtk_fixed_put(GTK_FIXED(home.root), icons[i].button, i * 110, 10);
		g_signal_connect(icons[i].button, "clicked", G_CALLBACK(activated), &activations);
		g_ptr_array_add(home.launcher_icons, &icons[i]);
	}
	GtkWidget *entry = gtk_entry_new();
	gtk_fixed_put(GTK_FIXED(home.root), entry, 10, 200);
	gtk_window_present(home.window);
	settle();
	gtk_widget_grab_focus(home.root);
	g_assert_true(home_navigation_key(NULL, GDK_KEY_Right, 0, 0, &home));
	g_assert_true(gtk_window_get_focus(home.window) == icons[0].button);
	g_assert_true(gtk_window_get_focus_visible(home.window));
	home_navigation_key(NULL, GDK_KEY_Right, 0, 0, &home);
	g_assert_true(gtk_window_get_focus(home.window) == icons[1].button);
	home_navigation_key(NULL, GDK_KEY_Right, 0, 0, &home);
	g_assert_true(gtk_window_get_focus(home.window) == icons[1].button);
	g_assert_false(home_navigation_key(NULL, GDK_KEY_Left, 0, GDK_SUPER_MASK, &home));
	home_navigation_key(NULL, GDK_KEY_Return, 0, 0, &home);
	settle();
	g_assert_cmpint(activations, ==, 1);
	home.launcher_current_page = 1;
	g_assert_true(home_navigation_key(NULL, GDK_KEY_Return, 0, 0, &home));
	settle();
	g_assert_cmpint(activations, ==, 1);
	home_navigation_key(NULL, GDK_KEY_Right, 0, 0, &home);
	g_assert_true(gtk_window_get_focus(home.window) == icons[2].button);
	gtk_widget_grab_focus(entry);
	g_assert_false(home_navigation_key(NULL, GDK_KEY_Left, 0, 0, &home));
	home.visible = false;
	g_assert_false(home_navigation_key(NULL, GDK_KEY_Right, 0, 0, &home));
	g_print("PASS: visible Home selection, spatial arrows, current-page isolation, Enter, text and modifier pass-through\n");
	gtk_window_destroy(home.window);
	g_ptr_array_unref(home.launcher_icons);
	return 0;
}
