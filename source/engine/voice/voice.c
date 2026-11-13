/* Voice playback (docs/voice.md §4).
 *
 * A message's cue (main pack, XPAK_T_VOICECUE of the script: FD offset, part, clip name) names
 * a clip of the voice pack. The clip is decoded on the engine's thread, a little ahead, into a
 * ring of samples at the clip's rate; the output takes it from there:
 *   XE_VOICE_MIX      voice_mix(), called by the sound renderer under the audio lock (in pull
 *                     mode on the backend's audio thread), resamples and adds it to the music;
 *   XE_VOICE_CHANNEL  voice_pump() resamples it to the rate the backend accepted and pushes it.
 * Pack reads therefore never happen on an audio thread. In MIX mode every access to the ring
 * and the play state takes the audio lock (snd_lock); in CHANNEL mode there is one thread.
 *
 * Behaviour (Windows release, docs/voice.md §4.1, plus OK cutting a clip): the part-0 clip
 * starts with the message; before each key prompt (in-message {KEY} and end) and before a timed
 * wait the printer waits for the clip to end; CANCEL (and OK unless voice_strict) cuts it, CTRL
 * held cuts it and starts no clip. */
#include <string.h>
#include "xe_internal.h"
#include "sys.h"
#include "snd/snd.h"
#include "voice/vdec.h"
#include "voice/voice.h"

#define RING        8192            /* samples at the clip rate (~0.34 s at 24000 Hz) */
#define LEAD        4096            /* decode this far ahead (~0.19 s) */
#define CHUNK       512
#define CUE_REC     20

static struct {
    int on;                         /* voice pack open and the backend has an output */
    int mode;                       /* XE_VOICE_MIX / XE_VOICE_CHANNEL */
    struct xpak pak;
    int gain;                       /* 0..256 */
    uint32_t ch_rate;               /* CHANNEL: the rate the backend accepted */

    uint8_t *cues;                  /* VOICECUE entry of the current script */
    uint32_t cues_size;
    uint32_t ncues;
    int msg_valid;
    uint32_t msg_off;               /* FD offset of the current message */

    int playing;                    /* a clip is decoding or still being played out */
    int dec_done;
    int end_sent;                   /* CHANNEL: the end-of-clip push has been made */
    struct vdec dec;
    char clip[13];
    int part;

    int16_t *ring;                  /* RING samples, allocated when voice is on */
    uint32_t rhead, rtail;          /* free-running counters: rhead - rtail = samples queued */
    /* resampler (the one consumer of the ring) */
    uint32_t step;                  /* clip rate / output rate, 16.16 */
    uint32_t step_rate;             /* output rate the step was computed for */
    uint32_t frac;
    int16_t s0, s1;
    int primed;
    unsigned long n_ev[3];          /* statistics: clips started, cut, ended (XE_VOICE_EV_*) */
} v;

static void lock(void)   { if (v.mode == XE_VOICE_MIX) snd_lock(); }
static void unlock(void) { if (v.mode == XE_VOICE_MIX) snd_unlock(); }

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* value of `key=` in a META text entry, or "" */
static void meta_get(const struct xpak *pk, const char *entry, const char *key, char *out, int cap)
{
    const struct xpak_entry *e = xpak_find(pk, entry, XPAK_T_META);
    uint8_t *d;
    uint32_t n, i, kl = (uint32_t)strlen(key);
    out[0] = 0;
    if (!e || !(d = xpak_load(pk, e, &n)))
        return;
    for (i = 0; i + kl < n; i++) {
        if ((i == 0 || d[i - 1] == '\n') && !memcmp(d + i, key, kl) && d[i + kl] == '=') {
            /* a separate index: vbcc 0.9h -O2 lost an "i += ..." of the loop variable here
             * and copied the whole line (the Amiga build read codec "codec=ldp6") */
            uint32_t j = i + kl + 1;
            int k = 0;
            while (j < n && d[j] != '\n' && k < cap - 1)
                out[k++] = (char)d[j++];
            out[k] = 0;
            break;
        }
    }
    xpak_free(pk, d, n);
}

void voice_init(void)
{
    const struct xe_hal *hal = xe.hal;
    char codec[16], cues[40], want[40], rate_s[12];
    int vol = xe.cfg.voice_volume;
    int rate = 0, i;

    memset(&v, 0, sizeof v);
    if (hal->voice_mode != XE_VOICE_MIX && hal->voice_mode != XE_VOICE_CHANNEL)
        return;
    if (xpak_open(&v.pak, hal, XE_PACK_VOICE) != 0) {
        sys_log(XE_LOG_INFO, "voice: no voice pack");
        return;
    }
    meta_get(&v.pak, "voice", "codec", codec, sizeof codec);
    meta_get(&v.pak, "voice", "rate", rate_s, sizeof rate_s);
    for (i = 0; rate_s[i] >= '0' && rate_s[i] <= '9' && rate < 1000000; i++)
        rate = rate * 10 + (rate_s[i] - '0');
    if (rate <= 0)
        rate = 22050;
    if (!vdec_supports(codec)) {
        sys_log(XE_LOG_WARN, "voice: the voice pack's codec \"%s\" is not supported by this build",
                codec);
        xpak_close(&v.pak);
        return;
    }
    meta_get(&v.pak, "voice", "cues", cues, sizeof cues);
    meta_get(&xe.pak, "meta", "voicemap", want, sizeof want);
    if (!want[0])
        sys_log(XE_LOG_WARN, "voice: the asset pack has no voice cues");
    else if (strcmp(cues, want) != 0)
        sys_log(XE_LOG_WARN, "voice: the voice pack was built from a different cue table "
                "than the asset pack (rebuild one of them)");
    v.mode = hal->voice_mode;
    if (v.mode == XE_VOICE_CHANNEL) {
        int got = hal->voice_open ? hal->voice_open(rate) : 0;
        if (got <= 0) {
            sys_log(XE_LOG_WARN, "voice: the backend could not open its voice output");
            xpak_close(&v.pak);
            v.mode = 0;
            return;
        }
        v.ch_rate = (uint32_t)got;
    }
    if (vol <= 0 || vol > 100)
        vol = 100;
    v.ring = sys_alloc(RING * sizeof v.ring[0]);
    v.gain = vol * 256 / 100;
    if (hal->set_volume && hal->set_volume(XE_VOL_VOICE, vol))
        v.gain = 256;               /* the backend's output applies it */
    v.on = 1;
    snd_update_gain();
    sys_log(XE_LOG_INFO, "voice: %u entries, %s %d Hz, %s, volume %d%%, music %d%%",
            (unsigned)v.pak.count, codec, rate,
            v.mode == XE_VOICE_MIX ? "mixed with the music" : "own channel", vol,
            xe.cfg.music_volume > 0 && xe.cfg.music_volume <= 100 ? xe.cfg.music_volume
                                                                  : VOICE_MUSIC_VOLUME);
}

int voice_enabled(void)
{
    return v.on;
}

/* ---- cues ------------------------------------------------------------------------------------ */

void voice_script_unload(void)
{
    if (v.cues)
        xpak_free(&xe.pak, v.cues, v.cues_size);
    v.cues = 0;
    v.cues_size = 0;
    v.ncues = 0;
    v.msg_valid = 0;
}

void voice_script(const char *name)
{
    const struct xpak_entry *e;
    voice_script_unload();
    if (!v.on)
        return;
    e = xpak_find(&xe.pak, name, XPAK_T_VOICECUE);
    if (!e)
        return;
    v.cues = xpak_load(&xe.pak, e, &v.cues_size);
    if (!v.cues)
        return;
    if (v.cues_size < 8 || memcmp(v.cues, "VCUE", 4) != 0 || v.cues[4] != 1 || v.cues[5] != 0 ||
        8u + (uint32_t)(v.cues[6] | (v.cues[7] << 8)) * CUE_REC > v.cues_size) {
        sys_log(XE_LOG_WARN, "voice: bad cue table for %s", name);
        voice_script_unload();
        return;
    }
    v.ncues = (uint32_t)(v.cues[6] | (v.cues[7] << 8));
}

/* clip name of cue (off, part) or NULL; records are sorted by (offset, part) */
static const char *find_cue(uint32_t off, int part)
{
    uint32_t lo = 0, hi = v.ncues;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        const uint8_t *r = v.cues + 8 + mid * CUE_REC;
        uint32_t o = rd32(r);
        if (o < off || (o == off && r[4] < part))
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo < v.ncues) {
        const uint8_t *r = v.cues + 8 + lo * CUE_REC;
        if (rd32(r) == off && r[4] == part)
            return (const char *)r + 8;
    }
    return 0;
}

/* ---- playback -------------------------------------------------------------------------------- */

static uint32_t queued(void)
{
    return v.rhead - v.rtail;
}

/* decode / output scratch, static: the engine's task only, and the Amiga's main task runs on
 * the shell's 4 KB stack (backends/amiga/README.md "Voice") */
static int16_t g_tmp[CHUNK];

/* decode up to `want` samples into the ring (engine thread) */
static void fill(uint32_t want)
{
    int16_t *tmp = g_tmp;
    while (!v.dec_done && want > 0) {
        int n, i;
        uint32_t room;
        lock();
        room = RING - queued();
        unlock();
        if (room == 0)
            break;
        n = (int)(want < CHUNK ? want : CHUNK);
        if ((uint32_t)n > room)
            n = (int)room;
        n = vdec_read(&v.dec, tmp, n);
        if (n <= 0) {
            v.dec_done = 1;
            break;
        }
        if (v.gain != 256)
            for (i = 0; i < n; i++)
                tmp[i] = (int16_t)(((int32_t)tmp[i] * v.gain) >> 8);
        lock();
        for (i = 0; i < n; i++)
            v.ring[(v.rhead + (uint32_t)i) & (RING - 1)] = tmp[i];
        v.rhead += (uint32_t)n;
        unlock();
        want -= (uint32_t)n;
    }
}

static void trace_ev(int ev)
{
    if (ev >= 0 && ev < 3)
        v.n_ev[ev]++;
    sys_trace(XE_TR_VOICE, v.clip, 0, v.part, ev, 0, 0);
}

/* the clip is over or cut: back to idle (the lock is taken here) */
static void halt(int ev)
{
    int was = v.playing;
    lock();
    v.playing = 0;
    v.rhead = v.rtail = 0;
    v.primed = 0;
    unlock();
    if (v.mode == XE_VOICE_CHANNEL && xe.hal->voice_flush && ev == XE_VOICE_EV_CUT)
        xe.hal->voice_flush();
    if (was)
        trace_ev(ev);
}

void voice_stop(void)
{
    if (v.on && v.playing)
        halt(XE_VOICE_EV_CUT);
}

int voice_playing(void)
{
    return v.on && v.playing;
}

static void start(const char *clip, int part)
{
    const struct xpak_entry *e;
    voice_stop();
    e = xpak_find(&v.pak, clip, XPAK_T_VOICE);
    if (!e) {
        sys_log(XE_LOG_WARN, "voice: clip %s missing from the voice pack", clip);
        return;
    }
    if (vdec_open(&v.dec, &v.pak, e) != 0) {
        sys_log(XE_LOG_WARN, "voice: clip %s: unknown format", clip);
        return;
    }
    strncpy(v.clip, clip, sizeof v.clip - 1);
    v.clip[sizeof v.clip - 1] = 0;
    v.part = part;
    v.dec_done = 0;
    v.end_sent = 0;
    lock();
    v.rhead = v.rtail = 0;
    v.primed = 0;
    v.step_rate = 0;
    unlock();
    fill(LEAD);                     /* audible from the first render on */
    lock();
    v.playing = 1;
    unlock();
    trace_ev(XE_VOICE_EV_START);
}

void voice_message(const uint8_t *src, int timed)
{
    const char *clip;
    if (!v.on)
        return;
    voice_stop();
    v.msg_valid = 0;
    if (!v.ncues || !xe.script_buf || src < xe.script_buf + 2 || src >= xe.script_buf + xe.script_size)
        return;
    v.msg_off = (uint32_t)(src - xe.script_buf) - 2;     /* host_message gets buf + pos + 2 */
    v.msg_valid = 1;
    (void)timed;                    /* Windows starts a timed message's clip even when skipping;
                                     * voice_wait would cut it at once, so none is started */
    if (sys_ctrl_held())
        return;                     /* skipping: the text appears at once, no voice */
    clip = find_cue(v.msg_off, 0);
    if (clip)
        start(clip, 0);
}

void voice_part(int k, int skipped)
{
    const char *clip;
    if (!v.on || !v.msg_valid)
        return;
    if (skipped) {
        voice_stop();
        v.msg_valid = 0;            /* no further clip of this message */
        return;
    }
    clip = find_cue(v.msg_off, k);
    if (clip)
        start(clip, k);
}

/* an output that plays nothing (no audio DMA in an emulator, a stuck driver) must not hold
 * the game: no progress for this long while waiting ends the clip */
#define STALL_TICKS 113             /* 2 s */

/* how far the output has got: samples taken from the ring, minus what the backend still holds */
static uint32_t out_progress(void)
{
    uint32_t p = v.rtail;
    if (v.mode == XE_VOICE_CHANNEL && xe.hal->voice_queued)
        p -= (uint32_t)xe.hal->voice_queued();
    return p;
}

void voice_wait(void)
{
    int why = XE_LEAVE_TIMEOUT, still = 0;
    uint32_t last;
    if (!voice_playing())
        return;
    sys_trace(XE_TR_WAIT, 0, 0, XE_WAIT_VOICE, 0, 0, 0);
    last = out_progress();
    for (;;) {
        int k;
        uint32_t now;
        if (!voice_playing())
            break;                  /* the clip ended */
        now = out_progress();
        if (now != last) {
            last = now;
            still = 0;
        } else if (++still > STALL_TICKS) {
            sys_log(XE_LOG_WARN, "voice: %s made no progress for 2 s, ended", v.clip);
            voice_stop();
            break;
        }
        if (sys_quitting()) {
            voice_stop();
            why = XE_LEAVE_QUIT;
            break;
        }
        if (sys_ctrl_held()) {
            voice_stop();
            why = XE_LEAVE_CTRL;
            break;
        }
        k = sys_poll_cmd();
        if (k == SYS_CMD_CANCEL || (k == SYS_CMD_OK && !xe.cfg.voice_strict)) {
            voice_stop();
            why = XE_LEAVE_KEY;
            break;
        }
        sys_frame_wait();
    }
    sys_trace(XE_TR_WAIT, 0, 0, XE_WAIT_VOICE, 1, why, 0);
}

/* the clip is decoded and the resampler has output all of it, its last step faded to zero
 * (the caller holds the lock in MIX mode) */
static int drained(void)
{
    return v.dec_done && queued() == 0 && (!v.primed || (v.s0 == 0 && v.s1 == 0));
}

/* next output sample through the resampler (the caller holds the lock in MIX mode) */
static int next_out(uint32_t rate, int16_t *out)
{
    if (v.step_rate != rate) {
        v.step = (uint32_t)(((uint64_t)v.dec.rate << 16) / (rate ? rate : 1));
        v.step_rate = rate;
    }
    if (v.primed && drained())
        return 0;                   /* the clip is over (no endless silence) */
    if (v.step == 0x10000u) {       /* the output plays the clip's rate: copy, no interpolation */
        if (queued() == 0)
            return 0;               /* drained, or an underrun (the caller stops either way) */
        *out = v.ring[v.rtail++ & (RING - 1)];
        return 1;
    }
    if (!v.primed) {
        if (queued() < 2) {
            if (v.dec_done)
                v.rtail = v.rhead;  /* a last lone sample: drop it, the clip is over */
            return 0;
        }
        v.s0 = v.ring[v.rtail++ & (RING - 1)];
        v.s1 = v.ring[v.rtail++ & (RING - 1)];
        v.frac = 0;
        v.primed = 1;
    }
    *out = (int16_t)(v.s0 + (int16_t)(((int32_t)(v.s1 - v.s0) * (int32_t)(v.frac >> 1)) >> 15));
    v.frac += v.step;
    while (v.frac >= 0x10000u) {
        v.frac -= 0x10000u;
        v.s0 = v.s1;
        if (queued() > 0)
            v.s1 = v.ring[v.rtail++ & (RING - 1)];
        else if (v.dec_done)
            v.s1 = 0;               /* the clip has run out: fade the last step to zero */
        else
            break;                  /* underrun: hold */
    }
    return 1;
}

void voice_mix(int16_t *pcm, int frames, uint32_t rate)
{
    int i;
    if (!v.on || v.mode != XE_VOICE_MIX || !v.playing)
        return;
    for (i = 0; i < frames; i++) {
        int16_t s;
        int32_t m;
        if (!next_out(rate, &s))
            break;
        m = (int32_t)pcm[i] + s;
        pcm[i] = (int16_t)(m > 32767 ? 32767 : m < -32768 ? -32768 : m);
    }
}

/* 1 if the end-of-clip push was made already; marks it as made */
static int end_sent_and_mark(void)
{
    int was = v.end_sent;
    v.end_sent = 1;
    return was;
}

void voice_pump(void)
{
    int over;
    if (!v.on || !v.playing)
        return;
    lock();
    over = (int)(LEAD - (queued() < LEAD ? queued() : LEAD));
    unlock();
    fill((uint32_t)over);
    if (v.mode == XE_VOICE_CHANNEL) {
        int16_t *tmp = g_tmp;           /* fill() is done with it */
        uint32_t lead_out = (uint32_t)(((uint64_t)LEAD * v.ch_rate) / (v.dec.rate ? v.dec.rate : 1));
        if (xe.hal->voice_lead_ms > 0) {
            uint32_t want = (uint32_t)(((uint64_t)v.ch_rate * (uint32_t)xe.hal->voice_lead_ms) / 1000);
            if (want > lead_out)
                lead_out = want;
        }
        while ((uint32_t)xe.hal->voice_queued() < lead_out) {
            int n = 0, space = xe.hal->voice_space();
            if (space <= 0)
                break;
            if (space > CHUNK)
                space = CHUNK;
            while (n < space && next_out(v.ch_rate, &tmp[n]))
                n++;
            if (n == 0)
                break;
            xe.hal->voice_push(tmp, n);
        }
        /* the end, once nothing more will be pushed: the backend plays out what it holds
         * back (SDL keeps a resampler's last frames until a flush). Pushing after it would
         * leave those frames held for good, and the clip would never end. */
        if (drained() && !end_sent_and_mark())
            xe.hal->voice_push(tmp, 0);
        if (v.end_sent && xe.hal->voice_queued() == 0)
            halt(XE_VOICE_EV_END);
        return;
    }
    lock();
    over = drained();
    unlock();
    if (over)
        halt(XE_VOICE_EV_END);
}

void voice_shutdown(void)
{
    if (!v.on)
        return;
    voice_stop();
    sys_log(XE_LOG_INFO, "voice: %lu clips started, %lu played to the end, %lu cut",
            v.n_ev[XE_VOICE_EV_START], v.n_ev[XE_VOICE_EV_END], v.n_ev[XE_VOICE_EV_CUT]);
    voice_script_unload();
    vdec_close(&v.dec);
    if (v.mode == XE_VOICE_CHANNEL && xe.hal->voice_close)
        xe.hal->voice_close();
    xpak_close(&v.pak);
    sys_free(v.ring, RING * sizeof v.ring[0]);
    v.ring = 0;
    v.on = 0;
}
