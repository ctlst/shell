/* Grid geometry belongs to Home, not the minimum size of card contents.
 * Fit unusually small cells using GTK transforms, including input picking. */
#ifndef HOME_CELL_CONTENT_H
#define HOME_CELL_CONTENT_H
#include <gtk/gtk.h>

typedef struct {
	GtkWidget parent_instance;
	GtkWidget *child;
} HomeCellContent;
typedef GtkWidgetClass HomeCellContentClass;
G_DEFINE_TYPE(HomeCellContent, home_cell_content, GTK_TYPE_WIDGET)

static void home_cell_content_measure(GtkWidget *widget, GtkOrientation orientation,
    int for_size, int *minimum, int *natural, int *min_baseline, int *nat_baseline)
{
	(void)widget; (void)orientation; (void)for_size;
	*minimum = *natural = 0;
	*min_baseline = *nat_baseline = -1;
}

static void home_cell_content_size_allocate(GtkWidget *widget, int width, int height,
    int baseline)
{
	HomeCellContent *host = (HomeCellContent *)widget;
	int min_width, min_height;
	gtk_widget_measure(host->child, GTK_ORIENTATION_HORIZONTAL, -1,
	    &min_width, NULL, NULL, NULL);
	gtk_widget_measure(host->child, GTK_ORIENTATION_VERTICAL, MAX(width, min_width),
	    &min_height, NULL, NULL, NULL);
	float scale = MIN(1.f, MIN((float)MAX(1, width) / MAX(1, min_width),
	    (float)MAX(1, height) / MAX(1, min_height)));
	gtk_widget_allocate(host->child, MAX(min_width, (int)(width / scale)),
	    MAX(min_height, (int)(height / scale)), baseline,
	    gsk_transform_scale(NULL, scale, scale));
}

static void home_cell_content_snapshot(GtkWidget *widget, GtkSnapshot *snapshot)
{
	gtk_widget_snapshot_child(widget, ((HomeCellContent *)widget)->child, snapshot);
}

static void home_cell_content_dispose(GObject *object)
{
	g_clear_pointer(&((HomeCellContent *)object)->child, gtk_widget_unparent);
	G_OBJECT_CLASS(home_cell_content_parent_class)->dispose(object);
}

static void home_cell_content_class_init(HomeCellContentClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
	widget_class->measure = home_cell_content_measure;
	widget_class->size_allocate = home_cell_content_size_allocate;
	widget_class->snapshot = home_cell_content_snapshot;
	G_OBJECT_CLASS(klass)->dispose = home_cell_content_dispose;
}

static void home_cell_content_init(HomeCellContent *host)
{
	gtk_widget_set_hexpand(GTK_WIDGET(host), TRUE);
	gtk_widget_set_vexpand(GTK_WIDGET(host), TRUE);
	gtk_widget_set_overflow(GTK_WIDGET(host), GTK_OVERFLOW_HIDDEN);
}

static GtkWidget *home_cell_content_new(GtkWidget *child)
{
	HomeCellContent *host = g_object_new(home_cell_content_get_type(), NULL);
	host->child = child;
	gtk_widget_set_parent(child, GTK_WIDGET(host));
	return GTK_WIDGET(host);
}
#endif
