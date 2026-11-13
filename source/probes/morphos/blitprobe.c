/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * blitprobe: which way of getting the engine's planar page onto a depth-8 graphics-card screen
 * works on MorphOS (68k binary). rtgprobe showed BltBitMap from an InitBitMap()ed 8-plane
 * fast-RAM bitmap writing pen 0 on QEMU 11.1.3 ati-vga. Each method draws the same 16-bar
 * pattern (expected pens 16..31, bar width 40 px) into its own 40-row band of the screen and
 * is checked with ReadPixel at 4 bars; the host takes one screenshot at the end.
 *
 * usage: blitprobe [MODE] [SECONDS]   (default 0x8004 = HIRESLACE_KEY, 15 s to look at it)
 *
 *   band 0  BltBitMap, InitBitMap source depth 8 (planes 4 = ones, 5-7 = zeros), as the game
 *   band 1  the same, every plane its own AllocMem (no shared constant planes)
 *   band 2  BltBitMap from AllocBitMap(depth 8, no friend) after copying the planes in
 *   band 3  BltBitMapRastPort, source as band 0
 *   band 4  WriteChunkyPixels from a chunky buffer (graphics.library V40)
 *   band 5  WritePixelArray8 from a chunky buffer (V39)
 *   band 6  BltBitMap, source as band 0 but depth 4 (pens 0..15 expected)
 *   band 7  BltBitMap from AllocBitMap(depth 8, friend = screen bitmap) filled by
 *           WriteChunkyPixels, i.e. a chunky-to-chunky blit
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
#define BAND 40

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;

static UBYTE *pl[8];                /* planes for band 1, also the source of everything */
static UBYTE *chunky;               /* W x H pens 16..31 */

static int bar_pen(int x) { return 16 + ((x / 40) & 15); }

static void check(struct RastPort *rp, int band, const char *what, int base)
{
    int i, ok = 1;
    printf("band %d %-44s", band, what);
    for (i = 0; i < 4; i++) {
        int x = 5 + i * 165, y = band * BAND + 5 + i * 7;
        long got = ReadPixel(rp, x, y), want = (long)(bar_pen(x) - 16 + base);
        printf(" %ld", got);
        if (got != want) ok = 0;
    }
    printf("  %s\n", ok ? "OK" : "WRONG");
}

int main(int argc, char **argv)
{
    ULONG mode = argc > 1 ? strtoul(argv[1], 0, 16) : (DEFAULT_MONITOR_ID | HIRESLACE_KEY);
    int secs = argc > 2 ? atoi(argv[2]) : 15;
    static ULONG pal[2 + 256 * 3];
    struct Screen *scr;
    struct RastPort *rp, tmp;
    struct BitMap src, src1, *abm = 0, *fbm = 0;
    UBYTE *ones = 0, *zeros = 0;
    int i, p, x, y, b;

    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39);
    GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 40);
    if (!IntuitionBase || !GfxBase) {
        printf("need graphics.library 40\n");
        return 20;
    }
    scr = OpenScreenTags(NULL, SA_DisplayID, mode, SA_Width, W, SA_Height, H, SA_Depth, 8,
                         SA_Quiet, TRUE, SA_ShowTitle, FALSE, SA_Type, CUSTOMSCREEN, TAG_DONE);
    if (!scr) {
        printf("OpenScreenTags failed\n");
        return 10;
    }
    rp = &scr->RastPort;
    printf("screen mode 0x%08lx\n", (unsigned long)GetVPModeID(&scr->ViewPort));
    pal[0] = 256UL << 16;
    for (i = 0; i < 256; i++) {
        ULONG v = (i >= 16 && i < 32) ? (ULONG)(i - 16) * 0x11 : (i < 16 ? (ULONG)i * 0x11 : 0x80);
        pal[1 + i * 3] = (i & 1 ? v : 0x40) * 0x01010101UL;
        pal[2 + i * 3] = (i & 2 ? v : 0x40) * 0x01010101UL;
        pal[3 + i * 3] = v * 0x01010101UL;
    }
    pal[1 + 256 * 3] = 0;
    LoadRGB32(&scr->ViewPort, pal);
    SetRast(rp, 0);

    for (p = 0; p < 8; p++)
        pl[p] = AllocMem(BPR * H, MEMF_ANY | MEMF_CLEAR);
    ones = AllocMem(BPR * H, MEMF_ANY);
    zeros = AllocMem(BPR * H, MEMF_ANY | MEMF_CLEAR);
    chunky = AllocMem(W * H, MEMF_ANY);
    for (p = 0; p < 8; p++)
        if (!pl[p]) return 10;
    if (!ones || !zeros || !chunky)
        return 10;
    memset(ones, 0xFF, BPR * H);
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            int c = bar_pen(x);
            chunky[y * W + x] = (UBYTE)c;
            for (p = 0; p < 8; p++)
                if (c >> p & 1)
                    pl[p][y * BPR + x / 8] |= (UBYTE)(0x80 >> (x & 7));
        }

    /* the game's source */
    InitBitMap(&src, 8, W, H);
    for (p = 0; p < 4; p++) src.Planes[p] = pl[p];
    src.Planes[4] = ones;
    for (p = 5; p < 8; p++) src.Planes[p] = zeros;
    /* every plane separate */
    InitBitMap(&src1, 8, W, H);
    for (p = 0; p < 8; p++) src1.Planes[p] = pl[p];

    b = 0;
    BltBitMap(&src, 0, b * BAND, rp->BitMap, 0, b * BAND, W, BAND, 0xC0, 0xFF, NULL);
    check(rp, b, "BltBitMap InitBitMap d8 (as the game)", 16);
    b = 1;
    BltBitMap(&src1, 0, b * BAND, rp->BitMap, 0, b * BAND, W, BAND, 0xC0, 0xFF, NULL);
    check(rp, b, "BltBitMap InitBitMap d8 separate planes", 16);
    b = 2;
    abm = AllocBitMap(W, H, 8, BMF_CLEAR, NULL);
    if (abm) {
        printf("  AllocBitMap(no friend): flags 0x%lx bpr %u\n",
               (unsigned long)GetBitMapAttr(abm, BMA_FLAGS), (unsigned)abm->BytesPerRow);
        if (GetBitMapAttr(abm, BMA_FLAGS) & BMF_STANDARD) {
            for (p = 0; p < 8; p++)
                for (y = 0; y < H; y++)
                    memcpy(abm->Planes[p] + y * abm->BytesPerRow, pl[p] + y * BPR, BPR);
            BltBitMap(abm, 0, b * BAND, rp->BitMap, 0, b * BAND, W, BAND, 0xC0, 0xFF, NULL);
            check(rp, b, "BltBitMap AllocBitMap planar", 16);
        } else
            printf("band 2 skipped: not BMF_STANDARD\n");
    }
    b = 3;
    InitRastPort(&tmp);
    BltBitMapRastPort(&src, 0, b * BAND, rp, 0, b * BAND, W, BAND, 0xC0);
    check(rp, b, "BltBitMapRastPort InitBitMap d8", 16);
    b = 4;
    WriteChunkyPixels(rp, 0, b * BAND, W - 1, b * BAND + BAND - 1, chunky + b * BAND * W, W);
    check(rp, b, "WriteChunkyPixels", 16);
    b = 5;
    {
        struct RastPort trp;
        struct BitMap *tbm = AllocBitMap(W, 1, 8, BMF_CLEAR, NULL);
        trp = *rp;
        trp.Layer = NULL;
        trp.BitMap = tbm;
        tmp = *rp;
        tmp.Layer = NULL;
        if (tbm) {
            WritePixelArray8(&tmp, 0, b * BAND, W - 1, b * BAND + BAND - 1, chunky + b * BAND * W, &trp);
            check(rp, b, "WritePixelArray8", 16);
            FreeBitMap(tbm);
        }
    }
    b = 6;
    src.Depth = 4;
    BltBitMap(&src, 0, b * BAND, rp->BitMap, 0, b * BAND, W, BAND, 0xC0, 0xFF, NULL);
    src.Depth = 8;
    check(rp, b, "BltBitMap InitBitMap d4", 0);
    b = 7;
    fbm = AllocBitMap(W, H, 8, BMF_CLEAR, rp->BitMap);
    if (fbm) {
        struct RastPort frp;
        InitRastPort(&frp);
        frp.BitMap = fbm;
        printf("  AllocBitMap(friend): flags 0x%lx\n", (unsigned long)GetBitMapAttr(fbm, BMA_FLAGS));
        WriteChunkyPixels(&frp, 0, b * BAND, W - 1, b * BAND + BAND - 1, chunky + b * BAND * W, W);
        BltBitMap(fbm, 0, b * BAND, rp->BitMap, 0, b * BAND, W, BAND, 0xC0, 0xFF, NULL);
        check(rp, b, "BltBitMap friend bitmap (chunky->chunky)", 16);
    }
    fflush(stdout);
    Delay(secs * 50);

    if (fbm) FreeBitMap(fbm);
    if (abm) FreeBitMap(abm);
    CloseScreen(scr);
    for (p = 0; p < 8; p++) FreeMem(pl[p], BPR * H);
    FreeMem(ones, BPR * H);
    FreeMem(zeros, BPR * H);
    FreeMem(chunky, W * H);
    printf("done\n");
    CloseLibrary((struct Library *)GfxBase);
    CloseLibrary((struct Library *)IntuitionBase);
    return 0;
}
