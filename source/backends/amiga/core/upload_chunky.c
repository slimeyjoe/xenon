/* The chunky upload (upload.h): the changed rectangle converted to 8-bit pixels here
 * (engine/gfx/p2c.h: pen = pen base + colour index), then one WriteChunkyPixels (graphics
 * V40) per rectangle. The system hands chunky pixels to the graphics card directly, which is
 * the fast path on AROS and the one that worked in MorphOS's QEMU, where planar blits into an
 * 8-bit bitmap failed (probes/morphos/README.md). */
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
#include "gfx/p2c.h"

#define BUF_SIZE ((ULONG)AM_W * AM_H)

static UBYTE *g_buf;                /* the page as pixels, AM_W bytes per row */
static struct RastPort g_rp;
static uint8_t g_base;

static void chunky_fini(void)
{
    if (g_buf)
        FreeMem(g_buf, BUF_SIZE);
    g_buf = 0;
}

static int chunky_init(int depth, int pen_base)
{
    (void)depth;
    chunky_fini();
    g_buf = AllocMem(BUF_SIZE, MEMF_ANY);
    if (!g_buf)
        return -1;
    InitRastPort(&g_rp);
    g_base = (uint8_t)pen_base;
    xe_p2c_init();
    return 0;
}

static void chunky_copy(struct BitMap *dst, const struct xe_planes *page, int x0, int y0, int x1,
                        int y1, int scroll, int dx, int dy)
{
    UBYTE *d = g_buf + (ULONG)y0 * AM_W + (ULONG)x0 * 8;
    xe_p2c8(page, x0, y0, x1, y1, scroll, d, AM_W, g_base);
    /* WarpOS: WriteChunkyPixels reads the pixels on the 68k */
    am_xcpu_flush(d, (ULONG)(y1 - y0 - 1) * AM_W + (ULONG)(x1 - x0) * 8);
    g_rp.BitMap = dst;
    WriteChunkyPixels(&g_rp, (ULONG)(x0 * 8 + dx), (ULONG)(y0 + dy), (ULONG)(x1 * 8 + dx) - 1,
                      (ULONG)(y1 + dy) - 1, d, AM_W);
}

const struct am_upload am_upload_chunky = {
    "chunky", 0, chunky_init, chunky_fini, chunky_copy
};
