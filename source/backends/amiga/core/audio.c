/* Audio task and the pull-mode HAL audio entry points.
 *
 * The engine's sound calls and the renderer are serialised by a SignalSemaphore
 * (audio_lock/audio_unlock in xe_hal.h). The audio task runs at a priority above the game
 * task, so long decode loops in the game never starve the output; the block size bounds
 * the latency (AUDIOBUF, default 93 ms of frames: 1024 at 11 kHz, two blocks in flight).
 *
 * CPU budget (AUDIOCPU, default 75 % of real time): rendering a heavy song can cost more
 * than real time on a 68040/25, and a priority-5 task that never sleeps starves the game,
 * Workbench and every shell. Each rendered block adds its render time minus the budget to
 * a debt; while in debt, blocks are skipped (xe_audio_skip: silence, but the sequencer and
 * the chip registers advance, so the script's music cues stay on time). The audio task's
 * average share of the CPU therefore stays within the budget; a song that fits plays
 * unchanged, one that does not plays with gaps instead of hanging the machine.
 * It is a DOS process, not a bare exec task: the drivers open devices that may have to be
 * loaded from disk (ahi.device and its sub-drivers), which needs dos.library. On WarpOS it
 * is a PPC task (amsys.h am_task_start): the render runs on the PPC, and its OS calls run in
 * the 68k mirror process WarpOS gives it (a DOS process too).
 *
 * Voice (docs/voice.md §5.3, the HAL's XE_VOICE_CHANNEL): the engine pushes the clip's samples
 * from the main task into a FIFO here (am_voice_push); the audio task opens the driver's voice
 * output when the engine asks (a command signal, answered on the main task's reply signal),
 * then refills the two voice blocks from the FIFO as the driver reports them done, with
 * silence when it is empty. A cut (am_voice_flush) drops the FIFO and silences the blocks
 * at once. The voice blocks last as long as the music's, so the music render that holds the
 * task up never starves them. Where the driver has no voice output, or with PAULA14 or
 * VOICEMIX, the engine mixes the voice into the music instead (XE_VOICE_MIX). */
#include "os_begin.h"
#include <exec/memory.h>
#include <exec/tasks.h>
#include <exec/semaphores.h>
#include <dos/dostags.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include "os_end.h"
#include <string.h>
#include "amiga.h"
#include "audio.h"
#include "amsys.h"
#include "flavour.h"

#define TASK_PRI   5
#define TASK_NAME  "Xenon audio"
#define VFIFO      16384            /* voice FIFO, samples (power of two) */

volatile ULONG am_audio_stat_ints, am_audio_stat_flags;

static struct am_sem *g_sem;        /* the engine's audio lock (no 68k call on WarpOS) */
static struct {
    const struct am_audio_drv *drv;
    int want_rate, frames;
    volatile int rate;              /* result of drv->open, set by the task */
    volatile int running;
    struct Task *task;
    BYTE ready_sig, done_sig;       /* allocated by the main task */
    volatile BYTE quit_sig;         /* allocated by the audio task */
    int16_t *scratch;               /* render buffer for drivers without block_buffer */
    volatile ULONG blocks;          /* statistics: blocks rendered */
    LONG debt;                      /* CPU budget, profile ticks over the budget so far */
    ULONG block_e, budget_e;        /* one block's duration and its budget, profile ticks */
    ULONG busy_e, peak_e, skipped;  /* statistics: render time, the longest render, skips */

    /* voice: commands from the main task (set there, cleared by the audio task) */
    volatile BYTE cmd_sig;          /* allocated by the audio task */
    BYTE reply_sig;                 /* allocated by the main task */
    volatile int cmd_open, cmd_close;
    volatile int vresult;           /* voice_open's answer */
    int vrate, vframes;             /* the voice stream: rate asked for, frames per block */
    int vopen;                      /* audio task: the driver's voice output is open */
    int16_t *vscratch;              /* audio task: one voice block, 16-bit */
    /* the FIFO: vhead written by the main task, vtail and vinflight by the audio task */
    int16_t *vfifo;
    volatile ULONG vhead, vtail;
    volatile ULONG vinflight[2];    /* real samples in each voice block not yet played */
    volatile ULONG vflush_gen, vflush_ack, vflush_to;
    volatile int vdead;             /* the voice output is gone: everything counts as played */
    volatile ULONG vblocks;         /* statistics: voice blocks filled, of them with sound */
    volatile ULONG vblocks_sound;
} g;

void am_audio_init(void)
{
    g_sem = am_sem_create();
    if (!g_sem)
        am_log("audio: no memory for the audio lock\n");
}

void am_audio_exit(void)
{
    if (g_sem)
        am_sem_delete(g_sem);
    g_sem = 0;
}

/* without the lock (out of memory) audio stays closed, so nothing races */
void am_audio_lock(void)
{
    if (g_sem)
        am_sem_obtain(g_sem);
}

void am_audio_unlock(void)
{
    if (g_sem)
        am_sem_release(g_sem);
}

static void fill_block(int idx)
{
    int16_t *buf = g.drv->block_buffer ? g.drv->block_buffer(idx) : g.scratch;
    ULONG t0 = am_prof_now(), t;
    if (g.debt > 0) {
        xe_audio_skip(buf, g.frames);
        g.skipped++;
        t = am_prof_now() - t0;
        g.debt -= (LONG)g.budget_e - (LONG)t;
    } else {
        xe_audio_render(buf, g.frames);
        t = am_prof_now() - t0;
        if (t > g.peak_e)
            g.peak_e = t;
        g.debt += (LONG)t - (LONG)g.budget_e;
    }
    if (g.debt < 0)
        g.debt = 0;                 /* no credit: an idle stretch does not buy a burst */
    g.busy_e += t;
    am_prof_audio += t;
    g.drv->submit_block(idx, buf, g.frames);
    g.blocks++;
}

/* ---- voice, audio task side ---- */

/* a cut from the main task: the FIFO up to the cut is dropped */
static void voice_take_flush(void)
{
    if (g.vflush_gen != g.vflush_ack) {
        ULONG gen = g.vflush_gen;
        g.vtail = g.vflush_to;
        g.vinflight[0] = g.vinflight[1] = 0;
        g.vflush_ack = gen;
    }
}

static void voice_fill(int idx)
{
    ULONG avail, n, i, t;
    if (!g.vopen)
        return;
    voice_take_flush();
    avail = g.vhead - g.vtail;
    n = avail < (ULONG)g.vframes ? avail : (ULONG)g.vframes;
    t = g.vtail;
    for (i = 0; i < n; i++)
        g.vscratch[i] = g.vfifo[(t + i) & (VFIFO - 1)];
    /* counted in flight before it leaves the FIFO: voice_queued never reads it as played */
    g.vinflight[idx] = n;
    g.vtail = t + n;
    g.drv->voice_submit(idx, g.vscratch, (int)n);
    g.vblocks++;
    if (n)
        g.vblocks_sound++;
}

static void voice_free_task_side(void)
{
    if (g.vscratch)
        FreeMem(g.vscratch, sizeof(int16_t) * (ULONG)g.vframes);
    g.vscratch = 0;
    g.vopen = 0;
}

static void voice_cmd(void)
{
    if (g.cmd_open) {
        int r = 0;
        /* blocks as long as the music's, so a music render never outlasts a voice block */
        g.vframes = (int)(((LONG)g.frames * (LONG)g.vrate / (g.rate ? g.rate : 1)) & ~1L);
        if (g.vframes < 256)
            g.vframes = 256;
        g.vscratch = AllocMem(sizeof(int16_t) * (ULONG)g.vframes, MEMF_PUBLIC | MEMF_CLEAR);
        if (g.vscratch && g.drv->voice_open)
            r = g.drv->voice_open(g.vrate, g.vframes);
        if (r > 0) {
            g.vopen = 1;
            voice_fill(0);
            voice_fill(1);
            g.drv->voice_start();
        } else {
            voice_free_task_side();
        }
        g.vresult = r;
        g.cmd_open = 0;
        Signal(am_main_task, 1UL << g.reply_sig);
    }
    if (g.cmd_close) {
        if (g.vopen)
            g.drv->voice_close();
        voice_free_task_side();
        g.cmd_close = 0;
        Signal(am_main_task, 1UL << g.reply_sig);
    }
    if (g.vflush_gen != g.vflush_ack) {    /* a cut: silence now, not after the blocks */
        voice_take_flush();
        if (g.vopen && g.drv->voice_silence)
            g.drv->voice_silence();
    }
}

/* The audio task. Signals the main task once the device is open (ready) and once more when
 * it has closed everything and is about to end (done). */
struct am_stack am_audio_stack;

static void audio_task(void)
{
    ULONG quit_mask;
    /* the task as the OS sees it: on WarpOS the 68k mirror, which the Paula interrupt and
     * the main task's Signal() use; the drivers' am_wait() receives those signals */
    g.task = FindTask(NULL);
    am_stack_paint(&am_audio_stack);
    g.quit_sig = AllocSignal(-1);
    if (g.quit_sig == -1) {
        g.rate = 0;
        am_task_end_forbid();
        Signal(am_main_task, (1UL << g.ready_sig) | (1UL << g.done_sig));
        return;
    }
    quit_mask = 1UL << g.quit_sig;
    g.rate = g.drv->open(g.want_rate, g.frames);
    if (g.rate <= 0) {
        g.rate = 0;
        FreeSignal(g.quit_sig);
        am_task_end_forbid();
        Signal(am_main_task, (1UL << g.ready_sig) | (1UL << g.done_sig));
        return;
    }
    g.cmd_sig = AllocSignal(-1);
    g.running = 1;
    Signal(am_main_task, 1UL << g.ready_sig);
    fill_block(0);
    fill_block(1);
    g.drv->start();
    for (;;) {
        int r = g.drv->wait_block(quit_mask, g.cmd_sig != -1 ? 1UL << g.cmd_sig : 0);
        if (r < 0)
            break;
        if (r == AM_AUDIO_CMD)
            voice_cmd();
        else if (r == AM_VOICE_LOST)
            g.vdead = 1;
        else if (r >= AM_VOICE_BLOCK)
            voice_fill(r - AM_VOICE_BLOCK);
        else
            fill_block(r);
    }
    g.drv->close();                 /* the voice output too */
    voice_free_task_side();
    if (g.cmd_sig != -1)
        FreeSignal(g.cmd_sig);
    g.cmd_sig = -1;
    FreeSignal(g.quit_sig);
    g.running = 0;
    am_task_end_forbid();
    Signal(am_main_task, 1UL << g.done_sig);
    /* returning ends the task (68k: dos frees the process, Forbid holds until then; WarpOS:
     * the main task waits until the task is gone, free_task) */
}

static int start_task(void)
{
    g.task = 0;                     /* set by the task itself */
    return am_task_start(audio_task, TASK_NAME, TASK_PRI, am_flavour.audio_stack);
}

/* after the task's done signal */
static void free_task(void)
{
    if (am_task_wait_gone(TASK_NAME, 100) != 0)
        am_log("audio: the task did not end within 2 s\n");
    g.task = 0;
}

/* AUDIO=name from the flavour's drivers (flavour.h), by default its first; none for
 * AUDIO=none or a driver this flavour does not have */
static const struct am_audio_drv *pick_driver(void)
{
    const struct am_audio_drv *const *d = am_flavour.audio;
    const char *a = am_opts.audio;
    if (!a)
        return d[0];
    for (; *d; d++)
        if (am_strieq(a, (*d)->name))
            return *d;
    if (!am_strieq(a, "none"))
        am_log("audio: no driver \"%s\" in this build\n", a);
    return 0;
}

int am_audio_open(int rate)
{
    const struct am_audio_drv *drv = pick_driver();
    if (!drv || rate <= 0 || !g_sem)
        return 0;
    g.drv = drv;
    g.want_rate = rate;
    g.frames = am_opts.audiobuf > 0 ? (int)am_opts.audiobuf : (int)((LONG)rate * 2048 / 22050);
    g.frames &= ~1;                 /* Paula wants whole words */
    if (g.frames < 256)
        g.frames = 256;
    g.scratch = AllocMem(sizeof(int16_t) * (ULONG)g.frames, MEMF_PUBLIC);
    g.ready_sig = AllocSignal(-1);
    g.done_sig = AllocSignal(-1);
    g.reply_sig = AllocSignal(-1);
    g.cmd_sig = -1;
    if (!g.scratch || g.ready_sig == -1 || g.done_sig == -1 || g.reply_sig == -1)
        goto fail;
    /* the budget from the asked-for rate: the task renders before the real one is known
     * (Paula rounds to a period: 11 025 -> 11 015 Hz) */
    if (am_opts.audio_cpu < 10) am_opts.audio_cpu = 10;
    if (am_opts.audio_cpu > 100) am_opts.audio_cpu = 100;
    g.block_e = (ULONG)((unsigned long long)g.frames * am_prof_freq() / (ULONG)rate);
    /* multiplied first: with a coarse profile clock (200 Hz on hosted AROS) a block is only a
     * few dozen ticks, and dividing first made the budget 0 */
    g.budget_e = (ULONG)((unsigned long long)g.block_e * (ULONG)am_opts.audio_cpu / 100);
    g.debt = 0;
    g.busy_e = g.peak_e = g.skipped = 0;
    if (start_task() != 0)
        goto fail;
    /* Ctrl-C lets the user out should the device open hang (a driver waiting for
     * hardware); the process is then left to finish on its own */
    if (!(Wait((1UL << g.ready_sig) | SIGBREAKF_CTRL_C) & (1UL << g.ready_sig))) {
        am_log("audio: interrupted while opening %s\n", drv->name);
        return 0;
    }
    if (g.rate == 0) {
        Wait(1UL << g.done_sig);
        free_task();
        goto fail;
    }
    am_log("audio: %s, %ld Hz, %ld-frame blocks\n", drv->name, (long)g.rate, (long)g.frames);
    return g.rate;
fail:
    if (g.scratch) FreeMem(g.scratch, sizeof(int16_t) * (ULONG)g.frames);
    if (g.ready_sig != -1) FreeSignal(g.ready_sig);
    if (g.done_sig != -1) FreeSignal(g.done_sig);
    if (g.reply_sig != -1) FreeSignal(g.reply_sig);
    g.scratch = 0;
    g.ready_sig = g.done_sig = g.reply_sig = -1;
    am_log("audio: %s unavailable, running silent\n", drv->name);
    return 0;
}

void am_audio_close(void)
{
    if (g.task && g.running) {
        Signal(g.task, 1UL << g.quit_sig);
        Wait(1UL << g.done_sig);
    }
    free_task();
    am_log("audio: %lu blocks rendered, %lu device interrupts, flags %lx\n", (unsigned long)g.blocks,
           (unsigned long)am_audio_stat_ints, (unsigned long)am_audio_stat_flags);
    if (g.vblocks)
        am_log("audio: voice %ld Hz, %ld-frame blocks: %lu filled, %lu with sound\n", (long)g.vrate,
               (long)g.vframes, (unsigned long)g.vblocks, (unsigned long)g.vblocks_sound);
    if (g.blocks && g.block_e)
        am_log("audio: render load %lu%% of real time on average, peak %lu%%; %lu blocks skipped "
               "over the %ld%% CPU budget\n",
               (unsigned long)((unsigned long long)g.busy_e * 100 / ((unsigned long long)g.blocks * g.block_e)),
               (unsigned long)((unsigned long long)g.peak_e * 100 / g.block_e),
               (unsigned long)g.skipped, (long)am_opts.audio_cpu);
    if (g.scratch) FreeMem(g.scratch, sizeof(int16_t) * (ULONG)g.frames);
    if (g.ready_sig != -1) FreeSignal(g.ready_sig);
    if (g.done_sig != -1) FreeSignal(g.done_sig);
    if (g.reply_sig != -1) FreeSignal(g.reply_sig);
    g.scratch = 0;
    g.ready_sig = g.done_sig = g.reply_sig = -1;
    if (g.vfifo)
        FreeMem(g.vfifo, sizeof(int16_t) * VFIFO);
    g.vfifo = 0;
}

/* ---- voice, main task side (the HAL's voice_*) ---- */

/* XE_VOICE_CHANNEL when the driver plays a voice stream itself, XE_VOICE_MIX when the engine
 * must mix it into the music, XE_VOICE_NONE without audio */
int am_audio_voice_mode(void)
{
    const struct am_audio_drv *drv = pick_driver();
    if (!drv || am_opts.novoice || am_opts.rate <= 0)
        return XE_VOICE_NONE;
    if (!drv->voice_open || am_opts.paula14 || am_opts.voicemix)
        return XE_VOICE_MIX;
    return XE_VOICE_CHANNEL;
}

/* in MIX mode the voice is in the music stream: no hardware volume for either */
int am_audio_set_volume(int which, int percent)
{
    const struct am_audio_drv *drv = pick_driver();
    if (!drv || !drv->set_volume || am_audio_voice_mode() == XE_VOICE_MIX)
        return 0;
    return drv->set_volume(which, percent);
}

/* ask the audio task, wait for its answer */
static void voice_call(volatile int *flag)
{
    *flag = 1;
    Signal(g.task, 1UL << g.cmd_sig);
    Wait(1UL << g.reply_sig);
}

int am_voice_open(int rate)
{
    if (!g.running || !g.task || g.cmd_sig == -1 || rate <= 0 || !g.drv->voice_open)
        return 0;
    if (!g.vfifo)
        g.vfifo = AllocMem(sizeof(int16_t) * VFIFO, MEMF_PUBLIC | MEMF_CLEAR);
    if (!g.vfifo)
        return 0;
    g.vhead = g.vtail = 0;
    g.vinflight[0] = g.vinflight[1] = 0;
    g.vflush_gen = g.vflush_ack = g.vflush_to = 0;
    g.vdead = 0;
    g.vrate = rate;
    voice_call(&g.cmd_open);
    if (g.vresult <= 0)
        return 0;
    am_log("audio: voice through %s, %ld Hz\n", g.drv->name, (long)g.vresult);
    return g.vresult;
}

void am_voice_close(void)
{
    if (g.running && g.task && g.cmd_sig != -1 && g.vrate)
        voice_call(&g.cmd_close);
}

/* no output any more: the audio task has ended (the music's channels were taken) or the voice's
 * were; everything counts as played, so the engine never waits for it */
static int voice_gone(void)
{
    return g.vdead || !g.running;
}

int am_voice_space(void)
{
    if (voice_gone())
        return VFIFO;
    return (int)(VFIFO - (g.vhead - g.vtail));
}

void am_voice_push(const int16_t *pcm, int frames)
{
    ULONG h = g.vhead;
    int i;
    if (frames <= 0 || voice_gone() || !g.vfifo)
        return;                     /* frames == 0: the clip's end; nothing is held back here */
    if ((ULONG)frames > VFIFO - (h - g.vtail))
        frames = (int)(VFIFO - (h - g.vtail));
    for (i = 0; i < frames; i++)
        g.vfifo[(h + (ULONG)i) & (VFIFO - 1)] = pcm[i];
    g.vhead = h + (ULONG)frames;
}

int am_voice_queued(void)
{
    ULONG tail, n;
    if (voice_gone())
        return 0;
    if (g.vflush_gen != g.vflush_ack)       /* a cut the audio task has not taken yet */
        return (int)(g.vhead - g.vflush_to);
    tail = g.vtail;                         /* before the blocks: see voice_fill */
    n = g.vhead - tail;
    return (int)(n + g.vinflight[0] + g.vinflight[1]);
}

void am_voice_flush(void)
{
    g.vflush_to = g.vhead;
    g.vflush_gen++;
    if (g.task && g.cmd_sig != -1)
        Signal(g.task, 1UL << g.cmd_sig);   /* silence now; the answer is not waited for */
}
