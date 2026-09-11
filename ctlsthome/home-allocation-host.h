/* Allocation-owned Home reflow. GTK4 has no notify::width/height properties. */
#ifndef HOME_ALLOCATION_HOST_H
#define HOME_ALLOCATION_HOST_H
#include <gtk/gtk.h>

typedef void (*HomeAllocationCallback)(gpointer data, int width, int height);
typedef struct {
	GtkWidget parent_instance;
	GtkWidget *child;
	HomeAllocationCallback before, after;
	gpointer data;
} HomeAllocationHost;
typedef GtkWidgetClass HomeAllocationHostClass;
G_DEFINE_TYPE(HomeAllocationHost, home_allocation_host, GTK_TYPE_WIDGET)

static void
home_allocation_host_measure(GtkWidget *widget, GtkOrientation orientation,
    int for_size, int *minimum, int *natural, int *minimum_baseline,
    int *natural_baseline)
{
	(void)widget;
	(void)orientation;
	(void)for_size;
	/* The compositor owns the full-screen viewport, not its previous content. */
	*minimum = *natural = 0;
	*minimum_baseline = *natural_baseline = -1;
}

static void
home_allocation_host_size_allocate(GtkWidget *widget, int width, int height,
    int baseline)
{
	HomeAllocationHost *host = (HomeAllocationHost *)widget;
	if (host->before != NULL)
		host->before(host->data, width, height);
	gtk_widget_allocate(host->child, width, height, baseline, NULL);
	if (host->after != NULL)
		host->after(host->data, width, height);
}

static void
home_allocation_host_snapshot(GtkWidget *widget, GtkSnapshot *snapshot)
{
	HomeAllocationHost *host = (HomeAllocationHost *)widget;
	gtk_widget_snapshot_child(widget, host->child, snapshot);
}

static void
home_allocation_host_dispose(GObject *object)
{
	HomeAllocationHost *host = (HomeAllocationHost *)object;
	g_clear_pointer(&host->child, gtk_widget_unparent);
	G_OBJECT_CLASS(home_allocation_host_parent_class)->dispose(object);
}

static void
home_allocation_host_class_init(HomeAllocationHostClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
	widget_class->measure = home_allocation_host_measure;
	widget_class->size_allocate = home_allocation_host_size_allocate;
	widget_class->snapshot = home_allocation_host_snapshot;
	G_OBJECT_CLASS(klass)->dispose = home_allocation_host_dispose;
}

static void
home_allocation_host_init(HomeAllocationHost *host)
{
	(void)host;
}

static GtkWidget *
home_allocation_host_new(GtkWidget *child, HomeAllocationCallback before,
    HomeAllocationCallback after, gpointer data)
{
	HomeAllocationHost *host = g_object_new(home_allocation_host_get_type(), NULL);
	host->child = child;
	host->before = before;
	host->after = after;
	host->data = data;
	gtk_widget_set_parent(child, GTK_WIDGET(host));
	return GTK_WIDGET(host);
}
#endif
