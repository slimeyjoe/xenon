/* WarpOS on a PowerPC card (TARGET=warpos): defaults and drivers (flavour.h). The same as the
 * 68k build's (os3/flavour.c) except where the PPC changes the picture. */
#include "os_begin.h"
#include <exec/types.h>
#include <exec/memory.h>
#include <graphics/modeid.h>
#include <devices/timer.h>
#include "os_end.h"
#include <string.h>
#include "amiga.h"
#include "audio.h"
#include "flavour.h"
#include "upload.h"
#include "version.h"
#include "wos.h"

/* for the AmigaOS Version command ("version xenon"); also printed at start */
const char am_version[] = "$VER: xenon " XE_VERSION " (" XE_VERSION_DATE ") WarpOS";

static const struct am_audio_drv *const g_audio[] = { &am_audio_paula, &am_audio_ahi, 0 };
static const struct am_upload *const g_upload[] = { &am_upload_planar, &am_upload_rtg,
                                                    &am_upload_chunky, 0 };

const struct am_flavour am_flavour = {
    "WarpOS",
    DEFAULT_MONITOR_ID | HIRESLACE_KEY,
    11025,
    MEMF_FAST,
    UNIT_WAITUNTIL,
    g_audio,
    g_upload,
    32768,
    &am_upload_planar_map
};

/* Picasso96API.library p96GetBitMapAttr (LVO -42, a0 = bitmap, d0 = attribute), on the 68k */
ULONG am_p96_bitmap_attr(struct Library *p96base, struct BitMap *bm, ULONG attr)
{
    struct am_regs68k r;
    memset(&r, 0, sizeof r);
    r.a[0] = (ULONG)bm;
    r.d[0] = attr;
    return am_call68k_lib(p96base, -42, &r);
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
