/* SPDX-License-Identifier: BSD-2-Clause */
/* The 68k half of wosprobe (compiled by vbccm68k, linked into the PowerPC executable).
 *
 * - wp68_layout[]: the OS structure layout as the 68k compiler sees it (the reference)
 * - wp68_vbserver: a vertical-blank interrupt server that signals a task every N frames
 *   (the pattern of the Paula interrupt: 68k interrupt code signalling the PPC audio task)
 * - wp68_procentry: a DOS process entry (CreateNewProcTags from PPC with a 68k entry)
 * - wp68_add, wp68_sum: plain functions the PPC calls through Run68K */
#define __NOLIBBASE__
#include "os_begin.h"
#include <exec/types.h>
#include <exec/nodes.h>
#include <exec/lists.h>
#include <exec/ports.h>
#include <exec/tasks.h>
#include <exec/interrupts.h>
#include <exec/io.h>
#include <exec/libraries.h>
#include <exec/semaphores.h>
#include <exec/execbase.h>
#include <dos/dosextens.h>
#include <dos/rdargs.h>
#include <devices/timer.h>
#include <devices/audio.h>
#include <devices/ahi.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <graphics/rastport.h>
#include <graphics/view.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <intuition/intuitionbase.h>
#include <workbench/startup.h>
#include <workbench/workbench.h>
#include <utility/tagitem.h>
#include <hardware/custom.h>
#include <proto/exec.h>
#include "os_end.h"
#include "audio_paula_irq.h"
#include "oslayout.h"
#include "wosprobe.h"

#define SysBase (*(struct ExecBase **)4L)

#define WP_VALUE(name, v) (ULONG)(v),
const ULONG wp68_layout[] = { AM_OSLAYOUT(WP_VALUE) WP_LAYOUT_END };

__amigainterrupt ULONG wp68_vbserver(__reg("a1") struct wp_vb *d)
{
    if (d->task && ++d->count % d->every == 0) {
        d->sent++;
        Signal(d->task, d->mask);
    }
    return 0;
}

struct wp_proc *wp68_procarg;

void wp68_procentry(void)
{
    struct wp_proc *p = wp68_procarg;
    p->self = FindTask(NULL);
    p->ran = WP_RAN_MAGIC;
    Signal(p->parent, p->mask);
}

ULONG wp68_add(__reg("d0") ULONG a, __reg("d1") ULONG b)
{
    return a + b;
}

ULONG wp68_sum(__reg("a0") const UBYTE *p, __reg("d0") ULONG n)
{
    ULONG s = 0;
    while (n--)
        s += *p++;
    return s;
}
