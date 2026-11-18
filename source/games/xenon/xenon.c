/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Xenon's native modules (xenon.h). */
#include "mod/mod.h"
#include "xenon.h"

static const struct xe_module modules[] = {
    { "xenon_logo", XE_HOOK_LOGO, xenon_logo },
    { "xenon_opening", XE_HOOK_OPENING, xenon_opening },
    { "xenon_staff_roll", XE_HOOK_STAFF_ROLL, xenon_staff_roll },
    { "xenon_frame_close", XE_HOOK_WINDOW_CLOSE, xenon_frame_close }
};

void xenon_register(void)
{
    unsigned i;
    for (i = 0; i < sizeof modules / sizeof modules[0]; i++)
        mod_register(&modules[i]);
}
