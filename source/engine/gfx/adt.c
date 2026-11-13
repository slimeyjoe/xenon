/* ADT animation player 0ED8:0001 / 0078 / 0217 (graphics.md §3.6).
 * Frames alternate between the two VRAM pages: page = 1; for each frame: wait delay[i]
 * frames, decode into `page`, display `page`, page ^= 1. */
#include "xe_internal.h"
#include "gfx/adt.h"
#include "gfx/gdt.h"
#include "gfx/gfx.h"
#include "util/endian.h"

static uint8_t *g_adt;
static uint32_t g_adt_size;
static int g_count;
static int g_loop, g_loop_start;

int adt_load(const char *name)
{
    const struct xpak_entry *e;
    adt_free();
    e = xpak_find(&xe.pak, name, XPAK_T_ADT);
    if (!e) {
        sys_log(XE_LOG_WARN, "ADT file is not fount: %s", name);   /* sic, 0ED8 */
        return 0;
    }
    g_adt = xpak_load(&xe.pak, e, &g_adt_size);
    if (!g_adt || g_adt_size < 0x1E9) {
        adt_free();
        return 0;
    }
    g_count = rd16le(g_adt + 3);
    if (g_count > 120)
        g_count = 120;
    g_loop = rd16le(g_adt + 0xF5);
    g_loop_start = rd16le(g_adt + 0xF7);
    sys_trace(XE_TR_ADT, name, 0, -1, g_count, g_loop, g_loop_start);
    return 1;
}

void adt_free(void)
{
    if (g_adt)
        xpak_free(&xe.pak, g_adt, g_adt_size);
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
        off += rd16le(g_adt + 5 + 2 * i);
    }
    sys_trace(XE_TR_ADT, 0, 0, -2, 0, 0, 0);
    xe.trace_quiet++;
    for (;;) {
        int delay = cur < 120 ? rd16le(g_adt + 0xF9 + 2 * cur) : 0;
        struct gdt_header h;
        struct xe_rect written;
        uint32_t fo, fs;
        sys_wait_ticks(delay);
        if (cur >= g_count) {
            if (!g_loop)
                break;
            cur = g_loop_start - 1;
            if (cur < 0 || cur >= g_count)
                break;
        }
        fo = frame_off[cur];
        fs = rd16le(g_adt + 5 + 2 * cur);
        if (fo + fs > g_adt_size)
            break;
        gfx_set_access_page(page);
        if (gdt_parse(g_adt + fo, fs, &h) == 0) {
            gdt_decode(g_adt + fo, fs, &h, &xe.page[page], &written);
            if (!xe_rect_empty(&written))
                gfx_mark_dirty(page, written.x0, written.y0, written.x1, written.y1);
        }
        gfx_set_display_page(page);
        sys_trace(XE_TR_ADT, 0, 0, cur, page, delay, 0);
        page ^= 1;
        cur++;
    }
    xe.trace_quiet--;
    sys_trace(XE_TR_ADT, 0, 0, -3, 0, 0, 0);
}
