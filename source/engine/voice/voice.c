/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Voice (docs/voice.md §4): the cue table and when a clip plays.
 *
 * A message's cue (main pack, LRV_PT_VOICECUE entry of the script: FD offset, part, clip name)
 * names a clip of the voice pack, which lrvne's player (xe.voice) decodes and outputs, mixed
 * into the music or on a channel of the backend's own.
 *
 * Behaviour (Windows release, docs/voice.md §4.1, plus OK cutting a clip): the part-0 clip
 * starts with the message; before each key prompt (in-message {KEY} and end) and before a timed
 * wait the printer waits for the clip to end; CANCEL (and OK unless --voice-strict) cuts it,
 * CTRL held cuts it and starts no clip. */
#include <string.h>
#include "xe.h"
#include "lrvne/endian.h"
#include "snd/snd.h"
#include "voice/voice.h"

#define CUE_REC     20
#define CLIP_RATE   22050           /* a voice pack that names no rate */
/* an output that plays nothing (no audio DMA in an emulator, a stuck driver) must not hold
 * the game: no progress for this long while waiting ends the clip */
#define STALL_TICKS 113             /* 2 s */

static struct {
    uint8_t *cues;                  /* VOICECUE entry of the current script */
    uint32_t cues_size;
    uint32_t ncues;
    int msg_valid;
    uint32_t msg_off;               /* FD offset of the current message */
} v;

/* the player's events: the trace (XE_TR_VOICE, a0 = the cue's part) */
static void on_event(void *ctx, int ev, const char *clip, int part)
{
    (void)ctx;
    lrv_sys_trace(&xe.sys, XE_TR_VOICE, clip, 0, part, ev, 0, 0);
}

void voice_init(void)
{
    struct lrv_voice_cfg vc;
    char cues[40], want[40];
    int rc;

    memset(&v, 0, sizeof v);
    memset(&vc, 0, sizeof vc);
    vc.volume = xe.cfg.aux_volume;
    vc.default_rate = CLIP_RATE;
    vc.event = on_event;
    rc = lrv_voice_open(&xe.voice, xe.hal, &vc);
    if (rc == LRV_ERR_FORMAT)
        lrv_log(xe.hal, LRV_LOG_WARN, "voice: the voice pack's codec \"%s\" is not supported by "
                "this build", xe.voice.codec);
    else if (rc == LRV_ERR_IO)
        lrv_log(xe.hal, LRV_LOG_WARN, "voice: the backend could not open its voice output");
    else if (rc != LRV_OK)
        lrv_log(xe.hal, LRV_LOG_INFO, "voice: no voice pack");
    if (rc != LRV_OK)
        return;
    if (xe.voice.mode == LRV_AUX_MIX)
        lrv_mixer_set_aux(&xe.music.mixer, lrv_voice_mix, &xe.voice);
    lrv_sys_add_hook(&xe.sys, LRV_HOOK_PUMP, lrv_voice_pump, &xe.voice);

    lrv_voice_meta(&xe.voice, "cues", cues, sizeof cues);
    lrv_pack_meta_get(&xe.pack, "meta", "voicemap", want, sizeof want);
    if (!want[0])
        lrv_log(xe.hal, LRV_LOG_WARN, "voice: the asset pack has no voice cues");
    else if (strcmp(cues, want) != 0)
        lrv_log(xe.hal, LRV_LOG_WARN, "voice: the voice pack was built from a different cue "
                "table than the asset pack (rebuild one of them)");
    snd_update_gain();
    lrv_log(xe.hal, LRV_LOG_INFO, "voice: %u entries, %s %d Hz, %s, volume %d%%, music %d%%",
            (unsigned)xe.voice.pak.count, xe.voice.codec, xe.voice.pack_rate,
            xe.voice.mode == LRV_AUX_MIX ? "mixed with the music" : "own channel",
            xe.cfg.aux_volume > 0 ? xe.cfg.aux_volume : 100,
            xe.cfg.volume > 0 ? xe.cfg.volume : xe.profile.voice_music_volume);
}

int voice_enabled(void)
{
    return lrv_voice_enabled(&xe.voice);
}

/* ---- cues ------------------------------------------------------------------------------------ */

void voice_script_unload(void)
{
    if (v.cues)
        lrv_pack_free(&xe.pack, v.cues, v.cues_size);
    v.cues = 0;
    v.cues_size = 0;
    v.ncues = 0;
    v.msg_valid = 0;
}

void voice_script(const char *name)
{
    const struct lrv_pack_entry *e;
    voice_script_unload();
    if (!voice_enabled())
        return;
    e = lrv_pack_find(&xe.pack, name, LRV_PT_VOICECUE);
    if (!e)
        return;
    v.cues = lrv_pack_load(&xe.pack, e, &v.cues_size);
    if (!v.cues)
        return;
    if (v.cues_size < 8 || memcmp(v.cues, "VCUE", 4) != 0 || lrv_rd16le(v.cues + 4) != 1 ||
        8u + (uint32_t)lrv_rd16le(v.cues + 6) * CUE_REC > v.cues_size) {
        lrv_log(xe.hal, LRV_LOG_WARN, "voice: bad cue table for %s", name);
        voice_script_unload();
        return;
    }
    v.ncues = lrv_rd16le(v.cues + 6);
}

/* clip name of cue (off, part) or NULL; records are sorted by (offset, part) */
static const char *find_cue(uint32_t off, int part)
{
    uint32_t lo = 0, hi = v.ncues;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        const uint8_t *r = v.cues + 8 + mid * CUE_REC;
        uint32_t o = lrv_rd32le(r);
        if (o < off || (o == off && r[4] < part))
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo < v.ncues) {
        const uint8_t *r = v.cues + 8 + lo * CUE_REC;
        if (lrv_rd32le(r) == off && r[4] == part)
            return (const char *)r + 8;
    }
    return 0;
}

/* ---- playback -------------------------------------------------------------------------------- */

void voice_stop(void)
{
    lrv_voice_stop(&xe.voice);
}

static void start(const char *clip, int part)
{
    int rc = lrv_voice_play(&xe.voice, clip, part);
    if (rc == LRV_ERR_NOTFOUND)
        lrv_log(xe.hal, LRV_LOG_WARN, "voice: clip %s missing from the voice pack", clip);
    else if (rc != LRV_OK)
        lrv_log(xe.hal, LRV_LOG_WARN, "voice: clip %s: unknown format", clip);
}

void voice_message(const uint8_t *src)
{
    const char *clip;
    if (!voice_enabled())
        return;
    voice_stop();
    v.msg_valid = 0;
    if (!v.ncues || !xe.script_buf || src < xe.script_buf + 2 ||
        src >= xe.script_buf + xe.script_size)
        return;
    v.msg_off = (uint32_t)(src - xe.script_buf) - 2;     /* host_message gets buf + pos + 2 */
    v.msg_valid = 1;
    /* Windows starts a timed message's clip even when skipping; voice_wait would cut it at
     * once, so none is started */
    if (ctrl_held())
        return;                     /* skipping: the text appears at once, no voice */
    clip = find_cue(v.msg_off, 0);
    if (clip)
        start(clip, 0);
}

void voice_part(int k, int skipped)
{
    const char *clip;
    if (!voice_enabled() || !v.msg_valid)
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

void voice_wait(void)
{
    int why = XE_LEAVE_TIMEOUT, still = 0;
    uint32_t last;
    if (!lrv_voice_playing(&xe.voice))
        return;
    lrv_sys_trace(&xe.sys, LRV_TR_WAIT, 0, 0, XE_WAIT_VOICE, 0, 0, 0);
    last = lrv_voice_progress(&xe.voice);
    for (;;) {
        int k;
        uint32_t now;
        if (!lrv_voice_playing(&xe.voice))
            break;                  /* the clip ended */
        now = lrv_voice_progress(&xe.voice);
        if (now != last) {
            last = now;
            still = 0;
        } else if (++still > STALL_TICKS) {
            lrv_log(xe.hal, LRV_LOG_WARN, "voice: %s made no progress for 2 s, ended",
                    xe.voice.clip);
            voice_stop();
            break;
        }
        if (quitting()) {
            voice_stop();
            why = XE_LEAVE_QUIT;
            break;
        }
        if (ctrl_held()) {
            voice_stop();
            why = XE_LEAVE_CTRL;
            break;
        }
        k = poll_cmd();
        if (k == XE_CMD_CANCEL || (k == XE_CMD_OK && !xe.opt.voice_strict)) {
            voice_stop();
            why = XE_LEAVE_KEY;
            break;
        }
        frame_wait();
    }
    lrv_sys_trace(&xe.sys, LRV_TR_WAIT, 0, 0, XE_WAIT_VOICE, 1, why, 0);
}

void voice_shutdown(void)
{
    if (!voice_enabled())
        return;
    voice_stop();
    lrv_log(xe.hal, LRV_LOG_INFO, "voice: %lu clips started, %lu played to the end, %lu cut",
            xe.voice.n_ev[LRV_VOICE_EV_START], xe.voice.n_ev[LRV_VOICE_EV_END],
            xe.voice.n_ev[LRV_VOICE_EV_CUT]);
    voice_script_unload();
    lrv_mixer_set_aux(&xe.music.mixer, 0, 0);
    lrv_voice_close(&xe.voice);
}
