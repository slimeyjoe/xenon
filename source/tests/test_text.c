/* Unit test of the text character stream: the UTF-8 decoder (util/utf8.h) and text_next /
 * text_cols (engine/text/enc.c) in both encodings. Exit status 0 = pass. */
#include <stdio.h>
#include <string.h>
#include "xe_internal.h"
#include "text/text.h"
#include "util/utf8.h"

static int failures;

#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

static void dec(const char *bytes, int want_len, uint32_t want_cp)
{
    uint32_t cp = 0;
    int n = utf8_decode((const uint8_t *)bytes, &cp);
    CHECK(n == want_len && cp == want_cp, "utf8_decode(%02X..) = %d U+%04X, want %d U+%04X",
          (uint8_t)bytes[0], n, (unsigned)cp, want_len, (unsigned)want_cp);
}

static void next(const char *bytes, unsigned code, int len, int cols)
{
    struct text_ch ch;
    int n = text_next((const uint8_t *)bytes, &ch);
    CHECK(n == len && ch.len == len && ch.code == code && ch.cols == cols,
          "text_next(%02X..) = code %04X len %d cols %d, want %04X %d %d",
          (uint8_t)bytes[0], ch.code, ch.len, ch.cols, code, len, cols);
}

/* a font8 with a few Polish letters (glyph contents do not matter) */
static uint8_t font8_codes[2 * 4];
static uint8_t font8_glyphs[16 * 4];

static void fake_font8(void)
{
    static const unsigned codes[4] = { 0x00E9, 0x0105, 0x0142, 0x017C };   /* é ą ł ż */
    int k;
    for (k = 0; k < 4; k++) {
        font8_codes[2 * k] = (uint8_t)codes[k];
        font8_codes[2 * k + 1] = (uint8_t)(codes[k] >> 8);
    }
    memset(&xe.font8, 0, sizeof xe.font8);
    xe.font8.cell_w = 8;
    xe.font8.cell_h = 16;
    xe.font8.count = 4;
    xe.font8.codes = font8_codes;
    xe.font8.glyphs = font8_glyphs;
    xe.font8.glyph_bytes = 16;
}

int main(void)
{
    /* ---- decoder ---- */
    dec("A", 1, 'A');
    dec("\xC4\x85", 2, 0x105);                  /* ą */
    dec("\xE2\x97\x86", 3, 0x25C6);             /* ◆ */
    dec("\xEE\x80\x80", 3, 0xE000);             /* PUA */
    dec("\xF0\x9F\x98\x80", 4, 0x1F600);
    dec("\xC0\x80", 1, UTF8_REPLACEMENT);       /* overlong NUL */
    dec("\xE0\x80\xAF", 1, UTF8_REPLACEMENT);   /* overlong '/' */
    dec("\xED\xA0\x80", 1, UTF8_REPLACEMENT);   /* surrogate */
    dec("\xF4\x90\x80\x80", 1, UTF8_REPLACEMENT);   /* > U+10FFFF */
    dec("\x85", 1, UTF8_REPLACEMENT);           /* stray continuation byte */
    dec("\xC4", 1, UTF8_REPLACEMENT);           /* truncated before the NUL */
    dec("\xE2\x97\x0A", 1, UTF8_REPLACEMENT);   /* truncated before a control code */
    dec("\xFF", 1, UTF8_REPLACEMENT);

    /* ---- cp932 stream ---- */
    xe.text_enc = XE_ENC_CP932;
    next("A", 'A', 1, 1);
    next("\x82\xA0", 0x2422, 2, 2);             /* あ -> JIS 2422h */
    next("\x81\x40", 0x2121, 2, 2);             /* full-width space */
    next("\xB1", 0xB1, 1, 1);                   /* half-width katakana byte */
    next("\x82", 0x82, 1, 1);                   /* lead byte before the NUL */
    CHECK(text_cols((const uint8_t *)"\x81\x9F Next") == 7, "cp932 text_cols");

    /* ---- UTF-8 stream ---- */
    xe.text_enc = XE_ENC_UTF8;
    fake_font8();
    next("A", 'A', 1, 1);
    next("\xC4\x85", 0x105, 2, 1);              /* ą: in font8 -> narrow */
    next("\xC3\xA9", 0xE9, 2, 1);               /* é */
    next("\xE2\x97\x86", 0x25C6, 3, 2);         /* ◆: not in font8 -> wide */
    next("\xE3\x80\x80", 0x3000, 3, 2);         /* full-width space */
    next("\xF0\x9F\x98\x80", UTF8_REPLACEMENT, 4, 2);   /* outside the BMP */
    next("\xC4", UTF8_REPLACEMENT, 1, 2);       /* malformed */
    /* "◆ Zażółć" with ó, ć missing from this font8 -> wide */
    CHECK(text_cols((const uint8_t *)"\xE2\x97\x86 Za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87") == 11,
          "utf-8 text_cols = %d", text_cols((const uint8_t *)"\xE2\x97\x86 Za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87"));
    CHECK(text_prompt_str(0)[0] == 0xE2 && text_prompt_str(1)[0] == 0xEE, "utf-8 prompts");
    xe.text_enc = XE_ENC_CP932;
    CHECK(text_prompt_str(0)[0] == 0x81 && text_prompt_str(1)[0] == 0xEB, "cp932 prompts");

    printf("test_text: %s\n", failures ? "FAILED" : "ok");
    return failures != 0;
}
