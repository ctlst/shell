#include "home-config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>

void
home_config_set_defaults(struct home_config *cfg)
{
	memset(cfg, 0, sizeof(*cfg));
	/* V2 baked defaults (see HOME-V2.md). */
	cfg->grid_columns = 5;
	cfg->grid_rows = 7;
	cfg->grid_spacing_px = 10;
	cfg->grid_cluster_rows = 4;
	cfg->edit_inset_px = 12;
	cfg->edit_controls_gap_px = 12;

	cfg->drag_icon_arm_ms = 350;
	cfg->drag_push_dwell_ms = 280;
	cfg->drag_cell_switch_fraction = 0.24;
	cfg->drag_remove_zone_fraction = 0.16;

	cfg->page_snap_fraction = 0.5;
	cfg->page_fling_velocity = 0.35;
	cfg->page_animation_ms = 220;
	cfg->page_edge_px = 48.0;
	cfg->page_edge_dwell_ms = 200;
	cfg->page_max_pages = 8;

	/* Match ctlstdrawer-ui disc (56px) + SafeIcon (44px). */
	cfg->launcher_icon_scale = 1.0;
	cfg->launcher_icon_min_px = 56;
	cfg->launcher_icon_max_px = 56;

	cfg->calendar_month_min_columns = 3;
	cfg->calendar_month_min_rows = 2;
	cfg->calendar_month_min_height_px = 160;
	cfg->widget_compact_height_px = 96;

	cfg->rotation_transition_enabled = false; /* compositor rotation only */
	cfg->rotation_zoom_out_scale = 0.91;
	cfg->rotation_zoom_in_scale = 0.94;
	cfg->rotation_overshoot_scale = 1.015;
	cfg->rotation_cut_ms = 90;
	cfg->rotation_hold_ms = 70;
	cfg->rotation_duration_ms = 230;
}

static char *
trim(char *s)
{
	char *end;

	while (*s != '\0' && isspace((unsigned char)*s))
		s++;
	if (*s == '\0')
		return s;
	end = s + strlen(s) - 1;
	while (end > s && isspace((unsigned char)*end))
		*end-- = '\0';
	return s;
}

static bool
parse_int(const char *value, int *out)
{
	char *end = NULL;
	long v;

	v = strtol(value, &end, 10);
	if (end == value || (end != NULL && *end != '\0'))
		return false;
	*out = (int)v;
	return true;
}

static bool
parse_double(const char *value, double *out)
{
	char *end = NULL;
	double v;

	v = strtod(value, &end);
	if (end == value || (end != NULL && *end != '\0'))
		return false;
	*out = v;
	return true;
}

static bool
append_id(char (*list)[HOME_CONFIG_ID_MAX], int *count, int max,
    const char *id)
{
	size_t len;

	if (*count >= max || id == NULL || id[0] == '\0')
		return false;
	len = strlen(id);
	if (len == 0 || len >= HOME_CONFIG_ID_MAX)
		return false;
	memcpy(list[*count], id, len + 1);
	(*count)++;
	return true;
}

static void
apply_scalar(struct home_config *cfg, const char *section, const char *key,
    const char *value)
{
	int iv;
	double dv;

	if (strcmp(section, "grid") == 0) {
		if (strcmp(key, "columns") == 0 && parse_int(value, &iv) &&
		    iv >= 1 && iv <= 12)
			cfg->grid_columns = iv;
		else if (strcmp(key, "rows") == 0 && parse_int(value, &iv) &&
		    iv >= 1 && iv <= 12)
			cfg->grid_rows = iv;
		else if (strcmp(key, "spacing_px") == 0 && parse_int(value, &iv) &&
		    iv >= 0 && iv <= 48)
			cfg->grid_spacing_px = iv;
		else if (strcmp(key, "cluster_rows") == 0 && parse_int(value, &iv) &&
		    iv >= 1 && iv < cfg->grid_rows)
			cfg->grid_cluster_rows = iv;
		return;
	}
	if (strcmp(section, "edit") == 0) {
		if (strcmp(key, "inset_px") == 0 && parse_int(value, &iv) &&
		    iv >= 0 && iv <= 48)
			cfg->edit_inset_px = iv;
		else if (strcmp(key, "controls_gap_px") == 0 && parse_int(value, &iv) &&
		    iv >= 0 && iv <= 48)
			cfg->edit_controls_gap_px = iv;
		return;
	}
	if (strcmp(section, "drag") == 0) {
		if (strcmp(key, "icon_arm_ms") == 0 && parse_int(value, &iv) &&
		    iv >= 50 && iv <= 2000)
			cfg->drag_icon_arm_ms = iv;
		else if (strcmp(key, "push_dwell_ms") == 0 &&
		    parse_int(value, &iv) && iv >= 0 && iv <= 2000)
			cfg->drag_push_dwell_ms = iv;
		else if (strcmp(key, "cell_switch_fraction") == 0 &&
		    parse_double(value, &dv) && dv >= 0.0 && dv <= 0.45)
			cfg->drag_cell_switch_fraction = dv;
		else if (strcmp(key, "remove_zone_fraction") == 0 &&
		    parse_double(value, &dv) && dv >= 0.05 && dv <= 0.5)
			cfg->drag_remove_zone_fraction = dv;
		return;
	}
	if (strcmp(section, "page") == 0) {
		if (strcmp(key, "snap_fraction") == 0 && parse_double(value, &dv) &&
		    dv >= 0.2 && dv <= 0.9)
			cfg->page_snap_fraction = dv;
		else if (strcmp(key, "fling_velocity") == 0 &&
		    parse_double(value, &dv) && dv >= 0.05 && dv <= 5.0)
			cfg->page_fling_velocity = dv;
		else if (strcmp(key, "animation_ms") == 0 &&
		    parse_int(value, &iv) && iv >= 0 && iv <= 1000)
			cfg->page_animation_ms = iv;
		else if (strcmp(key, "edge_px") == 0 && parse_double(value, &dv) &&
		    dv >= 8.0 && dv <= 120.0)
			cfg->page_edge_px = dv;
		else if (strcmp(key, "edge_dwell_ms") == 0 &&
		    parse_int(value, &iv) && iv >= 50 && iv <= 2000)
			cfg->page_edge_dwell_ms = iv;
		else if (strcmp(key, "max_pages") == 0 && parse_int(value, &iv) &&
		    iv >= 1 && iv <= 16)
			cfg->page_max_pages = iv;
		return;
	}
	if (strcmp(section, "launcher") == 0) {
		if (strcmp(key, "icon_scale") == 0 && parse_double(value, &dv) &&
		    dv >= 0.2 && dv <= 1.0)
			cfg->launcher_icon_scale = dv;
		else if (strcmp(key, "icon_min_px") == 0 && parse_int(value, &iv) &&
		    iv >= 8 && iv <= 128)
			cfg->launcher_icon_min_px = iv;
		else if (strcmp(key, "icon_max_px") == 0 && parse_int(value, &iv) &&
		    iv >= 16 && iv <= 256)
			cfg->launcher_icon_max_px = iv;
		return;
	}
	if (strcmp(section, "calendar") == 0) {
		if (strcmp(key, "month_min_columns") == 0 &&
		    parse_int(value, &iv) && iv >= 1 && iv <= 12)
			cfg->calendar_month_min_columns = iv;
		else if (strcmp(key, "month_min_rows") == 0 &&
		    parse_int(value, &iv) && iv >= 1 && iv <= 12)
			cfg->calendar_month_min_rows = iv;
		else if (strcmp(key, "month_min_height_px") == 0 &&
		    parse_int(value, &iv) && iv >= 40 && iv <= 600)
			cfg->calendar_month_min_height_px = iv;
		return;
	}
	if (strcmp(section, "rotation") == 0) {
		if (strcmp(key, "transition") == 0) {
			if (strcmp(value, "spring-snap") == 0)
				cfg->rotation_transition_enabled = false; /* legacy alias */
			else if (strcmp(value, "none") == 0)
				cfg->rotation_transition_enabled = false;
		} else if (strcmp(key, "zoom_out_scale") == 0 &&
		    parse_double(value, &dv) && dv >= 0.75 && dv <= 1.0)
			cfg->rotation_zoom_out_scale = dv;
		else if (strcmp(key, "zoom_in_scale") == 0 &&
		    parse_double(value, &dv) && dv >= 0.75 && dv <= 1.0)
			cfg->rotation_zoom_in_scale = dv;
		else if (strcmp(key, "overshoot_scale") == 0 &&
		    parse_double(value, &dv) && dv >= 1.0 && dv <= 1.08)
			cfg->rotation_overshoot_scale = dv;
		else if (strcmp(key, "cut_ms") == 0 && parse_int(value, &iv) &&
		    iv >= 40 && iv <= 300)
			cfg->rotation_cut_ms = iv;
		else if (strcmp(key, "hold_ms") == 0 && parse_int(value, &iv) &&
		    iv >= 0 && iv <= 300)
			cfg->rotation_hold_ms = iv;
		else if (strcmp(key, "duration_ms") == 0 &&
		    parse_int(value, &iv) && iv >= 100 && iv <= 800)
			cfg->rotation_duration_ms = iv;
		return;
	}
	if (strcmp(section, "widgets") == 0) {
		if (strcmp(key, "compact_height_px") == 0 &&
		    parse_int(value, &iv) && iv >= 40 && iv <= 300)
			cfg->widget_compact_height_px = iv;
		/* list items handled separately as "- id" lines */
		(void)key;
		(void)value;
	}
}

bool
home_config_load_file(struct home_config *cfg, const char *path)
{
	char *contents = NULL;
	gsize length = 0;
	char section[64] = "";
	char list_target[32] = "";
	char **lines;
	bool parsed = false;

	if (path == NULL || !g_file_get_contents(path, &contents, &length, NULL))
		return false;
	parsed = true;
	lines = g_strsplit(contents, "\n", -1);
	for (int i = 0; lines[i] != NULL; i++) {
		char *line = trim(lines[i]);
		char *colon;
		char *key;
		char *value;
		size_t len;

		if (line[0] == '\0' || line[0] == '#')
			continue;
		/* list item under widgets.enabled / widgets.hidden */
		if (line[0] == '-' && list_target[0] != '\0') {
			char *id = trim(line + 1);

			if (id[0] == '"' || id[0] == '\'') {
				len = strlen(id);
				if (len >= 2 && id[len - 1] == id[0]) {
					id[len - 1] = '\0';
					id++;
				}
			}
			if (strcmp(list_target, "hidden") == 0)
				append_id(cfg->widgets_hidden,
				    &cfg->widgets_hidden_count,
				    HOME_CONFIG_HIDDEN_MAX, id);
			else if (strcmp(list_target, "enabled") == 0) {
				cfg->widgets_enabled_set = true;
				append_id(cfg->widgets_enabled,
				    &cfg->widgets_enabled_count,
				    HOME_CONFIG_ENABLED_MAX, id);
			}
			continue;
		}
		len = strlen(line);
		if (line[len - 1] == ':' && strchr(line, ' ') == NULL) {
			line[len - 1] = '\0';
			/* top-level section or widgets.hidden: */
			if (strcmp(section, "widgets") == 0 &&
			    (strcmp(line, "hidden") == 0 ||
			    strcmp(line, "enabled") == 0)) {
				g_strlcpy(list_target, line, sizeof(list_target));
				if (strcmp(line, "enabled") == 0)
					cfg->widgets_enabled_set = true;
				continue;
			}
			g_strlcpy(section, line, sizeof(section));
			list_target[0] = '\0';
			continue;
		}
		colon = strchr(line, ':');
		if (colon == NULL)
			continue;
		*colon = '\0';
		key = trim(line);
		value = trim(colon + 1);
		if (value[0] == '"' || value[0] == '\'') {
			len = strlen(value);
			if (len >= 2 && value[len - 1] == value[0]) {
				value[len - 1] = '\0';
				value++;
			}
		}
		/* empty list marker: enabled: [] */
		if (strcmp(section, "widgets") == 0 &&
		    (strcmp(key, "enabled") == 0 || strcmp(key, "hidden") == 0)) {
			if (strcmp(value, "[]") == 0) {
				if (strcmp(key, "enabled") == 0)
					cfg->widgets_enabled_set = true;
				list_target[0] = '\0';
				continue;
			}
			if (value[0] == '\0') {
				g_strlcpy(list_target, key, sizeof(list_target));
				if (strcmp(key, "enabled") == 0)
					cfg->widgets_enabled_set = true;
				continue;
			}
		}
		list_target[0] = '\0';
		if (strcmp(key, "version") == 0)
			continue;
		apply_scalar(cfg, section, key, value);
	}
	g_strfreev(lines);
	g_free(contents);
	return parsed;
}

bool
home_config_load_user(struct home_config *cfg)
{
	const char *layers = g_getenv("CTLST_HOME_CONFIG_PATHS");
	if (layers != NULL) {
		char **paths = g_strsplit(layers, G_SEARCHPATH_SEPARATOR_S, -1);
		bool ok = true;
		for (size_t i = 0; paths[i] != NULL; i++)
			if (paths[i][0] != '\0' && !home_config_load_file(cfg, paths[i]))
				ok = false;
		g_strfreev(paths);
		return ok;
	}
	char *path = g_build_filename(g_get_user_config_dir(), "ctlst",
	    "home.yaml", NULL);
	bool ok = home_config_load_file(cfg, path);

	g_free(path);
	return ok;
}
