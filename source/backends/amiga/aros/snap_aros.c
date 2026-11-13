/* Snapshots on AROS (flavour.h am_snap_capture): the shown page read back from its true colour
 * bitmap with ReadPixelArray (what the display really shows, the palette applied) and written
 * as a binary PPM: 640x400, or as large as SCALE/ASPECT show it (si->sx, si->dst_h; in a
 * window its inside, which the test keeps uncovered). tests/e2e/aros.py compares it with the
 * null backend's frame. */
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <graphics/rastport.h>
#include <cybergraphx/cybergraphics.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/cybergraphics.h>
#include <stdio.h>
#include <string.h>
#include "amiga.h"
#include "flavour.h"

#define W 640
#define H 400

int am_snap_capture(const struct am_snapinfo *si, const char *path)
{
    struct RastPort rp;
    UBYTE *rgb;
    char head[32];
    BPTR fh;
    LONG n;
    int ok = 0, w = si->sx ? W * si->sx : W, h = si->sx ? si->dst_h : H;
    ULONG size = (ULONG)w * (ULONG)h * 3;
    if (!si->bm || !CyberGfxBase)
        return 0;
    rgb = AllocMem(size, MEMF_ANY);
    if (!rgb)
        return 0;
    InitRastPort(&rp);
    rp.BitMap = si->bm;
    ReadPixelArray(rgb, 0, 0, w * 3, &rp, si->dx, si->dy, w, h, RECTFMT_RGB);
    fh = Open((CONST_STRPTR)path, MODE_NEWFILE);
    if (fh) {
        n = sprintf(head, "P6\n%d %d\n255\n", w, h);
        ok = Write(fh, head, n) == n && Write(fh, rgb, (LONG)size) == (LONG)size;
        Close(fh);
    }
    if (!ok)
        am_log("snap: cannot write %s\n", path);
    FreeMem(rgb, size);
    return ok;
}
