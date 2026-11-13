/* opnbench: how much of a 68040 does opn_lite need?  (plan §1.4 / §7 phase 0c, done in
 * phase 3 once the core existed)
 *
 * Replays a real register log (opnbench_log.h, generated from a song VGM by mkopnlog.py)
 * through opn_lite at several output rates, with and without 2x FM oversampling, rendering
 * into a scratch buffer, and times each run with the E clock (timer.device ReadEClock,
 * guest time, independent of WinUAE's warp mode). A calibration loop reports the CPU's
 * integer throughput so the figures can be read against a real machine.
 *
 * Output (DH0:out.txt via the startup-sequence): one line per configuration with the
 * rendered seconds, the elapsed E-clock seconds and the CPU share in percent.
 *
 * "opnbench sum" prints a hash of every rendered sample per configuration instead of the
 * timing; opnsum.c computes the same on the host with the same C core, so the Amiga build
 * (vbcc, and the assembly render loop) is checked bit for bit against it. */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/io.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "snd/opn_core.h"
#include <opnbench_log.h>          /* bench-<song>/, see the Makefile (SONG=) */

struct Device *TimerBase;
static struct MsgPort *tport;
static struct timerequest *treq;

#define CLOCK 3993600UL
#define BUF 1024

static int timer_open(void)
{
    tport = CreateMsgPort();
    if (!tport)
        return 0;
    treq = (struct timerequest *)CreateIORequest(tport, sizeof(struct timerequest));
    if (!treq)
        return 0;
    if (OpenDevice("timer.device", UNIT_MICROHZ, (struct IORequest *)treq, 0) != 0)
        return 0;
    TimerBase = treq->tr_node.io_Device;
    return 1;
}

static void timer_close(void)
{
    if (TimerBase)
        CloseDevice((struct IORequest *)treq);
    if (treq)
        DeleteIORequest((struct IORequest *)treq);
    if (tport)
        DeleteMsgPort(tport);
}

static ULONG eclock_freq;

static ULONG eclock_now(void)
{
    struct EClockVal ev;
    eclock_freq = ReadEClock(&ev);
    return ev.ev_lo;             /* wraps after ~100 min, fine here */
}

static int do_sum;
static unsigned long sum;

static void fold(const short *buf, int n)
{
    int k;
    for (k = 0; k < n; k++)
        sum = sum * 31 + (unsigned short)buf[k];
}

/* render the log once; returns rendered samples */
static unsigned long run_log(struct opn_core *c, unsigned long rate, short *buf)
{
    unsigned long i, done = 0;
    for (i = 0; i < OPNLOG_COUNT; i++) {
        /* core samples at which this write is due */
        unsigned long target = (unsigned long)(((unsigned long long)opnlog[i].t * rate + 22050) / 44100);
        while (done < target) {
            int n = (int)((target - done) > BUF ? BUF : (target - done));
            opn_lite_ops.render(c, buf, n);
            if (do_sum)
                fold(buf, n);
            done += n;
        }
        opn_lite_ops.write(c, opnlog[i].reg, opnlog[i].val);
    }
    {
        unsigned long target = (unsigned long)(((unsigned long long)OPNLOG_SAMPLES * rate + 22050) / 44100);
        while (done < target) {
            int n = (int)((target - done) > BUF ? BUF : (target - done));
            opn_lite_ops.render(c, buf, n);
            if (do_sum)
                fold(buf, n);
            done += n;
        }
    }
    return done;
}

static void bench(FILE *out, unsigned long want, int os, short *buf)
{
    struct opn_core *c;
    unsigned long rate = 0, samples;
    ULONG t0, t1, dt;
    unsigned long ms_audio, ms_cpu, permille;

    opn_lite_fm_oversample = os;
    c = opn_lite_ops.create(CLOCK, want, &rate);
    if (!c) {
        fprintf(out, "rate %lu: create failed\n", want);
        return;
    }
    sum = 0;
    t0 = eclock_now();
    samples = run_log(c, rate, buf);
    t1 = eclock_now();
    dt = t1 - t0;
    if (do_sum) {
        fprintf(out, "rate %5lu os %d: %7lu samples, sum %08lx\n", rate, os, samples, sum);
        opn_lite_ops.destroy(c);
        return;
    }
    /* integer only: vbcc's double support needs a math library we do not want here */
    ms_audio = (unsigned long)(((unsigned long long)samples * 1000) / rate);
    ms_cpu = (unsigned long)(((unsigned long long)dt * 1000) / eclock_freq);
    permille = (unsigned long)(((unsigned long long)ms_cpu * 1000) / (ms_audio ? ms_audio : 1));
    fprintf(out, "rate %5lu os %d: %7lu samples = %5lu.%03lu s audio in %5lu.%03lu s cpu -> %3lu.%lu%% of the CPU\n",
            rate, os, samples, ms_audio / 1000, ms_audio % 1000, ms_cpu / 1000, ms_cpu % 1000,
            permille / 10, permille % 10);
    opn_lite_ops.destroy(c);
}

/* integer throughput reference: 10 M iterations of a dependent add/shift/xor chain with no
 * multiply (about 8 instructions per iteration including the loop), and 10 M 32x32 multiplies */
static void calibrate(FILE *out)
{
    volatile unsigned long sink;
    unsigned long i, a = 1, b = 2, x = 3, ms, ms2;
    ULONG t0, t1;
    t0 = eclock_now();
    for (i = 0; i < 10000000UL; i++) {
        a = (a + b) ^ x;
        b = (b >> 1) + a;
        x = (x << 1) + 1;
    }
    t1 = eclock_now();
    ms = (unsigned long)(((unsigned long long)(t1 - t0) * 1000) / eclock_freq);
    t0 = eclock_now();
    for (i = 0; i < 10000000UL; i++)
        a = a * b + i;
    t1 = eclock_now();
    ms2 = (unsigned long)(((unsigned long long)(t1 - t0) * 1000) / eclock_freq);
    sink = a + b + x;
    (void)sink;
    fprintf(out, "calibration: 10 M x (add, xor, shift, add, shift, add, loop) in %lu.%03lu s; 10 M x (mul, add, loop) in %lu.%03lu s (E clock %lu Hz)\n",
            ms / 1000, ms % 1000, ms2 / 1000, ms2 % 1000, (unsigned long)eclock_freq);
}

int main(int argc, char **argv)
{
    FILE *out = stdout;
    short *buf;
    int i;
    static const unsigned long rates[] = { 22050, 11025, 27500, 44100 };

    if (argc > 1 && !strcmp(argv[1], "sum"))
        do_sum = 1;
    else if (argc > 1) {
        out = fopen(argv[1], "a");
        if (!out)
            out = stdout;
    }
    if (!timer_open()) {
        fprintf(out, "timer.device failed\n");
        return 20;
    }
    buf = AllocMem(BUF * sizeof(short), MEMF_ANY);
    fprintf(out, "opnbench: log of %d writes, %lu s of song data\n", OPNLOG_COUNT,
            (unsigned long)(OPNLOG_SAMPLES / 44100));
    calibrate(out);
    for (i = 0; i < 4; i++) {
        bench(out, rates[i], 1, buf);
        if (rates[i] <= 22050)
            bench(out, rates[i], 2, buf);
    }
    FreeMem(buf, BUF * sizeof(short));
    timer_close();
    if (out != stdout)
        fclose(out);
    return 0;
}
