/* Screen and presentation.
 *
 * One custom screen, 640x400, in the mode the user chose (MODE=, default the flavour's:
 * HIRESLACE_KEY on the Amiga, which Picasso96 may promote to a graphics card mode) with two
 * V39 ScreenBuffers. The engine draws into its own planes; present copies the changed rows
 * and byte columns into the buffer that is displayed, exactly like the original wrote into
 * the displayed VRAM page, so there is no vertical-blank wait that would tie the 56.42 Hz
 * game tick to the 50 Hz display. A page switch (the engine's display-page register) uploads
 * the whole new page into the hidden buffer and flips with ChangeScreenBuffer.
 *
 * The copy itself is one of the flavour's uploads (upload.h): the planar CPU copy for a
 * BMF_STANDARD (planar chip RAM) bitmap, otherwise the graphics card upload (UPLOAD=, by
 * default the flavour's first). The overscan border is blanked (black), as on the PC-98 and in
 * the SDL3 backend: on a native screen it would otherwise show colour 0, which a fade to
 * white turns white.
 *
 * With a planar upload the screen is first opened at depth 4; when it turns out to be a
 * graphics card screen (Picasso96, also a promoted native mode) it is reopened with depth 8
 * and the engine's colours on pens 16..31. Pen 0 stays black, so the board rows outside a
 * 400-line screen are black, and the uploads write every byte of the chunky buffer (see
 * upload_rtg.c). A flavour without a planar upload opens depth 8 directly.
 *
 * A depth-8 screen whose bitmap is really deeper (truecolour underneath, as on AROS) does not
 * recolour what is already drawn when the palette changes: there every palette change also
 * re-uploads the whole page.
 *
 * SCALE= and ASPECT (backends/amiga/README.md "Window, scale, aspect") take a path of their own
 * at open, open_scaled: a native screen gets the page's rows through a row map (4:3 picture,
 * upload planar-map), a graphics card screen a mode large enough for 640 x sx pixels across
 * and the scaled upload. Without them (and when they come to nothing on this display) the
 * plain path below opens the screen exactly as it always has. */
#include "os_begin.h"
#include <exec/memory.h>
#include <graphics/gfx.h>
#include <graphics/modeid.h>
#include <graphics/videocontrol.h>
#include <graphics/displayinfo.h>
#include <utility/tagitem.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include "os_end.h"
#include <string.h>
#include "amiga.h"
#include "amsys.h"
#include "flavour.h"
#include "upload.h"

#define W AM_W
#define H AM_H
#define BPR AM_BPR
#define DEPTH 4

static struct Screen *g_scr;
static struct ScreenBuffer *g_sb[2];
static int g_standard[2];
static int g_shown;                 /* index of the displayed buffer */
static int g_first;                 /* nothing uploaded yet */
static int g_scroll[2];             /* scroll line each buffer was last drawn with */
static uint8_t g_pal[48];
static int g_pal_on = -1;
static int g_depth;                 /* screen depth: 4 native, 8 graphics card */
static int g_pen_base;              /* pen of engine colour 0: 0 native, 16 graphics card */
static int g_truecolour;            /* palette changes do not recolour the bitmap */
static int g_dx, g_dy;              /* the page's top left on a screen larger than 640x400 */
static const struct am_upload *g_planar;    /* for BMF_STANDARD bitmaps, or NULL */
static const struct am_upload *g_up;        /* for everything else */
static const struct am_upload *g_up_live;   /* g_up after a successful init */
static ULONG g_n_full, g_n_flip, g_n_flipfail, g_n_dirty, g_n_pal;   /* diagnostics */

struct am_geom am_geom;
static int g_window;                /* WINDOW: the game is in video_win.c's window */

struct Screen *am_video_screen(void)
{
    return g_scr;
}

static void set_palette(const uint8_t rgb4[48], int on)
{
    ULONG pal[2 + 16 * 3];
    int i;
    pal[0] = (16UL << 16) | (ULONG)g_pen_base;
    for (i = 0; i < 48; i++)
        pal[1 + i] = on ? (ULONG)rgb4[i] * 0x11111111UL : 0;
    pal[1 + 48] = 0;
    LoadRGB32(&g_scr->ViewPort, pal);
    memcpy(g_pal, rgb4, 48);
    g_pal_on = on;
    g_n_pal++;
}

/* frees the screen buffers, the screen and the upload's buffers; safe on a half-open screen */
static void close_screen(void)
{
    if (g_scr) {
        if (g_shown != 0 && g_sb[0]) {
            ChangeScreenBuffer(g_scr, g_sb[0]);
            WaitTOF();
        }
        if (g_sb[1]) FreeScreenBuffer(g_scr, g_sb[1]);
        if (g_sb[0]) FreeScreenBuffer(g_scr, g_sb[0]);
        CloseScreen(g_scr);
    }
    g_sb[0] = g_sb[1] = 0;
    g_scr = 0;
    if (g_up_live)
        g_up_live->fini();
    g_up_live = 0;
    g_shown = 0;
}

/* opens the screen with two screen buffers; 0 or -1 (nothing left open). With probe set, a
 * graphics card screen is closed again before any screen buffer exists and 1 is returned
 * (freeing screen buffers of a never-flipped Picasso96-promoted screen crashed WinUAE's
 * Workbench, plan §10). */
static int open_screen(ULONG mode, int depth, int w, int h, const char *title, int probe)
{
    int i;
    g_scr = OpenScreenTags(NULL,
                           SA_DisplayID, mode,
                           SA_Width, w,
                           SA_Height, h,
                           SA_Depth, depth,
                           SA_Quiet, TRUE,
                           SA_ShowTitle, FALSE,
                           SA_Type, CUSTOMSCREEN,
                           SA_Title, (IPTR)title,
                           TAG_DONE);
    if (!g_scr) {
        am_log("video: cannot open screen mode 0x%08lx depth %d\n", (unsigned long)mode, depth);
        return -1;
    }
    if (probe && !(GetBitMapAttr(g_scr->RastPort.BitMap, BMA_FLAGS) & BMF_STANDARD)) {
        CloseScreen(g_scr);
        g_scr = 0;
        return 1;
    }
    {
        /* black overscan border instead of colour 0 (ECS/AGA; ignored elsewhere) */
        struct TagItem vt[2];
        vt[0].ti_Tag = VTAG_BORDERBLANK_SET;
        vt[0].ti_Data = TRUE;
        vt[1].ti_Tag = TAG_DONE;
        vt[1].ti_Data = 0;
        if (VideoControl(g_scr->ViewPort.ColorMap, vt) == 0) {
            MakeScreen(g_scr);
            RethinkDisplay();
        }
    }
    g_sb[0] = AllocScreenBuffer(g_scr, NULL, SB_SCREEN_BITMAP);
    g_sb[1] = AllocScreenBuffer(g_scr, NULL, 0);
    if (!g_sb[0] || !g_sb[1]) {
        am_log("video: AllocScreenBuffer failed\n");
        close_screen();
        return -1;
    }
    for (i = 0; i < 2; i++)
        g_standard[i] = (GetBitMapAttr(g_sb[i]->sb_BitMap, BMA_FLAGS) & BMF_STANDARD) != 0;
    g_depth = depth;
    return 0;
}

/* the flavour's uploads (flavour.h): its planar one, and the one for everything else (named
 * by UPLOAD=, else the first that is not planar) */
static int pick_uploads(void)
{
    const struct am_upload *const *u;
    g_planar = g_up = 0;
    for (u = am_flavour.upload; *u; u++) {
        if ((*u)->planar) {
            if (!g_planar)
                g_planar = *u;
        } else if (am_opts.upload ? am_strieq(am_opts.upload, (*u)->name) : !g_up) {
            g_up = *u;
        }
    }
    if (!g_up) {
        am_log("video: no upload \"%s\" in this build\n", am_opts.upload ? am_opts.upload : "");
        return -1;
    }
    return 0;
}

/* opens a w x h screen at `depth` (pen base: 0 at depth 4, 16 at depth 8) and readies upload
 * u; 0 or -1 (nothing left open) */
static int open_screen_upload(ULONG mode, int depth, int w, int h, const char *title,
                              const struct am_upload *u)
{
    if (open_screen(mode, depth, w, h, title, 0) != 0)
        return -1;
    g_pen_base = depth == 8 ? 16 : 0;
    if (u->init(depth, g_pen_base) != 0) {
        close_screen();
        return -1;
    }
    g_up_live = u;
    return 0;
}

static int open_with_upload(ULONG mode, int depth, const char *title)
{
    return open_screen_upload(mode, depth, W, H, title, g_up);
}

/* the mode for MODE= 0: the flavour's, or where it has none (INVALID_ID: AROS, no Amiga
 * display modes there) the system's best for 640x400 at depth 8, else for 640x480 (the
 * screen then takes its top 400 lines) */
static ULONG default_mode(void)
{
    ULONG m = am_flavour.mode_id;
    if (m != (ULONG)INVALID_ID)
        return m;
    m = BestModeID(BIDTAG_NominalWidth, W, BIDTAG_NominalHeight, H, BIDTAG_Depth, 8, TAG_DONE);
    if (m == (ULONG)INVALID_ID)
        m = BestModeID(BIDTAG_NominalWidth, W, BIDTAG_NominalHeight, 480, BIDTAG_Depth, 8, TAG_DONE);
    return m;
}

/* the plain 640x400 screen: 0 or -1 */
static int open_plain(ULONG mode, const char *title)
{
    int p;
    if (g_planar) {
        p = open_screen(mode, DEPTH, W, H, title, 1);
        if (p < 0)
            return -1;
        if (p == 0 && !(g_standard[0] && g_standard[1])) {
            /* a planar screen with a buffer that is not: that one takes the other upload */
            if (g_up->init(DEPTH, 0) != 0) {
                close_screen();
                return -1;
            }
            g_up_live = g_up;
        } else if (p == 1) {
            /* a graphics card screen: depth 8 with the engine colours on pens 16..31; a
             * depth 8 that comes out planar is not expected: back to depth 4 */
            if (open_with_upload(mode, 8, title) != 0 || g_standard[0] || g_standard[1]) {
                if (g_scr)
                    close_screen();
                if (open_with_upload(mode, DEPTH, title) != 0)
                    return -1;
            }
        }
    } else if (open_with_upload(mode, 8, title) != 0) {
        return -1;
    }
    return 0;
}

/* ---- SCALE and ASPECT -------------------------------------------------------------------- */

/* what kind of display `mode` is: opens a quiet 640x400 screen and closes it again before
 * any screen buffer exists (see open_screen); 1 = native planar, 0 = graphics card, -1 = none.
 * *w, *h: the size the screen came out (a graphics card mode may be larger) */
static int probe_mode(ULONG mode, int *w, int *h)
{
    struct Screen *s = OpenScreenTags(NULL, SA_DisplayID, mode, SA_Width, W, SA_Height, H,
                                      SA_Depth, g_planar ? DEPTH : 8, SA_Quiet, TRUE,
                                      SA_ShowTitle, FALSE, SA_Type, CUSTOMSCREEN, TAG_DONE);
    int native;
    if (!s)
        return -1;
    native = g_planar && (GetBitMapAttr(s->RastPort.BitMap, BMA_FLAGS) & BMF_STANDARD);
    *w = s->Width;
    *h = s->Height;
    CloseScreen(s);
    return native;
}

/* the pixel shape of a native mode from its DisplayInfo (PAL HiRes Laced 22:22); 1:1 if none */
static void mode_aspect(ULONG mode, unsigned *pw, unsigned *ph)
{
    struct DisplayInfo di;
    DisplayInfoHandle dh = FindDisplayInfo(mode);
    *pw = *ph = 1;
    if (dh && GetDisplayInfoData(dh, (UBYTE *)&di, sizeof di, DTAG_DISP, 0) &&
        di.Resolution.x > 0 && di.Resolution.y > 0) {
        *pw = (unsigned)di.Resolution.x;
        *ph = (unsigned)di.Resolution.y;
    }
}

/* the nominal height of a mode (0 if unknown) */
static int mode_height(ULONG mode)
{
    struct DimensionInfo dim;
    DisplayInfoHandle dh = FindDisplayInfo(mode);
    if (dh && GetDisplayInfoData(dh, (UBYTE *)&dim, sizeof dim, DTAG_DIMS, 0))
        return dim.Nominal.MaxY - dim.Nominal.MinY + 1;
    return 0;
}

/* the smallest available graphics card mode of at least w x h at depth 8 (INVALID_ID: none).
 * Where the flavour has a planar display, the native modes (monitor ids below 0x00100000:
 * default, NTSC, PAL, VGA, A2024, Euro, Super72, DblNTSC/DblPAL) are left out: they are not
 * what a scaled screen is for, and AGA's 1280-wide modes are planar. */
static ULONG board_mode(int w, int h)
{
    ULONG id = INVALID_ID, best = INVALID_ID, best_area = 0;
    while ((id = NextDisplayInfo(id)) != (ULONG)INVALID_ID) {
        struct DimensionInfo dim;
        struct DisplayInfo di;
        DisplayInfoHandle dh;
        ULONG mw, mh;
        if (g_planar && id < 0x00100000UL)
            continue;
        dh = FindDisplayInfo(id);
        if (!dh || !GetDisplayInfoData(dh, (UBYTE *)&dim, sizeof dim, DTAG_DIMS, 0) ||
            !GetDisplayInfoData(dh, (UBYTE *)&di, sizeof di, DTAG_DISP, 0))
            continue;
        mw = (ULONG)(dim.Nominal.MaxX - dim.Nominal.MinX + 1);
        mh = (ULONG)(dim.Nominal.MaxY - dim.Nominal.MinY + 1);
        if (di.NotAvailable || dim.MaxDepth < 8 || mw < (ULONG)w || mh < (ULONG)h)
            continue;
        if (best == (ULONG)INVALID_ID || mw * mh < best_area) {
            best = id;
            best_area = mw * mh;
        }
    }
    return best;
}

static void set_geom(int sx, int dst_h)
{
    am_geom.sx = sx;
    am_geom.dst_w = W * sx;
    am_geom.dst_h = dst_h;
    xe_rowmap_init(&am_geom.map, dst_h);
}

/* SCALE/ASPECT on a graphics card: square pixels (Picasso96's DisplayInfo assumes a 4:3
 * monitor for every mode, wrong for the LCDs these modes run on); the largest factor that a
 * mode holds. `mode` is the one asked for, *pw x *ph the size it opened (0 x 0: none, look for
 * a board mode). 0 = open, 1 = nothing to scale (the plain path) */
static int open_board(ULONG mode, int pw, int ph, int mode_given, const char *title)
{
    int sx, h;
    for (sx = am_opts.scale ? am_opts.scale : XE_SCALE_MAX; sx >= 1; sx--) {
        ULONG m = mode;
        int w = W * sx;
        h = am_opts.aspect ? xe_aspect_height(sx, 1, 1) : H * sx;
        if (sx == 1 && h == H)
            return 1;                       /* nothing left to do: the plain path */
        if (pw < w || ph < h) {
            if (mode_given) {
                am_log("video: mode 0x%08lx is %dx%d, too small for %dx%d\n",
                       (unsigned long)mode, pw, ph, w, h);
                continue;
            }
            m = board_mode(w, h);
            if (m == (ULONG)INVALID_ID)
                continue;
        }
        set_geom(sx, h);
        if (open_screen_upload(m, 8, w, h, title, &am_upload_scaled) == 0) {
            if (!g_standard[0] && !g_standard[1])
                return 0;
            close_screen();
        }
        am_log("video: no %dx%d screen in mode 0x%08lx\n", w, h, (unsigned long)m);
    }
    am_geom.sx = 0;
    return 1;
}

/* SCALE/ASPECT: 0 = a scaled screen is open, 1 = nothing to scale here (the plain path
 * opens), -1 = failure */
static int open_scaled(ULONG mode, const char *title, int mode_given)
{
    int pw_scr, ph_scr, kind, h;
    unsigned pw, ph;
    kind = probe_mode(mode, &pw_scr, &ph_scr);
    if (kind < 0) {
        am_log("video: cannot open screen mode 0x%08lx\n", (unsigned long)mode);
        return -1;
    }
    if (kind == 0)
        return open_board(mode, pw_scr, ph_scr, mode_given, title);
    /* native: a larger picture needs a graphics card mode (when MODE= does not insist) */
    if (am_opts.scale != 1) {
        if (!mode_given && open_board(mode, 0, 0, 0, title) == 0)
            return 0;
        am_log("video: SCALE needs a graphics card mode, 1x on this screen\n");
    }
    /* the aspect through the mode's pixel shape */
    if (!am_opts.aspect || !am_flavour.planar_map)
        return 1;
    mode_aspect(mode, &pw, &ph);
    h = xe_aspect_height(1, pw, ph);
    if (h == H) {
        am_log("video: mode 0x%08lx already shows 640x400 as 4:3\n", (unsigned long)mode);
        return 1;
    }
    if (mode_height(mode) < h) {
        am_log("video: mode 0x%08lx has no %d lines for ASPECT\n", (unsigned long)mode, h);
        return 1;
    }
    set_geom(1, h);
    if (open_screen_upload(mode, DEPTH, W, h, title, am_flavour.planar_map) != 0 ||
        !(g_standard[0] && g_standard[1])) {
        if (g_scr)
            close_screen();
        am_geom.sx = 0;
        return 1;
    }
    return 0;
}

int am_video_open(const struct xe_video_cfg *cfg)
{
    ULONG mode = cfg->mode_id ? cfg->mode_id : default_mode();
    const char *title = cfg->title ? cfg->title : "Xenon";
    int r = 1;

    if (pick_uploads() != 0)
        return -1;
    g_pen_base = 0;
    am_geom.sx = 0;
    g_window = 0;
    if (am_opts.window) {
        r = am_win_open(cfg);
        if (r <= 0) {
            g_window = r == 0;
            return r;
        }
        r = 1;                          /* the screen cannot take a window: an own screen */
    }
    if (am_opts.scale != 1 || am_opts.aspect) {
        r = open_scaled(mode, title, cfg->mode_id != 0);
        if (r < 0)
            return -1;
        if (r == 0) {
            /* every buffer takes the scaled screen's upload (upload() below) */
            g_planar = 0;
            g_up = g_up_live;
        }
    }
    if (r == 1 && open_plain(mode, title) != 0)
        return -1;
    g_truecolour = GetBitMapAttr(g_sb[0]->sb_BitMap, BMA_DEPTH) > (ULONG)g_depth;
    /* a mode larger than 640x400 (AROS: 640x480, VESA's single 1024x768) opens a larger
     * screen: centre the page (whole bytes across, for the planar upload) */
    if (am_geom.sx) {
        int w = am_geom.dst_w, h = am_geom.dst_h;
        g_dx = g_scr->Width > w ? ((g_scr->Width - w) / 2) & ~7 : 0;
        g_dy = g_scr->Height > h ? (g_scr->Height - h) / 2 : 0;
    } else {
        g_dx = g_scr->Width > W ? ((g_scr->Width - W) / 2) & ~7 : 0;
        g_dy = g_scr->Height > H ? (g_scr->Height - H) / 2 : 0;
    }
    am_log("video: mode 0x%08lx, %dx%d depth %d%s, %s upload\n",
           (unsigned long)GetVPModeID(&g_scr->ViewPort), g_scr->Width, g_scr->Height,
           g_depth, g_truecolour ? " (true colour underneath)" : "",
           g_standard[0] && g_planar ? g_planar->name : g_up->name);
    if (am_geom.sx)
        am_log("video: page shown %dx%d (%dx%s)\n", am_geom.dst_w, am_geom.dst_h, am_geom.sx,
               am_geom.dst_h != H * am_geom.sx ? ", 4:3" : "");
    if (g_pen_base) {
        /* all 256 pens black; set_palette loads 16..31 */
        static ULONG black[2 + 256 * 3];
        black[0] = 256UL << 16;
        LoadRGB32(&g_scr->ViewPort, black);
    }

    /* a Picasso96-promoted screen loses what was drawn into the second buffer before the
     * first flip: flip once now, both ways */
    ChangeScreenBuffer(g_scr, g_sb[1]);
    WaitTOF();
    ChangeScreenBuffer(g_scr, g_sb[0]);
    WaitTOF();
    g_shown = 0;
    g_first = 1;
    g_scroll[0] = g_scroll[1] = 0;
    g_pal_on = -1;

    if (am_input_open(g_scr) != 0) {
        am_video_close();
        return -1;
    }
    return 0;
}

void am_video_snapinfo(struct am_snapinfo *si)
{
    struct BitMap *bm;
    int i;
    if (g_window) {
        am_win_snapinfo(si);
        return;
    }
    memset(si, 0, sizeof *si);
    if (!g_scr)
        return;
    bm = g_sb[g_shown]->sb_BitMap;
    si->shown = g_shown;
    si->standard = g_standard[g_shown];
    si->bpr = bm->BytesPerRow;
    si->rows = bm->Rows;
    si->depth = bm->Depth;
    for (i = 0; i < 8 && i < bm->Depth; i++)
        si->planes[i] = bm->Planes[i];
    si->mode = GetVPModeID(&g_scr->ViewPort);
    si->bm = bm;
    si->pen_base = g_pen_base;
    si->dx = g_dx;
    si->dy = g_dy;
    si->sx = am_geom.sx;
    si->dst_h = am_geom.dst_h;
}

void am_video_close(void)
{
    ULONG calls, ms;
    if (g_window) {
        ms = am_prof_present_ms(&calls);
        am_log("video: %lu presents took %lu ms\n", (unsigned long)calls, (unsigned long)ms);
        am_win_close();
        g_window = 0;
        return;
    }
    am_input_close();
    ms = am_prof_present_ms(&calls);
    am_log("video: %lu full uploads, %lu flips (%lu failed), %lu dirty uploads, %lu palette loads; "
           "%lu presents took %lu ms\n",
           (unsigned long)g_n_full, (unsigned long)g_n_flip, (unsigned long)g_n_flipfail,
           (unsigned long)g_n_dirty, (unsigned long)g_n_pal, (unsigned long)calls, (unsigned long)ms);
    close_screen();
}

/* copy rows [y0,y1) x byte columns [x0,x1) of the page (rotated by `scroll` rows) into buffer idx */
static void upload(int idx, const struct xe_planes *page, int x0, int y0, int x1, int y1, int scroll)
{
    const struct am_upload *u = g_standard[idx] && g_planar ? g_planar : g_up;
    u->copy(g_sb[idx]->sb_BitMap, page, x0, y0, x1, y1, scroll, g_dx, g_dy);
}

void am_video_present(const struct xe_planes *page, const struct xe_rect *dirty, int page_switched,
                      int scroll_line, const uint8_t rgb4[48], int graphics_on)
{
    int full = g_first || page_switched || dirty == NULL;
    int target = g_shown;

    if (g_window) {
        am_win_present(page, dirty, page_switched, scroll_line, rgb4, graphics_on);
        return;
    }
    if (!g_scr)
        return;
    if (g_pal_on != graphics_on || memcmp(g_pal, rgb4, 48) != 0) {
        set_palette(rgb4, graphics_on);
        if (g_truecolour)
            full = 1;               /* the pixels already drawn keep their old colours */
    }

    if (page_switched && !g_first) {
        /* the original flips its display page register: upload the new page into the
         * hidden buffer and flip, so the change is instantaneous and tear-free */
        target = g_shown ^ 1;
        upload(target, page, 0, 0, BPR, H, scroll_line);
        g_scroll[target] = scroll_line;
        if (ChangeScreenBuffer(g_scr, g_sb[target])) {
            g_n_flip++;
            g_shown = target;
            g_first = 0;
            return;
        }
        g_n_flipfail++;
        target = g_shown;           /* could not flip: draw into the displayed buffer */
        full = 1;
    }
    if (scroll_line != g_scroll[target])
        full = 1;
    if (full) {
        g_n_full++;
        upload(target, page, 0, 0, BPR, H, scroll_line);
    } else if (!xe_rect_empty(dirty)) {
        g_n_dirty++;
        int x0 = dirty->x0 < 0 ? 0 : dirty->x0, y0 = dirty->y0 < 0 ? 0 : dirty->y0;
        int x1 = dirty->x1 > BPR ? BPR : dirty->x1, y1 = dirty->y1 > H ? H : dirty->y1;
        if (x1 > x0 && y1 > y0) {
            /* the rectangle is in page rows; page row r is shown at display row
             * (r - scroll) mod H, so a scrolled update may wrap around the bottom */
            int d0 = y0 - scroll_line, n = y1 - y0;
            if (d0 < 0)
                d0 += H;
            if (d0 + n <= H) {
                upload(target, page, x0, d0, x1, d0 + n, scroll_line);
            } else {
                upload(target, page, x0, d0, x1, H, scroll_line);
                upload(target, page, x0, 0, x1, d0 + n - H, scroll_line);
            }
        }
    }
    g_scroll[target] = scroll_line;
    g_first = 0;
}
