/* Integer scaling and the 4:3 aspect stretch of a page, for backends that draw it larger than
 * 640x400 themselves (the Amiga family's SCALE= and ASPECT options and its Workbench window,
 * backends/amiga/README.md "Window, scale, aspect"). The default 640x400 paths never call this.
 *
 * Horizontally each pixel becomes sx pixels (sx = 1..XE_SCALE_MAX). Vertically a row map sends
 * display row r to the destination rows [start[r], start[r + 1]): nearest neighbour, so 400
 * rows to 480 repeats every fifth row, and any destination height from 400 up works.
 *
 * The expansion converts a rectangle of the page (planes.h, rotated by `scroll` as in p2c.h)
 * and writes each pixel through a 16-entry pen table (WriteChunkyPixels draws pens on palette
 * and true colour screens alike: probes/amiga/README.md "The Workbench window"). */
#ifndef XE_GFX_SCALE_H
#define XE_GFX_SCALE_H

#include <stddef.h>
#include <stdint.h>
#include "gfx/planes.h"

#define XE_SCALE_MAX 3

struct xe_rowmap {
    int dst_h;                          /* destination height, >= 400 */
    uint16_t start[XE_SCREEN_H + 1];    /* start[400] = dst_h */
};

/* The map from 400 rows to dst_h (values below 400 are taken as 400). */
void xe_rowmap_init(struct xe_rowmap *m, int dst_h);

/* The destination height that shows the page as a 4:3 picture (the PC-98's 640x400 on its
 * 4:3 monitor) at sx pixels across per page pixel, on a display whose pixels are px_w:px_h
 * (width:height, e.g. 1:1 on a graphics card; a native mode's DisplayInfo Resolution). Never
 * below 400 * sx: rows are repeated, never dropped, and within 2 % of 400 * sx it is 400 * sx. */
int xe_aspect_height(int sx, unsigned px_w, unsigned px_h);

/* Byte columns [x0, x1) of display rows [y0, y1), at sx pixels per page pixel and through the
 * row map: destination rows m->start[y0] .. m->start[y1] - 1, (x1 - x0) * 8 * sx pixels each.
 * Destination row k (counted from m->start[y0]) starts at dst + k * pitch (bytes). */
void xe_expand8(const struct xe_planes *page, int x0, int y0, int x1, int y1, int scroll, int sx,
                const struct xe_rowmap *m, const uint8_t pen[16], uint8_t *dst, size_t pitch);

#endif
