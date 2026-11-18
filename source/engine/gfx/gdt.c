/* SPDX-License-Identifier: GPL-3.0-or-later */
/* GDT ("DA1") decoder - see gdt.h and docs/re/graphics.md §2.3-2.5.
 *
 * Structure follows re-tools/gdt.py (which is byte-exact against the x86 original on all 292
 * shipped blobs): a column program is a stream of commands for one 8-pixel column (1-byte
 * codec) or one 16-pixel column pair (2-byte codec) of one plane, written top to bottom with
 * stride 80, terminated by 0xFF. Planes are decoded in order 0..3 for each column, so a plane
 * may copy from lower planes already decoded for that column. */
#include "gfx/gdt.h"
#include "lrvne/endian.h"

#define STRIDE GDT_STRIDE

/* 1-byte codec, opcodes A0..EF: copy from (di - offset) in the same plane, by high nibble. */
static const uint16_t rel1[16] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0x500, 0x280, 0x140, 0xA0, 2, 0
};

/* 2-byte codec, opcodes D3..F2: copy a word from (di + offset) in the same plane. */
static const int16_t rel2[32] = {
    -0x500, -0x3C0, -0x280, -0x140, -0xA0, -0x50,
    -0x282, -0x142, -0xA2, -0x52, -2, 0x4E, 0x9E, 0x13E, 0x27E,
    -0x284, -0x144, -0xA4, -0x54, -4, 0x4C, 0x9C, 0x13C, 0x27C,
    -0x146, -0xA6, -0x56, -6, 0x4A, 0x9A, 0x13A, -8
};

/* Plane pairs for the AND commands: index 1 -> (0,1), 2 -> (0,2), 3 -> (1,2). */
static const uint8_t pair_a[4] = { 0, 0, 0, 1 };
static const uint8_t pair_b[4] = { 0, 1, 2, 2 };

static uint8_t dup4(unsigned n)
{
    n &= 0xF;
    return (uint8_t)(n | (n << 4));
}

static uint8_t ror2(unsigned x)
{
    return (uint8_t)(((x >> 2) | (x << 6)) & 0xFF);
}

/* Decoder state shared by the column routines. */
struct dec {
    const uint8_t *d;
    size_t len;
    uint8_t *pl[4];
    int lo, hi;          /* lowest / highest plane offset written (for the bounding box) */
    int err;
};

#define PUT(val) do { v[di] = (uint8_t)(val); last = di; di += STRIDE; } while (0)
#define NEED(n) do { if (si + (n) > s->len) { s->err = 1; return si; } } while (0)

/* graphics.md §2.4 - one 8-pixel column of plane `pl` starting at offset `di`. */
static size_t column1(struct dec *s, size_t si, int pl, int di)
{
    const uint8_t *d = s->d;
    uint8_t *v = s->pl[pl];
    int first = -1, last = -1;
    unsigned b, n, i;

    for (;;) {
        NEED(1);
        b = d[si++];
        if (b == 0xFF)
            break;
        if (first < 0)
            first = di;
        if (b < 0x40) {                             /* 00-1F fill 00, 20-3F fill FF */
            uint8_t val = (b < 0x20) ? 0x00 : 0xFF;
            n = b & 0x1F;
            if (n == 0) { NEED(1); n = d[si++]; }
            for (i = 0; i < n; i++)
                PUT(val);
        } else if (b < 0xA0) {                      /* 40/60/80: copy plane 0/1/2 */
            const uint8_t *src = s->pl[(b - 0x40) >> 5];
            n = b & 0x1F;
            if (n == 0) { NEED(1); n = d[si++]; }
            for (i = 0; i < n; i++)
                PUT(src[di]);
        } else if (b < 0xF0) {                      /* A0-EF: copy from earlier in this plane */
            int off = rel1[b >> 4];
            n = b & 0xF;
            if (n == 0) { NEED(1); n = d[si++]; }
            for (i = 0; i < n; i++)
                PUT(v[di - off]);
        } else if (b <= 0xF8) {                     /* F0 n / F1-F8: literal bytes */
            if (b == 0xF0) { NEED(1); n = d[si++]; }
            else n = b - 0xF0;
            NEED(n);
            for (i = 0; i < n; i++)
                PUT(d[si++]);
        } else if (b == 0xF9) {                     /* skip n lines */
            NEED(1);
            di += STRIDE * d[si++];
        } else if (b == 0xFA) {                     /* fill n x byte */
            uint8_t val;
            NEED(2);
            n = d[si]; val = d[si + 1]; si += 2;
            for (i = 0; i < n; i++)
                PUT(val);
        } else if (b == 0xFB) {                     /* NOT plane0 (n<80) / NOT plane1 */
            const uint8_t *src;
            NEED(1);
            n = d[si++];
            src = s->pl[n < 0x80 ? 0 : 1];
            n &= 0x7F;
            for (i = 0; i < n; i++)
                PUT(src[di] ^ 0xFF);
        } else if (b == 0xFC) {
            NEED(1);
            n = d[si++];
            if (n < 0x80) {                         /* NOT plane2 */
                const uint8_t *src = s->pl[2];
                for (i = 0; i < n; i++)
                    PUT(src[di] ^ 0xFF);
            } else {                                /* 2-line nibble pattern */
                uint8_t a, c;
                NEED(1);
                a = dup4(d[si]); c = dup4(d[si] >> 4); si++;
                n &= 0x7F;
                for (i = 0; i < n; i++) {
                    PUT(a);
                    PUT(c);
                }
            }
        } else if (b == 0xFD) {
            uint8_t pat[4];
            unsigned np;
            NEED(1);
            n = d[si++];
            if (n < 0x80) {                         /* 2-line byte pattern */
                NEED(2);
                pat[0] = d[si]; pat[1] = d[si + 1]; si += 2;
                np = 2;
                n &= 0x7F;
            } else if (n < 0xC0) {                  /* 4-line: nibbles + rotated */
                NEED(1);
                pat[0] = dup4(d[si]); pat[1] = dup4(d[si] >> 4); si++;
                pat[2] = ror2(pat[0]); pat[3] = ror2(pat[1]);
                np = 4;
                n &= 0x3F;
            } else {                                /* 4-line nibble pattern */
                NEED(2);
                pat[0] = dup4(d[si]); pat[1] = dup4(d[si] >> 4);
                pat[2] = dup4(d[si + 1]); pat[3] = dup4(d[si + 1] >> 4); si += 2;
                np = 4;
                n &= 0x3F;
            }
            for (i = 0; i < n; i++) {
                unsigned k;
                for (k = 0; k < np; k++)
                    PUT(pat[k]);
            }
        } else {                                    /* 0xFE */
            NEED(1);
            n = d[si++];
            if (n < 0x40) {                         /* 4-line byte pattern */
                const uint8_t *pat;
                NEED(4);
                pat = d + si; si += 4;
                for (i = 0; i < n; i++) {
                    PUT(pat[0]); PUT(pat[1]); PUT(pat[2]); PUT(pat[3]);
                }
            } else {                                /* AND of two planes */
                const uint8_t *pa = s->pl[pair_a[n >> 6]];
                const uint8_t *pb = s->pl[pair_b[n >> 6]];
                n &= 0x3F;
                for (i = 0; i < n; i++)
                    PUT(pa[di] & pb[di]);
            }
        }
    }
    if (first >= 0) {
        if (first < s->lo) s->lo = first;
        if (last > s->hi) s->hi = last;
    }
    return si;
}

#undef PUT

/* 16-bit little-endian word at plane offset `at`: left byte = column x, right = x+1. */
#define WORD(p, at) ((unsigned)(p)[at] | ((unsigned)(p)[(at) + 1] << 8))
#define PUTW(w) do { v[di] = (uint8_t)(w); v[di + 1] = (uint8_t)((w) >> 8); last = di; di += STRIDE; } while (0)

/* graphics.md §2.5 - one 16-pixel column pair; tokens are words (parameter a, opcode op). */
static size_t column2(struct dec *s, size_t si, int pl, int di)
{
    const uint8_t *d = s->d;
    uint8_t *v = s->pl[pl];
    int first = -1, last = -1;
    unsigned a, op, n, i;

    for (;;) {
        NEED(2);
        a = d[si]; op = d[si + 1]; si += 2;
        if (op == 0xFF)
            break;
        if (first < 0)
            first = di;
        if (op < 0x80) {                            /* 2-line nibble pattern, op reps */
            unsigned w0 = dup4(a) * 0x101u, w1 = dup4(a >> 4) * 0x101u;
            for (i = 0; i < op; i++) {
                PUTW(w0);
                PUTW(w1);
            }
        } else if (op < 0xC0) {                     /* 4-line nibble + rotated */
            unsigned lo = dup4(a), hi = dup4(a >> 4);
            unsigned p0 = lo * 0x101u, p1 = hi * 0x101u;
            unsigned p2 = ror2(lo) * 0x101u, p3 = ror2(hi) * 0x101u;
            n = op & 0x3F;
            for (i = 0; i < n; i++) {
                PUTW(p0); PUTW(p1); PUTW(p2); PUTW(p3);
            }
        } else if (op < 0xD0) {                     /* fill a lines with nibble op&15 */
            unsigned w = dup4(op) * 0x101u;
            for (i = 0; i < a; i++)
                PUTW(w);
        } else if (op < 0xD2) {                     /* literal words, n = a + 256*(op&1) */
            n = a | ((op & 1) << 8);
            NEED(2 * n);
            for (i = 0; i < n; i++) {
                unsigned w = WORD(d, si);
                si += 2;
                PUTW(w);
            }
        } else if (op == 0xD2) {                    /* skip a lines */
            di += STRIDE * (int)a;
        } else if (op <= 0xF2) {                    /* copy from relative position */
            int off = rel2[op - 0xD3];
            for (i = 0; i < a; i++) {
                unsigned w = WORD(v, di + off);
                PUTW(w);
            }
        } else if (op <= 0xF5) {                    /* copy plane 0/1/2 */
            const uint8_t *src = s->pl[op - 0xF3];
            for (i = 0; i < a; i++) {
                unsigned w = WORD(src, di);
                PUTW(w);
            }
        } else if (op <= 0xF8) {                    /* NOT plane 0/1/2 */
            const uint8_t *src = s->pl[op - 0xF6];
            for (i = 0; i < a; i++) {
                unsigned w = WORD(src, di) ^ 0xFFFFu;
                PUTW(w);
            }
        } else if (op <= 0xFB) {                    /* AND planes (0,1) (0,2) (1,2) */
            const uint8_t *pa = s->pl[pair_a[op - 0xF8]];
            const uint8_t *pb = s->pl[pair_b[op - 0xF8]];
            for (i = 0; i < a; i++) {
                unsigned w = WORD(pa, di) & WORD(pb, di);
                PUTW(w);
            }
        } else if (op == 0xFC) {                    /* fill a lines with literal word */
            unsigned w;
            NEED(2);
            w = WORD(d, si); si += 2;
            for (i = 0; i < a; i++)
                PUTW(w);
        } else if (op == 0xFD) {                    /* 2-line pattern */
            unsigned p0, p1;
            if (a < 0x80) {
                NEED(2);
                p0 = d[si] * 0x101u; p1 = d[si + 1] * 0x101u; si += 2;
            } else {
                NEED(4);
                p0 = WORD(d, si); p1 = WORD(d, si + 2); si += 4;
            }
            n = a & 0x7F;
            for (i = 0; i < n; i++) {
                PUTW(p0);
                PUTW(p1);
            }
        } else {                                    /* 0xFE: 4-line patterns */
            unsigned pat[4];
            if (a < 0x40) {
                /* quirk reproduced from 0456:055E (never used by game data) */
                unsigned pa, pc, pd, swn;
                NEED(4);
                pa = d[si]; pc = d[si + 2]; pd = d[si + 3]; si += 4;
                swn = ((pa & 15) << 4) | (pa >> 4);
                pat[0] = pa * 0x101u;
                pat[1] = pd * 0x101u;
                pat[2] = pd | (swn << 8);
                pat[3] = (((pd & 15) << 4) | (pc >> 4)) | ((((pc & 15) << 4) | (pd >> 4)) << 8);
            } else if (a < 0x80) {
                NEED(2);
                pat[0] = dup4(d[si]) * 0x101u; pat[1] = dup4(d[si] >> 4) * 0x101u;
                pat[2] = dup4(d[si + 1]) * 0x101u; pat[3] = dup4(d[si + 1] >> 4) * 0x101u;
                si += 2;
            } else if (a < 0xC0) {
                NEED(4);
                pat[0] = d[si] * 0x101u; pat[1] = d[si + 1] * 0x101u;
                pat[2] = d[si + 2] * 0x101u; pat[3] = d[si + 3] * 0x101u;
                si += 4;
            } else {
                NEED(8);
                pat[0] = WORD(d, si); pat[1] = WORD(d, si + 2);
                pat[2] = WORD(d, si + 4); pat[3] = WORD(d, si + 6);
                si += 8;
            }
            n = a & 0x3F;
            for (i = 0; i < n; i++) {
                PUTW(pat[0]); PUTW(pat[1]); PUTW(pat[2]); PUTW(pat[3]);
            }
        }
    }
    if (first >= 0) {
        if (first < s->lo) s->lo = first;
        if (last > s->hi) s->hi = last;
    }
    return si;
}

#undef PUTW
#undef WORD
#undef NEED

void gdt_unpack_palette(const uint8_t raw[24], uint8_t rgb4[48])
{
    int i;
    for (i = 0; i < 8; i++) {
        unsigned b0 = raw[3 * i], b1 = raw[3 * i + 1], b2 = raw[3 * i + 2];
        uint8_t *e = rgb4 + 6 * i;
        e[0] = (uint8_t)(b0 & 15);  e[1] = (uint8_t)(b1 >> 4);  e[2] = (uint8_t)(b0 >> 4);
        e[3] = (uint8_t)(b2 >> 4);  e[4] = (uint8_t)(b2 & 15);  e[5] = (uint8_t)(b1 & 15);
    }
}

int gdt_parse(const uint8_t *data, size_t len, struct gdt_header *h)
{
    size_t p;
    int i;

    if (len < 24 || data[0] != 'D' || data[1] != 'A' || data[2] != '1' || data[3] != 0)
        return -1;
    h->xb = data[8];
    h->wb = data[9];
    h->y = lrv_rd16le(data + 10);
    h->h = lrv_rd16le(data + 12);
    h->flags = lrv_rd16le(data + 14);
    p = 16;
    if (h->flags & GDT_FLAG_PALETTE) {
        if (len < 48)
            return -1;
        gdt_unpack_palette(data + 16, h->rgb4);
        p = 40;
    }
    if (p + 8 > len)
        return -1;
    for (i = 0; i < 4; i++) {
        h->plane_size[i] = lrv_rd16le(data + p + 2 * i);
    }
    p += 8;
    for (i = 0; i < 4; i++) {
        h->plane_off[i] = (uint32_t)p;
        p += h->plane_size[i];
    }
    h->end = (uint32_t)p;
    return p <= len ? 0 : -1;
}

/* Decode the four planes of one 1-byte-codec column at offset c. */
static void single(struct dec *s, size_t *ptr, int c)
{
    int pl;
    for (pl = 0; pl < 4; pl++)
        ptr[pl] = column1(s, ptr[pl], pl, c);
}

/* 0456:0120..015B: `npairs` 16-pixel column pairs starting at offset c. */
static int pairs(struct dec *s, size_t *ptr, int c, int npairs, unsigned flags)
{
    int k, pl;
    if (npairs == 0)
        return c;
    if (flags & GDT_FLAG_CODEC1) {
        for (k = 0; k < npairs; k++) {
            single(s, ptr, c);
            single(s, ptr, c + 1);
            c += 2;
        }
        return c;
    }
    for (pl = 0; pl < 4; pl++)           /* 0456:3E64: round each stream up to even */
        ptr[pl] += ptr[pl] & 1;
    for (k = 0; k < npairs; k++) {
        for (pl = 0; pl < 4; pl++)
            ptr[pl] = column2(s, ptr[pl], pl, c);
        c += 2;
    }
    return c;
}

int gdt_decode(const uint8_t *data, size_t len, const struct gdt_header *h,
               struct lrv_surface *dst, struct lrv_rect *written)
{
    struct dec s;
    size_t ptr[4];
    int pos, c, half, pl;

    s.d = data;
    s.len = len;
    for (pl = 0; pl < 4; pl++) {
        s.pl[pl] = dst->plane[pl];
        ptr[pl] = h->plane_off[pl];
    }
    s.lo = 0x7fffffff;
    s.hi = -1;
    s.err = 0;

    pos = h->y * STRIDE + h->xb;
    half = h->wb >> 1;
    if (!(pos & 1)) {
        c = pairs(&s, ptr, pos, half, h->flags);
        if (h->wb & 1)
            single(&s, ptr, c);
    } else if (!(h->wb & 1)) {
        single(&s, ptr, pos);
        c = pairs(&s, ptr, pos + 1, half - 1, h->flags);
        single(&s, ptr, c);
    } else {
        single(&s, ptr, pos);
        pairs(&s, ptr, pos + 1, half, h->flags);
    }

    if (written) {
        lrv_rect_clear(written);
        if (s.hi >= 0) {
            written->x0 = h->xb;
            written->x1 = h->xb + h->wb;
            written->y0 = s.lo / STRIDE;
            written->y1 = s.hi / STRIDE + 1;
        }
    }
    return s.err ? -1 : 0;
}
