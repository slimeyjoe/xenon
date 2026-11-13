/* System glue of the Amiga backend: what differs between a plain exec system and WarpOS's
 * two CPUs. One part implements all of it (README.md "Layout"):
 *   exec/sys_exec.c    one CPU running exec: AmigaOS 3.x on the 68k, AROS (later MorphOS,
 *                      AmigaOS 4)
 *   warpos/sys_wos.c   WarpOS: the backend runs on the PPC and reaches the OS through
 *                      amiga.lib's stubs, each a Run68K context switch to the 68k; code that
 *                      must run on the 68k is compiled by the 68k compiler (config/warpos.mk)
 * The WarpOS side is described in README.md "WarpOS" and verified by probes/warpos. */
#ifndef XE_AMIGA_AMSYS_H
#define XE_AMIGA_AMSYS_H

#include "os_begin.h"
#include <exec/types.h>
#include "os_end.h"

struct am_frameos;

/* Make data this CPU wrote visible to DMA (and to the other CPU): CacheClearE on exec, a PPC
 * data cache flush of the range on WarpOS. */
void am_cache_flush(const void *p, ULONG len);

/* The same, only for the other CPU: data the 68k reads that a single-CPU system never needs
 * to flush (the 68k reads its own writes; chip RAM is not copyback-cached there). A no-op on
 * exec, so that build is unchanged; a flush on WarpOS. */
void am_xcpu_flush(const void *p, ULONG len);

/* Make data DMA or the other CPU wrote visible to this CPU before reading it: nothing on exec
 * (chip RAM is not copyback-cached on the 68k), a PPC data cache invalidate of the range on
 * WarpOS. The range must be whole cache lines (32 bytes, aligned) that this CPU does not
 * write: an invalidate drops pending writes. */
void am_cache_invalidate(const void *p, ULONG len);

/* A task running `entry` (no arguments; it ends by returning): a DOS process on exec, a PPC
 * task on WarpOS (CreateTaskPPC; its OS calls run in a 68k mirror process that WarpOS
 * creates). Returns 0 on success. */
int am_task_start(void (*entry)(void), const char *name, LONG pri, ULONG stack);

/* After the task signalled that it is ending: wait until it is really gone, so its code is
 * never unloaded under it. An exec task Forbid()s before its last signal and needs nothing;
 * a PPC task runs on after its signal (Forbid() on the 68k does not hold the PPC
 * scheduler): this waits until the task left WarpOS's task list, at most `ticks` (1/50 s).
 * Returns 0 when gone, -1 on timeout. */
int am_task_wait_gone(const char *name, int ticks);

/* Forbid()/Permit() around a task's last signal before it returns (exec only; a no-op on
 * WarpOS, see am_task_wait_gone). */
void am_task_end_forbid(void);

/* The stack bounds of the calling task (the PPC stack on WarpOS). */
void am_task_stack(void **lower, void **upper);

/* Wait for signals: Wait() on exec; WaitPPC() on WarpOS, no 68k call. Signals are shared
 * between a PPC task and its 68k mirror (powerpc.doc WaitPPC, V11+; probes/warpos "5"):
 * a 68k Signal() to the mirror (the Paula interrupt, the main task) wakes it. */
ULONG am_wait(ULONG mask);

/* A semaphore between the tasks of the program: an exec SignalSemaphore, or a
 * SignalSemaphorePPC on WarpOS (both tasks are PPC tasks there; no 68k call). NULL when out
 * of memory. */
struct am_sem;
struct am_sem *am_sem_create(void);
void am_sem_delete(struct am_sem *s);
void am_sem_obtain(struct am_sem *s);
void am_sem_release(struct am_sem *s);

/* The profile clock (amiga.h am_prof_*): ticks of about 1 MHz or less, readable from any task
 * without a 68k call. am_prof_clock_init (after timer.device is open, main task) returns its
 * rate in ticks per second: the E-clock's on exec; the PPC time base / 16 measured against the
 * E-clock on WarpOS. */
ULONG am_prof_clock_init(void);

/* Run the once-per-frame OS work (frameos.h): a direct call on exec, one Run68K on WarpOS. */
void am_frameos_call(struct am_frameos *f, ULONG size);

/* Start-up check of the system part: WarpOS compares the OS structure layout its compiler used
 * with the 68k compiler's (warpos/oslayout.h), logs the first mismatches and returns -1 if
 * any. Always 0 on exec. */
int am_sys_check(void);

#endif
