/* The three canned sequences of segment 0E31 (engine.md §4): logo, opening, staff roll. */
#include <string.h>
#include "xe_internal.h"
#include "game/game.h"
#include "gfx/gfx.h"
#include "gfx/adt.h"
#include "snd/snd.h"
#include "sys.h"

#define STRIDE XE_PLANE_STRIDE

/* seq_slide_up_iplane 0E31:002D: for k = 0, step, .. <= h copy buffer rows 0..k of plane 3
 * only to VRAM rows h-1-k .. h-1 of the rect, one frame per step; full = copy all 4 planes
 * afterwards. */
static void seq_slide_up_iplane(int off, int w, int h, int step, int full)
{
    int k;
    for (k = 0; k <= h; k += step) {
        int y;
        struct xe_planes *pg = gfx_access();
        for (y = 0; y <= k && y < h; y++) {
            int dst = off + (h - 1 - k + y) * STRIDE, src = off + y * STRIDE;
            if (dst < 0 || dst + w > XE_PLANE_BYTES)
                continue;
            memcpy(pg->p[3] + dst, xe.gbuf.p[3] + src, (size_t)w);
        }
        gfx_mark_dirty_off(xe.access_page, off, w, h);
        sys_wait_ticks(1);
    }
    if (full) {
        gfx_copy_rows(gfx_access(), &xe.gbuf, off, w, h);
        gfx_mark_dirty_off(xe.access_page, off, w, h);
    }
}

/* seq_slide_up 0E31:0110: the same rising reveal with all four planes, k < h */
static void seq_slide_up(int off, int w, int h, int step)
{
    int k;
    for (k = 0; k < h; k += step) {
        int y, p;
        struct xe_planes *pg = gfx_access();
        for (y = 0; y <= k && y < h; y++) {
            int dst = off + (h - 1 - k + y) * STRIDE, src = off + y * STRIDE;
            if (dst < 0 || dst + w > XE_PLANE_BYTES)
                continue;
            for (p = 0; p < 4; p++)
                memcpy(pg->p[p] + dst, xe.gbuf.p[p] + src, (size_t)w);
        }
        gfx_mark_dirty_off(xe.access_page, off, w, h);
        sys_wait_ticks(1);
    }
}

static void copy_rect(int off, int w, int h)
{
    gfx_copy_rows(gfx_access(), &xe.gbuf, off, w, h);
    gfx_mark_dirty_off(xe.access_page, off, w, h);
}

/* ---- logo 0E31:03FA ------------------------------------------------------------------------ */

static const int hidden[5] = { 5, 6, 7, 9, 10 };

static void seq_logo_body(void)
{
    uint8_t work[48];
    int i;

    gdt_load("rogo");                               /* palette set at once, screen blank */
    for (i = 0; i < 5; i++)
        pal_set(hidden[i], 0, 0, 0);
    /* logo_show_mark: work palette = target with 5-7,9,10 black; black-out; left part of
     * the mark; fade in */
    memcpy(work, xe.pal_target, 48);
    for (i = 0; i < 5; i++)
        work[hidden[i] * 3] = work[hidden[i] * 3 + 1] = work[hidden[i] * 3 + 2] = 0;
    pal_fade_black(0);
    copy_rect(0x17, 17, 249);
    pal_fade_to(work, 6);

    seq_slide_up_iplane(0x1839, 14, 176, 8, 1);
    sys_wait_ticks(1);
    copy_rect(0x28, 14, 75);                        /* logo_show_top */
    se_play(46);
    /* logo_flash_white: hidden colours up to white, then the true palette */
    memcpy(work, xe.pal_target, 48);
    for (i = 0; i < 5; i++)
        work[hidden[i] * 3] = work[hidden[i] * 3 + 1] = work[hidden[i] * 3 + 2] = 15;
    pal_fade_to(work, 1);
    pal_fade_to(xe.pal_target, 1);
    sys_wait_ticks(40);
    seq_slide_up(0x5B04, 40, 43, 2);
    sys_wait_ticks(120);
    pal_fade_black(3);
    gfx_gvram_clear();
}

void seq_logo(void)
{
    sys_trace(XE_TR_SEQ, 0, 0, 0, 0, 0, 0);
    seq_logo_body();
    sys_trace(XE_TR_SEQ, 0, 0, 0, 1, 0, 0);
}


/* ---- opening 0E31:04AE (OPENING n) --------------------------------------------------------- */

static void seq_opening_body(int n)
{
    gfx_clear_buffers();
    gdt_load("0100b");
    fx_dissolve();
    gfx_set_access_page(1);
    gfx_gvram_clear();
    gfx_show();
    gfx_set_access_page(0);
    adt_load("0100b");
    music_fade(20);
    adt_play();                                     /* ends with page 1 displayed */
    adt_free();
    music_load("xen_03a");
    music_play();
    pal_fade_white(0);
    sys_wait_ticks(6);
    pal_fade_to(xe.pal_target, 0);
    sys_wait_ticks(60);
    music_load("xen_03b");
    if (n == 1)
        gdt_load("0100bb");
    else
        gdt_load("xenon2");
    music_play();
    pal_fade_white(0);
    gfx_set_access_page(1);                         /* overlay drawn into the displayed page 1 */
    gfx_show();
    sys_wait_ticks(6);
    pal_fade_to(xe.pal_target, 0);
    sys_wait_ok_key();
    music_fade(20);
    pal_fade_black(4);
    gfx_set_access_page(1); gfx_gvram_clear();
    gfx_set_access_page(0); gfx_gvram_clear();
    gfx_set_display_page(0);
    music_stop();
}

void seq_opening(int n)
{
    sys_trace(XE_TR_SEQ, 0, 0, 1, 0, n, 0);
    seq_opening_body(n);
    sys_trace(XE_TR_SEQ, 0, 0, 1, 1, n, 0);
}


/* ---- staff roll 0E31:05D2 ------------------------------------------------------------------ */

static void seq_staff_roll_body(void)
{
    int i;
    music_load("xenon_02");
    music_play();
    sys_wait_ticks(120);
    gfx_graph_on(0);
    gdt_load("st00");
    gfx_show();
    music_wait_status(0);
    gfx_graph_on(1);
    sys_wait_ticks(420);
    for (i = 1; i <= 9; i++) {
        char name[8];
        name[0] = 's'; name[1] = 't'; name[2] = '0'; name[3] = (char)('0' + i); name[4] = 0;
        pal_fade_black(4);
        gfx_gvram_clear();
        gdt_load(name);
        pal_fade_black(0);
        gfx_show();
        sys_wait_ticks(240);
        pal_fade_to(xe.pal_target, 4);
        sys_wait_ticks(420);
    }
    sys_wait_ticks(180);
    pal_fade_black(4);
    gfx_gvram_clear();
}

void seq_staff_roll(void)
{
    sys_trace(XE_TR_SEQ, 0, 0, 2, 0, 0, 0);
    seq_staff_roll_body();
    sys_trace(XE_TR_SEQ, 0, 0, 2, 1, 0, 0);
}

