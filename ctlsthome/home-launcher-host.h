/* Cell-sized launcher host. Edit controls reserve space without changing
 * the grid's natural size; GTK/GSK transforms keep paint and picking aligned. */
#ifndef HOME_LAUNCHER_HOST_H
#define HOME_LAUNCHER_HOST_H
#include <gtk/gtk.h>

typedef struct {
	GtkWidget parent_instance;
	GtkWidget *disc, *badge;
} HomeLauncherHost;
typedef GtkWidgetClass HomeLauncherHostClass;
G_DEFINE_TYPE(HomeLauncherHost, home_launcher_host, GTK_TYPE_WIDGET)

static void home_launcher_host_measure(GtkWidget *widget, GtkOrientation orientation,
    int for_size, int *minimum, int *natural, int *min_baseline, int *nat_baseline)
{
	HomeLauncherHost *host = (HomeLauncherHost *)widget;
	/* Showing a delete control must not grow or reflow the saved grid. */
	gtk_widget_measure(host->disc, orientation, for_size,
	    minimum, natural, min_baseline, nat_baseline);
}

static void home_launcher_host_size_allocate(GtkWidget *widget, int width, int height,
    int baseline)
{
	HomeLauncherHost *host = (HomeLauncherHost *)widget;
	int disc_width, disc_height;
	gtk_widget_measure(host->disc, GTK_ORIENTATION_HORIZONTAL, -1,
	    NULL, &disc_width, NULL, NULL);
	gtk_widget_measure(host->disc, GTK_ORIENTATION_VERTICAL, disc_width,
	    NULL, &disc_height, NULL, NULL);
	disc_width = MAX(1, disc_width);
	disc_height = MAX(1, disc_height);
	float room_width = width, room_height = height, top = 0;
	if (gtk_widget_get_visible(host->badge)) {
		int badge_width, badge_height;
		gtk_widget_measure(host->badge, GTK_ORIENTATION_HORIZONTAL, -1,
		    NULL, &badge_width, NULL, NULL);
		gtk_widget_measure(host->badge, GTK_ORIENTATION_VERTICAL, badge_width,
		    NULL, &badge_height, NULL, NULL);
		graphene_point_t corner = GRAPHENE_POINT_INIT(MAX(0, width - badge_width), 0);
		gtk_widget_allocate(host->badge, badge_width, badge_height, baseline,
		    gsk_transform_translate(NULL, &corner));
		/* Choose the side that retains more artwork, using actual CSS sizes.
		 * Portrait normally reserves the top band; landscape the right band. */
		float left_width = MAX(0, width - badge_width);
		float below_height = MAX(0, height - badge_height);
		float left_scale = MIN(1.f, MIN(left_width / disc_width, (float)height / disc_height));
		float below_scale = MIN(1.f, MIN((float)width / disc_width, below_height / disc_height));
		if (left_scale > below_scale ||
		    (left_scale == below_scale && width >= height)) {
			room_width = left_width;
		} else {
			room_height = below_height;
			top = badge_height;
		}
	}
	float scale = MIN(1.f, MIN(room_width / disc_width, room_height / disc_height));
	graphene_point_t origin = GRAPHENE_POINT_INIT(
	    (room_width - disc_width * scale) / 2,
	    top + (room_height - disc_height * scale) / 2);
	GskTransform *transform = gsk_transform_translate(NULL, &origin);
	transform = gsk_transform_scale(transform, scale, scale);
	gtk_widget_allocate(host->disc, disc_width, disc_height, baseline, transform);
}

static void home_launcher_host_snapshot(GtkWidget *widget, GtkSnapshot *snapshot)
{
	HomeLauncherHost *host = (HomeLauncherHost *)widget;
	gtk_widget_snapshot_child(widget, host->disc, snapshot);
	if (gtk_widget_get_visible(host->badge))
		gtk_widget_snapshot_child(widget, host->badge, snapshot);
}

static void home_launcher_host_dispose(GObject *object)
{
	HomeLauncherHost *host = (HomeLauncherHost *)object;
	g_clear_pointer(&host->disc, gtk_widget_unparent);
	g_clear_pointer(&host->badge, gtk_widget_unparent);
	G_OBJECT_CLASS(home_launcher_host_parent_class)->dispose(object);
}

static void home_launcher_host_class_init(HomeLauncherHostClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
	widget_class->measure = home_launcher_host_measure;
	widget_class->size_allocate = home_launcher_host_size_allocate;
	widget_class->snapshot = home_launcher_host_snapshot;
	G_OBJECT_CLASS(klass)->dispose = home_launcher_host_dispose;
}

static void home_launcher_host_init(HomeLauncherHost *host)
{
	gtk_widget_set_hexpand(GTK_WIDGET(host), TRUE);
	gtk_widget_set_vexpand(GTK_WIDGET(host), TRUE);
}

static GtkWidget *home_launcher_host_new(GtkWidget *disc, GtkWidget *badge)
{
	HomeLauncherHost *host = g_object_new(home_launcher_host_get_type(), NULL);
	host->disc = disc;
	host->badge = badge;
	gtk_widget_set_parent(disc, GTK_WIDGET(host));
	gtk_widget_set_parent(badge, GTK_WIDGET(host));
	return GTK_WIDGET(host);
}
#endif
