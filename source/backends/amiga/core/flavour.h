/* What sets one member of the Amiga family apart from the others (README.md "Layout"):
 * defaults, the drivers it has, small machine-specific helpers. Each flavour directory
 * (os3/, warpos/, aros/) has a flavour.c that defines all of this, and nothing else in the
 * backend needs to know which flavour it is. */
#ifndef XE_AMIGA_FLAVOUR_H
#define XE_AMIGA_FLAVOUR_H

#include "os_begin.h"
#include <exec/types.h>
#include <exec/libraries.h>
#include <graphics/gfx.h>
#include "os_end.h"

struct am_audio_drv;
struct am_upload;

struct am_flavour {
    const char *name;                       /* for the log: "AmigaOS", "WarpOS", ... */
    ULONG mode_id;                          /* the default display mode (MODE=) */
    LONG rate;                              /* the default output rate (RATE=) */
    ULONG preload_memf;                     /* where PRELOAD puts the pack: MEMF_FAST or MEMF_ANY */
    /* the frame clock's timer.device unit: UNIT_WAITUNTIL (absolute deadlines), or
     * UNIT_MICROHZ where WAITUNTIL is coarse (AROS: 50 Hz steps): the same absolute
     * deadlines, waited for as the time remaining */
    ULONG timer_unit;
    /* the audio output drivers, NULL-terminated; the first is the default (AUDIO=) */
    const struct am_audio_drv *const *audio;
    /* the video uploads (upload.h), NULL-terminated: at most one planar, then the ones for
     * graphics card bitmaps; the first of those is the default (UPLOAD=) */
    const struct am_upload *const *upload;
    ULONG audio_stack;                      /* the audio process's stack, bytes */
    /* the planar upload through a row map (ASPECT on a native screen, upload.h), or NULL where
     * there is no planar display */
    const struct am_upload *planar_map;
};

extern const struct am_flavour am_flavour;

/* "$VER: xenon <version> (<date>)[ <flavour>]", for the Version command; also logged */
extern const char am_version[];

/* Picasso96API.library p96GetBitMapAttr (LVO -42; snapshots only, snap.c), or 0 where the
 * flavour has no way to call it. */
ULONG am_p96_bitmap_attr(struct Library *p96base, struct BitMap *bm, ULONG attr);

/* Snapshots (snap.c): where the flavour can read the shown page back itself (AROS: true
 * colour bitmaps, ReadPixelArray), it writes it as a 640x400 PPM to `path` and returns 1; the
 * host then needs no access to the machine and the guest does not wait for it. 0: the host
 * takes the snapshot (WinUAE, tests/e2e/amiga_snapd.py). */
struct am_snapinfo;
int am_snap_capture(const struct am_snapinfo *si, const char *path);

/* Runs the program's body (main.c) on a stack the flavour is sure of: AROS swaps to its own
 * 256 KB when the task's is smaller (an icon's tool stack, hosted AROS's X11 driver crashed
 * with 64 KB); elsewhere it just calls it. */
int am_main_stack(int (*body)(void));

#endif
