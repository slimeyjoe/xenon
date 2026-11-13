/* AROS probe for the Xenon port (README.md): the facts the backend's AROS flavour relies on.
 *
 *   arosprobe [STARTUP] [TIMER] [SCREEN] [AHI [RATE=hz] [SECONDS=n]]
 *
 * Built with the AROS cross compiler (Makefile). Each section prints "name: result" lines.
 *   startup  ReadArgs into IPTR, the Workbench message symbol, own library bases next to the
 *            startup's auto-opened ones, the stack, memory types
 *   timer    ReadEClock rate; 200 frames on UNIT_WAITUNTIL absolute deadlines at 56.42 Hz:
 *            how late each wait returns
 *   screen   BestModeID for 640x400x8 and 640x480x8; a 640x400 depth-8 screen with two
 *            ScreenBuffers: real bitmap depth, BMF_STANDARD, flips; WriteChunkyPixels time
 *            for a full page; whether LoadRGB32 recolours pixels already drawn (read back
 *            with ReadPixelArray)
 *   ahi      ahi.device unit 0: CMD_WRITE double buffering, 16-bit mono, SECONDS (2) of a
 *            440 Hz tone */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <devices/timer.h>
#include <devices/ahi.h>
#include <graphics/gfx.h>
#include <graphics/modeid.h>
#include <graphics/displayinfo.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <workbench/startup.h>
#include <cybergraphx/cybergraphics.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/timer.h>
#include <proto/cybergraphics.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

extern struct WBStartup *WBenchMsg;

struct Device *TimerBase;
static struct timerequest *g_treq;
static struct MsgPort *g_tport;
static ULONG g_efreq;

static unsigned long long eclock(void)
{
    struct EClockVal ev;
    ReadEClock(&ev);
    return (unsigned long long)ev.ev_hi << 32 | ev.ev_lo;
}

static unsigned long us_since(unsigned long long e0)
{
    return (unsigned long)((eclock() - e0) * 1000000ULL / g_efreq);
}

static int timer_open(void)
{
    g_tport = CreateMsgPort();
    g_treq = (struct timerequest *)CreateIORequest(g_tport, sizeof *g_treq);
    if (!g_treq || OpenDevice(TIMERNAME, UNIT_WAITUNTIL, (struct IORequest *)g_treq, 0) != 0) {
        printf("timer: cannot open timer.device UNIT_WAITUNTIL\n");
        return -1;
    }
    TimerBase = g_treq->tr_node.io_Device;
    {
        struct EClockVal ev;
        g_efreq = ReadEClock(&ev);
    }
    return 0;
}

static void timer_close(void)
{
    if (TimerBase)
        CloseDevice((struct IORequest *)g_treq);
    if (g_treq) DeleteIORequest((struct IORequest *)g_treq);
    if (g_tport) DeleteMsgPort(g_tport);
}

/* ---- startup ---- */

static void probe_startup(IPTR *args)
{
    struct Task *t = FindTask(NULL);
    printf("startup: WBenchMsg %p, sizeof(IPTR) %d, sizeof(ULONG) %d, sizeof(void *) %d\n",
           (void *)WBenchMsg, (int)sizeof(IPTR), (int)sizeof(ULONG), (int)sizeof(void *));
    printf("startup: RATE arg %s\n", args[4] ? "given" : "absent");
    printf("startup: stack %lu bytes (lower %p upper %p, sp near %p)\n",
           (unsigned long)((UBYTE *)t->tc_SPUpper - (UBYTE *)t->tc_SPLower), t->tc_SPLower,
           t->tc_SPUpper, (void *)&t);
    printf("startup: AvailMem chip %lu fast %lu any %lu largest-any %lu\n",
           (unsigned long)AvailMem(MEMF_CHIP), (unsigned long)AvailMem(MEMF_FAST),
           (unsigned long)AvailMem(MEMF_ANY), (unsigned long)AvailMem(MEMF_ANY | MEMF_LARGEST));
    printf("startup: IntuitionBase %p (v%d) GfxBase %p (v%d) CyberGfxBase %p\n",
           (void *)IntuitionBase, IntuitionBase ? ((struct Library *)IntuitionBase)->lib_Version : 0,
           (void *)GfxBase, GfxBase ? ((struct Library *)GfxBase)->lib_Version : 0,
           (void *)CyberGfxBase);
}

/* ---- timer ---- */

#define TICK_US 17724
static void probe_timer(void)
{
    struct timeval dl, now;
    unsigned long late[200], mx = 0, sum = 0, over2ms = 0;
    unsigned long long e0;
    int i;
    printf("timer: E-clock %lu Hz\n", (unsigned long)g_efreq);
    GetSysTime(&dl);
    e0 = eclock();
    for (i = 0; i < 200; i++) {
        dl.tv_micro += TICK_US;
        while (dl.tv_micro >= 1000000) {
            dl.tv_micro -= 1000000;
            dl.tv_secs++;
        }
        g_treq->tr_node.io_Command = TR_ADDREQUEST;
        g_treq->tr_time.tv_secs = dl.tv_secs;    /* a struct timeval32 on AROS x86_64 */
        g_treq->tr_time.tv_micro = dl.tv_micro;
        DoIO((struct IORequest *)g_treq);
        GetSysTime(&now);
        late[i] = (unsigned long)((long)(now.tv_secs - dl.tv_secs) * 1000000L +
                                  ((long)now.tv_micro - (long)dl.tv_micro));
        if ((long)late[i] < 0)
            late[i] = 0;
        sum += late[i];
        if (late[i] > mx) mx = late[i];
        if (late[i] > 2000) over2ms++;
    }
    printf("timer: 200 waits in %lu us (want %d), late avg %lu us max %lu us, %lu over 2 ms\n",
           us_since(e0), 200 * TICK_US, sum / 200, mx, over2ms);
}

/* the same 200 frames on other units: MICROHZ with the remaining time as a relative wait,
 * WAITECLOCK with absolute E-clock deadlines; lateness measured on the E-clock */
static void probe_timer_units(void)
{
    static const ULONG units[2] = { UNIT_MICROHZ, UNIT_WAITECLOCK };
    static const char *const names[2] = { "MICROHZ", "WAITECLOCK" };
    int u, i;
    for (u = 0; u < 2; u++) {
        struct timerequest *r = (struct timerequest *)CreateIORequest(g_tport, sizeof *r);
        unsigned long long tick = (unsigned long long)g_efreq * TICK_US / 1000000ULL, dl, now, e0;
        unsigned long mx = 0, sum = 0, over2 = 0;
        if (!r || OpenDevice((STRPTR)TIMERNAME, units[u], (struct IORequest *)r, 0) != 0) {
            printf("timer: cannot open unit %s\n", names[u]);
            if (r) DeleteIORequest((struct IORequest *)r);
            continue;
        }
        e0 = dl = eclock();
        for (i = 0; i < 200; i++) {
            unsigned long late;
            dl += tick;
            now = eclock();
            if (dl > now) {
                r->tr_node.io_Command = TR_ADDREQUEST;
                if (units[u] == UNIT_WAITECLOCK) {
                    r->tr_time.tv_secs = (ULONG)(dl >> 32);
                    r->tr_time.tv_micro = (ULONG)dl;
                } else {
                    unsigned long long us = (dl - now) * 1000000ULL / g_efreq;
                    r->tr_time.tv_secs = (ULONG)(us / 1000000);
                    r->tr_time.tv_micro = (ULONG)(us % 1000000);
                }
                DoIO((struct IORequest *)r);
            }
            now = eclock();
            late = now > dl ? (unsigned long)((now - dl) * 1000000ULL / g_efreq) : 0;
            sum += late;
            if (late > mx) mx = late;
            if (late > 2000) over2++;
        }
        printf("timer: %s: 200 waits in %lu us (want %d), late avg %lu us max %lu us, %lu over 2 ms\n",
               names[u], us_since(e0), 200 * TICK_US, sum / 200, mx, over2);
        CloseDevice((struct IORequest *)r);
        DeleteIORequest((struct IORequest *)r);
    }
}

/* ---- screen ---- */

static void mode_info(const char *what, ULONG id)
{
    struct DimensionInfo di;
    struct NameInfo ni;
    if (id == INVALID_ID) {
        printf("screen: %s: INVALID_ID\n", what);
        return;
    }
    memset(&ni, 0, sizeof ni);
    GetDisplayInfoData(NULL, (UBYTE *)&ni, sizeof ni, DTAG_NAME, id);
    if (GetDisplayInfoData(NULL, (UBYTE *)&di, sizeof di, DTAG_DIMS, id))
        printf("screen: %s: 0x%08lx \"%s\" nominal %dx%d maxdepth %d\n", what, (unsigned long)id,
               ni.Name, di.Nominal.MaxX - di.Nominal.MinX + 1, di.Nominal.MaxY - di.Nominal.MinY + 1,
               di.MaxDepth);
    else
        printf("screen: %s: 0x%08lx (no dimension info)\n", what, (unsigned long)id);
}

static void read_rgb(struct RastPort *rp, int x, int y, UBYTE out[3])
{
    UBYTE px[3 * 4];
    memset(px, 0, sizeof px);
    if (CyberGfxBase)
        ReadPixelArray(px, 0, 0, 3 * 4, rp, x, y, 1, 1, RECTFMT_RGB);
    memcpy(out, px, 3);
}

static void probe_screen(void)
{
    ULONG id400, id480, id;
    struct Screen *scr;
    struct ScreenBuffer *sb[2];
    struct RastPort rp;
    UBYTE *chunky;
    ULONG pal[2 + 256 * 3];
    UBYTE c0[3], c1[3];
    unsigned long long e0;
    int i;

    id400 = BestModeID(BIDTAG_NominalWidth, 640, BIDTAG_NominalHeight, 400, BIDTAG_Depth, 8, TAG_DONE);
    id480 = BestModeID(BIDTAG_NominalWidth, 640, BIDTAG_NominalHeight, 480, BIDTAG_Depth, 8, TAG_DONE);
    mode_info("BestModeID 640x400x8", id400);
    mode_info("BestModeID 640x480x8", id480);
    id = id400 != INVALID_ID ? id400 : id480;
    scr = OpenScreenTags(NULL, SA_DisplayID, id, SA_Width, 640, SA_Height, 400, SA_Depth, 8,
                         SA_Quiet, TRUE, SA_ShowTitle, FALSE, SA_Type, CUSTOMSCREEN,
                         SA_Title, (IPTR)"arosprobe", TAG_DONE);
    if (!scr) {
        printf("screen: OpenScreenTags 640x400x8 failed\n");
        return;
    }
    printf("screen: opened %dx%d, mode 0x%08lx, bitmap depth %lu, flags 0x%lx (standard %d), "
           "width %lu height %lu\n", scr->Width, scr->Height,
           (unsigned long)GetVPModeID(&scr->ViewPort),
           (unsigned long)GetBitMapAttr(scr->RastPort.BitMap, BMA_DEPTH),
           (unsigned long)GetBitMapAttr(scr->RastPort.BitMap, BMA_FLAGS),
           (GetBitMapAttr(scr->RastPort.BitMap, BMA_FLAGS) & BMF_STANDARD) != 0,
           (unsigned long)GetBitMapAttr(scr->RastPort.BitMap, BMA_WIDTH),
           (unsigned long)GetBitMapAttr(scr->RastPort.BitMap, BMA_HEIGHT));
    if (CyberGfxBase)
        printf("screen: cgx depth %lu pixfmt %lu bpp %lu iscgx %lu\n",
               (unsigned long)GetCyberMapAttr(scr->RastPort.BitMap, CYBRMATTR_DEPTH),
               (unsigned long)GetCyberMapAttr(scr->RastPort.BitMap, CYBRMATTR_PIXFMT),
               (unsigned long)GetCyberMapAttr(scr->RastPort.BitMap, CYBRMATTR_BPPIX),
               (unsigned long)GetCyberMapAttr(scr->RastPort.BitMap, CYBRMATTR_ISCYBERGFX));
    sb[0] = AllocScreenBuffer(scr, NULL, SB_SCREEN_BITMAP);
    sb[1] = AllocScreenBuffer(scr, NULL, 0);
    printf("screen: AllocScreenBuffer %p %p\n", (void *)sb[0], (void *)sb[1]);

    /* palette: pens 16..31 a grey ramp, pen 0 black */
    memset(pal, 0, sizeof pal);
    pal[0] = 256UL << 16;
    for (i = 16; i < 32; i++) {
        ULONG v = (ULONG)(i - 16) * 0x11111111UL;
        pal[1 + i * 3] = v; pal[2 + i * 3] = v; pal[3 + i * 3] = v;
    }
    LoadRGB32(&scr->ViewPort, pal);

    chunky = AllocMem(640 * 400, MEMF_ANY);
    if (!chunky)
        goto out;
    for (i = 0; i < 640 * 400; i++)
        chunky[i] = (UBYTE)(16 + ((i % 640) / 40));
    InitRastPort(&rp);
    rp.BitMap = sb[0] ? sb[0]->sb_BitMap : scr->RastPort.BitMap;
    e0 = eclock();
    for (i = 0; i < 20; i++)
        WriteChunkyPixels(&rp, 0, 0, 639, 399, chunky, 640);
    printf("screen: WriteChunkyPixels 640x400: %lu us each (20 runs)\n", us_since(e0) / 20);
    e0 = eclock();
    for (i = 0; i < 20; i++)
        WritePixelArray8(&rp, 0, 0, 639, 399, chunky, NULL);
    printf("screen: WritePixelArray8 640x400 (no temp rp): %lu us each\n", us_since(e0) / 20);
    if (CyberGfxBase) {
        e0 = eclock();
        for (i = 0; i < 20; i++)
            WritePixelArray(chunky, 0, 0, 640, &rp, 0, 0, 640, 400, RECTFMT_LUT8);
        printf("screen: cgx WritePixelArray LUT8 640x400: %lu us each\n", us_since(e0) / 20);
    }
    read_rgb(&rp, 600, 10, c0);         /* pen 31: white */
    pal[0] = (1UL << 16) | 31;
    pal[1] = 0xFFFFFFFF; pal[2] = 0; pal[3] = 0; pal[4] = 0;
    LoadRGB32(&scr->ViewPort, pal);     /* pen 31 -> red */
    WaitTOF();
    read_rgb(&rp, 600, 10, c1);
    printf("screen: pen 31 read back %02x%02x%02x before, %02x%02x%02x after LoadRGB32(red): %s\n",
           c0[0], c0[1], c0[2], c1[0], c1[1], c1[2],
           memcmp(c0, c1, 3) ? "RECOLOURED" : "NOT recoloured (true colour underneath)");
    WriteChunkyPixels(&rp, 560, 0, 639, 399, chunky + 560, 640);
    read_rgb(&rp, 600, 10, c1);
    printf("screen: pen 31 after redraw: %02x%02x%02x\n", c1[0], c1[1], c1[2]);

    if (sb[0] && sb[1]) {
        int ok = 0;
        rp.BitMap = sb[1]->sb_BitMap;
        WriteChunkyPixels(&rp, 0, 0, 639, 399, chunky, 640);
        e0 = eclock();
        for (i = 0; i < 10; i++)
            ok += ChangeScreenBuffer(scr, sb[(i + 1) & 1]) != 0;
        printf("screen: 10 ChangeScreenBuffer: %d ok, %lu us each\n", ok, us_since(e0) / 10);
        if (!(i & 1))
            ChangeScreenBuffer(scr, sb[0]);
    }
    e0 = eclock();
    for (i = 0; i < 10; i++)
        WaitTOF();
    printf("screen: WaitTOF %lu us each\n", us_since(e0) / 10);
    Delay(100);                         /* 2 s for a screenshot */
    FreeMem(chunky, 640 * 400);
out:
    WaitTOF();
    if (sb[1]) FreeScreenBuffer(scr, sb[1]);
    if (sb[0]) FreeScreenBuffer(scr, sb[0]);
    CloseScreen(scr);
}

/* ---- ahi ---- */

static void probe_ahi(LONG rate, LONG secs)
{
    struct MsgPort *port = CreateMsgPort();
    struct AHIRequest *req[2] = { 0, 0 }, *prev = NULL;
    int16_t *buf[2] = { 0, 0 };
    int frames = rate / 10, i, n, opened = 0;
    double ph = 0;
    unsigned long long e0;
    if (!port)
        return;
    req[0] = (struct AHIRequest *)CreateIORequest(port, sizeof(struct AHIRequest));
    if (!req[0])
        goto out;
    req[0]->ahir_Version = 4;
    if (OpenDevice(AHINAME, 0, (struct IORequest *)req[0], 0) != 0) {
        printf("ahi: cannot open ahi.device unit 0 (error %d)\n", req[0]->ahir_Std.io_Error);
        goto out;
    }
    opened = 1;
    printf("ahi: ahi.device v%d.%d\n", req[0]->ahir_Std.io_Device->dd_Library.lib_Version,
           req[0]->ahir_Std.io_Device->dd_Library.lib_Revision);
    req[1] = AllocMem(sizeof(struct AHIRequest), MEMF_PUBLIC);
    buf[0] = AllocMem(frames * 2, MEMF_PUBLIC);
    buf[1] = AllocMem(frames * 2, MEMF_PUBLIC);
    if (!req[1] || !buf[0] || !buf[1])
        goto out;
    memcpy(req[1], req[0], sizeof(struct AHIRequest));
    e0 = eclock();
    for (n = 0; n < 10 * secs; n++) {
        int k = n & 1;
        if (n >= 2) {
            WaitIO((struct IORequest *)req[k]);
            if (n == 2)
                printf("ahi: first block back after %lu us\n", us_since(e0));
        }
        for (i = 0; i < frames; i++) {
            buf[k][i] = (int16_t)(8000 * sin(ph));
            ph += 2 * 3.14159265 * 440.0 / rate;
        }
        req[k]->ahir_Std.io_Command = CMD_WRITE;
        req[k]->ahir_Std.io_Data = buf[k];
        req[k]->ahir_Std.io_Length = frames * 2;
        req[k]->ahir_Std.io_Offset = 0;
        req[k]->ahir_Type = AHIST_M16S;
        req[k]->ahir_Frequency = rate;
        req[k]->ahir_Volume = 0x10000;
        req[k]->ahir_Position = 0x8000;
        req[k]->ahir_Link = prev;
        SendIO((struct IORequest *)req[k]);
        prev = req[k];
    }
    WaitIO((struct IORequest *)req[0]);
    WaitIO((struct IORequest *)req[1]);
    printf("ahi: %ld blocks of %d frames at %ld Hz played in %lu us (want %lu)\n", (long)(10 * secs),
           frames, (long)rate, us_since(e0), (unsigned long)(10 * secs) * frames * 1000000UL / rate);
out:
    if (buf[0]) FreeMem(buf[0], frames * 2);
    if (buf[1]) FreeMem(buf[1], frames * 2);
    if (req[1]) FreeMem(req[1], sizeof(struct AHIRequest));
    if (opened) CloseDevice((struct IORequest *)req[0]);
    if (req[0]) DeleteIORequest((struct IORequest *)req[0]);
    DeleteMsgPort(port);
}

int main(void)
{
    IPTR args[6];
    struct RDArgs *rda;
    memset(args, 0, sizeof args);
    rda = ReadArgs("SCREEN/S,AHI/S,TIMER/S,STARTUP/S,RATE/N/K,SECONDS/N/K", args, NULL);
    if (!rda) {
        PrintFault(IoErr(), "arosprobe");
        return 10;
    }
    printf("arosprobe\n");
    if (timer_open() == 0) {
        if (args[3]) probe_startup(args);
        if (args[2]) {
            probe_timer();
            probe_timer_units();
        }
        if (args[0]) probe_screen();
        if (args[1]) probe_ahi(args[4] ? *(LONG *)args[4] : 44100, args[5] ? *(LONG *)args[5] : 2);
    }
    timer_close();
    FreeArgs(rda);
    printf("arosprobe: end\n");
    return 0;
}
