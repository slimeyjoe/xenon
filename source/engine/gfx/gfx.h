/* Graphics subsystem - docs/re/graphics.md §3, platform.md §6.
 * Pages, the off-screen buffers, copies/effects, palette and GDT loading. */
#ifndef XE_GFX_GFX_H
#define XE_GFX_GFX_H

#include <stdint.h>
#include "gfx/planes.h"

void gfx_init(void);
void gfx_shutdown(void);

/* pages (0DCA:0150 / 0162) and layer control (INT 18h 40h/41h) */
void gfx_set_access_page(int p);
void gfx_set_display_page(int p);
void gfx_graph_on(int on);
void gfx_scroll(int line);                       /* G_SCROLL */
struct xe_planes *gfx_access(void);              /* planes of the access page */
void gfx_mark_dirty(int page, int x0, int y0, int x1, int y1);   /* byte cols, lines */
void gfx_mark_dirty_off(int page, int off, int wb, int h);       /* off = y*80 + x */

/* clears and copies */
void gfx_gvram_clear(void);                      /* GRCG clear of the access page */
void gfx_clear_buffers(void);                    /* memset the off-screen planes */
void gfx_copy_rows(struct xe_planes *dst, const struct xe_planes *src, int off, int wb, int h);
void gfx_show(void);                             /* G_DISP: display rect buffers -> access page */

/* effects (one frame per step, CTRL skips) */
void fx_blind(void);                             /* G_EFFECT 0 */
void fx_center_open(void);                       /* G_EFFECT 1 */
void fx_waku2_close(void);                       /* WINDOW 1 */
void fx_dissolve(void);                          /* opening */

/* palette (0DCA) */
void pal_set(int i, int r, int g, int b);
void pal_set_all(const uint8_t rgb4[48]);
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

/* raw DA1 blob access for the sequences / ADT: load a GDT entry from the pack */
uint8_t *gdt_read_file(const char *name, uint32_t *size);
void gdt_release(uint8_t *data, uint32_t size);

#endif
