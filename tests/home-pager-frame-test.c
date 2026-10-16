/* Real frame-clock coalescing, retained paint, and settled GTK hit testing. */
#define main home_application_main
#include "../ctlsthome/ctlsthome.c"
#undef main

static unsigned layouts, paints;
static void prepared(gpointer data, int width, int height)
{
	(void)data; (void)width; (void)height;
	layouts++;
}
static void draw(GtkDrawingArea *area, cairo_t *cr, int w, int h, gpointer data)
{
	(void)area; (void)w; (void)h; (void)data;
	paints++;
	cairo_set_source_rgb(cr, .2, .4, .6);
	cairo_paint(cr);
}
static void drain(void)
{
	gint64 until = g_get_monotonic_time() + 100000;
	while (g_get_monotonic_time() < until) {
		while (g_main_context_iteration(NULL, FALSE)) {}
		g_usleep(1000);
	}
}
int main(void)
{
	gtk_init();
	g_log_set_always_fatal(G_LOG_FATAL_MASK | G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL);
	struct home home = {.pager_gsk=true, .launcher_page_count=3,
	    .page_panel_count=3, .page_settle_commit=-1};
	home_config_set_defaults(&home.config);
	home.window = GTK_WINDOW(gtk_window_new());
	gtk_layer_init_for_window(home.window);
	gtk_layer_set_layer(home.window, GTK_LAYER_SHELL_LAYER_OVERLAY);
	gtk_window_set_default_size(home.window, 320, 640);
	home.page_strip = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, HOME_PAGER_SEAM_GUARD_PX);
	gtk_box_set_homogeneous(GTK_BOX(home.page_strip), TRUE);
	for (int i=0;i<3;i++) {
		GtkWidget *page = gtk_drawing_area_new();
		gtk_widget_set_hexpand(page, TRUE);
		gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(page), draw, NULL, NULL);
		gtk_box_append(GTK_BOX(home.page_strip), page);
	}
	GtkWidget *counted = home_allocation_host_new(home.page_strip, prepared, NULL, NULL);
	home.page_motion = home_pager_motion_new(counted);
	home_pager_motion_set_geometry(home.page_motion, 3, HOME_PAGER_SEAM_GUARD_PX);
	home.context_overlay = GTK_OVERLAY(gtk_overlay_new());
	gtk_overlay_set_child(home.context_overlay, GTK_WIDGET(home.page_motion));
	gtk_window_set_child(home.window, GTK_WIDGET(home.context_overlay));
	gtk_window_present(home.window); drain();
	home.page_drag_active = home.page_drag_owned = true;
	apply_page_drag_offset(&home, -1); drain();
	unsigned before_layouts = layouts, before_paints = paints;
	for (int frame=0;frame<5;frame++) {
		for (int sample=0;sample<100;sample++)
			queue_page_drag_frame(&home, -20-frame*20-sample/100.0);
		g_assert_cmpfloat(home.page_strip_x, !=, home.page_drag_target);
		guint source = home.page_drag_source;
		queue_page_drag_frame(&home, -20-frame*20-.99);
		g_assert_cmpuint(home.page_drag_source, ==, source);
		drain();
		g_assert_cmpfloat(home.page_strip_x, ==, home.page_drag_target);
		g_assert_cmpuint(home.page_drag_source, ==, 0);
	}
	g_assert_cmpuint(layouts, ==, before_layouts);
	g_assert_cmpuint(paints, ==, before_paints);
	/* A queued touch sample cannot overwrite release/settle on the next frame. */
	queue_page_drag_frame(&home, -200);
	double painted = home.page_strip_x;
	home_page_drag_end(NULL, -240, 0, &home);
	g_assert_cmpuint(home.page_drag_source, ==, 0);
	g_assert_cmpfloat(home.page_settle_from, ==, painted);
	cancel_page_settle(&home); finish_page_settle(&home); drain();
	g_assert_cmpint(home.launcher_current_page, ==, 1);
	GtkWidget *page = gtk_widget_get_next_sibling(gtk_widget_get_first_child(home.page_strip));
	graphene_point_t origin = GRAPHENE_POINT_INIT(0,0), mapped;
	g_assert_true(gtk_widget_compute_point(page, GTK_WIDGET(home.page_motion), &origin, &mapped));
	g_assert_cmpfloat_with_epsilon(mapped.x, 0, .01);
	g_assert_true(gtk_widget_pick(GTK_WIDGET(home.page_motion), 40, 40, GTK_PICK_DEFAULT) == page);
	/* Cancellation leaves no frame callback running at rest. */
	home.page_drag_active = home.page_drag_owned = true;
	queue_page_drag_frame(&home, 100);
	abort_page_settle(&home); drain();
	g_assert_cmpuint(home.page_drag_source, ==, 0);
	/* Edit guides use GTK coordinates throughout their motion. */
	home.layout_editing = true;
	home.page_drag_active = true;
	apply_page_drag_offset(&home, 20); drain();
	g_assert_true(gtk_widget_compute_point(home.page_strip, GTK_WIDGET(home.page_motion), &origin, &mapped));
	g_assert_cmpfloat_with_epsilon(mapped.x, home.page_strip_x, .01);
	gtk_window_destroy(home.window);
	g_print("PASS: input coalesced, no moving layouts/repaints, release continuity, settled picking, cancellation and edit coordinates\n");
	return 0;
}
