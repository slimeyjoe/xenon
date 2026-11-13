/* opn_lite: the engine's own small YM2203 (OPN) core for low-end machines.
 *
 * Integer-only C99, no divisions in the per-sample path, renders directly at the output
 * rate instead of the chip's 55 467 Hz: phase steps and the envelope clock are scaled by
 * 55 467 / rate, so pitch, envelope times and levels match the chip. The FM behaviour
 * (log-sin / exp tables, 10-bit envelope generator with rate scaling and the OPN attack
 * curve, detune, multiple, feedback, the 8 algorithms, FM3 per-slot frequencies) is a port of
 * ymfm's OPN implementation (BSD-3-Clause, Aaron Giles) and is verified against it by
 * tests/check_opn.py. Deliberate simplifications (plan §6): the prescaler is fixed at /6
 * (/4 for the SSG), no CSM, no SSG-EG (Xenon's voices never enable it), no status/timer
 * emulation (timers live in the sequencer), no DAC floating-point rounding, register and
 * key events take effect immediately instead of at the next chip clock.
 *
 * SSG: tones, noise and the hardware envelope are modelled by integrating the square wave
 * over each output sample (the fraction of the sample during which the channel output was
 * high), which is exact for the tone frequencies in use and acts as a first-order
 * anti-alias filter; a channel with tone and noise enabled together uses the product of the
 * two duty fractions. Levels, mixing and the output DC-blocking filter mirror opn_ymfm.cpp
 * so the two cores are interchangeable.
 *
 * Output: mono 16-bit at the rate returned by create(). FM harmonics above half the output
 * rate fold back (the chip itself runs at 55 467 Hz); with opn_lite_fm_oversample = 2 the FM
 * part is computed at twice the rate and decimated with an 11-tap half-band filter, which
 * removes most of that aliasing at about 1.6x the FM cost.
 *
 * Cost on a 68040/25 (probes/amiga/README.md "Probe 3", measured with opnprof on a real
 * A1200): the envelope generator runs at 18 489 clocks/s whatever the output rate, so it is
 * the fixed cost; the per-sample cost is the operators. Work is skipped only where the
 * output is provably unchanged (every step below left all 81 reference renders
 * bit-identical): a fully released operator (0x3ff) is not visited again until a key-on or
 * a register write re-arms it; a channel is visited only when its earliest operator is due
 * (chan.next); a channel whose carriers are all above EG_QUIET outputs 0 and computes only
 * op1 (its feedback history); the SSG is not computed while no channel has a non-zero
 * amplitude (the hardware envelope counter is caught up when it is next needed); one
 * envelope deadline (eg_next) for the whole core. With XE_OPN_ASM_68K the os == 1 render
 * loop is opn_lite_68k.s (same arithmetic, checked bit for bit by probes/amiga "opnbench
 * sum"); this file's loop is the reference and the host/oversampled path. Board, XENON_24:
 * 154 % -> 65 % of a 68040/25 at 22 050 Hz, 102 % -> 42 % at 11 025 Hz. */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "snd/opn_core.h"

int opn_lite_fm_oversample = 1;

#define FM_DIV      72u             /* master clock / 72 = FM sample rate */
#define SSG_DIV     16u             /* master clock / 16 = SSG counter clock */
#define EG_QUIET    0x380
#define EG_PER_FM   3               /* envelope clock = FM rate / 3 */
#define TONE_MIN    2048            /* half period below 1/32 sample: treat as DC (>16 toggles / sample) */

enum { EG_ATTACK, EG_DECAY, EG_SUSTAIN, EG_RELEASE };

struct op {
    uint32_t phase;                 /* wave index in bits 18..27 */
    uint32_t step;                  /* phase increment per (oversampled) output sample */
    uint32_t next;                  /* envelope clock count of the next visit (clock_env) */
    uint16_t env;                   /* 10-bit envelope attenuation */
    uint16_t att;                   /* min(env + tl, 0x3ff) << 2, cached */
    uint16_t tl;                    /* total level << 3 */
    uint16_t sl;                    /* sustain level, 10-bit */
    uint8_t state;
    uint8_t key;
    uint8_t rate[4];                /* effective rates A/D/S/R after key scaling */
};

struct chan {
    struct op op[4];                /* algorithm order: op1 (S1), op2 (S2), op3 (S3), op4 (S4) */
    int32_t fb1, fb2;               /* op1's two previous outputs (feedback) */
    uint32_t next;                  /* min of the operators' next (envelope clock) */
    uint8_t active;
    uint8_t quiet;                  /* every carrier is above EG_QUIET: the output is 0 */
    uint8_t alg, fb;                /* from register B0 */
};

struct ssg_tone {
    uint32_t next;                  /* sample-fraction units (1/65536 sample) to the next toggle */
    uint32_t half;                  /* half period in units */
    uint32_t state;
};

struct opn_core {
    uint32_t rate;
    uint32_t fstep;                 /* FM samples per (oversampled) output sample, 16.16 */
    uint32_t fint, fhi, flo;        /* fstep split for the 32-bit step scaling */
    int os;                         /* FM oversampling factor */
    uint32_t eg_acc;                /* FM samples towards the next envelope clock, 16.16 */
    uint32_t egc;                   /* envelope clock counter */
    uint32_t eg_next;               /* min of the active channels' next: nothing is due before */
    uint8_t regs[256];
    uint8_t latch[2];               /* A4-A6 / AC-AE high-byte latches */
    struct chan ch[3];

    /* SSG */
    uint32_t pq4;                   /* sample-fraction units per SSG clock, 28.4 */
    uint32_t sstep;                 /* SSG clocks per output sample, 16.16 */
    struct ssg_tone tone[3];
    struct ssg_tone noise;
    uint32_t lfsr;
    uint32_t env_period;
    uint32_t env_count, env_acc, env_state;
    uint32_t ssg_on;                /* some channel has a non-zero amplitude (regs 8-10) */
    uint32_t ssg_idle;              /* samples skipped since the SSG was last computed */

    /* output DC blocker */
    int32_t hp_x1, hp_y1;           /* y1 in 24.8 */
    uint32_t hp_k;                  /* 65536 * (1 - pole) */

    /* 2x FM oversampling: half-band decimator history */
    int32_t hb[16];
    uint32_t hb_pos;

    /* render_split: one DC blocker per stream and one decimator history per FM channel */
    int32_t sp_x1[OPN_SPLIT_STREAMS], sp_y1[OPN_SPLIT_STREAMS];
    int32_t sp_hb[3][16];
};

/* ---- tables (die-extracted, via ymfm) --------------------------------------------------- */

static const uint16_t sin_table[256] = {
    0x859,0x6c3,0x607,0x58b,0x52e,0x4e4,0x4a6,0x471,0x443,0x41a,0x3f5,0x3d3,0x3b5,0x398,0x37e,0x365,
    0x34e,0x339,0x324,0x311,0x2ff,0x2ed,0x2dc,0x2cd,0x2bd,0x2af,0x2a0,0x293,0x286,0x279,0x26d,0x261,
    0x256,0x24b,0x240,0x236,0x22c,0x222,0x218,0x20f,0x206,0x1fd,0x1f5,0x1ec,0x1e4,0x1dc,0x1d4,0x1cd,
    0x1c5,0x1be,0x1b7,0x1b0,0x1a9,0x1a2,0x19b,0x195,0x18f,0x188,0x182,0x17c,0x177,0x171,0x16b,0x166,
    0x160,0x15b,0x155,0x150,0x14b,0x146,0x141,0x13c,0x137,0x133,0x12e,0x129,0x125,0x121,0x11c,0x118,
    0x114,0x10f,0x10b,0x107,0x103,0x0ff,0x0fb,0x0f8,0x0f4,0x0f0,0x0ec,0x0e9,0x0e5,0x0e2,0x0de,0x0db,
    0x0d7,0x0d4,0x0d1,0x0cd,0x0ca,0x0c7,0x0c4,0x0c1,0x0be,0x0bb,0x0b8,0x0b5,0x0b2,0x0af,0x0ac,0x0a9,
    0x0a7,0x0a4,0x0a1,0x09f,0x09c,0x099,0x097,0x094,0x092,0x08f,0x08d,0x08a,0x088,0x086,0x083,0x081,
    0x07f,0x07d,0x07a,0x078,0x076,0x074,0x072,0x070,0x06e,0x06c,0x06a,0x068,0x066,0x064,0x062,0x060,
    0x05e,0x05c,0x05b,0x059,0x057,0x055,0x053,0x052,0x050,0x04e,0x04d,0x04b,0x04a,0x048,0x046,0x045,
    0x043,0x042,0x040,0x03f,0x03e,0x03c,0x03b,0x039,0x038,0x037,0x035,0x034,0x033,0x031,0x030,0x02f,
    0x02e,0x02d,0x02b,0x02a,0x029,0x028,0x027,0x026,0x025,0x024,0x023,0x022,0x021,0x020,0x01f,0x01e,
    0x01d,0x01c,0x01b,0x01a,0x019,0x018,0x017,0x017,0x016,0x015,0x014,0x014,0x013,0x012,0x011,0x011,
    0x010,0x00f,0x00f,0x00e,0x00d,0x00d,0x00c,0x00c,0x00b,0x00a,0x00a,0x009,0x009,0x008,0x008,0x007,
    0x007,0x007,0x006,0x006,0x005,0x005,0x005,0x004,0x004,0x004,0x003,0x003,0x003,0x002,0x002,0x002,
    0x002,0x001,0x001,0x001,0x001,0x001,0x001,0x001,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000
};

#define X(a) (uint16_t)((((a) | 0x400) << 2))
const uint16_t opn_lite_pow[256] = {
    X(0x3fa),X(0x3f5),X(0x3ef),X(0x3ea),X(0x3e4),X(0x3df),X(0x3da),X(0x3d4),
    X(0x3cf),X(0x3c9),X(0x3c4),X(0x3bf),X(0x3b9),X(0x3b4),X(0x3ae),X(0x3a9),
    X(0x3a4),X(0x39f),X(0x399),X(0x394),X(0x38f),X(0x38a),X(0x384),X(0x37f),
    X(0x37a),X(0x375),X(0x370),X(0x36a),X(0x365),X(0x360),X(0x35b),X(0x356),
    X(0x351),X(0x34c),X(0x347),X(0x342),X(0x33d),X(0x338),X(0x333),X(0x32e),
    X(0x329),X(0x324),X(0x31f),X(0x31a),X(0x315),X(0x310),X(0x30b),X(0x306),
    X(0x302),X(0x2fd),X(0x2f8),X(0x2f3),X(0x2ee),X(0x2e9),X(0x2e5),X(0x2e0),
    X(0x2db),X(0x2d6),X(0x2d2),X(0x2cd),X(0x2c8),X(0x2c4),X(0x2bf),X(0x2ba),
    X(0x2b5),X(0x2b1),X(0x2ac),X(0x2a8),X(0x2a3),X(0x29e),X(0x29a),X(0x295),
    X(0x291),X(0x28c),X(0x288),X(0x283),X(0x27f),X(0x27a),X(0x276),X(0x271),
    X(0x26d),X(0x268),X(0x264),X(0x25f),X(0x25b),X(0x257),X(0x252),X(0x24e),
    X(0x249),X(0x245),X(0x241),X(0x23c),X(0x238),X(0x234),X(0x230),X(0x22b),
    X(0x227),X(0x223),X(0x21e),X(0x21a),X(0x216),X(0x212),X(0x20e),X(0x209),
    X(0x205),X(0x201),X(0x1fd),X(0x1f9),X(0x1f5),X(0x1f0),X(0x1ec),X(0x1e8),
    X(0x1e4),X(0x1e0),X(0x1dc),X(0x1d8),X(0x1d4),X(0x1d0),X(0x1cc),X(0x1c8),
    X(0x1c4),X(0x1c0),X(0x1bc),X(0x1b8),X(0x1b4),X(0x1b0),X(0x1ac),X(0x1a8),
    X(0x1a4),X(0x1a0),X(0x19c),X(0x199),X(0x195),X(0x191),X(0x18d),X(0x189),
    X(0x185),X(0x181),X(0x17e),X(0x17a),X(0x176),X(0x172),X(0x16f),X(0x16b),
    X(0x167),X(0x163),X(0x160),X(0x15c),X(0x158),X(0x154),X(0x151),X(0x14d),
    X(0x149),X(0x146),X(0x142),X(0x13e),X(0x13b),X(0x137),X(0x134),X(0x130),
    X(0x12c),X(0x129),X(0x125),X(0x122),X(0x11e),X(0x11b),X(0x117),X(0x114),
    X(0x110),X(0x10c),X(0x109),X(0x106),X(0x102),X(0x0ff),X(0x0fb),X(0x0f8),
    X(0x0f4),X(0x0f1),X(0x0ed),X(0x0ea),X(0x0e7),X(0x0e3),X(0x0e0),X(0x0dc),
    X(0x0d9),X(0x0d6),X(0x0d2),X(0x0cf),X(0x0cc),X(0x0c8),X(0x0c5),X(0x0c2),
    X(0x0be),X(0x0bb),X(0x0b8),X(0x0b5),X(0x0b1),X(0x0ae),X(0x0ab),X(0x0a8),
    X(0x0a4),X(0x0a1),X(0x09e),X(0x09b),X(0x098),X(0x094),X(0x091),X(0x08e),
    X(0x08b),X(0x088),X(0x085),X(0x082),X(0x07e),X(0x07b),X(0x078),X(0x075),
    X(0x072),X(0x06f),X(0x06c),X(0x069),X(0x066),X(0x063),X(0x060),X(0x05d),
    X(0x05a),X(0x057),X(0x054),X(0x051),X(0x04e),X(0x04b),X(0x048),X(0x045),
    X(0x042),X(0x03f),X(0x03c),X(0x039),X(0x036),X(0x033),X(0x030),X(0x02d),
    X(0x02a),X(0x028),X(0x025),X(0x022),X(0x01f),X(0x01c),X(0x019),X(0x016),
    X(0x014),X(0x011),X(0x00e),X(0x00b),X(0x008),X(0x006),X(0x003),X(0x000)
};
#undef X

static const uint32_t inc_table[64] = {
    0x00000000, 0x00000000, 0x10101010, 0x10101010, 0x10101010, 0x10101010, 0x11101110, 0x11101110,
    0x10101010, 0x10111010, 0x11101110, 0x11111110, 0x10101010, 0x10111010, 0x11101110, 0x11111110,
    0x10101010, 0x10111010, 0x11101110, 0x11111110, 0x10101010, 0x10111010, 0x11101110, 0x11111110,
    0x10101010, 0x10111010, 0x11101110, 0x11111110, 0x10101010, 0x10111010, 0x11101110, 0x11111110,
    0x10101010, 0x10111010, 0x11101110, 0x11111110, 0x10101010, 0x10111010, 0x11101110, 0x11111110,
    0x10101010, 0x10111010, 0x11101110, 0x11111110, 0x10101010, 0x10111010, 0x11101110, 0x11111110,
    0x11111111, 0x21112111, 0x21212121, 0x22212221, 0x22222222, 0x42224222, 0x42424242, 0x44424442,
    0x44444444, 0x84448444, 0x84848484, 0x88848884, 0x88888888, 0x88888888, 0x88888888, 0x88888888
};

static const uint8_t detune_table[32][4] = {
    {0, 0, 1, 2}, {0, 0, 1, 2}, {0, 0, 1, 2}, {0, 0, 1, 2}, {0, 1, 2, 2}, {0, 1, 2, 3}, {0, 1, 2, 3}, {0, 1, 2, 3},
    {0, 1, 2, 4}, {0, 1, 3, 4}, {0, 1, 3, 4}, {0, 1, 3, 5}, {0, 2, 4, 5}, {0, 2, 4, 6}, {0, 2, 4, 6}, {0, 2, 5, 7},
    {0, 2, 5, 8}, {0, 3, 6, 8}, {0, 3, 6, 9}, {0, 3, 7, 10}, {0, 4, 8, 11}, {0, 4, 8, 12}, {0, 4, 9, 13}, {0, 5, 10, 14},
    {0, 5, 11, 16}, {0, 6, 12, 17}, {0, 6, 13, 19}, {0, 7, 14, 20}, {0, 8, 16, 22}, {0, 8, 16, 22}, {0, 8, 16, 22}, {0, 8, 16, 22}
};

/* SSG volume -> amplitude (MAME/ymfm), 32 steps */
static const int16_t ssg_amp[32] = {
        0,   32,   78,  141,  178,  222,  262,  306,  369,  441,  509,  585,  701,  836,  965, 1112,
     1334, 1595, 1853, 2146, 2576, 3081, 3576, 4135, 5000, 6006, 7023, 8155, 9963,11976,14132,16382
};


/* 1024-entry full-wave table: 4.8 log attenuation | sign << 15 (built on first use; the
 * tables are external for the assembly render loop, opn_lite_68k.s) */
uint16_t opn_lite_wave[1024];
static int wave_ready;

static void build_wave(void)
{
    int i;
    for (i = 0; i < 1024; i++) {
        uint32_t idx = (uint32_t)i;
        uint32_t t = (idx & 0x100) ? (~idx & 0xff) : (idx & 0xff);
        opn_lite_wave[i] = (uint16_t)(sin_table[t] | ((idx & 0x200) ? 0x8000 : 0));
    }
    wave_ready = 1;
}

/* ---- operator cache ---------------------------------------------------------------------- */

/* register offset of algorithm-order operator k of channel ch: S1 +0, S2 +8, S3 +4, S4 +12 */
static const uint8_t slot_off[4] = { 0, 8, 4, 12 };

static uint32_t scale_step(const struct opn_core *c, uint32_t ps)
{
    /* ps (<= 21 bits) * fstep (16.16) with 8 extra fraction bits, in 32-bit arithmetic */
    return (ps << 8) * c->fint + ps * c->fhi + ((ps * c->flo) >> 8);
}

static void update_att(struct op *o)
{
    uint32_t e = (uint32_t)o->env + o->tl;
    o->att = (uint16_t)((e > 0x3ff ? 0x3ff : e) << 2);
}

static void cache_op(struct opn_core *c, int ch, int k)
{
    struct op *o = &c->ch[ch].op[k];
    uint32_t off = (uint32_t)ch + slot_off[k];
    uint32_t block_freq, keycode, fnum, block, ps, ksr, dt, mul, sl, raw;
    int32_t det;

    block_freq = ((uint32_t)(c->regs[0xA4 + ch] & 0x3f) << 8) | c->regs[0xA0 + ch];
    if (ch == 2 && (c->regs[0x27] & 0xC0) && k != 3) {
        /* FM3 special mode: S1 <- AD/A9, S2 <- AE/AA, S3 <- AC/A8, S4 keeps the channel's */
        static const uint8_t mreg[3] = { 1, 2, 0 };
        uint32_t m = mreg[k];
        block_freq = ((uint32_t)(c->regs[0xAC + m] & 0x3f) << 8) | c->regs[0xA8 + m];
    }
    keycode = ((block_freq >> 10) & 0xf) << 1;
    keycode |= (0xfe80u >> ((block_freq >> 7) & 0xf)) & 1;

    dt = (c->regs[0x30 + off] >> 4) & 7;
    det = detune_table[keycode][dt & 3];
    if (dt & 4)
        det = -det;
    mul = (uint32_t)(c->regs[0x30 + off] & 0xf) * 2;
    if (mul == 0)
        mul = 1;
    fnum = (block_freq & 0x7ff) << 1;
    block = block_freq >> 11;
    ps = (((fnum << block) >> 2) + (uint32_t)det) & 0x1ffff;
    ps = (ps * mul) >> 1;
    o->step = scale_step(c, ps);

    o->tl = (uint16_t)((c->regs[0x40 + off] & 0x7f) << 3);
    sl = (c->regs[0x80 + off] >> 4) & 0xf;
    sl |= (sl + 1) & 0x10;
    o->sl = (uint16_t)(sl << 5);

    ksr = keycode >> ((c->regs[0x50 + off] >> 6) ^ 3);
#define EFF(r) ((r) == 0 ? 0 : ((r) + ksr > 63 ? 63 : (r) + ksr))
    raw = (uint32_t)(c->regs[0x50 + off] & 0x1f) * 2; o->rate[EG_ATTACK] = (uint8_t)EFF(raw);
    raw = (uint32_t)(c->regs[0x60 + off] & 0x1f) * 2; o->rate[EG_DECAY] = (uint8_t)EFF(raw);
    raw = (uint32_t)(c->regs[0x70 + off] & 0x1f) * 2; o->rate[EG_SUSTAIN] = (uint8_t)EFF(raw);
    raw = (uint32_t)(c->regs[0x80 + off] & 0x0f) * 4 + 2; o->rate[EG_RELEASE] = (uint8_t)EFF(raw);
#undef EFF
    update_att(o);
    o->next = 0;                    /* rates may have changed: revisit at the next EG clock */
    c->ch[ch].next = 0;
    c->eg_next = 0;
}

static void cache_chan(struct opn_core *c, int ch)
{
    int k;
    for (k = 0; k < 4; k++)
        cache_op(c, ch, k);
}

/* carriers (output operators) of each algorithm, bit k = op k+1 */
static const uint8_t carriers[8] = { 8, 8, 8, 8, 10, 14, 14, 15 };

/* A channel is active until all operators are quiet after their release; it is quiet
 * (output exactly 0, only op1's feedback history still matters) while every carrier is
 * above EG_QUIET. Called whenever an envelope, a key or the algorithm changed. */
static void chan_update_flags(struct chan *cc)
{
    uint32_t cm = carriers[cc->alg];
    int k, active = 0, quiet = 1;
    for (k = 0; k < 4; k++) {
        const struct op *o = &cc->op[k];
        if (o->state != EG_RELEASE || o->env < EG_QUIET)
            active = 1;
        if ((cm & (1u << k)) && o->env <= EG_QUIET)
            quiet = 0;
    }
    if (!active)
        cc->fb1 = cc->fb2 = 0;
    cc->active = (uint8_t)active;
    cc->quiet = (uint8_t)quiet;
}

static void key_on(struct opn_core *c, struct chan *cc, struct op *o)
{
    if (o->state == EG_ATTACK)
        return;
    o->state = EG_ATTACK;
    o->phase = 0;
    if (o->rate[EG_ATTACK] >= 62) {
        o->env = 0;
        update_att(o);
    }
    o->next = 0;
    cc->next = 0;
    c->eg_next = 0;
    chan_update_flags(cc);
}

static void key_off(struct opn_core *c, struct chan *cc, struct op *o)
{
    if (o->state < EG_RELEASE) {
        o->state = EG_RELEASE;
        o->next = 0;
        cc->next = 0;
        c->eg_next = 0;
    }
}

/* ---- SSG derived values ------------------------------------------------------------------ */

/* a period change keeps the elapsed count, as the chip compares its free-running counter
 * with the period register (counter >= period toggles) */
static void ssg_set_period(struct ssg_tone *t, uint32_t half)
{
    uint32_t elapsed = t->half - t->next;
    t->half = half;
    t->next = half > elapsed ? half - elapsed : 1;
}

static void ssg_tone_update(struct opn_core *c, int n)
{
    uint32_t p = ((uint32_t)(c->regs[2 * n + 1] & 0x0f) << 8) | c->regs[2 * n];
    if (p == 0)
        p = 1;
    ssg_set_period(&c->tone[n], (p * c->pq4) >> 4);
}

static void ssg_noise_update(struct opn_core *c)
{
    uint32_t p = c->regs[6] & 0x1f;
    if (p == 0)
        p = 1;
    ssg_set_period(&c->noise, (2 * p * c->pq4) >> 4);
}

static void ssg_env_update(struct opn_core *c)
{
    c->env_period = ((uint32_t)c->regs[12] << 8) | c->regs[11];
    if (c->env_period == 0)
        c->env_period = 1;
}

static void ssg_catch_up(struct opn_core *c);
/* external for opn_lite_68k.s */
int32_t opn_lite_ssg_output(struct opn_core *c);
void opn_lite_eg_due(struct opn_core *c);

/* ---- interface ---------------------------------------------------------------------------- */

static void lite_reset(struct opn_core *c)
{
    int ch, k;
    memset(c->regs, 0, sizeof c->regs);
    c->latch[0] = c->latch[1] = 0;
    for (ch = 0; ch < 3; ch++) {
        struct chan *cc = &c->ch[ch];
        cc->fb1 = cc->fb2 = 0;
        cc->active = 0;
        cc->quiet = 1;
        cc->next = 0;
        cc->alg = cc->fb = 0;
        for (k = 0; k < 4; k++) {
            struct op *o = &cc->op[k];
            o->phase = 0;
            o->env = 0x3ff;
            o->state = EG_RELEASE;
            o->key = 0;
            o->next = 0;
        }
        cache_chan(c, ch);
    }
    c->eg_acc = 0;
    c->egc = 0;
    c->eg_next = 0;
    for (k = 0; k < 3; k++) {
        c->tone[k].half = c->tone[k].next = 0;
        c->tone[k].state = 0;
        ssg_tone_update(c, k);
    }
    c->noise.half = c->noise.next = 0;
    ssg_noise_update(c);
    c->lfsr = 1;
    ssg_env_update(c);
    c->env_count = c->env_acc = c->env_state = 0;
    c->ssg_on = 0;
    c->ssg_idle = 0;
    c->hp_x1 = c->hp_y1 = 0;
    memset(c->hb, 0, sizeof c->hb);
    c->hb_pos = 0;
    memset(c->sp_x1, 0, sizeof c->sp_x1);
    memset(c->sp_y1, 0, sizeof c->sp_y1);
    memset(c->sp_hb, 0, sizeof c->sp_hb);
}

static struct opn_core *lite_create(uint32_t clock_hz, uint32_t want_rate, uint32_t *rate)
{
    struct opn_core *c;
    uint32_t fm_rate = clock_hz / FM_DIV;             /* 55 466 */
    uint32_t r = want_rate ? want_rate : 22050;
    uint32_t q, rem;

    if (!wave_ready)
        build_wave();
    if (r > fm_rate)
        r = fm_rate;
    if (r < 8000)
        r = 8000;
    c = malloc(sizeof *c);
    if (!c)
        return 0;
    memset(c, 0, sizeof *c);
    c->rate = r;
    c->os = opn_lite_fm_oversample >= 2 ? 2 : 1;

    if (r == fm_rate) {
        /* native rate: exactly one chip sample per output sample, like opn_ymfm (whose
         * integer rate is the same rounding of clock/72) */
        c->fstep = 0x10000;
        c->sstep = (FM_DIV << 16) / SSG_DIV;           /* 4.5 SSG clocks per FM sample */
        c->pq4 = (65536u * 16 * SSG_DIV) / FM_DIV;
    } else {
        uint32_t sclk = clock_hz / SSG_DIV;            /* 249 600 */
        uint32_t ri = r << 16;                         /* r < 65536 */
        /* FM samples per output sample, 16.16: (clock/72) / r, clock remainder kept */
        q = ((fm_rate << 16) + (((clock_hz % FM_DIV) << 16) / FM_DIV)) / r;
        rem = ((fm_rate << 16) + (((clock_hz % FM_DIV) << 16) / FM_DIV)) % r;
        c->fstep = q + ((rem << 4) / r >= 8 ? 1 : 0); /* round to nearest */
        /* SSG: clocks per sample and sample-fraction units per clock */
        c->sstep = ((sclk / r) << 16) + (((sclk % r) << 16) / r);
        c->pq4 = (ri / sclk) * 16 + (((ri % sclk) * 16) / sclk);
    }
    if (c->os == 2)
        c->fstep = (c->fstep + 1) / 2;
    c->fint = c->fstep >> 16;
    c->fhi = (c->fstep >> 8) & 0xff;
    c->flo = c->fstep & 0xff;
    /* DC blocker, ~45 Hz: pole = 1 - 2*pi*45/r; k = 65536 * (1 - pole) = 18 535 609 / r */
    c->hp_k = 18535609u / r;

    lite_reset(c);
    *rate = r;
    return c;
}

static void lite_destroy(struct opn_core *c) { free(c); }

static void lite_write(struct opn_core *c, uint8_t reg, uint8_t val)
{
    if (reg < 0x10) {
        c->regs[reg] = val;
        if (reg <= 5)
            ssg_tone_update(c, reg >> 1);
        else if (reg == 6)
            ssg_noise_update(c);
        else if (reg >= 8 && reg <= 10)
            c->ssg_on = (c->regs[8] | c->regs[9] | c->regs[10]) & 0x1f;
        else if (reg == 11 || reg == 12) {
            ssg_catch_up(c);
            ssg_env_update(c);
        } else if (reg == 13) {
            ssg_catch_up(c);
            c->env_state = 0;
        }
        return;
    }
    if (reg == 0x27) {
        c->regs[reg] = val;
        cache_chan(c, 2);
        return;
    }
    if (reg == 0x28) {
        int ch = val & 3, k;
        struct chan *cc;
        if (ch == 3)
            return;
        cc = &c->ch[ch];
        for (k = 0; k < 4; k++) {
            uint8_t on = (val >> (4 + k)) & 1;
            struct op *o = &cc->op[k];
            if (on != o->key) {
                o->key = on;
                if (on)
                    key_on(c, cc, o);
                else
                    key_off(c, cc, o);
            }
        }
        return;
    }
    if (reg >= 0x30 && reg < 0x90) {
        int ch = reg & 3, slot = (reg >> 2) & 3, k;
        if (ch == 3)
            return;
        c->regs[reg] = val;
        /* slot -> algorithm-order operator: +0 S1=op1, +4 S3=op3, +8 S2=op2, +12 S4=op4 */
        k = slot == 0 ? 0 : slot == 1 ? 2 : slot == 2 ? 1 : 3;
        cache_op(c, ch, k);
        return;
    }
    if ((reg & 0xf0) == 0xA0) {
        int ch = reg & 3;
        if (ch == 3)
            return;
        if (reg & 4) {
            c->latch[(reg >> 3) & 1] = val & 0x3f;     /* A4-A6 / AC-AE only latch */
        } else {
            c->regs[reg] = val;
            c->regs[reg | 4] = c->latch[(reg >> 3) & 1];
            if (reg & 8)
                cache_chan(c, 2);                      /* A8-AA: FM3 per-slot frequencies */
            else
                cache_chan(c, ch);
        }
        return;
    }
    if (reg >= 0xB0 && reg <= 0xB2) {
        struct chan *cc = &c->ch[reg - 0xB0];
        c->regs[reg] = val;
        cc->alg = val & 7;
        cc->fb = (val >> 3) & 7;
        chan_update_flags(cc);          /* the carrier set changed */
        return;
    }
    /* 0x21-0x26, 0x2D-0x2F (test, timers, prescaler), 0xB4+: ignored */
}

/* ---- rendering ---------------------------------------------------------------------------- */

/* Envelope step of one operator at envelope clock `egc`. The chip only changes the
 * attenuation at clocks where (egc << (rate >> 2)) has zero low 11 bits, so after each visit
 * the operator schedules its next one (o->next) and the tick loop skips it until then; a
 * pending state transition (attack done, sustain level reached) is scheduled for the very
 * next clock so the new rate's schedule starts exactly where the chip's would. */
static void clock_env(struct op *o, uint32_t egc)
{
    uint32_t rate, shift, cnt, idx, inc, period;

    if (o->state == EG_RELEASE && o->env == 0x3ff) {
        /* fully released: nothing changes until a key-on or a register write (both set
         * next = 0). Without this, RR 15 revisits the operator at every clock as long as
         * another operator keeps the channel active - 78 % of all visits in XENON_24. */
        o->next = ~0u;
        return;
    }
    if (o->state == EG_ATTACK && o->env == 0)
        o->state = EG_DECAY;
    if (o->state == EG_DECAY && o->env >= o->sl)
        o->state = EG_SUSTAIN;
    rate = o->rate[o->state];
    shift = rate >> 2;
    cnt = egc << shift;
    if (cnt & 0x7ff) {
        /* shift < 11 here: next clock that is a multiple of 2^(11 - shift) */
        period = 1u << (11 - shift);
        o->next = (egc | (period - 1)) + 1;
        return;
    }
    idx = (cnt >> (shift <= 11 ? 11 : shift)) & 7;
    inc = (inc_table[rate] >> (4 * idx)) & 15;
    if (o->state == EG_ATTACK) {
        if (rate < 62)
            o->env = (uint16_t)(o->env - ((((uint32_t)o->env + 1) * inc + 15) >> 4));
    } else {
        uint32_t e = o->env + inc;
        o->env = (uint16_t)(e >= 0x400 ? 0x3ff : e);
    }
    update_att(o);
    if ((o->state == EG_ATTACK && o->env == 0) || (o->state == EG_DECAY && o->env >= o->sl))
        o->next = egc + 1;
    else
        o->next = egc + (shift >= 11 ? 1 : (1u << (11 - shift)));
}

/* 14-bit signed operator output for a wave index (10 bits used) */
#define OP_VOLUME(o, idx, out) do { \
    if ((o)->env > EG_QUIET) { \
        (out) = 0; \
    } else { \
        uint32_t s_ = opn_lite_wave[(idx) & 1023]; \
        uint32_t a_ = (s_ & 0x7fff) + (o)->att; \
        int32_t r_ = (int32_t)((uint32_t)opn_lite_pow[a_ & 0xff] >> (a_ >> 8)); \
        (out) = (s_ & 0x8000) ? -r_ : r_; \
    } \
} while (0)

#define OPV(k, mod, out) OP_VOLUME(&o[k], (o[k].phase >> 18) + (uint32_t)(mod), out)

static int32_t chan_output(struct chan *cc)
{
    struct op *o = cc->op;
    uint32_t fb = cc->fb;
    int32_t m, o1, o2, o3, o4;

    o[0].phase += o[0].step;
    o[1].phase += o[1].step;
    o[2].phase += o[2].step;
    o[3].phase += o[3].step;

    /* op1 with self-feedback from its two previous outputs */
    m = fb ? (cc->fb1 + cc->fb2) >> (10 - fb) : 0;
    OPV(0, m, o1);
    cc->fb2 = cc->fb1;
    cc->fb1 = o1;

    if (cc->quiet)
        return 0;                   /* every carrier is silent; op1's history is kept above */

    /* the 8 OPN algorithms; modulator outputs feed the next operator halved. Operator
     * outputs are 14-bit, so a sum of up to four never reaches the 16-bit clamp. */
    switch (cc->alg) {
    default:
    case 0: OPV(1, o1 >> 1, o2); OPV(2, o2 >> 1, o3); OPV(3, o3 >> 1, o4); return o4;
    case 1: OPV(1, 0, o2); OPV(2, (o1 + o2) >> 1, o3); OPV(3, o3 >> 1, o4); return o4;
    case 2: OPV(1, 0, o2); OPV(2, o2 >> 1, o3); OPV(3, (o1 + o3) >> 1, o4); return o4;
    case 3: OPV(1, o1 >> 1, o2); OPV(2, 0, o3); OPV(3, (o2 + o3) >> 1, o4); return o4;
    case 4: OPV(1, o1 >> 1, o2); OPV(2, 0, o3); OPV(3, o3 >> 1, o4); return o2 + o4;
    case 5: OPV(1, o1 >> 1, o2); OPV(2, o1 >> 1, o3); OPV(3, o1 >> 1, o4); return o2 + o3 + o4;
    case 6: OPV(1, o1 >> 1, o2); OPV(2, 0, o3); OPV(3, 0, o4); return o2 + o3 + o4;
    case 7: OPV(1, 0, o2); OPV(2, 0, o3); OPV(3, 0, o4); return o1 + o2 + o3 + o4;
    }
}

/* fraction (0..65536) of one sample during which a square wave is high; advances it */
static uint32_t square_duty(struct ssg_tone *t)
{
    uint32_t rem = 65536, hi = 0;
    if (t->half < TONE_MIN)
        return 32768;
    for (;;) {
        if (t->next > rem) {
            if (t->state)
                hi += rem;
            t->next -= rem;
            return hi;
        }
        if (t->state)
            hi += t->next;
        rem -= t->next;
        t->state ^= 1;
        t->next = t->half;
    }
}

/* same for the noise generator: a 17-bit LFSR stepped every noise period */
static uint32_t noise_duty(struct opn_core *c)
{
    struct ssg_tone *t = &c->noise;
    uint32_t rem = 65536, hi = 0;
    for (;;) {
        if (t->next > rem) {
            if (c->lfsr & 1)
                hi += rem;
            t->next -= rem;
            return hi;
        }
        if (c->lfsr & 1)
            hi += t->next;
        rem -= t->next;
        c->lfsr ^= ((c->lfsr ^ (c->lfsr >> 3)) & 1) << 17;
        c->lfsr >>= 1;
        t->next = t->half;
    }
}

/* Advance the hardware envelope counter over the samples skipped while no SSG channel
 * sounded: the same accumulation as ssg_output's per-sample one, in chunks (sstep < 2^21).
 * Called before anything reads or changes the envelope state (ssg_output, registers 11-13). */
static void ssg_catch_up(struct opn_core *c)
{
    if (!c->ssg_idle)
        return;                     /* the counter is only reduced at sample time */
    while (c->ssg_idle) {
        uint32_t m = c->ssg_idle > 1024 ? 1024 : c->ssg_idle;
        c->env_acc += c->sstep * m;
        c->env_count += c->env_acc >> 16;
        c->env_acc &= 0xffff;
        c->ssg_idle -= m;
    }
    if (c->env_count >= c->env_period) {
        c->env_state += c->env_count / c->env_period;
        c->env_count %= c->env_period;
    }
}

int32_t opn_lite_ssg_output(struct opn_core *c)
{
    uint32_t mixer = c->regs[7];
    uint32_t nduty = 65536, env_vol = 0, k, n, need_noise = 0;
    int32_t sum = 0;

    if (!c->ssg_on) {
        /* no channel sounds: the tones stop with their channels (below), the noise is
         * parked as when no sounding channel mixes it, and the hardware envelope counter
         * is caught up when the SSG is next heard */
        c->noise.next = c->noise.half;
        c->ssg_idle++;
        return 0;
    }

    /* hardware envelope: one step every env_period clocks */
    if (c->ssg_idle)
        ssg_catch_up(c);
    c->env_acc += c->sstep;
    n = c->env_acc >> 16;
    c->env_acc &= 0xffff;
    c->env_count += n;
    if (c->env_count >= c->env_period) {
        c->env_state += c->env_count / c->env_period;
        c->env_count %= c->env_period;
    }
    if ((c->regs[8] | c->regs[9] | c->regs[10]) & 0x10) {
        uint32_t shape = c->regs[13];
        uint32_t cont = (shape >> 3) & 1, att = (shape >> 2) & 1, alt = (shape >> 1) & 1, hold = shape & 1;
        if ((hold | (cont ^ 1)) && c->env_state >= 32) {
            c->env_state = 32;
            env_vol = ((att ^ alt) & cont) ? 31 : 0;
        } else {
            uint32_t a = att;
            if (alt)
                a ^= (c->env_state >> 5) & 1;
            env_vol = (c->env_state & 31) ^ (a ? 0 : 31);
        }
    }

    /* noise is only generated when a sounding channel mixes it in; otherwise the generator
     * is parked (its phase is not audible) */
    for (k = 0; k < 3; k++)
        if (!(mixer & (8u << k)) && (c->regs[8 + k] & 0x1f))
            need_noise = 1;
    if (need_noise)
        nduty = noise_duty(c);
    else
        c->noise.next = c->noise.half;

    for (k = 0; k < 3; k++) {
        uint32_t amp = c->regs[8 + k], vol, f, nd, td;
        if (!(amp & 0x1f))
            continue;                              /* silent part: its tone phase stops too */
        td = (mixer & (1u << k)) ? 65536 : square_duty(&c->tone[k]);
        nd = (mixer & (8u << k)) ? 65536 : nduty;
        f = td == 65536 ? nd : nd == 65536 ? td : (td * nd) >> 16;
        if (f == 0)
            continue;
        if (amp & 0x10) {
            vol = env_vol;
        } else {
            vol = (amp & 0xf) * 2 + 1;
        }
        if (f == 65536)
            sum += ssg_amp[vol];
        else
            sum += (int32_t)(((uint32_t)ssg_amp[vol] * f) >> 16);
    }
    return sum;
}

/* 2x oversampling decimator: 11-tap half-band FIR (Kaiser), unity DC gain / 1024 */
#define HB_H0 514
#define HB_H1 298
#define HB_H3 (-45)
#define HB_H5 2

static void step_envelopes(struct opn_core *c, int any);

/* the half-band output after the second input of a pair; p = position after that input,
 * the centre tap is 5 samples back (group delay), taps at +-1, +-3, +-5 */
static int32_t hb_decimate(const int32_t *h, uint32_t p)
{
#define HB(n) h[(p - 1 - (n)) & 15]
    int32_t fm = HB(5) * HB_H0 + (HB(4) + HB(6)) * HB_H1 + (HB(2) + HB(8)) * HB_H3 +
                 (HB(0) + HB(10)) * HB_H5;
#undef HB
    return fm >> 10;
}

/* The envelope clock (step_envelopes) and the SSG (ssg_output) are entered from the render
 * loops only when there is work: the checks are inlined so that the common case costs no
 * call (vbcc does not inline), which is why they are written out in each loop. */
#define EG_STEP(c, any) do { \
    (c)->eg_acc += (c)->fstep; \
    if ((c)->eg_acc >= (EG_PER_FM << 16)) \
        step_envelopes((c), (any)); \
} while (0)

#define SSG_OUT(c, out) do { \
    if ((c)->ssg_on) { \
        (out) = opn_lite_ssg_output(c); \
    } else { \
        (c)->noise.next = (c)->noise.half; \
        (c)->ssg_idle++; \
        (out) = 0; \
    } \
} while (0)

#ifdef XE_OPN_ASM_68K
/* opn_lite_68k.s: the os == 1 render loop; it hard-codes vbcc's layout of these structures */
void opn_lite_render_68k(__reg("a0") struct opn_core *c, __reg("a1") int16_t *mono, __reg("d0") int frames);
#define LAYOUT(name, cond) typedef char opn_layout_##name[(cond) ? 1 : -1]
LAYOUT(op, sizeof(struct op) == 26 && offsetof(struct op, step) == 4 && offsetof(struct op, env) == 12 &&
       offsetof(struct op, att) == 14);
LAYOUT(chan, sizeof(struct chan) == 120 && offsetof(struct chan, fb1) == 104 && offsetof(struct chan, fb2) == 108 &&
       offsetof(struct chan, active) == 116 && offsetof(struct chan, quiet) == 117 &&
       offsetof(struct chan, alg) == 118 && offsetof(struct chan, fb) == 119);
LAYOUT(core, offsetof(struct opn_core, fstep) == 4 && offsetof(struct opn_core, eg_acc) == 24 &&
       offsetof(struct opn_core, egc) == 28 && offsetof(struct opn_core, eg_next) == 32 &&
       offsetof(struct opn_core, ch) == 294 && offsetof(struct opn_core, noise) == 698 &&
       offsetof(struct ssg_tone, half) == 4 && offsetof(struct opn_core, ssg_on) == 730 &&
       offsetof(struct opn_core, ssg_idle) == 734 && offsetof(struct opn_core, hp_x1) == 738 &&
       offsetof(struct opn_core, hp_y1) == 742 && offsetof(struct opn_core, hp_k) == 746);
#undef LAYOUT
#endif

static void lite_render(struct opn_core *c, int16_t *mono, int frames)
{
    struct chan *const ch = c->ch;
    int i;
#ifdef XE_OPN_ASM_68K
    if (c->os == 1) {
        opn_lite_render_68k(c, mono, frames);
        return;
    }
#endif
    for (i = 0; i < frames; i++) {
        int32_t fm, ssg, x, y;
        int any = ch[0].active | ch[1].active | ch[2].active;
        EG_STEP(c, any);
        fm = 0;
        if (any) {
            if (ch[0].active) fm += chan_output(&ch[0]);
            if (ch[1].active) fm += chan_output(&ch[1]);
            if (ch[2].active) fm += chan_output(&ch[2]);
        }
        if (c->os == 2) {
            /* half-band decimation over the 2x stream: a second pass, output every second
             * input */
            int32_t *h = c->hb;
            h[c->hb_pos] = fm;
            c->hb_pos = (c->hb_pos + 1) & 15;
            any = ch[0].active | ch[1].active | ch[2].active;
            EG_STEP(c, any);
            fm = 0;
            if (any) {
                if (ch[0].active) fm += chan_output(&ch[0]);
                if (ch[1].active) fm += chan_output(&ch[1]);
                if (ch[2].active) fm += chan_output(&ch[2]);
            }
            h[c->hb_pos] = fm;
            c->hb_pos = (c->hb_pos + 1) & 15;
            fm = hb_decimate(h, c->hb_pos);
        }

        /* mix as opn_ymfm.cpp: FM + SSG/4, x2, then the DC blocker
         * y = x - x1 + pole * y1, with y kept in 24.8 so the feedback does not stall */
        SSG_OUT(c, ssg);
        x = (fm + (ssg >> 2)) * 2;
        y = ((x - c->hp_x1) << 8) + c->hp_y1 - (((c->hp_y1 >> 8) * (int32_t)c->hp_k) >> 8);
        c->hp_x1 = x;
        c->hp_y1 = y;
        y >>= 8;
        if (y > 32767) y = 32767;
        if (y < -32768) y = -32768;
        mono[i] = (int16_t)y;
    }
}

/* Envelope clocks due in one (oversampled) step; shared by both render loops. */
/* Envelope clock egc has reached eg_next: visit the channels that are due, reschedule.
 * (External for opn_lite_68k.s.) */
void opn_lite_eg_due(struct opn_core *c)
{
    int chn, k;
    uint32_t enext = ~0u;
    for (chn = 0; chn < 3; chn++) {
        struct chan *cc = &c->ch[chn];
        uint32_t nmin = ~0u;
        if (!cc->active)
            continue;
        if (c->egc >= cc->next) {
            for (k = 0; k < 4; k++) {
                struct op *o = &cc->op[k];
                if (c->egc >= o->next)
                    clock_env(o, c->egc);
                if (o->next < nmin)
                    nmin = o->next;
            }
            cc->next = nmin;
            chan_update_flags(cc);
            if (!cc->active)
                continue;
        }
        if (cc->next < enext)
            enext = cc->next;
    }
    c->eg_next = enext;
}

static void step_envelopes(struct opn_core *c, int any)
{
    do {
        c->eg_acc -= EG_PER_FM << 16;
        c->egc++;
        if (any && c->egc >= c->eg_next)
            opn_lite_eg_due(c);
    } while (c->eg_acc >= (EG_PER_FM << 16));
}

/* DC blocker + clamp of one stream sample, the same arithmetic as lite_render */
static int16_t hp_out(int32_t x, int32_t *x1, int32_t *y1, uint32_t k)
{
    int32_t y = ((x - *x1) << 8) + *y1 - (((*y1 >> 8) * (int32_t)k) >> 8);
    *x1 = x;
    *y1 = y;
    y >>= 8;
    if (y > 32767) y = 32767;
    if (y < -32768) y = -32768;
    return (int16_t)y;
}

/* Split rendering (opn_core.h): the same synthesis as lite_render, but each FM channel is
 * decimated (os = 2) and DC-blocked on its own and the SSG mix goes to its own stream. Kept
 * as a separate loop so the mono path stays as tested; the two are equal up to the rounding
 * of the per-stream filters (tests/test_opn_split.c measures it). */
static void lite_render_split(struct opn_core *c, int16_t *const streams[OPN_SPLIT_STREAMS], int frames)
{
    int i, chn;
    for (i = 0; i < frames; i++) {
        int32_t fm[3] = { 0, 0, 0 }, ssg;
        int pass;
        for (pass = 0; pass < c->os; pass++) {
            int32_t pass_fm[3] = { 0, 0, 0 };
            int any = c->ch[0].active | c->ch[1].active | c->ch[2].active;
            EG_STEP(c, any);
            if (any)
                for (chn = 0; chn < 3; chn++)
                    if (c->ch[chn].active)
                        pass_fm[chn] = chan_output(&c->ch[chn]);
            if (c->os == 2) {
                for (chn = 0; chn < 3; chn++) {
                    int32_t *h = c->sp_hb[chn];
                    h[c->hb_pos] = pass_fm[chn];
                    if (pass == 1) {
                        uint32_t p = (c->hb_pos + 1) & 15;
#define HB(n) h[(p - 1 - (n)) & 15]
                        fm[chn] = HB(5) * HB_H0 + (HB(4) + HB(6)) * HB_H1 + (HB(2) + HB(8)) * HB_H3 +
                                  (HB(0) + HB(10)) * HB_H5;
#undef HB
                        fm[chn] >>= 10;
                    }
                }
                c->hb_pos = (c->hb_pos + 1) & 15;
            } else {
                fm[0] = pass_fm[0]; fm[1] = pass_fm[1]; fm[2] = pass_fm[2];
            }
        }
        for (chn = 0; chn < 3; chn++)
            streams[chn][i] = hp_out(fm[chn] * 2, &c->sp_x1[chn], &c->sp_y1[chn], c->hp_k);
        SSG_OUT(c, ssg);
        streams[OPN_SPLIT_SSG][i] = hp_out((ssg >> 2) * 2, &c->sp_x1[OPN_SPLIT_SSG],
                                           &c->sp_y1[OPN_SPLIT_SSG], c->hp_k);
    }
}

const struct opn_core_ops opn_lite_ops = {
    "lite", lite_create, lite_destroy, lite_reset, lite_write, lite_render, lite_render_split
};
