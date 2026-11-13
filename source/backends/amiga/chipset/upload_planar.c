/* The planar upload (upload.h): the CPU copies the engine's planes into a planar chip RAM
 * bitmap (BMF_STANDARD). The blitter cannot read the engine's planes in fast RAM; a CPU copy
 * of the changed rows and byte columns is the fast way (probes/amiga/README.md). */
#include "os_begin.h"
#include <exec/types.h>
#include <graphics/gfx.h>
#include "os_end.h"
#include <string.h>
#include "amiga.h"
#include "amsys.h"
#include "upload.h"

static int planar_init(int depth, int pen_base)
{
    (void)depth; (void)pen_base;
    return 0;
}

static void planar_fini(void)
{
}

static void planar_copy(struct BitMap *dst, const struct xe_planes *page, int x0, int y0, int x1,
                        int y1, int scroll, int dx, int dy)
{
    int bpr = dst->BytesPerRow;
    int wbytes = x1 - x0;
    int p, y;
    for (p = 0; p < 4; p++) {
        UBYTE *d = dst->Planes[p] + dy * bpr + dx / 8;
        const uint8_t *s = page->p[p];
        if (scroll == 0 && x0 == 0 && x1 == AM_BPR && bpr == AM_BPR) {
            memcpy(d + y0 * AM_BPR, s + y0 * AM_BPR, (size_t)(y1 - y0) * AM_BPR);
            continue;
        }
        for (y = y0; y < y1; y++) {
            int sy = y + scroll;
            if (sy >= AM_H)
                sy -= AM_H;
            memcpy(d + y * bpr + x0, s + sy * AM_BPR + x0, (size_t)wbytes);
        }
    }
    /* WarpOS: the display DMA reads the chip planes the PPC just wrote */
    for (p = 0; p < 4; p++)
        am_xcpu_flush(dst->Planes[p] + (y0 + dy) * bpr, (ULONG)(y1 - y0) * (ULONG)bpr);
}

const struct am_upload am_upload_planar = {
    "planar", 1, planar_init, planar_fini, planar_copy
};
