/* How the engine's page gets into a screen buffer (video.c): one of a few uploads, listed per
 * flavour (flavour.h). video.c opens the screen and decides which one draws where:
 *
 *   planar   chipset/upload_planar.c: the CPU copies the planes into a planar chip RAM
 *            bitmap (BMF_STANDARD, the native display, depth 4). The blitter cannot read the
 *            engine's planes in fast RAM, so this is the fast way there.
 *   rtg      upload_rtg.c: BltBitMap from the engine's planes (a planar bitmap in fast RAM)
 *            into a graphics card bitmap; the system converts (Picasso96 does it in software).
 *   chunky   upload_chunky.c: the page converted to 8-bit pixels here (engine/gfx/p2c.h),
 *            then WriteChunkyPixels. For systems whose planar blits are slow or broken
 *            (AROS, MorphOS), and selectable everywhere with UPLOAD=chunky.
 *
 * A planar upload draws into standard bitmaps only; the flavour's other (first, or the one
 * named by UPLOAD=) draws into everything else. */
#ifndef XE_AMIGA_UPLOAD_H
#define XE_AMIGA_UPLOAD_H

#include "os_begin.h"
#include <exec/types.h>
#include <graphics/gfx.h>
#include "os_end.h"
#include "xe.h"
#include "gfx/scale.h"

#define AM_W 640
#define AM_H 400
#define AM_BPR 80                       /* bytes per row of a page plane */

struct am_upload {
    const char *name;                   /* for UPLOAD= and the log */
    int planar;                         /* draws into BMF_STANDARD bitmaps (depth 4) only */
    /* after the screen is open: its depth and the pen of engine colour 0 (0, or 16 on a
     * depth-8 screen, where pens 16..31 hold the engine's colours). 0 = ready, else the
     * screen is reopened at depth 4 (pen 0) and init called again. */
    int  (*init)(int depth, int pen_base);
    void (*fini)(void);
    /* rows [y0,y1) x byte columns [x0,x1) of the page, page rows rotated by `scroll`
     * (display row y shows page row (y + scroll) mod 400), into bm at the same place moved
     * by dx pixels (a multiple of 8) and dy rows: a screen larger than 640x400 shows the
     * page in its middle */
    void (*copy)(struct BitMap *bm, const struct xe_planes *page, int x0, int y0, int x1, int y1,
                 int scroll, int dx, int dy);
};

/* A screen that shows the page larger (SCALE=, ASPECT; backends/amiga/README.md "Window, scale,
 * aspect"): sx pixels across per page pixel, display row r on rows map.start[r] ..
 * map.start[r + 1] - 1. Set by video.c before the init of the two uploads that use it;
 * sx = 0 while the plain 640x400 path is in use. */
struct am_geom {
    int sx;
    int dst_w, dst_h;
    struct xe_rowmap map;
};
extern struct am_geom am_geom;                      /* video.c */

extern const struct am_upload am_upload_planar;     /* chipset/upload_planar.c */
extern const struct am_upload am_upload_planar_map; /* chipset/upload_planar_map.c: ASPECT, native */
extern const struct am_upload am_upload_scaled;     /* upload_scaled.c: SCALE/ASPECT, graphics card */

/* upload_scaled.c for the Workbench window: buffers for am_geom and the 16 pens of the engine
 * colours (0 or -1); rows [y0,y1) x byte columns [x0,x1) of the page into rp at dx,dy through
 * am_geom; free the buffers */
struct RastPort;
int  am_scaled_setup(const uint8_t pen[16]);
void am_scaled_draw(struct RastPort *rp, const struct xe_planes *page, int x0, int y0, int x1,
                    int y1, int scroll, int dx, int dy);
void am_scaled_release(void);
extern const struct am_upload am_upload_rtg;        /* upload_rtg.c */
extern const struct am_upload am_upload_chunky;     /* upload_chunky.c */

#endif
