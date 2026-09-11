/* Pure helpers for Home V2 cluster/strip landscape mapping (testable). */
#ifndef CTLST_HOME_ROTATION_H
#define CTLST_HOME_ROTATION_H

#include <stdbool.h>

/* Map a portrait-logical cell into the view grid for the active orientation.
 * Cluster cells (row < cluster_rows) remain upright and keep (col, row).
 * Strip cells (row >= cluster_rows): landscape docks to the side as
 * strip_rows × columns transposed to strip_rows columns × columns rows:
 *   local_row = row - cluster_rows
 *   (col, row) -> (local_row, col)
 * Portrait: identity. Returns false if the logical cell is out of range. */
bool home_rotation_map_cell(bool landscape, int columns, int rows,
    int cluster_rows, int logical_col, int logical_row,
    int *view_col, int *view_row, bool *in_strip);

/* Inverse: view coordinates back to portrait-logical (for drag while landscape). */
bool home_rotation_unmap_cell(bool landscape, int columns, int rows,
    int cluster_rows, bool in_strip, int view_col, int view_row,
    int *logical_col, int *logical_row);

/* Clamp a widget rect so it stays entirely inside the cluster region. */
bool home_rotation_clamp_widget(int columns, int cluster_rows,
    int *column, int *row, int *column_span, int *row_span);

/* Frame-clock scale curves for the no-crossfade spring-snap transition. */
double home_rotation_zoom_out_scale(double elapsed_ms, double cut_ms,
    double end_scale);
double home_rotation_spring_scale(double elapsed_ms, double duration_ms,
    double start_scale, double overshoot_scale);

#endif
