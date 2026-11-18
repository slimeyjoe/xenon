/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Engine sound API (seg 0B2C, sound.md §3) over the PMD driver.
 *
 * The original copies files into the resident driver's buffers and talks INT 60h; here the
 * driver is lrvne's PMD module (lrvne/pmd.h), in the engine's state, and lrvne runs it in
 * sample time: it is the frame-driven sequencer of the music path (lrvne/music.h), whose write
 * queue spaces the register writes as PMD's polling of the BUSY flag did on the 26K board, one
 * chip sample apart (pmd-sequencer.md §6.2: a key-off and key-on of the same channel must
 * reach the core in separate samples to re-attack). The queue feeds an OPN core, the mixer
 * scales the music and adds the voice.
 *
 * Without an audio device the driver still has to run: scripts wait on its state
 * (music_wait_status for a cue of the song, music_fade_stop_wait, a song's end), so the music
 * path renders a frame's worth of samples per frame into nothing. */
#include <string.h>
#include "xe.h"
#include "snd/snd.h"
#include "voice/voice.h"

#define QUEUE_SIZE 1024                 /* a song start writes ~200 registers */
#define NO_DEVICE_RATE 44100            /* the driver's clock without a device */

static uint8_t *g_music;                /* music buffer (mutable copy of the .M file) */
static uint32_t g_music_size;           /* its size: the game's /M option (profile) */
static uint8_t *g_efc;
static uint32_t g_efc_size;
static uint8_t *g_ssgtab;
static uint32_t g_ssgtab_size;
static char g_music_name[16];           /* DS:13C8 */
static int g_music_started;             /* DS:13D7 */

/* the driver writes into the music path's queue */
static void chip_write(void *ctx, uint8_t reg, uint8_t val)
{
    (void)ctx;
    lrv_opnq_write(&xe.music.q, reg, val);
}

/* the music buffer and the effects: the game's FM effect bank, PMD's own SSG effects */
static void load_effects(void)
{
    const struct lrv_pack_entry *e = 0;
    g_music_size = (uint32_t)xe.profile.music_buffer;
    g_music = lrv_xalloc(xe.hal, g_music_size);
    if (xe.profile.se_bank[0]) {
        e = lrv_pack_find(&xe.pack, xe.profile.se_bank, XE_PT_EFC);
        if (e)
            g_efc = lrv_pack_load(&xe.pack, e, &g_efc_size);
        else
            lrv_log(xe.hal, LRV_LOG_WARN, "sound: the effect bank %s is missing",
                    xe.profile.se_bank);
    }
    e = lrv_pack_find(&xe.pack, "ssgefc", XE_PT_SSGEFC);
    if (e)
        g_ssgtab = lrv_pack_load(&xe.pack, e, &g_ssgtab_size);
}

/* The music path: driver -> write queue -> OPN core -> mixer -> the HAL's stream (offline: no
 * stream, the caller renders) */
static int music_open(int offline)
{
    struct lrv_music_cfg mc;
    memset(&mc, 0, sizeof mc);
    lrv_pmd_seq(&xe.pmd, &mc.seq);
    mc.core = xe.cfg.opn_core;
    mc.rate = xe.cfg.audio_rate;
    mc.clock_hz = LRV_OPN_CLOCK_PC98;
    mc.bus_clocks = LRV_OPN_FM_DIV;
    mc.queue_size = QUEUE_SIZE;
    mc.queue_flags = LRV_OPNQ_FLUSH_ON_FULL;
    mc.idle_rate = NO_DEVICE_RATE;
    mc.frame_num = 5642;                /* the 56.42 Hz frame */
    mc.frame_den = 100;
    mc.gain_floor = 1;
    mc.flags = offline ? LRV_MUSIC_OFFLINE : 0;
    return lrv_music_open(&xe.music, xe.hal, &mc);
}

/* the driver as it is installed, counting time at the music path's rate; then it renders */
static void driver_start(void)
{
    lrv_mixer_lock(&xe.music.mixer);
    lrv_pmd_init(&xe.pmd, chip_write, 0, lrv_music_rate(&xe.music));
    xe.pmd.stop_after_fade = !xe.profile.music_keep_after_fade;
    if (g_efc)
        lrv_pmd_set_efc(&xe.pmd, g_efc, g_efc_size);
    if (g_ssgtab)
        lrv_pmd_set_ssgtab(&xe.pmd, g_ssgtab, g_ssgtab_size);
    lrv_mixer_unlock(&xe.music.mixer);
    lrv_music_start(&xe.music);
}

void snd_init(void)
{
    g_music_name[0] = 0;
    g_music_started = 0;
    load_effects();
    if (music_open(0) != LRV_OK)
        lrv_fatal(xe.hal, "sound: cannot create the FM chip");
    snd_update_gain();
    driver_start();
}

void snd_shutdown(void)
{
    lrv_music_close(&xe.music);         /* first: the sources are not called after it */
    if (g_efc)
        lrv_pack_free(&xe.pack, g_efc, g_efc_size);
    if (g_ssgtab)
        lrv_pack_free(&xe.pack, g_ssgtab, g_ssgtab_size);
    lrv_free(xe.hal, g_music, g_music_size);
    g_efc = 0;
    g_ssgtab = 0;
    g_music = 0;
}

/* --volume, or the default: the profile's voice_music_volume while the voice is on, which the
 * FM music would mask (docs/voice.md §6). voice_init calls this again once it knows. */
void snd_update_gain(void)
{
    int v = xe.cfg.volume;
    if (v == 0)
        v = voice_enabled() ? xe.profile.voice_music_volume : 100;
    lrv_mixer_set_volume(&xe.music.mixer, v < 0 ? 0 : v);
}

/* ---- music (sound.md §3) ----------------------------------------------------------------- */

static void music_stop_nolock(void)
{
    lrv_pmd_mstop(&xe.pmd);
    g_music_name[0] = 0;
}

void music_load(const char *name)
{
    const struct lrv_pack_entry *e;
    uint32_t size;
    uint8_t *data;
    if (!name || lrv_name_eq(g_music_name, name)) {
        g_music_started = 1;            /* same song keeps playing, no restart */
        return;
    }
    g_music_started = 0;
    /* the pack read happens before the lock: the audio thread must not wait on file I/O */
    e = name[0] ? lrv_pack_find(&xe.pack, name, XE_PT_MUSIC) : 0;
    data = e ? lrv_pack_load(&xe.pack, e, &size) : 0;
    if (name[0] && !e)
        lrv_log(xe.hal, LRV_LOG_WARN, "music: %s.M not found", name);
    lrv_mixer_lock(&xe.music.mixer);
    music_stop_nolock();
    if (data) {
        /* the engine fread()s the file into PMD's buffer starting at "music data - 1" */
        uint32_t n = size > g_music_size ? g_music_size : size;
        memcpy(g_music, data, n);
        lrv_pmd_load_music(&xe.pmd, g_music + 1, n - 1);
        strncpy(g_music_name, name, sizeof g_music_name - 1);
        g_music_name[sizeof g_music_name - 1] = 0;
    }
    lrv_mixer_unlock(&xe.music.mixer);        /* missing file: nothing loaded, name stays cleared */
    if (data)
        lrv_pack_free(&xe.pack, data, size);
}

void music_play(void)
{
    if (g_music_started != 1 && g_music_name[0]) {
        g_music_started = 1;
        lrv_mixer_lock(&xe.music.mixer);
        lrv_pmd_mstart(&xe.pmd);
        lrv_mixer_unlock(&xe.music.mixer);
    }
}

void music_stop(void)
{
    lrv_mixer_lock(&xe.music.mixer);
    music_stop_nolock();
    lrv_mixer_unlock(&xe.music.mixer);
}

void music_fade(int speed)
{
    if (g_music_name[0]) {
        lrv_mixer_lock(&xe.music.mixer);
        lrv_pmd_fade(&xe.pmd, (uint8_t)speed);
        lrv_mixer_unlock(&xe.music.mixer);
    }
}

/* music_fade_stop_wait 0B2C:0427: fade 25, wait until fully faded, stop (≈1.6 s) */
void music_fade_stop_wait(void)
{
    int guard = 0;
    music_fade(0x19);
    lrv_sys_trace(&xe.sys, LRV_TR_WAIT, 0, 0, XE_WAIT_MUSIC, 0, -1, 0);
    for (;;) {
        uint8_t fv;
        lrv_mixer_lock(&xe.music.mixer);
        fv = lrv_pmd_get_fadevol(&xe.pmd);
        lrv_mixer_unlock(&xe.music.mixer);
        if (!g_music_name[0] || fv == 0xFF || guard++ >= 600)
            break;
        frame_wait();
        if (quitting())
            break;
    }
    lrv_sys_trace(&xe.sys, LRV_TR_WAIT, 0, 0, XE_WAIT_MUSIC, 1, XE_LEAVE_TIMEOUT, guard);
    music_stop();
}

void music_set_fadevol(int v)
{
    lrv_mixer_lock(&xe.music.mixer);
    lrv_pmd_set_fadevol(&xe.pmd, (uint8_t)v);
    lrv_mixer_unlock(&xe.music.mixer);
}

/* music_wait_status 0B2C:04D8: wait while status1 == n (no timeout in the original) */
void music_wait_status(int n)
{
    uint8_t s1, s2;
    int guard = 0;
    if (!g_music_name[0])
        return;
    lrv_sys_trace(&xe.sys, LRV_TR_WAIT, 0, 0, XE_WAIT_MUSIC, 0, n, 0);
    for (;;) {
        lrv_mixer_lock(&xe.music.mixer);
        lrv_pmd_get_status(&xe.pmd, &s1, &s2);
        lrv_mixer_unlock(&xe.music.mixer);
        if (s1 != n || s2 == 0xFF)
            break;
        if (quitting() || guard++ > 56 * 120)
            break;
        frame_wait();
    }
    lrv_log(xe.hal, LRV_LOG_INFO, "music_wait_status(%d): %s after %d ticks (status %u/%u)", n,
            guard > 56 * 120 ? "gave up" : "released", guard, s1, s2);
    lrv_sys_trace(&xe.sys, LRV_TR_WAIT, 0, 0, XE_WAIT_MUSIC, 1, XE_LEAVE_TIMEOUT, guard);
}

const char *music_get_name(void)
{
    return g_music_name;
}

int music_started(void)
{
    return g_music_started;
}

void se_play(int n)
{
    lrv_mixer_lock(&xe.music.mixer);
    lrv_pmd_fm_effect_on(&xe.pmd, n);
    lrv_mixer_unlock(&xe.music.mixer);
}

void se_stop(void)
{
    lrv_mixer_lock(&xe.music.mixer);
    lrv_pmd_fm_effect_off(&xe.pmd);
    lrv_mixer_unlock(&xe.music.mixer);
}

/* ---- offline render (struct lrv_app.render_music) ----------------------------------------- */

/* `name` for `seconds`, the same path as the game's without a device; --se N [--se-at S]
 * starts sound effect N S seconds in. */
int xe_render_music(void *ctx, const struct lrv_hal *hal, const struct lrv_run_cfg *cfg,
                    const char *name, double seconds, uint32_t *rate,
                    void (*sink)(void *sctx, const int16_t *pcm, int frames), void *sctx)
{
    const struct xe_options *opt = ctx;
    int16_t buf[LRV_MIXER_BLOCK];
    uint64_t total, done = 0, se_sample;
    int se_done = 0, rc;

    memset(&xe, 0, sizeof xe);
    xe.hal = hal;
    xe.cfg = *cfg;
    rc = lrv_pack_open(&xe.pack, hal, LRV_PACK_MAIN);
    if (rc != LRV_OK)
        return rc;
    rc = profile_load();
    if (rc != LRV_OK) {
        lrv_pack_close(&xe.pack);
        return rc;
    }
    g_music_name[0] = 0;
    g_music_started = 0;
    load_effects();
    rc = music_open(1);                 /* at --rate, or the clock without a device */
    if (rc == LRV_OK) {
        uint32_t r = lrv_music_rate(&xe.music);
        snd_update_gain();
        driver_start();
        *rate = r;
        if (name && name[0]) {
            music_load(name);
            music_play();
        }
        total = (uint64_t)(seconds * r);
        se_sample = (uint64_t)(opt->se_at * r);
        while (done < total) {
            int n = (int)((total - done) > LRV_MIXER_BLOCK ? LRV_MIXER_BLOCK : (total - done));
            if (opt->se > 0 && !se_done && done >= se_sample) {
                se_play(opt->se);
                se_done = 1;
            } else if (opt->se > 0 && !se_done && done + (uint64_t)n > se_sample) {
                n = (int)(se_sample - done);
            }
            lrv_mixer_render(&xe.music.mixer, buf, n);
            sink(sctx, buf, n);
            done += (uint64_t)n;
        }
    }
    snd_shutdown();
    lrv_pack_close(&xe.pack);
    return rc;
}
