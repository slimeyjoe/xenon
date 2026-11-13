/* Paula output through audio.device.
 *
 * The channels are allocated from audio.device (so the system and other programs know they
 * are taken; an ADCMD_LOCK request tells us if a higher-priority user steals them), then
 * driven directly: DMA from two chip-RAM blocks, the block interrupt of the stream's first
 * channel (SetIntVector INTB_AUDn, restored on close) re-points the DMA at the other block and
 * signals the audio task to refill the one that just finished. Both stereo sides play the
 * same mono data.
 *
 *   8-bit (default): a left and a right channel share one buffer per block (the top byte of
 *     each sample).
 *   14-bit (PAULA14): all four channels; per side one channel plays the top byte at volume
 *     64 and one the next six bits at volume 1 (the classic 14-bit trick, uncalibrated).
 *
 * Voice (docs/voice.md §5.3): with 8-bit music, the other left and right channel play the
 * voice clips, a second stream with its own allocation, rate, blocks and interrupt; there is
 * no software mixing at all. Volumes are Paula's (AUDxVOL 0-64), so the 8-bit music keeps all
 * its bits when it is turned down (with PAULA14 the volumes are the 14-bit trick's: no voice
 * stream, no hardware volume).
 *
 * The CPU writes the blocks through its data cache; am_cache_flush (CacheClearE on the 68k,
 * a PPC cache flush on WarpOS) makes them visible to the DMA. The LED filter is switched off.
 *
 * The interrupt handler is 68k code in both builds (paula_irq.c); it shares its state
 * with this driver through a chip-RAM block laid out per writer (audio_paula_irq.h), so the
 * WarpOS build can run this driver on the PPC. */
#include "os_begin.h"
#include <exec/memory.h>
#include <exec/interrupts.h>
#include <exec/io.h>
#include <devices/audio.h>
#include <hardware/custom.h>
#include <hardware/cia.h>
#include <hardware/intbits.h>
#include <hardware/dmabits.h>
#include <graphics/gfxbase.h>
#include <proto/exec.h>
#include "os_end.h"
#include <stdint.h>
#include "amiga.h"
#include "audio.h"
#include "audio_paula_irq.h"
#include "amsys.h"
#include "xe_hal.h"

extern struct GfxBase *GfxBase;

#define HWCUSTOM ((volatile struct Custom *)0xdff000)
#define CIAA   ((volatile struct CIA *)0xbfe001)

#define PAL_CLOCK  3546895UL
#define NTSC_CLOCK 3579545UL
#define MIN_PERIOD 124

/* one DMA stream: the music's channels or the voice's */
struct pstream {
    ULONG sigmask;                  /* the interrupt's signal to the audio task */
    struct am_paula_irq *irq;       /* state shared with the handler (chip RAM, 32-aligned) */
    void *irq_mem;                  /* its allocation */
    int frames;
    int bits14;
    UBYTE chmask;                   /* DMA / channel bits we own */
    int hi_ch[2], lo_ch[2];         /* channel numbers: [0] left side, [1] right side */
    UBYTE *hi[2], *lo[2];           /* chip blocks (lo only for 14-bit) */
    UWORD period;
    UWORD intmask;                  /* INTF_AUDn of hi_ch[0], whose interrupt drives it */
    int intb;
    struct Interrupt intr;
    struct Interrupt *old_intr;
    int vector_set;
    struct IOAudio *alloc_req, *lock_req;
    int dev_open, lock_sent;
    int lost;                       /* the channels were stolen: the hardware is not ours */
    const char *name;
};

static struct {
    struct Task *task;              /* audio task, signalled by the interrupts */
    struct MsgPort *port;           /* the lock requests' replies */
    struct pstream m, v;            /* music, voice */
    ULONG pend;                     /* signals received but not yet handled (wait) */
    UBYTE old_pra;
    int filter_set;
} P;

/* hardware volumes (set_volume, main task), 0..64 */
static volatile UWORD g_vol_music = 64, g_vol_voice = 64;

static UWORD vol_of(const struct pstream *s)
{
    return s == &P.v ? g_vol_voice : g_vol_music;
}

static void set_block_regs(struct pstream *s, int idx)
{
    int i;
    for (i = 0; i < 2; i++) {
        HWCUSTOM->aud[s->hi_ch[i]].ac_ptr = (UWORD *)s->hi[idx];
        HWCUSTOM->aud[s->hi_ch[i]].ac_len = (UWORD)(s->frames / 2);
        HWCUSTOM->aud[s->hi_ch[i]].ac_per = s->period;
        HWCUSTOM->aud[s->hi_ch[i]].ac_vol = s->bits14 ? 64 : vol_of(s);
        if (s->bits14) {
            HWCUSTOM->aud[s->lo_ch[i]].ac_ptr = (UWORD *)s->lo[idx];
            HWCUSTOM->aud[s->lo_ch[i]].ac_len = (UWORD)(s->frames / 2);
            HWCUSTOM->aud[s->lo_ch[i]].ac_per = s->period;
            HWCUSTOM->aud[s->lo_ch[i]].ac_vol = 1;
        }
    }
}

/* channel numbers from an allocation mask: 0 and 3 are left, 1 and 2 right */
static int assign_channels(struct pstream *s, UBYTE mask)
{
    int left[2], right[2], nl = 0, nr = 0, c;
    for (c = 0; c < 4; c++) {
        if (!(mask & (1 << c)))
            continue;
        if (c == 0 || c == 3) left[nl++] = c;
        else right[nr++] = c;
    }
    if (s->bits14) {
        if (nl != 2 || nr != 2)
            return -1;
        s->hi_ch[0] = left[0]; s->lo_ch[0] = left[1];
        s->hi_ch[1] = right[0]; s->lo_ch[1] = right[1];
    } else {
        if (nl < 1 || nr < 1)
            return -1;
        s->hi_ch[0] = left[0];
        s->hi_ch[1] = right[0];
    }
    s->intb = INTB_AUD0 + s->hi_ch[0];
    s->intmask = (UWORD)(1u << s->intb);
    return 0;
}

#define IRQ_MEM_SIZE (sizeof(struct am_paula_irq) + AM_PAULA_IRQ_LINE)

static void stream_close(struct pstream *s)
{
    int i;
    if (s->vector_set) {
        HWCUSTOM->intena = s->intmask;
        if (!s->lost)
            HWCUSTOM->dmacon = s->chmask;
        SetIntVector(s->intb, s->old_intr);
        s->old_intr = 0;
        s->vector_set = 0;
    }
    if (s->irq) {
        am_cache_invalidate(s->irq, AM_PAULA_IRQ_LINE);
        am_audio_stat_ints += s->irq->ints;
    }
    if (s->lock_sent) {
        AbortIO((struct IORequest *)s->lock_req);
        WaitIO((struct IORequest *)s->lock_req);
        s->lock_sent = 0;
    }
    if (s->dev_open)
        CloseDevice((struct IORequest *)s->alloc_req);    /* frees the channels */
    s->dev_open = 0;
    if (s->lock_req) DeleteIORequest((struct IORequest *)s->lock_req);
    if (s->alloc_req) DeleteIORequest((struct IORequest *)s->alloc_req);
    s->lock_req = s->alloc_req = 0;
    for (i = 0; i < 2; i++) {
        if (s->hi[i]) FreeMem(s->hi[i], (ULONG)s->frames);
        if (s->lo[i]) FreeMem(s->lo[i], (ULONG)s->frames);
        s->hi[i] = s->lo[i] = 0;
    }
    if (s->irq_mem)
        FreeMem(s->irq_mem, IRQ_MEM_SIZE);
    s->irq_mem = 0;
    s->irq = 0;
    if (s->sigmask) {
        BYTE sig = 0;
        while (!(s->sigmask & (1UL << sig)))
            sig++;
        FreeSignal(sig);
        s->sigmask = 0;
    }
}

/* allocate the channels, blocks and shared state of a stream; returns the rate it plays */
static int stream_open(struct pstream *s, const char *name, int bits14, int rate, int frames)
{
    static UBYTE masks8[] = { 0x3, 0xC, 0x5, 0xA };    /* one left + one right channel */
    static UBYTE masks14[] = { 0xF };
    ULONG clock;
    BYTE sig;
    int i;

    s->name = name;
    s->lost = 0;
    s->frames = frames;
    s->bits14 = bits14;
    sig = AllocSignal(-1);
    if (sig == -1)
        return 0;
    s->sigmask = 1UL << sig;
    s->alloc_req = (struct IOAudio *)CreateIORequest(P.port, sizeof(struct IOAudio));
    s->lock_req = (struct IOAudio *)CreateIORequest(P.port, sizeof(struct IOAudio));
    if (!s->alloc_req || !s->lock_req)
        goto fail;
    /* allocation happens at OpenDevice time when the request carries ADCMD_ALLOCATE; each
     * stream opens the device with its own allocation (its own key) */
    s->alloc_req->ioa_Request.io_Message.mn_Node.ln_Pri = 100;
    s->alloc_req->ioa_Request.io_Command = ADCMD_ALLOCATE;
    s->alloc_req->ioa_Request.io_Flags = ADIOF_NOWAIT;
    s->alloc_req->ioa_Data = bits14 ? masks14 : masks8;
    s->alloc_req->ioa_Length = bits14 ? sizeof masks14 : sizeof masks8;
    if (OpenDevice(AUDIONAME, 0, (struct IORequest *)s->alloc_req, 0) != 0)
        goto fail;
    s->dev_open = 1;
    if (s->alloc_req->ioa_Request.io_Error != 0)
        goto fail;
    s->chmask = (UBYTE)(IPTR)s->alloc_req->ioa_Request.io_Unit;
    if (assign_channels(s, s->chmask) != 0)
        goto fail;
    /* ADCMD_LOCK returns when the channels are stolen */
    *s->lock_req = *s->alloc_req;
    s->lock_req->ioa_Request.io_Command = ADCMD_LOCK;
    s->lock_req->ioa_Request.io_Flags = 0;
    SendIO((struct IORequest *)s->lock_req);
    s->lock_sent = 1;

    for (i = 0; i < 2; i++) {
        s->hi[i] = AllocMem((ULONG)frames, MEMF_CHIP | MEMF_CLEAR);
        if (!s->hi[i])
            goto fail;
        if (bits14) {
            s->lo[i] = AllocMem((ULONG)frames, MEMF_CHIP | MEMF_CLEAR);
            if (!s->lo[i])
                goto fail;
        }
    }
    s->irq_mem = AllocMem(IRQ_MEM_SIZE, MEMF_CHIP | MEMF_CLEAR);
    if (!s->irq_mem)
        goto fail;
    s->irq = (struct am_paula_irq *)(((uintptr_t)s->irq_mem + AM_PAULA_IRQ_LINE - 1) &
                                     ~(uintptr_t)(AM_PAULA_IRQ_LINE - 1));

    clock = (GfxBase->DisplayFlags & PAL) ? PAL_CLOCK : NTSC_CLOCK;
    s->period = (UWORD)((clock + (ULONG)rate / 2) / (ULONG)rate);
    if (s->period < MIN_PERIOD)
        s->period = MIN_PERIOD;
    return (int)(clock / s->period);
fail:
    stream_close(s);
    return 0;
}

static void stream_fill(struct pstream *s, int idx, const int16_t *pcm, int frames)
{
    UBYTE *hi = s->hi[idx];
    int i;
    if (s->bits14) {
        UBYTE *lo = s->lo[idx];
        for (i = 0; i < frames; i++) {
            int v = pcm[i];
            hi[i] = (UBYTE)(v >> 8);
            lo[i] = (UBYTE)((v & 0xff) >> 2);
        }
        am_cache_flush(lo, (ULONG)s->frames);
    } else {
        for (i = 0; i < frames; i++)
            hi[i] = (UBYTE)(pcm[i] >> 8);
    }
    for (; i < s->frames; i++)          /* a short voice block: silence after it */
        hi[i] = 0;
    am_cache_flush(hi, (ULONG)s->frames);
}

static void stream_start(struct pstream *s)
{
    struct am_paula_irq *q = s->irq;
    ULONG n;
    int i;

    q->task = P.task;
    q->sigmask = s->sigmask;
    q->len = (ULONG)(s->frames / 2);
    q->bits14 = (ULONG)s->bits14;
    q->intmask = s->intmask;
    for (i = 0; i < 2; i++) {
        q->hi_ch[i] = (ULONG)s->hi_ch[i];
        q->lo_ch[i] = (ULONG)s->lo_ch[i];
        q->hi[i] = s->hi[i];
        q->lo[i] = s->lo[i];
    }
    q->cur = 0;
    q->ints = 0;

    s->intr.is_Node.ln_Type = NT_INTERRUPT;
    s->intr.is_Node.ln_Pri = 0;
    s->intr.is_Node.ln_Name = (char *)s->name;
    s->intr.is_Data = q;
    s->intr.is_Code = am_paula_irq_handler;
    am_xcpu_flush(&s->intr, sizeof s->intr);
    HWCUSTOM->intena = s->intmask;                          /* mask while we switch vectors */
    s->old_intr = SetIntVector(s->intb, &s->intr);
    s->vector_set = 1;

    /* block 0 first: start the DMA, wait until Paula has latched its registers (INTREQ
     * rises, even while the interrupt is disabled), then point the registers at block 1 and
     * enable the interrupt; the first interrupt then comes at the start of block 1. */
    set_block_regs(s, 0);
    HWCUSTOM->intreq = s->intmask;
    HWCUSTOM->dmacon = (UWORD)(DMAF_SETCLR | s->chmask);
    for (n = 0; n < 2000000UL; n++)
        if (HWCUSTOM->intreqr & s->intmask)
            break;
    if (n < 2000000UL)
        am_audio_stat_flags |= s == &P.m ? 1 : 2;
    HWCUSTOM->intreq = s->intmask;
    q->cur = 1;
    set_block_regs(s, 1);
    am_xcpu_flush(q, sizeof *q);         /* the handler reads it from now on */
    HWCUSTOM->intena = (UWORD)(INTF_SETCLR | s->intmask);
}

/* ---- the driver -------------------------------------------------------------------------- */

static int paula_open(int rate, int frames)
{
    int got;
    P.task = FindTask(NULL);
    P.pend = 0;
    P.port = CreateMsgPort();
    if (!P.port)
        return 0;
    got = stream_open(&P.m, "Xenon audio", am_opts.paula14, rate, frames);
    if (!got) {
        DeleteMsgPort(P.port);
        P.port = 0;
    }
    return got;
}

static void paula_submit(int idx, const int16_t *pcm, int frames)
{
    stream_fill(&P.m, idx, pcm, frames);
}

static void paula_start(void)
{
    P.old_pra = CIAA->ciapra;
    CIAA->ciapra = (UBYTE)(P.old_pra | CIAF_LED);        /* filter (and LED) off */
    P.filter_set = 1;
    stream_start(&P.m);
}

/* the channels of `s` were stolen: stop touching the hardware, go silent */
static int lock_lost(struct pstream *s)
{
    if (s->lock_sent && CheckIO((struct IORequest *)s->lock_req)) {
        WaitIO((struct IORequest *)s->lock_req);
        s->lock_sent = 0;
        s->lost = 1;
        HWCUSTOM->intena = s->intmask;
        return 1;
    }
    return 0;
}

static int paula_wait(ULONG quit_mask, ULONG cmd_mask)
{
    for (;;) {
        ULONG port_bit = 1UL << P.port->mp_SigBit;
        ULONG got = P.pend;
        if (!got)
            got = am_wait(P.m.sigmask | P.v.sigmask | quit_mask | cmd_mask | port_bit);
        P.pend = 0;
        if (got & quit_mask)
            return -1;
        /* the lock requests are the only ones replied to this port: its signal says when */
        if (got & port_bit) {
            if (lock_lost(&P.m)) {
                if (P.v.vector_set)
                    HWCUSTOM->intena = P.v.intmask;
                return -1;
            }
            got &= ~port_bit;
            if (lock_lost(&P.v)) {          /* only the voice's: it falls silent */
                am_audio_stat_flags |= 4;
                P.pend = got;
                return AM_VOICE_LOST;
            }
        }
        /* one event per call; the rest waits in P.pend (Wait cleared their signals) */
        if (got & cmd_mask) {
            P.pend = got & ~cmd_mask;
            return AM_AUDIO_CMD;
        }
        if (got & P.m.sigmask) {
            P.pend = got & ~P.m.sigmask;
            am_cache_invalidate(P.m.irq, AM_PAULA_IRQ_LINE);    /* the handler's line */
            return (int)P.m.irq->cur;
        }
        if (P.v.sigmask && (got & P.v.sigmask)) {
            P.pend = got & ~P.v.sigmask;
            am_cache_invalidate(P.v.irq, AM_PAULA_IRQ_LINE);
            return AM_VOICE_BLOCK + (int)P.v.irq->cur;
        }
    }
}

static void paula_voice_close(void);

static void paula_close(void)
{
    paula_voice_close();
    stream_close(&P.m);
    if (P.filter_set)
        CIAA->ciapra = (UBYTE)((CIAA->ciapra & ~CIAF_LED) | (P.old_pra & CIAF_LED));
    P.filter_set = 0;
    if (P.port)
        DeleteMsgPort(P.port);
    P.port = 0;
}

/* The voice plays at the Paula rate nearest to `rate`; the driver reports `rate` itself, so the
 * engine copies the clip's samples without resampling (161 for 22 050 Hz is 22 030 Hz on PAL:
 * 0.09 % slower, 1.6 cents, inaudible). */
static int paula_voice_open(int rate, int frames)
{
    if (P.m.bits14 || P.m.lost)
        return 0;                           /* no channels left */
    if (!stream_open(&P.v, "Xenon voice", 0, rate, frames))
        return 0;
    return rate;
}

static void paula_voice_submit(int idx, const int16_t *pcm, int frames)
{
    if (P.v.irq)
        stream_fill(&P.v, idx, pcm, frames);
}

static void paula_voice_start(void)
{
    if (P.v.irq)
        stream_start(&P.v);
}

static void paula_voice_close(void)
{
    stream_close(&P.v);
}

static void paula_voice_silence(void)
{
    int i, k;
    if (!P.v.irq)
        return;
    for (k = 0; k < 2; k++) {
        for (i = 0; i < P.v.frames; i++)
            P.v.hi[k][i] = 0;
        am_cache_flush(P.v.hi[k], (ULONG)P.v.frames);
    }
}

static int paula_set_volume(int which, int percent)
{
    UWORD v = (UWORD)((percent * 64 + 50) / 100);
    if (am_opts.paula14)
        return 0;                           /* AUDxVOL is part of the 14-bit trick */
    if (v > 64)
        v = 64;
    if (which == XE_VOL_MUSIC) {
        g_vol_music = v;
        if (P.m.vector_set) {
            HWCUSTOM->aud[P.m.hi_ch[0]].ac_vol = v;
            HWCUSTOM->aud[P.m.hi_ch[1]].ac_vol = v;
        }
    } else {
        g_vol_voice = v;
        if (P.v.vector_set) {
            HWCUSTOM->aud[P.v.hi_ch[0]].ac_vol = v;
            HWCUSTOM->aud[P.v.hi_ch[1]].ac_vol = v;
        }
    }
    return 1;
}

const struct am_audio_drv am_audio_paula = {
    "paula", paula_open, paula_close, 0, paula_submit, paula_start, paula_wait,
    paula_voice_open, paula_voice_submit, paula_voice_start, paula_voice_close,
    paula_voice_silence, paula_set_volume
};
