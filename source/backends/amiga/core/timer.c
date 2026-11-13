/* Frame clock: 56.42 Hz ticks from timer.device with absolute deadlines (UNIT_WAITUNTIL, or on
 * AROS UNIT_MICROHZ waiting for the time left: flavour.h timer_unit), so
 * the tick rate does not drift with request latency (a chained TR_ADDREQUEST would). The
 * wait itself runs in the main task's once-per-frame OS call (frameos.h, frameos.c); this
 * file opens the device, keeps the stall statistics and the profile (its clock is the
 * system part's, amsys.h am_prof_clock_init). */
#include "os_begin.h"
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/timer.h>
#include "os_end.h"
#include <string.h>
#include "amiga.h"
#include "amsys.h"
#include "flavour.h"
#include "frameos.h"

struct Device *TimerBase;

static struct MsgPort *g_port;
static struct timerequest *g_req;
static int g_open;
static struct timeval g_start;
static ULONG g_resyncs, g_lost;     /* stalls of more than ten ticks and the ticks they cost */
static ULONG g_waits;               /* am_frame_wait calls = the frame number being waited for */

/* Stall profile, see amiga.h. A period starts each time the clock is on time (the frame
 * really waits); a stall line reports the period that ended in the stall. */
#define STALL_LOGS 32
volatile ULONG am_prof_audio;
static ULONG g_efreq;               /* profile clock rate (E-clock 709 379 Hz on a PAL Amiga) */
static struct {
    ULONG t0, frames, audio0;
    ULONG pack_t, seek_t, pack_n, pack_bytes, pack_first, big_off, big_n;
    ULONG pres_t, pres_n, pres_full;
    ULONG wake_us;                  /* how late the last timer wait returned */
} P;
static ULONG g_pres_total, g_pres_calls;   /* the whole run, for the exit line (am_prof_present_ms) */


void am_prof_pack(ULONG t0, ULONG t_seek, ULONG off, ULONG n)
{
    P.pack_t += am_prof_now() - t0;
    P.seek_t += t_seek - t0;
    if (P.pack_n++ == 0)
        P.pack_first = off;
    P.pack_bytes += n;
    if (n > P.big_n) {
        P.big_n = n;
        P.big_off = off;
    }
}

void am_prof_present(ULONG t0, int full)
{
    ULONG d = am_prof_now() - t0;
    P.pres_t += d;
    g_pres_total += d;
    g_pres_calls++;
    P.pres_n++;
    if (full)
        P.pres_full++;
}

static void prof_reset(void)
{
    memset(&P, 0, sizeof P);
    P.t0 = am_prof_now();
    P.audio0 = am_prof_audio;
}

ULONG am_prof_present_ms(ULONG *calls)
{
    if (calls)
        *calls = g_pres_calls;
    return am_prof_ms(g_pres_total);
}

ULONG am_prof_freq(void)
{
    return g_efreq;
}

ULONG am_prof_ms(ULONG eticks)
{
    return g_efreq ? (ULONG)((unsigned long long)eticks * 1000ULL / g_efreq) : 0;
}

/* t_now: the profile clock when the frame call started (the stall ended) */
static void prof_stall(ULONG late_ticks, ULONG t_now)
{
    ULONG total = t_now - P.t0, audio = am_prof_audio - P.audio0;
    ULONG rest = total - P.pack_t - P.pres_t;
    if (g_resyncs > STALL_LOGS)
        return;
    am_log("stall at frame %lu%s: %lu ticks late, %lu frames behind in %lu ms: "
           "pack %lu reads %lu bytes %lu ms (seeks %lu ms; first 0x%lx, largest %lu at 0x%lx), "
           "present %lu (%lu full) %lu ms, other %lu ms; audio task ran %lu ms of it; "
           "woken %lu ms late\n",
           (unsigned long)g_waits, g_waits == 1 ? " (start-up)" : "", (unsigned long)late_ticks,
           (unsigned long)P.frames, (unsigned long)am_prof_ms(total),
           (unsigned long)P.pack_n, (unsigned long)P.pack_bytes, (unsigned long)am_prof_ms(P.pack_t),
           (unsigned long)am_prof_ms(P.seek_t), (unsigned long)P.pack_first,
           (unsigned long)P.big_n, (unsigned long)P.big_off,
           (unsigned long)P.pres_n, (unsigned long)P.pres_full, (unsigned long)am_prof_ms(P.pres_t),
           (unsigned long)((LONG)rest > 0 ? am_prof_ms(rest) : 0), (unsigned long)am_prof_ms(audio),
           (unsigned long)(P.wake_us / 1000));
}

int am_timer_open(void)
{
    g_port = CreateMsgPort();
    if (!g_port)
        return -1;
    g_req = (struct timerequest *)CreateIORequest(g_port, sizeof(struct timerequest));
    if (!g_req)
        return -1;
    if (OpenDevice((STRPTR)TIMERNAME, am_flavour.timer_unit, (struct IORequest *)g_req, 0) != 0)
        return -1;
    g_open = 1;
    TimerBase = g_req->tr_node.io_Device;
    g_efreq = am_prof_clock_init();
    prof_reset();
    GetSysTime(&g_start);
    am_fr->timer_base = TimerBase;
    am_fr->req = g_req;
    am_fr->relative = am_flavour.timer_unit == UNIT_MICROHZ;
    am_fr->dl_secs = g_start.tv_secs;
    am_fr->dl_micro = g_start.tv_micro;
    am_fr->dl_frac = 0;
    return 0;
}

void am_timer_close(void)
{
    am_log("timer: %lu stalls over ten ticks, %lu ticks lost in them\n", (unsigned long)g_resyncs,
           (unsigned long)g_lost);
    if (g_open)
        CloseDevice((struct IORequest *)g_req);
    g_open = 0;
    if (g_req) DeleteIORequest((struct IORequest *)g_req);
    if (g_port) DeleteMsgPort(g_port);
    g_req = 0;
    g_port = 0;
}

/* The frame tick: one OS call with `ops` (frameos.h). With AM_F_WAIT it waits for the
 * next 56.42 Hz deadline; after a stall (disk load, screen in the background with a busy
 * system) the clock resynchronises instead of running the missed ticks back to back. */
void am_frame_wait(ULONG ops)
{
    struct am_frameos *f = am_fr;
    ULONG t_call = 0;
    if (ops & AM_F_WAIT) {
        g_waits++;
        P.frames++;
        t_call = am_prof_now();
    }
    am_frame_call(ops);
    if (!(ops & AM_F_WAIT))
        return;
    if (f->resynced) {
        ULONG late = (ULONG)f->late_us / AM_TICK_US;
        g_resyncs++;
        g_lost += late;
        prof_stall(late, t_call);
    }
    if (f->waited) {
        prof_reset();
        P.wake_us = f->wake_us;
    }
}

uint32_t am_ticks(void)
{
    struct timeval now;
    unsigned long long us;
    GetSysTime(&now);
    us = (unsigned long long)(now.tv_secs - g_start.tv_secs) * 1000000ULL + now.tv_micro;
    us -= g_start.tv_micro;
    return (uint32_t)(us / AM_TICK_US);
}
