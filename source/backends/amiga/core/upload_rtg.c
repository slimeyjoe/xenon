/* The rtg upload (upload.h): BltBitMap from the engine's planes, wrapped in a graphics.library
 * bitmap, into a graphics card bitmap; the system does the planar->chunky conversion
 * (Picasso96 in software, which is the right path there: probes/amiga/README.md).
 *
 * On a depth-8 screen the source bitmap gets four constant planes (plane 4 all ones, 5-7
 * zero): the engine's colours land on pens 16..31 and every byte of the chunky buffer is
 * written, so stale upper bits in uncleared video memory cannot show (a depth-4
 * planar->chunky blit writes only the low four bits of each byte; found by the WinUAE
 * presentation test, plan §10). */
#include "os_begin.h"
#include <exec/memory.h>
#include <graphics/gfx.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include "os_end.h"
#include <string.h>
#include "amiga.h"
#include "amsys.h"
#include "upload.h"

#define CONST_SIZE (2 * AM_BPR * AM_H)

static struct BitMap g_src;         /* the engine page as a graphics.library bitmap */
static UBYTE *g_const_planes;       /* depth 8: a ones plane and a zero plane */

static void rtg_fini(void)
{
    if (g_const_planes)
        FreeMem(g_const_planes, CONST_SIZE);
    g_const_planes = 0;
}

static int rtg_init(int depth, int pen_base)
{
    int p;
    (void)depth;
    rtg_fini();
    InitBitMap(&g_src, pen_base ? 8 : 4, AM_W, AM_H);
    if (pen_base) {
        g_const_planes = AllocMem(CONST_SIZE, MEMF_ANY | MEMF_CLEAR);
        if (!g_const_planes)
            return -1;
        memset(g_const_planes, 0xFF, AM_BPR * AM_H);
        am_xcpu_flush(g_const_planes, CONST_SIZE);  /* read by BltBitMap on the 68k */
        g_src.Planes[4] = g_const_planes;
        for (p = 5; p < 8; p++)
            g_src.Planes[p] = g_const_planes + AM_BPR * AM_H;
    }
    return 0;
}

/* WarpOS: the page rows [y0,y1) (rotated by `scroll`) that the 68k is about to read
 * (BltBitMap runs on the 68k), out of the PPC data cache; nothing elsewhere */
static void flush_page_rows(const struct xe_planes *page, int y0, int y1, int scroll)
{
    int p, a = y0 + scroll, b = y1 + scroll;
    for (p = 0; p < 4; p++) {
        const uint8_t *s = page->p[p];
        if (b <= AM_H) {
            am_xcpu_flush(s + a * AM_BPR, (ULONG)(b - a) * AM_BPR);
        } else if (a >= AM_H) {
            am_xcpu_flush(s + (a - AM_H) * AM_BPR, (ULONG)(b - a) * AM_BPR);
        } else {
            am_xcpu_flush(s + a * AM_BPR, (ULONG)(AM_H - a) * AM_BPR);
            am_xcpu_flush(s, (ULONG)(b - AM_H) * AM_BPR);
        }
    }
}

static void rtg_copy(struct BitMap *dst, const struct xe_planes *page, int x0, int y0, int x1,
                     int y1, int scroll, int dx, int dy)
{
    int p;
    flush_page_rows(page, y0, y1, scroll);
    for (p = 0; p < 4; p++)
        g_src.Planes[p] = (PLANEPTR)page->p[p];
    if (scroll == 0) {
        BltBitMap(&g_src, x0 * 8, y0, dst, x0 * 8 + dx, y0 + dy, (x1 - x0) * 8, y1 - y0, 0xC0, 0xFF, NULL);
    } else {
        /* rows scroll..H-1 land at 0.., rows 0..scroll-1 at H-scroll.. */
        int top = AM_H - scroll;
        if (y0 < top) {
            int ye = y1 < top ? y1 : top;
            BltBitMap(&g_src, x0 * 8, y0 + scroll, dst, x0 * 8 + dx, y0 + dy, (x1 - x0) * 8, ye - y0, 0xC0, 0xFF, NULL);
        }
        if (y1 > top) {
            int ys = y0 > top ? y0 : top;
            BltBitMap(&g_src, x0 * 8, ys - top, dst, x0 * 8 + dx, ys + dy, (x1 - x0) * 8, y1 - ys, 0xC0, 0xFF, NULL);
        }
    }
}

const struct am_upload am_upload_rtg = {
    "rtg", 0, rtg_init, rtg_fini, rtg_copy
};
