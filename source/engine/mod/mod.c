/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The module registry (mod.h). */
#include <string.h>
#include "xe.h"
#include "mod/mod.h"

static const struct xe_module *g_all[XE_MODULES_MAX];
static int g_nall;
static const struct xe_module *g_hook[XE_HOOK_COUNT];

static const char *const hook_names[XE_HOOK_COUNT] = {
    "the logo", "OPENING", "STAFF_ROLL", "WINDOW 1"
};

int mod_register(const struct xe_module *m)
{
    if (g_nall == XE_MODULES_MAX || m->hook < 0 || m->hook >= XE_HOOK_COUNT)
        return -1;
    g_all[g_nall++] = m;
    return 0;
}

int mod_init(const char *enabled)
{
    const char *p = enabled;
    int i, rc = 0;
    memset(g_hook, 0, sizeof g_hook);
    while (*p) {
        char name[48];
        size_t n;
        while (*p == ' ' || *p == ',')
            p++;
        n = strcspn(p, ",");
        while (n && p[n - 1] == ' ')
            n--;
        if (!n)
            break;
        if (n >= sizeof name)
            n = sizeof name - 1;
        memcpy(name, p, n);
        name[n] = 0;
        p += strcspn(p, ",");
        for (i = 0; i < g_nall && !lrv_name_eq(g_all[i]->name, name); i++)
            ;
        if (i == g_nall) {
            lrv_log(xe.hal, LRV_LOG_ERROR, "profile: no module %s in this program", name);
            rc = -1;
        } else if (g_hook[g_all[i]->hook]) {
            lrv_log(xe.hal, LRV_LOG_ERROR, "profile: modules %s and %s both take %s",
                    g_hook[g_all[i]->hook]->name, name, hook_names[g_all[i]->hook]);
            rc = -1;
        } else {
            g_hook[g_all[i]->hook] = g_all[i];
        }
    }
    return rc;
}

int mod_run(int hook, int arg)
{
    const struct xe_module *m;
    int seq = hook != XE_HOOK_WINDOW_CLOSE;
    if (hook < 0 || hook >= XE_HOOK_COUNT)
        return 0;
    m = g_hook[hook];
    if (!m) {
        lrv_log(xe.hal, LRV_LOG_WARN, "%s: the game has no module for it", hook_names[hook]);
        return 0;
    }
    if (seq)
        lrv_sys_trace(&xe.sys, XE_TR_SEQ, 0, 0, hook, 0, arg, 0);
    m->run(arg);
    if (seq)
        lrv_sys_trace(&xe.sys, XE_TR_SEQ, 0, 0, hook, 1, arg, 0);
    return 1;
}
