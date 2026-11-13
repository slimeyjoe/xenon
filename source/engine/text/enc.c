/* The character stream of the current language's text. Every text consumer (printer, menus,
 * overlay, boxes) walks strings through text_next(), so the encoding is decided in one place.
 *
 * cp932 (Japanese, the original bytecode): a lead byte
 * with a trail byte is one 2-column glyph, keyed by its JIS X 0208 code in font16; any other
 * byte is one 1-column glyph keyed by the byte (JIS X 0201) in font8. A lead byte right before
 * the NUL counts as a 1-column byte, as in the original loops.
 *
 * UTF-8 (translations): both fonts of the language are keyed by Unicode code point. A code
 * point is 1 column if it is ASCII or font8 has a glyph for it, otherwise 2 columns (font16).
 * mkpack.py guarantees that every character of the language's texts has a glyph in one of
 * the two. Malformed bytes and code points outside the BMP (the fonts use 16-bit codes) become
 * U+FFFD. */
#include <string.h>
#include "xe_internal.h"
#include "text/text.h"
#include "util/sjis.h"
#include "util/utf8.h"

int text_next(const uint8_t *s, struct text_ch *ch)
{
    if (xe.text_enc == XE_ENC_UTF8) {
        uint32_t cp;
        ch->len = utf8_decode(s, &cp);
        if (cp > 0xFFFF)
            cp = UTF8_REPLACEMENT;
        ch->code = cp;
        ch->cols = (cp < 0x80 || font_lookup8(cp)) ? 1 : 2;
        return ch->len;
    }
    if (sjis_is_lead(s[0]) && s[1]) {
        ch->code = sjis_to_jis(s[0], s[1]);
        ch->len = 2;
        ch->cols = 2;
    } else {
        ch->code = s[0];
        ch->len = 1;
        ch->cols = 1;
    }
    return ch->len;
}

int text_cols(const uint8_t *s)
{
    struct text_ch ch;
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
 * lines `encoding=cp932|utf-8` and `wrap=kinsoku|word`. Packs without it get the settings
 * the languages had before the entry existed: English word wrap, otherwise kinsoku, cp932. */
void text_enc_init(void)
{
    const struct xpak_entry *e = xpak_find(&xe.pak, "lang", XPAK_T_META);
    uint8_t *blob;
    uint32_t size, i = 0;

    xe.text_enc = XE_ENC_CP932;
    xe.wrap_mode = (xe.lang == XPAK_L_EN) ? XE_WRAP_EN : XE_WRAP_JA;
    if (!e || !(blob = xpak_load(&xe.pak, e, &size)))
        return;
    while (i < size) {
        char line[64];
        uint32_t n = 0;
        while (i < size && blob[i] != '\n') {
            if (n < sizeof line - 1 && blob[i] != '\r')
                line[n++] = (char)blob[i];
            i++;
        }
        i++;
        line[n] = 0;
        if (!strcmp(line, "encoding=utf-8"))
            xe.text_enc = XE_ENC_UTF8;
        else if (!strcmp(line, "encoding=cp932"))
            xe.text_enc = XE_ENC_CP932;
        else if (!strcmp(line, "wrap=word"))
            xe.wrap_mode = XE_WRAP_EN;
        else if (!strcmp(line, "wrap=kinsoku"))
            xe.wrap_mode = XE_WRAP_JA;
        else if (n)
            sys_log(XE_LOG_WARN, "lang: unknown setting '%s'", line);
    }
    xpak_free(&xe.pak, blob, size);
}
