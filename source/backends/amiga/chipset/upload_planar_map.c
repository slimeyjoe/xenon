/* The planar upload through a row map (upload.h): ASPECT on a native screen. The CPU copies
 * each display row of the page into every screen row the map gives it (am_geom: 400 rows to
 * e.g. 480 on PAL HiRes Laced, whose pixels DisplayInfo calls square). The plain planar
 * upload (upload_planar.c) stays as it is for the default 640x400 screen. */
#include "os_begin.h"
#include <exec/types.h>
#include <graphics/gfx.h>
#include "os_end.h"
#include <string.h>
#include "amiga.h"
#include "amsys.h"
#include "upload.h"

static int map_init(int depth, int pen_base)
{
    (void)depth; (void)pen_base;
    return 0;
}

static void map_fini(void)
{
}

static void map_copy(struct BitMap *dst, const struct xe_planes *page, int x0, int y0, int x1,
                     int y1, int scroll, int dx, int dy)
{
    const uint16_t *start = am_geom.map.start;
    int bpr = dst->BytesPerRow;
    int wbytes = x1 - x0;
    int p, y, r;
    for (p = 0; p < 4; p++) {
        UBYTE *d = dst->Planes[p] + dy * bpr + dx / 8 + x0;
        const uint8_t *s = page->p[p] + x0;
        for (y = y0; y < y1; y++) {
            int sy = y + scroll;
            if (sy >= AM_H)
                sy -= AM_H;
            for (r = start[y]; r < start[y + 1]; r++)
                memcpy(d + r * bpr, s + sy * AM_BPR, (size_t)wbytes);
        }
    }
    /* WarpOS: the display DMA reads the chip planes the PPC just wrote */
    for (p = 0; p < 4; p++)
        am_xcpu_flush(dst->Planes[p] + (start[y0] + dy) * bpr,
                      (ULONG)(start[y1] - start[y0]) * (ULONG)bpr);
}

const struct am_upload am_upload_planar_map = {
    "planar-map", 1, map_init, map_fini, map_copy
};
