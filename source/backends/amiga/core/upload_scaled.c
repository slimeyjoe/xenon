/* The scaled upload (upload.h): SCALE and ASPECT on a graphics card screen (and on AROS). The
 * changed rectangle is expanded here (engine/gfx/scale.h: sx pixels across, rows through the
 * row map of am_geom, pens pen base + colour) a strip of source rows at a time into a buffer
 * of a few dozen KB, and each strip is written with one WriteChunkyPixels - the chunky
 * upload's way, never a full-screen buffer (a 4 MB machine has no 1-2 MB to spare).
 *
 * The Workbench window (video_win.c) draws the same way into its own RastPort, where the
 * layers clip it, with the pens it obtained: am_scaled_setup / am_scaled_draw. */
#include "os_begin.h"
#include <exec/memory.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include "os_end.h"
#include "amiga.h"
#include "amsys.h"
#include "upload.h"

#define STRIP 16                    /* source rows per WriteChunkyPixels */

static UBYTE *g_buf;
static ULONG g_size;
static struct RastPort g_rp;
static uint8_t g_pen[16];

static void scaled_fini(void)
{
    if (g_buf)
        FreeMem(g_buf, g_size);
    g_buf = 0;
}

int am_scaled_setup(const uint8_t pen[16])
{
    int i, reps = (am_geom.dst_h + AM_H - 1) / AM_H;   /* the most rows one display row takes */
    scaled_fini();
    g_size = (ULONG)am_geom.dst_w * (ULONG)(STRIP * reps);
    g_buf = AllocMem(g_size, MEMF_ANY);
    if (!g_buf)
        return -1;
    for (i = 0; i < 16; i++)
        g_pen[i] = pen[i];
    return 0;
}

void am_scaled_release(void)
{
    scaled_fini();
}

void am_scaled_draw(struct RastPort *rp, const struct xe_planes *page, int x0, int y0, int x1,
                    int y1, int scroll, int dx, int dy)
{
    const struct am_geom *g = &am_geom;
    int sx = g->sx, w = (x1 - x0) * 8 * sx, left = x0 * 8 * sx + dx, y, n;
    for (y = y0; y < y1; y += n) {
        int d0, d1;
        n = y1 - y < STRIP ? y1 - y : STRIP;
        d0 = g->map.start[y];
        d1 = g->map.start[y + n];
        xe_expand8(page, x0, y, x1, y + n, scroll, sx, &g->map, g_pen, g_buf, (size_t)w);
        /* WarpOS: WriteChunkyPixels reads the pixels on the 68k */
        am_xcpu_flush(g_buf, (ULONG)(d1 - d0) * (ULONG)w);
        WriteChunkyPixels(rp, (ULONG)left, (ULONG)(d0 + dy), (ULONG)(left + w - 1),
                          (ULONG)(d1 + dy - 1), g_buf, (LONG)w);
    }
}

static int scaled_init(int depth, int pen_base)
{
    uint8_t pen[16];
    int i;
    (void)depth;
    for (i = 0; i < 16; i++)
        pen[i] = (uint8_t)(pen_base + i);
    InitRastPort(&g_rp);
    return am_scaled_setup(pen);
}

static void scaled_copy(struct BitMap *dst, const struct xe_planes *page, int x0, int y0, int x1,
                        int y1, int scroll, int dx, int dy)
{
    g_rp.BitMap = dst;
    am_scaled_draw(&g_rp, page, x0, y0, x1, y1, scroll, dx, dy);
}

const struct am_upload am_upload_scaled = {
    "scaled", 0, scaled_init, scaled_fini, scaled_copy
};
