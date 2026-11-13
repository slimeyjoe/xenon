/* System glue for WarpOS (amsys.h): the backend runs on the PPC; see README.md "WarpOS". */
#include "os_begin.h"
#include <exec/memory.h>
#include <exec/execbase.h>
#include <devices/timer.h>
#include <utility/tagitem.h>
#include <powerpc/powerpc.h>
#include <powerpc/tasksPPC.h>
#include <powerpc/semaphoresPPC.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>
#include <proto/powerpc.h>
#include "oslayout_os.h"
#include "os_end.h"
#include <stdlib.h>
#include <string.h>
#include "amiga.h"
#include "amsys.h"
#include "frameos.h"
#include "wos.h"
#include "audio_paula_irq.h"
#include "oslayout.h"

void am_cache_flush(const void *p, ULONG len)
{
    if (len)
        SetCache(CACHE_DCACHEFLUSH, (APTR)p, len);
}

void am_xcpu_flush(const void *p, ULONG len)
{
    am_cache_flush(p, len);
}

void am_cache_invalidate(const void *p, ULONG len)
{
    if (len)
        SetCache(CACHE_DCACHEINV, (APTR)p, len);
}

int am_task_start(void (*entry)(void), const char *name, LONG pri, ULONG stack)
{
    /* INHERITR2: r2 is the TOC pointer, the task reaches the program's globals through it
     * (without it the task hangs on its first global, probes/warpos "5") */
    struct TagItem tags[6];
    tags[0].ti_Tag = TASKATTR_CODE;       tags[0].ti_Data = (ULONG)entry;
    tags[1].ti_Tag = TASKATTR_NAME;       tags[1].ti_Data = (ULONG)name;
    tags[2].ti_Tag = TASKATTR_PRI;        tags[2].ti_Data = (ULONG)pri;
    tags[3].ti_Tag = TASKATTR_STACKSIZE;  tags[3].ti_Data = stack;
    tags[4].ti_Tag = TASKATTR_INHERITR2;  tags[4].ti_Data = 1;
    tags[5].ti_Tag = TAG_DONE;            tags[5].ti_Data = 0;
    return CreateTaskPPC(tags) ? 0 : -1;
}

int am_task_wait_gone(const char *name, int ticks)
{
    int n;
    for (n = 0; n < ticks; n++) {
        if (!FindTaskPPC((STRPTR)name))
            return 0;
        Delay(1);
    }
    return FindTaskPPC((STRPTR)name) ? -1 : 0;
}

void am_task_end_forbid(void)
{
}

void am_task_stack(void **lower, void **upper)
{
    struct TaskPPC *t = FindTaskPPC(NULL);
    *lower = t->tp_Task.tc_SPLower;
    *upper = t->tp_Task.tc_SPUpper;
}

ULONG am_wait(ULONG mask)
{
    return WaitPPC(mask);
}

/* in the program's (PPC) memory, like the static semaphore it replaces */
struct am_sem {
    struct SignalSemaphorePPC s;
};

struct am_sem *am_sem_create(void)
{
    struct am_sem *s = calloc(1, sizeof *s);
    if (s && InitSemaphorePPC(&s->s) != SSPPC_SUCCESS) {
        free(s);
        s = 0;
    }
    return s;
}

void am_sem_delete(struct am_sem *s)
{
    if (s) {
        FreeSemaphorePPC(&s->s);
        free(s);
    }
}

void am_sem_obtain(struct am_sem *s)
{
    ObtainSemaphorePPC(&s->s);
}

void am_sem_release(struct am_sem *s)
{
    ReleaseSemaphorePPC(&s->s);
}

/* ---- the profile clock --------------------------------------------------------------------- */

/* The PPC time base (the bus clock / 4), read without a 68k call. Its bits 4-35 make the
 * profile ticks: about 1 MHz, so the rate and the wrap-around of the 32-bit sums stay close
 * to the 68k build's E-clock. */
ULONG am_mftbu(void) = "\tmftbu\t3";
ULONG am_mftbl(void) = "\tmftb\t3";

ULONG am_prof_now(void)
{
    ULONG hi, lo;
    do {
        hi = am_mftbu();
        lo = am_mftbl();
    } while (am_mftbu() != hi);
    return hi << 28 | lo >> 4;
}

/* E-clock time of one ReadEClock (a 68k call), placed between the profile clock readings
 * before and after it: returns the E-clock value, *t the profile ticks at the midpoint */
static unsigned long long eclock_at(ULONG *t)
{
    struct EClockVal ev;
    ULONG a = am_prof_now(), b;
    ReadEClock(&ev);
    b = am_prof_now();
    *t = a + (b - a) / 2;
    return (unsigned long long)ev.ev_hi << 32 | ev.ev_lo;
}

/* The time base rate measured against the E-clock over 200 ms. The bus clock WarpOS reports
 * (time base = bus / 4) is not trusted: WarpOS measures it at boot, and in WinUAE's Blizzard
 * PPC it came out anywhere from 65 to 678 MHz for a 16.6 MHz time base (probes/warpos "9"),
 * which is also why GetSysTimePPC is wrong there. */
ULONG am_prof_clock_init(void)
{
    struct TagItem ti[] = { { GETINFO_BUSCLOCK, 0 }, { TAG_DONE, 0 } };
    struct EClockVal ev;
    ULONG efreq = ReadEClock(&ev), t0, t1, f = 0;
    unsigned long long e0, e1;
    GetInfo(ti);
    e0 = eclock_at(&t0);
    Delay(10);
    e1 = eclock_at(&t1);
    if (e1 > e0 && t1 != t0)
        f = (ULONG)((unsigned long long)(t1 - t0) * efreq / (e1 - e0));
    if (!f)
        f = ti[0].ti_Data / 64;
    am_log("timer: PPC time base %lu Hz measured (the bus clock WarpOS reports says %lu)\n",
           (unsigned long)f * 16, (unsigned long)(ti[0].ti_Data / 4));
    return f;
}

/* ---- 68k calls ------------------------------------------------------------------------------ */

static ULONG call68k(void *code, LONG lvo, struct am_regs68k *r)
{
    struct PPCArgs a;
    memset(&a, 0, sizeof a);
    a.PP_Code = code;
    a.PP_Offset = lvo;
    memcpy(&a.PP_Regs[PPREG_D0], r->d, sizeof r->d);
    memcpy(&a.PP_Regs[PPREG_A0], r->a, sizeof r->a);
    Run68K(&a);
    memcpy(r->d, &a.PP_Regs[PPREG_D0], sizeof r->d);
    memcpy(r->a, &a.PP_Regs[PPREG_A0], sizeof r->a);
    return r->d[0];
}

ULONG am_call68k(void *code, struct am_regs68k *r)
{
    return call68k(code, 0, r);
}

ULONG am_call68k_lib(struct Library *base, LONG lvo, struct am_regs68k *r)
{
    r->a[6] = (ULONG)base;
    return call68k(base, lvo, r);
}

void am_frameos_call(struct am_frameos *f, ULONG size)
{
    /* the struct goes to the 68k out of the PPC data cache and comes back without stale
     * lines: flushed whole, so the invalidate afterwards drops nothing pending (this task is
     * blocked in Run68K meanwhile) */
    struct am_regs68k r;
    memset(&r, 0, sizeof r);
    r.a[0] = (ULONG)f;
    am_cache_flush(f, size);
    am_call68k((void *)am_frameos, &r);
    am_cache_invalidate(f, size);
}

/* ---- the layout guard ------------------------------------------------------------------------ */

extern const ULONG am_oslayout68k[];        /* oslayout68k.c */

#define AM_LAYOUT_NAME(name, v) name,
#define AM_LAYOUT_VALUE(name, v) (ULONG)(v),
static const char *const g_layout_names[] = { AM_OSLAYOUT(AM_LAYOUT_NAME) 0 };
static const ULONG g_layout_ppc[] = { AM_OSLAYOUT(AM_LAYOUT_VALUE) 0xFFFFFFFFUL };

int am_sys_check(void)
{
    int i, bad = 0;
    for (i = 0; g_layout_names[i]; i++) {
        if (g_layout_ppc[i] == am_oslayout68k[i])
            continue;
        if (bad++ < 4)
            am_log("layout: %s is %lu for the PPC, %lu for the 68k\n", g_layout_names[i],
                   (unsigned long)g_layout_ppc[i], (unsigned long)am_oslayout68k[i]);
    }
    if (bad)
        am_log("layout: %d of %d OS structure sizes/offsets differ from the 68k's: the OS "
               "headers must be read under #pragma amiga-align (core/os_begin.h)\n", bad, i);
    return bad ? -1 : 0;
}
