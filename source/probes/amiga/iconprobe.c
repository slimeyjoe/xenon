/*
 * iconprobe: reads the game's Workbench icon the way Workbench 3.1 does
 * (icon.library GetDiskObject on "xenon" in the current directory) and
 * prints what it holds - type, stack, image geometry, every tool type and
 * what FindToolType makes of the ones in parentheses.  Then it shows the
 * image: with a Workbench running (run-icon.sh starts one on an 8-colour
 * HiRes screen), it draws it 1:1, complemented (= selected) and enlarged 4x
 * in a window on the Workbench screen and adds it as an AppIcon so that
 * Workbench itself draws it on the backdrop; without one it opens its own
 * HiRes screens of 8 and 4 colours (Workbench's default palette).  One
 * screenshot per screen: DH0:shot_icon_<n>.req, served by run-lib.sh.
 */
#include <exec/types.h>
#include <exec/ports.h>
#include <dos/dos.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/modeid.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <workbench/workbench.h>
#include <workbench/icon.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/icon.h>
#include <proto/wb.h>
#include <stdio.h>
#include <string.h>

struct Library *IconBase;
struct Library *WorkbenchBase;
struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;

static int shotno;

/* The default palette of an 8-colour Workbench 3.1 (IPrefs without Palette prefs, as
 * measured by run-icon.sh); custom screens default to blue/magenta/cyan/white for 4-7. */
static const ULONG wb_colors[] = {
	8 << 16 | 0,
	0xaaaaaaaa, 0xaaaaaaaa, 0xaaaaaaaa,
	0x00000000, 0x00000000, 0x00000000,
	0xffffffff, 0xffffffff, 0xffffffff,
	0x66666666, 0x88888888, 0xbbbbbbbb,
	0xeeeeeeee, 0x44444444, 0x44444444,
	0x55555555, 0xdddddddd, 0x55555555,
	0x00000000, 0x44444444, 0xdddddddd,
	0xeeeeeeee, 0x99999999, 0x00000000,
	0
};

static void
shot(void)
{
	char req[64], ack[64];
	BPTR fh, lock;
	int i;

	shotno++;
	sprintf(req, "DH0:shot_icon_%d.req", shotno);
	sprintf(ack, "DH0:shot_icon_%d.ack", shotno);
	printf("SHOT %d: requesting %s\n", shotno, req);
	fflush(stdout);
	fh = Open(req, MODE_NEWFILE);
	if (fh)
		Close(fh);
	for (i = 0; i < 160; i++) {
		Delay(12);
		lock = Lock(ack, ACCESS_READ);
		if (lock) {
			UnLock(lock);
			printf("SHOT %d: ack after %d polls\n", shotno, i + 1);
			return;
		}
	}
	printf("SHOT %d: no ack (timeout)\n", shotno);
}

/* the icon 1:1 (x0,y0), complemented below it, both enlarged 4x to the right */
static void
draw(struct RastPort *rp, struct DiskObject *dobj, int depth, int x0, int y0)
{
	struct Image *img = (struct Image *)dobj->do_Gadget.GadgetRender;
	int w = dobj->do_Gadget.Width, h = dobj->do_Gadget.Height;
	int x, y, i;

	DrawImage(rp, img, x0, y0);
	DrawImage(rp, img, x0, y0 + h + 10);
	SetDrMd(rp, COMPLEMENT);
	SetAPen(rp, (1 << depth) - 1);
	RectFill(rp, x0, y0 + h + 10, x0 + w - 1, y0 + h + 10 + h - 1);
	SetDrMd(rp, JAM1);
	for (i = 0; i < 2; i++) {
		int sy = y0 + i * (h + 10), dy = y0 + i * (4 * h + 10);
		for (y = 0; y < h; y++)
			for (x = 0; x < w; x++) {
				SetAPen(rp, ReadPixel(rp, x0 + x, sy + y));
				RectFill(rp, x0 + w + 16 + x * 4, dy + y * 4,
				    x0 + w + 16 + x * 4 + 3, dy + y * 4 + 3);
			}
	}
}

static void
show_colors(struct Screen *scr, int depth)
{
	ULONG table[3 * 8];
	int n = depth > 3 ? 8 : 1 << depth, i;

	GetRGB32(scr->ViewPort.ColorMap, 0, n, table);
	printf("screen depth %d, pens:", depth);
	for (i = 0; i < n; i++)
		printf(" %d=%02lx%02lx%02lx", i, table[3 * i] >> 24, table[3 * i + 1] >> 24,
		    table[3 * i + 2] >> 24);
	printf("\n");
	fflush(stdout);
}

static int
on_workbench(struct DiskObject *dobj)
{
	struct Screen *scr;
	struct Window *win;
	struct MsgPort *port;
	struct AppIcon *ai = NULL;
	struct Message *msg;
	int depth;

	scr = LockPubScreen((UBYTE *)"Workbench");
	if (!scr)
		return 0;
	depth = scr->RastPort.BitMap->Depth;
	show_colors(scr, depth);
	win = OpenWindowTags(NULL,
	    WA_PubScreen, (ULONG)scr, WA_Left, 220, WA_Top, 30,
	    WA_Width, 380, WA_Height, 200, WA_Borderless, TRUE,
	    WA_Activate, FALSE, WA_SimpleRefresh, TRUE, WA_NoCareRefresh, TRUE,
	    TAG_DONE);
	if (win) {
		SetAPen(win->RPort, 0);
		RectFill(win->RPort, 0, 0, win->Width - 1, win->Height - 1);
		draw(win->RPort, dobj, depth, 8, 8);
	} else
		printf("OpenWindow on Workbench FAILED\n");
	port = CreateMsgPort();
	if (WorkbenchBase && port) {
		ai = AddAppIcon(0, 0, (UBYTE *)"Xenon", port, 0, dobj, TAG_DONE);
		printf("AddAppIcon: %s\n", ai ? "ok (Workbench draws the icon)" : "FAILED");
	}
	fflush(stdout);
	Delay(50);
	shot();
	if (ai)
		RemoveAppIcon(ai);
	if (port) {
		while ((msg = GetMsg(port)))
			ReplyMsg(msg);
		DeleteMsgPort(port);
	}
	if (win)
		CloseWindow(win);
	UnlockPubScreen(NULL, scr);
	return 1;
}

static void
own_screen(struct DiskObject *dobj, int depth)
{
	struct Screen *scr;

	scr = OpenScreenTags(NULL,
	    SA_Width, 640, SA_Height, 256, SA_Depth, depth,
	    SA_DisplayID, HIRES_KEY, SA_Colors32, (ULONG)wb_colors,
	    SA_Title, (ULONG)"iconprobe", SA_ShowTitle, FALSE, SA_Quiet, TRUE,
	    TAG_DONE);
	if (!scr) {
		printf("OpenScreen depth %d FAILED\n", depth);
		return;
	}
	show_colors(scr, depth);
	draw(&scr->RastPort, dobj, depth, 20, 40);
	Delay(25);
	shot();
	CloseScreen(scr);
}

int
main(void)
{
	struct DiskObject *dobj;
	struct Image *img;
	char **tt;
	const char *probe[] = { "AUDIO", "RATE", "LANG", "PAULA14", "QUIET", "(LANG", NULL };
	int i;

	IconBase = OpenLibrary("icon.library", 37);
	WorkbenchBase = OpenLibrary("workbench.library", 37);
	IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39);
	GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 39);
	if (!IconBase || !IntuitionBase || !GfxBase) {
		printf("libraries missing\n");
		return 20;
	}
	printf("icon.library %d.%d\n", IconBase->lib_Version, IconBase->lib_Revision);
	dobj = GetDiskObject("xenon");
	if (!dobj) {
		printf("GetDiskObject(xenon) FAILED, IoErr %ld\n", IoErr());
		return 10;
	}
	img = (struct Image *)dobj->do_Gadget.GadgetRender;
	printf("type %d stack %ld pos %ld,%ld gadget %dx%d flags 0x%x select %s\n",
	    dobj->do_Type, dobj->do_StackSize, dobj->do_CurrentX, dobj->do_CurrentY,
	    dobj->do_Gadget.Width, dobj->do_Gadget.Height, dobj->do_Gadget.Flags,
	    dobj->do_Gadget.SelectRender ? "image" : "complement");
	printf("image %dx%d depth %d planepick 0x%x\n", img->Width, img->Height, img->Depth,
	    img->PlanePick);
	printf("default tool: %s\n", dobj->do_DefaultTool ? dobj->do_DefaultTool : "(none)");
	for (tt = (char **)dobj->do_ToolTypes, i = 0; tt && *tt; tt++, i++)
		printf("tooltype %2d: %s\n", i, *tt);
	for (i = 0; probe[i]; i++) {
		char *v = (char *)FindToolType((UBYTE **)dobj->do_ToolTypes, (UBYTE *)probe[i]);
		printf("FindToolType(%s) = %s%s%s\n", probe[i], v ? "\"" : "", v ? v : "NULL",
		    v ? "\"" : "");
	}
	fflush(stdout);
	if (!on_workbench(dobj)) {
		printf("no Workbench screen: own screens\n");
		own_screen(dobj, 3);
		own_screen(dobj, 2);
	}
	FreeDiskObject(dobj);
	CloseLibrary((struct Library *)GfxBase);
	CloseLibrary((struct Library *)IntuitionBase);
	if (WorkbenchBase)
		CloseLibrary(WorkbenchBase);
	CloseLibrary(IconBase);
	return 0;
}
