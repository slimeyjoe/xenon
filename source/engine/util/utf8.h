/* UTF-8 decoding for the text of UTF-8 languages (the translations; Japanese stays cp932).
 * Strict decoder: overlong forms, surrogates, code points above U+10FFFF and truncated
 * sequences decode to U+FFFD and consume one byte, so a bad byte never swallows the NUL
 * or the control code after it. */
#ifndef XE_UTIL_UTF8_H
#define XE_UTIL_UTF8_H

#include <stdint.h>

#define UTF8_REPLACEMENT 0xFFFDu

/* Decodes the sequence at s (s[0] != 0). Returns the number of bytes consumed (1..4) and
 * stores the code point in *cp. */
static inline int utf8_decode(const uint8_t *s, uint32_t *cp)
{
    uint32_t c = s[0], min;
    int n, k;
    if (c < 0x80) {
        *cp = c;
        return 1;
    }
    if (c >= 0xC2 && c <= 0xDF) {
        n = 2; c &= 0x1F; min = 0x80;
    } else if (c >= 0xE0 && c <= 0xEF) {
        n = 3; c &= 0x0F; min = 0x800;
    } else if (c >= 0xF0 && c <= 0xF4) {
        n = 4; c &= 0x07; min = 0x10000;
    } else {
        *cp = UTF8_REPLACEMENT;
        return 1;
    }
    for (k = 1; k < n; k++) {
        if ((s[k] & 0xC0) != 0x80) {          /* also stops at the terminating NUL */
            *cp = UTF8_REPLACEMENT;
            return 1;
        }
        c = (c << 6) | (s[k] & 0x3Fu);
    }
    if (c < min || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) {
        *cp = UTF8_REPLACEMENT;
        return 1;
    }
    *cp = c;
    return n;
}

#endif
