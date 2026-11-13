#include "pack/lzss.h"
#include "util/endian.h"

#define N 4096
#define F 18
#define THRESHOLD 2

size_t lzss_decode(const uint8_t *src, size_t srclen, uint8_t *dst, size_t dstcap)
{
    uint8_t ring[N];
    unsigned r = N - F;
    unsigned flags = 0;
    size_t i = 0, out = 0, k;

    for (k = 0; k < N; k++)
        ring[k] = 0;

    for (;;) {
        flags >>= 1;
        if (!(flags & 0x100)) {
            if (i >= srclen)
                break;
            flags = src[i++] | 0xFF00u;
        }
        if (flags & 1) {
            uint8_t c;
            if (i >= srclen)
                break;
            c = src[i++];
            if (out < dstcap)
                dst[out++] = c;
            ring[r] = c;
            r = (r + 1) & (N - 1);
        } else {
            unsigned lo, hi, pos, ln;
            if (i + 1 >= srclen)
                break;
            lo = src[i]; hi = src[i + 1]; i += 2;
            pos = lo | ((hi & 0xF0) << 4);
            ln = (hi & 0x0F) + THRESHOLD;
            for (k = 0; k <= ln; k++) {
                uint8_t c = ring[(pos + k) & (N - 1)];
                if (out < dstcap)
                    dst[out++] = c;
                ring[r] = c;
                r = (r + 1) & (N - 1);
            }
        }
    }
    return out;
}

uint32_t lcz_size(const uint8_t *raw, size_t len)
{
    if (len < LCZ_HEADER_SIZE)
        return 0;
    return rd32le(raw + 0x14);
}
