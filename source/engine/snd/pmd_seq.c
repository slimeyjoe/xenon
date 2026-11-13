/* PMD 4.7g sequencer - see pmd.h. Structure and names follow re-tools/pmdmodel.py, which this
 * port must match register for register (tests/check_pmd.py against the VGM references). */
#include <string.h>
#include "snd/pmd.h"

static const uint16_t FNUM[12] = { 0x26A, 0x28F, 0x2B6, 0x2DF, 0x30B, 0x339, 0x36A, 0x39E,
                                   0x3D5, 0x410, 0x44E, 0x48F };                     /* 2CA9 */
static const uint16_t SSGP[12] = { 0xEE8, 0xE12, 0xD48, 0xC89, 0xBD5, 0xB2B, 0xA8A, 0x9F3,
                                   0x964, 0x8DD, 0x85E, 0x7E6 };                     /* 2CC1 */
static const uint8_t CARRIER[8] = { 0x80, 0x80, 0x80, 0x80, 0xA0, 0xE0, 0xE0, 0xF0 };   /* 2CD9 */
static const uint8_t NONCAR[8]  = { 0xEE, 0xEE, 0xEE, 0xEE, 0xCC, 0x88, 0x88, 0x00 };   /* 2CE1 */

/* operand byte counts of commands the model skips (B5..FF; 0 where handled or unused) */
static int skip_len(unsigned c)
{
    switch (c) {
    case 0xEC: case 0xEB: case 0xEA: case 0xE9: case 0xE8: case 0xE6: case 0xE4: case 0xE1:
    case 0xE0: case 0xD9: case 0xD8: case 0xD7: case 0xD1: case 0xEE: case 0xED: case 0xCC:
    case 0xC9:
        return 1;
    case 0xE5: case 0xC3:
        return 2;
    case 0xF0:
        return 4;
    case 0xCD:
        return 5;
    case 0xCE:
        return 6;
    default:
        return 0;
    }
}

#define S8(x)  ((int)(int8_t)(uint8_t)(x))
#define S16(x) ((int)(int16_t)(uint16_t)(x))

static void timer_reg(struct pmd *p, unsigned r, unsigned v);
static void silence_all(struct pmd *p);
void pmd_ssg_effect_start(struct pmd *p, int n, int pps_flags);

static void w(struct pmd *p, unsigned r, unsigned v)
{
    timer_reg(p, r, v);
    p->write(p->ctx, (uint8_t)r, (uint8_t)v);
}

static unsigned rb(const struct pmd *p, const struct pmd_part *q, unsigned a)
{
    const uint8_t *d = q->is_x ? p->efc : p->m;
    uint32_t n = q->is_x ? p->efcsize : p->msize;
    return a < n ? d[a] : 0x80;
}

static void wb(struct pmd *p, const struct pmd_part *q, unsigned a, unsigned v)
{
    uint8_t *d = q->is_x ? p->efc : p->m;
    uint32_t n = q->is_x ? p->efcsize : p->msize;
    if (a < n)
        d[a] = (uint8_t)v;
}

static unsigned rw(const struct pmd *p, const struct pmd_part *q, unsigned a)
{
    return rb(p, q, a) | (rb(p, q, a + 1) << 8);
}

static int is_ch3(const struct pmd_part *q)
{
    return q->kind_fm && q->ch == 3 && !q->is_x;
}

static int part_index(const struct pmd *p, const struct pmd_part *q)
{
    return (int)(q - p->part);
}

/* ---- song start / tempo / fade ------------------------------------------------------------ */

static void tempo_reg(struct pmd *p)                  /* 2C3B */
{
    if (p->TB != p->TBc) {
        p->TBc = p->TB;
        w(p, 0x26, p->TB);
    }
}

static void set_t(struct pmd *p, unsigned t)          /* 1487 */
{
    unsigned q, r;
    p->t = (uint8_t)t;
    if (t < 0x12) {
        p->TB = 0;
        return;
    }
    q = 0x112C / t;
    r = 0x112C % t;
    p->TB = (uint8_t)(256 - q);
    if (r & 0x80)
        p->TB--;
}

static void fade_step(struct pmd *p)                  /* 21CE */
{
    unsigned al = p->fadesp, v;
    if (al == 0)
        return;
    if (al < 0x80) {
        v = p->fade + al;
        if (v > 0xFF) {
            p->fade = 0xFF;
            p->fadesp = 0;
            if (p->stop_after_fade)
                pmd_mstop(p);
        } else {
            p->fade = (uint8_t)v;
        }
    } else {
        v = p->fade + al;
        if (v <= 0xFF) {
            p->fade = 0;
            p->fadesp = 0;
        } else {
            p->fade = (uint8_t)v;
        }
    }
}

/* ---- note helpers -------------------------------------------------------------------------- */

static unsigned transpose(const struct pmd_part *q, unsigned al)   /* 16C2 */
{
    int n, o;
    if ((al & 0x0F) == 0x0F || q->trans == 0)
        return al;
    n = (int)(al & 0x0F);
    o = (int)(al >> 4);
    n += S8(q->trans);
    while (n < 0) { n += 12; o--; }
    while (n >= 12) { n -= 12; o++; }
    return (unsigned)(((o & 0x0F) << 4) | n);
}

static unsigned calc_gate(struct pmd *p, struct pmd_part *q, unsigned si)   /* 076F */
{
    if (rb(p, q, si) == 0xC1) {
        q->gate = 0;
        return si + 1;
    }
    if (q->Q)
        q->gate = (uint8_t)(((q->len * q->Q) >> 8) + q->q);
    else
        q->gate = q->q;
    return si;
}

/* ---- FM -------------------------------------------------------------------------------- */

static void fm_note_freq(struct pmd_part *q, unsigned al)   /* 1717 */
{
    if ((al & 0x0F) == 0x0F) {
        q->note = 0xFF;
        if (!(q->flags & 0x11))
            q->freq = 0;
        return;
    }
    q->note = (uint8_t)al;
    q->freq = (uint16_t)(FNUM[al & 0x0F] | (((al >> 4) & 7) << 11));
}

static void normalize(int ax, int cx, unsigned *oa, int *oc)   /* 18E7 */
{
    ax = S16(ax);
    for (;;) {
        if (ax >= 0 && ax >= 0x26A) {
            if (ax < 0x4D4) {
                *oa = (unsigned)ax & 0xFFFF; *oc = cx;
                return;
            }
            cx += 0x800;
            if (cx == 0x4000) {
                *oc = 0x3800;
                *oa = (ax >= 0x800) ? 0x7FF : (unsigned)ax;
                return;
            }
            ax -= 0x26A;
            continue;
        }
        cx -= 0x800;
        if (cx < 0) {
            *oc = 0;
            if (ax < 8)
                ax = 8;
            *oa = (unsigned)ax & 0xFFFF;
            return;
        }
        ax += 0x26A;
    }
}

static void fm_freq(struct pmd *p, struct pmd_part *q)      /* 177E */
{
    int ax = q->freq, cx;
    unsigned a;
    int c;
    if (ax == 0 || q->slot == 0)
        return;
    cx = ax & 0x3800;
    ax = ((ax & 0x7FF) + q->porta + q->detune) & 0xFFFF;
    if (is_ch3(q) && p->reg27 != 0x3F) {
        unsigned bh = (q->lslot[0] & 0x0F) ? q->lslot[0] : 0xF0;
        unsigned ch = (q->lslot[1] & 0x0F) ? q->lslot[1] : 0xF0;
        unsigned bl = q->slot;
        static const struct { unsigned bit; int sd; unsigned hi, lo; } tab[4] = {
            { 0x80, 3, 0xA6, 0xA2 }, { 0x40, 2, 0xAC, 0xA8 }, { 0x20, 1, 0xAE, 0xAA }, { 0x10, 0, 0xAD, 0xA9 }
        };
        int i;
        for (i = 0; i < 4; i++) {
            int aa;
            unsigned v;
            if (!(bl & tab[i].bit))
                continue;
            aa = (ax + p->sd[tab[i].sd]) & 0xFFFF;
            if ((bh & 0x80) && (q->flags & 1))
                aa += q->lfo[0].val;
            if ((ch & 0x80) && (q->flags & 0x10))
                aa += q->lfo[1].val;
            bh = ((bh << 1) | (bh >> 7)) & 0xFF;      /* rotated only for present slots (quirk) */
            ch = ((ch << 1) | (ch >> 7)) & 0xFF;
            normalize(aa, cx, &a, &c);
            v = (a | (unsigned)c) & 0xFFFF;
            w(p, tab[i].hi, v >> 8);
            w(p, tab[i].lo, v & 0xFF);
        }
        return;
    }
    if (q->flags & 1)
        ax += q->lfo[0].val;
    if (q->flags & 0x10)
        ax += q->lfo[1].val;
    normalize(ax, cx, &a, &c);
    {
        unsigned v = (a | (unsigned)c) & 0xFFFF;
        w(p, 0xA3 + q->ch, v >> 8);
        w(p, 0x9F + q->ch, v & 0xFF);
    }
}

static void fm_volume(struct pmd *p, struct pmd_part *q)    /* 19DD */
{
    unsigned bl = q->slot, cl, b, bh;
    int buf[4] = { 0x80, 0x80, 0x80, 0x80 };      /* index 0 = slot4 .. 3 = slot1 */
    int i, k;
    static const unsigned regs[4] = { 0x4B, 0x43, 0x47, 0x3F };
    static const int tli[4] = { 3, 1, 2, 0 };
    if (bl == 0)
        return;
    cl = q->tmpvol ? ((q->tmpvol - 1) & 0xFF) : q->vol;
    if (!q->is_x && p->voldown[0])
        cl = (cl * (256 - p->voldown[0])) >> 8;
    if (!q->is_x && p->fade >= 2)
        cl = (cl * (256 - (p->fade >> 1))) >> 8;
    cl = (~cl) & 0xFF;
    b = bl & q->car;
    bh = b;
    for (i = 0; i < 4; i++)
        if (b & (0x80u >> i))
            buf[i] = (int)cl;
    if (cl != 0xFF) {
        for (k = 0; k < 2; k++) {
            unsigned fl = k ? 0x20 : 2;
            if (q->flags & fl) {
                unsigned m = q->lslot[k] & bl;
                int v = q->lfo[k].val;
                unsigned al = (unsigned)v & 0xFF;
                bh |= m;
                for (i = 0; i < 4; i++) {
                    if (m & (0x80u >> i)) {
                        int r = buf[i] - (int)al;
                        if (al & 0x80)
                            buf[i] = r >= 0 ? 0xFF : (r & 0xFF);
                        else
                            buf[i] = r < 0 ? 0 : r;
                    }
                }
            }
        }
    }
    for (i = 0; i < 4; i++) {
        if (bh & (0x80u >> i)) {
            int a = buf[i] + q->tl[tli[i]];
            if (a > 0xFF)
                a = 0xFF;
            a -= 0x80;
            if (a < 0)
                a = 0;
            w(p, regs[i] + q->ch, (unsigned)a);
        }
    }
}

static void keyon(struct pmd *p, struct pmd_part *q)        /* 1BBE */
{
    unsigned c, al;
    if (q->note == 0xFF)
        return;
    c = q->ch - 1u;
    al = p->ks[c] | q->slot;
    if (q->kdc)
        al &= q->kdm;
    p->ks[c] = (uint8_t)al;
    w(p, 0x28, c | al);
}

static void keyoff(struct pmd *p, struct pmd_part *q, int check)   /* 1C38 / 1C3F */
{
    unsigned c;
    if (check && q->note == 0xFF)
        return;
    c = q->ch - 1u;
    p->ks[c] &= (uint8_t)~q->slot;
    w(p, 0x28, c | p->ks[c]);
}

static unsigned voice_rec(const struct pmd *p, const struct pmd_part *q, unsigned n)   /* 1D40 */
{
    unsigned t = q->is_x ? p->efc_voicetab : p->voicetab;
    int guard = 0;
    while (rb(p, q, t) != n && guard++ < 512)
        t += 26;
    return t + 1;
}

static void voice_mute(struct pmd *p, struct pmd_part *q)   /* 1D7B */
{
    unsigned al = q->vmask, reg = 0x3F + q->ch;
    int i;
    for (i = 0; i < 4; i++) {
        al = ((al << 1) | (al >> 7)) & 0xFF;
        if (al & 1) {
            w(p, reg, 0x7F);
            w(p, reg + 0x40, 0x7F);
        }
        reg += 4;
    }
    keyoff(p, q, 0);
}

static void voice_load(struct pmd *p, struct pmd_part *q);

static void voice_restore(struct pmd *p, struct pmd_part *q)   /* 0EA2 */
{
    uint8_t tl[4];
    unsigned al;
    static const unsigned bits[4] = { 0x80, 0x40, 0x20, 0x10 };
    static const unsigned regs[4] = { 0x4B, 0x43, 0x47, 0x3F };
    static const int tli[4] = { 3, 1, 2, 0 };
    int i;
    if (q->vmask == 0)
        return;
    memcpy(tl, q->tl, 4);
    p->restore = 1;
    voice_load(p, q);
    p->restore = 0;
    memcpy(q->tl, tl, 4);
    al = (~q->car) & q->slot;
    for (i = 0; i < 4; i++)
        if (al & bits[i])
            w(p, regs[i] + q->ch, tl[tli[i]]);
}

static void voice_load(struct pmd *p, struct pmd_part *q)   /* 1C74 */
{
    unsigned bx = voice_rec(p, q, q->voice);
    uint8_t v[25];
    unsigned dl, alg, ah, al, reg;
    int i, k;
    for (i = 0; i < 25; i++)
        v[i] = (uint8_t)rb(p, q, bx + (unsigned)i);
    if (q->vmask) {
        voice_mute(p, q);
    } else {
        memcpy(q->tl, v + 4, 4);
        return;
    }
    dl = v[24];
    if (p->restore)
        dl = q->fbalg;
    if (is_ch3(q)) {
        if (p->restore)
            dl = p->fb3;
        else if (q->slot & 0x10)
            p->fb3 = (uint8_t)dl;
        else {
            dl = (p->fb3 & 0x38) | (dl & 7);
            p->fb3 = (uint8_t)dl;
        }
    }
    w(p, 0xAF + q->ch, dl);
    q->fbalg = (uint8_t)dl;
    alg = dl & 7;
    if (!(q->lslot[0] & 0x0F))
        q->lslot[0] = CARRIER[alg];
    if (!(q->lslot[1] & 0x0F))
        q->lslot[1] = CARRIER[alg];
    q->car = CARRIER[alg];
    ah = NONCAR[alg] & q->vmask;
    al = q->vmask;
    reg = 0x2F + q->ch;
    k = 0;
    for (i = 0; i < 4; i++) {
        al = ((al << 1) | (al >> 7)) & 0xFF;
        if (al & 1)
            w(p, reg, v[k]);
        reg += 4; k++;
    }
    for (i = 0; i < 4; i++) {
        ah = ((ah << 1) | (ah >> 7)) & 0xFF;
        if (ah & 1)
            w(p, reg, v[k]);
        reg += 4; k++;
    }
    for (i = 0; i < 16; i++) {
        al = ((al << 1) | (al >> 7)) & 0xFF;
        if (al & 1)
            w(p, reg, v[k]);
        reg += 4; k++;
    }
    memcpy(q->tl, v + 4, 4);
}

static void fm3_update(struct pmd *p, struct pmd_part *q)   /* 1095 */
{
    int idx = part_index(p, q);
    unsigned bit = idx == 2 ? 1 : idx == 3 ? 2 : idx == 4 ? 4 : 8;
    unsigned s = q->slot & 0xF0;
    int need;
    unsigned ah;
    if (s == 0)
        need = 0;
    else if (s != 0xF0)
        need = 1;
    else if (!(q->lslot[0] & 0x0F))
        need = 0;
    else if (q->flags & 1)
        need = 1;
    else if (!(q->lslot[1] & 0x0F))
        need = 0;
    else
        need = (q->flags & 0x10) != 0;
    if (need) {
        p->fm3need |= (uint8_t)bit;
        ah = 0x7F;
    } else {
        p->fm3need &= (uint8_t)~bit;
        ah = (p->fm3need || p->sd_any == 1) ? 0x7F : 0x3F;
    }
    if (q->mask & 2)
        return;
    if (ah == p->reg27)
        return;
    p->reg27 = (uint8_t)ah;
    w(p, 0x27, ah & 0xCF);
    if (ah == 0x3F || idx == 2)
        return;
    {
        int n;
        for (n = 2; n < 5; n++) {
            struct pmd_part *r;
            if (n == idx)
                break;
            r = &p->part[n];
            if (r->mask == 0)
                fm_freq(p, r);
        }
    }
}

/* ---- LFO ------------------------------------------------------------------------------- */

static void lfo_reset(struct pmd_part *q, int k)           /* 203A */
{
    struct pmd_lfo *L = &q->lfo[k];
    L->val = 0;
    memcpy(L->w, L->w + 4, 4);
    L->mdc[0] = L->mdc[1];
    if (q->flags & (4u << (4 * k))) {
        L->w[0]++;
        L->w[4]++;
    }
}

static void lfo_md(struct pmd_part *q, int k)              /* 1ECA */
{
    struct pmd_lfo *L = &q->lfo[k];
    unsigned c, st, d, al;
    L->md[1]--;
    if (L->md[1])
        return;
    L->md[1] = L->md[2];
    c = L->mdc[0];
    if (c == 0)
        return;
    if (!(c & 0x80))
        L->mdc[0] = (uint8_t)(c - 1);
    st = L->w[2];
    d = L->md[0];
    if (st & 0x80) {
        al = (((0u - st) & 0xFF) + d) & 0xFF;
        if (al & 0x80) {
            al = (d & 0x80) ? 0 : 0x81;
            L->w[2] = (uint8_t)al;
            return;
        }
        L->w[2] = (uint8_t)((0u - al) & 0xFF);
    } else {
        al = (st + d) & 0xFF;
        if (al & 0x80)
            al = (d & 0x80) ? 0 : 0x7F;
        L->w[2] = (uint8_t)al;
    }
}

static unsigned lfo_rand(struct pmd *p, unsigned x)        /* 1F18 */
{
    p->rnd = (uint16_t)((p->rnd * 0x103u + 3u) & 0x7FFF);
    return (unsigned)(((uint32_t)(x & 0xFFFF) * p->rnd) / 0x7FFF);
}

static void lfo_step(struct pmd *p, struct pmd_part *q, int k)   /* 1DE5 */
{
    struct pmd_lfo *L = &q->lfo[k];
    unsigned wv = L->wave;
    unsigned al;
    if (wv == 2) {
        L->val = (int16_t)(S8(L->w[2]) * S8(L->w[3]));
        if (L->w[1] != 1) {
            L->w[1]--;
            return;
        }
        L->w[1] = L->w[5];
        lfo_md(q, k);
        L->w[2] = (uint8_t)((0u - L->w[2]) & 0xFF);
        return;
    }
    if (L->w[1] != 1) {
        L->w[1]--;
        return;
    }
    L->w[1] = L->w[5];
    if (wv == 0 || wv == 4 || wv == 5) {
        int d;
        if (wv == 5) {
            int s = S8(L->w[2]);
            d = s * (s < 0 ? -s : s);
        } else {
            d = S8(L->w[2]);
        }
        L->val = (int16_t)((L->val + d) & 0xFFFF);
        if (L->val == 0)
            lfo_md(q, k);
        al = L->w[3];
        if (al == 0xFF)
            return;
        al = (al - 1) & 0xFF;
        if (al) {
            L->w[3] = (uint8_t)al;
            return;
        }
        L->w[3] = (uint8_t)(wv == 4 ? L->w[7] : (2 * L->w[7]) & 0xFF);
        L->w[2] = (uint8_t)((0u - L->w[2]) & 0xFF);
        return;
    }
    if (wv == 6) {
        al = L->w[3];
        if (al == 0)
            return;
        if (al != 0xFF)
            L->w[3] = (uint8_t)(al - 1);
        L->val = (int16_t)((L->val + S8(L->w[2])) & 0xFFFF);
        return;
    }
    if (wv == 1) {
        L->val = (int16_t)((L->val + S8(L->w[2])) & 0xFFFF);
        al = L->w[3];
        if (al != 0xFF) {
            al = (al - 1) & 0xFF;
            if (al == 0) {
                L->val = (int16_t)((0 - L->val) & 0xFFFF);
                lfo_md(q, k);
                al = (2 * L->w[7]) & 0xFF;
            }
        }
        L->w[3] = (uint8_t)al;
        return;
    }
    {
        int s = S8(L->w[2]);
        unsigned a = (unsigned)((s < 0 ? -s : s) * L->w[3]) & 0xFFFF;
        unsigned r = lfo_rand(p, 2 * a);
        L->val = (int16_t)((r - a) & 0xFFFF);
        lfo_md(q, k);
    }
}

static int lfo_tick(struct pmd *p, struct pmd_part *q, int k)   /* 1DAE */
{
    struct pmd_lfo *L = &q->lfo[k];
    int old;
    if (L->w[0]) {
        L->w[0]--;
        return 0;
    }
    old = L->val;
    if (q->ext & (2u << (4 * k))) {
        unsigned n = p->na, i;
        if (n == 0)
            return 0;
        for (i = 0; i < n; i++)
            lfo_step(p, q, k);
    } else {
        lfo_step(p, q, k);
    }
    return L->val != old;
}

static void lfo_keyon(struct pmd *p, struct pmd_part *q)   /* 1FF5 */
{
    int k;
    q->kdc = q->kd;
    for (k = 0; k < 2; k++) {
        unsigned sh = 4u * k;
        if (q->flags & (3u << sh)) {
            if (q->flags & (4u << sh))
                lfo_tick(p, q, k);
            else
                lfo_reset(q, k);
        }
    }
}

static void lfo_only(struct pmd *p, struct pmd_part *q)    /* 1FD5 */
{
    int k;
    for (k = 0; k < 2; k++)
        if (q->flags & (3u << (4 * k)))
            lfo_tick(p, q, k);
}

/* ---- SSG ------------------------------------------------------------------------------- */

#define E(q, off) ((q)->e[(off) - 0x21])

static void ssg_period(struct pmd_part *q, unsigned al)    /* 174E */
{
    unsigned o, v, r;
    if ((al & 0x0F) == 0x0F) {
        q->note = 0xFF;
        if (!(q->flags & 0x11))
            q->freq = 0;
        return;
    }
    q->note = (uint8_t)al;
    o = (al >> 4) & 0x0F;
    v = SSGP[al & 0x0F];
    r = v >> o;
    if (o && ((v >> (o - 1)) & 1))
        r++;
    q->freq = (uint16_t)r;
}

static int ssg_scale(int a, int d)
{
    int prod = S16(a) * S16(d);
    int dx = prod >> 12;
    return dx >= 0 ? dx + 1 : dx - 1;
}

static void ssg_freq(struct pmd *p, struct pmd_part *q)    /* 192A */
{
    int ax = q->freq;
    unsigned r;
    if (ax == 0)
        return;
    ax = (ax + q->porta) & 0xFFFF;
    if (q->ext & 1) {
        if (q->detune)
            ax = (ax - ssg_scale(ax, q->detune)) & 0xFFFF;
        if (q->flags & 0x11) {
            int d = 0;
            if (q->flags & 1)
                d = q->lfo[0].val;
            if (q->flags & 0x10)
                d = (d + q->lfo[1].val) & 0xFFFF;
            if (d)
                ax = (ax - ssg_scale(ax, d)) & 0xFFFF;
        }
    } else {
        ax = (ax - q->detune) & 0xFFFF;
        if (q->flags & 1)
            ax = (ax - q->lfo[0].val) & 0xFFFF;
        if (q->flags & 0x10)
            ax = (ax - q->lfo[1].val) & 0xFFFF;
    }
    if (ax >= 0x1000)
        ax = (ax & 0x8000) ? 0 : 0xFFF;
    r = (q->ch - 1u) * 2;
    w(p, r, (unsigned)ax & 0xFF);
    w(p, r + 1, (unsigned)ax >> 8);
}

static void ssg_volume(struct pmd *p, struct pmd_part *q)  /* 1B15 */
{
    unsigned m = E(q, 0x21);
    int dl;
    if (m == 3 || (m == 0xFF && E(q, 0x22) == 0))
        return;
    dl = q->tmpvol ? (int)((q->tmpvol - 1) & 0xFF) : q->vol;
    if (p->voldown[1])
        dl = (dl * (256 - p->voldown[1])) >> 8;
    if (p->fade)
        dl = (dl * (256 - p->fade)) >> 8;
    if (dl) {
        if (m == 0xFF) {
            unsigned lv = E(q, 0x2D);
            if (lv == 0)
                dl = 0;
            else {
                unsigned x = ((unsigned)dl * (lv + 1)) & 0xFF;
                dl = (int)((x >> 4) + ((x >> 3) & 1));
            }
        } else {
            dl = dl + S8(E(q, 0x2D));
            if (dl < 0 || (dl & 0x80))
                dl = 0;
            else if (dl >= 16)
                dl = 15;
        }
        if (dl && (q->flags & 0x22)) {
            int a = 0, d;
            if (q->flags & 2)
                a = q->lfo[0].val;
            if (q->flags & 0x20)
                a = (a + q->lfo[1].val) & 0xFFFF;
            d = S16(dl + a);
            dl = d < 0 ? 0 : (d >= 16 ? 15 : d);
        }
    }
    w(p, 7 + q->ch, (unsigned)dl);
}

static void ssg_keyon(struct pmd *p, struct pmd_part *q)   /* 1BEA */
{
    unsigned bits, al, ah;
    if (q->note == 0xFF)
        return;
    bits = (1u << (q->ch - 1)) | (8u << (q->ch - 1));
    al = p->reg7 | bits;
    ah = (~(bits & q->mix)) & 0xFF;
    al &= ah;
    p->reg7 = (uint8_t)al;
    w(p, 7, al);
    if (p->noise != p->noisec && !p->sfx.active) {
        w(p, 6, p->noise);
        p->noisec = p->noise;
    }
}

static void ssg_keyoff(struct pmd_part *q)                 /* 1C5D */
{
    if (q->note == 0xFF)
        return;
    if (E(q, 0x21) != 0xFF)
        E(q, 0x21) = 2;
    else
        E(q, 0x22) = 4;
}

static void env_ext_step(struct pmd_part *q);

static void env_keyon(struct pmd_part *q)                  /* 1F50.. */
{
    if (E(q, 0x21) != 0xFF) {
        unsigned al;
        E(q, 0x21) = 0; E(q, 0x2D) = 0;
        al = E(q, 0x29);
        E(q, 0x23) = (uint8_t)al;
        if (al == 0) {
            E(q, 0x21) = 1; E(q, 0x2D) = E(q, 0x24);
        }
        E(q, 0x25) = E(q, 0x2B); E(q, 0x26) = E(q, 0x2C);
    } else {
        int a;
        E(q, 0x29) = (uint8_t)(E(q, 0x23) - 16);
        a = S8(E(q, 0x24) - 16); E(q, 0x2A) = (uint8_t)(a < 0 ? a * 2 : a);
        a = S8(E(q, 0x25) - 16); E(q, 0x2B) = (uint8_t)(a < 0 ? a * 2 : a);
        E(q, 0x2C) = (uint8_t)(2 * E(q, 0x26) - 16);
        E(q, 0x2D) = E(q, 0x28);
        E(q, 0x22) = 1;
        env_ext_step(q);
    }
}

static void env_normal_step(struct pmd_part *q)            /* 2091 */
{
    unsigned st = E(q, 0x21), v, lv;
    if (st == 0) {
        v = (E(q, 0x23) - 1) & 0xFF;
        E(q, 0x23) = (uint8_t)v;
        if (v == 0) {
            E(q, 0x21) = 1; E(q, 0x2D) = E(q, 0x24);
        }
        return;
    }
    if (st == 2) {
        if (E(q, 0x26) == 0) {
            E(q, 0x2D) = 0xF1;
            return;
        }
        v = (E(q, 0x26) - 1) & 0xFF;
        E(q, 0x26) = (uint8_t)v;
        if (v)
            return;
        E(q, 0x26) = E(q, 0x2C);
    } else {
        if (E(q, 0x25) == 0)
            return;
        v = (E(q, 0x25) - 1) & 0xFF;
        E(q, 0x25) = (uint8_t)v;
        if (v)
            return;
        E(q, 0x25) = E(q, 0x2B);
    }
    lv = (E(q, 0x2D) - 1) & 0xFF;
    if (!(lv >= 0xF1 || lv < 0x0F))
        lv = 0xF1;
    E(q, 0x2D) = (uint8_t)lv;
}

static void env_ext_step(struct pmd_part *q)               /* 20E9 / 20FE */
{
    unsigned ph = E(q, 0x22);
    int lv = E(q, 0x2D);
    unsigned al;
    int a, r;
    if (ph == 0)
        return;
    if (ph == 1) {
        al = E(q, 0x29);
        if (S8(al - 1) < 0) {
            if (E(q, 0x23))
                E(q, 0x29) = (uint8_t)(al + 1);
            return;
        }
        lv = lv + (int)al;
        if ((lv & 0xFF) >= 0x0F || lv > 0xFF) {
            E(q, 0x2D) = 0x0F;
            E(q, 0x22) = 2;
            if (E(q, 0x27) == 0x0F)
                E(q, 0x22) = 3;
            return;
        }
        E(q, 0x2D) = (uint8_t)lv;
        E(q, 0x29) = (uint8_t)(E(q, 0x23) - 16);
        return;
    }
    if (ph == 2) {
        unsigned sl;
        al = E(q, 0x2A);
        if (S8(al - 1) < 0) {
            if (E(q, 0x24))
                E(q, 0x2A) = (uint8_t)(al + 1);
            return;
        }
        r = lv - (int)al;
        sl = E(q, 0x27);
        if (r < 0 || r < (int)sl) {
            E(q, 0x2D) = (uint8_t)sl; E(q, 0x22) = 3;
            return;
        }
        E(q, 0x2D) = (uint8_t)r;
        a = S8(E(q, 0x24) - 16); E(q, 0x2A) = (uint8_t)(a < 0 ? a * 2 : a);
        return;
    }
    if (ph == 3) {
        al = E(q, 0x2B);
        if (S8(al - 1) < 0) {
            if (E(q, 0x25))
                E(q, 0x2B) = (uint8_t)(al + 1);
            return;
        }
        r = lv - (int)al;
        E(q, 0x2D) = (uint8_t)(r < 0 ? 0 : r);
        a = S8(E(q, 0x25) - 16); E(q, 0x2B) = (uint8_t)(a < 0 ? a * 2 : a);
        return;
    }
    al = E(q, 0x2C);
    if (S8(al - 1) < 0) {
        if (E(q, 0x26))
            E(q, 0x2C) = (uint8_t)(al + 1);
        return;
    }
    r = lv - (int)al;
    E(q, 0x2D) = (uint8_t)(r < 0 ? 0 : r);
    E(q, 0x2C) = (uint8_t)(2 * E(q, 0x26) - 16);
}

static int env_tick1(struct pmd_part *q)                   /* 207E */
{
    unsigned old = E(q, 0x2D);
    if (E(q, 0x21) == 0xFF)
        env_ext_step(q);
    else
        env_normal_step(q);
    return E(q, 0x2D) != old;
}

static int env_tick(struct pmd *p, struct pmd_part *q)     /* 205E */
{
    if (q->ext & 4) {
        unsigned n = p->na, i;
        int ch = 0;
        for (i = 0; i < n; i++)
            if (env_tick1(q))
                ch = 1;
        return ch;
    }
    return env_tick1(q);
}

/* ---- commands ---------------------------------------------------------------------------- */

enum { ACT_NONE = 0, ACT_MODE = 1 };

/* execute command c with operands at si; returns the new si, *act = ACT_MODE when the handler
 * jumped back into the parse loops (C0, CF) */
static unsigned cmd(struct pmd *p, struct pmd_part *q, unsigned si, unsigned c, int *act)
{
    int fm = q->kind_fm;
    *act = ACT_NONE;
    switch (c) {
    case 0xFF:
        q->voice = (uint8_t)rb(p, q, si);
        if (fm) {
            if (q->mask == 0)
                voice_load(p, q);
            else {
                unsigned bx = voice_rec(p, q, q->voice);
                int i;
                q->fbalg = (uint8_t)rb(p, q, bx + 24);
                for (i = 0; i < 4; i++)
                    q->tl[i] = (uint8_t)rb(p, q, bx + 4 + (unsigned)i);
            }
        }
        return si + 1;
    case 0xFE: q->q = (uint8_t)rb(p, q, si); return si + 1;
    case 0xFD: q->vol = (uint8_t)rb(p, q, si); return si + 1;
    case 0xFC: {
        unsigned al = rb(p, q, si);
        si++;
        if (al < 0xFB) {
            unsigned bl;
            p->TB = (uint8_t)al;
            bl = (256 - al) & 0xFF;
            if (bl < 0x12)
                p->t = 0xFF;
            else
                p->t = (uint8_t)(0x112C / bl + ((0x112C % bl) & 0x80 ? 1 : 0));
        } else if (al == 0xFF) {
            unsigned t = rb(p, q, si);
            si++;
            set_t(p, t < 0x12 ? 0x12 : t);
        } else if (al == 0xFE) {
            int d = S8(rb(p, q, si)), v;
            si++;
            v = p->TB + d;
            if (d >= 0)
                v = v > 0xFF ? 0xFA : v;
            else
                v = v < 0 ? 0 : v;
            if (v >= 0xFB)
                v = 0xFA;
            p->TB = (uint8_t)v;
        } else {
            int d = S8(rb(p, q, si)), v;
            si++;
            v = p->t + d;
            v = v > 0xFF ? 0xFF : (v < 0x12 ? 0x12 : v);
            set_t(p, (unsigned)v);
        }
        return si;
    }
    case 0xFB: p->tie |= 1; return si;
    case 0xFA: q->detune = (int16_t)rw(p, q, si); return si + 2;
    case 0xD5: q->detune = (int16_t)((q->detune + rw(p, q, si)) & 0xFFFF); return si + 2;
    case 0xF9: wb(p, q, rw(p, q, si) + 1, 0); return si + 2;
    case 0xF8: {
        unsigned cnt = rb(p, q, si);
        if (cnt == 0) {
            q->lstat = 1;
            return rw(p, q, si + 2) + 2;
        }
        wb(p, q, si + 1, (rb(p, q, si + 1) + 1) & 0xFF);
        if (rb(p, q, si + 1) == cnt)
            return si + 4;
        return rw(p, q, si + 2) + 2;
    }
    case 0xF7: {
        unsigned bx = rw(p, q, si);
        if (((rb(p, q, bx) - 1) & 0xFF) == rb(p, q, bx + 1))
            return bx + 4;
        return si + 2;
    }
    case 0xF6: q->loop = (uint16_t)si; return si;
    case 0xF5: q->trans = (uint8_t)rb(p, q, si); return si + 1;
    case 0xE7: q->trans = (uint8_t)(q->trans + rb(p, q, si)); return si + 1;
    case 0xF4: case 0xE3: case 0xF3: case 0xE2: {
        int n, v;
        if (c == 0xF4 || c == 0xF3)
            n = fm ? 4 : 1;
        else {
            n = (int)rb(p, q, si);
            si++;
        }
        if (c == 0xF4 || c == 0xE3) {
            int lim = fm ? 0x80 : 0x10;
            v = (q->vol + n) & 0xFF;
            if (v >= lim)
                v = lim - 1;
        } else {
            v = q->vol - n;
            if (v < 0)
                v = 0;
        }
        q->vol = (uint8_t)v;
        return si;
    }
    case 0xF2: case 0xBF: {
        struct pmd_lfo *L = &q->lfo[c == 0xF2 ? 0 : 1];
        L->w[0] = L->w[4] = (uint8_t)rb(p, q, si);
        L->w[1] = L->w[5] = (uint8_t)rb(p, q, si + 1);
        L->w[2] = L->w[6] = (uint8_t)rb(p, q, si + 2);
        L->w[3] = L->w[7] = (uint8_t)rb(p, q, si + 3);
        lfo_reset(q, c == 0xF2 ? 0 : 1);
        return si + 4;
    }
    case 0xF1: {
        unsigned al = rb(p, q, si);
        if (al & 0xF8)
            al = 1;
        q->flags = (uint8_t)((q->flags & 0xF8) | (al & 7));
        lfo_reset(q, 0);
        if (fm && is_ch3(q))
            fm3_update(p, q);
        return si + 1;
    }
    case 0xCB: q->lfo[0].wave = (uint8_t)rb(p, q, si); return si + 1;
    case 0xBC: q->lfo[1].wave = (uint8_t)rb(p, q, si); return si + 1;
    case 0xCA: q->ext = (uint8_t)((q->ext & 0xFD) | ((rb(p, q, si) & 1) << 1)); return si + 1;
    case 0xBB: q->ext = (uint8_t)((q->ext & 0xDF) | ((rb(p, q, si) & 1) << 5)); return si + 1;
    case 0xCC:
        if (!fm)
            q->ext = (uint8_t)((q->ext & 0xFE) | (rb(p, q, si) & 1));
        return si + 1;
    case 0xC9:
        if (!fm)
            q->ext = (uint8_t)((q->ext & 0xFB) | ((rb(p, q, si) & 1) << 2));
        return si + 1;
    case 0xEF: w(p, rb(p, q, si), rb(p, q, si + 1)); return si + 2;
    case 0xD4: {                                      /* SSG effect (0167): n = 0 stops */
        extern void pmd_ssg_effect_start(struct pmd *, int, int);
        extern void pmd_ssg_effect_stop(struct pmd *);
        unsigned n = rb(p, q, si);
        if (n == 0)
            pmd_ssg_effect_stop(p);
        else
            pmd_ssg_effect_start(p, (int)n, 1);
        return si + 1;
    }
    case 0xD3: {                                      /* FM effect from music data */
        unsigned n = rb(p, q, si);
        if (n == 0)
            pmd_fm_effect_off(p);
        else
            pmd_fm_effect_on(p, (int)n);
        return si + 1;
    }
    case 0xD2: p->fadesp = (uint8_t)rb(p, q, si); return si + 1;
    case 0xDC: p->status1 = (uint8_t)rb(p, q, si); return si + 1;
    case 0xDB: p->status1 = (uint8_t)(p->status1 + rb(p, q, si)); return si + 1;
    case 0xDF: return si + 1;
    case 0xDE: case 0xDD: {
        unsigned n = rb(p, q, si);
        int v;
        if (c == 0xDE) {
            v = (int)(n + q->vol);
            if (fm)
                v = (v & 0xFF) >= 0x80 ? 0x7F : (v & 0xFF);
            else
                v = (v & 0xFF) >= 0x10 ? 0x0F : (v & 0xFF);
        } else {
            v = (int)q->vol - (int)n;
            v = v < 0 ? 0 : (v >= 0xFF ? 0xFE : v);
        }
        q->tmpvol = (uint8_t)(v + 1);
        p->tvc = 1;
        return si + 1;
    }
    case 0xF0:
        if (fm)
            return si + 4;
        E(q, 0x23) = (uint8_t)rb(p, q, si); E(q, 0x29) = (uint8_t)rb(p, q, si);
        E(q, 0x24) = (uint8_t)rb(p, q, si + 1);
        E(q, 0x25) = (uint8_t)rb(p, q, si + 2); E(q, 0x2B) = (uint8_t)rb(p, q, si + 2);
        E(q, 0x26) = (uint8_t)rb(p, q, si + 3); E(q, 0x2C) = (uint8_t)rb(p, q, si + 3);
        if (E(q, 0x21) == 0xFF) {
            E(q, 0x21) = 2; E(q, 0x2D) = 0xF1;
        }
        return si + 4;
    case 0xCD:
        if (fm)
            return si + 5;
        E(q, 0x23) = (uint8_t)(rb(p, q, si) & 0x1F);
        E(q, 0x24) = (uint8_t)(rb(p, q, si + 1) & 0x1F);
        E(q, 0x25) = (uint8_t)(rb(p, q, si + 2) & 0x1F);
        {
            unsigned b4 = rb(p, q, si + 3);
            E(q, 0x26) = (uint8_t)(b4 & 0x0F);
            E(q, 0x27) = (uint8_t)(((b4 >> 4) & 0x0F) ^ 0x0F);
        }
        E(q, 0x28) = (uint8_t)(rb(p, q, si + 4) & 0x0F);
        if (E(q, 0x21) != 0xFF) {
            E(q, 0x21) = 0xFF; E(q, 0x22) = 4; E(q, 0x2D) = 0;
        }
        return si + 5;
    case 0xEE:
        if (!fm)
            p->noise = (uint8_t)rb(p, q, si);
        return si + 1;
    case 0xD0:
        if (!fm) {
            int v = (S8(rb(p, q, si)) + p->noise) & 0xFF;
            if (v & 0x80)
                v = 0;
            if (v >= 0x20)
                v = 0x1F;
            p->noise = (uint8_t)v;
        }
        return si + 1;
    case 0xED:
        if (!fm)
            q->mix = (uint8_t)rb(p, q, si);
        return si + 1;
    case 0xC4: q->Q = (uint8_t)rb(p, q, si); return si + 1;
    case 0xC1: return si;
    case 0xCF: {
        unsigned al, ah, vm;
        if (!fm)
            return si + 1;
        al = rb(p, q, si);
        si++;
        if (al & 0x0F)
            q->car = (uint8_t)((al & 0x0F) << 4);
        else {
            unsigned b = is_ch3(q) ? p->fb3 : rb(p, q, voice_rec(p, q, q->voice) + 24);
            q->car = CARRIER[b & 7];
        }
        ah = al & 0xF0;
        if (q->slot == ah)
            return si;
        q->slot = (uint8_t)ah;
        if (ah)
            q->mask &= 0xDF;
        else
            q->mask |= 0x20;
        if (is_ch3(q)) {
            int idx = part_index(p, q), n;
            fm3_update(p, q);
            if (idx != 2) {
                for (n = 2; n < 5; n++) {
                    struct pmd_part *r;
                    if (n == idx)
                        break;
                    r = &p->part[n];
                    if (r->mask == 0 && !(r->kst & 1))
                        keyon(p, r);
                }
            }
        }
        vm = 0;
        if (ah & 0x80) vm |= 0x11;
        if (ah & 0x40) vm |= 0x44;
        if (ah & 0x20) vm |= 0x22;
        if (ah & 0x10) vm |= 0x88;
        q->vmask = (uint8_t)vm;
        *act = ACT_MODE;
        return si;
    }
    case 0xC8: case 0xC7: {
        unsigned bl, ax;
        int i;
        if (!fm || !is_ch3(q))
            return si + 3;
        bl = rb(p, q, si);
        ax = rw(p, q, si + 1);
        si += 3;
        for (i = 0; i < 4; i++)
            if (bl & (1u << i))
                p->sd[i] = (uint16_t)(c == 0xC8 ? ax : (p->sd[i] + ax) & 0xFFFF);
        p->sd_any = (uint8_t)((p->sd[0] | p->sd[1] | p->sd[2] | p->sd[3]) ? 1 : 0);
        fm3_update(p, q);
        return si;
    }
    case 0xC5: case 0xBA: {
        unsigned al;
        int k = c == 0xC5 ? 0 : 1;
        if (!fm)
            return si + 1;
        al = rb(p, q, si) & 0x0F;
        q->lslot[k] = (uint8_t)(al ? ((al << 4) | 0x0F) : q->car);
        if (is_ch3(q))
            fm3_update(p, q);
        return si + 1;
    }
    case 0xB8: {
        unsigned al, dl, ah;
        static const unsigned bits[4] = { 1, 2, 4, 8 };
        static const int tli[4] = { 0, 2, 1, 3 };
        static const unsigned regs[4] = { 0x3F, 0x47, 0x43, 0x4B };
        int i;
        if (!fm)
            return si + 2;
        al = rb(p, q, si);
        dl = rb(p, q, si + 1);
        si += 2;
        ah = al & 0x0F & (q->slot >> 4);
        for (i = 0; i < 4; i++) {
            int v;
            if (!(ah & bits[i]))
                continue;
            if (al & 0x80) {
                v = q->tl[tli[i]] + S8(dl);
                if ((v & 0x80) || v < 0)
                    v = (dl & 0x80) ? 0 : 0x7F;
            } else {
                v = (int)(dl & 0x7F);
            }
            if (q->mask == 0)
                w(p, regs[i] + q->ch, (unsigned)v);
            q->tl[tli[i]] = (uint8_t)v;
        }
        return si;
    }
    case 0xB6: {
        unsigned al, fb, dl;
        int ch3;
        if (!fm)
            return si + 1;
        al = rb(p, q, si);
        si++;
        ch3 = is_ch3(q);
        if (al & 0x80) {
            int cur, v;
            if (!(al & 0x40))
                al &= 7;
            cur = (int)(((ch3 ? p->fb3 : q->fbalg) >> 3) & 7);
            v = S8(al) + cur;
            fb = v < 0 ? 0 : (v >= 8 ? 0x38 : (unsigned)(v << 3));
        } else {
            fb = ((al << 3) | (al >> 5)) & 0xFF;
        }
        if (ch3) {
            if (!(q->slot & 0x10))
                return si;
            dl = (p->fb3 & 7) | fb;
            p->fb3 = (uint8_t)dl;
        } else {
            dl = (q->fbalg & 7) | fb;
        }
        w(p, 0xAF + q->ch, dl);
        q->fbalg = (uint8_t)dl;
        return si;
    }
    case 0xB5:
        q->kdm = (uint8_t)((((rb(p, q, si) & 0x0F) ^ 0x0F) << 4) & 0xFF);
        q->kd = q->kdc = (uint8_t)rb(p, q, si + 1);
        return si + 2;
    case 0xD6: case 0xBD: {
        struct pmd_lfo *L = &q->lfo[c == 0xD6 ? 0 : 1];
        L->md[1] = L->md[2] = (uint8_t)rb(p, q, si);
        L->md[0] = (uint8_t)rb(p, q, si + 1);
        return si + 2;
    }
    case 0xB7: {
        unsigned al = rb(p, q, si);
        int k = (al & 0x80) ? 1 : 0;
        al &= k ? 0x7F : 0xFF;
        if (al == 0)
            al = 0xFF;
        q->lfo[k].mdc[0] = q->lfo[k].mdc[1] = (uint8_t)al;
        return si + 1;
    }
    case 0xC2: case 0xB9: {
        int k = c == 0xC2 ? 0 : 1;
        q->lfo[k].w[0] = q->lfo[k].w[4] = (uint8_t)rb(p, q, si);
        lfo_reset(q, k);
        return si + 1;
    }
    case 0xBE:
        q->flags = (uint8_t)((q->flags & 0x8F) | ((rb(p, q, si) & 7) << 4));
        lfo_reset(q, 0);
        if (fm && is_ch3(q))
            fm3_update(p, q);
        return si + 1;
    case 0xC0: {
        unsigned al = rb(p, q, si);
        si++;
        if (al >= 2) {
            unsigned v;
            int idx;
            if (al < 0xF7)
                return si;                    /* invalid: treated as part end by the data */
            v = rb(p, q, si);
            si++;
            idx = (al == 0xFF || al == 0xFE) ? 0 : (al == 0xFD || al == 0xFC) ? 1 : -1;
            if (idx < 0)
                return si;
            if (al == 0xFF || al == 0xFD)
                p->voldown[idx] = (uint8_t)v;
            else if (v == 0)
                p->voldown[idx] = idx ? 0 : 0x10;
            else if (v & 0x80) {
                unsigned r = p->voldown[idx] + v;
                p->voldown[idx] = (uint8_t)(r > 0xFF ? (r & 0xFF) : 0);
            } else {
                unsigned r = p->voldown[idx] + v;
                p->voldown[idx] = (uint8_t)(r > 0xFF ? 0xFF : r);
            }
            return si;
        }
        if (al) {
            q->mask |= 0x40;
            if (q->mask == 0x40) {
                if (fm)
                    voice_mute(p, q);
                else {
                    unsigned bits = (1u << (q->ch - 1)) | (8u << (q->ch - 1));
                    p->reg7 |= (uint8_t)bits;
                    w(p, 7, p->reg7);
                }
            }
        } else {
            q->mask &= 0xBF;
            if (q->mask == 0 && fm)
                voice_restore(p, q);
        }
        *act = ACT_MODE;
        return si;
    }
    default:
        return si + (unsigned)skip_len(c);
    }
}

/* ---- part tick ----------------------------------------------------------------------------- */

enum { R_NOTE, R_END, R_PORTA, R_REMODE };

/* the command loop: returns R_NOTE (note byte in *al, *si after it), R_END, R_PORTA, R_REMODE */
static int parse(struct pmd *p, struct pmd_part *q, unsigned *si, unsigned *al)
{
    for (;;) {
        unsigned b = rb(p, q, *si);
        int act;
        (*si)++;
        if (b == 0x80) {
            (*si)--;
            q->addr = (uint16_t)*si;
            q->lstat = 3;
            q->note = 0xFF;
            if (q->loop) {
                *si = q->loop;
                q->lstat = 1;
                continue;
            }
            return R_END;
        }
        if (b < 0x80) {
            *al = b;
            return R_NOTE;
        }
        if (b < 0xB5) {                           /* invalid: becomes a part end */
            wb(p, q, *si - 1, 0x80);
            (*si)--;
            continue;
        }
        if (b == 0xDA)
            return R_PORTA;
        *si = cmd(p, q, *si, b, &act);
        if (act == ACT_MODE)
            return R_REMODE;
    }
}

static void after_note(struct pmd *p, struct pmd_part *q)  /* 06A8 */
{
    if (q->tmpvol && q->note != 0xFF) {
        p->tvc--;
        if (p->tvc) {
            p->tvc = 0;
            q->tmpvol = 0;
        }
    }
}

static void finish_note(struct pmd *p, struct pmd_part *q, unsigned si)
{
    q->addr = (uint16_t)si;
    p->tie = 0;
    p->tvc = 0;
    q->kst = (uint8_t)(rb(p, q, si) == 0xFB ? 2 : 0);
}

static void fm_prep(struct pmd *p, struct pmd_part *q, unsigned al)   /* 1FBF */
{
    if ((al & 0x0F) == 0x0F || (p->tie & 1)) {
        if ((al & 0x0F) != 0x0F)
            q->porta = 0;
        lfo_only(p, q);
        return;
    }
    q->porta = 0;
    lfo_keyon(p, q);
}

static void porta_step(struct pmd_part *q)                  /* 128A */
{
    int r;
    q->porta = (int16_t)((q->porta + q->pstep) & 0xFFFF);
    r = q->prem;
    if (r > 0) {
        q->prem = (int16_t)(r - 1);
        q->porta = (int16_t)((q->porta + 1) & 0xFFFF);
    } else if (r < 0) {
        q->prem = (int16_t)(r + 1);
        q->porta = (int16_t)((q->porta - 1) & 0xFFFF);
    }
}

static unsigned lfo_changes(struct pmd *p, struct pmd_part *q)
{
    unsigned cl = q->flags, f = cl & 8;
    if ((cl & 3) && lfo_tick(p, q, 0))
        f |= cl & 3;
    if ((cl & 0x30) && lfo_tick(p, q, 1))
        f |= q->flags & 0x30;
    return f;
}

static void fm_sustain(struct pmd *p, struct pmd_part *q)   /* 06EA */
{
    if (q->kdc) {
        q->kdc--;
        if (q->kdc == 0 && !(q->kst & 1))
            keyon(p, q);
    }
    if (q->flags) {
        unsigned f = lfo_changes(p, q);
        if (f & 0x19) {
            if (f & 8)
                porta_step(q);
            fm_freq(p, q);
        }
        if ((f & 0x22) || p->fadesp)
            fm_volume(p, q);
    } else if (p->fadesp) {
        fm_volume(p, q);
    }
}

static void fm_parse_note(struct pmd *p, struct pmd_part *q, unsigned si);

static void fm_masked_parse(struct pmd *p, struct pmd_part *q, unsigned si)
{
    unsigned al = 0;
    int r;
    for (;;) {
        r = parse(p, q, &si, &al);
        if (r == R_REMODE) {
            if (q->mask == 0) {
                fm_parse_note(p, q, si);
                return;
            }
            continue;
        }
        break;
    }
    if (r == R_END) {
        p->tie = 0;
        p->tvc = 0;
        return;
    }
    if (r == R_PORTA)
        si += 2;
    q->freq = 0;
    q->note = 0xFF;
    q->len = (uint8_t)rb(p, q, si);
    si++;
    q->addr = (uint16_t)si;
    p->tie = 0;
    p->tvc = 0;
}

static void fm_parse_note(struct pmd *p, struct pmd_part *q, unsigned si)
{
    unsigned al = 0;
    int r;
    q->flags &= 0xF7;
    for (;;) {
        r = parse(p, q, &si, &al);
        if (r == R_REMODE) {
            if (q->mask) {
                fm_masked_parse(p, q, si);
                return;
            }
            continue;
        }
        break;
    }
    if (r == R_END) {
        fm_sustain(p, q);
        return;
    }
    if (r == R_PORTA) {
        unsigned n1 = rb(p, q, si), n2;
        unsigned f1, f2, nt;
        int b1, b2, diff, qq;
        si++;
        fm_prep(p, q, n1);
        fm_note_freq(q, transpose(q, n1));
        f1 = q->freq; nt = q->note;
        n2 = rb(p, q, si);
        si++;
        fm_note_freq(q, transpose(q, n2));
        f2 = q->freq;
        q->note = (uint8_t)nt;
        q->freq = (uint16_t)f1;
        b1 = (int)((f1 >> 11) & 7);
        b2 = (int)((f2 >> 11) & 7);
        diff = (b2 - b1) * 0x26A + ((int)(f2 & 0x7FF) - (int)(f1 & 0x7FF));
        q->len = (uint8_t)rb(p, q, si);
        si++;
        si = calc_gate(p, q, si);
        qq = q->len ? diff / (int)q->len : 0;      /* C division truncates toward zero as int() */
        q->pstep = (int16_t)(qq & 0xFFFF);
        q->prem = (int16_t)((diff - qq * (int)q->len) & 0xFFFF);
        q->flags |= 8;
    } else {
        fm_prep(p, q, al);
        fm_note_freq(q, transpose(q, al));
        q->len = (uint8_t)rb(p, q, si);
        si++;
        si = calc_gate(p, q, si);
    }
    after_note(p, q);
    fm_volume(p, q);
    fm_freq(p, q);
    keyon(p, q);
    finish_note(p, q, si);
}

static void fm_masked(struct pmd *p, struct pmd_part *q)   /* 0796 */
{
    q->len--;
    if (q->len) {
        p->allst &= q->lstat;
        return;
    }
    /* channel taken by an effect: released at the note boundary once the effect is over */
    if ((q->mask & 2) && !p->fm_eff_active) {
        q->mask &= 0xFD;
        if (q->mask == 0) {
            fm_parse_note(p, q, q->addr);
            p->allst &= q->lstat;
            return;
        }
    }
    fm_masked_parse(p, q, q->addr);
    p->allst &= q->lstat;
}

static void fm_tick(struct pmd *p, struct pmd_part *q)
{
    if (q->addr == 0)
        return;
    if (q->mask) {
        fm_masked(p, q);
        return;
    }
    q->len--;
    if (!(q->kst & 3) && q->len <= q->gate) {
        keyoff(p, q, 1);
        q->kst = 0xFF;
    }
    if (q->len == 0)
        fm_parse_note(p, q, q->addr);
    else
        fm_sustain(p, q);
    p->allst &= q->lstat;
}

void pmd_ssg_effect_stop(struct pmd *p);

static void ssg_prep(struct pmd *p, struct pmd_part *q, unsigned al)   /* 1F34 */
{
    if ((al & 0x0F) == 0x0F) {
        lfo_only(p, q);
        return;
    }
    q->porta = 0;
    if (p->tie & 1) {
        lfo_only(p, q);
        return;
    }
    env_keyon(q);
    lfo_keyon(p, q);
}

static void ssg_sustain(struct pmd *p, struct pmd_part *q)  /* 089F */
{
    unsigned f = q->flags & 8;
    int ch;
    if (q->flags) {
        f = lfo_changes(p, q);
        if (f & 0x19) {
            if (f & 8)
                porta_step(q);
            ssg_freq(p, q);
        }
    }
    ch = env_tick(p, q);
    if (ch || (f & 0x22) || p->fadesp)
        ssg_volume(p, q);
}

static void ssg_parse_note(struct pmd *p, struct pmd_part *q, unsigned si);

static void ssg_masked_parse(struct pmd *p, struct pmd_part *q, unsigned si)
{
    unsigned al = 0;
    int r;
    for (;;) {
        r = parse(p, q, &si, &al);
        if (r == R_REMODE) {
            if (q->mask == 0) {
                ssg_parse_note(p, q, si);
                return;
            }
            continue;
        }
        break;
    }
    if (r == R_END) {
        p->tie = 0;
        p->tvc = 0;
        return;
    }
    if (r == R_PORTA)
        si += 2;
    q->freq = 0;
    q->note = 0xFF;
    q->len = (uint8_t)rb(p, q, si);
    si++;
    q->addr = (uint16_t)si;
    p->tie = 0;
    p->tvc = 0;
}

/* the note body of ssg_parse_note (r = R_NOTE with note al, or R_PORTA), si after the byte */
static void ssg_play_note(struct pmd *p, struct pmd_part *q, unsigned si, unsigned al, int r)
{
    if (r == R_PORTA) {
        unsigned n1 = rb(p, q, si), n2, f1, nt;
        int diff, qq;
        si++;
        ssg_prep(p, q, n1);
        ssg_period(q, transpose(q, n1));
        f1 = q->freq; nt = q->note;
        n2 = rb(p, q, si);
        si++;
        ssg_period(q, transpose(q, n2));
        diff = (int)q->freq - (int)f1;
        q->note = (uint8_t)nt;
        q->freq = (uint16_t)f1;
        q->len = (uint8_t)rb(p, q, si);
        si++;
        si = calc_gate(p, q, si);
        qq = q->len ? diff / (int)q->len : 0;
        q->pstep = (int16_t)(qq & 0xFFFF);
        q->prem = (int16_t)((diff - qq * (int)q->len) & 0xFFFF);
        q->flags |= 8;
    } else {
        ssg_prep(p, q, al);
        ssg_period(q, transpose(q, al));
        q->len = (uint8_t)rb(p, q, si);
        si++;
        si = calc_gate(p, q, si);
    }
    after_note(p, q);
    ssg_volume(p, q);
    ssg_freq(p, q);
    ssg_keyon(p, q);
    finish_note(p, q, si);
}

static void ssg_parse_note(struct pmd *p, struct pmd_part *q, unsigned si)
{
    unsigned al = 0;
    int r;
    for (;;) {
        r = parse(p, q, &si, &al);
        if (r == R_REMODE) {
            if (q->mask) {
                ssg_masked_parse(p, q, si);
                return;
            }
            continue;
        }
        break;
    }
    if (r == R_END) {
        ssg_sustain(p, q);
        return;
    }
    ssg_play_note(p, q, si, al, r);
}

/* masked part at a note boundary (0911): with bit 1 (SSG effect) a non-rest note takes the
 * channel back when the running effect has priority < 2 (095D) */
static void ssg_masked_boundary(struct pmd *p, struct pmd_part *q, unsigned si)
{
    unsigned al = 0;
    int r;
    for (;;) {
        r = parse(p, q, &si, &al);
        if (r == R_REMODE) {
            if (q->mask == 0) {
                ssg_parse_note(p, q, si);
                return;
            }
            continue;
        }
        break;
    }
    if (r == R_END) {
        p->tie = 0;
        p->tvc = 0;
        return;
    }
    if ((q->mask & 2) && (r == R_PORTA || (al & 0x0F) != 0x0F)) {
        if (p->sfx.active && p->sfx.priority < 2)
            pmd_ssg_effect_stop(p);
        if (!p->sfx.active) {
            q->mask &= 0xFD;
            if (q->mask == 0) {
                ssg_play_note(p, q, si, al, r);
                return;
            }
        }
    }
    if (r == R_PORTA)
        si += 2;
    q->freq = 0;
    q->note = 0xFF;
    q->len = (uint8_t)rb(p, q, si);
    si++;
    q->addr = (uint16_t)si;
    p->tie = 0;
    p->tvc = 0;
}

static void ssg_tick(struct pmd *p, struct pmd_part *q)
{
    if (q->addr == 0)
        return;
    if (q->mask) {
        q->len--;
        if (q->len == 0) {
            q->flags &= 0xF7;
            ssg_masked_boundary(p, q, q->addr);
        }
        p->allst &= q->lstat;
        return;
    }
    q->len--;
    if (!(q->kst & 3) && q->len <= q->gate) {
        ssg_keyoff(q);
        q->kst = 0xFF;
    }
    if (q->len == 0) {
        q->flags &= 0xF7;
        ssg_parse_note(p, q, q->addr);
    } else {
        ssg_sustain(p, q);
    }
    p->allst &= q->lstat;
}

/* ---- music clock (Timer B) ----------------------------------------------------------------- */

static void music_tick(struct pmd *p)                       /* 05A0 */
{
    int i;
    p->allst = 3;
    for (i = 6; i < 9; i++)
        ssg_tick(p, &p->part[i]);
    for (i = 0; i < 6; i++)
        fm_tick(p, &p->part[i]);
    if (p->allst) {
        for (i = 0; i < PMD_PARTS; i++)
            if (p->part[i].lstat != 3)
                p->part[i].lstat = 0;
        if (p->allst == 3)
            p->status2 = 0xFF;
        else {
            p->status2++;
            if (p->status2 == 0xFF)
                p->status2 = 1;
        }
    }
    tempo_reg(p);
}

void pmd_timer_b(struct pmd *p)                             /* 2B60 */
{
    if (!p->playing)
        return;
    p->na = (uint8_t)(p->a3119 - p->a30B7);
    music_tick(p);
    p->a30B7 = p->a3119;
}

/* ---- SSG built-in effects (pmd-driver.md §7.2) ---------------------------------------------- */

static void ssgfx_write_frame(struct pmd *p, const uint8_t *f)   /* 0217 */
{
    struct pmd_ssgfx *s = &p->sfx;
    unsigned mixer;
    s->count = f[0];
    s->tone = (uint16_t)(f[1] | (f[2] << 8));
    s->noise = f[3];
    s->mixer_bits = f[4];
    s->tone_delta = (int8_t)f[9];
    s->sweep = f[10];
    s->sweep_ctr = f[10] & 0x0F;
    w(p, 0x04, s->tone & 0xFF);
    w(p, 0x05, s->tone >> 8);
    w(p, 0x06, s->noise);
    mixer = (p->reg7 & ~0x24u) | ((f[4] & 1) << 2) | ((f[4] & 8) << 2);
    p->reg7 = (uint8_t)mixer;
    w(p, 0x07, mixer);
    w(p, 0x0A, f[5]);
    w(p, 0x0B, f[6]);
    w(p, 0x0C, f[7]);
    w(p, 0x0D, f[8]);
}

void pmd_ssg_effect_stop(struct pmd *p)                     /* 0286 */
{
    struct pmd_ssgfx *s = &p->sfx;
    if (!s->active)
        return;
    s->active = 0;
    w(p, 0x0A, 0);
    p->reg7 |= 0x24;
    w(p, 0x07, p->reg7);
    s->priority = 0;
    s->number = -1;
}

void pmd_ssg_effect_start(struct pmd *p, int n, int pps_flags)  /* 0173 */
{
    struct pmd_ssgfx *s = &p->sfx;
    const uint8_t *ent;
    unsigned pri, off;
    (void)pps_flags;
    if (!p->ssgtab || n < 0 || (unsigned)(3 * n + 3) > p->ssgtab_size)
        return;
    ent = p->ssgtab + 3 * n;
    pri = ent[0];
    off = ent[1] | (ent[2] << 8);
    if (s->active && (unsigned)s->priority > pri)
        return;
    if (off + 11 > p->ssgtab_size)
        return;
    p->part[8].mask |= 2;                   /* part I loses SSG channel 3 */
    s->active = 1;
    s->number = n;
    s->priority = (int)pri;
    s->frame = p->ssgtab + off;
    ssgfx_write_frame(p, s->frame);
}

static void ssgfx_tick(struct pmd *p)                       /* 0206 */
{
    struct pmd_ssgfx *s = &p->sfx;
    if (!s->active)
        return;
    s->count--;
    if (s->count <= 0) {
        s->frame += 11;
        if (s->frame >= p->ssgtab + p->ssgtab_size || s->frame[0] == 0xFF) {
            pmd_ssg_effect_stop(p);
            return;
        }
        ssgfx_write_frame(p, s->frame);
        return;
    }
    s->tone = (uint16_t)(s->tone + s->tone_delta);
    w(p, 0x04, s->tone & 0xFF);
    w(p, 0x05, s->tone >> 8);
    w(p, 0x07, p->reg7);
    if (s->sweep & 0x0F) {
        if (--s->sweep_ctr <= 0) {
            int d = (int8_t)(s->sweep & 0xF0) >> 4;
            s->sweep_ctr = s->sweep & 0x0F;
            s->noise = (uint8_t)((s->noise + d) & 0x1F);
            w(p, 0x06, s->noise);
        }
    }
}

/* ---- FM effects (pmd-driver.md §7.1) -------------------------------------------------------- */

static void efc_part_tick(struct pmd *p)                    /* 032B */
{
    struct pmd_part *q = &p->part[PMD_PART_X];
    if (!p->fm_eff_active)
        return;
    p->allst = 3;
    fm_tick(p, q);
    if (rb(p, q, q->addr) == 0x80 && q->len == 0)
        pmd_fm_effect_off(p);
}

void pmd_fm_effect_on(struct pmd *p, int n)                 /* 29A8 */
{
    struct pmd_part *q = &p->part[PMD_PART_X];
    int i;
    if (!p->efc || n < 0 || n > 126)
        return;
    if (p->fm_eff_active)
        pmd_fm_effect_off(p);
    p->fm_eff_number = n;
    p->fm_eff_active = 1;
    for (i = 2; i < 6; i++)
        p->part[i].mask |= 2;
    memset(q, 0, sizeof *q);
    q->kind_fm = 1;
    q->ch = 3;
    q->is_x = 1;
    q->addr = (uint16_t)(p->efc[2 * n] | (p->efc[2 * n + 1] << 8));
    q->len = 1;
    q->vol = 0x6C;
    q->slot = 0xF0;
    q->vmask = 0xFF;
    q->lfo[0].mdc[0] = q->lfo[0].mdc[1] = 0xFF;
    q->lfo[1].mdc[0] = q->lfo[1].mdc[1] = 0xFF;
    p->reg27_saved = p->reg27;
    p->reg27 = 0x3F;
    /* re-phase the timers like the emulator does at an effect start */
    if (p->ta_on) p->ta_next = p->clock + 72u * (1024u - ((p->reg24 << 2) | (p->reg25 & 3)));
    if (p->tb_on) p->tb_next = p->clock + 1152u * (256u - p->reg26);
}

void pmd_fm_effect_off(struct pmd *p)                       /* 2A1D */
{
    struct pmd_part *q = &p->part[PMD_PART_X];
    if (!p->fm_eff_active)
        return;
    p->fm_eff_number = -1;
    p->fm_eff_active = 0;
    voice_mute(p, q);
    if (p->playing) {
        int i;
        for (i = 2; i < 6; i++)
            voice_restore(p, &p->part[i]);
        p->reg27 = p->reg27_saved;
        w(p, 0x27, p->reg27 & 0xCF);
    }
}

void pmd_timer_a(struct pmd *p)                             /* 2BA7 */
{
    p->a3119++;
    if ((p->a3119 & 7) == 0)
        fade_step(p);
    ssgfx_tick(p);
    efc_part_tick(p);
}

/* ---- INT 60h ---------------------------------------------------------------------------- */

void pmd_init(struct pmd *p, pmd_write_fn write, void *ctx, uint32_t out_rate)
{
    memset(p, 0, sizeof *p);
    p->write = write;
    p->ctx = ctx;
    p->reg27 = 0x3F;
    p->reg7 = 0xBF;
    p->TB = p->TBc = 0xC8;              /* installer wrote 26h = C8h */
    p->t = 78;
    p->reg26 = 0xC8;
    p->voldown[0] = 0x10;
    p->status2 = 0xFF;
    p->fade = 0xFF;
    p->fm_eff_number = -1;
    p->sfx.number = -1;
    p->stop_after_fade = 0;             /* /F */
    pmd_set_rate(p, out_rate);
    /* installer chip initialisation (pmd-driver.md §3.4, YM2203 variant) */
    w(p, 0x0B, 0xAA);                   /* board detection write */
    w(p, 0x07, 0x3F);
    w(p, 0x29, 0x00); w(p, 0x24, 0x00); w(p, 0x25, 0x00); w(p, 0x26, 0x00); w(p, 0x27, 0x3F);
    w(p, 0x29, 0x83);
    w(p, 0x07, 0xBF);
    silence_all(p);
    p->TBc = 0;                         /* force the 26h = C8h write of 2214 */
    tempo_reg(p);
    w(p, 0x25, 0x00); w(p, 0x24, 0x00); w(p, 0x27, 0x3F);
    w(p, 0x29, 0x83);
    /* timers run from install on */
    p->ta_on = p->tb_on = 1;
    p->ta_next = p->clock + 72u * 1024u;
    p->tb_next = p->clock + 1152u * (256u - 0xC8u);
}

void pmd_set_rate(struct pmd *p, uint32_t out_rate)
{
    uint64_t cps = ((uint64_t)PMD_CLOCK << 16) / (out_rate ? out_rate : 44100u);
    p->cps_int = (uint32_t)(cps >> 16);
    p->cps_frac = (uint32_t)(cps & 0xFFFF);
    p->cps_acc = 0;
}

void pmd_set_efc(struct pmd *p, uint8_t *efc, uint32_t size)
{
    p->efc = efc;
    p->efcsize = size;
    p->efc_voicetab = size >= 0x100 ? (uint16_t)(efc[0xFE] | (efc[0xFF] << 8)) : 0;
}

void pmd_set_ssgtab(struct pmd *p, const uint8_t *tab, uint32_t size)
{
    p->ssgtab = tab;
    p->ssgtab_size = size;
}

void pmd_load_music(struct pmd *p, uint8_t *mdata, uint32_t size)
{
    p->m = mdata;
    p->msize = size;
}

static void silence_all(struct pmd *p)                      /* 2248 */
{
    static const unsigned offs[12] = { 0, 1, 2, 4, 5, 6, 8, 9, 0xA, 0xC, 0xD, 0xE };
    int i;
    for (i = 0; i < 12; i++) {
        if (p->fm_eff_active && (offs[i] & 3) == 2)
            continue;                                     /* list 22B9: without ch3 */
        w(p, 0x80 + offs[i], 0xFF);
    }
    w(p, 0x28, 0);
    w(p, 0x28, 1);
    if (!p->fm_eff_active)
        w(p, 0x28, 2);
    if (!p->sfx.active) {
        p->reg7 = 0xBF;
        w(p, 0x07, 0xBF);
    } else {
        p->reg7 = (uint8_t)((p->reg7 & 0x3F) | 0x9B);
        w(p, 0x07, p->reg7);
    }
}

void pmd_mstop(struct pmd *p)                               /* 0569 / 0582 */
{
    p->playing = 0;
    p->fadesp = 0;
    p->status2 = 0xFF;
    p->fade = 0xFF;
    silence_all(p);
}

void pmd_mstart(struct pmd *p)                              /* 035A */
{
    uint16_t hdr[11];
    int i;
    const uint8_t *d;
    if (!p->m || p->msize < 0x1A)
        return;
    d = p->m;
    /* 1. stop */
    p->playing = 0;
    p->fadesp = 0;
    p->status2 = 0xFF;
    p->fade = 0xFF;
    silence_all(p);
    /* 2. clear the part work areas, keeping mask bits 0-4 */
    for (i = 0; i < PMD_PARTS; i++) {
        uint8_t keep = p->part[i].mask & 0x1F;
        memset(&p->part[i], 0, sizeof p->part[i]);
        p->part[i].mask = keep;
        p->part[i].note = 0xFF;
        p->part[i].kst = 0xFF;
        p->part[i].lfo[0].mdc[0] = p->part[i].lfo[0].mdc[1] = 0xFF;
        p->part[i].lfo[1].mdc[0] = p->part[i].lfo[1].mdc[1] = 0xFF;
    }
    p->tie = 0; p->tvc = 0;
    p->status1 = 0;
    p->status2 = 0;
    p->a3119 = 0; p->a30B7 = 0;
    memset(p->ks, 0, sizeof p->ks);
    p->fb3 = 0; p->restore = 0;
    memset(p->sd, 0, sizeof p->sd);
    p->sd_any = 0; p->fm3need = 0;
    p->reg27 = 0x3F;
    p->voldown[0] = 0x10;
    p->voldown[1] = 0;
    /* 3. header */
    for (i = 0; i < 11; i++)
        hdr[i] = (uint16_t)(d[2 * i] | (d[2 * i + 1] << 8));
    p->has_voices = hdr[0] != 0x18;
    p->voicetab = p->has_voices ? (uint16_t)(d[0x18] | (d[0x19] << 8)) : 0;
    for (i = 0; i < PMD_PARTS; i++) {
        struct pmd_part *q = &p->part[i];
        unsigned a = hdr[i];
        q->kind_fm = i < 6;
        q->ch = (uint8_t)(i < 3 ? i + 1 : i < 6 ? 3 : i - 5);
        q->is_x = 0;
        q->addr = (uint16_t)((a < p->msize && d[a] == 0x80) ? 0 : a);
        q->len = 1;
        if (i < 6) {
            q->vol = 0x6C;
            if (i < 3) {
                q->slot = 0xF0;
                q->vmask = 0xFF;
            } else {
                q->mask |= 0x20;
            }
        } else {
            q->vol = 8;
            q->mix = 7;
            E(q, 0x21) = 3;
        }
    }
    /* 4-6 */
    p->fade = 0;
    w(p, 0x29, 0x83);
    p->noise = 0; p->noisec = 0;
    if (p->sfx.active)
        w(p, 0x06, 0);
    p->TB = 0xC8;
    p->t = 78;
    tempo_reg(p);
    w(p, 0x25, 0);
    w(p, 0x24, 0);
    w(p, 0x27, 0x3F);
    /* the emulator re-phases both timers at song start */
    p->ta_next = p->clock + 72u * 1024u;
    p->tb_next = p->clock + 1152u * (256u - p->reg26);
    p->ta_on = p->tb_on = 1;
    /* 7 */
    p->playing = 1;
}

void pmd_fade(struct pmd *p, uint8_t speed)
{
    p->fadesp = speed;
}

uint8_t pmd_get_fadevol(const struct pmd *p)
{
    return p->fade;
}

void pmd_set_fadevol(struct pmd *p, uint8_t v)
{
    p->fade = v;
}

void pmd_get_status(const struct pmd *p, uint8_t *s1, uint8_t *s2)
{
    *s1 = p->status1;
    *s2 = p->status2;
}

/* ---- timers --------------------------------------------------------------------------------- */

/* OPN timer registers as the emulator models them (pmdemu.py _reg27): the load bit starts a
 * stopped timer; periods are sampled at each overflow. */
static void timer_reg(struct pmd *p, unsigned r, unsigned v)
{
    if (r == 0x24) p->reg24 = (uint8_t)v;
    else if (r == 0x25) p->reg25 = (uint8_t)v;
    else if (r == 0x26) p->reg26 = (uint8_t)v;
    else if (r == 0x27) {
        if (v & 1) {
            if (!p->ta_on) {
                p->ta_on = 1;
                p->ta_next = p->clock + 72u * (1024u - ((p->reg24 << 2) | (p->reg25 & 3)));
            }
        } else {
            p->ta_on = 0;
        }
        if (v & 2) {
            if (!p->tb_on) {
                p->tb_on = 1;
                p->tb_next = p->clock + 1152u * (256u - p->reg26);
            }
        } else {
            p->tb_on = 0;
        }
    }
}

static void fire_events(struct pmd *p)
{
    for (;;) {
        uint64_t next = (uint64_t)-1;
        int a = 0, b = 0;
        if (p->ta_on && p->ta_next < next) next = p->ta_next;
        if (p->tb_on && p->tb_next < next) next = p->tb_next;
        if (next > p->clock)
            return;
        if (p->ta_on && p->ta_next <= next) {
            p->ta_next += 72u * (1024u - ((p->reg24 << 2) | (p->reg25 & 3)));
            a = 1;
        }
        if (p->tb_on && p->tb_next <= next) {
            p->tb_next += 1152u * (256u - p->reg26);
            b = 1;
        }
        /* timer_dispatch 2B13: acknowledge with 27h = 30AD, B work first, then A */
        w(p, 0x27, p->reg27);
        if (b)
            pmd_timer_b(p);
        if (a)
            pmd_timer_a(p);
    }
}

void pmd_advance(struct pmd *p, int frames, void (*render)(void *ctx, int nframes), void *rctx)
{
    while (frames > 0) {
        uint64_t next = (uint64_t)-1;
        int n;
        fire_events(p);
        if (p->ta_on && p->ta_next < next) next = p->ta_next;
        if (p->tb_on && p->tb_next < next) next = p->tb_next;
        /* samples until the next event */
        if (next == (uint64_t)-1) {
            n = frames;
        } else {
            uint64_t dc = next - p->clock;
            uint64_t cps = ((uint64_t)p->cps_int << 16) | p->cps_frac;
            uint64_t ns = ((dc << 16) + cps - 1) / cps;
            n = ns > (uint64_t)frames ? frames : (int)ns;
            if (n < 1)
                n = 1;
        }
        render(rctx, n);
        {
            uint64_t adv = (uint64_t)n * p->cps_int;
            uint64_t fr = (uint64_t)n * p->cps_frac + p->cps_acc;
            adv += fr >> 16;
            p->cps_acc = (uint32_t)(fr & 0xFFFF);
            p->clock += adv;
        }
        frames -= n;
    }
    fire_events(p);
}

