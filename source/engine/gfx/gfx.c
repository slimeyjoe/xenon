/* Graphics subsystem: port of the routines of segments 0C2A (gfx) and 0DCA (display/palette),
 * docs/re/graphics.md §3, platform.md §6. The planes are plain memory (gfx/planes.h); every
 * write marks a dirty region on its page so the backend uploads only what changed. */
#include <string.h>
#include "xe_internal.h"
#include "gfx/gfx.h"
#include "gfx/gdt.h"
#include "util/endian.h"

#define STRIDE XE_PLANE_STRIDE

/* fx_dissolve piece order (DS:0966) */
static const uint8_t dissolve_order[16] = { 0, 10, 4, 14, 8, 2, 12, 6, 13, 7, 1, 11, 5, 15, 9, 3 };

/* ---- init ---------------------------------------------------------------------------------- */

void gfx_init(void)
{
    int p;
    xe.planemem = sys_alloc(3 * 4 * XE_PLANE_BYTES);
    for (p = 0; p < 4; p++) {
        xe.page[0].p[p] = xe.planemem + (0 * 4 + p) * XE_PLANE_BYTES;
        xe.page[1].p[p] = xe.planemem + (1 * 4 + p) * XE_PLANE_BYTES;
        xe.gbuf.p[p]    = xe.planemem + (2 * 4 + p) * XE_PLANE_BYTES;
    }
    xe_rect_clear(&xe.dirty[0]);
    xe_rect_clear(&xe.dirty[1]);
    xe.access_page = 0;
    xe.display_page = 0;
    xe.page_switched = 1;
    xe.graphics_on = 0;
    memset(xe.pal, 0, sizeof xe.pal);
    memset(xe.pal_target, 0, sizeof xe.pal_target);
    xe.rect_off = 0;
    xe.rect_wb = 0;
    xe.rect_h = 0;
    xe.image_name[0] = 0;
}

void gfx_shutdown(void)
{
    sys_free(xe.planemem, 3 * 4 * XE_PLANE_BYTES);
    xe.planemem = 0;
}

/* ---- pages, dirty regions ----------------------------------------------------------------- */

void gfx_set_access_page(int p)
{
    xe.access_page = p ? 1 : 0;
}

void gfx_set_display_page(int p)
{
    p = p ? 1 : 0;
    if (p != xe.display_page) {
        xe.display_page = p;
        xe.page_switched = 1;
    }
}

void gfx_graph_on(int on)
{
    xe.graphics_on = on;
}

void gfx_scroll(int line)
{
    xe.scroll_line = line;
}

struct xe_planes *gfx_access(void)
{
    return &xe.page[xe.access_page];
}

void gfx_mark_dirty(int page, int x0, int y0, int x1, int y1)
{
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > STRIDE) x1 = STRIDE;
    if (y1 > XE_SCREEN_H) y1 = XE_SCREEN_H;
    xe_rect_add(&xe.dirty[page], x0, y0, x1, y1);
}

void gfx_mark_dirty_off(int page, int off, int wb, int h)
{
    int y = off / STRIDE, x = off % STRIDE;
    /* a run of wb bytes from x may spill into the next line when x + wb > 80: mark the
     * whole band of lines then (only happens for odd script rectangles) */
    if (x + wb > STRIDE)
        gfx_mark_dirty(page, 0, y, STRIDE, y + h + 1);
    else
        gfx_mark_dirty(page, x, y, x + wb, y + h);
}

/* ---- clears and copies --------------------------------------------------------------------- */

void gfx_gvram_clear(void)
{
    struct xe_planes *pg = gfx_access();
    int p;
    for (p = 0; p < 4; p++)
        memset(pg->p[p], 0, XE_PLANE_BYTES);
    gfx_mark_dirty(xe.access_page, 0, 0, STRIDE, XE_SCREEN_H);
    sys_trace(XE_TR_CLEAR, 0, 0, 0, xe.access_page, 0, 0);
}

void gfx_clear_buffers(void)
{
    int p;
    for (p = 0; p < 4; p++)
        memset(xe.gbuf.p[p], 0, XE_PLANE_BYTES);
    sys_trace(XE_TR_CLEAR, 0, 0, 1, 0, 0, 0);
}

/* gfx_copy_row 0C2A:05E6 repeated over h lines: whole bytes of all four planes. */
void gfx_copy_rows(struct xe_planes *dst, const struct xe_planes *src, int off, int wb, int h)
{
    int p, y;
    if (off < 0 || wb <= 0 || h <= 0)
        return;
    for (p = 0; p < 4; p++) {
        uint8_t *d = dst->p[p] + off;
        const uint8_t *s = src->p[p] + off;
        for (y = 0; y < h; y++) {
            if (off + y * STRIDE + wb > XE_PLANE_BYTES)
                break;
            memcpy(d, s, (size_t)wb);
            d += STRIDE;
            s += STRIDE;
        }
    }
}

/* copy rows of the buffer rect into the access page and mark them dirty */
static void show_rows(int off, int wb, int h)
{
    gfx_copy_rows(gfx_access(), &xe.gbuf, off, wb, h);
    gfx_mark_dirty_off(xe.access_page, off, wb, h);
}

void gfx_show(void)
{
    show_rows(xe.rect_off, xe.rect_wb, xe.rect_h);
    sys_trace(XE_TR_GDISP, 0, 0, xe.rect_off, xe.rect_wb, xe.rect_h, 0);
}

/* ---- effects (graphics.md §3.2) ------------------------------------------------------------ */

/* fx_blind 0C2A:0690: 8-line bands; pass 1 top->bottom rows 0,1,4,5; pass 2 bottom->top
 * rows 7,6,3,2; one frame per band per pass. */
void fx_blind(void)
{
    int bands = xe.rect_h / 8, b, k;
    static const int pass1[4] = { 0, 1, 4, 5 }, pass2[4] = { 7, 6, 3, 2 };
    sys_trace(XE_TR_EFFECT, 0, 0, 0, 0, xe.rect_h, 0);
    for (b = 0; b < bands; b++) {
        int base = xe.rect_off + b * 8 * STRIDE;
        for (k = 0; k < 4; k++)
            show_rows(base + pass1[k] * STRIDE, xe.rect_wb, 1);
        sys_wait_ticks(1);
    }
    for (b = bands - 1; b >= 0; b--) {
        int base = xe.rect_off + b * 8 * STRIDE;
        for (k = 0; k < 4; k++)
            show_rows(base + pass2[k] * STRIDE, xe.rect_wb, 1);
        sys_wait_ticks(1);
    }
    sys_trace(XE_TR_EFFECT, 0, 0, 0, 1, 0, 0);
}

/* fx_center_open 0C2A:075E: from the middle row outwards, alternately down and up, 8 rows
 * per frame, h/8 steps. */
void fx_center_open(void)
{
    int steps = xe.rect_h / 8, s;
    int mid = xe.rect_h / 2 - 1;
    int down = mid + 1, up = mid;
    sys_trace(XE_TR_EFFECT, 0, 0, 1, 0, xe.rect_h, 0);
    for (s = 0; s < steps; s++) {
        int k;
        for (k = 0; k < 4; k++) {
            if (down < xe.rect_h)
                show_rows(xe.rect_off + down * STRIDE, xe.rect_wb, 1), down++;
            if (up >= 0)
                show_rows(xe.rect_off + up * STRIDE, xe.rect_wb, 1), up--;
        }
        sys_wait_ticks(1);
    }
    sys_trace(XE_TR_EFFECT, 0, 0, 1, 1, 0, 0);
}

/* fx_waku2_close 0C2A:07F0: load waku2 into the buffers, then over the fixed CG rect
 * (x 40, y 4, 70 bytes, 296 lines) copy rows from the top and the bottom towards the centre,
 * 8 rows per frame (37 steps). */
void fx_waku2_close(void)
{
    const int off = 4 * STRIDE + 5, wb = 70, h = 296;
    int top = 0, bot = h - 1, s;
    gdt_load_buf("waku2");
    sys_trace(XE_TR_EFFECT, 0, 0, 2, 0, 296, 0);
    for (s = 0; s < 37; s++) {
        int k;
        for (k = 0; k < 4; k++) {
            if (top <= bot) {
                show_rows(off + top * STRIDE, wb, 1);
                top++;
            }
            if (bot >= top) {
                show_rows(off + bot * STRIDE, wb, 1);
                bot--;
            }
        }
        sys_wait_ticks(1);
    }
    sys_trace(XE_TR_EFFECT, 0, 0, 2, 1, 0, 0);
}

/* fx_dissolve 0C2A:0962: whole screen, 16 frames; in step s every 8x8 cell receives one
 * 4x1 piece: t = order[s], row t&7, left nibble if t < 8 else right nibble. */
void fx_dissolve(void)
{
    struct xe_planes *pg;
    int s, p, cy, cx;
    xe.rect_off = 0;
    xe.rect_wb = STRIDE;
    xe.rect_h = XE_SCREEN_H;
    pg = gfx_access();
    sys_trace(XE_TR_EFFECT, 0, 0, 3, 0, XE_SCREEN_H, 0);
    for (s = 0; s < 16; s++) {
        int t = dissolve_order[s];
        int row = t & 7;
        uint8_t mask = (t < 8) ? 0xF0 : 0x0F;
        for (p = 0; p < 4; p++) {
            for (cy = 0; cy < XE_SCREEN_H / 8; cy++) {
                int off = (cy * 8 + row) * STRIDE;
                uint8_t *d = pg->p[p] + off;
                const uint8_t *sr = xe.gbuf.p[p] + off;
                for (cx = 0; cx < STRIDE; cx++)
                    d[cx] = (uint8_t)((d[cx] & ~mask) | (sr[cx] & mask));
            }
        }
        gfx_mark_dirty(xe.access_page, 0, 0, STRIDE, XE_SCREEN_H);
        sys_wait_ticks(1);
    }
    sys_trace(XE_TR_EFFECT, 0, 0, 3, 1, 0, 0);
}

/* ---- palette (0DCA:01B9..0494) ------------------------------------------------------------- */

void pal_set(int i, int r, int g, int b)
{
    if (i < 0 || i > 15)
        return;
    xe.pal[i * 3] = (uint8_t)(r & 15);
    xe.pal[i * 3 + 1] = (uint8_t)(g & 15);
    xe.pal[i * 3 + 2] = (uint8_t)(b & 15);
}

void pal_set_all(const uint8_t rgb4[48])
{
    memcpy(xe.pal, rgb4, 48);
}

/* pal_fade_to 0DCA:02F7: each step waits `delay` frames, moves every component one step
 * towards the target and writes all 16 entries; ends when all were equal at the start. */
void pal_fade_to(const uint8_t target[48], int delay)
{
    uint8_t tgt[48];
    int steps;
    memcpy(tgt, target, 48);                   /* passed by value in the original */
    sys_trace(XE_TR_FADE, 0, 0, delay, 0, 0, 0);
    xe.trace_quiet++;
    for (steps = 0; steps < 16; steps++) {
        int i, equal = 0;
        sys_wait_ticks(delay);
        for (i = 0; i < 48; i++) {
            if (xe.pal[i] == tgt[i])
                equal++;
            else if (xe.pal[i] < tgt[i])
                xe.pal[i]++;
            else
                xe.pal[i]--;
        }
        if (equal == 48)
            break;
    }
    xe.trace_quiet--;
    sys_trace(XE_TR_FADE, 0, 0, delay, 1, steps, 0);
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

uint8_t *gdt_read_file(const char *name, uint32_t *size)
{
    const struct xpak_entry *e = xpak_find(&xe.pak, name, XPAK_T_GDT);
    uint8_t *data;
    if (!e) {
        sys_log(XE_LOG_WARN, "gdt: %s.gdt not found", name);
        return 0;
    }
    data = xpak_load(&xe.pak, e, size);
    if (!data)
        sys_log(XE_LOG_WARN, "gdt: cannot read %s.gdt", name);
    return data;
}

void gdt_release(uint8_t *data, uint32_t size)
{
    xpak_free(&xe.pak, data, size);
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
    if (gdt_decode(data, size, &h, &xe.gbuf, 0) != 0)
        sys_log(XE_LOG_WARN, "gdt: stream overrun");
    if (!keep) {
        xe.rect_off = h.y * STRIDE + h.xb;
        xe.rect_wb = h.wb;
        xe.rect_h = h.h;
        if (h.flags & GDT_FLAG_PALETTE) {
            memcpy(xe.pal_target, h.rgb4, 48);
            pal_set_all(h.rgb4);
        }
    } else if (diff && (data[0x0F] == 0xCF)) {
        /* gdt_load_diff 0C2A:04E0: only a diff with the raw flag byte CFh sets its palette */
        memcpy(xe.pal_target, h.rgb4, 48);
        pal_set_all(h.rgb4);
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
    sys_trace(XE_TR_GDT, name, 0, 0, data != 0, 0, 0);
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
    sys_trace(XE_TR_GDT, name, 0, 1, data != 0, 0, 0);
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
    sys_trace(XE_TR_GDT, name, 0, 2, data != 0, 0, 0);
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
    struct xe_rect written;
    uint8_t *data = gdt_read_file(name, &size);
    sys_trace(XE_TR_GDT, name, 0, 3, data != 0, 0, 0);
    if (!data)
        return 0;
    if (gdt_parse(data, size, &h) == 0) {
        gdt_decode(data, size, &h, &xe.page[1], &written);
        xe.parts_off = h.y * STRIDE + h.xb;
        xe.parts_wb = h.wb;
        xe.parts_h = h.h;
        if (!xe_rect_empty(&written))
            gfx_mark_dirty(1, written.x0, written.y0, written.x1, written.y1);
    }
    gdt_release(data, size);
    return 1;
}

/* gfx_merge_page1 0C2A:01F9: merge the parts rect from page 1 into the buffers, colour 8
 * (only the intensity plane set) being transparent. */
void gfx_merge_page1(void)
{
    int y, x;
    for (y = 0; y < xe.parts_h; y++) {
        int off = xe.parts_off + y * STRIDE;
        for (x = 0; x < xe.parts_wb; x++) {
            int i = off + x;
            uint8_t p0, p1, p2, p3, keep;
            if (i >= XE_PLANE_BYTES)
                break;
            p0 = xe.page[1].p[0][i];
            p1 = xe.page[1].p[1][i];
            p2 = xe.page[1].p[2][i];
            p3 = xe.page[1].p[3][i];
            keep = (uint8_t)(~(p0 | p1 | p2) & p3);
            xe.gbuf.p[0][i] = (uint8_t)((xe.gbuf.p[0][i] & keep) | p0);
            xe.gbuf.p[1][i] = (uint8_t)((xe.gbuf.p[1][i] & keep) | p1);
            xe.gbuf.p[2][i] = (uint8_t)((xe.gbuf.p[2][i] & keep) | p2);
            xe.gbuf.p[3][i] = (uint8_t)((xe.gbuf.p[3][i] & keep) | (p3 & ~keep));
        }
    }
}

/* clear_lower_screen 0B7D:0852: rows y >= 0x128 of all planes of the access page */
void gfx_clear_lower(void)
{
    struct xe_planes *pg = gfx_access();
    int p;
    for (p = 0; p < 4; p++)
        memset(pg->p[p] + 0x128 * STRIDE, 0, (size_t)(XE_SCREEN_H - 0x128) * STRIDE);
    gfx_mark_dirty(xe.access_page, 0, 0x128, STRIDE, XE_SCREEN_H);
}
