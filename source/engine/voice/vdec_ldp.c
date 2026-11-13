/* ldp4 / ldp6 voice decoder: the Windows release's XENON.EXE 0x40E040 (header) and 0x40DE75
 * (blocks), see docs/re/other-versions.md §2.4 and tools/ldp.py (the reference).
 *
 *   "ldp" + '4' | '6'   code width; u32 BE sample rate; u32 BE sample count
 *   blocks: u16 BE byte count, u16 BE sample count, an MSB-first stream of codes.
 *   Per block the accumulator and the bit buffer start at 0. Code c != 0: acc += T[c]
 *   (16-bit wrap), output acc. Code 0: read a count n, output acc n+1 times. */
#include <string.h>
#include "voice/vdec.h"

/* XENON.EXE 0x41B628 (ldp4) and 0x41B648 (ldp6) */
static const int16_t tab4[16] = {
    0, 8, 26, 86, 283, 927, 3043, 9986, -32768, -9986, -3043, -927, -283, -86, -26, -8
};
static const int16_t tab6[64] = {
    0, 1, 2, 3, 4, 5, 6, 7, 10, 15, 20, 29, 40, 56, 78, 109,
    153, 214, 299, 419, 586, 819, 1145, 1601, 2240, 3132, 4380, 6126, 8567, 11980, 16754, 23431,
    -32768, -23431, -16754, -11980, -8567, -6126, -4380, -3132, -2240, -1601, -1145, -819, -586, -419, -299, -214,
    -153, -109, -78, -56, -40, -29, -20, -15, -10, -7, -6, -5, -4, -3, -2, -1
};

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

/* next block header; 0 at the end of the entry */
static int next_block(struct vdec_ldp *d)
{
    uint8_t h[4];
    d->pos += d->blk_bytes;                 /* skip what the previous block did not use */
    if (d->pos + 4 > d->e->size || vdec_in_read(d->in, d->pos, h, 4) != 4)
        return 0;
    d->pos += 4;
    d->blk_bytes = ((uint32_t)h[0] << 8) | h[1];
    d->blk_samples = ((uint32_t)h[2] << 8) | h[3];
    if (d->pos + d->blk_bytes > d->e->size)
        d->blk_bytes = d->e->size - d->pos;
    d->bitbuf = 0;
    d->nbits = 0;
    d->buf_n = d->buf_i = 0;
    d->acc = 0;
    return 1;
}

static unsigned next_byte(struct vdec_ldp *d)
{
    if (d->buf_i >= d->buf_n) {
        uint32_t n = d->blk_bytes < sizeof d->buf ? d->blk_bytes : (uint32_t)sizeof d->buf;
        if (n == 0)
            return 0;                       /* past the block: zero bits, as the original */
        d->buf_n = (int)vdec_in_read(d->in, d->pos, d->buf, n);
        d->buf_i = 0;
        if (d->buf_n <= 0) {
            d->blk_bytes = 0;
            return 0;
        }
        d->pos += (uint32_t)d->buf_n;
        d->blk_bytes -= (uint32_t)d->buf_n;
    }
    return d->buf[d->buf_i++];
}

static unsigned code(struct vdec_ldp *d)
{
    while (d->nbits < d->bits) {
        d->bitbuf = (d->bitbuf << 8) | next_byte(d);
        d->nbits += 8;
    }
    d->nbits -= d->bits;
    return (d->bitbuf >> d->nbits) & ((1u << d->bits) - 1);
}

int vdec_ldp_open(struct vdec_ldp *d, struct vdec_in *in)
{
    const struct xpak_entry *e = in->e;
    uint8_t h[12];
    memset(d, 0, sizeof *d);
    if (e->size < 12 || vdec_in_read(in, 0, h, 12) != 12 || memcmp(h, "ldp", 3) != 0)
        return -1;
    if (h[3] == '4') { d->bits = 4; d->table = tab4; }
    else if (h[3] == '6') { d->bits = 6; d->table = tab6; }
    else return -1;
    d->in = in;
    d->e = e;
    d->rate = be32(h + 4);
    d->left = be32(h + 8);
    d->pos = 12;
    return d->rate ? 0 : -1;
}

int vdec_ldp_read(struct vdec_ldp *d, int16_t *out, int n)
{
    int k = 0;
    while (k < n) {
        unsigned c, run;
        if (d->pend) {                      /* a run that did not fit the previous call */
            uint32_t m = d->pend < (uint32_t)(n - k) ? d->pend : (uint32_t)(n - k);
            d->pend -= m;
            while (m--)
                out[k++] = (int16_t)d->acc;
            continue;
        }
        if (d->left == 0)
            break;
        if (d->blk_samples == 0) {
            if (!next_block(d))
                d->left = 0;
            continue;
        }
        c = code(d);
        if (c) {
            d->acc = (uint16_t)(d->acc + (uint16_t)d->table[c]);
            run = 1;
        } else {
            run = code(d) + 1;
        }
        if (run > d->blk_samples)
            run = d->blk_samples;
        if (run > d->left)
            run = d->left;
        d->blk_samples -= run;
        d->left -= run;
        d->pend = run;
    }
    return k;
}
