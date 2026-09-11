#ifndef CTLST_HOME_WIDGET_RUNTIME_H
#define CTLST_HOME_WIDGET_RUNTIME_H

#include <gtk/gtk.h>

#include "home-widget-desc.h"

struct home_widget_runtime;

/* Start one declarative CTLST Widget Protocol v1 helper and return its native
 * Home canvas. The helper never owns a Wayland surface. */
struct home_widget_runtime *home_widget_runtime_new(
    const struct home_widget_desc *desc);
GtkWidget *home_widget_runtime_view(struct home_widget_runtime *runtime);
void home_widget_runtime_set_visible(struct home_widget_runtime *runtime,
    bool visible);
void home_widget_runtime_cancel_pointer(struct home_widget_runtime *runtime);
void home_widget_runtime_free(struct home_widget_runtime *runtime);

#endif
