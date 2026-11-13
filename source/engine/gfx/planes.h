/* Planar frame buffers: the PC-98 graphic VRAM model (docs/re/graphics.md §1, §7.2).
 *
 * A page is four bit-planes B, R, G, I (bit 0..3 of the colour index), each 80 bytes per
 * line x 400 lines, MSB = leftmost pixel. Offsets are y*80 + x/8, exactly as in the original.
 * The engine owns all planes (VRAM page 0, page 1 and the off-screen buffer "da_data"). */
#ifndef XE_GFX_PLANES_H
#define XE_GFX_PLANES_H

#include <stdint.h>

#define XE_SCREEN_W      640
#define XE_SCREEN_H      400
#define XE_PLANE_STRIDE  80
#define XE_PLANE_BYTES   (XE_PLANE_STRIDE * XE_SCREEN_H)   /* 32 000 */

struct xe_planes {
    uint8_t *p[4];
};

/* A rectangle in byte columns [x0, x1) and lines [y0, y1). Empty when x1 <= x0 or y1 <= y0. */
struct xe_rect {
    int x0, y0, x1, y1;
};

static inline void xe_rect_clear(struct xe_rect *r)
{
    r->x0 = r->y0 = 0x7fff;
    r->x1 = r->y1 = 0;
}

static inline int xe_rect_empty(const struct xe_rect *r)
{
    return r->x1 <= r->x0 || r->y1 <= r->y0;
}

static inline void xe_rect_union(struct xe_rect *r, const struct xe_rect *o)
{
    if (xe_rect_empty(o))
        return;
    if (o->x0 < r->x0) r->x0 = o->x0;
    if (o->y0 < r->y0) r->y0 = o->y0;
    if (o->x1 > r->x1) r->x1 = o->x1;
    if (o->y1 > r->y1) r->y1 = o->y1;
}

static inline void xe_rect_add(struct xe_rect *r, int x0, int y0, int x1, int y1)
{
    struct xe_rect o;
    o.x0 = x0; o.y0 = y0; o.x1 = x1; o.y1 = y1;
    xe_rect_union(r, &o);
}

#endif
