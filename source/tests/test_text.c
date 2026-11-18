/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Unit test of the text character stream: text_next / text_cols / text_prompt_str
 * (engine/text/enc.c) in both encodings (the decoders are lrvne's, tested there). Exit
 * status 0 = pass. */
#include <stdio.h>
#include <string.h>
#include "xe.h"
#include "lrvne/utf8.h"
#include "text/text.h"

static int failures;

#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

static void next(const char *bytes, unsigned code, int len, int cols)
{
    struct lrv_text_ch ch;
    int n = text_next((const uint8_t *)bytes, &ch);
    CHECK(n == len && ch.len == len && ch.code == code && ch.cols == cols,
          "text_next(%02X..) = code %04X len %d cols %d, want %04X %d %d",
          (uint8_t)bytes[0], ch.code, ch.len, ch.cols, code, len, cols);
}

/* a font8 with a few Polish letters (glyph contents do not matter): an XFNT table */
static uint8_t font8_table[12 + 2 * 4 + 16 * 4];

static void fake_font8(void)
{
    static const unsigned codes[4] = { 0x00E9, 0x0105, 0x0142, 0x017C };   /* é ą ł ż */
    int k;
    memcpy(font8_table, "XFNT", 4);
    font8_table[4] = 1;
    font8_table[5] = 8;
    font8_table[6] = 16;
    font8_table[8] = 4;
    for (k = 0; k < 4; k++) {
        font8_table[12 + 2 * k] = (uint8_t)codes[k];
        font8_table[12 + 2 * k + 1] = (uint8_t)(codes[k] >> 8);
    }
    CHECK(lrv_font_parse(&xe.font8, font8_table, sizeof font8_table, 8, 16) == LRV_OK,
          "font8 table");
}

int main(void)
{
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
    next("\xF0\x9F\x98\x80", LRV_UTF8_REPLACEMENT, 4, 2);   /* outside the BMP */
    next("\xC4", LRV_UTF8_REPLACEMENT, 1, 2);       /* malformed */
    /* "◆ Zażółć" with ó, ć missing from this font8 -> wide */
    CHECK(text_cols((const uint8_t *)"\xE2\x97\x86 Za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87") == 11,
          "utf-8 text_cols = %d", text_cols((const uint8_t *)"\xE2\x97\x86 Za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87"));
    CHECK(text_prompt_str(0)[0] == 0xE2 && text_prompt_str(1)[0] == 0xEE, "utf-8 prompts");
    xe.text_enc = XE_ENC_CP932;
    CHECK(text_prompt_str(0)[0] == 0x81 && text_prompt_str(1)[0] == 0xEB, "cp932 prompts");

    printf("test_text: %s\n", failures ? "FAILED" : "ok");
    return failures != 0;
}
