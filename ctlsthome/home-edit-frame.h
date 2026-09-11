/* Live edit preview: unchanged logical page geometry, transformed GTK input. */
#ifndef HOME_EDIT_FRAME_H
#define HOME_EDIT_FRAME_H

#include <gtk/gtk.h>
#include <stdbool.h>

typedef struct {
	GtkWidget parent_instance;
	GtkWidget *scene;
	GtkWidget *controls;
	bool editing;
	int inset;
	int gap;
	double scale;
} HomeEditFrame;
typedef GtkWidgetClass HomeEditFrameClass;
G_DEFINE_TYPE(HomeEditFrame, home_edit_frame, GTK_TYPE_WIDGET)

static void
home_edit_frame_measure(GtkWidget *widget, GtkOrientation orientation,
    int for_size, int *minimum, int *natural, int *minimum_baseline,
    int *natural_baseline)
{
	(void)widget;
	(void)orientation;
	(void)for_size;
	*minimum = *natural = 0;
	*minimum_baseline = *natural_baseline = -1;
}

static void
home_edit_frame_arrange_controls(HomeEditFrame *frame, int width, int height)
{
	if (!GTK_IS_BOX(frame->controls))
		return;
	GtkOrientation current = gtk_orientable_get_orientation(GTK_ORIENTABLE(frame->controls));
	int sum = 0, widest = 0, count = 0, natural = 0;
	for (GtkWidget *child = gtk_widget_get_first_child(frame->controls); child;
	    child = gtk_widget_get_next_sibling(child)) {
		if (!gtk_widget_get_visible(child))
			continue;
		int child_width = 0;
		gtk_widget_measure(child, GTK_ORIENTATION_HORIZONTAL, -1,
		    NULL, &child_width, NULL, NULL);
		sum += child_width;
		widest = MAX(widest, child_width);
		count++;
	}
	sum += MAX(0, count - 1) * gtk_box_get_spacing(GTK_BOX(frame->controls));
	gtk_widget_measure(frame->controls, GTK_ORIENTATION_HORIZONTAL, -1,
	    NULL, &natural, NULL, NULL);
	/* Preserve the parent's CSS padding/border rather than hard-coding a
	 * width breakpoint that stops fitting when a user changes their theme. */
	int chrome = MAX(0, natural - (current == GTK_ORIENTATION_HORIZONTAL ? sum : widest));
	GtkOrientation wanted = width > height && sum + chrome <= width - 2 * frame->inset
	    ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL;
	if (current != wanted)
		gtk_orientable_set_orientation(GTK_ORIENTABLE(frame->controls), wanted);
}

static void
home_edit_frame_size_allocate(GtkWidget *widget, int width, int height,
    int baseline)
{
	HomeEditFrame *frame = (HomeEditFrame *)widget;
	int controls_width = 0, controls_height = 0;
	graphene_point_t offset = GRAPHENE_POINT_INIT(0, 0);
	GskTransform *transform = NULL;

	frame->scale = 1.0;
	if (frame->editing && width > 0 && height > 0) {
		home_edit_frame_arrange_controls(frame, width, height);
		int available_width = MAX(1, width - 2 * frame->inset);
		gtk_widget_measure(frame->controls, GTK_ORIENTATION_HORIZONTAL,
		    -1, NULL, &controls_width, NULL, NULL);
		controls_width = MIN(controls_width, available_width);
		gtk_widget_measure(frame->controls, GTK_ORIENTATION_VERTICAL,
		    controls_width, NULL, &controls_height, NULL, NULL);
		int available_height = MAX(1, height - controls_height -
		    2 * frame->inset - frame->gap);
		frame->scale = MIN(1.0, MIN((double)available_width / width,
		    (double)available_height / height));
		offset.x = (width - width * frame->scale) / 2.0;
		offset.y = frame->inset +
		    (available_height - height * frame->scale) / 2.0;
		transform = gsk_transform_translate(NULL, &offset);
		transform = gsk_transform_scale(transform, frame->scale, frame->scale);
	}
	/* GTK owns the transform: picking and compute_point use the same matrix
	 * as rendering. Never resize/reflow the page just to enter edit mode. */
	gtk_widget_allocate(frame->scene, width, height, baseline, transform);
	if (frame->editing) {
		offset = GRAPHENE_POINT_INIT((width - controls_width) / 2.0,
		    height - frame->inset - controls_height);
		gtk_widget_allocate(frame->controls, controls_width, controls_height,
		    -1, gsk_transform_translate(NULL, &offset));
	}
}

static void
home_edit_frame_snapshot(GtkWidget *widget, GtkSnapshot *snapshot)
{
	HomeEditFrame *frame = (HomeEditFrame *)widget;
	gtk_widget_snapshot_child(widget, frame->scene, snapshot);
	if (frame->editing)
		gtk_widget_snapshot_child(widget, frame->controls, snapshot);
}

static void
home_edit_frame_dispose(GObject *object)
{
	HomeEditFrame *frame = (HomeEditFrame *)object;
	g_clear_pointer(&frame->scene, gtk_widget_unparent);
	g_clear_pointer(&frame->controls, gtk_widget_unparent);
	G_OBJECT_CLASS(home_edit_frame_parent_class)->dispose(object);
}

static void
home_edit_frame_class_init(HomeEditFrameClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
	widget_class->measure = home_edit_frame_measure;
	widget_class->size_allocate = home_edit_frame_size_allocate;
	widget_class->snapshot = home_edit_frame_snapshot;
	G_OBJECT_CLASS(klass)->dispose = home_edit_frame_dispose;
}

static void
home_edit_frame_init(HomeEditFrame *frame)
{
	frame->scale = 1.0;
	gtk_widget_set_overflow(GTK_WIDGET(frame), GTK_OVERFLOW_VISIBLE);
}

static HomeEditFrame *
home_edit_frame_new(GtkWidget *scene, GtkWidget *controls)
{
	HomeEditFrame *frame = g_object_new(home_edit_frame_get_type(), NULL);
	frame->scene = scene;
	frame->controls = controls;
	gtk_widget_set_parent(scene, GTK_WIDGET(frame));
	gtk_widget_set_parent(controls, GTK_WIDGET(frame));
	gtk_widget_set_visible(controls, false);
	return frame;
}

static void
home_edit_frame_configure(HomeEditFrame *frame, bool editing, int inset, int gap)
{
	frame->editing = editing;
	frame->inset = inset;
	frame->gap = gap;
	gtk_widget_set_overflow(GTK_WIDGET(frame), editing ?
	    GTK_OVERFLOW_HIDDEN : GTK_OVERFLOW_VISIBLE);
	gtk_widget_set_visible(frame->controls, editing);
	gtk_widget_queue_allocate(GTK_WIDGET(frame));
}

#endif
