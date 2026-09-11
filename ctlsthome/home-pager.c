#include "home-pager.h"

#include <math.h>
#include <stddef.h>

int
home_pager_commit_page(int current_page, int page_count,
    double offset_fraction, double velocity_x, double snap_fraction,
    double fling_velocity)
{
	int target = current_page;

	if (page_count < 1)
		return 0;
	if (current_page < 0)
		current_page = 0;
	if (current_page >= page_count)
		current_page = page_count - 1;
	target = current_page;

	/* Fling wins when strong enough. */
	if (velocity_x <= -fling_velocity && current_page + 1 < page_count)
		target = current_page + 1;
	else if (velocity_x >= fling_velocity && current_page > 0)
		target = current_page - 1;
	else if (offset_fraction <= -snap_fraction &&
	    current_page + 1 < page_count)
		target = current_page + 1;
	else if (offset_fraction >= snap_fraction && current_page > 0)
		target = current_page - 1;

	if (target < 0)
		target = 0;
	if (target >= page_count)
		target = page_count - 1;
	return target;
}

double
home_pager_settle_progress(int64_t started_us, int64_t now_us,
    int duration_ms, bool *done)
{
	double t;
	double eased;

	if (done != NULL)
		*done = false;
	if (duration_ms <= 0) {
		if (done != NULL)
			*done = true;
		return 1.0;
	}
	t = (double)(now_us - started_us) / ((double)duration_ms * 1000.0);
	if (t >= 1.0) {
		if (done != NULL)
			*done = true;
		return 1.0;
	}
	if (t <= 0.0)
		return 0.0;
	/* Ease-out cubic. */
	eased = 1.0 - pow(1.0 - t, 3.0);
	return eased;
}
