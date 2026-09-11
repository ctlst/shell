#include "home-rotation.h"

#include <stddef.h>

bool
home_rotation_map_cell(bool landscape, int columns, int rows,
    int cluster_rows, int logical_col, int logical_row,
    int *view_col, int *view_row, bool *in_strip)
{
	int strip_rows;

	if (columns < 1 || rows < 1 || cluster_rows < 1 ||
	    cluster_rows >= rows || logical_col < 0 || logical_row < 0 ||
	    logical_col >= columns || logical_row >= rows)
		return false;
	strip_rows = rows - cluster_rows;
	if (logical_row < cluster_rows) {
		if (in_strip != NULL)
			*in_strip = false;
		/* Widgets and any cluster content keep their upright layout. */
		if (view_col != NULL)
			*view_col = logical_col;
		if (view_row != NULL)
			*view_row = logical_row;
		return true;
	}
	if (in_strip != NULL)
		*in_strip = true;
	if (!landscape) {
		if (view_col != NULL)
			*view_col = logical_col;
		if (view_row != NULL)
			*view_row = logical_row - cluster_rows;
		return true;
	}
	/* Side dock: strip becomes strip_rows wide × columns tall. */
	if (view_col != NULL)
		*view_col = logical_row - cluster_rows;
	if (view_row != NULL)
		*view_row = logical_col;
	(void)strip_rows;
	return true;
}

bool
home_rotation_unmap_cell(bool landscape, int columns, int rows,
    int cluster_rows, bool in_strip, int view_col, int view_row,
    int *logical_col, int *logical_row)
{
	int strip_rows;

	if (columns < 1 || rows < 1 || cluster_rows < 1 || cluster_rows >= rows)
		return false;
	strip_rows = rows - cluster_rows;
	if (!in_strip) {
		if (view_col < 0 || view_row < 0 || view_col >= columns ||
		    view_row >= cluster_rows)
			return false;
		if (logical_col != NULL)
			*logical_col = view_col;
		if (logical_row != NULL)
			*logical_row = view_row;
		return true;
	}
	if (!landscape) {
		if (view_col < 0 || view_row < 0 || view_col >= columns ||
		    view_row >= strip_rows)
			return false;
		if (logical_col != NULL)
			*logical_col = view_col;
		if (logical_row != NULL)
			*logical_row = view_row + cluster_rows;
		return true;
	}
	if (view_col < 0 || view_row < 0 || view_col >= strip_rows ||
	    view_row >= columns)
		return false;
	if (logical_col != NULL)
		*logical_col = view_row;
	if (logical_row != NULL)
		*logical_row = view_col + cluster_rows;
	return true;
}

bool
home_rotation_clamp_widget(int columns, int cluster_rows,
    int *column, int *row, int *column_span, int *row_span)
{
	int c;
	int r;
	int cs;
	int rs;

	if (column == NULL || row == NULL || column_span == NULL ||
	    row_span == NULL || columns < 1 || cluster_rows < 1)
		return false;
	c = *column;
	r = *row;
	cs = *column_span;
	rs = *row_span;
	if (cs < 1)
		cs = 1;
	if (rs < 1)
		rs = 1;
	if (c < 0)
		c = 0;
	if (r < 0)
		r = 0;
	if (c >= columns)
		c = columns - 1;
	if (r >= cluster_rows)
		r = cluster_rows - 1;
	if (c + cs > columns)
		cs = columns - c;
	if (r + rs > cluster_rows)
		rs = cluster_rows - r;
	if (cs < 1 || rs < 1)
		return false;
	*column = c;
	*row = r;
	*column_span = cs;
	*row_span = rs;
	return true;
}

static double
clamp_unit(double value)
{
	if (value < 0.0)
		return 0.0;
	if (value > 1.0)
		return 1.0;
	return value;
}

double
home_rotation_zoom_out_scale(double elapsed_ms, double cut_ms,
    double end_scale)
{
	double progress;
	double inverse;
	double eased;

	if (cut_ms <= 0.0)
		return end_scale;
	progress = clamp_unit(elapsed_ms / cut_ms);
	inverse = 1.0 - progress;
	eased = 1.0 - inverse * inverse * inverse;
	return 1.0 + (end_scale - 1.0) * eased;
}

double
home_rotation_spring_scale(double elapsed_ms, double duration_ms,
    double start_scale, double overshoot_scale)
{
	const double apex = 0.68;
	double progress;
	double local;
	double eased;

	if (duration_ms <= 0.0)
		return 1.0;
	progress = clamp_unit(elapsed_ms / duration_ms);
	if (progress <= apex) {
		local = progress / apex;
		eased = 1.0 - (1.0 - local) * (1.0 - local) *
		    (1.0 - local);
		return start_scale + (overshoot_scale - start_scale) * eased;
	}
	local = (progress - apex) / (1.0 - apex);
	eased = local * local * (3.0 - 2.0 * local);
	return overshoot_scale + (1.0 - overshoot_scale) * eased;
}
