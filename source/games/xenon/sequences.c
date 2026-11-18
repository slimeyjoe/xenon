/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Xenon's three canned sequences, segment 0E31 of its AGS.EXE (docs/re/engine.md §4): the
 * logo, the opening and the staff roll, with the pictures, animation, songs and sound effect
 * they name. Native modules of the hooks XE_HOOK_LOGO, _OPENING and _STAFF_ROLL. */
#include <string.h>
#include "xe.h"
#include "gfx/gfx.h"
#include "gfx/adt.h"
#include "snd/snd.h"
#include "xenon.h"

#define STRIDE XE_STRIDE

/* seq_slide_up_iplane 0E31:002D: for k = 0, step, .. <= h copy buffer rows 0..k of plane 3
 * only to VRAM rows h-1-k .. h-1 of the rect, one frame per step; full = copy all 4 planes
 * afterwards. */
static void seq_slide_up_iplane(int off, int w, int h, int step, int full)
{
    int k;
    for (k = 0; k <= h; k += step) {
        int y;
        struct lrv_surface *pg = lrv_screen_access(&xe.screen), *buf = XE_GBUF;
        for (y = 0; y <= k && y < h; y++) {
            int dst = off + (h - 1 - k + y) * STRIDE, src = off + y * STRIDE;
            if (dst < 0 || dst + w > XE_PLANE_BYTES)
                continue;
            memcpy(pg->plane[3] + dst, buf->plane[3] + src, (size_t)w);
        }
        lrv_screen_mark_off(&xe.screen, xe.screen.access_page, off, w, h);
        wait_vsync(1);
    }
    if (full) {
        lrv_surface_copy_rows(lrv_screen_access(&xe.screen), XE_GBUF, off, w, h);
        lrv_screen_mark_off(&xe.screen, xe.screen.access_page, off, w, h);
    }
}

/* seq_slide_up 0E31:0110: the same rising reveal with all four planes, k < h */
static void seq_slide_up(int off, int w, int h, int step)
{
    int k;
    for (k = 0; k < h; k += step) {
        int y, p;
        struct lrv_surface *pg = lrv_screen_access(&xe.screen), *buf = XE_GBUF;
        for (y = 0; y <= k && y < h; y++) {
            int dst = off + (h - 1 - k + y) * STRIDE, src = off + y * STRIDE;
            if (dst < 0 || dst + w > XE_PLANE_BYTES)
                continue;
            for (p = 0; p < 4; p++)
                memcpy(pg->plane[p] + dst, buf->plane[p] + src, (size_t)w);
        }
        lrv_screen_mark_off(&xe.screen, xe.screen.access_page, off, w, h);
        wait_vsync(1);
    }
}

/* ---- logo 0E31:03FA ------------------------------------------------------------------------ */

static const int hidden[5] = { 5, 6, 7, 9, 10 };

static void seq_logo_body(void)
{
    uint8_t work[48];
    int i;

    gdt_load("rogo");                               /* palette set at once, screen blank */
    for (i = 0; i < 5; i++)
        lrv_screen_set_colour(&xe.screen, hidden[i], 0, 0, 0);
    /* logo_show_mark: work palette = target with 5-7,9,10 black; black-out; left part of
     * the mark; fade in */
    memcpy(work, xe.pal_target, 48);
    for (i = 0; i < 5; i++)
        work[hidden[i] * 3] = work[hidden[i] * 3 + 1] = work[hidden[i] * 3 + 2] = 0;
    pal_fade_black(0);
    gfx_show_rows(0x17, 17, 249);
    pal_fade_to(work, 6);

    seq_slide_up_iplane(0x1839, 14, 176, 8, 1);
    wait_vsync(1);
    gfx_show_rows(0x28, 14, 75);                        /* logo_show_top */
    se_play(46);
    /* logo_flash_white: hidden colours up to white, then the true palette */
    memcpy(work, xe.pal_target, 48);
    for (i = 0; i < 5; i++)
        work[hidden[i] * 3] = work[hidden[i] * 3 + 1] = work[hidden[i] * 3 + 2] = 15;
    pal_fade_to(work, 1);
    pal_fade_to(xe.pal_target, 1);
    wait_vsync(40);
    seq_slide_up(0x5B04, 40, 43, 2);
    wait_vsync(120);
    pal_fade_black(3);
    gvram_clear();
}

void xenon_logo(int arg)
{
    (void)arg;
    seq_logo_body();
}


/* ---- opening 0E31:04AE (OPENING n) --------------------------------------------------------- */

static void seq_opening_body(int n)
{
    gbuf_clear();
    gdt_load("0100b");
    fx_dissolve();
    lrv_screen_set_access(&xe.screen, 1);
    gvram_clear();
    gfx_show();
    lrv_screen_set_access(&xe.screen, 0);
    adt_load("0100b");
    music_fade(20);
    adt_play();                                     /* ends with page 1 displayed */
    adt_free();
    music_load("xen_03a");
    music_play();
    pal_fade_white(0);
    wait_vsync(6);
    pal_fade_to(xe.pal_target, 0);
    wait_vsync(60);
    music_load("xen_03b");
    if (n == 1)
        gdt_load("0100bb");
    else
        gdt_load("xenon2");
    music_play();
    pal_fade_white(0);
    lrv_screen_set_access(&xe.screen, 1);                         /* overlay drawn into the displayed page 1 */
    gfx_show();
    wait_vsync(6);
    pal_fade_to(xe.pal_target, 0);
    wait_ok_key();
    music_fade(20);
    pal_fade_black(4);
    lrv_screen_set_access(&xe.screen, 1); gvram_clear();
    lrv_screen_set_access(&xe.screen, 0); gvram_clear();
    lrv_screen_set_display(&xe.screen, 0);
    music_stop();
}

void xenon_opening(int n)
{
    seq_opening_body(n);
}


/* ---- staff roll 0E31:05D2 ------------------------------------------------------------------ */

static void seq_staff_roll_body(void)
{
    int i;
    music_load("xenon_02");
    music_play();
    wait_vsync(120);
    lrv_screen_set_display_on(&xe.screen, 0);
    gdt_load("st00");
    gfx_show();
    music_wait_status(0);
    lrv_screen_set_display_on(&xe.screen, 1);
    wait_vsync(420);
    for (i = 1; i <= 9; i++) {
        char name[8];
        name[0] = 's'; name[1] = 't'; name[2] = '0'; name[3] = (char)('0' + i); name[4] = 0;
        pal_fade_black(4);
        gvram_clear();
        gdt_load(name);
        pal_fade_black(0);
        gfx_show();
        wait_vsync(240);
        pal_fade_to(xe.pal_target, 4);
        wait_vsync(420);
    }
    wait_vsync(180);
    pal_fade_black(4);
    gvram_clear();
}

void xenon_staff_roll(int arg)
{
    (void)arg;
    seq_staff_roll_body();
}

