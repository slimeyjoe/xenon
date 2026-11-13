/* Integer scaling and the aspect stretch (scale.h). */
#include <string.h>
#include "gfx/scale.h"
#include "gfx/p2c.h"

void xe_rowmap_init(struct xe_rowmap *m, int dst_h)
{
    int r;
    if (dst_h < XE_SCREEN_H)
        dst_h = XE_SCREEN_H;
    m->dst_h = dst_h;
    for (r = 0; r <= XE_SCREEN_H; r++)
        m->start[r] = (uint16_t)((long)r * dst_h / XE_SCREEN_H);
}

int xe_aspect_height(int sx, unsigned px_w, unsigned px_h)
{
    /* 640 * sx pixels across of px_w each, as tall as 3/4 of that: h * px_h = 480 * sx * px_w */
    long h, min = (long)XE_SCREEN_H * sx;
    if (px_w == 0 || px_h == 0)
        px_w = px_h = 1;
    h = ((long)480 * sx * (long)px_w * 2 + (long)px_h) / ((long)px_h * 2);
    /* within 2 % of the plain height (NTSC HiRes Laced, 22:26 = 406 lines) is the plain height:
     * a few repeated rows would only make the picture uneven */
    return (int)(h <= min + min / 50 ? min : h);
}

/* one display row of the rectangle as colour indices 0..15 */
static void row_indices(const struct xe_planes *page, int x0, int x1, int y, int scroll,
                        uint8_t *idx)
{
    xe_p2c8(page, x0, y, x1, y + 1, scroll, idx, (size_t)XE_SCREEN_W, 0);
}

void xe_expand8(const struct xe_planes *page, int x0, int y0, int x1, int y1, int scroll, int sx,
                const struct xe_rowmap *m, const uint8_t pen[16], uint8_t *dst, size_t pitch)
{
    uint8_t idx[XE_SCREEN_W];
    int n = (x1 - x0) * 8, w = n * sx, y, i, k;
    uint8_t *d = dst;
    for (y = y0; y < y1; y++) {
        int reps = m->start[y + 1] - m->start[y];
        if (reps <= 0)
            continue;
        row_indices(page, x0, x1, y, scroll, idx);
        if (sx == 1) {
            for (i = 0; i < n; i++)
                d[i] = pen[idx[i]];
        } else if (sx == 2) {
            for (i = 0; i < n; i++)
                d[2 * i] = d[2 * i + 1] = pen[idx[i]];
        } else {
            uint8_t *o = d;
            for (i = 0; i < n; i++) {
                uint8_t v = pen[idx[i]];
                for (k = 0; k < sx; k++)
                    *o++ = v;
            }
        }
        for (k = 1; k < reps; k++)
            memcpy(d + (size_t)k * pitch, d, (size_t)w);
        d += (size_t)reps * pitch;
    }
}
