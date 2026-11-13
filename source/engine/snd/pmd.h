/* PMD 4.7g sequencer (OPN build) - docs/re/pmd-sequencer.md, pmd-driver.md.
 *
 * A C port of re-tools/pmdmodel.py (verified clock-for-clock against the real PMD.COM) plus the
 * driver layers the model leaves to the emulator: Timer A/B scheduling, the fade step, FM
 * effects (XEN_SE.EFC) with their FM3 masking of the music, PMD's built-in SSG effects, and the
 * INT 60h functions the engine uses. Register writes go to a callback; the caller renders the
 * chip. Addresses in comments are PMD.COM CS offsets. */
#ifndef XE_SND_PMD_H
#define XE_SND_PMD_H

#include <stddef.h>
#include <stdint.h>

#define PMD_CLOCK       3993600u          /* OPN master clock of the 26K board */
#define PMD_PARTS       9                 /* A B C D E F G H I */
#define PMD_PART_X      9                 /* FM effect part (work 3594) */
#define PMD_NPARTS      10

struct pmd_lfo {
    uint8_t w[8];        /* delay_ctr, speed_ctr, step, times_ctr, delay, speed, step0, times */
    uint8_t md[3];       /* delta, speed ctr, speed */
    uint8_t mdc[2];      /* count, reload */
    uint8_t wave;
    int16_t val;         /* current LFO value */
};

struct pmd_part {
    uint8_t kind_fm;     /* 1 = FM, 0 = SSG */
    uint8_t ch;          /* 1..3 */
    uint8_t is_x;        /* effect part */
    uint16_t addr, loop; /* stream pointer, L point (0 = none) */
    uint8_t len, gate;
    uint16_t freq;
    int16_t detune, porta, pstep, prem;
    uint8_t vol, trans, flags, tmpvol, ext, mix, voice, lstat, car, slot, vmask, mask, kst;
    uint8_t tl[4];
    uint8_t lslot[2];
    uint8_t q, Q;
    uint8_t note;
    uint8_t kd, kdc, kdm;
    uint8_t fbalg;
    uint8_t e[13];       /* envelope fields +21..+2D */
    struct pmd_lfo lfo[2];
};

typedef void (*pmd_write_fn)(void *ctx, uint8_t reg, uint8_t val);

struct pmd_ssgfx {
    int active;
    int priority;
    int number;
    const uint8_t *frame;    /* current frame in the table blob */
    int count;
    uint16_t tone;
    int8_t tone_delta;
    uint8_t noise;
    uint8_t sweep;           /* high nibble delta, low nibble interval */
    int sweep_ctr;
    uint8_t mixer_bits;      /* bit0 tone off, bit3 noise off */
};

struct pmd {
    pmd_write_fn write;
    void *ctx;

    /* music data (mutable: loop counters and F9 live in the data) */
    uint8_t *m;
    uint32_t msize;
    uint16_t voicetab;
    int has_voices;
    struct pmd_part part[PMD_NPARTS];

    /* globals (work area 30A0..) */
    uint8_t ks[3];           /* 30A5.. key-on slot bits per FM channel */
    uint8_t reg27;           /* 30AD */
    uint8_t reg27_saved;     /* 30AE */
    uint16_t sd[4];          /* slot detunes */
    uint8_t sd_any, fm3need, fb3, restore, tie, tvc;
    uint8_t TB, t, TBc;
    uint8_t status1, status2;
    uint8_t fade, fadesp;
    uint8_t voldown[2];
    uint8_t noise, noisec, reg7;
    uint8_t na;
    uint16_t rnd;
    uint8_t a3119, a30B7;
    uint8_t allst;
    int playing;
    int stop_after_fade;     /* 0 with /F (Xenon) */

    /* FM effect (EFC) */
    uint8_t *efc;
    uint32_t efcsize;
    uint16_t efc_voicetab;
    int fm_eff_active;
    int fm_eff_number;

    /* built-in SSG effects */
    const uint8_t *ssgtab;   /* bytes of PMD.COM 2CE9h.. (entries + frames) */
    uint32_t ssgtab_size;
    struct pmd_ssgfx sfx;

    /* OPN timers (master clocks) */
    uint64_t clock;          /* current time */
    uint64_t ta_next, tb_next;
    int ta_on, tb_on;
    uint8_t reg24, reg25, reg26;
    uint8_t status;          /* timer flags */
    uint32_t cps_int;        /* master clocks per output sample, 16.16 fixed */
    uint32_t cps_frac;
    uint32_t cps_acc;
};

void pmd_init(struct pmd *p, pmd_write_fn write, void *ctx, uint32_t out_rate);
void pmd_set_efc(struct pmd *p, uint8_t *efc, uint32_t size);          /* mutable copy */
void pmd_set_ssgtab(struct pmd *p, const uint8_t *tab, uint32_t size);
void pmd_set_rate(struct pmd *p, uint32_t out_rate);

/* INT 60h functions (sound.md §2) */
void pmd_load_music(struct pmd *p, uint8_t *mdata, uint32_t size);     /* AH=06h buffer copy; mutable */
void pmd_mstart(struct pmd *p);                                        /* AH=00h */
void pmd_mstop(struct pmd *p);                                         /* AH=01h */
void pmd_fade(struct pmd *p, uint8_t speed);                           /* AH=02h */
uint8_t pmd_get_fadevol(const struct pmd *p);                          /* AH=08h */
void pmd_set_fadevol(struct pmd *p, uint8_t v);                        /* AH=19h */
void pmd_get_status(const struct pmd *p, uint8_t *s1, uint8_t *s2);    /* AH=0Ah */
void pmd_fm_effect_on(struct pmd *p, int n);                           /* AH=0Ch */
void pmd_fm_effect_off(struct pmd *p);                                 /* AH=0Dh */

/* Advance time by `frames` output samples, firing the timer interrupts (and their register
 * writes) at the right sample positions. `render(ctx, nframes)` is called for each run of
 * samples between events so the caller can generate audio from the chip core. */
void pmd_advance(struct pmd *p, int frames, void (*render)(void *ctx, int nframes), void *rctx);

void pmd_ssg_effect_start(struct pmd *p, int n, int pps_flags);
void pmd_ssg_effect_stop(struct pmd *p);

/* for tests: run one Timer-B music clock / Timer-A effect clock directly */
void pmd_timer_b(struct pmd *p);
void pmd_timer_a(struct pmd *p);

#endif
