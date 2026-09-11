#ifndef CTLST_RUNTIME_H
#define CTLST_RUNTIME_H
#include <glib.h>

/* Returned paths are owned by the caller, like g_build_filename(). */
static inline char *
ctlst_script_path(const char *name)
{
    const char *root = g_getenv("CTLST_LIBEXEC_DIR");
    return g_build_filename(root != NULL ? root : "/usr/libexec/ctlst-shell",
        name, NULL);
}

static inline char *
ctlst_config_path(const char *name)
{
    const char *root = g_getenv("CTLST_GENERATED_DIR");
    if (root != NULL)
        return g_build_filename(root, name, NULL);
    return g_build_filename(g_get_user_config_dir(), "ctlst", name, NULL);
}
#endif
