/* Widget descriptor intake for Home V2 (YAML subset, no GTK). */
#ifndef CTLST_HOME_WIDGET_DESC_H
#define CTLST_HOME_WIDGET_DESC_H

#include <stdbool.h>

#define HOME_WIDGET_DESC_ID_MAX 64
#define HOME_WIDGET_DESC_NAME_MAX 96
#define HOME_WIDGET_DESC_EXEC_MAX 256
#define HOME_WIDGET_DESC_ARGS_MAX 8
#define HOME_WIDGET_DESC_ARG_MAX 128
#define HOME_WIDGET_DESC_MAX 64

enum home_widget_kind {
	HOME_WIDGET_KIND_BUILTIN = 0,
	HOME_WIDGET_KIND_EXEC = 1,
};

struct home_widget_desc {
	char id[HOME_WIDGET_DESC_ID_MAX];
	char name[HOME_WIDGET_DESC_NAME_MAX];
	enum home_widget_kind kind;
	int min_columns;
	int min_rows;
	int default_columns;
	int default_rows;
	char exec[HOME_WIDGET_DESC_EXEC_MAX];
	char args[HOME_WIDGET_DESC_ARGS_MAX][HOME_WIDGET_DESC_ARG_MAX];
	int args_count;
	char protocol[32];
	char interaction[16];
	char mime[HOME_WIDGET_DESC_EXEC_MAX];
	char desktop[HOME_WIDGET_DESC_EXEC_MAX];
	bool valid;
};

/* Reserved builtin ids that external descriptors must not override. */
bool home_widget_desc_is_builtin_id(const char *id);

/* Parse one descriptor file into out. Returns false if invalid/skipped. */
bool home_widget_desc_load_file(struct home_widget_desc *out, const char *path);

/* Scan a directory for sorted *.yaml / *.yml; append unique ids into descs[].
 * Returns the total descriptor count. First search path wins on collision. */
int home_widget_desc_scan_dir(struct home_widget_desc *descs, int capacity,
    int already, const char *dir_path);

#endif
