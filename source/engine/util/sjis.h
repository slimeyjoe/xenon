/* Shift-JIS (cp932) helpers: lead-byte test as the Japanese Borland RTL's _mbctype table
 * (scripts.md §5.1) and the standard SJIS -> JIS X 0208 conversion (font.md 0F16:0077). */
#ifndef XE_UTIL_SJIS_H
#define XE_UTIL_SJIS_H

#include <stdint.h>

static inline int sjis_is_lead(unsigned b)
{
    return (b >= 0x81 && b <= 0x9F) || (b >= 0xE0 && b <= 0xFC);
}

/* Returns the 16-bit JIS code (row/cell bytes 0x21..0x7E each) for an SJIS pair. */
static inline unsigned sjis_to_jis(unsigned hi, unsigned lo)
{
    hi -= (hi <= 0x9F) ? 0x71 : 0xB1;
    hi = hi * 2 + 1;
    if (lo > 0x7F)
        lo--;
    if (lo >= 0x9E) {
        lo -= 0x7D;
        hi++;
    } else {
        lo -= 0x1F;
    }
    return ((hi & 0xFF) << 8) | (lo & 0xFF);
}

#endif
