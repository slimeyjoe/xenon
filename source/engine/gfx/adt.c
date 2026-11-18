/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ADT animation player 0ED8:0001 / 0078 / 0217 (graphics.md §3.6).
 * Frames alternate between the two VRAM pages: page = 1; for each frame: wait delay[i]
 * frames, decode into `page`, display `page`, page ^= 1. */
#include "xe.h"
#include "lrvne/endian.h"
#include "gfx/adt.h"
#include "gfx/gdt.h"
#include "gfx/gfx.h"

static uint8_t *g_adt;
static uint32_t g_adt_size;
static int g_count;
static int g_loop, g_loop_start;

int adt_load(const char *name)
{
    const struct lrv_pack_entry *e;
    adt_free();
    e = lrv_pack_find(&xe.pack, name, XE_PT_ADT);
    if (!e) {
        lrv_log(xe.hal, LRV_LOG_WARN, "ADT file is not fount: %s", name);   /* sic, 0ED8 */
        return 0;
    }
    g_adt = lrv_pack_load(&xe.pack, e, &g_adt_size);
    if (!g_adt || g_adt_size < 0x1E9) {
        adt_free();
        return 0;
    }
    g_count = lrv_rd16le(g_adt + 3);
    if (g_count > 120)
        g_count = 120;
    g_loop = lrv_rd16le(g_adt + 0xF5);
    g_loop_start = lrv_rd16le(g_adt + 0xF7);
    lrv_sys_trace(&xe.sys, XE_TR_ADT, name, 0, -1, g_count, g_loop, g_loop_start);
    return 1;
}

void adt_free(void)
{
    if (g_adt)
        lrv_pack_free(&xe.pack, g_adt, g_adt_size);
    g_adt = 0;
    g_count = 0;
}

void adt_play(void)
{
    int page = 1, cur = 0;
    uint32_t off = 0x1E9;
    uint32_t frame_off[120];
    int i;
    if (!g_adt)
        return;
    for (i = 0; i < g_count; i++) {
        frame_off[i] = off;
        off += lrv_rd16le(g_adt + 5 + 2 * i);
    }
    lrv_sys_trace(&xe.sys, XE_TR_ADT, 0, 0, -2, 0, 0, 0);
    xe.trace_quiet++;
    for (;;) {
        int delay = cur < 120 ? lrv_rd16le(g_adt + 0xF9 + 2 * cur) : 0;
        struct gdt_header h;
        struct lrv_rect written;
        uint32_t fo, fs;
        wait_vsync(delay);
        if (cur >= g_count) {
            if (!g_loop)
                break;
            cur = g_loop_start - 1;
            if (cur < 0 || cur >= g_count)
                break;
        }
        fo = frame_off[cur];
        fs = lrv_rd16le(g_adt + 5 + 2 * cur);
        if (fo + fs > g_adt_size)
            break;
        lrv_screen_set_access(&xe.screen, page);
        if (gdt_parse(g_adt + fo, fs, &h) == 0) {
            gdt_decode(g_adt + fo, fs, &h, &xe.screen.page[page], &written);
            gfx_mark_written(page, &written);
        }
        lrv_screen_set_display(&xe.screen, page);
        lrv_sys_trace(&xe.sys, XE_TR_ADT, 0, 0, cur, page, delay, 0);
        page ^= 1;
        cur++;
    }
    xe.trace_quiet--;
    lrv_sys_trace(&xe.sys, XE_TR_ADT, 0, 0, -3, 0, 0, 0);
}
