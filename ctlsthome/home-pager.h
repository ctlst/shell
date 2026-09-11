/* Pure page-snap helpers for Home V2 pager (testable without GTK). */
#ifndef CTLST_HOME_PAGER_H
#define CTLST_HOME_PAGER_H

#include <stdbool.h>
#include <stdint.h>

/* Decide committed page from fractional drag offset and fling velocity.
 * offset_fraction: negative = dragging toward next page (content moves left).
 * Returns the page index to land on (clamped). */
int home_pager_commit_page(int current_page, int page_count,
    double offset_fraction, double velocity_x, double snap_fraction,
    double fling_velocity);

/* Progress of a settle animation from start→end over duration_ms.
 * Returns 0..1 eased; *done set when finished. */
double home_pager_settle_progress(int64_t started_us, int64_t now_us,
    int duration_ms, bool *done);

#endif
