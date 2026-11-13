/* Planar to chunky: a rectangle of a page (planes.h) as 8-bit pixels, one byte per pixel.
 *
 * For backends that hand the display system chunky pixels (the Amiga family on graphics cards
 * and on AROS: WriteChunkyPixels). Portable C, the same result on either byte order: the
 * conversion table holds bytes in memory order and is combined with bitwise operations only. */
#ifndef XE_GFX_P2C_H
#define XE_GFX_P2C_H

#include <stddef.h>
#include <stdint.h>
#include "gfx/planes.h"

/* Fills the table (once; any later call is free). */
void xe_p2c_init(void);

/* Byte columns [x0, x1) of lines [y0, y1) of `page`, rotated by `scroll` lines (display line y
 * shows page line (y + scroll) mod 400), as pixels base + colour index: the pixel at (x, y)
 * goes to dst[(y - y0) * pitch + (x - x0 * 8)]. base must not use bits 0-3 (0, 16, 32, ...). */
void xe_p2c8(const struct xe_planes *page, int x0, int y0, int x1, int y1, int scroll,
             uint8_t *dst, size_t pitch, uint8_t base);

#endif
