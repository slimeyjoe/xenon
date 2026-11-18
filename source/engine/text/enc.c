/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The character stream of the current language's text. Every text consumer (printer, menus,
 * overlay, boxes) walks strings through text_next(), so the encoding is decided in one place.
 *
 * cp932 (Japanese, the original bytecode): a lead byte with a trail byte is one 2-column glyph,
 * keyed by its JIS X 0208 code in font16; any other byte is one 1-column glyph keyed by the
 * byte (JIS X 0201) in font8 (lrv_text_next_sjis).
 *
 * UTF-8 (translations): both fonts of the language are keyed by Unicode code point; a code
 * point is 1 column if it is ASCII or font8 has a glyph for it, otherwise 2 columns
 * (lrv_text_next_utf8). mkpack.py guarantees that every character of the language's texts has
 * a glyph in one of the two. */
#include <string.h>
#include "xe.h"
#include "text/text.h"

int text_next(const uint8_t *s, struct lrv_text_ch *ch)
{
    if (xe.text_enc == XE_ENC_UTF8)
        return lrv_text_next_utf8(s, &xe.font8, ch);
    return lrv_text_next_sjis(s, ch);
}

int text_cols(const uint8_t *s)
{
    struct lrv_text_ch ch;
    int n = 0;
    while (*s) {
        s += text_next(s, &ch);
        n += ch.cols;
    }
    return n;
}

/* The two prompt glyphs of the message printer (DS:08D2 ▼, DS:08B1 the user-defined return
 * glyph JIS 7621h), in the current encoding. UTF-8 fonts carry the return glyph at
 * XE_U_RETURN (Private Use Area). */
const uint8_t *text_prompt_str(int overflow)
{
    static const uint8_t sj_tri[3] = { 0x81, 0xA5, 0 };
    static const uint8_t sj_ret[3] = { 0xEB, 0x9F, 0 };
    static const uint8_t u8_tri[4] = { 0xE2, 0x96, 0xBC, 0 };     /* U+25BC */
    static const uint8_t u8_ret[4] = { 0xEE, 0x80, 0x80, 0 };     /* U+E000 = XE_U_RETURN */
    if (xe.text_enc == XE_ENC_UTF8)
        return overflow ? u8_ret : u8_tri;
    return overflow ? sj_ret : sj_tri;
}

/* Per-language text settings: the pack entry "lang" (type meta) of the language, key=value
 * lines `encoding=cp932|utf-8` and `wrap=kinsoku|word`. Without it: the original's, cp932 and
 * kinsoku. */
void text_enc_init(void)
{
    const struct lrv_pack_entry *e = lrv_pack_find(&xe.pack, "lang", LRV_PT_META);
    uint8_t *blob;
    uint32_t size;
    char v[16];

    xe.text_enc = XE_ENC_CP932;
    xe.wrap_mode = XE_WRAP_JA;
    if (!e || !(blob = lrv_pack_load(&xe.pack, e, &size)))
        return;
    if (lrv_meta_get(blob, size, "encoding", v, sizeof v) != LRV_ERR_NOTFOUND) {
        if (!strcmp(v, "utf-8"))
            xe.text_enc = XE_ENC_UTF8;
        else if (strcmp(v, "cp932"))
            lrv_log(xe.hal, LRV_LOG_WARN, "lang: unknown encoding '%s'", v);
    }
    if (lrv_meta_get(blob, size, "wrap", v, sizeof v) != LRV_ERR_NOTFOUND) {
        if (!strcmp(v, "word"))
            xe.wrap_mode = XE_WRAP_EN;
        else if (strcmp(v, "kinsoku"))
            lrv_log(xe.hal, LRV_LOG_WARN, "lang: unknown wrap '%s'", v);
    }
    lrv_pack_free(&xe.pack, blob, size);
}
