/* Bounded, unscaled Home picker. Its child never dictates the output size. */
#ifndef HOME_PICKER_HOST_H
#define HOME_PICKER_HOST_H
#include <gtk/gtk.h>

typedef struct { GtkWidget parent_instance; GtkWidget *child; } HomePickerHost;
typedef GtkWidgetClass HomePickerHostClass;
G_DEFINE_TYPE(HomePickerHost, home_picker_host, GTK_TYPE_WIDGET)

static void home_picker_host_measure(GtkWidget *widget, GtkOrientation orientation,
    int for_size, int *minimum, int *natural, int *min_baseline, int *nat_baseline)
{
	(void)widget; (void)orientation; (void)for_size;
	*minimum = *natural = 0;
	*min_baseline = *nat_baseline = -1;
}

static void home_picker_host_size_allocate(GtkWidget *widget, int width, int height,
    int baseline)
{
	HomePickerHost *host = (HomePickerHost *)widget;
	int child_width = MAX(1, MIN(340, width - 24));
	int natural_height;
	gtk_widget_measure(host->child, GTK_ORIENTATION_VERTICAL, child_width,
	    NULL, &natural_height, NULL, NULL);
	int child_height = MAX(1, MIN(natural_height, MIN(320, height - 24)));
	graphene_point_t offset = GRAPHENE_POINT_INIT(
	    (width - child_width) / 2.0, (height - child_height) / 2.0);
	gtk_widget_allocate(host->child, child_width, child_height, baseline,
	    gsk_transform_translate(NULL, &offset));
}

static void home_picker_host_snapshot(GtkWidget *widget, GtkSnapshot *snapshot)
{
	gtk_widget_snapshot_child(widget, ((HomePickerHost *)widget)->child, snapshot);
}

static void home_picker_host_dispose(GObject *object)
{
	g_clear_pointer(&((HomePickerHost *)object)->child, gtk_widget_unparent);
	G_OBJECT_CLASS(home_picker_host_parent_class)->dispose(object);
}

static void home_picker_host_class_init(HomePickerHostClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
	widget_class->measure = home_picker_host_measure;
	widget_class->size_allocate = home_picker_host_size_allocate;
	widget_class->snapshot = home_picker_host_snapshot;
	G_OBJECT_CLASS(klass)->dispose = home_picker_host_dispose;
}

static void home_picker_host_init(HomePickerHost *host)
{
	gtk_widget_set_hexpand(GTK_WIDGET(host), TRUE);
	gtk_widget_set_vexpand(GTK_WIDGET(host), TRUE);
	gtk_widget_set_overflow(GTK_WIDGET(host), GTK_OVERFLOW_HIDDEN);
}

static GtkWidget *home_picker_host_new(GtkWidget *child)
{
	HomePickerHost *host = g_object_new(home_picker_host_get_type(), NULL);
	host->child = child;
	gtk_widget_set_parent(child, GTK_WIDGET(host));
	return GTK_WIDGET(host);
}
#endif
