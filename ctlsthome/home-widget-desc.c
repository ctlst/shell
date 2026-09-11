#include "home-widget-desc.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

static const char *const k_builtin_ids[] = {
	"clock", "calendar", "weather", "glance", NULL
};

bool
home_widget_desc_is_builtin_id(const char *id)
{
	if (id == NULL || id[0] == '\0')
		return false;
	for (int i = 0; k_builtin_ids[i] != NULL; i++) {
		if (strcmp(id, k_builtin_ids[i]) == 0)
			return true;
	}
	return false;
}

static void
trim(char *s)
{
	char *end;
	char *start = s;

	while (*start != '\0' && isspace((unsigned char)*start))
		start++;
	if (start != s)
		memmove(s, start, strlen(start) + 1);
	end = s + strlen(s);
	while (end > s && isspace((unsigned char)end[-1]))
		end--;
	*end = '\0';
}

static bool
valid_id(const char *id)
{
	if (id == NULL || id[0] == '\0')
		return false;
	if (!(islower((unsigned char)id[0]) || isdigit((unsigned char)id[0])))
		return false;
	for (const char *p = id; *p; p++) {
		if (!(islower((unsigned char)*p) || isdigit((unsigned char)*p) ||
		    *p == '_' || *p == '-'))
			return false;
	}
	return true;
}

static bool
copy_bounded(char *destination, size_t capacity, const char *source)
{
	size_t length;

	if (destination == NULL || capacity == 0 || source == NULL)
		return false;
	length = strlen(source);
	if (length >= capacity)
		return false;
	memcpy(destination, source, length + 1);
	return true;
}

static bool
parse_int(const char *s, int *out)
{
	char *end = NULL;
	long v;

	if (s == NULL || out == NULL)
		return false;
	errno = 0;
	v = strtol(s, &end, 10);
	if (errno == ERANGE || end == s || (end != NULL && *end != '\0') ||
	    v < INT_MIN || v > INT_MAX)
		return false;
	*out = (int)v;
	return true;
}

static bool
unquote(char *s)
{
	size_t n;

	trim(s);
	n = strlen(s);
	if (n >= 2 && ((s[0] == '"' && s[n - 1] == '"') ||
	    (s[0] == '\'' && s[n - 1] == '\''))) {
		s[n - 1] = '\0';
		memmove(s, s + 1, n - 1);
	}
	return true;
}

bool
home_widget_desc_load_file(struct home_widget_desc *out, const char *path)
{
	FILE *fp;
	char line[512];
	bool in_args = false;
	struct home_widget_desc desc;

	if (out == NULL || path == NULL)
		return false;
	memset(&desc, 0, sizeof(desc));
	desc.min_columns = 1;
	desc.min_rows = 1;
	desc.default_columns = 0;
	desc.default_rows = 0;
	fp = fopen(path, "r");
	if (fp == NULL)
		return false;
	while (fgets(line, sizeof(line), fp) != NULL) {
		char *colon;
		char key[128];
		char value[384];

		trim(line);
		if (line[0] == '\0' || line[0] == '#')
			continue;
		if (strcmp(line, "args:") == 0 || strcmp(line, "args: []") == 0) {
			in_args = true;
			continue;
		}
		if (in_args && line[0] == '-' ) {
			char *item = line + 1;

			trim(item);
			unquote(item);
			if (item[0] != '\0' &&
			    desc.args_count < HOME_WIDGET_DESC_ARGS_MAX) {
				if (!copy_bounded(desc.args[desc.args_count],
				    HOME_WIDGET_DESC_ARG_MAX, item)) {
					fclose(fp);
					return false;
				}
				desc.args_count++;
			}
			continue;
		}
		in_args = false;
		colon = strchr(line, ':');
		if (colon == NULL)
			continue;
		*colon = '\0';
		if (!copy_bounded(key, sizeof(key), line) ||
		    !copy_bounded(value, sizeof(value), colon + 1)) {
			fclose(fp);
			return false;
		}
		trim(key);
		trim(value);
		unquote(value);
		if (strcmp(key, "id") == 0) {
			if (!copy_bounded(desc.id, sizeof(desc.id), value)) {
				fclose(fp);
				return false;
			}
		} else if (strcmp(key, "name") == 0) {
			if (!copy_bounded(desc.name, sizeof(desc.name), value)) {
				fclose(fp);
				return false;
			}
		} else if (strcmp(key, "kind") == 0) {
			if (strcmp(value, "builtin") == 0)
				desc.kind = HOME_WIDGET_KIND_BUILTIN;
			else if (strcmp(value, "exec") == 0)
				desc.kind = HOME_WIDGET_KIND_EXEC;
			else {
				fclose(fp);
				return false;
			}
		} else if (strcmp(key, "min_columns") == 0) {
			if (!parse_int(value, &desc.min_columns)) {
				fclose(fp);
				return false;
			}
		} else if (strcmp(key, "min_rows") == 0) {
			if (!parse_int(value, &desc.min_rows)) {
				fclose(fp);
				return false;
			}
		} else if (strcmp(key, "default_columns") == 0) {
			if (!parse_int(value, &desc.default_columns)) {
				fclose(fp);
				return false;
			}
		} else if (strcmp(key, "default_rows") == 0) {
			if (!parse_int(value, &desc.default_rows)) {
				fclose(fp);
				return false;
			}
		} else if (strcmp(key, "exec") == 0) {
			if (!copy_bounded(desc.exec, sizeof(desc.exec), value)) {
				fclose(fp);
				return false;
			}
		} else if (strcmp(key, "protocol") == 0) {
			if (!copy_bounded(desc.protocol, sizeof(desc.protocol), value)) {
				fclose(fp);
				return false;
			}
		} else if (strcmp(key, "interaction") == 0) {
			if (!copy_bounded(desc.interaction, sizeof(desc.interaction), value)) {
				fclose(fp);
				return false;
			}
		} else if (strcmp(key, "mime") == 0) {
			if (!copy_bounded(desc.mime, sizeof(desc.mime), value)) {
				fclose(fp);
				return false;
			}
		} else if (strcmp(key, "desktop") == 0) {
			if (!copy_bounded(desc.desktop, sizeof(desc.desktop), value)) {
				fclose(fp);
				return false;
			}
		}
	}
	fclose(fp);
	if (!valid_id(desc.id) || desc.name[0] == '\0')
		return false;
	if (home_widget_desc_is_builtin_id(desc.id) &&
	    desc.kind != HOME_WIDGET_KIND_BUILTIN)
		return false;
	if (desc.kind == HOME_WIDGET_KIND_EXEC && desc.exec[0] == '\0')
		return false;
	if (desc.kind == HOME_WIDGET_KIND_EXEC &&
	    strcmp(desc.protocol, "ctlst-widget-1") != 0)
		return false;
	if (desc.kind == HOME_WIDGET_KIND_EXEC && desc.interaction[0] == '\0')
		copy_bounded(desc.interaction, sizeof(desc.interaction), "pointer");
	if (desc.kind == HOME_WIDGET_KIND_EXEC &&
	    strcmp(desc.interaction, "pointer") != 0 &&
	    strcmp(desc.interaction, "launch") != 0 &&
	    strcmp(desc.interaction, "none") != 0)
		return false;
	if (desc.min_columns < 1 || desc.min_rows < 1)
		return false;
	if (desc.default_columns < 1)
		desc.default_columns = desc.min_columns;
	if (desc.default_rows < 1)
		desc.default_rows = desc.min_rows;
	desc.valid = true;
	*out = desc;
	return true;
}

static bool
id_exists(const struct home_widget_desc *descs, int count, const char *id)
{
	for (int i = 0; i < count; i++) {
		if (strcmp(descs[i].id, id) == 0)
			return true;
	}
	return false;
}

static int
compare_names(const void *left, const void *right)
{
	const char *const *left_name = left;
	const char *const *right_name = right;

	return strcmp(*left_name, *right_name);
}

static bool
descriptor_filename(const char *name)
{
	size_t length;

	if (name == NULL || name[0] == '.')
		return false;
	length = strlen(name);
	return (length >= 5 && strcmp(name + length - 5, ".yaml") == 0) ||
	    (length >= 4 && strcmp(name + length - 4, ".yml") == 0);
}

int
home_widget_desc_scan_dir(struct home_widget_desc *descs, int capacity,
    int already, const char *dir_path)
{
	DIR *dir;
	struct dirent *ent;
	char **names = NULL;
	size_t names_count = 0;
	size_t names_capacity = 0;
	int count = already;

	if (descs == NULL || capacity <= 0 || dir_path == NULL)
		return already;
	dir = opendir(dir_path);
	if (dir == NULL)
		return already;
	while ((ent = readdir(dir)) != NULL) {
		char *name;
		size_t length;

		if (!descriptor_filename(ent->d_name))
			continue;
		if (names_count == names_capacity) {
			size_t next_capacity = names_capacity == 0 ? 16 :
			    names_capacity * 2;
			char **next = realloc(names, next_capacity * sizeof(*next));

			if (next == NULL)
				break;
			names = next;
			names_capacity = next_capacity;
		}
		length = strlen(ent->d_name) + 1;
		name = malloc(length);
		if (name == NULL)
			break;
		memcpy(name, ent->d_name, length);
		names[names_count++] = name;
	}
	closedir(dir);
	qsort(names, names_count, sizeof(*names), compare_names);
	for (size_t name_index = 0;
	    name_index < names_count && count < capacity; name_index++) {
		char path[512];
		struct home_widget_desc desc;
		struct stat path_stat;
		const char *name = names[name_index];

		if (snprintf(path, sizeof(path), "%s/%s", dir_path,
		    name) >= (int)sizeof(path))
			continue;
		if (stat(path, &path_stat) != 0 || !S_ISREG(path_stat.st_mode))
			continue;
		if (!home_widget_desc_load_file(&desc, path))
			continue;
		if (id_exists(descs, count, desc.id)) {
			fprintf(stderr,
			    "ctlsthome: ignoring duplicate widget id '%s' from %s\n",
			    desc.id, path);
			continue;
		}
		descs[count++] = desc;
	}
	for (size_t i = 0; i < names_count; i++)
		free(names[i]);
	free(names);
	return count;
}
