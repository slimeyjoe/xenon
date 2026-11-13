/* Snapshot handshake with the host (plan §10, tests/e2e/amiga_snapd.py).
 *
 * The guest waits until the display shows the current buffer (a few vertical blanks: a flip
 * takes effect at the next one, an interlaced frame needs two fields), then writes a request
 * file SNAPDIR/snap_<name>.req with what the host needs to find the image in memory:
 *
 *   name=<name> tick=<n> hash=<8 hex>|none standard=0|1 shown=<0|1> bpr=<n> rows=<n>
 *   depth=<n> mode=<8 hex> planes=<hex>,<hex>,.. penbase=<n>
 *   [sx=<n> dsth=<n> dx=<n> dy=<n>] [window=1 pens=<n>,.. mouse=<x>,<y>] [p96mem=<hex> p96bpr=<n> p96fmt=<n>]
 *
 * (sx..dy only on a SCALE/ASPECT screen: the page is shown sx times as wide and dsth rows tall,
 * through the row map of engine/gfx/scale.h, at dx,dy in the bitmap)
 *
 * (one line; written as .tmp and renamed, so the host never sees half a request). Where the
 * flavour reads the page back itself (flavour.h am_snap_capture: AROS) the image is already
 * in SNAPDIR/snap_<name>.ppm, the request says image=, and the guest goes on. Otherwise the host
 * attaches through the WinUAE gdbserver, which stops the CPU, takes the screenshot, reads the
 * displayed bitmap, detaches and creates snap_<name>.ack. The guest deletes the ack and
 * returns. On a graphics card bitmap the pixel memory comes from Picasso96API.library
 * (p96GetBitMapAttr; the library is optional, without it the bitmap oracle is skipped). */
#include "os_begin.h"
#include <dos/dos.h>
#include <exec/libraries.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include "os_end.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "amiga.h"
#include "flavour.h"

/* attribute numbers of p96GetBitMapAttr (flavour.h am_p96_bitmap_attr) from the P96 developer
 * kit's libraries/Picasso96.h */
#define P96BMA_MEMORY 3
#define P96BMA_BYTESPERROW 4
#define P96BMA_RGBFORMAT 7

static void wait_ack(const char *ack)
{
    int i;
    for (i = 0; i < 60 * 50 / 5; i++) {        /* 60 s */
        BPTR lock = Lock((STRPTR)ack, SHARED_LOCK);
        if (lock) {
            UnLock(lock);
            DeleteFile((STRPTR)ack);
            return;
        }
        Delay(5);
    }
    am_log("snap: no answer from the host for %s\n", ack);
}

void am_snap(const char *name, uint32_t tick, uint32_t hash, int have_hash)
{
    const char *dir = am_opts.snapdir ? am_opts.snapdir : "PROGDIR:";
    char tmp[160], req[160], ack[160], img[160], line[560], *o;
    int captured;
    struct am_snapinfo si;
    struct Library *p96;
    BPTR fh;
    ULONG i;

    for (i = 0; i < 4; i++)
        WaitTOF();
    am_video_snapinfo(&si);
    snprintf(tmp, sizeof tmp, "%ssnap_%s.tmp", dir, name);
    snprintf(req, sizeof req, "%ssnap_%s.req", dir, name);
    snprintf(ack, sizeof ack, "%ssnap_%s.ack", dir, name);
    snprintf(img, sizeof img, "%ssnap_%s.ppm", dir, name);
    captured = am_snap_capture(&si, img);

    o = line;
    o += sprintf(o, "name=%s tick=%lu ", name, (unsigned long)tick);
    if (have_hash)
        o += sprintf(o, "hash=%08lx", (unsigned long)hash);
    else
        o += sprintf(o, "hash=none");
    o += sprintf(o, " standard=%d shown=%d bpr=%lu rows=%lu depth=%lu mode=%08lx planes=",
                 si.standard, si.shown, (unsigned long)si.bpr, (unsigned long)si.rows,
                 (unsigned long)si.depth, (unsigned long)si.mode);
    for (i = 0; i < si.depth && i < 8; i++)
        o += sprintf(o, "%s%lx", i ? "," : "", (unsigned long)(uintptr_t)si.planes[i]);
    o += sprintf(o, " penbase=%d", si.pen_base);
    if (si.sx)                                  /* SCALE/ASPECT (upload.h am_geom) */
        o += sprintf(o, " sx=%d dsth=%d dx=%d dy=%d", si.sx, si.dst_h, si.dx, si.dy);
    if (si.window) {                            /* WINDOW: the engine colours' pens */
        o += sprintf(o, " window=1 pens=");
        for (i = 0; i < 16; i++)
            o += sprintf(o, "%s%d", i ? "," : "", si.pens[i]);
        o += sprintf(o, " mouse=%d,%d", si.mouse_x, si.mouse_y);
    }
    if (captured)
        o += sprintf(o, " image=snap_%s.ppm", name);
    if (!si.standard && (p96 = OpenLibrary("Picasso96API.library", 2)) != 0) {
        o += sprintf(o, " p96mem=%lx p96bpr=%lu p96fmt=%lu",
                     (unsigned long)am_p96_bitmap_attr(p96, si.bm, P96BMA_MEMORY),
                     (unsigned long)am_p96_bitmap_attr(p96, si.bm, P96BMA_BYTESPERROW),
                     (unsigned long)am_p96_bitmap_attr(p96, si.bm, P96BMA_RGBFORMAT));
        CloseLibrary(p96);
    }
    *o++ = '\n';
    *o = 0;

    fh = Open((STRPTR)tmp, MODE_NEWFILE);
    if (!fh) {
        am_log("snap: cannot write %s\n", tmp);
        return;
    }
    Write(fh, line, (LONG)strlen(line));
    Close(fh);
    DeleteFile((STRPTR)req);
    if (!Rename((STRPTR)tmp, (STRPTR)req)) {
        am_log("snap: cannot rename %s\n", tmp);
        return;
    }
    if (!captured)
        wait_ack(ack);
}
