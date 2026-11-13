/* Unit test of the scaling and aspect stretch (engine/gfx/scale.c) against a pixel-by-pixel
 * reference: row maps, aspect heights, and random rectangles, scrolls, factors and
 * destination heights through the expansion. Exit status 0 = pass. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gfx/scale.h"

static int failures, checks;

#define MAX_W (XE_SCREEN_W * XE_SCALE_MAX + 24)
#define MAX_H 1700                      /* the tallest destination below + one row */

static uint8_t g_mem[4][XE_PLANE_BYTES];
static uint8_t g_out8[(size_t)MAX_W * MAX_H];

static uint32_t g_seed = 4242;
static uint32_t rnd(void)
{
    g_seed = g_seed * 1103515245u + 12345u;
    return g_seed >> 8;
}

static void fail(const char *fmt, ...)
{
    va_list ap;
    if (failures++ >= 10)
        return;
    va_start(ap, fmt);
    printf("FAIL ");
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
}

static int ref_pixel(const struct xe_planes *pg, int x, int y, int scroll)
{
    int sy = (y + scroll) % XE_SCREEN_H, p, v = 0;
    for (p = 0; p < 4; p++)
        if (pg->p[p][sy * XE_PLANE_STRIDE + x / 8] & (0x80 >> (x & 7)))
            v |= 1 << p;
    return v;
}

static void check_map(int dst_h)
{
    struct xe_rowmap m;
    int r, want = dst_h < XE_SCREEN_H ? XE_SCREEN_H : dst_h;
    xe_rowmap_init(&m, dst_h);
    checks++;
    if (m.dst_h != want || m.start[0] != 0 || m.start[XE_SCREEN_H] != want) {
        fail("rowmap %d: dst_h %d, ends %d..%d", dst_h, m.dst_h, m.start[0], m.start[XE_SCREEN_H]);
        return;
    }
    for (r = 0; r < XE_SCREEN_H; r++) {
        int n = m.start[r + 1] - m.start[r];
        /* every row shown at least once, at most one more time than the average */
        if (n < 1 || n > want / XE_SCREEN_H + 1) {
            fail("rowmap %d: row %d shown %d times", dst_h, r, n);
            return;
        }
    }
    if (want == XE_SCREEN_H)
        for (r = 0; r <= XE_SCREEN_H; r++)
            if (m.start[r] != r) {
                fail("rowmap %d: not the identity at row %d (%d)", dst_h, r, m.start[r]);
                return;
            }
}

static void check_aspect(int sx, unsigned w, unsigned h, int want)
{
    int got = xe_aspect_height(sx, w, h);
    checks++;
    if (got != want)
        fail("aspect sx %d pixels %u:%u = %d, want %d", sx, w, h, got, want);
}

/* one rectangle through the expansion */
static void one(const struct xe_planes *pg, int x0, int y0, int x1, int y1, int scroll, int sx,
                int dst_h)
{
    struct xe_rowmap m;
    uint8_t pen[16];
    int w = (x1 - x0) * 8 * sx, pad = (int)(rnd() % 3) * 8, i, x, d;
    size_t pitch = (size_t)(w + pad);
    int d0, d1;
    for (i = 0; i < 16; i++)
        pen[i] = (uint8_t)(rnd() & 0xFF);
    xe_rowmap_init(&m, dst_h);
    d0 = m.start[y0];
    d1 = m.start[y1];
    memset(g_out8, 0xEE, sizeof g_out8);
    xe_expand8(pg, x0, y0, x1, y1, scroll, sx, &m, pen, g_out8, pitch);
    checks++;
    for (d = d0; d < d1; d++) {
        int r = y0;
        while (m.start[r + 1] <= d)
            r++;
        for (x = 0; x < (int)pitch; x++) {
            size_t o = (size_t)(d - d0) * pitch + (size_t)x;
            if (x < w) {
                int v = ref_pixel(pg, x0 * 8 + x / sx, r, scroll);
                if (g_out8[o] != pen[v]) {
                    fail("rect %d,%d-%d,%d sx %d dst_h %d scroll %d: dest pixel %d,%d", x0, y0, x1,
                         y1, sx, dst_h, scroll, x, d);
                    return;
                }
            } else if (g_out8[o] != 0xEE) {
                fail("rect %d,%d-%d,%d: wrote into the pitch padding", x0, y0, x1, y1);
                return;
            }
        }
    }
    /* nothing written below the last row */
    for (x = 0; x < (int)pitch; x++) {
        size_t o = (size_t)(d1 - d0) * pitch + (size_t)x;
        if (g_out8[o] != 0xEE) {
            fail("rect %d,%d-%d,%d: wrote below the last row", x0, y0, x1, y1);
            return;
        }
    }
}

int main(void)
{
    static const int heights[] = { 400, 480, 512, 800, 960, 1200, 1440, 1600 };
    struct xe_planes pg;
    int i, p;
    for (p = 0; p < 4; p++) {
        pg.p[p] = g_mem[p];
        for (i = 0; i < XE_PLANE_BYTES; i++)
            g_mem[p][i] = (uint8_t)rnd();
    }
    for (i = 0; i < (int)(sizeof heights / sizeof heights[0]); i++)
        check_map(heights[i]);
    {
        /* 400 -> 480: the fifth row of each five is the repeated one (start = floor(1.2 r)) */
        struct xe_rowmap m;
        static const uint16_t want[7] = { 0, 1, 2, 3, 4, 6, 7 };
        xe_rowmap_init(&m, 480);
        checks++;
        if (memcmp(m.start, want, sizeof want) != 0 || m.start[399] != 478)
            fail("rowmap 480: starts %d %d %d %d %d %d", m.start[0], m.start[1], m.start[2],
                 m.start[3], m.start[4], m.start[5]);
    }
    check_map(0);
    check_map(401);
    check_map(799);

    check_aspect(1, 1, 1, 480);         /* square pixels (graphics card) */
    check_aspect(2, 1, 1, 960);
    check_aspect(3, 1, 1, 1440);
    check_aspect(1, 16, 15, 512);       /* PAL HiRes Laced on a 4:3 monitor: 640x512 fills it */
    check_aspect(1, 5, 6, 400);         /* NTSC HiRes Laced: 640x400 fills it, already 4:3 */
    check_aspect(2, 1, 2, 800);         /* pixels twice as tall: never below 400 * sx */
    check_aspect(1, 0, 0, 480);         /* unknown: square */
    check_aspect(1, 22, 22, 480);       /* PAL HiRes Laced as DisplayInfo has it (probes/amiga) */
    check_aspect(1, 22, 26, 400);       /* 640x400 as Picasso96 has it: 406, within 2 % */
    check_aspect(2, 22, 26, 800);

    one(&pg, 0, 0, XE_PLANE_STRIDE, XE_SCREEN_H, 0, 1, 400);       /* identity */
    one(&pg, 0, 0, XE_PLANE_STRIDE, XE_SCREEN_H, 0, 2, 800);       /* x2 */
    one(&pg, 0, 0, XE_PLANE_STRIDE, XE_SCREEN_H, 77, 3, 1440);     /* x3 aspect, scrolled */
    one(&pg, 0, 0, XE_PLANE_STRIDE, XE_SCREEN_H, 0, 1, 512);       /* PAL aspect */
    one(&pg, 79, 399, 80, 400, 399, 3, 1200);                      /* the last byte */
    for (i = 0; i < 300; i++) {
        int x0 = (int)(rnd() % XE_PLANE_STRIDE), y0 = (int)(rnd() % XE_SCREEN_H);
        int x1 = x0 + 1 + (int)(rnd() % (unsigned)(XE_PLANE_STRIDE - x0));
        int y1 = y0 + 1 + (int)(rnd() % (unsigned)(XE_SCREEN_H - y0));
        int sx = 1 + (int)(rnd() % XE_SCALE_MAX);
        int h = heights[rnd() % (sizeof heights / sizeof heights[0])];
        if (h < XE_SCREEN_H * sx)
            h = XE_SCREEN_H * sx + (int)(rnd() % 200);
        one(&pg, x0, y0, x1, y1, (int)(rnd() % XE_SCREEN_H), sx, h);
    }
    printf("scale: %s (%d checks)\n", failures ? "FAILED" : "ok", checks);
    return failures ? 1 : 0;
}
