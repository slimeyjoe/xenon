/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * sbprobe: Intuition ScreenBuffer double buffering + BltBitMap from a
 * fast-RAM planar bitmap, on a native (AGA) screen and on a Picasso96
 * (uaegfx) screen.  Prints everything it learns to stdout.
 *
 * usage: sbprobe [MODE] [TAG] [HEIGHT]
 *   MODE   hex display ID (default 0x8004 = HIRESLACE_KEY), or "uaegfx"
 *          to look the uaegfx 640x480x8 mode up in the display database.
 *   TAG    name used for the screenshot handshake files
 *          DH0:shot_<TAG>_<n>.req / .ack (default "shot").
 *   HEIGHT screen height (default 400; 480 is used for uaegfx).
 *
 * Steps (each followed by a screenshot request):
 *   (a) BltBitMap(fast planar -> hidden buffer), ChangeScreenBuffer
 *   (b) CPU copy of the planes into the hidden buffer (only when it is
 *       BMF_STANDARD; else BltBitMap), ChangeScreenBuffer
 *   (c) BltBitMap(CHIP planar copy -> hidden buffer), ChangeScreenBuffer
 *   then 20 flips without and 20 with a SafeMessage port, timed in
 *   E-clock ticks and VERTB interrupts.
 * After every present the displayed bitmap is sampled with ReadPixel()
 * and compared to the source ("verify"); standard bitmaps are also
 * compared plane by plane.
 *
 * Screenshot handshake: before each "look at me" moment the probe creates
 * DH0:shot_<TAG>_<n>.req and then waits (up to ~40 s) for the host to
 * create DH0:shot_<TAG>_<n>.ack after taking the screenshot via the
 * WinUAE gdbserver.  Without a host watcher it just times out.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <exec/ports.h>
#include <exec/interrupts.h>
#include <exec/io.h>
#include <hardware/intbits.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <graphics/rastport.h>
#include <graphics/displayinfo.h>
#include <graphics/modeid.h>
#include <graphics/videocontrol.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <intuition/intuitionbase.h>
#include <utility/tagitem.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/timer.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Device *TimerBase;

extern void vbserver(void);		/* vbserver.s: addq.l #1,(a1) */
static volatile ULONG vbcount;
static struct Interrupt vbint;
static struct timerequest timereq;
static ULONG efreq = 709379;

#define W 640
#define BPR (W / 8)
#define DEPTH 4

static int height = 400;
static const char *tag = "shot";
static int shotno;

static void
out(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	fflush(stdout);
}

/* --- screenshot handshake with the host ------------------------------- */

static void
shot(const char *what)
{
	char req[64], ack[64];
	BPTR fh, lock;
	int i;

	shotno++;
	sprintf(req, "DH0:shot_%s_%d.req", tag, shotno);
	sprintf(ack, "DH0:shot_%s_%d.ack", tag, shotno);
	out("SHOT %d (%s): requesting %s\n", shotno, what, req);
	fh = Open(req, MODE_NEWFILE);
	if (fh)
		Close(fh);
	for (i = 0; i < 160; i++) {	/* 160 * 0.25 s = 40 s */
		Delay(12);
		lock = Lock(ack, ACCESS_READ);
		if (lock) {
			UnLock(lock);
			out("SHOT %d: ack after %d polls\n", shotno, i + 1);
			return;
		}
	}
	out("SHOT %d: no ack (timeout)\n", shotno);
}

/* --- timing ------------------------------------------------------------- */

static void
mark(ULONG *e, ULONG *v)
{
	struct EClockVal ev;

	if (TimerBase) {
		ReadEClock(&ev);
		*e = ev.ev_lo;
	} else {
		*e = 0;
	}
	*v = vbcount;
}

static ULONG
ems(ULONG e0, ULONG e1)
{
	return (e1 - e0) / (efreq / 1000);
}

/* --- display database ------------------------------------------------- */

static ULONG
find_uaegfx_mode(void)
{
	ULONG id = INVALID_ID, best = INVALID_ID;
	struct NameInfo ni;
	struct DimensionInfo di;
	struct DisplayInfo dsi;
	int n = 0, nf = 0;

	/* Walk the whole display database and print it. */
	while ((id = NextDisplayInfo(id)) != INVALID_ID) {
		int foreign, w, h;
		n++;
		memset(&ni, 0, sizeof(ni));
		memset(&di, 0, sizeof(di));
		memset(&dsi, 0, sizeof(dsi));
		if (!GetDisplayInfoData(NULL, (UBYTE *)&ni, sizeof(ni),
		    DTAG_NAME, id))
			strcpy((char *)ni.Name, "(no name)");
		GetDisplayInfoData(NULL, (UBYTE *)&di, sizeof(di), DTAG_DIMS, id);
		GetDisplayInfoData(NULL, (UBYTE *)&dsi, sizeof(dsi), DTAG_DISP, id);
		/* P96 does not set DIPF_IS_FOREIGN; its uaegfx modes are named
		 * "UAE: WxH depth" (no Picasso96Settings file) and have
		 * 0x5xxxxxxx mode IDs. */
		foreign = (dsi.PropertyFlags & DIPF_IS_FOREIGN) != 0 ||
		    strncmp((char *)ni.Name, "UAE", 3) == 0 ||
		    strncmp((char *)ni.Name, "uaegfx", 6) == 0 ||
		    (id & 0xf0000000) == 0x50000000;
		w = di.Nominal.MaxX - di.Nominal.MinX + 1;
		h = di.Nominal.MaxY - di.Nominal.MinY + 1;
		out("  mode 0x%08lx \"%s\" nominal %dx%d maxdepth %d "
		    "propflags 0x%08lx%s\n", id, ni.Name, w, h, (int)di.MaxDepth,
		    dsi.PropertyFlags, foreign ? " RTG" : "");
		if (!foreign)
			continue;
		nf++;
		if (best == INVALID_ID && w == 640 && h == 480 && di.MaxDepth == 8)
			best = id;
	}
	out("display database: %d modes, %d RTG; chosen 640x480x8: "
	    "0x%08lx\n", n, nf, best);
	if (best == INVALID_ID) {
		/* WinUAE's uaegfx log names 0x50031000 as 640x480: try it */
		if (GetDisplayInfoData(NULL, (UBYTE *)&ni, sizeof(ni), DTAG_NAME,
		    0x50031000)) {
			out("fallback: 0x50031000 is in the database as \"%s\"\n",
			    ni.Name);
			best = 0x50031000;
		}
	}
	if (best != INVALID_ID) {
		ULONG bm = BestModeID(BIDTAG_NominalWidth, 640,
		    BIDTAG_NominalHeight, 480, BIDTAG_Depth, 8,
		    BIDTAG_MonitorID, best & MONITOR_ID_MASK, TAG_DONE);
		out("BestModeID(640x480x8, MonitorID 0x%08lx) = 0x%08lx\n",
		    best & MONITOR_ID_MASK, bm);
		bm = BestModeID(BIDTAG_NominalWidth, 640,
		    BIDTAG_NominalHeight, 480, BIDTAG_Depth, 8,
		    BIDTAG_DIPFMustHave, DIPF_IS_FOREIGN, TAG_DONE);
		out("BestModeID(640x480x8, DIPFMustHave FOREIGN) = 0x%08lx\n", bm);
	}
	return best;
}

/* --- bitmap reporting ------------------------------------------------- */

static void
report_bm(const char *name, struct BitMap *bm)
{
	ULONG flags, depth, w, h, t;
	int i;

	if (!bm) {
		out("%s: NULL bitmap\n", name);
		return;
	}
	flags = GetBitMapAttr(bm, BMA_FLAGS);
	depth = GetBitMapAttr(bm, BMA_DEPTH);
	w = GetBitMapAttr(bm, BMA_WIDTH);
	h = GetBitMapAttr(bm, BMA_HEIGHT);
	out("%s: bm=%p BMA_FLAGS=0x%lx%s%s%s BMA_DEPTH=%lu BMA_WIDTH=%lu "
	    "BMA_HEIGHT=%lu\n", name, bm, flags,
	    (flags & BMF_STANDARD) ? " STANDARD" : " !STANDARD",
	    (flags & BMF_INTERLEAVED) ? " INTERLEAVED" : "",
	    (flags & BMF_DISPLAYABLE) ? " DISPLAYABLE" : "", depth, w, h);
	out("%s: struct: BytesPerRow=%u Rows=%u Flags=0x%x Depth=%u pad=0x%x\n",
	    name, bm->BytesPerRow, bm->Rows, bm->Flags, bm->Depth, bm->pad);
	for (i = 0; i < 8 && i < bm->Depth; i++) {
		t = TypeOfMem(bm->Planes[i]);
		out("%s: Planes[%d]=%p TypeOfMem=0x%lx%s%s\n", name, i,
		    bm->Planes[i], t, (t & MEMF_CHIP) ? " CHIP" : "",
		    (t & MEMF_FAST) ? " FAST" : (t ? "" : " (not RAM)"));
	}
}

/* --- test pattern ----------------------------------------------------- */

static void
setpix(struct BitMap *bm, int x, int y, int col)
{
	int p;
	UBYTE mask = 0x80 >> (x & 7);

	for (p = 0; p < DEPTH; p++) {
		UBYTE *b = bm->Planes[p] + y * bm->BytesPerRow + (x >> 3);
		if (col & (1 << p))
			*b |= mask;
		else
			*b &= ~mask;
	}
}

static int
getpix(struct BitMap *bm, int x, int y)
{
	int p, col = 0;
	UBYTE mask = 0x80 >> (x & 7);

	for (p = 0; p < DEPTH; p++)
		if (bm->Planes[p][y * bm->BytesPerRow + (x >> 3)] & mask)
			col |= 1 << p;
	return col;
}

static void
fill_pattern(struct BitMap *bm, int rectcol)
{
	int x, y;

	for (y = 0; y < height; y++) {
		UBYTE *p0 = bm->Planes[0] + y * bm->BytesPerRow;
		UBYTE *p1 = bm->Planes[1] + y * bm->BytesPerRow;
		UBYTE *p2 = bm->Planes[2] + y * bm->BytesPerRow;
		UBYTE *p3 = bm->Planes[3] + y * bm->BytesPerRow;
		for (x = 0; x < BPR; x++) {
			p0[x] = (x & 1) ? 0xff : 0x00;	/* 8 px vertical stripes */
			p1[x] = ((y >> 3) & 1) ? 0xff : 0x00; /* 8 row horizontal */
			p2[x] = 0;
			p3[x] = 0;
		}
		for (x = 0; x < W; x++) {
			if (((x + y) >> 4) & 1)
				p2[x >> 3] |= 0x80 >> (x & 7);
			if (((x - y + 4096) >> 5) & 1)
				p3[x >> 3] |= 0x80 >> (x & 7);
		}
	}
	/* solid rectangle at 40,4 size 560x296 */
	for (y = 4; y < 4 + 296 && y < height; y++)
		for (x = 40; x < 40 + 560; x++)
			setpix(bm, x, y, rectcol);
	/* a 4-pixel frame in colour 15 around the whole screen */
	for (y = 0; y < height; y++)
		for (x = 0; x < W; x++)
			if (x < 4 || x >= W - 4 || y < 4 || y >= height - 4)
				setpix(bm, x, y, 15);
}

static void
copy_planes(struct BitMap *dst, struct BitMap *src)
{
	int p, y;

	for (p = 0; p < DEPTH; p++)
		for (y = 0; y < height; y++)
			memcpy(dst->Planes[p] + y * dst->BytesPerRow,
			    src->Planes[p] + y * src->BytesPerRow, BPR);
}

/* Compare a (displayed) bitmap with the source; returns 1 when equal. */
static int
verify(const char *what, struct Screen *scr, struct BitMap *shown,
    struct BitMap *src, int standard)
{
	int x, y, n = 0, bad = 0, first = -1;
	struct RastPort rp;

	if (scr->RastPort.BitMap != shown && standard >= 0)
		out("verify %s: NOTE scr->RastPort.BitMap=%p != displayed %p\n",
		    what, scr->RastPort.BitMap, shown);
	InitRastPort(&rp);
	rp.BitMap = shown;
	for (y = 2; y < height; y += 23)
		for (x = 3; x < W; x += 37) {
			LONG got = ReadPixel(&rp, x, y);
			int exp = getpix(src, x, y);
			n++;
			if (got != exp) {
				bad++;
				if (first < 0) {
					first = 1;
					out("verify %s: first mismatch at %d,%d "
					    "got %ld expected %d\n", what, x, y, got,
					    exp);
				}
			}
		}
	out("verify %s: ReadPixel %d/%d sampled pixels differ -> %s\n", what,
	    bad, n, bad ? "WRONG" : "OK");
	if (standard > 0) {
		int p;
		ULONG diff = 0;
		for (p = 0; p < DEPTH; p++)
			for (y = 0; y < height; y++) {
				UBYTE *a = shown->Planes[p] + y * shown->BytesPerRow;
				UBYTE *b = src->Planes[p] + y * src->BytesPerRow;
				for (x = 0; x < BPR; x++)
					if (a[x] != b[x])
						diff++;
			}
		out("verify %s: plane compare %lu/%lu bytes differ -> %s\n",
		    what, diff, (ULONG)DEPTH * BPR * height,
		    diff ? "WRONG" : "OK");
		if (diff)
			bad++;
	}
	return bad == 0;
}

/* --- ChangeScreenBuffer helpers ---------------------------------------- */

static int
flip(struct Screen *scr, struct ScreenBuffer *sb, int *tries)
{
	int n = 0;

	while (!ChangeScreenBuffer(scr, sb)) {
		n++;
		if (n > 500) {
			out("ChangeScreenBuffer: gave up after 500 retries\n");
			*tries += n;
			return 0;
		}
		Delay(1);
	}
	*tries += n;
	return 1;
}

/* --- main --------------------------------------------------------------- */

int
main(int argc, char **argv)
{
	ULONG mode = 0x8004;	/* DEFAULT_MONITOR_ID | HIRESLACE_KEY */
	struct Screen *scr;
	struct ScreenBuffer *sb[2];
	struct BitMap fbm, cbm;
	UBYTE *planes[DEPTH], *cplanes[DEPTH];
	ULONG pal[2 + 16 * 3];
	ULONG e0, e1, v0, v1, memtype;
	int i, cur, ok, tries, failed_first;
	int standard[2];
	struct MsgPort *port;
	struct Message *msg;

	IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39);
	GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 39);
	if (!IntuitionBase || !GfxBase) {
		out("sbprobe: cannot open intuition/graphics v39\n");
		return 20;
	}
	out("sbprobe: intuition %u.%u graphics %u.%u ChipRevBits0=0x%x\n",
	    IntuitionBase->LibNode.lib_Version,
	    IntuitionBase->LibNode.lib_Revision, GfxBase->LibNode.lib_Version,
	    GfxBase->LibNode.lib_Revision, GfxBase->ChipRevBits0);
	out("sbprobe: AvailMem chip=%lu fast=%lu\n", AvailMem(MEMF_CHIP),
	    AvailMem(MEMF_FAST));
	if (FindName(&SysBase->LibList, "rtg.library"))
		out("sbprobe: rtg.library (Picasso96) is loaded\n");
	else
		out("sbprobe: no rtg.library\n");

	if (OpenDevice("timer.device", UNIT_VBLANK, &timereq.tr_node, 0) == 0) {
		struct EClockVal ev;
		TimerBase = timereq.tr_node.io_Device;
		efreq = ReadEClock(&ev);
		out("sbprobe: timer.device open, E-clock %lu Hz\n", efreq);
	}
	vbint.is_Node.ln_Type = NT_INTERRUPT;
	vbint.is_Node.ln_Pri = 10;
	vbint.is_Node.ln_Name = "sbprobe VERTB";
	vbint.is_Data = (APTR)&vbcount;
	vbint.is_Code = vbserver;
	AddIntServer(INTB_VERTB, &vbint);
	mark(&e0, &v0);
	Delay(50);
	mark(&e1, &v1);
	out("sbprobe: Delay(50) = %lu ms E-clock, %lu VERTB interrupts\n",
	    ems(e0, e1), v1 - v0);

	if (argc > 1) {
		if (strcmp(argv[1], "uaegfx") == 0) {
			height = 480;
			mode = find_uaegfx_mode();
			if (mode == INVALID_ID) {
				out("sbprobe: no uaegfx 640x480x8 mode found\n");
				RemIntServer(INTB_VERTB, &vbint);
				return 10;
			}
		} else {
			mode = strtoul(argv[1], NULL, 0);
		}
	}
	if (argc > 2)
		tag = argv[2];
	if (argc > 3)
		height = atoi(argv[3]);
	if (height > 480)
		height = 480;

	out("sbprobe: opening screen mode 0x%08lx %dx%dx%d tag=%s\n", mode, W,
	    height, DEPTH, tag);
	{
		struct NameInfo ni;
		if (GetDisplayInfoData(NULL, (UBYTE *)&ni, sizeof(ni), DTAG_NAME,
		    mode))
			out("sbprobe: mode name \"%s\"\n", ni.Name);
		else
			out("sbprobe: mode 0x%08lx not in display database\n", mode);
	}

	/* fast-RAM planar source bitmap, plus a chip-RAM twin for (c) */
	InitBitMap(&fbm, DEPTH, W, height);
	InitBitMap(&cbm, DEPTH, W, height);
	for (i = 0; i < DEPTH; i++) {
		planes[i] = AllocMem(BPR * height, MEMF_FAST | MEMF_CLEAR);
		if (!planes[i])
			planes[i] = AllocMem(BPR * height, MEMF_ANY | MEMF_CLEAR);
		fbm.Planes[i] = planes[i];
		cplanes[i] = AllocMem(BPR * height, MEMF_CHIP | MEMF_CLEAR);
		cbm.Planes[i] = cplanes[i];
	}
	memtype = TypeOfMem(planes[0]);
	out("source bitmap: InitBitMap %dx%dx%d BytesPerRow=%u Planes[0]=%p "
	    "TypeOfMem=0x%lx%s\n", W, height, DEPTH, fbm.BytesPerRow, planes[0],
	    memtype, (memtype & MEMF_FAST) ? " FAST" :
	    (memtype & MEMF_CHIP) ? " CHIP (no fast RAM!)" : "");
	report_bm("source", &fbm);
	fill_pattern(&fbm, 9);

	scr = OpenScreenTags(NULL,
	    SA_DisplayID, mode,
	    SA_Width, W,
	    SA_Height, height,
	    SA_Depth, DEPTH,
	    SA_Quiet, TRUE,
	    SA_ShowTitle, FALSE,
	    SA_Type, CUSTOMSCREEN,
	    SA_Title, (ULONG)"sbprobe",
	    TAG_DONE);
	if (!scr) {
		out("OpenScreenTags(0x%08lx) FAILED\n", mode);
		goto freeplanes;
	}
	out("screen opened: %dx%d, ViewPort modes 0x%x, GetVPModeID=0x%08lx, "
	    "BitMap %p\n", scr->Width, scr->Height, scr->ViewPort.Modes,
	    GetVPModeID(&scr->ViewPort), scr->RastPort.BitMap);
	report_bm("screen", scr->RastPort.BitMap);

	/* palette: 16 distinct colours */
	pal[0] = (16UL << 16) | 0;
	for (i = 0; i < 16; i++) {
		static const UBYTE rgb[16][3] = {
			{0, 0, 0}, {255, 255, 255}, {255, 0, 0}, {0, 255, 0},
			{0, 0, 255}, {255, 255, 0}, {0, 255, 255}, {255, 0, 255},
			{128, 128, 128}, {255, 128, 0}, {128, 0, 255}, {0, 128, 64},
			{128, 64, 0}, {64, 64, 192}, {192, 192, 64}, {255, 192, 203},
		};
		pal[1 + i * 3 + 0] = (ULONG)rgb[i][0] << 24 | 0xffffff;
		pal[1 + i * 3 + 1] = (ULONG)rgb[i][1] << 24 | 0xffffff;
		pal[1 + i * 3 + 2] = (ULONG)rgb[i][2] << 24 | 0xffffff;
	}
	pal[1 + 16 * 3] = 0;
	LoadRGB32(&scr->ViewPort, pal);
	out("LoadRGB32 done\n");

	sb[0] = AllocScreenBuffer(scr, NULL, SB_SCREEN_BITMAP);
	sb[1] = AllocScreenBuffer(scr, NULL, 0);
	out("AllocScreenBuffer: sb[0]=%p (SB_SCREEN_BITMAP) sb[1]=%p (0)\n",
	    sb[0], sb[1]);
	if (!sb[0] || !sb[1]) {
		out("AllocScreenBuffer FAILED\n");
		goto closescreen;
	}
	for (i = 0; i < 2; i++) {
		char n[8];
		sprintf(n, "sb[%d]", i);
		out("%s: sb_BitMap=%p (%s screen bitmap) sb_DBufInfo=%p\n", n,
		    sb[i]->sb_BitMap,
		    sb[i]->sb_BitMap == scr->RastPort.BitMap ? "==" : "!=",
		    sb[i]->sb_DBufInfo);
		report_bm(n, sb[i]->sb_BitMap);
		standard[i] = (GetBitMapAttr(sb[i]->sb_BitMap, BMA_FLAGS) &
		    BMF_STANDARD) != 0;
	}

	/* (a) BltBitMap fast planar -> hidden buffer sb[1], then flip */
	cur = 0;
	tries = 0;
	BltBitMap(&fbm, 0, 0, sb[1]->sb_BitMap, 0, 0, W, height, 0xC0, 0xFF,
	    NULL);
	WaitBlit();
	out("(a) BltBitMap(FAST source -> sb[1]) done\n");
	/* check the hidden buffer before flipping; on a P96-promoted screen
	 * the very first blit into a fresh ScreenBuffer bitmap was seen to
	 * be lost, so retry once and report */
	if (!verify("(a) hidden sb[1] before flip", scr, sb[1]->sb_BitMap,
	    &fbm, -1)) {
		BltBitMap(&fbm, 0, 0, sb[1]->sb_BitMap, 0, 0, W, height, 0xC0,
		    0xFF, NULL);
		WaitBlit();
		verify("(a) hidden sb[1] after 2nd BltBitMap", scr,
		    sb[1]->sb_BitMap, &fbm, -1);
	}
	ok = flip(scr, sb[1], &tries);
	out("(a) ChangeScreenBuffer(sb[1]) ok=%d, zero-returns before "
	    "success=%d\n", ok, tries);
	cur = 1;
	Delay(150);
	verify("(a) BltBitMap from FAST", scr, sb[1]->sb_BitMap, &fbm,
	    standard[1]);
	shot("after BltBitMap from FAST + ChangeScreenBuffer: stripes, rect "
	    "colour 9 (orange)");

	/* (b) CPU plane copy into the now hidden sb[0], if BMF_STANDARD */
	fill_pattern(&fbm, 5);		/* rectangle now colour 5 (yellow) */
	if (standard[0]) {
		copy_planes(sb[0]->sb_BitMap, &fbm);
		out("(b) CPU copy of planes into sb[0] (BMF_STANDARD set, "
		    "dst BytesPerRow=%u)\n", sb[0]->sb_BitMap->BytesPerRow);
	} else {
		BltBitMap(&fbm, 0, 0, sb[0]->sb_BitMap, 0, 0, W, height, 0xC0,
		    0xFF, NULL);
		out("(b) sb[0] is NOT BMF_STANDARD: skipped CPU plane copy, "
		    "used BltBitMap from FAST instead\n");
	}
	tries = 0;
	ok = flip(scr, sb[0], &tries);
	out("(b) ChangeScreenBuffer(sb[0]) ok=%d, zero-returns before "
	    "success=%d\n", ok, tries);
	cur = 0;
	Delay(150);
	verify(standard[0] ? "(b) CPU plane copy" : "(b) BltBitMap from FAST",
	    scr, sb[0]->sb_BitMap, &fbm, standard[0]);
	shot(standard[0] ? "after CPU plane copy+flip: rect colour 5 (yellow)"
	    : "after second BltBitMap+flip: rect colour 5 (yellow)");

	/* (c) BltBitMap from a CHIP-RAM copy of the source into sb[1] */
	fill_pattern(&fbm, 3);		/* rectangle now colour 3 (green) */
	copy_planes(&cbm, &fbm);
	BltBitMap(&cbm, 0, 0, sb[1]->sb_BitMap, 0, 0, W, height, 0xC0, 0xFF,
	    NULL);
	WaitBlit();
	out("(c) BltBitMap(CHIP source -> sb[1]) done\n");
	tries = 0;
	ok = flip(scr, sb[1], &tries);
	out("(c) ChangeScreenBuffer(sb[1]) ok=%d, zero-returns before "
	    "success=%d\n", ok, tries);
	cur = 1;
	Delay(150);
	verify("(c) BltBitMap from CHIP", scr, sb[1]->sb_BitMap, &fbm,
	    standard[1]);
	shot("after BltBitMap from CHIP + flip: rect colour 3 (green)");

	/* flip test, no message ports */
	tries = 0;
	failed_first = 0;
	mark(&e0, &v0);
	for (i = 0; i < 20; i++) {
		int t = 0;
		cur ^= 1;
		if (!ChangeScreenBuffer(scr, sb[cur])) {
			failed_first++;
			t = 1;
			if (!flip(scr, sb[cur], &t))
				break;
		}
		tries += t;
	}
	mark(&e1, &v1);
	out("flip test (no ports): 20 flips, %d returned 0 on first call, "
	    "%d total zero-returns, %lu ms, %lu VERTB\n", failed_first,
	    tries, ems(e0, e1), v1 - v0);

	/* flip test with a SafeMessage port: wait for safe before next flip */
	port = CreateMsgPort();
	if (port) {
		int got = 0;
		sb[0]->sb_DBufInfo->dbi_SafeMessage.mn_ReplyPort = port;
		sb[1]->sb_DBufInfo->dbi_SafeMessage.mn_ReplyPort = port;
		tries = 0;
		failed_first = 0;
		mark(&e0, &v0);
		for (i = 0; i < 20; i++) {
			int t = 0;
			cur ^= 1;
			if (!ChangeScreenBuffer(scr, sb[cur])) {
				failed_first++;
				t = 1;
				if (!flip(scr, sb[cur], &t))
					break;
			}
			tries += t;
			WaitPort(port);
			while ((msg = GetMsg(port)) != NULL)
				got++;
		}
		mark(&e1, &v1);
		out("flip test (SafeMessage port): 20 flips, %d returned 0 on "
		    "first call, %d total zero-returns, %d safe messages, "
		    "%lu ms, %lu VERTB\n", failed_first, tries, got, ems(e0, e1),
		    v1 - v0);
		/* drain anything outstanding before freeing */
		Delay(5);
		while ((msg = GetMsg(port)) != NULL)
			got++;
		sb[0]->sb_DBufInfo->dbi_SafeMessage.mn_ReplyPort = NULL;
		sb[1]->sb_DBufInfo->dbi_SafeMessage.mn_ReplyPort = NULL;
		DeleteMsgPort(port);
	}

	/* flip test with a DispMessage port: wait until the new buffer is
	 * actually displayed before flipping again */
	port = CreateMsgPort();
	if (port) {
		int got = 0;
		sb[0]->sb_DBufInfo->dbi_DispMessage.mn_ReplyPort = port;
		sb[1]->sb_DBufInfo->dbi_DispMessage.mn_ReplyPort = port;
		tries = 0;
		failed_first = 0;
		mark(&e0, &v0);
		for (i = 0; i < 20; i++) {
			int t = 0;
			cur ^= 1;
			if (!ChangeScreenBuffer(scr, sb[cur])) {
				failed_first++;
				t = 1;
				if (!flip(scr, sb[cur], &t))
					break;
			}
			tries += t;
			WaitPort(port);
			while ((msg = GetMsg(port)) != NULL)
				got++;
		}
		mark(&e1, &v1);
		out("flip test (DispMessage port): 20 flips, %d returned 0 on "
		    "first call, %d total zero-returns, %d disp messages, "
		    "%lu ms, %lu VERTB\n", failed_first, tries, got, ems(e0, e1),
		    v1 - v0);
		Delay(5);
		while ((msg = GetMsg(port)) != NULL)
			got++;
		sb[0]->sb_DBufInfo->dbi_DispMessage.mn_ReplyPort = NULL;
		sb[1]->sb_DBufInfo->dbi_DispMessage.mn_ReplyPort = NULL;
		DeleteMsgPort(port);
	}

	/* flip test, no ports, but one WaitTOF() per flip (the usual idiom) */
	tries = 0;
	failed_first = 0;
	mark(&e0, &v0);
	for (i = 0; i < 20; i++) {
		int t = 0;
		cur ^= 1;
		if (!ChangeScreenBuffer(scr, sb[cur])) {
			failed_first++;
			t = 1;
			if (!flip(scr, sb[cur], &t))
				break;
		}
		tries += t;
		WaitTOF();
	}
	mark(&e1, &v1);
	out("flip test (WaitTOF after each): 20 flips, %d returned 0 on first "
	    "call, %d total zero-returns, %lu ms, %lu VERTB\n", failed_first,
	    tries, ems(e0, e1), v1 - v0);

	/* leave the second buffer displayed (green rect), verify once more */
	if (cur != 1) {
		tries = 0;
		flip(scr, sb[1], &tries);
		cur = 1;
	}
	Delay(50);
	verify("end state", scr, sb[1]->sb_BitMap, &fbm, standard[1]);
	shot("end state: sb[1] displayed again (rect colour 3, green)");

	FreeScreenBuffer(scr, sb[1]);
	FreeScreenBuffer(scr, sb[0]);
	out("FreeScreenBuffer done\n");
closescreen:
	CloseScreen(scr);
	out("CloseScreen done\n");
freeplanes:
	for (i = 0; i < DEPTH; i++) {
		if (planes[i])
			FreeMem(planes[i], BPR * height);
		if (cplanes[i])
			FreeMem(cplanes[i], BPR * height);
	}
	RemIntServer(INTB_VERTB, &vbint);
	if (TimerBase)
		CloseDevice(&timereq.tr_node);
	CloseLibrary((struct Library *)GfxBase);
	CloseLibrary((struct Library *)IntuitionBase);
	out("sbprobe: done\n");
	return 0;
}
