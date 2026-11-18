/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Graphics - docs/re/graphics.md §3, platform.md §6: the routines of segments 0C2A (pictures,
 * copies, effects) and 0DCA (palette) on lrvne's screen (xe.screen: VRAM pages 0 and 1, the
 * off-screen buffers as its buffer 0, the DAC). Page selection, the display switch and the
 * scroll line are the screen's setters, called directly. */
#ifndef XE_GFX_GFX_H
#define XE_GFX_GFX_H

#include <stdint.h>
#include "lrvne/surface.h"

/* the off-screen "da_data" buffers DS:13DC */
#define XE_GBUF (&xe.screen.buffer[0])

/* clears and copies */
void gvram_clear(void);                          /* GRCG clear of the access page */
void gbuf_clear(void);                           /* memset the off-screen planes */
void gfx_show(void);                             /* G_DISP: display rect buffers -> access page */
void gfx_show_rows(int off, int wb, int h);      /* h lines of wb bytes at off: buffers -> page */

/* effects (one frame per step, CTRL skips) */
void fx_blind(void);                             /* G_EFFECT 0 */
void fx_center_open(void);                       /* G_EFFECT 1 */
void fx_dissolve(void);                          /* opening */

/* palette (0DCA) */
void pal_fade_to(const uint8_t target[48], int delay);
void pal_fade_black(int delay);
void pal_fade_white(int delay);

/* GDT loading (0C2A). Names are base names without extension. Return 1 on success. */
int gdt_load(const char *name);                  /* G_LOAD: buffers + palette + rect + name */
int gdt_load_buf(const char *name);              /* G_LOAD_OVL / G_LOAD_BUF */
int gdt_load_diff(const char *name);             /* G_SABUN_LOAD */
int gdt_load_page1(const char *name);            /* G_LOAD_PARTS */
void gfx_merge_page1(void);                      /* G_PARTS_COPY */
void gfx_clear_lower(void);                      /* CLEAR_LOWER */

/* a decoder wrote rectangle r into page `page` */
void gfx_mark_written(int page, const struct lrv_rect *r);

#endif
