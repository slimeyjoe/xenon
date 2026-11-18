/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The engine as a backend sees it (lrvne/app.h): its version, its own options, what runs the
 * game and renders a song, and the names of its trace events for test automation. The
 * program's main (games/main.c) names it. The backend the program is linked with (lrvne's
 * backends/) provides lrv_app_main. */
#include <stdlib.h>
#include <string.h>
#include "xe.h"
#include "version.h"

static struct xe_options options;

static const struct lrv_option option_table[] = {
    { "voice-strict", LRV_OPT_FLAG, 0,
      "Return waits for the voice to finish instead of cutting it (as on Windows)" },
    { "se", LRV_OPT_ARG, "N", "--render-wav: start sound effect N too" },
    { "se-at", LRV_OPT_ARG, "S", "... S seconds in (default 0)" }
};

static int option(void *ctx, const char *name, const char *arg)
{
    struct xe_options *o = ctx;
    char *end;
    if (!strcmp(name, "voice-strict")) {
        o->voice_strict = 1;
    } else if (!strcmp(name, "se")) {
        long v = strtol(arg, &end, 10);
        if (end == arg || *end || v < 0 || v > 255)
            return 1;
        o->se = (int)v;
    } else if (!strcmp(name, "se-at")) {
        o->se_at = strtod(arg, &end);
        if (end == arg || *end || o->se_at < 0)
            return 1;
    } else {
        return 1;
    }
    return 0;
}

/* enum xe_trace_kind, from LRV_TR_ENGINE */
static const char *const trace_names[] = { XE_TRACE_NAMES };

/* the wait that waits for the player: input_get_cmd's (every other wait for a key loops
 * around it; the timed ones, the music's and the voice's are only cut by input) */
static int wait_is_input(int wait_kind)
{
    return wait_kind == XE_WAIT_KEY;
}

static struct lrv_app app = {
    .version = XE_VERSION,
    .ctx = &options,
    .options = option_table,
    .noptions = (int)(sizeof option_table / sizeof option_table[0]),
    .option = option,
    .help = "keys: cursor keys or keypad 8/2 move in menus, Return/Space choose and turn the page,\n"
            "  Esc/keypad 0 cancel, F1 (or Help) is the PC-98's HELP key, Ctrl held skips,\n"
            "  Caps Lock or the kana lock shows Japanese text a page at once; the mouse\n"
            "  steps through menus by its motion, left button chooses, right cancels",
    .run = xe_run,
    .render_music = xe_render_music,
    .trace_names = trace_names,
    .ntrace_names = (int)(sizeof trace_names / sizeof trace_names[0]),
    .wait_is_input = wait_is_input
};

int xe_main(const char *program, int argc, char **argv)
{
    app.name = program;
    return lrv_app_main(&app, argc, argv);
}
