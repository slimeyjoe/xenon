/* The main task's once-per-frame OS work (frameos.h). The WarpOS build compiles this file with
 * the 68k compiler (config/warpos.mk) and reaches it with one Run68K per frame; everywhere
 * else it is ordinary code of the program, called directly. It uses no library base globals
 * (the struct carries them): 68k code linked into a PPC program cannot reach the PPC side's.
 *
 * The frame clock is the one timer.c had: absolute deadlines on timer.device UNIT_WAITUNTIL
 * (UNIT_MICROHZ with the time left where the flavour says so, f->relative),
 * stepped by 17 724.193 us, so the tick rate does not drift with request latency; after a
 * stall of more than ten ticks it restarts from now instead of running the missed ticks
 * back to back. */
#define __NOLIBBASE__
#include "os_begin.h"
#include <exec/types.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <devices/timer.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <proto/exec.h>
#include <proto/timer.h>
#include <proto/intuition.h>
#include "os_end.h"
#include "frameos.h"

/* exec's base: on the 68k from address 4 (the 68k objects of the WarpOS build have no
 * startup globals), elsewhere the startup code's */
#ifdef AM_CPU_68K
#define SysBase (*(struct ExecBase **)4L)
#else
extern struct ExecBase *SysBase;
#endif

static void tv_norm(struct timeval *tv)
{
    while (tv->tv_micro >= 1000000UL) {
        tv->tv_micro -= 1000000UL;
        tv->tv_secs++;
    }
}

/* a - b in microseconds, clamped to +-2^31 */
static LONG tv_diff(const struct timeval *a, const struct timeval *b)
{
    LONG ds = (LONG)a->tv_secs - (LONG)b->tv_secs;
    if (ds > 2000) return 0x7fffffff;
    if (ds < -2000) return -0x7fffffff;
    return ds * 1000000L + ((LONG)a->tv_micro - (LONG)b->tv_micro);
}

static void frame_wait(struct am_frameos *f)
{
    struct Device *TimerBase = f->timer_base;
    struct timeval now, dl;
    GetSysTime(&now);
    dl.tv_secs = f->dl_secs;
    dl.tv_micro = f->dl_micro;
    f->late_us = tv_diff(&now, &dl);
    f->resynced = f->late_us > 10L * AM_TICK_US;
    f->waited = 0;
    if (f->resynced)
        dl = now;
    dl.tv_micro += AM_TICK_US;
    f->dl_frac += AM_TICK_FRAC;
    if (f->dl_frac >= 1000) {
        f->dl_frac -= 1000;
        dl.tv_micro++;
    }
    tv_norm(&dl);
    f->dl_secs = dl.tv_secs;
    f->dl_micro = dl.tv_micro;
    if (tv_diff(&dl, &now) <= 0)
        return;
    f->req->tr_node.io_Command = TR_ADDREQUEST;
    if (f->relative) {
        LONG d = tv_diff(&dl, &now);         /* > 0 here */
        f->req->tr_time.tv_secs = (ULONG)d / 1000000UL;
        f->req->tr_time.tv_micro = (ULONG)d % 1000000UL;
    } else {
        f->req->tr_time.tv_secs = dl.tv_secs;    /* a struct timeval32 on AROS x86_64 */
        f->req->tr_time.tv_micro = dl.tv_micro;
    }
    DoIO((struct IORequest *)f->req);
    f->waited = 1;
    {
        LONG w;
        GetSysTime(&now);
        w = tv_diff(&now, &dl);
        f->wake_us = w > 0 ? (ULONG)w : 0;
    }
}

static void gather(struct am_frameos *f)
{
    struct IntuitionBase *IntuitionBase = f->ibase;
    struct IntuiMessage *m;
    ULONG n = 0, lock;
    f->more = 0;
    while (n < AM_F_EVENTS) {
        m = (struct IntuiMessage *)GetMsg(f->win->UserPort);
        if (!m)
            break;
        f->ev[n].cls = m->Class;
        f->ev[n].code = m->Code;
        f->ev[n].qual = m->Qualifier;
        f->ev[n].mx = m->MouseX;
        f->ev[n].my = m->MouseY;
        n++;
        ReplyMsg(&m->ExecMessage);
    }
    if (n == AM_F_EVENTS)
        f->more = 1;                /* the rest stays queued for the next gather */
    f->n_ev = n;
    f->gathered = 1;
    lock = LockIBase(0);
    f->front = IntuitionBase->FirstScreen == f->scr;
    UnlockIBase(lock);
    if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C)
        f->brk = 1;
}

void am_frameos(AM_REG(a0) struct am_frameos *f)
{
    if (f->ops & AM_F_WAIT)
        frame_wait(f);
    if ((f->ops & AM_F_INPUT) && f->win)
        gather(f);
    if (f->ops & AM_F_MEM) {
        f->avail_chip = AvailMem(MEMF_CHIP);
        f->avail_fast = AvailMem(MEMF_FAST);
    }
}
