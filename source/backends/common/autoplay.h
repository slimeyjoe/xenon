/* Scripted input for tests and automation, shared by the backends.
 *
 * A spec is a list of words separated by blanks or newlines; "@path" in place of a spec reads
 * it from a file (an AmigaDOS command line is too short for a long schedule).
 *   ok:N                 press OK every N ticks (cancel:N and down:N add a cancel / down press)
 *   rand:SEED            a random command every 20 ticks
 *   at:T=KEY[+KEY..],..  press the keys once at tick T (disables the periodic presses);
 *                        keys: ok cancel up down left right help f1..f5 other quit emergency
 *                        mouse_l mouse_r
 *   ctrl:T0-T1,..        hold CTRL during ticks T0 <= t < T1 (instant:, anykey: likewise for the
 *                        instant-text and any-key-down levels)
 *   snap:T,T,..          ticks at which the backend takes a snapshot (autoplay_snap_due)
 *
 * Presses behave like the control mode's pending edges (backends/control/control.c): an at:
 * entry is delivered by the first poll at or after its tick, once, and input_flush drops the
 * entries that are due. So a control-mode transcript (press at the stop of tick T) replays
 * tick for tick as at:T=... (tests/e2e/amiga.py). */
#ifndef XE_AUTOPLAY_H
#define XE_AUTOPLAY_H

#include <stdint.h>

struct xe_input;

enum { AP_CTRL, AP_INSTANT, AP_ANYKEY };

struct autoplay_at { uint32_t tick, keys; };
struct autoplay_range { uint32_t t0, t1; int what; };

struct autoplay {
    int ok_every, cancel_every, down_every;
    unsigned rnd;                       /* rand: seed (0 = off) */
    unsigned rnd_state;
    struct autoplay_at *at;             /* sorted by tick */
    int at_count, at_next;
    struct autoplay_range *rng;
    int rng_count;
    uint32_t *snap;                     /* sorted */
    int snap_count;
    uint32_t last_tick;                 /* periodic presses are delivered once per tick */
    int polled;
    uint32_t kept;                      /* quit / emergency presses a flush dropped the entry of */
};

/* Parse `spec` into *ap (cleared first). Returns 0, or -1 with a message in err. */
int autoplay_parse(struct autoplay *ap, const char *spec, char *err, int errlen);
void autoplay_free(struct autoplay *ap);

/* ORs the presses due at `tick` into in->pressed and sets the held levels. */
void autoplay_poll(struct autoplay *ap, uint32_t tick, struct xe_input *in);
/* input_flush: drops the presses due at `tick` (type-ahead is lost, as in the control mode);
 * their quit / emergency presses go to the next poll (xe_hal.h input_flush). */
void autoplay_flush(struct autoplay *ap, uint32_t tick);
/* 1 when `tick` is in the snap: list. */
int autoplay_snap_due(const struct autoplay *ap, uint32_t tick);
/* the last tick of the at:, ranges and snap: lists (0 when empty) */
uint32_t autoplay_last_tick(const struct autoplay *ap);

#endif
