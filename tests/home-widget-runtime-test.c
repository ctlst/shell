/* Run under a disposable Wayland display. Tests real helper pipes and GTK. */
#include "../ctlsthome/home-widget-runtime.c"

static unsigned int draws;
static void counted_draw(GtkDrawingArea *view, cairo_t *cr, int width, int height, gpointer data)
{
	draws++;
	draw_widget(view, cr, width, height, data);
}

static void spin(int milliseconds)
{
	gint64 until = g_get_monotonic_time() + milliseconds * 1000;
	while (g_get_monotonic_time() < until) {
		while (g_main_context_iteration(NULL, FALSE));
		g_usleep(1000);
	}
}

int main(void)
{
	char *config = g_dir_make_tmp("ctlst-theme-test-XXXXXX", NULL);
	g_setenv("XDG_CONFIG_HOME", config, TRUE);
	char *folder = g_build_filename(config, "ctlst", NULL);
	g_mkdir_with_parents(folder, 0700);
	char *theme = g_build_filename(folder, "theme.env", NULL);
	g_file_set_contents(theme, "SHELL_TEXT=17151bff\nSHELL_PANEL=ffffffff\nSHELL_ACCENT=6f24cfff\n", -1, NULL);
	gtk_init();
	struct home_widget_desc desc = {.kind=HOME_WIDGET_KIND_EXEC};
	g_strlcpy(desc.id, "test", sizeof(desc.id));
	g_strlcpy(desc.protocol, "ctlst-widget-1", sizeof(desc.protocol));
	g_strlcpy(desc.exec, "python3", sizeof(desc.exec));
	desc.args_count=2;
	g_strlcpy(desc.args[0], "-c", sizeof(desc.args[0]));
	/* cat-like helper gives complete protocol frames back asynchronously. */
	g_strlcpy(desc.args[1], "import sys\nfor s in sys.stdin: print(s,end='',flush=True)", sizeof(desc.args[1]));
	struct home_widget_runtime *r = home_widget_runtime_new(&desc);
	GtkWidget *window = gtk_window_new();
	gtk_drawing_area_set_draw_func(r->view, counted_draw, r, NULL);
	gtk_window_set_child(GTK_WINDOW(window), GTK_WIDGET(r->view));
	gtk_window_present(GTK_WINDOW(window));
	g_assert_cmpstr(resolve_color(r, "@ctlst_text", ""), ==, "#17151bff");
	g_assert_cmpstr(resolve_color(r, "@ctlst_panel", ""), ==, "#ffffffff");
	g_assert_cmpstr(resolve_color(r, "@ctlst_accent", ""), ==, "#6f24cfff");
	remove(theme); rmdir(folder); rmdir(config);
	g_free(theme); g_free(folder); g_free(config);
	spin(100);
	const char *raw="{\"ctlst_widget\":1,\"type\":\"frame\",\"seq\":1,\"view\":{\"width\":100,\"height\":100},\"nodes\":[]}";
	struct json_object *f=json_tokener_parse(raw);
	widget_send(r,f);spin(150);
	g_assert_nonnull(r->frame);
	struct json_object *first=json_object_get(r->frame);
	unsigned int painted = draws;
	home_widget_runtime_set_visible(r,false);
	for(int i=2;i<=20;i++) {
		json_object_object_add(f,"seq",json_object_new_int(i));
		struct json_object *view;
		json_object_object_get_ex(f,"view",&view);
		json_object_object_add(view,"background",json_object_new_string(i%2 ? "#000000" : "#ffffff"));
		widget_send(r,f);
	}
	spin(150);
	g_assert_true(r->frame==first);
	g_assert_cmpuint(draws,==,painted);
	g_assert_cmpuint(r->render_source,==,0);
	g_assert_nonnull(r->pending_frame);
	home_widget_runtime_set_visible(r,true);
	spin(100);
	struct json_object *seq;
	g_assert_true(json_object_object_get_ex(r->frame,"seq",&seq));
	g_assert_cmpint(json_object_get_int(seq),==,20);
	g_assert_null(r->pending_frame);
	g_assert_cmpuint(draws,==,painted+1);
	gtk_window_destroy(GTK_WINDOW(window));
	json_object_put(first);
	json_object_put(f);
	home_widget_runtime_free(r);spin(100);

	/* Stalled helper: writes are bounded, nonblocking, and safe to cancel. */
	g_strlcpy(desc.args[1],"import time; time.sleep(30)",sizeof(desc.args[1]));
	r=home_widget_runtime_new(&desc);
	f=json_object_new_object();
	char large[8192];memset(large,'x',sizeof(large)-1);large[sizeof(large)-1]=0;
	json_object_object_add(f,"text",json_object_new_string(large));
	gint64 start=g_get_monotonic_time();
	for(int i=0;i<100;i++)widget_send(r,f);
	g_assert_cmpint(g_get_monotonic_time()-start,<,500000);
	g_assert_true(r->stopped);
	g_assert_cmpuint(g_queue_get_length(&r->outgoing),<=,WIDGET_SEND_MAX);
	json_object_put(f);
	home_widget_runtime_free(r);spin(100);
	g_print("PASS: hidden frames retained, latest-frame catch-up, bounded nonblocking helper writes, cancellation lifetime\n");
	return 0;
}
