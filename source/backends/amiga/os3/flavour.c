/* AmigaOS 3.x on the 68k (TARGET=amiga): defaults and drivers (flavour.h). */
#include "os_begin.h"
#include <exec/types.h>
#include <exec/memory.h>
#include <graphics/modeid.h>
#include <devices/timer.h>
#include "os_end.h"
#include "amiga.h"
#include "audio.h"
#include "flavour.h"
#include "upload.h"
#include "version.h"

/* for the AmigaOS Version command ("version xenon"); also printed at start */
const char am_version[] = "$VER: xenon " XE_VERSION " (" XE_VERSION_DATE ")";

static const struct am_audio_drv *const g_audio[] = { &am_audio_paula, &am_audio_ahi, 0 };
static const struct am_upload *const g_upload[] = { &am_upload_planar, &am_upload_rtg,
                                                    &am_upload_chunky, 0 };

const struct am_flavour am_flavour = {
    "AmigaOS",
    DEFAULT_MONITOR_ID | HIRESLACE_KEY,     /* Picasso96 may promote it to a graphics card mode */
    11025,                                  /* opn_lite at 22 050 Hz needs 128-154 % of a 68040/25 */
    MEMF_FAST,
    UNIT_WAITUNTIL,
    g_audio,
    g_upload,
    32768,
    &am_upload_planar_map
};

/* jsr -42(a6) with a0 = bitmap, d0 = attribute */
static ULONG p96_get_bitmap_attr(__reg("a6") struct Library *base, __reg("a0") struct BitMap *bm,
                                 __reg("d0") ULONG attr) = "\tjsr\t-42(a6)";

ULONG am_p96_bitmap_attr(struct Library *p96base, struct BitMap *bm, ULONG attr)
{
    return p96_get_bitmap_attr(p96base, bm, attr);
}

int am_snap_capture(const struct am_snapinfo *si, const char *path)
{
    (void)si; (void)path;
    return 0;                       /* WinUAE's gdbserver takes it (tests/e2e/amiga_snapd.py) */
}

int am_main_stack(int (*body)(void))
{
    return body();
}
