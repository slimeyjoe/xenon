/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * rtgprobe: the Amiga backend's graphics-card presentation path, step by step, for MorphOS
 * (68k binary under its 68k emulation; no custom chip access).
 *
 * The game (backends/amiga/video.c) opens a 640x400 depth-8 screen, allocates two
 * ScreenBuffers, flips once both ways, loads the engine colours into pens 16..31 and then
 * BltBitMap()s an 8-plane planar source bitmap (engine planes 0-3 in fast RAM, plane 4 all
 * ones, planes 5-7 all zero) into the displayed buffer. On MorphOS 3.21 the screen stays black.
 * This probe repeats each step on its own, samples the result with ReadPixel() on the bitmap it
 * drew into AND on the screen's RastPort (what Intuition thinks is displayed), prints the
 * result, then waits so the host can take a screenshot.
 *
 * usage: rtgprobe [MODE] [SECONDS]   MODE hex display ID (default 0x8004 = HIRESLACE_KEY),
 *                                     SECONDS pause after each step (default 4)
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <graphics/gfx.h>
#include <graphics/modeid.h>
#include <graphics/rastport.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 640
#define H 400
#define BPR (W / 8)

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;

static struct Screen *scr;
static struct ScreenBuffer *sb[2];
static UBYTE *planes;               /* 4 engine planes + a ones plane + a zero plane */
static int pause_s = 4;
static int step;

static void say(const char *what)
{
    printf("step %d: %s\n", ++step, what);
    fflush(stdout);
}

static void wait_shot(void)
{
    Delay(pause_s * 50);
}

static void describe(const char *name, struct BitMap *bm)
{
    printf("  %s: bm %p depth %ld (attr %lu) %lux%lu flags 0x%lx bpr %u planes[0] %p\n", name,
           (void *)bm, (long)bm->Depth, (unsigned long)GetBitMapAttr(bm, BMA_DEPTH),
           (unsigned long)GetBitMapAttr(bm, BMA_WIDTH), (unsigned long)GetBitMapAttr(bm, BMA_HEIGHT),
           (unsigned long)GetBitMapAttr(bm, BMA_FLAGS), (unsigned)bm->BytesPerRow, (void *)bm->Planes[0]);
}

/* sample 4 points; print the pens */
static void sample(const char *name, struct BitMap *bm, int x0, int y0)
{
    struct RastPort rp;
    int i;
    InitRastPort(&rp);
    rp.BitMap = bm;
    printf("  %s pens:", name);
    for (i = 0; i < 4; i++)
        printf(" (%d,%d)=%ld", x0 + i * 37, y0 + i * 11, (long)ReadPixel(&rp, x0 + i * 37, y0 + i * 11));
    printf("\n");
}

static void fill(struct BitMap *bm, int pen, int x0, int y0, int x1, int y1)
{
    struct RastPort rp;
    InitRastPort(&rp);
    rp.BitMap = bm;
    SetAPen(&rp, pen);
    RectFill(&rp, x0, y0, x1, y1);
}

int main(int argc, char **argv)
{
    ULONG mode = argc > 1 ? strtoul(argv[1], 0, 16) : (DEFAULT_MONITOR_ID | HIRESLACE_KEY);
    static ULONG pal[2 + 256 * 3];
    struct BitMap src;
    struct RastPort srp;
    int i, x, y, p, rc = 0;

    if (argc > 2)
        pause_s = atoi(argv[2]);
    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39);
    GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 39);
    if (!IntuitionBase || !GfxBase)
        return 20;

    printf("rtgprobe: mode 0x%08lx, ModeNotAvailable %lu\n", (unsigned long)mode,
           (unsigned long)ModeNotAvailable(mode));
    scr = OpenScreenTags(NULL, SA_DisplayID, mode, SA_Width, W, SA_Height, H, SA_Depth, 8,
                         SA_Quiet, TRUE, SA_ShowTitle, FALSE, SA_Type, CUSTOMSCREEN, TAG_DONE);
    if (!scr) {
        printf("OpenScreenTags failed\n");
        rc = 10;
        goto out;
    }
    printf("screen mode 0x%08lx\n", (unsigned long)GetVPModeID(&scr->ViewPort));
    describe("screen", scr->RastPort.BitMap);

    /* palette: pen 0 black; 16..31 a ramp of distinct colours; everything else grey */
    pal[0] = 256UL << 16;
    for (i = 0; i < 256; i++) {
        ULONG r = 0x80, g = 0x80, b = 0x80;
        if (i == 0)
            r = g = b = 0;
        else if (i >= 16 && i < 32) {
            r = (i & 1) ? 0xFF : 0x20;
            g = (i & 2) ? 0xFF : 0x20;
            b = (i & 4) ? 0xFF : 0x20;
            if (i & 8) { r >>= 1; g >>= 1; b >>= 1; }
        }
        pal[1 + i * 3] = r * 0x01010101UL;
        pal[2 + i * 3] = g * 0x01010101UL;
        pal[3 + i * 3] = b * 0x01010101UL;
    }
    pal[1 + 256 * 3] = 0;
    LoadRGB32(&scr->ViewPort, pal);

    say("RectFill pen 17 (red) on the screen RastPort, x 0..199, no ScreenBuffers yet");
    SetAPen(&scr->RastPort, 17);
    RectFill(&scr->RastPort, 0, 0, 199, 399);
    sample("screen", scr->RastPort.BitMap, 10, 10);
    wait_shot();

    say("AllocScreenBuffer x2");
    sb[0] = AllocScreenBuffer(scr, NULL, SB_SCREEN_BITMAP);
    sb[1] = AllocScreenBuffer(scr, NULL, 0);
    printf("  sb0 %p sb1 %p\n", (void *)sb[0], (void *)sb[1]);
    if (!sb[0] || !sb[1]) {
        rc = 10;
        goto out;
    }
    describe("sb0", sb[0]->sb_BitMap);
    describe("sb1", sb[1]->sb_BitMap);
    printf("  sb0 bitmap %s the screen bitmap\n", sb[0]->sb_BitMap == scr->RastPort.BitMap ? "IS" : "is NOT");
    sample("sb0", sb[0]->sb_BitMap, 10, 10);
    sample("sb1", sb[1]->sb_BitMap, 10, 10);

    say("RectFill pen 18 (green) into sb0's bitmap, x 200..399 (no flip yet)");
    fill(sb[0]->sb_BitMap, 18, 200, 0, 399, 399);
    sample("sb0", sb[0]->sb_BitMap, 210, 10);
    sample("screen", scr->RastPort.BitMap, 210, 10);
    wait_shot();

    say("dummy flips as the game does: sb1, WaitTOF, sb0, WaitTOF");
    printf("  ChangeScreenBuffer(sb1) = %lu\n", (unsigned long)ChangeScreenBuffer(scr, sb[1]));
    WaitTOF();
    printf("  ChangeScreenBuffer(sb0) = %lu\n", (unsigned long)ChangeScreenBuffer(scr, sb[0]));
    WaitTOF();
    describe("screen now", scr->RastPort.BitMap);
    sample("sb0", sb[0]->sb_BitMap, 10, 10);
    sample("sb0", sb[0]->sb_BitMap, 210, 10);
    wait_shot();

    say("RectFill pen 20 (blue) into sb0's bitmap, x 400..639 (after the flips)");
    fill(sb[0]->sb_BitMap, 20, 400, 0, 639, 199);
    sample("sb0", sb[0]->sb_BitMap, 410, 10);
    wait_shot();

    /* the game's source: planes 0-3 = a pattern of 16 vertical bars (engine colours 0..15),
     * plane 4 all ones, planes 5-7 the zero plane */
    planes = AllocMem(6 * BPR * H, MEMF_ANY | MEMF_CLEAR);
    if (!planes) {
        rc = 10;
        goto out;
    }
    for (y = 0; y < H; y++)
        for (x = 0; x < BPR; x++) {
            int colour = (x * 8 / 40) & 15;         /* bar width 40 px */
            for (p = 0; p < 4; p++)
                planes[p * BPR * H + y * BPR + x] = (colour >> p) & 1 ? 0xFF : 0x00;
            planes[4 * BPR * H + y * BPR + x] = 0xFF;
        }
    InitBitMap(&src, 8, W, H);
    for (p = 0; p < 5; p++)
        src.Planes[p] = planes + p * BPR * H;
    for (p = 5; p < 8; p++)
        src.Planes[p] = planes + 5 * BPR * H;
    describe("src", &src);
    InitRastPort(&srp);
    srp.BitMap = &src;
    printf("  src ReadPixel (0,0)=%ld (45,0)=%ld (600,0)=%ld (expect 16 17 31)\n",
           (long)ReadPixel(&srp, 0, 0), (long)ReadPixel(&srp, 45, 0), (long)ReadPixel(&srp, 600, 0));

    say("BltBitMap 8-plane planar fast-RAM source -> sb0 bitmap, rows 200..399, all columns");
    printf("  BltBitMap = %ld\n", (long)BltBitMap(&src, 0, 200, sb[0]->sb_BitMap, 0, 200, W, 200, 0xC0, 0xFF, NULL));
    sample("sb0", sb[0]->sb_BitMap, 5, 210);
    wait_shot();

    say("BltBitMap the same source -> the screen's RastPort bitmap, rows 0..99, x 0..319");
    printf("  BltBitMap = %ld\n", (long)BltBitMap(&src, 0, 0, scr->RastPort.BitMap, 0, 0, 320, 100, 0xC0, 0xFF, NULL));
    sample("screen", scr->RastPort.BitMap, 5, 10);
    wait_shot();

    say("WritePixel-free check: BltBitMap 4-plane view of the source -> sb0, rows 100..199");
    src.Depth = 4;
    printf("  BltBitMap = %ld\n", (long)BltBitMap(&src, 0, 100, sb[0]->sb_BitMap, 0, 100, W, 100, 0xC0, 0xFF, NULL));
    src.Depth = 8;
    sample("sb0", sb[0]->sb_BitMap, 5, 110);
    wait_shot();

    say("draw into sb1 (pen 19 yellow everywhere + bars at the bottom) and flip to it");
    fill(sb[1]->sb_BitMap, 19, 0, 0, W - 1, H - 1);
    BltBitMap(&src, 0, 300, sb[1]->sb_BitMap, 0, 300, W, 100, 0xC0, 0xFF, NULL);
    printf("  ChangeScreenBuffer(sb1) = %lu\n", (unsigned long)ChangeScreenBuffer(scr, sb[1]));
    WaitTOF();
    sample("sb1", sb[1]->sb_BitMap, 5, 10);
    sample("sb1", sb[1]->sb_BitMap, 5, 310);
    wait_shot();

    say("flip back to sb0 and close");
    ChangeScreenBuffer(scr, sb[0]);
    WaitTOF();
    wait_shot();
out:
    if (scr) {
        if (sb[1]) FreeScreenBuffer(scr, sb[1]);
        if (sb[0]) FreeScreenBuffer(scr, sb[0]);
        CloseScreen(scr);
    }
    if (planes)
        FreeMem(planes, 6 * BPR * H);
    printf("done\n");
    CloseLibrary((struct Library *)GfxBase);
    CloseLibrary((struct Library *)IntuitionBase);
    return rc;
}
