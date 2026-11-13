/* Unit test of the planar to chunky conversion (engine/gfx/p2c.c) against a pixel-by-pixel
 * reference: random pages, random rectangles, scrolls and pen bases. Exit status 0 = pass. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gfx/p2c.h"

static int failures;

static uint8_t g_mem[4][XE_PLANE_BYTES];
static uint8_t g_out[(XE_SCREEN_W + 16) * XE_SCREEN_H];   /* up to two bytes of pitch padding */

static uint32_t g_seed = 12345;
static uint32_t rnd(void)
{
    g_seed = g_seed * 1103515245u + 12345u;
    return g_seed >> 8;
}

static int ref_pixel(const struct xe_planes *pg, int x, int y, int scroll)
{
    int sy = (y + scroll) % XE_SCREEN_H, p, v = 0;
    for (p = 0; p < 4; p++)
        if (pg->p[p][sy * XE_PLANE_STRIDE + x / 8] & (0x80 >> (x & 7)))
            v |= 1 << p;
    return v;
}

static void one(const struct xe_planes *pg, int x0, int y0, int x1, int y1, int scroll, uint8_t base)
{
    size_t pitch = (size_t)(x1 - x0) * 8 + (rnd() % 3) * 8;    /* also a pitch wider than the rect */
    int x, y;
    memset(g_out, 0xEE, sizeof g_out);
    xe_p2c8(pg, x0, y0, x1, y1, scroll, g_out, pitch, base);
    for (y = y0; y < y1; y++) {
        for (x = x0 * 8; x < x1 * 8; x++) {
            int got = g_out[(size_t)(y - y0) * pitch + (size_t)(x - x0 * 8)];
            int want = base + ref_pixel(pg, x, y, scroll);
            if (got != want) {
                if (failures++ < 10)
                    printf("FAIL rect %d,%d-%d,%d scroll %d base %d: pixel %d,%d = %d, want %d\n",
                           x0, y0, x1, y1, scroll, base, x, y, got, want);
                return;
            }
        }
        /* the pitch padding stays untouched */
        for (x = (x1 - x0) * 8; x < (int)pitch && y + 1 < y1; x++)
            if (g_out[(size_t)(y - y0) * pitch + (size_t)x] != 0xEE) {
                if (failures++ < 10)
                    printf("FAIL rect %d,%d-%d,%d: wrote past the row at %d,%d\n", x0, y0, x1, y1, x, y);
                return;
            }
    }
}

int main(void)
{
    struct xe_planes pg;
    int i, p;
    for (p = 0; p < 4; p++) {
        pg.p[p] = g_mem[p];
        for (i = 0; i < XE_PLANE_BYTES; i++)
            g_mem[p][i] = (uint8_t)rnd();
    }
    one(&pg, 0, 0, XE_PLANE_STRIDE, XE_SCREEN_H, 0, 0);            /* whole page */
    one(&pg, 0, 0, XE_PLANE_STRIDE, XE_SCREEN_H, 123, 16);         /* whole page, scrolled */
    one(&pg, 79, 399, 80, 400, 399, 16);                           /* the last byte */
    for (i = 0; i < 500; i++) {
        int x0 = (int)(rnd() % XE_PLANE_STRIDE), y0 = (int)(rnd() % XE_SCREEN_H);
        int x1 = x0 + 1 + (int)(rnd() % (unsigned)(XE_PLANE_STRIDE - x0));
        int y1 = y0 + 1 + (int)(rnd() % (unsigned)(XE_SCREEN_H - y0));
        one(&pg, x0, y0, x1, y1, (int)(rnd() % XE_SCREEN_H), (uint8_t)((rnd() % 16) * 16));
    }
    printf("p2c: %s\n", failures ? "FAILED" : "503 conversions ok");
    return failures ? 1 : 0;
}
