/* Text-layer overlay. The PC-98 text VRAM sits over the graphics; the engine uses it only
 * for the blinking page prompts (ESC[24;73H, i.e. cell 72,23) and the error / message boxes. The overlay
 * cells are drawn into the displayed page just before presenting (with save-under) and
 * restored afterwards, so the engine's own drawing never sees them. Colours are palette
 * indices (index 15 for "white"); a backend with a real second layer could do better. */
#include <string.h>
#include "xe_internal.h"
#include "gfx/gfx.h"
#include "text/text.h"
#include "sys.h"

#define BLINK_PERIOD 16     /* frames on / frames off */

static uint8_t saved[XE_OVERLAY_MAX][4][16 * 2];
static int applied;
static int blink_counter;

void text_layer_clear(void)
{
    xe.overlay_count = 0;
}

void text_overlay_put(int col, int row, const uint8_t *s, int colour, int attr)
{
    struct text_ch ch;
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
 * the code means cell (72, 23) counted from 0: y 368..383, inside the WAKU text box. */
void text_locate_put(int col1, int row1, const uint8_t *s, int colour, int attr)
{
    text_overlay_put(col1 - 1, row1 - 1, s, colour, attr);
}

void text_overlay_tick(void)
{
    if (++blink_counter >= BLINK_PERIOD) {
        blink_counter = 0;
        xe.overlay_blink_on = !xe.overlay_blink_on;
    }
}

/* draw one cell with save-under on page `pg` (planes), without touching dirty tracking of the
 * engine (the whole cell area is marked dirty for the upload) */
static void draw_cell(int page, int k)
{
    const struct xe_overlay_cell *c = &xe.overlay[k];
    struct xe_planes *pl = &xe.page[page];
    int wb = c->wide ? 2 : 1;
    int off = c->row * 16 * XE_PLANE_STRIDE + c->col;
    int p, y, b;
    const uint8_t *g;
    int fg = c->colour, bg = 0;
    static const uint8_t blank16[32];

    if (c->attr & XE_OVL_REVERSE) {
        bg = c->colour;
        fg = 0;
    }
    if (off + 15 * XE_PLANE_STRIDE + wb > XE_PLANE_BYTES)
        return;
    for (p = 0; p < 4; p++)
        for (y = 0; y < 16; y++)
            for (b = 0; b < wb; b++)
                saved[k][p][y * 2 + b] = pl->p[p][off + y * XE_PLANE_STRIDE + b];
    if ((c->attr & XE_OVL_BLINK) && !xe.overlay_blink_on)
        return;                                   /* blink phase off: cell stays as is */
    g = c->wide ? font_lookup16(c->code) : (c->code == ' ' ? blank16 : font_lookup8(c->code));
    if (!g)
        g = blank16;
    for (p = 0; p < 4; p++) {
        int fgb = (fg >> p) & 1, bgb = (bg >> p) & 1;
        for (y = 0; y < 16; y++)
            for (b = 0; b < wb; b++) {
                uint8_t cell = bgb ? 0xFF : 0x00;
                uint8_t gl = g[y * wb + b];
                pl->p[p][off + y * XE_PLANE_STRIDE + b] = (uint8_t)(fgb ? (cell | gl) : (cell & ~gl));
            }
    }
}

void text_overlay_apply(int page)
{
    int k;
    if (applied)
        return;
    for (k = 0; k < xe.overlay_count; k++) {
        draw_cell(page, k);
        gfx_mark_dirty(page, xe.overlay[k].col, xe.overlay[k].row * 16,
                       xe.overlay[k].col + (xe.overlay[k].wide ? 2 : 1), xe.overlay[k].row * 16 + 16);
    }
    applied = xe.overlay_count > 0;
}

void text_overlay_restore(int page)
{
    int k;
    if (!applied)
        return;
    for (k = xe.overlay_count - 1; k >= 0; k--) {
        const struct xe_overlay_cell *c = &xe.overlay[k];
        struct xe_planes *pl = &xe.page[page];
        int wb = c->wide ? 2 : 1;
        int off = c->row * 16 * XE_PLANE_STRIDE + c->col;
        int p, y, b;
        if (off + 15 * XE_PLANE_STRIDE + wb > XE_PLANE_BYTES)
            continue;
        for (p = 0; p < 4; p++)
            for (y = 0; y < 16; y++)
                for (b = 0; b < wb; b++)
                    pl->p[p][off + y * XE_PLANE_STRIDE + b] = saved[k][p][y * 2 + b];
        /* the restored area must be uploaded again next time */
        gfx_mark_dirty(page, c->col, c->row * 16, c->col + wb, c->row * 16 + 16);
    }
    applied = 0;
}

/* error_box 088D:13DF (engine.md §5.4): text layer cleared, graphics off, beep, the message in
 * reverse red centred on row 10, key wait. Non-fatal. */
void error_box(const uint8_t *msg)
{
    int cols = text_cols(msg), i;
    int was_on = xe.graphics_on;
    text_layer_clear();
    gfx_graph_on(0);
    sys_log(XE_LOG_WARN, "error box: %s", (const char *)msg);
    sys_trace(XE_TR_BOX, 0, msg, 1, 0, 0, 0);
    /* beep: sound(500) / delay(500) / nosound - 28 frames; audio side TODO */
    /* 088D:1412: text_locate((80 - strlen) / 2 + 1, 10), 1-based */
    text_locate_put((80 - cols) / 2 + 1, 10, msg, 15, XE_OVL_REVERSE);
    for (i = 0; i < 28; i++)
        sys_frame_wait();
    sys_wait_ok_key();
    text_layer_clear();
    gfx_graph_on(was_on);
}

/* msg_box 0B7D:07DF: text_locate(24, 22) (1-based), print, wait for a key, clear */
void msg_box(const uint8_t *msg)
{
    sys_trace(XE_TR_BOX, 0, msg, 0, 0, 0, 0);
    text_locate_put(24, 22, msg, 15, 0);
    sys_wait_ok_key();
    text_layer_clear();
}
