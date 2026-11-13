/* Game core: engine init/shutdown (0E1A), the title/omake/run loop game_main (0ABA, engine.md
 * §3), the system menu (088D:223D) and the script-opcode host (088D handlers -> subsystems,
 * opcodes.md). */
#include <string.h>
#include "xe_internal.h"
#include "game/game.h"
#include "game/save.h"
#include "gfx/gfx.h"
#include "gfx/adt.h"
#include "gfx/blink.h"
#include "snd/snd.h"
#include "text/text.h"
#include "sys.h"
#include "vm/cc.h"
#include "voice/voice.h"

/* ---- script loading ----------------------------------------------------------------------- */

static void unload_script(void)
{
    voice_stop();                       /* Windows stops the clip at every script load */
    voice_script_unload();
    if (xe.script_buf)
        xpak_free(&xe.pak, xe.script_buf, xe.script_size);
    xe.script_buf = 0;
    xe.script_size = 0;
    xe.vm.buf = 0;                      /* no statement position until the next load */
    xe.vm.size = 0;
}

int game_load_script(const char *name)
{
    const struct xpak_entry *e;
    char base[CC_NAME_LEN];
    size_t i;
    /* the name may carry ".cc" (saves store "s0305a.cc") */
    for (i = 0; i < sizeof base - 1 && name[i] && name[i] != '.'; i++)
        base[i] = name[i];
    base[i] = 0;
    unload_script();
    e = xpak_find(&xe.pak, base, XPAK_T_SCRIPT);
    if (!e) {
        sys_log(XE_LOG_ERROR, "script %s.cc not found", base);
        return 0;
    }
    xe.script_buf = xpak_load(&xe.pak, e, &xe.script_size);
    if (!xe.script_buf)
        return 0;
    cc_vm_set_script(&xe.vm, base, xe.script_buf, xe.script_size);
    voice_script(base);
    xe.vm.pos = 0;
    sys_trace(XE_TR_SCRIPT, base, 0, -1, 0, 0, 0);
    return 1;
}

/* ---- script host: opcodes -> subsystems (opcodes.md) --------------------------------------- */

static void host_message(void *ctx, const uint8_t *text)
{
    (void)ctx;
    text_print(text, !xe.in.instant_text);
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
    sys_trace(XE_TR_SYSMENU, 0, 0, r, 0, 0, 0);
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
    if (xe.quit_requested || xe.emergency_quit)
        return 2;
    return 0;
}

static void host_op(void *ctx, uint8_t op, const int *a, int n, const uint8_t *str)
{
    const char *s = (const char *)str;
    (void)ctx;
    if (XE_TRACING()) {
        struct xe_trace_event ev;
        ev.kind = XE_TR_OP; ev.tick = xe.tick; ev.pos = xe.vm.pos; ev.script = xe.vm.script;
        ev.name = s; ev.text = 0; ev.items = 0; ev.n = n;
        ev.a0 = op; ev.a1 = n > 0 ? a[0] : 0; ev.a2 = n > 1 ? a[1] : 0; ev.a3 = n > 2 ? a[2] : 0;
        ev.ints = a; ev.nints = n;
        xe.hal->trace(&ev);
    }
    switch (op) {
    case 0x03: gfx_clear_lower(); break;
    case 0x0C: gdt_load(s); break;
    case 0x0D: gfx_show(); break;
    case 0x0E: music_load(s); break;
    case 0x0F: music_play(); break;
    case 0x10: music_stop(); break;
    case 0x11:
        if (a[0]) fx_waku2_close();
        else gfx_gvram_clear();
        xe.image_name[0] = 0;
        break;
    case 0x16: gdt_load_diff(s); break;
    case 0x20: sys_wait_ticks(a[0]); break;
    case 0x21: pal_fade_to(xe.pal_target, a[0]); break;
    case 0x22: pal_fade_black(a[0]); break;
    case 0x23: gfx_clear_buffers(); break;
    case 0x24:
        if (a[0]) fx_center_open();
        else fx_blind();
        break;
    case 0x26: gfx_graph_on(0); break;
    case 0x27: gfx_graph_on(1); break;
    case 0x28: gdt_load_buf(s); break;
    case 0x29: seq_opening(a[0]); break;
    case 0x2A: adt_load(s); break;
    case 0x2B: adt_free(); break;
    case 0x2D: adt_play(); break;
    case 0x2E: blink_start(a[0], a[1], a[2], a[3], a[4], a[5]); break;
    case 0x2F: blink_stop(); break;
    case 0x30: music_fade(a[0]); break;
    case 0x31: music_fade_stop_wait(); break;
    case 0x32: gfx_scroll(a[0]); break;
    case 0x33: gdt_load_buf(s); break;
    case 0x34: gdt_load_page1(s); break;
    case 0x35: gfx_merge_page1(); break;
    case 0x36:
        if (a[0] >= 0 && a[0] < 16) {
            xe.pal_target[a[0] * 3] = (uint8_t)(a[1] & 15);
            xe.pal_target[a[0] * 3 + 1] = (uint8_t)(a[2] & 15);
            xe.pal_target[a[0] * 3 + 2] = (uint8_t)(a[3] & 15);
        }
        pal_set(a[0], a[1], a[2], a[3]);
        break;
    case 0x38:
        xe.text_x = a[0]; xe.text_y = a[1]; xe.menu_x = a[2]; xe.menu_y = a[3];
        break;
    case 0x39: xe.text_fg = a[0]; xe.text_bg = a[1]; break;
    case 0x3A: music_set_fadevol(a[0]); break;
    case 0x40: text_window_clear(); break;
    case 0x42: pal_fade_white(a[0]); break;
    case 0x43: gfx_set_access_page(a[0] != 0); break;
    case 0x44: gfx_set_display_page(a[0] != 0); break;
    case 0x45: se_play(a[0]); break;
    case 0x46: se_stop(); break;
    case 0x47: seq_staff_roll(); break;
    case 0x48: music_wait_status(a[0]); break;
    default:
        sys_log(XE_LOG_WARN, "unhandled opcode %02X", op);
        break;
    }
}

static void host_save(void *ctx, struct cc_vm *vm, const uint8_t *name)
{
    (void)ctx;
    voice_stop();
    sys_trace(XE_TR_SAVE, (const char *)name, 0, 0, 0, 0, 0);
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
    sys_trace(XE_TR_LOAD, nm, 0, r, 0, 0, 0);
    return r;
}

static void host_trace(void *ctx, int what, int a, int b)
{
    (void)ctx;
    sys_trace(what, 0, 0, a, b, 0, 0);
}

static void host_disk(void *ctx, const uint8_t *letter)
{
    (void)ctx;
    (void)letter;                   /* HDD mode: the disk-change request returns at once */
}

static void host_progress(void *ctx, int n)
{
    (void)ctx;
    sys_trace(XE_TR_PROGRESS, 0, 0, n, 0, 0, 0);
    progress_set_bit(n);
}

static void host_error(void *ctx, int code, const char *script, int page, int label)
{
    (void)ctx;
    sys_log(XE_LOG_ERROR, "script error %d in %s page=%d label=%d", code, script, page, label);
    sys_trace(XE_TR_ERROR, script, 0, code, page, label, 0);
    /* The original prints the message and exits to DOS; we show it and let the run loop end. */
    msg_box(lang_str(LS_SCRIPT_ERROR));
}

static const struct cc_host host = {
    0, host_message, host_menu, host_system_menu, host_op, host_save, host_load, host_disk,
    host_progress, host_error, host_trace
};

/* ---- title screen (0ABA, engine.md §3) ----------------------------------------------------- */

/* title_menu_hilite 0ABA:0056: XOR-invert bar n: 28 bytes x 32 lines at x = 26 bytes,
 * y from DS:06F7 = 176, 224, 272 */
static const int title_rows[3] = { 176, 224, 272 };

static void title_menu_hilite(int n, int rows_total)
{
    struct xe_planes *pg = gfx_access();
    int p, y, x;
    int y0 = title_rows[n % rows_total];
    for (p = 0; p < 4; p++)
        for (y = 0; y < 32; y++) {
            int off = (y0 + y) * XE_PLANE_STRIDE + 26;
            for (x = 0; x < 28; x++)
                pg->p[p][off + x] ^= 0xFF;
        }
    gfx_mark_dirty(xe.access_page, 26, y0, 54, y0 + 32);
}

/* title_menu_select 0ABA:0190: 3 rows, up/down, OK; no cancel, no wrap */
static int title_menu_select(int rows)
{
    int cur = 0;
    title_menu_hilite(cur, rows);
    sys_kbd_flush();
    sys_trace(XE_TR_TITLE_MENU, 0, 0, rows, 0, 0, 0);
    for (;;) {
        int k = sys_input_get_cmd();
        int next = cur;
        if (xe.quit_requested || xe.emergency_quit) {
            sys_trace(XE_TR_TITLE_SEL, 0, 0, -1, 0, 0, 0);
            return -1;
        }
        if (k == SYS_CMD_UP && cur > 0) next = cur - 1;
        if (k == SYS_CMD_DOWN && cur < rows - 1) next = cur + 1;
        if (k == SYS_CMD_OK) {
            sys_trace(XE_TR_TITLE_SEL, 0, 0, cur, 0, 0, 0);
            return cur;
        }
        if (k == SYS_CMD_CANCEL && rows == 2) {             /* omake menu: cancel allowed */
            sys_trace(XE_TR_TITLE_SEL, 0, 0, -1, 0, 0, 0);
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

/* omake_menu 0ABA:00F5: MENU2, 2 rows: CG gallery (k) / music mode (m); locked until all
 * seven progress bits are set. Returns the script name or NULL (cancel). */
static const char *omake_menu(void)
{
    for (;;) {
        int sel;
        show_menu_image("menu2", 1);
        sel = title_menu_select(2);
        if (sel < 0)
            return 0;
        if ((xe.progress & 0x7F) == 0x7F)
            return sel == 0 ? "k" : "m";
        text_print(lang_str(LS_OMAKE_LOCKED), 0);
        if (xe.quit_requested || xe.emergency_quit)
            return 0;
    }
}

static void game_main(void)
{
    seq_logo();
    sys_wait_ticks(0x78);

    for (;;) {
        const char *script;
        int sel;

        if (sys_quitting())
            return;

        gfx_graph_on(0);
        blink_stop();
        music_fade_stop_wait();
        se_stop();
        gfx_set_access_page(1); gfx_gvram_clear();
        gfx_set_access_page(0); gfx_gvram_clear();
        gfx_set_display_page(0);
        xe.progress = progress_read();

        music_stop();
        show_menu_image("menu1", 0);
        pal_fade_to(xe.pal_target, 1);
        gfx_graph_on(1);

        for (;;) {
            sel = title_menu_select(3);
            if (xe.quit_requested || xe.emergency_quit)
                return;
            if (sel == 0) {
                unsigned p = xe.progress;
                script = !(p & 1) ? "s00" : (!(p & 2) || !(p & 0x3C)) ? "s00b" : "s00c";
                break;
            }
            if (sel == 1) {
                script = "l";
                break;
            }
            script = omake_menu();
            if (script)
                break;
            show_menu_image("menu1", 0);
        }

        /* 0ABA:038D: pal_fade_black(1) - the palette goes black but the graphics display
         * stays ON. Scripts rely on that: L.CC only does PALETTE 15,15,15,15 and draws its
         * menu, it never issues GRAPH_ON. */
        pal_fade_black(1);
        xe.image_name[0] = 0;
        cc_vm_clear_flags(&xe.vm);
        xe.vm.leave = 0;
        xe.vm.quit = 0;
        xe.vm.resume_pending = 0;
        xe.vm.menu_cursor = 0;
        memset(xe.vm.submenu_cursor, 0, sizeof xe.vm.submenu_cursor);
        gfx_gvram_clear();
        cc_vm_set_script(&xe.vm, script, 0, 0);

        while (!xe.vm.leave && !xe.vm.quit && !sys_quitting()) {
            if (!game_load_script(xe.vm.script))
                break;
            cc_run(&xe.vm);
            unload_script();
            if (xe.quit_requested || xe.emergency_quit)
                return;
        }
        if (xe.vm.quit)
            return;
    }
}

/* ---- engine entry --------------------------------------------------------------------------- */

static int pick_language(void)
{
    if (xe.cfg.language)
        return xe.cfg.language;
    if (xpak_find(&xe.pak, "s00", XPAK_T_SCRIPT))
        return XPAK_L_ANY;
    return XPAK_L_EN;
}

int xe_run(const struct xe_hal *hal, const struct xe_config *cfg)
{
    memset(&xe, 0, sizeof xe);
    xe.hal = hal;
    xe.cfg = *cfg;

    if (xpak_open(&xe.pak, hal, XE_PACK_MAIN) != 0)
        sys_fatal("asset pack not found");
    xe.lang = pick_language();
    xe.pak.lang = (uint8_t)xe.lang;
    if (!xpak_find(&xe.pak, "s00", XPAK_T_SCRIPT))
        sys_fatal("the pack has no scripts for the selected language");

    if (hal->video_open(&cfg->video) != 0)
        sys_fatal("cannot open the display");
    gfx_init();
    text_init();
    snd_init();
    voice_init();
    cc_vm_init(&xe.vm, &host);
    xe.progress = progress_read();

    /* engine_init 0E1A:007F: both pages cleared, display page 0, graphics on, user glyph */
    gfx_set_access_page(1); gfx_gvram_clear();
    gfx_set_access_page(0); gfx_gvram_clear();
    gfx_set_display_page(0);
    gfx_graph_on(1);

    game_main();
    sys_trace(XE_TR_QUIT, 0, 0, 0, 0, 0, 0);

    /* engine_shutdown 0E1A:011C */
    gfx_graph_on(0);
    adt_free();
    blink_stop();
    se_stop();
    music_stop();
    unload_script();
    voice_shutdown();
    snd_shutdown();
    text_shutdown();
    gfx_shutdown();
    hal->video_close();
    xpak_close(&xe.pak);
    return 0;
}
