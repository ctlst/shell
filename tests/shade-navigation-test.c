/* Native control navigation only: no radios, providers or brightness writes. */
#define main shade_application_main
#include "../ctlstshade/ctlstshade.c"
#undef main

int main(void)
{
	gtk_init();
	struct app app = {0};
	app.window = GTK_WINDOW(gtk_window_new());
	GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	GtkWidget *first = gtk_button_new_with_label("First");
	GtkWidget *second = gtk_button_new_with_label("Second");
	GtkWidget *scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
	gtk_box_append(GTK_BOX(row), first);
	gtk_box_append(GTK_BOX(row), second);
	gtk_box_append(GTK_BOX(row), scale);
	gtk_window_set_child(app.window, row);
	gtk_window_present(app.window);
	gint64 deadline = g_get_monotonic_time() + 200000;
	while (g_get_monotonic_time() < deadline) {
		g_main_context_iteration(NULL, FALSE);
		g_usleep(1000);
	}
	gtk_widget_grab_focus(first);
	g_assert_true(key_pressed(NULL, GDK_KEY_Right, 0, 0, &app));
	g_assert_true(gtk_window_get_focus(app.window) == second);
	g_assert_true(gtk_window_get_focus_visible(app.window));
	g_assert_false(key_pressed(NULL, GDK_KEY_Right, 0, GDK_SUPER_MASK, &app));
	gtk_widget_grab_focus(scale);
	g_assert_false(key_pressed(NULL, GDK_KEY_Left, 0, 0, &app));
	g_assert_cmpfloat(gtk_range_get_value(GTK_RANGE(scale)), ==, 0);
	g_print("PASS: shade arrow traversal, visible focus, modifier and slider pass-through\n");
	gtk_window_destroy(app.window);
	return 0;
}
