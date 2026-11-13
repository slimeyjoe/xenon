/* SPDX-License-Identifier: BSD-2-Clause */
/* WarpOS probe: the facts the PowerPC build of the Amiga backend relies on.
 *
 *   wosprobe        (WarpOS, from a shell; prints to stdout)
 *
 * 1 struct layout: the OS structures under #pragma amiga-align vs. the 68k compiler, and
 *   without the pragma (wosprobe_nat.c), see backends/amiga/core/os_begin.h
 * 2 the tasks: FindTask/FindTaskPPC of the main program, stack sizes
 * 3 Run68K: a 68k function with register arguments, an OS call by base + LVO
 * 4 a variadic stub (CreateNewProcTags) starting a 68k process entry
 * 5 a PPC task (CreateTaskPPC): its 68k mirror, timer.device opened from the task,
 *   signal round trips with the main task, 68k interrupt signals to its mirror received
 *   with Wait() (68k stub) and with WaitPPC()
 * 6 the cost of a 68k call from PPC (context switch), PPC-native time
 * 7 custom chip registers read from PPC
 * 8 chip RAM written by PPC, read by 68k code
 * 9 timing: the PPC time base against the E-clock, the latency of a timer.device wait
 *   (DoIO from PPC), how fast a 68k Signal() wakes WaitPPC() and Wait() */
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
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
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
#include <hardware/intbits.h>
#include <powerpc/powerpc.h>
#include <powerpc/tasksPPC.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>
#include <proto/powerpc.h>
#include "os_end.h"
#include <stdio.h>
#include <string.h>
#include "audio_paula_irq.h"
#include "oslayout.h"
#include "wosprobe.h"

#define HWCUSTOM ((volatile struct Custom *)0xdff000)

/* wosprobe68k.c */
extern const ULONG wp68_layout[];
extern struct wp_proc *wp68_procarg;
extern void wp68_vbserver(void);
extern void wp68_procentry(void);
extern void wp68_add(void);
extern void wp68_sum(void);
/* wosprobe_nat.c: the same table without #pragma amiga-align */
extern const ULONG wp_nat_layout[];

#define WP_NAME(name, v) name,
#define WP_VALUE(name, v) (ULONG)(v),
static const char *const wp_names[] = { AM_OSLAYOUT(WP_NAME) 0 };
static const ULONG wp_ppc_layout[] = { AM_OSLAYOUT(WP_VALUE) WP_LAYOUT_END };

static void layout(void)
{
    int i, bad = 0, badnat = 0;
    for (i = 0; wp_names[i]; i++) {
        if (wp_ppc_layout[i] != wp68_layout[i]) {
            printf("  MISMATCH %-28s 68k %3lu  ppc(amiga-align) %3lu\n", wp_names[i],
                   (unsigned long)wp68_layout[i], (unsigned long)wp_ppc_layout[i]);
            bad++;
        }
        if (wp_nat_layout[i] != wp68_layout[i])
            badnat++;
    }
    printf("1 layout: %d entries, %d differ under amiga-align, %d differ without it\n", i, bad,
           badnat);
    for (i = 0; wp_names[i] && badnat; i++)
        if (wp_nat_layout[i] != wp68_layout[i] && badnat-- > 0)
            printf("  natural  %-28s 68k %3lu  ppc(natural) %3lu\n", wp_names[i],
                   (unsigned long)wp68_layout[i], (unsigned long)wp_nat_layout[i]);
}

struct Device *TimerBase;      /* the amiga.lib ReadEClock stub reads this global */
static struct timerequest g_eclock_tr;
static ULONG g_efreq;

/* PPC-native time (GetSysTimePPC: no 68k call) */
static ULONG ppc_us(void)
{
    struct timeval tv;
    GetSysTimePPC(&tv);
    return (ULONG)tv.tv_secs * 1000000UL + (ULONG)tv.tv_micro;
}

/* 68k E-clock time (one 68k call; the reference: CIA timers run in real time) */
static ULONG now_us(void)
{
    struct EClockVal ev;
    ReadEClock(&ev);
    return (ULONG)(((unsigned long long)ev.ev_hi << 32 | ev.ev_lo) * 1000000ULL / g_efreq);
}

static void tasks(void)
{
    struct Task *t68 = FindTask(NULL);
    struct TaskPPC *tp = FindTaskPPC(NULL);
    struct Process *pr = (struct Process *)t68;
    printf("2 tasks: FindTask(NULL) = %p \"%s\" type %d pri %d cli %s, stack %lu\n", (void *)t68,
           t68->tc_Node.ln_Name ? t68->tc_Node.ln_Name : "", (int)t68->tc_Node.ln_Type,
           (int)t68->tc_Node.ln_Pri, pr->pr_CLI ? "yes" : "no",
           (unsigned long)((ULONG)t68->tc_SPUpper - (ULONG)t68->tc_SPLower));
    printf("  FindTaskPPC(NULL) = %p \"%s\" pri %d, stack %lu (tp_StackSize %lu), sp %p in [%p,%p]\n",
           (void *)tp, tp->tp_Task.tc_Node.ln_Name ? tp->tp_Task.tc_Node.ln_Name : "",
           (int)tp->tp_Task.tc_Node.ln_Pri,
           (unsigned long)((ULONG)tp->tp_Task.tc_SPUpper - (ULONG)tp->tp_Task.tc_SPLower),
           (unsigned long)tp->tp_StackSize, (void *)&t68, tp->tp_Task.tc_SPLower,
           tp->tp_Task.tc_SPUpper);
    printf("  PowerPCBase %p version %d.%d\n", (void *)PowerPCBase,
           (int)((struct Library *)PowerPCBase)->lib_Version,
           (int)((struct Library *)PowerPCBase)->lib_Revision);
}

static ULONG call68k(void *code, LONG lvo, ULONG *regs)
{
    struct PPCArgs a;
    memset(&a, 0, sizeof a);
    a.PP_Code = code;
    a.PP_Offset = lvo;
    memcpy(a.PP_Regs, regs, sizeof a.PP_Regs);
    Run68K(&a);
    memcpy(regs, a.PP_Regs, sizeof a.PP_Regs);
    return a.PP_Regs[PPREG_D0];
}

static void run68k(void)
{
    ULONG r[15] = { 0 };
    ULONG got, stub;
    r[PPREG_D0] = 40;
    r[PPREG_D1] = 2;
    got = call68k((void *)wp68_add, 0, r);
    printf("3 Run68K: wp68_add(40, 2) = %lu\n", (unsigned long)got);
    memset(r, 0, sizeof r);
    r[PPREG_D1] = MEMF_CHIP;
    r[PPREG_A6] = (ULONG)SysBase;
    got = call68k((void *)SysBase, -216, r);         /* exec AvailMem */
    stub = AvailMem(MEMF_CHIP);
    printf("  Run68K exec AvailMem(MEMF_CHIP) by LVO = %lu, through the stub = %lu\n",
           (unsigned long)got, (unsigned long)stub);
}

static void proc68k(void)
{
    static struct wp_proc p;
    BYTE sig = AllocSignal(-1);
    struct Process *pr;
    int n;
    if (sig == -1)
        return;
    p.parent = FindTask(NULL);
    p.mask = 1UL << sig;
    p.self = 0;
    p.ran = 0;
    wp68_procarg = &p;
    pr = CreateNewProcTags(NP_Entry, (ULONG)wp68_procentry, NP_Name, (ULONG)"wosprobe 68k",
                           NP_StackSize, 4096, NP_Priority, 0, TAG_DONE);
    for (n = 0; pr && n < 100 && !(SetSignal(0, 0) & p.mask); n++)
        Delay(1);
    SetSignal(0, p.mask);
    printf("4 CreateNewProcTags(68k entry): %s, process %p, it saw itself as %p, ran %08lx after %d ticks\n",
           pr ? "created" : "FAILED", (void *)pr, (void *)p.self, (unsigned long)p.ran, n);
    FreeSignal(sig);
}

/* the PPC child task */
static struct {
    struct Task *main68k;       /* the main program's 68k task */
    struct Task *mirror;        /* the child's 68k mirror, as FindTask(NULL) gives it */
    struct TaskPPC *self;
    BYTE s_ready, s_pong;       /* main's signals */
    BYTE c_ping, c_vb, c_quit;  /* child's signals */
    volatile int ready, done;
    LONG timer_err;
    ULONG timer_us;
    int pings, vb_wait, vb_waitppc;
} C;

static void child(void)
{
    struct MsgPort *port;
    struct timerequest *tr;
    ULONG t0;
    int i;
    C.self = FindTaskPPC(NULL);
    C.mirror = FindTask(NULL);
    C.c_ping = AllocSignal(-1);
    C.c_vb = AllocSignal(-1);
    C.c_quit = AllocSignal(-1);
    /* a device opened from the PPC task: the 68k side runs in its mirror process */
    C.timer_err = -1;
    port = CreateMsgPort();
    tr = port ? (struct timerequest *)CreateIORequest(port, sizeof *tr) : 0;
    if (tr && (C.timer_err = OpenDevice(TIMERNAME, UNIT_MICROHZ, (struct IORequest *)tr, 0)) == 0) {
        t0 = now_us();
        tr->tr_node.io_Command = TR_ADDREQUEST;
        tr->tr_time.tv_secs = 0;
        tr->tr_time.tv_micro = 100000;
        DoIO((struct IORequest *)tr);
        C.timer_us = now_us() - t0;
        CloseDevice((struct IORequest *)tr);
    }
    if (tr) DeleteIORequest((struct IORequest *)tr);
    if (port) DeleteMsgPort(port);
    C.ready = 1;
    Signal(C.main68k, 1UL << C.s_ready);
    /* ping-pong: main signals our mirror, we answer */
    for (i = 0; i < 200; i++) {
        ULONG got = Wait((1UL << C.c_ping) | (1UL << C.c_quit));
        if (got & (1UL << C.c_quit))
            goto out;
        C.pings++;
        Signal(C.main68k, 1UL << C.s_pong);
    }
    /* vertical-blank signals from 68k interrupt code: 10 with the 68k Wait, 10 with WaitPPC */
    for (i = 0; i < 10; i++) {
        ULONG got = Wait((1UL << C.c_vb) | (1UL << C.c_quit));
        if (got & (1UL << C.c_quit))
            goto out;
        C.vb_wait++;
    }
    for (i = 0; i < 10; i++) {
        ULONG got = WaitPPC((1UL << C.c_vb) | (1UL << C.c_quit));
        if (got & (1UL << C.c_quit))
            goto out;
        C.vb_waitppc++;
    }
    Wait(1UL << C.c_quit);
out:
    FreeSignal(C.c_ping);
    FreeSignal(C.c_vb);
    FreeSignal(C.c_quit);
    C.done = 1;
    Signal(C.main68k, 1UL << C.s_ready);
}

static int wait_sig(BYTE sig, int ticks)
{
    int n;
    for (n = 0; n < ticks; n++) {
        if (SetSignal(0, 1UL << sig) & (1UL << sig))
            return 1;
        Delay(1);
    }
    return 0;
}

static void ppctask(void)
{
    struct TagItem tags[] = {
        { TASKATTR_CODE, (ULONG)child }, { TASKATTR_NAME, (ULONG)"wosprobe child" },
        { TASKATTR_PRI, 5 }, { TASKATTR_STACKSIZE, 32768 }, { TASKATTR_INHERITR2, 1 },
        { TAG_DONE, 0 } };
    struct wp_vb *vb = AllocMem(sizeof *vb, MEMF_CHIP | MEMF_CLEAR);
    struct Interrupt *vbi = AllocMem(sizeof *vbi, MEMF_PUBLIC | MEMF_CLEAR);
    struct TaskPPC *tp;
    ULONG t0, t1;
    int i, ok;
    memset(&C, 0, sizeof C);
    C.main68k = FindTask(NULL);
    C.s_ready = AllocSignal(-1);
    C.s_pong = AllocSignal(-1);
    if (!vb || !vbi || C.s_ready == -1 || C.s_pong == -1) {
        printf("5 PPC task: setup FAILED\n");
        return;
    }
    tp = CreateTaskPPC(tags);
    ok = tp && wait_sig(C.s_ready, 250);
    printf("5 CreateTaskPPC: %s, task %p (self %p), 68k mirror %p (main 68k task %p), "
           "timer.device from the task: err %ld, 100 ms took %lu us\n",
           ok ? "running" : "FAILED", (void *)tp, (void *)C.self, (void *)C.mirror,
           (void *)C.main68k, (long)C.timer_err, (unsigned long)C.timer_us);
    if (!ok)
        return;
    t0 = now_us();
    for (i = 0; i < 200; i++) {
        Signal(C.mirror, 1UL << C.c_ping);
        if (!wait_sig(C.s_pong, 50))
            break;
    }
    t1 = now_us();
    printf("  ping-pong main <-> task mirror: %d of 200, %lu us each\n", C.pings,
           (unsigned long)(i ? (t1 - t0) / (ULONG)i : 0));
    /* 68k vertical-blank interrupt server signals the task's mirror every 5 frames */
    vb->task = C.mirror;
    vb->mask = 1UL << C.c_vb;
    vb->every = 5;
    vbi->is_Node.ln_Type = NT_INTERRUPT;
    vbi->is_Node.ln_Pri = 0;
    vbi->is_Node.ln_Name = "wosprobe vb";
    vbi->is_Data = vb;
    vbi->is_Code = (void (*)(void))wp68_vbserver;
    AddIntServer(INTB_VERTB, vbi);
    for (i = 0; i < 250 && C.vb_wait + C.vb_waitppc < 20; i++)
        Delay(1);
    RemIntServer(INTB_VERTB, vbi);
    printf("  68k VERTB server -> mirror: %lu frames, %lu signals sent; task got %d with Wait(), "
           "%d with WaitPPC()\n", (unsigned long)vb->count, (unsigned long)vb->sent, C.vb_wait,
           C.vb_waitppc);
    Signal(C.mirror, 1UL << C.c_quit);
    ok = wait_sig(C.s_ready, 100);
    for (i = 0; ok && i < 100 && FindTaskPPC("wosprobe child"); i++)
        Delay(1);
    printf("  child end: %s, gone from the PPC task list after %d ticks\n",
           ok && C.done ? "signalled" : "NO SIGNAL", i);
    FreeSignal(C.s_ready);
    FreeSignal(C.s_pong);
    FreeMem(vbi, sizeof *vbi);
    FreeMem(vb, sizeof *vb);
}

static void cost(void)
{
    ULONG t0, t1, p0, p1, s = 0;
    int i;
    t0 = now_us();
    p0 = ppc_us();
    for (i = 0; i < 1000; i++)
        s += AvailMem(MEMF_CHIP) & 1;
    p1 = ppc_us();
    t1 = now_us();
    printf("6 cost of a 68k call from PPC (emulated, not real-hardware speed): AvailMem %lu us "
           "E-clock, %lu us PPC clock (checksum %lu)\n", (unsigned long)((t1 - t0) / 1000),
           (unsigned long)((p1 - p0) / 1000), (unsigned long)s);
    t0 = now_us();
    p0 = ppc_us();
    Delay(50);
    p1 = ppc_us();
    t1 = now_us();
    printf("  Delay(50) = 1 s: %lu us E-clock, %lu us PPC clock (GetSysTimePPC)\n",
           (unsigned long)(t1 - t0), (unsigned long)(p1 - p0));
}

/* the time base, lower half (no 68k call) */
ULONG wp_mftbl(void) = "\tmftb\t3";

static void timing(void)
{
    struct TagItem ti[] = { { GETINFO_BUSCLOCK, 0 }, { GETINFO_CPUCLOCK, 0 }, { TAG_DONE, 0 } };
    struct MsgPort *port = CreateMsgPort();
    struct timerequest *tr = port ? (struct timerequest *)CreateIORequest(port, sizeof *tr) : 0;
    struct wp_vb *vb = AllocMem(sizeof *vb, MEMF_CHIP | MEMF_CLEAR);
    struct Interrupt *vbi = AllocMem(sizeof *vbi, MEMF_PUBLIC | MEMF_CLEAR);
    BYTE sig = AllocSignal(-1);
    ULONG t0, t1, b0, b1, f0, late_max = 0;
    int i;
    GetInfo(ti);
    t0 = now_us();
    b0 = wp_mftbl();
    Delay(50);
    b1 = wp_mftbl();
    t1 = now_us();
    printf("9 PPC bus clock %lu Hz, CPU clock %lu Hz; time base over Delay(50): %lu ticks in "
           "%lu us E-clock = %lu ticks/s (bus/4 = %lu)\n", (unsigned long)ti[0].ti_Data,
           (unsigned long)ti[1].ti_Data, (unsigned long)(b1 - b0), (unsigned long)(t1 - t0),
           (unsigned long)((unsigned long long)(b1 - b0) * 1000000ULL / (t1 - t0)),
           (unsigned long)(ti[0].ti_Data / 4));
    if (!tr || !vb || !vbi || sig == -1 || OpenDevice(TIMERNAME, UNIT_MICROHZ, (struct IORequest *)tr, 0) != 0) {
        printf("  timing setup FAILED\n");
        goto out;
    }
    /* a timer wait from PPC: 20 x DoIO(TR_ADDREQUEST 1 ms), each measured on the E-clock */
    t0 = now_us();
    for (i = 0; i < 20; i++) {
        ULONG a = now_us(), d;
        tr->tr_node.io_Command = TR_ADDREQUEST;
        tr->tr_time.tv_secs = 0;
        tr->tr_time.tv_micro = 1000;
        DoIO((struct IORequest *)tr);
        d = now_us() - a;
        if (d > late_max)
            late_max = d;
    }
    t1 = now_us();
    printf("  DoIO(TR_ADDREQUEST 1 ms) from PPC: %lu us each (with two E-clock reads), "
           "longest %lu us\n", (unsigned long)((t1 - t0) / 20), (unsigned long)late_max);
    CloseDevice((struct IORequest *)tr);
    /* a 68k VERTB server signals this task (its 68k side) every frame: 50 wakes */
    vb->task = FindTask(NULL);
    vb->mask = 1UL << sig;
    vb->every = 1;
    vbi->is_Node.ln_Type = NT_INTERRUPT;
    vbi->is_Node.ln_Name = "wosprobe vb timing";
    vbi->is_Data = vb;
    vbi->is_Code = (void (*)(void))wp68_vbserver;
    SetSignal(0, 1UL << sig);
    AddIntServer(INTB_VERTB, vbi);
    f0 = vb->count;
    t0 = now_us();
    b0 = wp_mftbl();
    /* WaitTime = WaitPPC with a timeout (in PPC time, which runs slow in WinUAE): the
     * signal goes to the shell's process, not a mirror WarpOS created */
    {
        int got = 0;
        for (i = 0; i < 50; i++)
            if (WaitTime(1UL << sig, 200000))
                got++;
        b1 = wp_mftbl();
        t1 = now_us();
        printf("  50 x WaitTime(200 ms) for a 68k VERTB Signal to the shell process: %d woken, "
               "%lu us E-clock (%lu frames), %lu time base ticks\n", got,
               (unsigned long)(t1 - t0), (unsigned long)(vb->count - f0),
               (unsigned long)(b1 - b0));
    }
    f0 = vb->count;
    t0 = now_us();
    for (i = 0; i < 50; i++)
        Wait(1UL << sig);
    t1 = now_us();
    printf("  50 x Wait (68k stub) woken the same way: %lu us E-clock (%lu frames)\n",
           (unsigned long)(t1 - t0), (unsigned long)(vb->count - f0));
    RemIntServer(INTB_VERTB, vbi);
out:
    if (sig != -1) FreeSignal(sig);
    if (vbi) FreeMem(vbi, sizeof *vbi);
    if (vb) FreeMem(vb, sizeof *vb);
    if (tr) DeleteIORequest((struct IORequest *)tr);
    if (port) DeleteMsgPort(port);
}

static void custom(void)
{
    UWORD v1 = HWCUSTOM->vhposr, v2;
    int i;
    for (i = 0; i < 100000 && (v2 = HWCUSTOM->vhposr) == v1; i++)
        ;
    printf("7 custom chips from PPC: vhposr %04x -> %04x (after %d reads), intenar %04x, dmaconr %04x\n",
           (unsigned)v1, (unsigned)v2, i, (unsigned)HWCUSTOM->intenar, (unsigned)HWCUSTOM->dmaconr);
}

static void chipram(void)
{
    UBYTE *buf = AllocMem(4096, MEMF_CHIP);
    ULONG r[15] = { 0 }, want = 0, got;
    int i;
    if (!buf)
        return;
    for (i = 0; i < 4096; i++) {
        buf[i] = (UBYTE)(i * 7 + 3);
        want += buf[i];
    }
    r[PPREG_A0] = (ULONG)buf;
    r[PPREG_D0] = 4096;
    got = call68k((void *)wp68_sum, 0, r);
    printf("8 chip RAM written by PPC, summed by 68k (no explicit flush): %lu, expected %lu: %s\n",
           (unsigned long)got, (unsigned long)want, got == want ? "same" : "DIFFERENT");
    SetCache(CACHE_DCACHEFLUSH, buf, 4096);
    memset(r, 0, sizeof r);
    r[PPREG_A0] = (ULONG)buf;
    r[PPREG_D0] = 4096;
    got = call68k((void *)wp68_sum, 0, r);
    printf("  after SetCache(CACHE_DCACHEFLUSH): %lu: %s\n", (unsigned long)got,
           got == want ? "same" : "DIFFERENT");
    FreeMem(buf, 4096);
}

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 512);     /* a hang still leaves the lines before it */
    printf("wosprobe: PowerPC under WarpOS\n");
    if (OpenDevice(TIMERNAME, UNIT_ECLOCK, (struct IORequest *)&g_eclock_tr, 0) != 0) {
        printf("timer.device FAILED\n");
        return 20;
    }
    TimerBase = g_eclock_tr.tr_node.io_Device;
    {
        struct EClockVal ev;
        g_efreq = ReadEClock(&ev);
    }
    layout();
    tasks();
    run68k();
    proc68k();
    ppctask();
    cost();
    timing();
    custom();
    chipram();
    CloseDevice((struct IORequest *)&g_eclock_tr);
    printf("wosprobe: done\n");
    return 0;
}
