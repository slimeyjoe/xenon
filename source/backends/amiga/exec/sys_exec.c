/* System glue for one CPU running exec (amsys.h): AmigaOS 3.x on the 68k, AROS. */
#include "os_begin.h"
#include <exec/memory.h>
#include <exec/execbase.h>
#include <exec/semaphores.h>
#include <dos/dostags.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>
#include "os_end.h"
#include <string.h>
#include "amiga.h"
#include "amsys.h"
#include "frameos.h"

void am_cache_flush(const void *p, ULONG len)
{
    CacheClearE((APTR)p, len, CACRF_ClearD);
}

void am_xcpu_flush(const void *p, ULONG len)
{
    (void)p; (void)len;
}

void am_cache_invalidate(const void *p, ULONG len)
{
    (void)p; (void)len;
}

int am_task_start(void (*entry)(void), const char *name, LONG pri, ULONG stack)
{
    struct Process *pr = CreateNewProcTags(NP_Entry, (IPTR)entry,
                                           NP_Name, (IPTR)name,
                                           NP_Priority, pri,
                                           NP_StackSize, stack,
                                           NP_Input, 0, NP_Output, 0, NP_CloseInput, FALSE,
                                           NP_CloseOutput, FALSE, NP_WindowPtr, -1,
                                           TAG_DONE);
    return pr ? 0 : -1;
}

int am_task_wait_gone(const char *name, int ticks)
{
    (void)name; (void)ticks;
    return 0;                       /* a DOS process frees itself when its entry returns */
}

void am_task_end_forbid(void)
{
    Forbid();                       /* held until the process is gone */
}

void am_task_stack(void **lower, void **upper)
{
    struct Task *t = FindTask(NULL);
    *lower = t->tc_SPLower;
    *upper = t->tc_SPUpper;
}

ULONG am_wait(ULONG mask)
{
    return Wait(mask);
}

struct am_sem {
    struct SignalSemaphore s;
};

struct am_sem *am_sem_create(void)
{
    struct am_sem *s = AllocVec(sizeof *s, MEMF_PUBLIC | MEMF_CLEAR);
    if (s)
        InitSemaphore(&s->s);
    return s;
}

void am_sem_delete(struct am_sem *s)
{
    FreeVec(s);
}

void am_sem_obtain(struct am_sem *s)
{
    ObtainSemaphore(&s->s);
}

void am_sem_release(struct am_sem *s)
{
    ReleaseSemaphore(&s->s);
}

/* the profile clock: the E-clock's low 32 bits */
ULONG am_prof_now(void)
{
    struct EClockVal ev;
    if (!TimerBase)
        return 0;
    ReadEClock(&ev);
    return ev.ev_lo;
}

ULONG am_prof_clock_init(void)
{
    struct EClockVal ev;
    return ReadEClock(&ev);
}

void am_frameos_call(struct am_frameos *f, ULONG size)
{
    (void)size;
    am_frameos(f);
}

int am_sys_check(void)
{
    return 0;
}
