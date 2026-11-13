/* OPN (YM2203) chip core abstraction. The sequencer writes registers (timers are handled in
 * pmd_seq, but register 27h is forwarded: bits 6-7 select FM3 special mode), the core renders
 * mono 16-bit samples at its native rate. Implementations: opn_ymfm (C++, modern hosts) and
 * opn_lite (C, low-end machines). */
#ifndef XE_SND_OPN_CORE_H
#define XE_SND_OPN_CORE_H

#include <stdint.h>

struct opn_core;

/* render_split streams: the three FM channels and the SSG (its three tones mixed). Each
 * stream is scaled like the mono output, so their sum is the mono render up to rounding
 * (every stream has its own DC blocker); a sink that mixes channels itself (AHI's low-level
 * API on a card with hardware mixing, a per-voice volume control) consumes these. */
#define OPN_SPLIT_FM1   0
#define OPN_SPLIT_FM2   1
#define OPN_SPLIT_FM3   2
#define OPN_SPLIT_SSG   3
#define OPN_SPLIT_STREAMS 4

struct opn_core_ops {
    const char *name;
    /* create a core for `clock_hz` (3 993 600); `want_rate` is the caller's preferred output
     * rate - the core answers with the rate it really produces in `*rate` */
    struct opn_core *(*create)(uint32_t clock_hz, uint32_t want_rate, uint32_t *rate);
    void (*destroy)(struct opn_core *c);
    void (*reset)(struct opn_core *c);
    void (*write)(struct opn_core *c, uint8_t reg, uint8_t val);
    void (*render)(struct opn_core *c, int16_t *mono, int frames);
    /* optional (NULL when the core cannot): render into OPN_SPLIT_STREAMS separate streams
     * instead of the mono mix; a core instance is used with render() or render_split(), not
     * both (the split streams keep their own filter state) */
    void (*render_split)(struct opn_core *c, int16_t *const streams[OPN_SPLIT_STREAMS], int frames);
};

extern const struct opn_core_ops opn_lite_ops;
/* opn_lite: render FM at 2x the output rate and average (1 = off); set before create() */
extern int opn_lite_fm_oversample;
#ifdef XE_HAVE_YMFM
extern const struct opn_core_ops opn_ymfm_ops;
#endif

#endif
