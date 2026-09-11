/* Later-page picking, edit guides and production move/resize calculations.
 * No providers, external apps or personal layout are used. */
#define main home_application_main
#include "../ctlsthome/ctlsthome.c"
#undef main

static graphene_point_t point(GtkWidget *from, GtkWidget *to, float x, float y)
{
	graphene_point_t in = GRAPHENE_POINT_INIT(x, y), out;
	g_assert_true(gtk_widget_compute_point(from, to, &in, &out));
	return out;
}

static void count_layout(gpointer data, int width, int height)
{
	(void)width; (void)height;
	(*(unsigned *)data)++;
}

int main(void)
{
	gtk_init();
	g_log_set_always_fatal(G_LOG_FATAL_MASK | G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL);
	struct home home = {.pager_gsk=true, .launcher_page_count=3,
	    .page_panel_count=3, .haptic_fd=-1, .haptic_effect_id=-1};
	home_config_set_defaults(&home.config);
	home.page_strip = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
	gtk_box_set_homogeneous(GTK_BOX(home.page_strip), TRUE);
	for (int i = 0; i < 3; i++) {
		home.page_panels[i] = make_icon_page_panel(&home);
		home.page_grids[i] = g_object_get_data(G_OBJECT(home.page_panels[i]), "home-page-grid");
		gtk_box_append(GTK_BOX(home.page_strip), home.page_panels[i]);
	}
	home.context_body = home.page_panels[0];
	home.context_grid = home.page_grids[0];
	unsigned layouts = 0;
	GtkWidget *counted = home_allocation_host_new(home.page_strip, count_layout, NULL, &layouts);
	home.page_motion = home_pager_motion_new(counted);
	home_pager_motion_set_geometry(home.page_motion, 3, 16);
	home.edit_scene = GTK_OVERLAY(gtk_overlay_new());
	gtk_overlay_set_child(home.edit_scene, GTK_WIDGET(home.page_motion));
	home.grid_overlay = GTK_DRAWING_AREA(gtk_drawing_area_new());
	gtk_widget_set_can_target(GTK_WIDGET(home.grid_overlay), FALSE);
	gtk_overlay_add_overlay(home.edit_scene, GTK_WIDGET(home.grid_overlay));
	home.edit_controls = gtk_button_new_with_label("Done");
	gtk_widget_set_size_request(home.edit_controls, 100, 60);
	home.edit_frame = home_edit_frame_new(GTK_WIDGET(home.edit_scene), home.edit_controls);
	home.context_overlay = GTK_OVERLAY(g_object_ref_sink(gtk_overlay_new()));
	gtk_overlay_set_child(home.context_overlay, GTK_WIDGET(home.edit_frame));
	home.window = GTK_WINDOW(gtk_window_new());
	gtk_layer_init_for_window(home.window);
	gtk_layer_set_layer(home.window, GTK_LAYER_SHELL_LAYER_OVERLAY);
	gtk_window_set_child(home.window, GTK_WIDGET(home.context_overlay));
	home.launcher_icons = g_ptr_array_new();
	struct home_launcher_icon icon = {.home=&home, .desktop_id="test.desktop",
	    .column=1, .row=5};
	icon.button = g_object_ref_sink(gtk_button_new_with_label("A"));
	g_ptr_array_add(home.launcher_icons, &icon);
	home.grid_item_count = 1;
	struct home_grid_item *item = &home.grid_items[0];
	*item = (struct home_grid_item){.id="clock", .column=1, .row=1,
	    .column_span=2, .row_span=2};
	home_widget_container(&home, item, gtk_label_new("Test widget"));
	const int sizes[][2] = {{480,902}, {640,300}};
	for (unsigned s = 0; s < G_N_ELEMENTS(sizes); s++) {
		int width = sizes[s][0], height = sizes[s][1];
		gtk_window_set_default_size(home.window, width, height);
		gtk_window_present(home.window);
		int64_t until = g_get_monotonic_time() + 200000;
		while (g_get_monotonic_time() < until) {
			g_main_context_iteration(NULL, FALSE);
			g_usleep(1000);
		}
		for (int page = 1; page <= 2; page++) {
			for (int edit = 0; edit <= 1; edit++) {
				home.launcher_current_page = icon.page = item->page = page;
				home.layout_editing = edit;
				home_edit_frame_configure(home.edit_frame, edit, 12, 12);
				if (gtk_widget_get_parent(icon.button)) launcher_unparent_button(icon.button);
				attach_launcher_icon_to_page(&home, &icon);
				g_object_ref(item->container);
				if (gtk_widget_get_parent(item->container)) gtk_widget_unparent(item->container);
				gtk_grid_attach(home.page_grids[page], item->container, 1, 1, 2, 2);
				g_object_unref(item->container);
				refresh_grid_editing(&home);
				home_pager_motion_set_translation(home.page_motion, -page * (width + 16));
				gtk_widget_allocate(GTK_WIDGET(home.context_overlay), width, height, -1, NULL);
				graphene_point_t origin = point(GTK_WIDGET(home.page_grids[page]),
				    GTK_WIDGET(home.page_motion), 0, 0);
				g_assert_cmpfloat_with_epsilon(origin.x, 0, .01);
				graphene_point_t hit = point(item->container, GTK_WIDGET(home.context_overlay),
				    gtk_widget_get_width(item->container)/2.0f, gtk_widget_get_height(item->container)/2.0f);
				double lx, ly;
				GtkWidget *picked = gtk_widget_pick(GTK_WIDGET(home.context_overlay), hit.x, hit.y, GTK_PICK_DEFAULT);
				g_print("probe page=%d edit=%d hit=%.1f,%.1f picked=%s item=%dx%d\n",
				    page, edit, hit.x, hit.y, picked ? G_OBJECT_TYPE_NAME(picked) : "none",
				    gtk_widget_get_width(item->container), gtk_widget_get_height(item->container));
				g_assert_true(home_grid_item_at(&home, hit.x, hit.y, &lx, &ly) == item);
				hit = point(icon.button, GTK_WIDGET(home.context_overlay),
				    gtk_widget_get_width(icon.button)/2.0f, gtk_widget_get_height(icon.button)/2.0f);
				g_assert_true(home_launcher_icon_at(&home, hit.x, hit.y) == &icon);
				double gx, gy;
				int col, row;
				g_assert_true(overlay_to_grid_point(&home, hit.x, hit.y, &gx, &gy));
				g_assert_true(grid_point_to_cell(&home, gx, gy, &col, &row));
				g_assert_cmpint(col, ==, 1); g_assert_cmpint(row, ==, 5);
				if (edit) {
					/* The actual grid painter must paint on-screen, not one page offscreen. */
					cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
					cairo_t *cr = cairo_create(surface);
					home.accent_rgb[0] = 1;
					draw_grid_overlay(home.grid_overlay, cr, width, height, &home);
					cairo_surface_flush(surface);
					unsigned char *pixels = cairo_image_surface_get_data(surface);
					unsigned painted = 0;
					for (int n = 0; n < cairo_image_surface_get_stride(surface)*height; n++) painted += pixels[n] != 0;
					g_assert_cmpuint(painted, >, 100);
					cairo_destroy(cr); cairo_surface_destroy(surface);
					double cw, ch; g_assert_true(grid_cell_size(&home, &cw, &ch));
					double dx = (cw + home_spacing(&home))*home.edit_frame->scale;
					double dy = (ch + home_spacing(&home))*home.edit_frame->scale;
					int cs, rs;
					start_home_widget_drag(&home, item, 0, 0, 0);
					g_assert_true(home_widget_target_from_drag(&home, item, dx, 0, &col, &row, &cs, &rs));
					g_assert_cmpint(col, ==, 2); g_assert_cmpint(row, ==, 1);
					start_home_widget_drag(&home, item, HOME_RESIZE_RIGHT | HOME_RESIZE_BOTTOM, 0, 0);
					g_assert_true(home_widget_target_from_drag(&home, item, dx, dy, &col, &row, &cs, &rs));
					g_assert_cmpint(cs, ==, 3); g_assert_cmpint(rs, ==, 3);
					clear_drag_layout_snapshot(&home);
					home.active_drag_item = NULL;
				}
				/* Translation must not relayout retained page content every frame. */
				unsigned before = layouts;
				for (int frame = 1; frame <= 30; frame++) {
					home_pager_motion_set_translation(home.page_motion, -page*(width+16) + frame);
					gtk_widget_allocate(GTK_WIDGET(home.page_motion), width, height, -1, NULL);
				}
				g_assert_cmpuint(layouts, ==, before);
				g_print("PASS: %dx%d page %d edit=%d picking, grid, move/resize, cached layout\n", width, height, page+1, edit);
			}
		}
	}
	return 0;
}
