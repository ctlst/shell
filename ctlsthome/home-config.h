/* CTLST Home config: baked defaults + ~/.config/ctlst/home.yaml overrides.
 * Parses a constrained YAML subset (sections, key: value scalars). */
#ifndef CTLST_HOME_CONFIG_H
#define CTLST_HOME_CONFIG_H

#include <stdbool.h>

#define HOME_CONFIG_HIDDEN_MAX 32
#define HOME_CONFIG_ENABLED_MAX 32
#define HOME_CONFIG_ID_MAX 64

struct home_config {
	int grid_columns;
	int grid_rows;
	int grid_spacing_px;
	int grid_cluster_rows;
	int edit_inset_px;
	int edit_controls_gap_px;

	int drag_icon_arm_ms;
	int drag_push_dwell_ms;
	double drag_cell_switch_fraction;
	double drag_remove_zone_fraction;

	double page_snap_fraction;
	double page_fling_velocity;
	int page_animation_ms;
	double page_edge_px;
	int page_edge_dwell_ms;
	int page_max_pages;

	double launcher_icon_scale;
	int launcher_icon_min_px;
	int launcher_icon_max_px;

	int calendar_month_min_columns;
	int calendar_month_min_rows;
	int calendar_month_min_height_px;
	int widget_compact_height_px;

	bool rotation_transition_enabled;
	double rotation_zoom_out_scale;
	double rotation_zoom_in_scale;
	double rotation_overshoot_scale;
	int rotation_cut_ms;
	int rotation_hold_ms;
	int rotation_duration_ms;

	char widgets_hidden[HOME_CONFIG_HIDDEN_MAX][HOME_CONFIG_ID_MAX];
	int widgets_hidden_count;
	char widgets_enabled[HOME_CONFIG_ENABLED_MAX][HOME_CONFIG_ID_MAX];
	int widgets_enabled_count;
	bool widgets_enabled_set;
};

void home_config_set_defaults(struct home_config *cfg);
/* Load path or leave defaults if missing/unreadable. Returns true if a file
 * was parsed (even partially). */
bool home_config_load_file(struct home_config *cfg, const char *path);
/* ~/.config/ctlst/home.yaml */
bool home_config_load_user(struct home_config *cfg);

#endif
