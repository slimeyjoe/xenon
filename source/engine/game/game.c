/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Game core: engine init/shutdown (0E1A), the title/omake/run loop game_main (0ABA, engine.md
 * §3), the system menu (088D:223D) and the script-opcode host (088D handlers -> subsystems,
 * opcodes.md). */
#include <string.h>
#include "xe.h"
#include "game/game.h"
#include "game/save.h"
#include "gfx/gfx.h"
#include "gfx/adt.h"
#include "gfx/blink.h"
#include "mod/mod.h"
#include "snd/snd.h"
#include "text/text.h"
#include "vm/cc.h"
#include "voice/voice.h"

/* ---- script loading ----------------------------------------------------------------------- */

/* the statement being executed, for every trace event (~0u: no script in memory) */
static void script_at(void *ctx, uint32_t pos)
{
    (void)ctx;
    xe.sys.script_pos = pos;
}

static void unload_script(void)
{
    voice_stop();                       /* Windows stops the clip at every script load */
    voice_script_unload();
    if (xe.script_buf)
        lrv_pack_free(&xe.pack, xe.script_buf, xe.script_size);
    xe.script_buf = 0;
    xe.script_size = 0;
    xe.vm.buf = 0;                      /* no statement position until the next load */
    xe.vm.size = 0;
    script_at(0, ~0u);
}

int game_load_script(const char *name)
{
    const struct lrv_pack_entry *e;
    char base[CC_NAME_LEN];
    size_t i;
    /* the name may carry ".cc" (as a save stores it) */
    for (i = 0; i < sizeof base - 1 && name[i] && name[i] != '.'; i++)
        base[i] = name[i];
    base[i] = 0;
    unload_script();
    e = lrv_pack_find(&xe.pack, base, XE_PT_SCRIPT);
    if (!e) {
        lrv_log(xe.hal, LRV_LOG_ERROR, "script %s.cc not found", base);
        return 0;
    }
    xe.script_buf = lrv_pack_load(&xe.pack, e, &xe.script_size);
    if (!xe.script_buf)
        return 0;
    cc_vm_set_script(&xe.vm, base, xe.script_buf, xe.script_size);
    voice_script(base);
    xe.vm.pos = 0;
    script_at(0, 0);
    lrv_sys_trace(&xe.sys, XE_TR_SCRIPT, base, 0, -1, 0, 0, 0);
    return 1;
}

/* ---- script host: opcodes -> subsystems (opcodes.md) --------------------------------------- */

static void host_message(void *ctx, const uint8_t *text)
{
    (void)ctx;
    /* the PC-98's KANA lock: the text of a page appears at once */
    text_print(text, !(lrv_sys_mods(&xe.sys) & (LRV_MOD_CAPS | LRV_MOD_KANA)));
}

static int host_menu(void *ctx, const uint8_t *const *items, int count, int cursor)
{
    (void)ctx;
    return select_menu(items, count, cursor);
}

/* cc_system_menu 088D:223D: Cancel / Return to start / Quit (engine.md §8) */
static int system_menu_run(void);
static int host_system_menu(void *ctx)
{
    int r;
    (void)ctx;
    voice_stop();
    xe.in_sysmenu = 1;
    r = system_menu_run();
    xe.in_sysmenu = 0;
    lrv_sys_trace(&xe.sys, XE_TR_SYSMENU, 0, 0, r, 0, 0, 0);
    return r;
}

static int system_menu_run(void)
{
    const uint8_t *items[4];
    int sel;
    items[0] = lang_str(LS_SYSMENU_CANCEL);
    items[1] = lang_str(LS_SYSMENU_RESTART);
    items[2] = lang_str(LS_SYSMENU_QUIT);
    items[3] = 0;
    sel = select_menu(items, 3, 0);
    if (sel == 1)
        return 1;
    if (sel == 2) {
        items[0] = lang_str(LS_QUIT_CONTINUE);
        items[1] = lang_str(LS_QUIT_REALLY);
        items[2] = 0;
        if (select_menu(items, 2, 0) == 1)
            return 2;
    }
    if (quitting())
        return 2;
    return 0;
}

static void host_op(void *ctx, uint8_t op, const int *a, int n, const uint8_t *str)
{
    const char *s = (const char *)str;
    struct lrv_trace_event ev;
    (void)ctx;
    if (lrv_sys_trace_begin(&xe.sys, &ev, XE_TR_OP)) {
        ev.name = s;
        ev.n = n;
        ev.a0 = op;
        ev.a1 = n > 0 ? a[0] : 0;
        ev.a2 = n > 1 ? a[1] : 0;
        ev.a3 = n > 2 ? a[2] : 0;
        ev.ints = a;
        ev.nints = n;
        lrv_sys_trace_send(&xe.sys, &ev);
    }
    switch (op) {
    case 0x03: gfx_clear_lower(); break;
    case 0x0C: gdt_load(s); break;
    case 0x0D: gfx_show(); break;
    case 0x0E: music_load(s); break;
    case 0x0F: music_play(); break;
    case 0x10: music_stop(); break;
    case 0x11:
        if (a[0]) mod_run(XE_HOOK_WINDOW_CLOSE, 0);
        else gvram_clear();
        xe.image_name[0] = 0;
        break;
    case 0x16: gdt_load_diff(s); break;
    case 0x20: wait_vsync(a[0]); break;
    case 0x21: pal_fade_to(xe.pal_target, a[0]); break;
    case 0x22: pal_fade_black(a[0]); break;
    case 0x23: gbuf_clear(); break;
    case 0x24:
        if (a[0]) fx_center_open();
        else fx_blind();
        break;
    case 0x26: lrv_screen_set_display_on(&xe.screen, 0); break;
    case 0x27: lrv_screen_set_display_on(&xe.screen, 1); break;
    case 0x28: gdt_load_buf(s); break;
    case 0x29: mod_run(XE_HOOK_OPENING, a[0]); break;
    case 0x2A: adt_load(s); break;
    case 0x2B: adt_free(); break;
    case 0x2D: adt_play(); break;
    case 0x2E: blink_start(a[0], a[1], a[2], a[3], a[4], a[5]); break;
    case 0x2F: blink_stop(); break;
    case 0x30: music_fade(a[0]); break;
    case 0x31: music_fade_stop_wait(); break;
    case 0x32: lrv_screen_set_scroll(&xe.screen, a[0]); break;
    case 0x33: gdt_load_buf(s); break;
    case 0x34: gdt_load_page1(s); break;
    case 0x35: gfx_merge_page1(); break;
    case 0x36:
        if (a[0] >= 0 && a[0] < 16) {
            xe.pal_target[a[0] * 3] = (uint8_t)(a[1] & 15);
            xe.pal_target[a[0] * 3 + 1] = (uint8_t)(a[2] & 15);
            xe.pal_target[a[0] * 3 + 2] = (uint8_t)(a[3] & 15);
        }
        lrv_screen_set_colour(&xe.screen, a[0], a[1], a[2], a[3]);
        break;
    case 0x38:
        xe.text_x = a[0]; xe.text_y = a[1]; xe.menu_x = a[2]; xe.menu_y = a[3];
        break;
    case 0x39: xe.text_fg = a[0]; xe.text_bg = a[1]; break;
    case 0x3A: music_set_fadevol(a[0]); break;
    case 0x40: text_window_clear(); break;
    case 0x42: pal_fade_white(a[0]); break;
    case 0x43: lrv_screen_set_access(&xe.screen, a[0] != 0); break;
    case 0x44: lrv_screen_set_display(&xe.screen, a[0] != 0); break;
    case 0x45: se_play(a[0]); break;
    case 0x46: se_stop(); break;
    case 0x47: mod_run(XE_HOOK_STAFF_ROLL, 0); break;
    case 0x48: music_wait_status(a[0]); break;
    default:
        lrv_log(xe.hal, LRV_LOG_WARN, "unhandled opcode %02X", op);
        break;
    }
}

static void host_save(void *ctx, struct cc_vm *vm, const uint8_t *name)
{
    (void)ctx;
    voice_stop();
    lrv_sys_trace(&xe.sys, XE_TR_SAVE, (const char *)name, 0, 0, 0, 0, 0);
    save_write((const char *)name, vm);
}

static int host_load(void *ctx, struct cc_vm *vm, const uint8_t *name)
{
    char nm[CC_NAME_LEN];
    int r;
    (void)ctx;
    /* name points into the script's bytecode, which save_read frees when the save belongs to
     * another script: keep a copy for the trace */
    strncpy(nm, (const char *)name, sizeof nm - 1);
    nm[sizeof nm - 1] = 0;
    voice_stop();
    r = save_read(nm, vm);
    lrv_sys_trace(&xe.sys, XE_TR_LOAD, nm, 0, r, 0, 0, 0);
    return r;
}

static void host_trace(void *ctx, int what, int a, int b)
{
    static const int kinds[] = { XE_TR_PAGE, XE_TR_LABEL, XE_TR_FLAG };    /* enum cc_trace */
    (void)ctx;
    lrv_sys_trace(&xe.sys, kinds[what], 0, 0, a, b, 0, 0);
}

static void host_disk(void *ctx, const uint8_t *letter)
{
    (void)ctx;
    (void)letter;                   /* HDD mode: the disk-change request returns at once */
}

static void host_progress(void *ctx, int n)
{
    (void)ctx;
    lrv_sys_trace(&xe.sys, XE_TR_PROGRESS, 0, 0, n, 0, 0, 0);
    progress_set_bit(n);
}

static void host_error(void *ctx, int code, const char *script, int page, int label)
{
    (void)ctx;
    lrv_log(xe.hal, LRV_LOG_ERROR, "script error %d in %s page=%d label=%d", code, script, page, label);
    lrv_sys_trace(&xe.sys, LRV_TR_ERROR, script, 0, code, page, label, 0);
    /* The original prints the message and exits to DOS; we show it and let the run loop end. */
    msg_box(lang_str(LS_SCRIPT_ERROR));
}

static const struct cc_host host = {
    0, host_message, host_menu, host_system_menu, host_op, host_save, host_load, host_disk,
    host_progress, host_error, host_trace, script_at
};

/* ---- title screen (0ABA, engine.md §3) ----------------------------------------------------- */

/* title_menu_hilite 0ABA:0056: XOR-invert bar n, the game's bar (profile title_bar) at the
 * top line of its row (title_rows, DS:06F7) */
static void title_menu_hilite(int n, int rows_total)
{
    struct lrv_surface *pg = lrv_screen_access(&xe.screen);
    const int *bar = xe.profile.title_bar;          /* column, width, lines */
    int p, y, x;
    int y0 = xe.profile.title_rows[n % rows_total];
    for (p = 0; p < 4; p++)
        for (y = 0; y < bar[2]; y++) {
            int off = (y0 + y) * XE_STRIDE + bar[0];
            for (x = 0; x < bar[1]; x++)
                pg->plane[p][off + x] ^= 0xFF;
        }
    lrv_screen_mark(&xe.screen, xe.screen.access_page, bar[0], y0, bar[0] + bar[1],
                    y0 + bar[2]);
}

/* title_menu_select 0ABA:0190: `rows` rows, up/down, OK; no wrap; cancel only in the extras
 * menu */
static int title_menu_select(int rows, int can_cancel)
{
    int cur = 0;
    title_menu_hilite(cur, rows);
    kbd_flush();
    lrv_sys_trace(&xe.sys, XE_TR_TITLE_MENU, 0, 0, rows, 0, 0, 0);
    for (;;) {
        int k = input_get_cmd();
        int next = cur;
        if (quitting()) {
            lrv_sys_trace(&xe.sys, XE_TR_TITLE_SEL, 0, 0, -1, 0, 0, 0);
            return -1;
        }
        if (k == XE_CMD_UP && cur > 0) next = cur - 1;
        if (k == XE_CMD_DOWN && cur < rows - 1) next = cur + 1;
        if (k == XE_CMD_OK) {
            lrv_sys_trace(&xe.sys, XE_TR_TITLE_SEL, 0, 0, cur, 0, 0, 0);
            return cur;
        }
        if (k == XE_CMD_CANCEL && can_cancel) {
            lrv_sys_trace(&xe.sys, XE_TR_TITLE_SEL, 0, 0, -1, 0, 0, 0);
            return -1;
        }
        if (next != cur) {
            title_menu_hilite(cur, rows);
            cur = next;
            title_menu_hilite(cur, rows);
        }
    }
}

static void show_menu_image(const char *name, int diff)
{
    if (diff)
        gdt_load_diff(name);
    else
        gdt_load(name);
    gfx_show();
}

/* omake_menu 0ABA:00F5: the extras menu (profile omake_image, one row per script of
 * omake_scripts), locked until the progress bits of omake_unlock are set. Returns the script
 * name or NULL (cancel). */
static const char *omake_menu(void)
{
    const struct xe_profile *pr = &xe.profile;
    for (;;) {
        int sel;
        show_menu_image(pr->omake_image, 1);
        sel = title_menu_select(pr->nomake, 1);
        if (sel < 0)
            return 0;
        if ((xe.progress & pr->omake_unlock) == pr->omake_unlock)
            return pr->omake[sel];
        text_print(lang_str(LS_OMAKE_LOCKED), 0);
        if (quitting())
            return 0;
    }
}

/* the script a new game starts with: the first rule none of whose progress bits are set,
 * else the profile's start */
static const char *start_script(void)
{
    const struct xe_profile *pr = &xe.profile;
    int i;
    for (i = 0; i < pr->nrules; i++)
        if (!(xe.progress & pr->rule[i].mask))
            return pr->rule[i].script;
    return pr->start;
}

static void game_main(void)
{
    if (mod_run(XE_HOOK_LOGO, 0))
        wait_vsync(0x78);

    for (;;) {
        const char *script;
        int sel;

        if (quitting())
            return;

        lrv_screen_set_display_on(&xe.screen, 0);
        blink_stop();
        music_fade_stop_wait();
        se_stop();
        lrv_screen_set_access(&xe.screen, 1); gvram_clear();
        lrv_screen_set_access(&xe.screen, 0); gvram_clear();
        lrv_screen_set_display(&xe.screen, 0);
        xe.progress = progress_read();

        music_stop();
        show_menu_image(xe.profile.title_image, 0);
        pal_fade_to(xe.pal_target, 1);
        lrv_screen_set_display_on(&xe.screen, 1);

        for (;;) {
            sel = title_menu_select(XE_TITLE_ROWS, 0);
            if (quitting())
                return;
            if (sel == 0) {
                script = start_script();
                break;
            }
            if (sel == 1) {
                script = xe.profile.load_script;
                break;
            }
            script = omake_menu();
            if (script)
                break;
            show_menu_image(xe.profile.title_image, 0);
        }

        /* 0ABA:038D: pal_fade_black(1) - the palette goes black but the graphics display
         * stays ON. Scripts rely on that: a load script may only set its palette and draw its
         * menu, never issuing GRAPH_ON. */
        pal_fade_black(1);
        xe.image_name[0] = 0;
        cc_vm_clear_flags(&xe.vm);
        xe.vm.leave = 0;
        xe.vm.quit = 0;
        xe.vm.resume_pending = 0;
        xe.vm.menu_cursor = 0;
        memset(xe.vm.submenu_cursor, 0, sizeof xe.vm.submenu_cursor);
        gvram_clear();
        cc_vm_set_script(&xe.vm, script, 0, 0);

        while (!xe.vm.leave && !xe.vm.quit && !quitting()) {
            if (!game_load_script(xe.vm.script))
                break;
            cc_run(&xe.vm);
            unload_script();
            if (quitting())
                return;
        }
        if (xe.vm.quit)
            return;
    }
}

/* ---- engine entry --------------------------------------------------------------------------- */

/* engine_init 0E1A:007F: both pages cleared, display page 0, graphics on */
static void engine_init(void)
{
    cc_vm_init(&xe.vm, &host);
    xe.progress = progress_read();
    lrv_screen_set_access(&xe.screen, 1); gvram_clear();
    lrv_screen_set_access(&xe.screen, 0); gvram_clear();
    lrv_screen_set_display(&xe.screen, 0);
    lrv_screen_set_display_on(&xe.screen, 1);
}

/* what is running when the game ends or starts again */
static void engine_stop(void)
{
    lrv_screen_set_display_on(&xe.screen, 0);
    adt_free();
    blink_stop();
    se_stop();
    music_stop();
    unload_script();
    text_layer_clear();
}

/* the pack's language: --lang, else the profile's */
static void select_language(void)
{
    const char *tag = xe.cfg.lang ? xe.cfg.lang : xe.profile.base.lang;
    int id = lrv_pack_lang_id(&xe.pack, tag);
    if (!id)
        lrv_fatal(xe.hal, "the pack has no language \"%s\"", tag);
    lrv_pack_set_lang(&xe.pack, id);
}

int xe_run(void *ctx, const struct lrv_hal *hal, const struct lrv_run_cfg *cfg)
{
    struct lrv_video_cfg vc;

    memset(&xe, 0, sizeof xe);
    xe.hal = hal;
    xe.cfg = *cfg;
    xe.opt = *(const struct xe_options *)ctx;

    if (lrv_pack_open(&xe.pack, hal, LRV_PACK_MAIN) != LRV_OK)
        lrv_fatal(hal, "asset pack not found");
    if (profile_load() != LRV_OK || mod_init(xe.profile.base.modules) != 0)
        lrv_fatal(hal, "the pack's game profile is missing or wrong (see the log)");
    select_language();
    if (hal->storage_open(xe.profile.base.id) != LRV_OK)
        lrv_log(hal, LRV_LOG_WARN, "storage: cannot open the store of %s", xe.profile.base.id);

    memset(&vc, 0, sizeof vc);
    vc.mode = lrv_pc98_video_mode;
    vc.title = xe.profile.base.title;
    vc.fullscreen = cfg->fullscreen;
    vc.scale_mode = cfg->scale_mode;
    vc.host_mode = cfg->host_mode;
    if (hal->video_open(&vc) != LRV_OK)
        lrv_fatal(hal, "cannot open the display");
    /* VRAM pages 0 and 1 and the off-screen buffers */
    if (lrv_screen_init(&xe.screen, hal, &lrv_pc98_video_mode, 2, 1, LRV_MEM_ANY) != LRV_OK)
        lrv_fatal(hal, "out of memory for the screen");
    lrv_sys_init(&xe.sys, hal, LRV_PC98_W, LRV_PC98_H);
    xe.sys.script_name = xe.vm.script;
    lrv_sys_set_present(&xe.sys, lrv_screen_present_hook, &xe.screen);
    /* the frame as the original's VSYNC interrupt ran it: the blink animator, the sound, the
     * voice (voice_init), then the picture */
    lrv_sys_add_hook(&xe.sys, LRV_HOOK_PUMP, blink_tick, 0);
    text_init();
    text_layer_init();
    snd_init();
    lrv_sys_add_hook(&xe.sys, LRV_HOOK_PUMP, lrv_mixer_pump, &xe.music.mixer);
    voice_init();

    for (;;) {
        engine_init();
        game_main();
        if (!lrv_sys_resetting(&xe.sys))
            break;
        /* the reset button: the game starts again from its logo */
        engine_stop();
        lrv_sys_trace(&xe.sys, LRV_TR_RESET, 0, 0, 0, 0, 0, 0);
        lrv_sys_reset_done(&xe.sys);
    }
    lrv_sys_trace(&xe.sys, LRV_TR_QUIT, 0, 0, 0, 0, 0, 0);

    /* engine_shutdown 0E1A:011C */
    engine_stop();
    voice_shutdown();
    snd_shutdown();
    text_shutdown();
    lrv_screen_free(&xe.screen);
    hal->video_close();
    lrv_pack_close(&xe.pack);
    return 0;
}
