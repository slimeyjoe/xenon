/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Graphics: port of the routines of segments 0C2A (pictures, copies, effects) and 0DCA
 * (palette), docs/re/graphics.md §3, platform.md §6, on lrvne's screen (gfx.h). Every write
 * into a page is marked, so a backend uploads only what changed. */
#include <string.h>
#include "xe.h"
#include "gfx/gfx.h"
#include "gfx/gdt.h"

#define STRIDE XE_STRIDE

/* fx_dissolve piece order (DS:0966) */
static const uint8_t dissolve_order[16] = { 0, 10, 4, 14, 8, 2, 12, 6, 13, 7, 1, 11, 5, 15, 9, 3 };

/* ---- clears and copies --------------------------------------------------------------------- */

void gvram_clear(void)
{
    lrv_screen_clear_access(&xe.screen);
    lrv_sys_trace(&xe.sys, XE_TR_CLEAR, 0, 0, 0, xe.screen.access_page, 0, 0);
}

void gbuf_clear(void)
{
    lrv_surface_clear(XE_GBUF);
    lrv_sys_trace(&xe.sys, XE_TR_CLEAR, 0, 0, 1, 0, 0, 0);
}

void gfx_mark_written(int page, const struct lrv_rect *r)
{
    if (!lrv_rect_empty(r))
        lrv_screen_mark(&xe.screen, page, r->x0, r->y0, r->x1, r->y1);
}

/* gfx_copy_row 0C2A:05E6 over h lines: rows of the buffer rect into the access page */
void gfx_show_rows(int off, int wb, int h)
{
    lrv_surface_copy_rows(lrv_screen_access(&xe.screen), XE_GBUF, off, wb, h);
    lrv_screen_mark_off(&xe.screen, xe.screen.access_page, off, wb, h);
}

void gfx_show(void)
{
    gfx_show_rows(xe.rect_off, xe.rect_wb, xe.rect_h);
    lrv_sys_trace(&xe.sys, XE_TR_GDISP, 0, 0, xe.rect_off, xe.rect_wb, xe.rect_h, 0);
}

/* ---- effects (graphics.md §3.2) ------------------------------------------------------------ */

/* fx_blind 0C2A:0690: 8-line bands; pass 1 top->bottom rows 0,1,4,5; pass 2 bottom->top
 * rows 7,6,3,2; one frame per band per pass. */
void fx_blind(void)
{
    int bands = xe.rect_h / 8, b, k;
    static const int pass1[4] = { 0, 1, 4, 5 }, pass2[4] = { 7, 6, 3, 2 };
    lrv_sys_trace(&xe.sys, XE_TR_EFFECT, 0, 0, 0, 0, xe.rect_h, 0);
    for (b = 0; b < bands; b++) {
        int base = xe.rect_off + b * 8 * STRIDE;
        for (k = 0; k < 4; k++)
            gfx_show_rows(base + pass1[k] * STRIDE, xe.rect_wb, 1);
        wait_vsync(1);
    }
    for (b = bands - 1; b >= 0; b--) {
        int base = xe.rect_off + b * 8 * STRIDE;
        for (k = 0; k < 4; k++)
            gfx_show_rows(base + pass2[k] * STRIDE, xe.rect_wb, 1);
        wait_vsync(1);
    }
    lrv_sys_trace(&xe.sys, XE_TR_EFFECT, 0, 0, 0, 1, 0, 0);
}

/* fx_center_open 0C2A:075E: from the middle row outwards, alternately down and up, 8 rows
 * per frame, h/8 steps. */
void fx_center_open(void)
{
    int steps = xe.rect_h / 8, s;
    int mid = xe.rect_h / 2 - 1;
    int down = mid + 1, up = mid;
    lrv_sys_trace(&xe.sys, XE_TR_EFFECT, 0, 0, 1, 0, xe.rect_h, 0);
    for (s = 0; s < steps; s++) {
        int k;
        for (k = 0; k < 4; k++) {
            if (down < xe.rect_h)
                gfx_show_rows(xe.rect_off + down * STRIDE, xe.rect_wb, 1), down++;
            if (up >= 0)
                gfx_show_rows(xe.rect_off + up * STRIDE, xe.rect_wb, 1), up--;
        }
        wait_vsync(1);
    }
    lrv_sys_trace(&xe.sys, XE_TR_EFFECT, 0, 0, 1, 1, 0, 0);
}

/* fx_dissolve 0C2A:0962: whole screen, 16 frames; in step s every 8x8 cell receives one
 * 4x1 piece: t = order[s], row t&7, left nibble if t < 8 else right nibble. */
void fx_dissolve(void)
{
    struct lrv_surface *pg, *buf = XE_GBUF;
    int s, p, cy, cx;
    xe.rect_off = 0;
    xe.rect_wb = STRIDE;
    xe.rect_h = XE_SCREEN_H;
    pg = lrv_screen_access(&xe.screen);
    lrv_sys_trace(&xe.sys, XE_TR_EFFECT, 0, 0, 3, 0, XE_SCREEN_H, 0);
    for (s = 0; s < 16; s++) {
        int t = dissolve_order[s];
        int row = t & 7;
        uint8_t mask = (t < 8) ? 0xF0 : 0x0F;
        for (p = 0; p < 4; p++) {
            for (cy = 0; cy < XE_SCREEN_H / 8; cy++) {
                int off = (cy * 8 + row) * STRIDE;
                uint8_t *d = pg->plane[p] + off;
                const uint8_t *sr = buf->plane[p] + off;
                for (cx = 0; cx < STRIDE; cx++)
                    d[cx] = (uint8_t)((d[cx] & ~mask) | (sr[cx] & mask));
            }
        }
        lrv_screen_mark(&xe.screen, xe.screen.access_page, 0, 0, STRIDE, XE_SCREEN_H);
        wait_vsync(1);
    }
    lrv_sys_trace(&xe.sys, XE_TR_EFFECT, 0, 0, 3, 1, 0, 0);
}

/* ---- palette (0DCA:01B9..0494) ------------------------------------------------------------- */

/* pal_fade_to 0DCA:02F7: each step waits `delay` frames, moves every component one step
 * towards the target and writes all 16 entries; ends when all were equal at the start. */
void pal_fade_to(const uint8_t target[48], int delay)
{
    uint8_t tgt[48], cur[48];
    int steps;
    memcpy(tgt, target, 48);                   /* passed by value in the original */
    lrv_sys_trace(&xe.sys, XE_TR_FADE, 0, 0, delay, 0, 0, 0);
    xe.trace_quiet++;
    for (steps = 0; steps < 16; steps++) {
        wait_vsync(delay);
        memcpy(cur, xe.screen.rgb, 48);
        if (!lrv_pal_step_toward(cur, tgt, 48))
            break;
        lrv_screen_set_palette(&xe.screen, cur);
    }
    xe.trace_quiet--;
    lrv_sys_trace(&xe.sys, XE_TR_FADE, 0, 0, delay, 1, steps, 0);
}

void pal_fade_black(int delay)
{
    uint8_t t[48];
    memset(t, 0, sizeof t);
    pal_fade_to(t, delay);
}

void pal_fade_white(int delay)
{
    uint8_t t[48];
    memset(t, 15, sizeof t);
    pal_fade_to(t, delay);
}

/* ---- GDT loading (0C2A:0315 ..) ------------------------------------------------------------ */

static uint8_t *gdt_read_file(const char *name, uint32_t *size)
{
    const struct lrv_pack_entry *e = lrv_pack_find(&xe.pack, name, XE_PT_GDT);
    uint8_t *data;
    if (!e) {
        lrv_log(xe.hal, LRV_LOG_WARN, "gdt: %s.gdt not found", name);
        return 0;
    }
    data = lrv_pack_load(&xe.pack, e, size);
    if (!data)
        lrv_log(xe.hal, LRV_LOG_WARN, "gdt: cannot read %s.gdt", name);
    return data;
}

static void gdt_release(uint8_t *data, uint32_t size)
{
    lrv_pack_free(&xe.pack, data, size);
}

static void set_name(const char *name)
{
    size_t i;
    for (i = 0; i < sizeof xe.image_name - 1 && name[i]; i++)
        xe.image_name[i] = name[i];
    xe.image_name[i] = 0;
}

/* decode into the buffers; keep == 0 also takes palette and display rect (gdt_decode_buf) */
static int decode_to_buffers(const uint8_t *data, uint32_t size, int keep, int diff)
{
    struct gdt_header h;
    if (gdt_parse(data, size, &h) != 0)
        return 0;
    if (gdt_decode(data, size, &h, XE_GBUF, 0) != 0)
        lrv_log(xe.hal, LRV_LOG_WARN, "gdt: stream overrun");
    if (!keep) {
        xe.rect_off = h.y * STRIDE + h.xb;
        xe.rect_wb = h.wb;
        xe.rect_h = h.h;
        if (h.flags & GDT_FLAG_PALETTE) {
            memcpy(xe.pal_target, h.rgb4, 48);
            lrv_screen_set_palette(&xe.screen, h.rgb4);
        }
    } else if (diff && (data[0x0F] == 0xCF)) {
        /* gdt_load_diff 0C2A:04E0: only a diff with the raw flag byte CFh sets its palette */
        memcpy(xe.pal_target, h.rgb4, 48);
        lrv_screen_set_palette(&xe.screen, h.rgb4);
    }
    return 1;
}

int gdt_load(const char *name)
{
    uint32_t size;
    uint8_t *data;
    if (!name[0])
        return 1;
    data = gdt_read_file(name, &size);
    lrv_sys_trace(&xe.sys, XE_TR_GDT, name, 0, 0, data != 0, 0, 0);
    if (!data)
        return 0;
    decode_to_buffers(data, size, 0, 0);
    gdt_release(data, size);
    set_name(name);
    return 1;
}

int gdt_load_buf(const char *name)
{
    uint32_t size;
    uint8_t *data = gdt_read_file(name, &size);
    lrv_sys_trace(&xe.sys, XE_TR_GDT, name, 0, 1, data != 0, 0, 0);
    if (!data)
        return 0;
    decode_to_buffers(data, size, 1, 0);
    gdt_release(data, size);
    set_name(name);
    return 1;
}

int gdt_load_diff(const char *name)
{
    uint32_t size;
    uint8_t *data = gdt_read_file(name, &size);
    lrv_sys_trace(&xe.sys, XE_TR_GDT, name, 0, 2, data != 0, 0, 0);
    if (!data)
        return 0;
    decode_to_buffers(data, size, 1, 1);
    gdt_release(data, size);
    return 1;
}

/* gdt_load_page1 0C2A:0142: decode straight into VRAM page 1, remember the rect. */
int gdt_load_page1(const char *name)
{
    uint32_t size;
    struct gdt_header h;
    struct lrv_rect written;
    uint8_t *data = gdt_read_file(name, &size);
    lrv_sys_trace(&xe.sys, XE_TR_GDT, name, 0, 3, data != 0, 0, 0);
    if (!data)
        return 0;
    if (gdt_parse(data, size, &h) == 0) {
        gdt_decode(data, size, &h, &xe.screen.page[1], &written);
        xe.parts_off = h.y * STRIDE + h.xb;
        xe.parts_wb = h.wb;
        xe.parts_h = h.h;
        gfx_mark_written(1, &written);
    }
    gdt_release(data, size);
    return 1;
}

/* gfx_merge_page1 0C2A:01F9: merge the parts rect from page 1 into the buffers, colour 8
 * (only the intensity plane set) being transparent. */
void gfx_merge_page1(void)
{
    const struct lrv_surface *pg1 = &xe.screen.page[1];
    struct lrv_surface *buf = XE_GBUF;
    int y, x;
    for (y = 0; y < xe.parts_h; y++) {
        int off = xe.parts_off + y * STRIDE;
        for (x = 0; x < xe.parts_wb; x++) {
            int i = off + x;
            uint8_t p0, p1, p2, p3, keep;
            if (i >= XE_PLANE_BYTES)
                break;
            p0 = pg1->plane[0][i];
            p1 = pg1->plane[1][i];
            p2 = pg1->plane[2][i];
            p3 = pg1->plane[3][i];
            keep = (uint8_t)(~(p0 | p1 | p2) & p3);
            buf->plane[0][i] = (uint8_t)((buf->plane[0][i] & keep) | p0);
            buf->plane[1][i] = (uint8_t)((buf->plane[1][i] & keep) | p1);
            buf->plane[2][i] = (uint8_t)((buf->plane[2][i] & keep) | p2);
            buf->plane[3][i] = (uint8_t)((buf->plane[3][i] & keep) | (p3 & ~keep));
        }
    }
}

/* clear_lower_screen 0B7D:0852: rows y >= 0x128 of all planes of the access page */
void gfx_clear_lower(void)
{
    struct lrv_surface *pg = lrv_screen_access(&xe.screen);
    int p;
    for (p = 0; p < 4; p++)
        memset(pg->plane[p] + 0x128 * STRIDE, 0, (size_t)(XE_SCREEN_H - 0x128) * STRIDE);
    lrv_screen_mark(&xe.screen, xe.screen.access_page, 0, 0x128, STRIDE, XE_SCREEN_H);
}
