/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Glyph drawing - port of segment 0F16 (font.md §2-3) on lrvne's glyph tables (xe.font16,
 * xe.font8). The glyph bitmaps in the pack are already emboldened the way the engine does it. */
#include <string.h>
#include "xe.h"
#include "text/text.h"

/* Opaque cell (font.md §2): `wbytes` = 1 or 2 (or 4 for the 32x32 glyph), rows = 16 or 32;
 * glyph rows are wbytes wide. */
static void put_cell(int x, int y, int fg, int bg, const uint8_t *glyph, int wbytes, int rows)
{
    struct lrv_rect r;
    lrv_rect_clear(&r);
    lrv_glyph_blit(lrv_screen_access(&xe.screen), x, y, glyph, wbytes, rows, fg, bg, 15, &r);
    if (!lrv_rect_empty(&r))
        lrv_screen_mark(&xe.screen, xe.screen.access_page, r.x0, r.y0, r.x1, r.y1);
}

void kanji_draw(int x, int y, int fg, int bg, unsigned code, int wait)
{
    const uint8_t *g = lrv_font_glyph(&xe.font16, code);
    static const uint8_t blank[32];
    if (wait)
        wait_vsync(wait);
    put_cell(x, y, fg, bg, g ? g : blank, 2, 16);
}

/* kanji_draw_big 0F16:051D: 32x32 at (x, y-16), always colour 15 on 0 */
void kanji_draw_big(int x, int y, unsigned code, int wait)
{
    const uint8_t *g = lrv_font_glyph(&xe.font16, code);
    uint8_t big[32 * 4];
    int r, k;
    if (!g)
        return;
    for (r = 0; r < 16; r++) {
        unsigned row = (unsigned)(g[2 * r] << 8) | g[2 * r + 1];
        unsigned out = 0;
        for (k = 0; k < 16; k++)
            if (row & (0x8000u >> k))
                out |= 3u << (30 - 2 * k);
        big[(2 * r) * 4] = big[(2 * r + 1) * 4] = (uint8_t)(out >> 24);
        big[(2 * r) * 4 + 1] = big[(2 * r + 1) * 4 + 1] = (uint8_t)(out >> 16);
        big[(2 * r) * 4 + 2] = big[(2 * r + 1) * 4 + 2] = (uint8_t)(out >> 8);
        big[(2 * r) * 4 + 3] = big[(2 * r + 1) * 4 + 3] = (uint8_t)out;
    }
    if (wait)
        wait_vsync(wait);
    put_cell(x, y - 16, 15, 0, big, 4, 32);
}

void ank_draw(int x, int y, int fg, int bg, unsigned code)
{
    static const uint8_t blank[16];
    const uint8_t *g = (code == 0x20) ? blank : lrv_font_glyph(&xe.font8, code);
    put_cell(x, y, fg, bg, g ? g : blank, 1, 16);
}

/* text_draw_string 0F16:07D5 */
void text_draw_string(int x, int y, int fg, int bg, const uint8_t *s, int wait)
{
    struct lrv_text_ch ch;
    while (*s) {
        s += text_next(s, &ch);
        if (ch.cols == 2)
            kanji_draw(x, y, fg, bg, ch.code, wait);
        else
            ank_draw(x, y, fg, bg, ch.code);
        x += ch.cols;
    }
}
