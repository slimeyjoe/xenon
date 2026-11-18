/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Text-layer overlay. The PC-98 text VRAM sits over the graphics; the engine uses it only
 * for the blinking page prompts (ESC[24;73H, i.e. cell 72,23) and the error / message boxes.
 * It is an overlay of lrvne's screen: the cells are drawn into the displayed page just before
 * it is presented (with save-under) and restored afterwards, so the engine's own drawing never
 * sees them. Colours are palette indices (index 15 for "white"). */
#include <string.h>
#include "xe.h"
#include "text/text.h"

#define BLINK_PERIOD 16     /* frames on / frames off */

static uint8_t saved[XE_OVERLAY_MAX][4][16 * 2];
static int applied;

void text_layer_clear(void)
{
    xe.overlay_count = 0;
}

void text_overlay_put(int col, int row, const uint8_t *s, int colour, int attr)
{
    struct lrv_text_ch ch;
    while (*s && xe.overlay_count < XE_OVERLAY_MAX) {
        struct xe_overlay_cell *c = &xe.overlay[xe.overlay_count++];
        s += text_next(s, &ch);
        c->col = (uint8_t)col;
        c->row = (uint8_t)row;
        c->colour = (uint8_t)colour;
        c->attr = (uint8_t)attr;
        c->code = (uint16_t)ch.code;
        c->wide = (uint8_t)(ch.cols == 2);
        col += ch.cols;
    }
}

/* The original positions text-layer output with printf("\e[%d;%dH", row, col) (text_locate
 * 0B7D:0A34). That cursor-position sequence is 1-based (home = 1;1), so "row 24, col 73" in
 * the code means cell (72, 23) counted from 0: y 368..383, inside the text box. */
void text_locate_put(int col1, int row1, const uint8_t *s, int colour, int attr)
{
    text_overlay_put(col1 - 1, row1 - 1, s, colour, attr);
}

/* the blink phase: a hook of the frame (LRV_HOOK_TICK) */
static void overlay_tick(void *unused)
{
    (void)unused;
    if (++xe.overlay_blink_counter >= BLINK_PERIOD) {
        xe.overlay_blink_counter = 0;
        xe.overlay_blink_on = !xe.overlay_blink_on;
    }
}

/* draw one cell with save-under into the displayed page */
static void draw_cell(struct lrv_surface *pl, int k)
{
    const struct xe_overlay_cell *c = &xe.overlay[k];
    int wb = c->wide ? 2 : 1;
    int off = c->row * 16 * XE_STRIDE + c->col;
    int p, y, b;
    const uint8_t *g;
    int fg = c->colour, bg = 0;
    static const uint8_t blank16[32];

    if (c->attr & XE_OVL_REVERSE) {
        bg = c->colour;
        fg = 0;
    }
    if (off + 15 * XE_STRIDE + wb > XE_PLANE_BYTES)
        return;
    for (p = 0; p < 4; p++)
        for (y = 0; y < 16; y++)
            for (b = 0; b < wb; b++)
                saved[k][p][y * 2 + b] = pl->plane[p][off + y * XE_STRIDE + b];
    if ((c->attr & XE_OVL_BLINK) && !xe.overlay_blink_on)
        return;                                   /* blink phase off: cell stays as is */
    g = c->wide ? lrv_font_glyph(&xe.font16, c->code)
                : c->code == ' ' ? blank16 : lrv_font_glyph(&xe.font8, c->code);
    lrv_glyph_blit(pl, c->col, c->row * 16, g ? g : blank16, wb, 16, fg, bg, 15, 0);
}

static void overlay_apply(void *ctx, struct lrv_surface *page, struct lrv_rect *dirty)
{
    int k;
    (void)ctx;
    if (applied)
        return;
    for (k = 0; k < xe.overlay_count; k++) {
        const struct xe_overlay_cell *c = &xe.overlay[k];
        draw_cell(page, k);
        lrv_rect_add(dirty, c->col, c->row * 16, c->col + (c->wide ? 2 : 1), c->row * 16 + 16);
    }
    applied = xe.overlay_count > 0;
}

static void overlay_restore(void *ctx, struct lrv_surface *page, struct lrv_rect *dirty)
{
    int k;
    (void)ctx;
    if (!applied)
        return;
    for (k = xe.overlay_count - 1; k >= 0; k--) {
        const struct xe_overlay_cell *c = &xe.overlay[k];
        int wb = c->wide ? 2 : 1;
        int off = c->row * 16 * XE_STRIDE + c->col;
        int p, y, b;
        if (off + 15 * XE_STRIDE + wb > XE_PLANE_BYTES)
            continue;
        for (p = 0; p < 4; p++)
            for (y = 0; y < 16; y++)
                for (b = 0; b < wb; b++)
                    page->plane[p][off + y * XE_STRIDE + b] = saved[k][p][y * 2 + b];
        /* the restored area must be uploaded again next time */
        lrv_rect_add(dirty, c->col, c->row * 16, c->col + wb, c->row * 16 + 16);
    }
    applied = 0;
}

void text_layer_init(void)
{
    xe.overlay_count = 0;
    lrv_screen_add_overlay(&xe.screen, overlay_apply, overlay_restore, 0);
    lrv_sys_add_hook(&xe.sys, LRV_HOOK_TICK, overlay_tick, 0);
}

/* error_box 088D:13DF (engine.md §5.4): text layer cleared, graphics off, beep, the message in
 * reverse red centred on row 10, key wait. Non-fatal. */
void error_box(const uint8_t *msg)
{
    int cols = text_cols(msg), i;
    int was_on = xe.screen.display_on;
    text_layer_clear();
    lrv_screen_set_display_on(&xe.screen, 0);
    lrv_log(xe.hal, LRV_LOG_WARN, "error box: %s", (const char *)msg);
    lrv_sys_trace(&xe.sys, XE_TR_BOX, 0, msg, 1, 0, 0, 0);
    /* beep: sound(500) / delay(500) / nosound - 28 frames; audio side TODO */
    /* 088D:1412: text_locate((80 - strlen) / 2 + 1, 10), 1-based */
    text_locate_put((80 - cols) / 2 + 1, 10, msg, 15, XE_OVL_REVERSE);
    for (i = 0; i < 28; i++)
        frame_wait();
    wait_ok_key();
    text_layer_clear();
    lrv_screen_set_display_on(&xe.screen, was_on);
}

/* msg_box 0B7D:07DF: text_locate(24, 22) (1-based), print, wait for a key, clear */
void msg_box(const uint8_t *msg)
{
    lrv_sys_trace(&xe.sys, XE_TR_BOX, 0, msg, 0, 0, 0, 0);
    text_locate_put(24, 22, msg, 15, 0);
    wait_ok_key();
    text_layer_clear();
}
