/*
 * winprobe: what a game window on the Workbench screen can rely on (plan: Workbench window,
 * scale, aspect; probes/amiga/README.md "The Workbench window").
 *
 *   winprobe modes     every display mode: id, name, nominal size, Resolution (the pixel
 *                      aspect), maximum depth
 *   winprobe wb        on the default public screen (run-win.sh starts a Workbench in the
 *                      mode under test):
 *                      1. the screen: size, depth, BMF_STANDARD, Picasso96 RGB format
 *                      2. how many exclusive pens ObtainPen gives (16 wanted)
 *                      3. drawing into a 640x400 window partly covered by another window:
 *                         WriteChunkyPixels with the pens, Picasso96API p96WritePixelArray
 *                         and cybergraphics WritePixelArray in ARGB; for each, whether the
 *                         uncovered part shows the colour and the covered part does not, and
 *                         the time of a full 640x400 write
 *                      4. input, injected through input.device as a mouse would: is the
 *                         click that activates an inactive window also delivered to it as
 *                         SELECTDOWN, and do IDCMP_DELTAMOVE deltas keep coming while the
 *                         pointer is outside the active window and pressed to the screen edge
 *
 * Results go to stdout (run-win.sh appends them to DH0:out.txt), one screenshot through
 * DH0:shot_win_<n>.req. The library calls that the NDK 3.9 has no headers for are declared
 * here by their offsets (from vbcc's inline files).
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/io.h>
#include <devices/input.h>
#include <devices/inputevent.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <graphics/gfx.h>
#include <graphics/displayinfo.h>
#include <graphics/modeid.h>
#include <graphics/rastport.h>
#include <graphics/view.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <intuition/intuitionbase.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/timer.h>
#include <stdio.h>
#include <string.h>

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Device *TimerBase;
static struct Library *P96Base, *CyberGfxBase;

/* Picasso96API.library */
struct RenderInfo {
	APTR Memory;
	WORD BytesPerRow;
	WORD pad;
	ULONG RGBFormat;
};
#define RGBFB_CLUT 1
#define RGBFB_A8R8G8B8 6
#define P96BMA_DEPTH 2
#define P96BMA_RGBFORMAT 7
#define P96BMA_ISP96 8
static ULONG p96_bm_attr(__reg("a6") struct Library *b, __reg("a0") struct BitMap *bm,
	__reg("d0") ULONG attr) = "\tjsr\t-42(a6)";
static void p96_write(__reg("a6") struct Library *b, __reg("a0") struct RenderInfo *ri,
	__reg("d0") UWORD sx, __reg("d1") UWORD sy, __reg("a1") struct RastPort *rp,
	__reg("d2") UWORD dx, __reg("d3") UWORD dy, __reg("d4") UWORD w, __reg("d5") UWORD h)
	= "\tjsr\t-102(a6)";
static void p96_read(__reg("a6") struct Library *b, __reg("a0") struct RenderInfo *ri,
	__reg("d0") UWORD dx, __reg("d1") UWORD dy, __reg("a1") struct RastPort *rp,
	__reg("d2") UWORD sx, __reg("d3") UWORD sy, __reg("d4") UWORD w, __reg("d5") UWORD h)
	= "\tjsr\t-108(a6)";

/* cybergraphics.library */
#define RECTFMT_ARGB 2
static ULONG cgx_write(__reg("a6") struct Library *b, __reg("a0") APTR src, __reg("d0") UWORD sx,
	__reg("d1") UWORD sy, __reg("d2") UWORD mod, __reg("a1") struct RastPort *rp,
	__reg("d3") UWORD dx, __reg("d4") UWORD dy, __reg("d5") UWORD w, __reg("d6") UWORD h,
	__reg("d7") UBYTE fmt) = "\tjsr\t-126(a6)";

#define GW 640
#define GH 400

static struct MsgPort *g_tport, *g_iport;
static struct timerequest *g_treq;
static struct IOStdReq *g_ireq;
static ULONG g_efreq;
static int g_shotno;

/* ---- helpers ------------------------------------------------------------------------------ */

static ULONG now_us(void)
{
	struct EClockVal e;
	unsigned long long t;
	g_efreq = ReadEClock(&e);
	t = ((unsigned long long)e.ev_hi << 32) | e.ev_lo;
	return (ULONG)(t * 1000000ULL / g_efreq);
}

static void shot(void)
{
	char req[64], ack[64];
	BPTR fh, lock;
	int i;
	g_shotno++;
	sprintf(req, "DH0:shot_win_%d.req", g_shotno);
	sprintf(ack, "DH0:shot_win_%d.ack", g_shotno);
	fh = Open(req, MODE_NEWFILE);
	if (fh)
		Close(fh);
	for (i = 0; i < 160; i++) {
		Delay(12);
		if ((lock = Lock(ack, ACCESS_READ)) != 0) {
			UnLock(lock);
			printf("shot %d: taken\n", g_shotno);
			return;
		}
	}
	printf("shot %d: no ack\n", g_shotno);
}

static void inject(struct InputEvent *ie)
{
	ie->ie_NextEvent = NULL;
	g_ireq->io_Command = IND_WRITEEVENT;
	g_ireq->io_Data = ie;
	g_ireq->io_Length = sizeof *ie;
	g_ireq->io_Flags = 0;
	DoIO((struct IORequest *)g_ireq);
}

static void pointer_to(struct Screen *scr, WORD x, WORD y)
{
	struct InputEvent ie;
	struct IEPointerPixel pp;
	memset(&ie, 0, sizeof ie);
	pp.iepp_Screen = scr;
	pp.iepp_Position.X = x;
	pp.iepp_Position.Y = y;
	ie.ie_Class = IECLASS_NEWPOINTERPOS;
	ie.ie_SubClass = IESUBCLASS_PIXEL;
	ie.ie_EventAddress = &pp;
	inject(&ie);
}

static void mouse_rel(WORD dx, WORD dy, UWORD code, UWORD qual)
{
	struct InputEvent ie;
	memset(&ie, 0, sizeof ie);
	ie.ie_Class = IECLASS_RAWMOUSE;
	ie.ie_Code = code;
	ie.ie_Qualifier = IEQUALIFIER_RELATIVEMOUSE | qual;
	ie.ie_X = dx;
	ie.ie_Y = dy;
	inject(&ie);
}

static const char *cls_name(ULONG c)
{
	switch (c) {
	case IDCMP_MOUSEBUTTONS: return "MOUSEBUTTONS";
	case IDCMP_MOUSEMOVE: return "MOUSEMOVE";
	case IDCMP_ACTIVEWINDOW: return "ACTIVEWINDOW";
	case IDCMP_INACTIVEWINDOW: return "INACTIVEWINDOW";
	case IDCMP_REFRESHWINDOW: return "REFRESHWINDOW";
	case IDCMP_CHANGEWINDOW: return "CHANGEWINDOW";
	case IDCMP_RAWKEY: return "RAWKEY";
	default: return "other";
	}
}

/* the messages queued at w: printed one by one (verbose) or summed as deltas */
static void drain(struct Window *w, int verbose, LONG *sdx, LONG *sdy, int *nmove)
{
	struct IntuiMessage *m;
	while ((m = (struct IntuiMessage *)GetMsg(w->UserPort)) != NULL) {
		if (m->Class == IDCMP_MOUSEMOVE && sdx) {
			*sdx += m->MouseX;
			*sdy += m->MouseY;
			(*nmove)++;
		} else if (verbose) {
			printf("  %s code 0x%04x qual 0x%04x mouse %d,%d\n", cls_name(m->Class),
			       (unsigned)m->Code, (unsigned)m->Qualifier, m->MouseX, m->MouseY);
		}
		ReplyMsg(&m->ExecMessage);
	}
}

/* ---- modes -------------------------------------------------------------------------------- */

static void list_modes(void)
{
	ULONG id = INVALID_ID;
	while ((id = NextDisplayInfo(id)) != (ULONG)INVALID_ID) {
		DisplayInfoHandle h = FindDisplayInfo(id);
		struct DisplayInfo di;
		struct DimensionInfo dim;
		struct NameInfo nm;
		if (!h)
			continue;
		if (!GetDisplayInfoData(h, (UBYTE *)&di, sizeof di, DTAG_DISP, 0))
			continue;
		if (!GetDisplayInfoData(h, (UBYTE *)&dim, sizeof dim, DTAG_DIMS, 0))
			continue;
		if (!GetDisplayInfoData(h, (UBYTE *)&nm, sizeof nm, DTAG_NAME, 0))
			strcpy((char *)nm.Name, "?");
		printf("mode 0x%08lx %-32s nominal %dx%d depth %d resolution %d:%d "
		       "pixel %d ns, not available 0x%x%s%s\n",
		       (unsigned long)id, (char *)nm.Name,
		       dim.Nominal.MaxX - dim.Nominal.MinX + 1, dim.Nominal.MaxY - dim.Nominal.MinY + 1,
		       (int)dim.MaxDepth, (int)di.Resolution.x, (int)di.Resolution.y,
		       (int)di.PixelSpeed, (unsigned)di.NotAvailable,
		       (di.PropertyFlags & DIPF_IS_LACE) ? " lace" : "",
		       (di.PropertyFlags & DIPF_IS_FOREIGN) ? " foreign" : "");
	}
}

/* ---- the Workbench screen ----------------------------------------------------------------- */

static struct Screen *g_scr;
static LONG g_pens[16];
static int g_npens;
static int g_truecolour;

static void screen_info(void)
{
	struct BitMap *bm = g_scr->RastPort.BitMap;
	ULONG modeid = GetVPModeID(&g_scr->ViewPort);
	ULONG depth = GetBitMapAttr(bm, BMA_DEPTH), flags = GetBitMapAttr(bm, BMA_FLAGS);
	printf("screen: %dx%d mode 0x%08lx depth %u (bitmap %lu), flags 0x%lx%s, borders "
	       "%d/%d/%d/%d bar %d\n",
	       g_scr->Width, g_scr->Height, (unsigned long)modeid, (unsigned)g_scr->BitMap.Depth,
	       (unsigned long)depth, (unsigned long)flags,
	       (flags & BMF_STANDARD) ? " STANDARD" : "",
	       g_scr->WBorLeft, g_scr->WBorTop, g_scr->WBorRight, g_scr->WBorBottom,
	       g_scr->BarHeight);
	if (P96Base && p96_bm_attr(P96Base, bm, P96BMA_ISP96)) {
		ULONG fmt = p96_bm_attr(P96Base, bm, P96BMA_RGBFORMAT);
		printf("screen: Picasso96 bitmap, RGB format %lu, depth %lu\n", (unsigned long)fmt,
		       (unsigned long)p96_bm_attr(P96Base, bm, P96BMA_DEPTH));
		g_truecolour = fmt != RGBFB_CLUT;
	} else {
		g_truecolour = depth > 8;
	}
	{
		struct DisplayInfo di;
		DisplayInfoHandle h = FindDisplayInfo(modeid);
		if (h && GetDisplayInfoData(h, (UBYTE *)&di, sizeof di, DTAG_DISP, 0))
			printf("screen: resolution %d:%d\n", di.Resolution.x, di.Resolution.y);
	}
}

static void pens_obtain(void)
{
	struct ColorMap *cm = g_scr->ViewPort.ColorMap;
	int i;
	g_npens = 0;
	for (i = 0; i < 16; i++) {
		LONG p = ObtainPen(cm, -1, 0x11111111UL * (ULONG)i, 0x22222222UL, 0x33333333UL,
		                   PEN_EXCLUSIVE);
		if (p < 0)
			break;
		g_pens[g_npens++] = p;
	}
	printf("pens: %d of 16 exclusive pens:", g_npens);
	for (i = 0; i < g_npens; i++)
		printf(" %ld", (long)g_pens[i]);
	printf("\n");
}

static void pens_release(void)
{
	int i;
	for (i = 0; i < g_npens; i++)
		ReleasePen(g_scr->ViewPort.ColorMap, (ULONG)g_pens[i]);
	g_npens = 0;
}

/* the screen's colour at (x, y): a pen on a palette screen, 0x00RRGGBB on a true colour one */
static ULONG read_at(int x, int y)
{
	if (g_truecolour && P96Base) {
		ULONG px = 0;
		struct RenderInfo ri;
		ri.Memory = &px;
		ri.BytesPerRow = 4;
		ri.pad = 0;
		ri.RGBFormat = RGBFB_A8R8G8B8;
		p96_read(P96Base, &ri, 0, 0, &g_scr->RastPort, (UWORD)x, (UWORD)y, 1, 1);
		return px & 0xFFFFFFUL;
	}
	return (ULONG)ReadPixel(&g_scr->RastPort, x, y);
}

#define NO_WANT 0xFFFFFFFFUL

/* a pen exactly; a colour within the rounding of a 15/16-bit screen */
static int same(ULONG got, ULONG want)
{
	int i;
	if (!g_truecolour)
		return got == want;
	for (i = 0; i < 24; i += 8) {
		int d = (int)((got >> i) & 0xFF) - (int)((want >> i) & 0xFF);
		if (d < -8 || d > 8)
			return 0;
	}
	return 1;
}

/* one drawing path into window a (covered on its right by window b): the colour lands in
 * the uncovered part and not under b; the time of a full write */
static void try_write(const char *name, struct Window *a, struct Window *b, void *buf,
                      int w, int h, ULONG want, int kind)
{
	struct RastPort *rp = a->RPort;
	int x0 = a->BorderLeft, y0 = a->BorderTop, i;
	ULONG t0, t1, got_open, got_cov;
	int ox = a->LeftEdge + x0 + 8, oy = a->TopEdge + y0 + h / 2;
	int cx = b->LeftEdge + b->Width / 2, cy = b->TopEdge + b->Height / 2;
	t0 = now_us();
	for (i = 0; i < 4; i++) {
		switch (kind) {
		case 0:
			WriteChunkyPixels(rp, (ULONG)x0, (ULONG)y0, (ULONG)(x0 + w - 1), (ULONG)(y0 + h - 1),
			                  (UBYTE *)buf, (LONG)w);
			break;
		case 1: {
			struct RenderInfo ri;
			ri.Memory = buf;
			ri.BytesPerRow = (WORD)(w * 4);
			ri.pad = 0;
			ri.RGBFormat = RGBFB_A8R8G8B8;
			p96_write(P96Base, &ri, 0, 0, rp, (UWORD)x0, (UWORD)y0, (UWORD)w, (UWORD)h);
			break;
		}
		default:
			cgx_write(CyberGfxBase, buf, 0, 0, (UWORD)(w * 4), rp, (UWORD)x0, (UWORD)y0, (UWORD)w,
			          (UWORD)h, RECTFMT_ARGB);
			break;
		}
	}
	WaitBlit();
	t1 = now_us();
	got_open = read_at(ox, oy);
	got_cov = read_at(cx, cy);
	if (want == NO_WANT) {
		printf("write %s: %dx%d, %lu us per full write; uncovered 0x%06lx, covered 0x%06lx "
		       "(nothing expected on this screen)\n", name, w, h, (unsigned long)((t1 - t0) / 4),
		       (unsigned long)got_open, (unsigned long)got_cov);
		return;
	}
	printf("write %s: %dx%d, %lu us per full write; uncovered 0x%06lx (%s), covered 0x%06lx "
	       "(%s)\n", name, w, h, (unsigned long)((t1 - t0) / 4), (unsigned long)got_open,
	       same(got_open, want) ? "ok" : "WRONG", (unsigned long)got_cov,
	       same(got_cov, want) ? "DRAWN OVER" : "clipped");
}

static void write_tests(struct Window *a, struct Window *b, int w, int h)
{
	if (g_npens > 0) {
		UBYTE *buf = AllocMem((ULONG)w * (ULONG)h, MEMF_ANY);
		if (buf) {
			SetRGB32(&g_scr->ViewPort, (ULONG)g_pens[0], 0x33333333UL, 0x66666666UL, 0x99999999UL);
			memset(buf, (int)g_pens[0], (size_t)w * (size_t)h);
			try_write("chunky-pens", a, b, buf, w, h,
			          g_truecolour ? 0x336699UL : (ULONG)g_pens[0], 0);
			FreeMem(buf, (ULONG)w * (ULONG)h);
		}
	} else {
		printf("write chunky-pens: no pens\n");
	}
	if (P96Base || CyberGfxBase) {
		ULONG n = (ULONG)w * (ULONG)h, i;
		ULONG *buf = AllocMem(n * 4, MEMF_ANY);
		if (buf) {
			for (i = 0; i < n; i++)
				buf[i] = 0x00996633UL;
			if (P96Base)
				try_write("p96-argb", a, b, buf, w, h, g_truecolour ? 0x996633UL : NO_WANT, 1);
			else
				printf("write p96-argb: no Picasso96API.library\n");
			for (i = 0; i < n; i++)
				buf[i] = 0x00669933UL;
			if (CyberGfxBase)
				try_write("cgx-argb", a, b, buf, w, h, g_truecolour ? 0x669933UL : NO_WANT, 2);
			else
				printf("write cgx-argb: no cybergraphics.library\n");
			FreeMem(buf, n * 4);
		}
	}
}

static void input_tests(struct Window *a, struct Window *b)
{
	LONG sdx, sdy;
	int n, i;

	/* 1. b active; click into a: which messages does a get? */
	ActivateWindow(b);
	Delay(25);
	drain(a, 0, 0, 0, 0);
	pointer_to(g_scr, (WORD)(a->LeftEdge + a->BorderLeft + 40), (WORD)(a->TopEdge + a->BorderTop + 40));
	Delay(5);
	drain(a, 0, 0, 0, 0);
	mouse_rel(0, 0, IECODE_LBUTTON, IEQUALIFIER_LEFTBUTTON);
	Delay(5);
	mouse_rel(0, 0, IECODE_LBUTTON | IECODE_UP_PREFIX, 0);
	Delay(25);
	printf("click into inactive window: active now %s; messages:\n",
	       IntuitionBase->ActiveWindow == a ? "yes" : "NO");
	drain(a, 1, 0, 0, 0);
	/* a second click, a already active */
	mouse_rel(0, 0, IECODE_LBUTTON, IEQUALIFIER_LEFTBUTTON);
	Delay(5);
	mouse_rel(0, 0, IECODE_LBUTTON | IECODE_UP_PREFIX, 0);
	Delay(25);
	printf("click into the active window: messages:\n");
	drain(a, 1, 0, 0, 0);

	/* 2. a active, the pointer driven right past a's edge and on into the screen edge */
	pointer_to(g_scr, (WORD)(a->LeftEdge + a->Width / 2), (WORD)(a->TopEdge + a->Height / 2));
	Delay(5);
	drain(a, 0, 0, 0, 0);
	/* one move at a time, read at once (Intuition queues at most WA_MouseQueue = 5 moves
	 * per window and drops the rest) */
	sdx = sdy = 0;
	n = 0;
	for (i = 0; i < 100; i++) {
		mouse_rel(20, 0, IECODE_NOBUTTON, 0);
		WaitTOF();
		drain(a, 0, &sdx, &sdy, &n);
		if (i == 39)
			printf("deltamove: after 40 x +20 the pointer is at %d,%d (window %d..%d): %d "
			       "messages, sum %ld,%ld\n", g_scr->MouseX, g_scr->MouseY, a->LeftEdge,
			       a->LeftEdge + a->Width - 1, n, (long)sdx, (long)sdy);
	}
	printf("deltamove: after 100 x +20 the pointer is at %d,%d: %d messages, sum %ld,%ld\n",
	       g_scr->MouseX, g_scr->MouseY, n, (long)sdx, (long)sdy);
	sdx = sdy = 0;
	n = 0;
	for (i = 0; i < 100; i++) {
		mouse_rel(0, 20, IECODE_NOBUTTON, 0);
		WaitTOF();
		drain(a, 0, &sdx, &sdy, &n);
	}
	printf("deltamove: then 100 x +20 down, pointer at %d,%d: %d messages, sum %ld,%ld\n",
	       g_scr->MouseX, g_scr->MouseY, n, (long)sdx, (long)sdy);
	/* the old way: 100 moves, read once */
	sdx = sdy = 0;
	n = 0;
	for (i = 0; i < 100; i++)
		mouse_rel(-20, 0, IECODE_NOBUTTON, 0);
	Delay(25);
	drain(a, 0, &sdx, &sdy, &n);
	printf("deltamove: 100 x -20 read once: %d messages, sum %ld,%ld (mouse queue)\n", n,
	       (long)sdx, (long)sdy);
}

static void wb_tests(void)
{
	struct Window *a, *b;
	int iw, ih, fits, top;
	g_scr = LockPubScreen(NULL);
	if (!g_scr) {
		printf("no default public screen\n");
		return;
	}
	screen_info();
	pens_obtain();
	{
		/* a window with a title bar: the screen's border sizes plus a title of its font */
		int bw = g_scr->WBorLeft + g_scr->WBorRight;
		int bh = g_scr->WBorTop + g_scr->Font->ta_YSize + 1 + g_scr->WBorBottom;
		iw = GW;
		ih = GH;
		fits = bw + GW <= g_scr->Width && bh + GH <= g_scr->Height;
		top = bh + GH + g_scr->BarHeight + 1 <= g_scr->Height ? g_scr->BarHeight + 1 : 0;
		if (!fits) {
			iw = g_scr->Width - bw - 40;
			ih = g_scr->Height - bh - 40;
			top = 0;
		}
		printf("window: 640x400 inner needs %dx%d, %s (top %d)\n", bw + GW, bh + GH,
		       fits ? "fits" : "does NOT fit", top);
	}
	a = OpenWindowTags(NULL, WA_PubScreen, (ULONG)g_scr, WA_Left, 0, WA_Top, top,
	                   WA_InnerWidth, iw, WA_InnerHeight, ih, WA_Title, (ULONG)"winprobe A",
	                   WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
	                   WA_RMBTrap, TRUE, WA_ReportMouse, TRUE, WA_SimpleRefresh, TRUE,
	                   WA_Activate, TRUE,
	                   WA_IDCMP, IDCMP_RAWKEY | IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE |
	                             IDCMP_DELTAMOVE | IDCMP_ACTIVEWINDOW | IDCMP_INACTIVEWINDOW |
	                             IDCMP_REFRESHWINDOW | IDCMP_CHANGEWINDOW,
	                   TAG_DONE);
	b = a ? OpenWindowTags(NULL, WA_PubScreen, (ULONG)g_scr,
	                       WA_Left, a->LeftEdge + a->BorderLeft + iw - iw / 3,
	                       WA_Top, a->TopEdge + a->BorderTop + 20, WA_Width, iw / 3 + 10,
	                       WA_Height, ih / 3, WA_Title, (ULONG)"winprobe B", WA_DragBar, TRUE,
	                       WA_DepthGadget, TRUE, WA_SimpleRefresh, TRUE, WA_IDCMP, 0,
	                       TAG_DONE) : 0;
	UnlockPubScreen(NULL, g_scr);
	if (!a || !b) {
		printf("cannot open the windows\n");
		if (a) CloseWindow(a);
		pens_release();
		return;
	}
	printf("window A at %d,%d size %dx%d borders %d/%d/%d/%d; B at %d,%d %dx%d\n", a->LeftEdge,
	       a->TopEdge, a->Width, a->Height, a->BorderLeft, a->BorderTop, a->BorderRight,
	       a->BorderBottom, b->LeftEdge, b->TopEdge, b->Width, b->Height);
	Delay(10);
	write_tests(a, b, iw, ih);
	shot();
	input_tests(a, b);
	CloseWindow(b);
	CloseWindow(a);
	pens_release();
}

int main(int argc, char **argv)
{
	int rc = 0;
	IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39);
	GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 40);
	if (!IntuitionBase || !GfxBase) {
		printf("needs graphics.library 40\n");
		return 20;
	}
	P96Base = OpenLibrary("Picasso96API.library", 2);
	CyberGfxBase = OpenLibrary("cybergraphics.library", 40);
	printf("winprobe: Picasso96API %s, cybergraphics %s\n", P96Base ? "yes" : "no",
	       CyberGfxBase ? "yes" : "no");
	g_tport = CreateMsgPort();
	g_treq = (struct timerequest *)CreateIORequest(g_tport, sizeof *g_treq);
	g_iport = CreateMsgPort();
	g_ireq = (struct IOStdReq *)CreateIORequest(g_iport, sizeof *g_ireq);
	if (!g_treq || !g_ireq || OpenDevice((STRPTR)TIMERNAME, UNIT_ECLOCK, (struct IORequest *)g_treq, 0) ||
	    OpenDevice((STRPTR)"input.device", 0, (struct IORequest *)g_ireq, 0)) {
		printf("cannot open timer.device / input.device\n");
		return 20;
	}
	TimerBase = g_treq->tr_node.io_Device;
	if (argc > 1 && strcmp(argv[1], "modes") == 0)
		list_modes();
	else if (argc > 1 && strcmp(argv[1], "wb") == 0)
		wb_tests();
	else {
		printf("usage: winprobe modes|wb\n");
		rc = 5;
	}
	CloseDevice((struct IORequest *)g_ireq);
	CloseDevice((struct IORequest *)g_treq);
	DeleteIORequest((struct IORequest *)g_ireq);
	DeleteIORequest((struct IORequest *)g_treq);
	DeleteMsgPort(g_iport);
	DeleteMsgPort(g_tport);
	if (CyberGfxBase) CloseLibrary(CyberGfxBase);
	if (P96Base) CloseLibrary(P96Base);
	CloseLibrary((struct Library *)GfxBase);
	CloseLibrary((struct Library *)IntuitionBase);
	return rc;
}
