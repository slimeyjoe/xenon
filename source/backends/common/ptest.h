/* Synthetic presentation test (plan §10 stage A), shared by the null and Amiga backends.
 *
 * pt_run drives a fixed sequence of video_present calls through the paths a backend has:
 * the first upload, full and dirty uploads (odd byte columns, the screen edges), palette-only
 * changes, graphics off and on, page flips (the first one, flips of a page drawn while hidden,
 * several in a row), scroll changes, dirty redraws while scrolled with an unchanged scroll line
 * (also right after a flip), colour 0 non-black and an all-white palette (the overscan border
 * must stay black), and a pseudo-random soak. After selected calls it asks the backend for a
 * snapshot of what must now be on the screen. The pages, dirty rectangles and flags follow
 * the engine's contract (hal/xe_hal.h): a dirty rectangle covers every change of the displayed
 * page since its previous present, in page coordinates; a page drawn while hidden is uploaded
 * because of page_switched; a scroll change is not reported as dirty. The null backend prints
 * the snapshots as the reference (tests/e2e/amiga.py), the Amiga backend shows them to WinUAE.
 *
 * Everything is deterministic and the same C on both sides; the frame hash of each snapshot
 * proves the sequence ran identically on the 68k build. */
#ifndef XE_PTEST_H
#define XE_PTEST_H

#include <stdint.h>
#include "xe_hal.h"

struct pt_io {
    void (*present)(const struct xe_planes *page, const struct xe_rect *dirty, int page_switched,
                    int scroll_line, const uint8_t rgb4[48], int graphics_on);
    /* the screen must now show `page` rotated by `scroll` with the palette (black when off) */
    void (*snap)(int index, const char *name, const struct xe_planes *page, int scroll,
                 const uint8_t rgb4[48], int on, uint32_t hash);
};

/* Returns the number of snapshots taken, or -1 when out of memory. */
int pt_run(const struct pt_io *io);

/* FNV-1a over the displayed image (rows rotated by scroll, all four planes), the palette and
 * the graphics-on flag. Used by the present test and by the game snapshots (SNAPAT). */
uint32_t xe_frame_hash32(const struct xe_planes *page, int scroll, const uint8_t rgb4[48], int on);

#endif
