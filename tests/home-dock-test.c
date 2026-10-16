/* Native dock/grid transfers, transformed hit tests and persisted membership.
 * Run with a disposable HOME and the ctlst-preferences helper installed there. */
#define main home_application_main
#include "../ctlsthome/ctlsthome.c"
#undef main

static void settle(void)
{
	int64_t until = g_get_monotonic_time() + 100000;
	while (g_get_monotonic_time() < until) {
		g_main_context_iteration(NULL, FALSE);
		g_usleep(1000);
	}
}

static graphene_point_t point(struct home *home, GtkWidget *from, float x, float y)
{
	graphene_point_t in = GRAPHENE_POINT_INIT(x, y), out;
	g_assert_true(gtk_widget_compute_point(from, GTK_WIDGET(home->context_overlay), &in, &out));
	return out;
}

static graphene_point_t center(struct home *home, GtkWidget *widget)
{
	return point(home, widget, gtk_widget_get_width(widget)/2.f, gtk_widget_get_height(widget)/2.f);
}

static void drag_to(struct home *home, struct home_launcher_icon *icon, graphene_point_t target)
{
	graphene_point_t start = center(home, icon->button);
	home->launcher_press_x = start.x;
	home->launcher_press_y = start.y;
	start_launcher_icon_drag(home, icon);
	for (int frame = 1; frame <= 30; frame++)
		update_launcher_icon_drag(home, (target.x-start.x)*frame/30.,
		    (target.y-start.y)*frame/30.);
	settle();
	end_launcher_icon_drag(home, target.x-start.x, target.y-start.y);
	settle();
}

int main(void)
{
	gtk_init();
	g_log_set_always_fatal(G_LOG_FATAL_MASK | G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL);
	struct home home = {.pager_gsk=true, .launcher_page_count=3,
	    .page_panel_count=3, .haptic_fd=-1, .haptic_effect_id=-1};
	home_config_set_defaults(&home.config);
	GtkCssProvider *css = gtk_css_provider_new();
	gtk_css_provider_load_from_string(css, style_css);
	gtk_style_context_add_provider_for_display(gdk_display_get_default(),
	    GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	home.page_strip = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
	gtk_box_set_homogeneous(GTK_BOX(home.page_strip), TRUE);
	for (int i = 0; i < 3; i++) {
		home.page_panels[i] = make_icon_page_panel(&home);
		home.page_grids[i] = g_object_get_data(G_OBJECT(home.page_panels[i]), "home-page-grid");
		gtk_box_append(GTK_BOX(home.page_strip), home.page_panels[i]);
	}
	home.context_body = home.page_panels[0];
	home.context_grid = home.page_grids[0];
	home.page_motion = home_pager_motion_new(home.page_strip);
	home.page_motion->prepare = prepare_home_pager_allocation;
	home.page_motion->prepare_data = &home;
	home_pager_motion_set_geometry(home.page_motion, 3, 16);
	home.page_viewport = GTK_WIDGET(home.page_motion);
	GtkWidget *scene = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	home.scene_content = scene;
	gtk_widget_set_hexpand(home.page_viewport, TRUE);
	gtk_widget_set_vexpand(home.page_viewport, TRUE);
	gtk_box_append(GTK_BOX(scene), home.page_viewport);
	home.tray = make_home_tray(&home);
	gtk_box_append(GTK_BOX(scene), home.tray);
	home.edit_scene = GTK_OVERLAY(gtk_overlay_new());
	gtk_overlay_set_child(home.edit_scene, scene);
	home.grid_overlay = GTK_DRAWING_AREA(gtk_drawing_area_new());
	gtk_widget_set_can_target(GTK_WIDGET(home.grid_overlay), FALSE);
	gtk_overlay_add_overlay(home.edit_scene, GTK_WIDGET(home.grid_overlay));
	home.edit_controls = gtk_button_new_with_label("Done");
	gtk_widget_set_size_request(home.edit_controls, 100, 60);
	home.edit_frame = home_edit_frame_new(GTK_WIDGET(home.edit_scene), home.edit_controls);
	home.context_overlay = GTK_OVERLAY(gtk_overlay_new());
	gtk_overlay_set_child(home.context_overlay, GTK_WIDGET(home.edit_frame));
	home.window = GTK_WINDOW(gtk_window_new());
	gtk_widget_set_name(GTK_WIDGET(home.window), "ctlsthome");
	gtk_layer_init_for_window(home.window);
	gtk_layer_set_layer(home.window, GTK_LAYER_SHELL_LAYER_OVERLAY);
	gtk_window_set_child(home.window, GTK_WIDGET(home.context_overlay));
	home.launcher_icons = g_ptr_array_new_with_free_func(launcher_icon_free);
	seed_default_dock(&home);
	guint initial = home.launcher_icons->len;
	g_assert_cmpuint(initial, <=, 1);
	seed_default_dock(&home);
	g_assert_cmpuint(home.launcher_icons->len, ==, initial);
	g_ptr_array_set_size(home.launcher_icons, 0);
	const char *fixture_apps[] = {"dev.ctlst.Dialer.desktop", "dev.ctlst.Messages.desktop",
	    "dev.ctlst.Camera.desktop", "test.browser.desktop"};
	for (unsigned i = 0; i < G_N_ELEMENTS(fixture_apps); i++) {
		struct home_launcher_icon *icon = g_new0(struct home_launcher_icon, 1);
		*icon = (struct home_launcher_icon){.home=&home, .desktop_id=g_strdup(fixture_apps[i]),
		    .page=HOME_DOCK_PAGE, .column=(int)i};
		g_ptr_array_add(home.launcher_icons, icon);
	}
	struct home_launcher_icon *phone = launcher_icon_for_desktop_id(&home, "dev.ctlst.Dialer.desktop");
	struct home_launcher_icon *app = g_new0(struct home_launcher_icon, 1);
	*app = (struct home_launcher_icon){.home=&home, .desktop_id=g_strdup("dev.ctlst.LinuxToolkit.desktop"),
	    .page=2, .column=1, .row=4};
	g_ptr_array_add(home.launcher_icons, app);
	for (guint i = 0; i < home.launcher_icons->len; i++) {
		struct home_launcher_icon *icon = g_ptr_array_index(home.launcher_icons, i);
		ensure_launcher_button(&home, icon);
		g_object_set_data(G_OBJECT(icon->button), "launcher-icon", icon);
	}
	home.layout_editing = true;
	home_edit_frame_configure(home.edit_frame, true, 12, 12);
	home.launcher_current_page = 2;
	const int sizes[][2] = {{480,902}, {960,422}, {640,360}, {480,902}};
	for (guint n = 0; n < G_N_ELEMENTS(sizes); n++) {
		home.landscape = sizes[n][0] > sizes[n][1];
		layout_home_tray(&home, home.landscape);
		gtk_window_set_default_size(home.window, sizes[n][0], sizes[n][1]);
		gtk_window_present(home.window);
		render_launcher_page(&home);
		settle();
		graphene_rect_t tray_bounds, page_bounds;
		g_assert_true(gtk_widget_compute_bounds(home.tray, scene, &tray_bounds));
		g_assert_true(gtk_widget_compute_bounds(home.page_viewport, scene, &page_bounds));
		if (home.landscape) {
			g_assert_cmpfloat(tray_bounds.origin.x, >=, page_bounds.origin.x + page_bounds.size.width - 1);
			g_assert_cmpfloat(tray_bounds.size.width, <=, 90);
			g_assert_cmpfloat(page_bounds.size.height, >=, gtk_widget_get_height(scene) - 1);
		} else {
			g_assert_cmpfloat(tray_bounds.origin.y, >=, page_bounds.origin.y + page_bounds.size.height - 1);
			g_assert_cmpfloat(tray_bounds.size.height, <=, 90);
		}
		/* Paging must not move the tray or cause a new tray allocation. */
		graphene_rect_t before = tray_bounds;
		set_page_strip_x(&home, home.page_strip_x + 40);
		settle();
		g_assert_true(gtk_widget_compute_bounds(home.tray, scene, &tray_bounds));
		g_assert_true(graphene_rect_equal(&before, &tray_bounds));
		set_page_strip_x(&home, home.page_strip_x - 40);
		settle();
		graphene_point_t hit = center(&home, phone->button);
		g_assert_true(home_launcher_icon_at(&home, hit.x, hit.y) == phone);
		g_assert_false(home_edit_page_swipe_at(&home, hit.x, hit.y));
		/* Desktop to dock, with a fifth icon and no duplicate. */
		graphene_point_t dock = point(&home, home.tray,
		    home.landscape ? gtk_widget_get_width(home.tray)/2.f : gtk_widget_get_width(home.tray)-10,
		    home.landscape ? gtk_widget_get_height(home.tray)-10 : gtk_widget_get_height(home.tray)/2.f);
		drag_to(&home, app, dock);
		g_assert_cmpint(app->page, ==, HOME_DOCK_PAGE);
		g_assert_cmpuint(home.launcher_icons->len, ==, 5);
		g_assert_true(gtk_widget_get_parent(app->button) == home.tray);
		/* Dock to the last row of page three. */
		double cw, ch;
		g_assert_true(grid_cell_size(&home, &cw, &ch));
		graphene_point_t cell = point(&home, home.page_panels[2],
		    4*(cw+home_spacing(&home))+cw/2, 6*(ch+home_spacing(&home))+ch/2);
		drag_to(&home, phone, cell);
		g_assert_cmpint(phone->page, ==, 2);
		g_assert_cmpint(phone->column, ==, 4);
		g_assert_cmpint(phone->row, ==, 6);
		/* Toolbar drops cannot remove or relocate a dock app. */
		drag_to(&home, app, center(&home, home.edit_controls));
		g_assert_cmpint(app->page, ==, HOME_DOCK_PAGE);
		/* A widget blocks the destination without losing the dock icon. */
		home.grid_item_count = 1;
		home.grid_items[0] = (struct home_grid_item){.id="blocker",
		    .page=2, .column=2, .row=4, .column_span=1, .row_span=1};
		home_widget_container(&home, &home.grid_items[0], gtk_label_new("Occupied"));
		cell = point(&home, home.page_panels[2], 2*(cw+home_spacing(&home))+cw/2,
		    4*(ch+home_spacing(&home))+ch/2);
		drag_to(&home, app, cell);
		g_assert_cmpint(app->page, ==, HOME_DOCK_PAGE);
		g_object_unref(home.grid_items[0].container);
		home.grid_item_count = 0;
		/* Restore fixture positions and reorder Phone back to dock slot zero. */
		cell = point(&home, home.page_panels[2], cw+home_spacing(&home)+cw/2,
		    4*(ch+home_spacing(&home))+ch/2);
		drag_to(&home, app, cell);
		g_assert_cmpint(app->page, ==, 2);
		dock = point(&home, home.tray, home.landscape ? gtk_widget_get_width(home.tray)/2.f : 2,
		    home.landscape ? 2 : gtk_widget_get_height(home.tray)/2.f);
		drag_to(&home, phone, dock);
		g_assert_cmpint(phone->page, ==, HOME_DOCK_PAGE);
		g_assert_cmpint(phone->column, ==, 0);
		g_assert_true(gtk_widget_get_first_child(home.tray) == phone->button);
		g_print("PASS: %dx%d dock/grid transfer, bounds, transformed picking and ordering\n", sizes[n][0], sizes[n][1]);
	}
	save_home_layout(&home);
	struct home loaded = {0};
	home_config_set_defaults(&loaded.config);
	loaded.launcher_icons = g_ptr_array_new_with_free_func(launcher_icon_free);
	load_home_layout(&loaded);
	g_assert_true(loaded.dock_initialized);
	seed_default_dock(&loaded);
	g_assert_cmpuint(loaded.launcher_icons->len, ==, 5);
	g_assert_cmpint(launcher_icon_for_desktop_id(&loaded, phone->desktop_id)->page, ==, HOME_DOCK_PAGE);
	g_assert_cmpint(launcher_icon_for_desktop_id(&loaded, app->desktop_id)->page, ==, 2);
	/* An explicitly empty dock stays empty on reload. */
	for (guint i = 0; i < loaded.launcher_icons->len; i++) {
		struct home_launcher_icon *icon = g_ptr_array_index(loaded.launcher_icons, i);
		icon->page = 1;
	}
	save_home_layout(&loaded);
	g_ptr_array_set_size(loaded.launcher_icons, 0);
	load_home_layout(&loaded);
	seed_default_dock(&loaded);
	for (guint i = 0; i < loaded.launcher_icons->len; i++)
		g_assert_cmpint(((struct home_launcher_icon *)g_ptr_array_index(loaded.launcher_icons,i))->page, ==, 1);
	g_print("PASS: dock membership/order and explicit empty dock survive reload without duplicates\n");
	return 0;
}
