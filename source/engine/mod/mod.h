/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Native modules: what a game's AGS.EXE does in code of its own where the engine has a hook.
 *
 * The engine has none of its own. A module belongs to a game (source/games/<game>/), is
 * registered with mod_register before the engine runs, and is enabled by the game's profile
 * (modules=, docs/profile.md). The hooks are the places where a game's executable runs a
 * routine of its own instead of an interpreted script:
 *
 *   XE_HOOK_LOGO          the sequence before the title screen (game_main 0ABA)
 *   XE_HOOK_OPENING       the script opcode OPENING n (29h), arg = n
 *   XE_HOOK_STAFF_ROLL    the script opcode STAFF_ROLL (47h)
 *   XE_HOOK_WINDOW_CLOSE  the script opcode WINDOW 1 (11h): the effect that closes the
 *                         picture frame
 *
 * The engine reports a sequence (the first three) as XE_TR_SEQ events around the module, with
 * a0 = the hook and a2 = arg. A hook without a module is logged and does nothing.
 *
 * A module's code is engine code: it draws, waits and plays through the engine's own
 * functions (gfx/gfx.h, gfx/adt.h, snd/snd.h, input.c's waits, lrv_screen on xe.screen). */
#ifndef XE_MOD_MOD_H
#define XE_MOD_MOD_H

enum xe_hook {
    XE_HOOK_LOGO = 0, XE_HOOK_OPENING, XE_HOOK_STAFF_ROLL, XE_HOOK_WINDOW_CLOSE, XE_HOOK_COUNT
};

struct xe_module {
    const char *name;               /* as the profile's modules= names it */
    int hook;                       /* enum xe_hook */
    void (*run)(int arg);
};

#define XE_MODULES_MAX 16

/* Adds a module (the structure must stay valid). Returns 0, or -1 when the table is full. */
int  mod_register(const struct xe_module *m);
/* The profile's comma-separated module names: each must be a registered module, and no two
 * enabled modules may take the same hook. Returns 0, or -1 (logged). */
int  mod_init(const char *enabled);
/* Runs the module of a hook. Returns 1 when one ran, 0 when the game has none for it. */
int  mod_run(int hook, int arg);

#endif
