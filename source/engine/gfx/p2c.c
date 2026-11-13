/* Planar to chunky (p2c.h). */
#include <string.h>
#include "gfx/p2c.h"

/* g_lut[b] = the eight pixels of plane byte b with bit 0 set where b has a 1 (MSB = leftmost),
 * as two 32-bit words in memory order. A plane's contribution is that shifted left by its
 * plane number; no byte lane ever exceeds 15, so shifting and ORing never crosses bytes. */
static uint32_t g_lut[256][2];
static int g_ready;

void xe_p2c_init(void)
{
    int b, i;
    if (g_ready)
        return;
    for (b = 0; b < 256; b++) {
        uint8_t px[8];
        for (i = 0; i < 8; i++)
            px[i] = (uint8_t)((b >> (7 - i)) & 1);
        memcpy(&g_lut[b][0], px, 4);
        memcpy(&g_lut[b][1], px + 4, 4);
    }
    g_ready = 1;
}

void xe_p2c8(const struct xe_planes *page, int x0, int y0, int x1, int y1, int scroll,
             uint8_t *dst, size_t pitch, uint8_t base)
{
    uint32_t base4;
    uint8_t b4[4];
    int y, x;
    xe_p2c_init();
    memset(b4, base, 4);
    memcpy(&base4, b4, 4);
    for (y = y0; y < y1; y++) {
        int sy = y + scroll;
        size_t off;
        uint8_t *d = dst + (size_t)(y - y0) * pitch;
        const uint8_t *p0, *p1, *p2, *p3;
        if (sy >= XE_SCREEN_H)
            sy -= XE_SCREEN_H;
        off = (size_t)sy * XE_PLANE_STRIDE;
        p0 = page->p[0] + off;
        p1 = page->p[1] + off;
        p2 = page->p[2] + off;
        p3 = page->p[3] + off;
        for (x = x0; x < x1; x++) {
            uint32_t w[2];
            w[0] = base4 | g_lut[p0[x]][0] | g_lut[p1[x]][0] << 1 | g_lut[p2[x]][0] << 2 |
                   g_lut[p3[x]][0] << 3;
            w[1] = base4 | g_lut[p0[x]][1] | g_lut[p1[x]][1] << 1 | g_lut[p2[x]][1] << 2 |
                   g_lut[p3[x]][1] << 3;
            memcpy(d, w, 8);
            d += 8;
        }
    }
}
