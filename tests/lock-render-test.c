/* Native GTK regression. Run only inside a disposable Wayland session. */
#define main lock_application_main
#include "../ctlstlock/ctlstlock.c"
#undef main
#include <assert.h>

static void snapshot_once(struct app *app)
{
	GtkSnapshot *snapshot = gtk_snapshot_new();
	draw_lock(app->canvas, snapshot, app);
	GskRenderNode *node = gtk_snapshot_free_to_node(snapshot);
	assert(node != NULL);
	/* The first node covers applications even with translucent theme colors. */
	GskRenderNode *background = gsk_container_node_get_child(node, 0);
	assert(gsk_render_node_get_node_type(background) == GSK_LINEAR_GRADIENT_NODE);
	gsize count;
	const GskColorStop *stops = gsk_linear_gradient_node_get_color_stops(background, &count);
	for (gsize i = 0; i < count; i++)
		assert(stops[i].color.alpha == 1);
	gsk_render_node_unref(node);
}

int main(void)
{
	gtk_init();
	struct app app = {0};
	init_theme(&app.theme);
	app.theme.bg.alpha = 0.1;
	app.theme.bg_top.alpha = 0.1;
	app.theme.bg_bottom.alpha = 0.1;
	app.canvas = g_object_ref_sink(g_object_new(lock_canvas_get_type(), NULL));
	((LockCanvas *)app.canvas)->app = &app;
	g_strlcpy(app.network, "Wi-Fi | A very long network name with a truncated UTF-8 byte \303", sizeof(app.network));
	g_strlcpy(app.battery, "+100%", sizeof(app.battery));
	int sizes[][2] = {{480,960}, {960,480}, {320,568}, {800,320}};
	for (unsigned j = 0; j < G_N_ELEMENTS(sizes); j++) {
		int width = sizes[j][0], height = sizes[j][1];
		gtk_widget_allocate(app.canvas, width, height, -1, NULL);
		snapshot_once(&app);
		PangoLayout *clock = g_object_ref(app.labels[LOCK_CLOCK]);
		for (int frame = 0; frame < 40; frame++) {
			app.drag_offset = -frame * 4;
			snapshot_once(&app);
			assert(app.labels[LOCK_CLOCK] == clock);
		}
		g_object_unref(clock);
		for (int label = 0; label < LOCK_LABEL_COUNT; label++) {
			if (!app.labels[label])
				continue;
			assert(pango_layout_get_width(app.labels[label]) <= width * PANGO_SCALE);
			assert(g_utf8_validate(pango_layout_get_text(app.labels[label]), -1, NULL));
		}
		int clock_height, date_height;
		pango_layout_get_pixel_size(app.labels[LOCK_CLOCK], NULL, &clock_height);
		pango_layout_get_pixel_size(app.labels[LOCK_DATE], NULL, &date_height);
		assert(fmax(82, height * .23) + clock_height + 8 + date_height < height - 110);
		assert(height >= 420 || app.labels[LOCK_HINT] == NULL);
		app.drag_offset = 0;
	}
	clear_labels(&app);
	g_object_unref(app.canvas);
	g_print("PASS: privacy geometry, retained text, UTF-8 and opaque background\n");
	return 0;
}
