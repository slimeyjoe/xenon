/* The game in a window on the Workbench (or any public) screen: WINDOW, PUBSCREEN=
 * (backends/amiga/README.md "Window, scale, aspect"; what it relies on was measured by
 * probes/amiga/winprobe.c, probes/amiga/README.md "The Workbench window").
 *
 * The engine's 16 colours are 16 exclusive pens of the screen (ObtainPen): a palette screen
 * changes their colours in place, so fades cost what they cost on the game's own screen; on a
 * true colour screen a pen's new colour does not reach pixels already drawn, so a palette
 * change redraws the page - while the frame clock is behind, only the last of a run of changes
 * (a fade shows fewer steps but takes its time; a full redraw of 640x400 true colour pixels
 * costs a 68040 several ticks). The page is drawn with WriteChunkyPixels into the window's
 * RastPort, where the layers clip it against other windows, through the scaled upload's
 * strips (upload_scaled.c, am_geom: SCALE and ASPECT apply here too). There is one buffer:
 * a page switch is a full redraw in place, and a REFRESHWINDOW (part of the window uncovered)
 * redraws the page at the next present.
 *
 * When the screen cannot take the window - fewer than 16 free pens (8 and 16 colour
 * Workbenches), or too small for 640x400 even without borders - am_win_open says so and the
 * game opens its own screen as without WINDOW. A Workbench 640 pixels wide (every native HiRes
 * one) cannot hold 640 pixels plus borders: there the window is borderless.
 *
 * F10 (input.c) steps through the sizes that fit: 1x, 1x 4:3, 2x, 2x 4:3, 3x, 3x 4:3. */
#include "os_begin.h"
#include <exec/memory.h>
#include <graphics/gfx.h>
#include <graphics/displayinfo.h>
#include <graphics/rastport.h>
#include <graphics/view.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include "os_end.h"
#include <string.h>
#include "amiga.h"
#include "frameos.h"
#include "upload.h"

#define W AM_W
#define H AM_H
#define BPR AM_BPR
#define NSTEPS (XE_SCALE_MAX * 2)

static struct Window *g_win;
static struct Screen *g_scr;            /* the public screen (kept open by the window) */
static LONG g_pens[16];
static int g_npens;
static int g_truecolour;                /* the screen's pixels are not pens */
static unsigned g_pw, g_ph;             /* the screen's pixel shape */
static int g_step;                      /* index of the shown size: sx = step / 2 + 1, aspect = odd */
static int g_borderless;
static int g_first, g_scroll;
static uint8_t g_pal[48];
static int g_pal_on = -1;
static int g_pal_pending;               /* true colour: new colours not redrawn yet */
static ULONG g_n_full, g_n_dirty, g_n_pal;

/* ---- sizes -------------------------------------------------------------------------------- */

static int step_sx(int step)
{
    return step / 2 + 1;
}

static int step_h(int step)
{
    int sx = step_sx(step);
    return (step & 1) ? xe_aspect_height(sx, g_pw, g_ph) : H * sx;
}

/* the outer size of a window with title bar and borders around w x h on g_scr */
static void bordered(int w, int h, int *ow, int *oh)
{
    *ow = w + g_scr->WBorLeft + g_scr->WBorRight;
    *oh = h + g_scr->WBorTop + g_scr->Font->ta_YSize + 1 + g_scr->WBorBottom;
}

/* does a step fit: 2 with borders, 1 borderless, 0 no */
static int step_fits(int step)
{
    int w = W * step_sx(step), h = step_h(step), ow, oh;
    if ((step & 1) && h == H * step_sx(step))
        return 0;                       /* 4:3 is the plain size on this screen: a twin */
    bordered(w, h, &ow, &oh);
    if (ow <= g_scr->Width && oh <= g_scr->Height)
        return 2;
    if (w <= g_scr->Width && h <= g_scr->Height)
        return 1;
    return 0;
}

static void set_step(int step)
{
    g_step = step;
    am_geom.sx = step_sx(step);
    am_geom.dst_w = W * am_geom.sx;
    am_geom.dst_h = step_h(step);
    xe_rowmap_init(&am_geom.map, am_geom.dst_h);
}

/* the step SCALE= / ASPECT ask for, or the largest that fits (SCALE=auto); -1 if none fits */
static int first_step(void)
{
    int sx, aspect;
    for (aspect = am_opts.aspect; aspect >= 0; aspect--) {
        for (sx = am_opts.scale ? am_opts.scale : XE_SCALE_MAX; sx >= 1; sx--) {
            int step = (sx - 1) * 2 + aspect;
            if (step_fits(step))
                return step;
            if (aspect && step_h(step) == H * sx && step_fits(step - 1))
                return step - 1;        /* 4:3 already: the plain size */
        }
        if (aspect)
            am_log("window: no room for a 4:3 picture, showing 640x400 pixels as they are\n");
    }
    return -1;
}

/* ---- pens --------------------------------------------------------------------------------- */

static void pens_release(void)
{
    int i;
    for (i = 0; i < g_npens; i++)
        ReleasePen(g_scr->ViewPort.ColorMap, (ULONG)g_pens[i]);
    g_npens = 0;
}

static int pens_obtain(void)
{
    struct ColorMap *cm = g_scr->ViewPort.ColorMap;
    g_npens = 0;
    while (g_npens < 16) {
        LONG p = ObtainPen(cm, (ULONG)-1, 0, 0, 0, PEN_EXCLUSIVE);
        if (p < 0)
            break;
        g_pens[g_npens++] = p;
    }
    return g_npens == 16;
}

static void set_palette(const uint8_t rgb4[48], int on)
{
    int i;
    for (i = 0; i < 16; i++) {
        ULONG r = on ? (ULONG)rgb4[i * 3] * 0x11111111UL : 0;
        ULONG g = on ? (ULONG)rgb4[i * 3 + 1] * 0x11111111UL : 0;
        ULONG b = on ? (ULONG)rgb4[i * 3 + 2] * 0x11111111UL : 0;
        SetRGB32(&g_scr->ViewPort, (ULONG)g_pens[i], r, g, b);
    }
    memcpy(g_pal, rgb4, 48);
    g_pal_on = on;
    g_n_pal++;
}

/* ---- the window --------------------------------------------------------------------------- */

static int inner_left(void)
{
    return g_win->BorderLeft;
}

static int inner_top(void)
{
    return g_win->BorderTop;
}

int am_win_open(const struct xe_video_cfg *cfg)
{
    const char *title = cfg->title ? cfg->title : "Xenon";
    struct BitMap *bm;
    int step, fit, w, h, ow, oh, i;
    uint8_t pen[16];

    g_scr = LockPubScreen((CONST_STRPTR)am_opts.pubscreen);
    if (!g_scr) {
        am_log("window: no public screen %s, using an own screen\n",
               am_opts.pubscreen ? am_opts.pubscreen : "(default)");
        return 1;
    }
    bm = g_scr->RastPort.BitMap;
    g_truecolour = GetBitMapAttr(bm, BMA_DEPTH) > 8;
    /* native screens: the pixel shape of the mode (PAL HiRes Laced is square by DisplayInfo);
     * graphics cards: square (probes/amiga/README.md "The Workbench window") */
    g_pw = g_ph = 1;
    if (GetBitMapAttr(bm, BMA_FLAGS) & BMF_STANDARD) {
        struct DisplayInfo di;
        DisplayInfoHandle dh = FindDisplayInfo(GetVPModeID(&g_scr->ViewPort));
        if (dh && GetDisplayInfoData(dh, (UBYTE *)&di, sizeof di, DTAG_DISP, 0) &&
            di.Resolution.x > 0 && di.Resolution.y > 0) {
            g_pw = (unsigned)di.Resolution.x;
            g_ph = (unsigned)di.Resolution.y;
        }
    }
    if (!pens_obtain()) {
        am_log("window: the screen has %d free pens, 16 needed (a 256-colour or graphics card "
               "Workbench), using an own screen\n", g_npens);
        pens_release();
        UnlockPubScreen(NULL, g_scr);
        return 1;
    }
    step = first_step();
    if (step < 0) {
        am_log("window: the %dx%d screen is too small for 640x400, using an own screen\n",
               g_scr->Width, g_scr->Height);
        pens_release();
        UnlockPubScreen(NULL, g_scr);
        return 1;
    }
    fit = step_fits(step);
    set_step(step);
    w = am_geom.dst_w;
    h = am_geom.dst_h;
    g_borderless = fit == 1;
    if (g_borderless) {
        ow = w;
        oh = h;
    } else {
        bordered(w, h, &ow, &oh);
    }
    {
        int top = g_scr->BarHeight + 1, left = (g_scr->Width - ow) / 2;
        if (top + oh > g_scr->Height)
            top = g_scr->Height - oh;
        g_win = OpenWindowTags(NULL,
                               WA_PubScreen, (IPTR)g_scr,
                               WA_Left, left, WA_Top, top,
                               WA_InnerWidth, w, WA_InnerHeight, h,
                               WA_Title, g_borderless ? 0 : (IPTR)title,
                               WA_ScreenTitle, (IPTR)title,
                               WA_DragBar, !g_borderless, WA_DepthGadget, !g_borderless,
                               WA_CloseGadget, !g_borderless, WA_Borderless, g_borderless,
                               WA_Activate, TRUE, WA_RMBTrap, TRUE, WA_ReportMouse, TRUE,
                               WA_SimpleRefresh, TRUE,
                               WA_IDCMP, IDCMP_RAWKEY | IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE |
                                         IDCMP_DELTAMOVE | IDCMP_ACTIVEWINDOW |
                                         IDCMP_INACTIVEWINDOW | IDCMP_CLOSEWINDOW |
                                         IDCMP_REFRESHWINDOW | IDCMP_CHANGEWINDOW,
                               TAG_DONE);
    }
    UnlockPubScreen(NULL, g_scr);       /* the window keeps the screen open */
    if (!g_win) {
        am_log("window: cannot open a %dx%d window, using an own screen\n", ow, oh);
        pens_release();
        am_geom.sx = 0;
        return 1;
    }
    for (i = 0; i < 16; i++)
        pen[i] = (uint8_t)g_pens[i];
    if (am_scaled_setup(pen) != 0) {
        am_log("window: out of memory\n");
        am_win_close();
        return -1;
    }
    am_log("window: on %dx%d %s screen, page %dx%d (%dx%s)%s, pens %ld..%ld\n",
           g_scr->Width, g_scr->Height, g_truecolour ? "true colour" : "palette", w, h,
           am_geom.sx, h != H * am_geom.sx ? ", 4:3" : "", g_borderless ? ", borderless" : "",
           (long)g_pens[15], (long)g_pens[0]);
    g_first = 1;
    g_scroll = 0;
    g_pal_on = -1;
    g_pal_pending = 0;
    if (am_input_attach(g_win) != 0) {
        am_win_close();
        return -1;
    }
    return 0;
}

void am_win_close(void)
{
    if (g_win) {
        am_log("window: %lu full redraws, %lu dirty, %lu palette changes\n",
               (unsigned long)g_n_full, (unsigned long)g_n_dirty, (unsigned long)g_n_pal);
        am_input_close();
        am_scaled_release();
        pens_release();
        CloseWindow(g_win);
    }
    g_win = 0;
    am_geom.sx = 0;
}

void am_win_refresh(void)
{
    if (g_win) {
        BeginRefresh(g_win);
        EndRefresh(g_win, TRUE);
        g_first = 1;                    /* the whole page at the next present */
    }
}

/* F10: the next size that fits (same kind: with borders, or borderless) */
void am_win_cycle(void)
{
    int i, step = -1, ow, oh, left, top;
    if (!g_win)
        return;
    for (i = 1; i <= NSTEPS; i++) {
        int s = (g_step + i) % NSTEPS, f = step_fits(s);
        if (f == 2 || (f == 1 && g_borderless)) {
            step = s;
            break;
        }
    }
    if (step < 0 || step == g_step)
        return;
    {
        int sx = step_sx(step);
        int w = W * sx, h = step_h(step);
        if (g_borderless) {
            ow = w;
            oh = h;
        } else {
            bordered(w, h, &ow, &oh);
        }
    }
    left = g_win->LeftEdge;
    top = g_win->TopEdge;
    if (left + ow > g_scr->Width)
        left = g_scr->Width - ow;
    if (top + oh > g_scr->Height)
        top = g_scr->Height - oh;
    am_scaled_release();
    set_step(step);
    {
        uint8_t pen[16];
        for (i = 0; i < 16; i++)
            pen[i] = (uint8_t)g_pens[i];
        if (am_scaled_setup(pen) != 0) {
            am_log("window: out of memory for %dx%d\n", am_geom.dst_w, am_geom.dst_h);
            am_geom.sx = 0;             /* presents draw nothing until the next F10 */
            return;
        }
    }
    ChangeWindowBox(g_win, left, top, ow, oh);
    am_log("window: page %dx%d (%dx%s)\n", am_geom.dst_w, am_geom.dst_h, am_geom.sx,
           am_geom.dst_h != H * am_geom.sx ? ", 4:3" : "");
    g_first = 1;
}

/* IDCMP_CHANGEWINDOW: the new size is in place, draw everything */
void am_win_changed(void)
{
    g_first = 1;
}

void am_win_snapinfo(struct am_snapinfo *si)
{
    struct BitMap *bm;
    int i;
    memset(si, 0, sizeof *si);
    if (!g_win)
        return;
    bm = g_scr->RastPort.BitMap;
    si->standard = (GetBitMapAttr(bm, BMA_FLAGS) & BMF_STANDARD) != 0;
    si->bpr = bm->BytesPerRow;
    si->rows = bm->Rows;
    si->depth = bm->Depth;
    for (i = 0; i < 8 && i < bm->Depth; i++)
        si->planes[i] = bm->Planes[i];
    si->mode = GetVPModeID(&g_scr->ViewPort);
    si->bm = bm;
    si->dx = g_win->LeftEdge + inner_left();
    si->dy = g_win->TopEdge + inner_top();
    si->sx = am_geom.sx;
    si->dst_h = am_geom.dst_h;
    si->window = 1;
    si->mouse_x = g_scr->MouseX;
    si->mouse_y = g_scr->MouseY;
    for (i = 0; i < 16; i++)
        si->pens[i] = (int)g_pens[i];
}

/* the page rows [y0,y1) x byte columns [x0,x1) in display rows, through the window */
static void draw(const struct xe_planes *page, int x0, int y0, int x1, int y1, int scroll)
{
    am_scaled_draw(g_win->RPort, page, x0, y0, x1, y1, scroll, inner_left(), inner_top());
}

void am_win_present(const struct xe_planes *page, const struct xe_rect *dirty, int page_switched,
                    int scroll_line, const uint8_t rgb4[48], int graphics_on)
{
    int full = g_first || page_switched || dirty == NULL || scroll_line != g_scroll, changed = 0;
    if (!g_win || !am_geom.sx)
        return;
    if (g_pal_on != graphics_on || memcmp(g_pal, rgb4, 48) != 0) {
        set_palette(rgb4, graphics_on);
        changed = 1;
        if (g_truecolour)
            g_pal_pending = 1;          /* the pixels already drawn keep their old colours */
    }
    /* the redraw for new colours waits while the clock is behind and they keep changing */
    if (g_pal_pending && (!changed || am_fr->late_us <= AM_TICK_US))
        full = 1;
    if (full) {
        g_pal_pending = 0;
        g_n_full++;
        draw(page, 0, 0, BPR, H, scroll_line);
    } else if (!xe_rect_empty(dirty)) {
        int x0 = dirty->x0 < 0 ? 0 : dirty->x0, y0 = dirty->y0 < 0 ? 0 : dirty->y0;
        int x1 = dirty->x1 > BPR ? BPR : dirty->x1, y1 = dirty->y1 > H ? H : dirty->y1;
        g_n_dirty++;
        if (x1 > x0 && y1 > y0) {
            /* page rows to display rows, as video.c: a scrolled update may wrap */
            int d0 = y0 - scroll_line, n = y1 - y0;
            if (d0 < 0)
                d0 += H;
            if (d0 + n <= H) {
                draw(page, x0, d0, x1, d0 + n, scroll_line);
            } else {
                draw(page, x0, d0, x1, H, scroll_line);
                draw(page, x0, 0, x1, d0 + n - H, scroll_line);
            }
        }
    }
    g_scroll = scroll_line;
    g_first = 0;
}
