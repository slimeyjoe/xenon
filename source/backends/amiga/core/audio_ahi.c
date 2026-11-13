/* ahi.device output (device interface, CMD_WRITE double buffering).
 *
 * Optional: ahi.device is opened at run time only when AUDIO=ahi is given, so AHI is not a
 * dependency of the program. The unit (AHIUNIT, default 0) decides the mode and mixing
 * frequency through the user's AHI preferences; we hand AHI mono 16-bit blocks at the
 * engine's rate and it resamples/mixes to the device. Two AHIRequests are linked
 * (ahir_Link) as the autodoc prescribes: a request completes when the device has consumed
 * it, which is when its buffer may be refilled.
 *
 * Voice (docs/voice.md §5.3): a second pair of linked requests on the same unit, at the
 * clips' own rate; the device mixes it with the music (on the CPU inside ahi.device, or on
 * the card for a driver that mixes in hardware). A unit mixes as many such streams as its
 * "Channels" preference allows (4 when there are no preferences); the voice has the music's
 * priority, so with a single channel it is not heard rather than cutting the music (AHI puts
 * it on its silent list; it still completes in time). VOICEMIX makes the engine mix the voice
 * into the music instead, for such a unit. Volumes are ahir_Volume (AHI scales while mixing:
 * no extra cost).
 *
 * AHI's low-level API (AHI_AllocAudio, dynamic samples per channel) would let a card with
 * its own mixer take the chip's voices as separate channels; opn_lite exposes them through
 * render_split (opn_core.h). That driver is not written yet - see README.md. */
#include "os_begin.h"
#include <exec/memory.h>
#include <exec/io.h>
#include <devices/ahi.h>
#include <proto/exec.h>
#include "os_end.h"
#include "amiga.h"
#include "audio.h"
#include "amsys.h"
#include "xe_hal.h"

/* one CMD_WRITE stream: two linked requests and their buffers */
struct astream {
    struct AHIRequest *req[2];
    int16_t *buf[2];
    int frames, rate;
    int sent[2];
    int cur;                        /* request expected to complete next */
    int open;
};

static struct {
    struct MsgPort *port;
    int dev_open;
    struct astream m, v;            /* music, voice */
    ULONG pend;                     /* signals received but not yet handled (wait) */
} A;

static volatile ULONG g_vol_music = 0x10000, g_vol_voice = 0x10000;    /* Fixed, 1.0 */

static void stream_free(struct astream *s)
{
    int i;
    for (i = 0; i < 2; i++) {
        if (s->sent[i]) {
            AbortIO((struct IORequest *)s->req[i]);
            WaitIO((struct IORequest *)s->req[i]);
            s->sent[i] = 0;
        }
    }
    for (i = 0; i < 2; i++) {
        if (s->buf[i]) FreeMem(s->buf[i], sizeof(int16_t) * (ULONG)s->frames);
        s->buf[i] = 0;
    }
    /* the requests of the stream that opened the device are deleted after CloseDevice */
    if (s != &A.m)
        for (i = 0; i < 2; i++) {
            if (s->req[i]) DeleteIORequest((struct IORequest *)s->req[i]);
            s->req[i] = 0;
        }
    s->open = 0;
}

/* buffers and requests copied from the opened one (same device and unit) */
static int stream_init(struct astream *s, int rate, int frames, const struct AHIRequest *proto)
{
    int i;
    s->frames = frames;
    s->rate = rate;
    s->cur = 0;
    for (i = 0; i < 2; i++) {
        s->sent[i] = 0;
        if (!s->req[i]) {
            s->req[i] = (struct AHIRequest *)CreateIORequest(A.port, sizeof(struct AHIRequest));
            if (!s->req[i])
                return -1;
            *s->req[i] = *proto;
        }
        s->buf[i] = AllocMem(sizeof(int16_t) * (ULONG)frames, MEMF_PUBLIC | MEMF_CLEAR);
        if (!s->buf[i])
            return -1;
    }
    s->open = 1;
    return 0;
}

static void stream_submit(struct astream *s, int idx, ULONG volume)
{
    struct AHIRequest *r = s->req[idx];
    r->ahir_Std.io_Message.mn_Node.ln_Pri = 0;
    r->ahir_Std.io_Command = CMD_WRITE;
    r->ahir_Std.io_Data = s->buf[idx];
    r->ahir_Std.io_Length = (ULONG)s->frames * sizeof(int16_t);
    r->ahir_Std.io_Offset = 0;
    r->ahir_Frequency = (ULONG)s->rate;
    r->ahir_Type = AHIST_M16S;
    r->ahir_Volume = volume;
    r->ahir_Position = 0x8000;
    r->ahir_Link = s->sent[idx ^ 1] ? s->req[idx ^ 1] : NULL;
    /* AHI reads the block on the 68k (or by DMA): on WarpOS it was written on the PPC */
    am_xcpu_flush(s->buf[idx], (ULONG)s->frames * sizeof(int16_t));
    SendIO((struct IORequest *)r);
    s->sent[idx] = 1;
}

/* the next request of `s` has completed: its index, else -1 */
static int stream_done(struct astream *s)
{
    int idx = s->cur;
    if (!s->open || !s->sent[idx] || !CheckIO((struct IORequest *)s->req[idx]))
        return -1;
    WaitIO((struct IORequest *)s->req[idx]);
    am_audio_stat_ints++;
    s->sent[idx] = 0;
    s->cur = idx ^ 1;
    return idx;
}

static int ahi_open(int rate, int frames)
{
    A.pend = 0;
    A.port = CreateMsgPort();
    if (!A.port)
        goto fail;
    A.m.req[0] = (struct AHIRequest *)CreateIORequest(A.port, sizeof(struct AHIRequest));
    if (!A.m.req[0])
        goto fail;
    A.m.req[0]->ahir_Version = 4;
    if (OpenDevice(AHINAME, (ULONG)am_opts.ahi_unit, (struct IORequest *)A.m.req[0], 0) != 0)
        goto fail;
    A.dev_open = 1;
    if (stream_init(&A.m, rate, frames, A.m.req[0]) != 0)
        goto fail;
    return rate;
fail:
    stream_free(&A.m);
    if (A.dev_open)
        CloseDevice((struct IORequest *)A.m.req[0]);
    A.dev_open = 0;
    if (A.m.req[0]) DeleteIORequest((struct IORequest *)A.m.req[0]);
    if (A.m.req[1]) DeleteIORequest((struct IORequest *)A.m.req[1]);
    A.m.req[0] = A.m.req[1] = 0;
    if (A.port) DeleteMsgPort(A.port);
    A.port = 0;
    return 0;
}

static int16_t *ahi_block_buffer(int idx)
{
    return A.m.buf[idx];
}

static void ahi_submit(int idx, const int16_t *pcm, int frames)
{
    (void)pcm;
    (void)frames;
    stream_submit(&A.m, idx, g_vol_music);
}

static void ahi_start(void)
{
    /* CMD_WRITE starts playing as soon as it is queued; nothing to do */
}

static int ahi_wait(ULONG quit_mask, ULONG cmd_mask)
{
    for (;;) {
        int idx;
        if (A.pend & quit_mask)
            return -1;
        if (A.pend & cmd_mask) {
            A.pend &= ~cmd_mask;
            return AM_AUDIO_CMD;
        }
        /* completions are checked, not signalled one by one: one port signal may stand for
         * both streams' requests */
        if ((idx = stream_done(&A.m)) >= 0)
            return idx;
        if ((idx = stream_done(&A.v)) >= 0)
            return AM_VOICE_BLOCK + idx;
        A.pend |= am_wait(quit_mask | cmd_mask | (1UL << A.port->mp_SigBit));
    }
}

static void ahi_voice_close(void);

static void ahi_close(void)
{
    ahi_voice_close();
    stream_free(&A.m);
    if (A.dev_open)
        CloseDevice((struct IORequest *)A.m.req[0]);
    A.dev_open = 0;
    if (A.m.req[0]) DeleteIORequest((struct IORequest *)A.m.req[0]);
    if (A.m.req[1]) DeleteIORequest((struct IORequest *)A.m.req[1]);
    A.m.req[0] = A.m.req[1] = 0;
    if (A.port) DeleteMsgPort(A.port);
    A.port = 0;
}

static int ahi_voice_open(int rate, int frames)
{
    if (!A.dev_open)
        return 0;
    if (stream_init(&A.v, rate, frames, A.m.req[0]) != 0) {
        stream_free(&A.v);
        return 0;
    }
    return rate;                            /* AHI plays any rate: no resampling here */
}

static void ahi_voice_submit(int idx, const int16_t *pcm, int frames)
{
    int16_t *b = A.v.buf[idx];
    int i;
    if (!A.v.open)
        return;
    for (i = 0; i < frames; i++)
        b[i] = pcm[i];
    for (; i < A.v.frames; i++)
        b[i] = 0;
    stream_submit(&A.v, idx, g_vol_voice);
}

static void ahi_voice_start(void)
{
}

static void ahi_voice_close(void)
{
    stream_free(&A.v);
}

/* the device mixes from our buffers while a request plays: clearing them silences it */
static void ahi_voice_silence(void)
{
    int i, k;
    if (!A.v.open)
        return;
    for (k = 0; k < 2; k++) {
        for (i = 0; i < A.v.frames; i++)
            A.v.buf[k][i] = 0;
        am_xcpu_flush(A.v.buf[k], (ULONG)A.v.frames * sizeof(int16_t));
    }
}

static int ahi_set_volume(int which, int percent)
{
    ULONG v = (ULONG)percent * 0x10000UL / 100;
    if (which == XE_VOL_MUSIC)
        g_vol_music = v;
    else
        g_vol_voice = v;
    return 1;                               /* the next blocks carry it */
}

const struct am_audio_drv am_audio_ahi = {
    "ahi", ahi_open, ahi_close, ahi_block_buffer, ahi_submit, ahi_start, ahi_wait,
    ahi_voice_open, ahi_voice_submit, ahi_voice_start, ahi_voice_close, ahi_voice_silence,
    ahi_set_volume
};
