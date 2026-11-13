/* opnprof: where does opn_lite spend a 68040's time?
 *
 * The opnbench replay (opnbench.c) of one song log at one rate/oversampling setting, with a
 * PC-sampling profiler around it: a CIA-A timer B interrupt (ciaa.resource, E clock / 709 =
 * 1000.5 Hz; the VERTB server chain at 50 Hz when that timer is taken) runs ciaprof.s, which
 * finds the interrupted exception frame on the supervisor stack and counts its PC in a
 * histogram over the program's code hunk. The core is linked from an assembly listing built
 * with -Dstatic= (see the Makefile), so its functions are named here and symprof.py can map
 * every bucket onto an instruction of that listing.
 *
 * Usage: opnprof RATE OS [FILE [REPEAT]]   (e.g. opnprof 11025 1 DH0:prof-11025.txt 3; REPEAT
 * replays the 20 s log that many times for more samples)
 * Output: the bench line, the sampler statistics, "F <hex hunk offset> <name>" for every
 * named function, "S <hex hunk offset> <count>" for every non-empty two-byte bucket, and
 * "O <hex pc>" for the last PCs seen outside the hunk. */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/io.h>
#include <exec/interrupts.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <resources/cia.h>
#include <hardware/cia.h>
#include <hardware/intbits.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>
#include <proto/cia.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "snd/opn_core.h"
#include <opnbench_log.h>

struct Device *TimerBase;
static struct MsgPort *tport;
static struct timerequest *treq;

#define CLOCK 3993600UL
#define BUF 1024

/* ---- the sampler (ciaprof.s) ---------------------------------------------------------- */

struct profdata {
    ULONG *hist;
    ULONG base, len;
    ULONG total, outside, super, nomatch;
    UWORD vecoff, pad;
    ULONG outpc[16];
    ULONG outpos;
};

extern void profsample(void);

static struct profdata prof;
static struct Interrupt profint;
static struct Library *ciares;
static int prof_mode;                   /* 0 none, 1 CIA-A TB, 2 VERTB */
#define CIAA ((volatile struct CIA *)0xBFE001)
#define CIAB ((volatile struct CIA *)0xBFD000)
static volatile struct CIA *cia;
#define TB_PERIOD 708                   /* 709 E-clock cycles -> 1000.5 Hz */

/* the program's first segment = its code hunk (vlink links all code into one hunk) */
static void code_range(ULONG *base, ULONG *len)
{
    struct Process *pr = (struct Process *)FindTask(NULL);
    struct CommandLineInterface *cli = (struct CommandLineInterface *)BADDR(pr->pr_CLI);
    ULONG *seg = (ULONG *)BADDR(cli->cli_Module);
    *base = (ULONG)(seg + 1);
    *len = seg[-1] - 8;
}

static int prof_start(void)
{
    memset(&prof, 0, sizeof prof);
    code_range(&prof.base, &prof.len);
    prof.hist = AllocMem((prof.len / 2 + 1) * sizeof(ULONG), MEMF_ANY | MEMF_CLEAR);
    if (!prof.hist)
        return 0;
    profint.is_Node.ln_Type = NT_INTERRUPT;
    profint.is_Node.ln_Pri = 0;
    profint.is_Node.ln_Name = "opnprof";
    profint.is_Data = &prof;
    profint.is_Code = profsample;

    /* CIA-A timer B first (level 2), then CIA-B timer B (level 6; timer.device owns CIA-B
     * timer A), then the VERTB server chain at 50 Hz */
    ciares = (struct Library *)OpenResource(CIAANAME);
    cia = CIAA;
    if (!(ciares && AddICRVector(ciares, CIAICRB_TB, &profint) == NULL)) {
        ciares = (struct Library *)OpenResource(CIABNAME);
        cia = CIAB;
        if (!(ciares && AddICRVector(ciares, CIAICRB_TB, &profint) == NULL))
            ciares = NULL;
    }
    if (ciares) {
        prof.vecoff = cia == CIAA ? 0x0068 : 0x0078;   /* level 2 / level 6 autovector, format 0 */
        cia->ciacrb = 0;                               /* stop, count E clock */
        cia->ciatblo = (UBYTE)(TB_PERIOD & 0xff);
        cia->ciatbhi = (UBYTE)(TB_PERIOD >> 8);
        SetICR(ciares, CIAICRF_TB);                    /* clear a stale request */
        AbleICR(ciares, CIAICRF_SETCLR | CIAICRF_TB);
        cia->ciacrb = CIACRBF_START | CIACRBF_LOAD;    /* continuous */
        prof_mode = 1;
        return 1;
    }
    prof.vecoff = 0x006C;                              /* level 3 */
    AddIntServer(INTB_VERTB, &profint);
    prof_mode = 2;
    return 1;
}

static void prof_stop(void)
{
    if (prof_mode == 1) {
        cia->ciacrb = 0;
        AbleICR(ciares, CIAICRF_TB);
        RemICRVector(ciares, CIAICRB_TB, &profint);
    } else if (prof_mode == 2) {
        RemIntServer(INTB_VERTB, &profint);
    }
    prof_mode = 0;
}

/* ---- the replay (as opnbench.c) ------------------------------------------------------- */

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
    return ev.ev_lo;
}

int main(int argc, char **argv);

static unsigned long run_log(struct opn_core *c, unsigned long rate, short *buf)
{
    unsigned long i, done = 0;
    for (i = 0; i < OPNLOG_COUNT; i++) {
        unsigned long target = (unsigned long)(((unsigned long long)opnlog[i].t * rate + 22050) / 44100);
        while (done < target) {
            int n = (int)((target - done) > BUF ? BUF : (target - done));
            opn_lite_ops.render(c, buf, n);
            done += n;
        }
        opn_lite_ops.write(c, opnlog[i].reg, opnlog[i].val);
    }
    {
        unsigned long target = (unsigned long)(((unsigned long long)OPNLOG_SAMPLES * rate + 22050) / 44100);
        while (done < target) {
            int n = (int)((target - done) > BUF ? BUF : (target - done));
            opn_lite_ops.render(c, buf, n);
            done += n;
        }
    }
    return done;
}

/* opn_lite's functions (static in the engine, external in this build) and our own */
extern void build_wave(), scale_step(), update_att(), cache_op(), cache_chan(), key_on(),
    key_off(), ssg_set_period(), ssg_tone_update(), ssg_noise_update(), ssg_env_update(),
    lite_reset(), lite_create(), lite_destroy(), lite_write(), clock_env(), chan_output(),
    chan_update_flags(), ssg_catch_up(), square_duty(), noise_duty(), lite_render(),
    step_envelopes(), hp_out(), lite_render_split(), opn_lite_eg_due(), opn_lite_ssg_output(),
    opn_lite_render_68k();

static void dump(FILE *out)
{
    struct { const char *name; void (*fn)(); } tab[] = {
        { "build_wave", build_wave }, { "scale_step", scale_step }, { "update_att", update_att },
        { "cache_op", cache_op }, { "cache_chan", cache_chan }, { "key_on", key_on },
        { "key_off", key_off }, { "ssg_set_period", ssg_set_period },
        { "ssg_tone_update", ssg_tone_update }, { "ssg_noise_update", ssg_noise_update },
        { "ssg_env_update", ssg_env_update }, { "lite_reset", lite_reset },
        { "lite_create", lite_create }, { "lite_destroy", lite_destroy },
        { "lite_write", lite_write }, { "clock_env", clock_env }, { "chan_output", chan_output },
        { "chan_update_flags", chan_update_flags }, { "ssg_catch_up", ssg_catch_up }, { "square_duty", square_duty },
        { "noise_duty", noise_duty }, { "lite_render", lite_render },
        { "step_envelopes", step_envelopes }, { "hp_out", hp_out },
        { "lite_render_split", lite_render_split }, { "eg_due", opn_lite_eg_due },
        { "ssg_output", opn_lite_ssg_output }, { "render_68k", opn_lite_render_68k },
        { "run_log", (void (*)())run_log }, { "main", (void (*)())main },
    };
    ULONG i, inside = 0;
    for (i = 0; i < sizeof tab / sizeof tab[0]; i++) {
        ULONG a = (ULONG)tab[i].fn;
        if (a - prof.base < prof.len)
            fprintf(out, "F %lx %s\n", (unsigned long)(a - prof.base), tab[i].name);
        else
            fprintf(out, "# %s at %08lx is outside the code hunk\n", tab[i].name, (unsigned long)a);
    }
    for (i = 0; i < prof.len / 2; i++)
        inside += prof.hist[i];
    fprintf(out, "# %lu samples inside the code hunk\n", (unsigned long)inside);
    for (i = 0; i < prof.len / 2; i++)
        if (prof.hist[i])
            fprintf(out, "S %lx %lu\n", (unsigned long)(i * 2), (unsigned long)prof.hist[i]);
    for (i = 0; i < 16; i++)
        if (prof.outpc[i])
            fprintf(out, "O %08lx\n", (unsigned long)prof.outpc[i]);
}

int main(int argc, char **argv)
{
    FILE *out = stdout;
    short *buf;
    struct opn_core *c;
    unsigned long want = 11025, rate = 0, samples;
    int os = 1, repeat = 1, r;
    ULONG t0, t1, dt;
    unsigned long ms_audio, ms_cpu, permille;

    if (argc > 1)
        want = strtoul(argv[1], 0, 10);
    if (argc > 2)
        os = atoi(argv[2]);
    if (argc > 3) {
        out = fopen(argv[3], "w");
        if (!out)
            out = stdout;
    }
    if (argc > 4)
        repeat = atoi(argv[4]);
    if (repeat < 1)
        repeat = 1;
    if (!timer_open()) {
        fprintf(out, "timer.device failed\n");
        return 20;
    }
    buf = AllocMem(BUF * sizeof(short), MEMF_ANY);
    opn_lite_fm_oversample = os;
    c = opn_lite_ops.create(CLOCK, want, &rate);
    if (!c || !buf) {
        fprintf(out, "create failed\n");
        return 20;
    }
    if (!prof_start()) {
        fprintf(out, "profiler setup failed\n");
        return 20;
    }
    t0 = eclock_now();
    samples = 0;
    for (r = 0; r < repeat; r++) {
        if (r)
            opn_lite_ops.reset(c);
        samples += run_log(c, rate, buf);
    }
    t1 = eclock_now();
    prof_stop();
    dt = t1 - t0;
    ms_audio = (unsigned long)(((unsigned long long)samples * 1000) / rate);
    ms_cpu = (unsigned long)(((unsigned long long)dt * 1000) / eclock_freq);
    permille = (unsigned long)(((unsigned long long)ms_cpu * 1000) / (ms_audio ? ms_audio : 1));
    fprintf(out, "opnprof: log of %d writes, %lu s x %d; rate %5lu os %d: %7lu samples = %5lu.%03lu s audio in %5lu.%03lu s cpu -> %3lu.%lu%% of the CPU\n",
            OPNLOG_COUNT, (unsigned long)(OPNLOG_SAMPLES / 44100), repeat,
            rate, os, samples, ms_audio / 1000, ms_audio % 1000, ms_cpu / 1000, ms_cpu % 1000,
            permille / 10, permille % 10);
    fprintf(out, "sampler: %s; total %lu, outside %lu, supervisor %lu, no frame %lu; code hunk %08lx + %lx\n",
            prof.vecoff == 0x68 ? "CIA-A timer B, 1000.5 Hz" : prof.vecoff == 0x78 ? "CIA-B timer B, 1000.5 Hz" : "VERTB, 50 Hz",
            (unsigned long)prof.total, (unsigned long)prof.outside, (unsigned long)prof.super,
            (unsigned long)prof.nomatch, (unsigned long)prof.base, (unsigned long)prof.len);
    dump(out);
    opn_lite_ops.destroy(c);
    FreeMem(prof.hist, (prof.len / 2 + 1) * sizeof(ULONG));
    FreeMem(buf, BUF * sizeof(short));
    timer_close();
    if (out != stdout)
        fclose(out);
    return 0;
}
