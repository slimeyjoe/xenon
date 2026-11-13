/* Fonts (XFNT tables from the pack) and glyph drawing - port of segment 0F16 (font.md §2-3).
 * The glyph bitmaps in the pack are already emboldened the way the engine does it. */
#include <string.h>
#include "xe_internal.h"
#include "gfx/gfx.h"
#include "text/text.h"
#include "util/endian.h"

#define STRIDE XE_PLANE_STRIDE

static int font_load(struct xe_font *f, const char *name)
{
    const struct xpak_entry *e = xpak_find(&xe.pak, name, XPAK_T_FONT);
    if (!e)
        return 0;
    f->data = xpak_load(&xe.pak, e, &f->size);
    if (!f->data || f->size < 12 || memcmp(f->data, "XFNT", 4) != 0)
        return 0;
    f->cell_w = f->data[5];
    f->cell_h = f->data[6];
    f->count = rd16le(f->data + 8);
    f->codes = f->data + 12;
    f->glyph_bytes = f->cell_h * f->cell_w / 8;
    f->glyphs = f->codes + 2 * f->count;
    if (12u + (uint32_t)f->count * (2u + (uint32_t)f->glyph_bytes) > f->size)
        return 0;
    return 1;
}

static const uint8_t *font_glyph(const struct xe_font *f, unsigned code)
{
    int lo = 0, hi = f->count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        unsigned c = rd16le(f->codes + 2 * mid);
        if (c == code)
            return f->glyphs + mid * f->glyph_bytes;
        if (c < code)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return 0;
}

void font_init(void)
{
    if (!font_load(&xe.font16, "font16"))
        sys_fatal("font16 missing from the pack");
    if (!font_load(&xe.font8, "font8"))
        sys_fatal("font8 missing from the pack");
}

void font_shutdown(void)
{
    if (xe.font16.data)
        xpak_free(&xe.pak, xe.font16.data, xe.font16.size);
    if (xe.font8.data)
        xpak_free(&xe.pak, xe.font8.data, xe.font8.size);
    xe.font16.data = xe.font8.data = 0;
}

/* Opaque cell: fill with bg, then fg where the glyph is set (font.md §2). `wbytes` = 1 or 2
 * (or 4 for the 32x32 glyph), rows = 16 or 32; glyph rows are wbytes wide. */
static void put_cell(int x, int y, int fg, int bg, const uint8_t *glyph, int wbytes, int rows)
{
    struct xe_planes *pg = gfx_access();
    int p, r, k;
    int off = y * STRIDE + x;
    if (off < 0 || off + (rows - 1) * STRIDE + wbytes > XE_PLANE_BYTES)
        return;
    for (p = 0; p < 4; p++) {
        uint8_t *d = pg->p[p] + off;
        int fgb = (fg >> p) & 1, bgb = (bg >> p) & 1;
        for (r = 0; r < rows; r++) {
            const uint8_t *g = glyph + r * wbytes;
            for (k = 0; k < wbytes; k++) {
                uint8_t cell = bgb ? 0xFF : 0x00;
                d[k] = (uint8_t)(fgb ? (cell | g[k]) : (cell & ~g[k]));
            }
            d += STRIDE;
        }
    }
    gfx_mark_dirty(xe.access_page, x, y, x + wbytes, y + rows);
}

void kanji_draw(int x, int y, int fg, int bg, unsigned code, int wait)
{
    const uint8_t *g = font_glyph(&xe.font16, code);
    static const uint8_t blank[32];
    if (wait)
        sys_wait_ticks(wait);
    put_cell(x, y, fg, bg, g ? g : blank, 2, 16);
}

/* kanji_draw_big 0F16:051D: 32x32 at (x, y-16), always colour 15 on 0 */
void kanji_draw_big(int x, int y, unsigned code, int wait)
{
    const uint8_t *g = font_glyph(&xe.font16, code);
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
        sys_wait_ticks(wait);
    put_cell(x, y - 16, 15, 0, big, 4, 32);
}

void ank_draw(int x, int y, int fg, int bg, unsigned code)
{
    static const uint8_t blank[16];
    const uint8_t *g = (code == 0x20) ? blank : font_glyph(&xe.font8, code);
    put_cell(x, y, fg, bg, g ? g : blank, 1, 16);
}

/* text_draw_string 0F16:07D5 */
void text_draw_string(int x, int y, int fg, int bg, const uint8_t *s, int wait)
{
    struct text_ch ch;
    while (*s) {
        s += text_next(s, &ch);
        if (ch.cols == 2)
            kanji_draw(x, y, fg, bg, ch.code, wait);
        else
            ank_draw(x, y, fg, bg, ch.code);
        x += ch.cols;
    }
}

/* lookups for the overlay */
const uint8_t *font_lookup16(unsigned code)
{
    return font_glyph(&xe.font16, code);
}

const uint8_t *font_lookup8(unsigned code)
{
    return font_glyph(&xe.font8, code);
}
