/* Engine sound API (seg 0B2C, sound.md §3) over the PMD sequencer and an OPN core.
 *
 * The original copies files into the resident driver's buffers and talks INT 60h; here the
 * sequencer lives in the engine. Audio is rendered on demand from the frame tick (snd_pump):
 * the sequencer advances in sample time, firing its timer interrupts, while the core renders
 * the samples in between; the result is pushed to the backend's sink. */
#include <string.h>
#include "xe_internal.h"
#include "snd/snd.h"
#include "snd/pmd.h"
#include "snd/opn_core.h"
#include "voice/voice.h"

#define MUSIC_BUF  (10 * 1024)       /* /M10 */
#define RENDER_MAX 2048

static struct pmd g_pmd;
static const struct opn_core_ops *g_ops;
static struct opn_core *g_core;
static uint32_t g_rate;
static int g_audio;                  /* audio available */
static int g_pull;                   /* the backend renders on its own thread (xe_audio_render) */
static int g_live;                   /* sequencer initialised: xe_audio_render may run it */
static uint32_t g_seq_rate;          /* sample rate the sequencer counts time in */
static uint32_t g_silent_acc;        /* 16.16 samples owed by the silent frame clock */

/* Every entry into the sequencer from the engine is bracketed by these; in pull mode the
 * backend's audio thread holds the same lock inside xe_audio_render. Never nested. */
#define SND_LOCK()   do { if (xe.hal->audio_lock) xe.hal->audio_lock(); } while (0)
#define SND_UNLOCK() do { if (xe.hal->audio_unlock) xe.hal->audio_unlock(); } while (0)
static uint8_t *g_music;             /* music buffer (mutable copy of the .M file) */
static uint8_t *g_efc;
static uint32_t g_efc_size;
static uint8_t *g_ssgtab;
static uint32_t g_ssgtab_size;
static char g_music_name[16];        /* DS:13C8 */
static int g_music_started;          /* DS:13D7 */
static int16_t g_buf[RENDER_MAX];
static int g_skip;                   /* xe_audio_skip: the core is not run, output is silence */
static int32_t g_gain = 256;         /* cfg.music_volume as x/256 (music and effects: one chip) */

static int name_eq(const char *a, const char *b)
{
    for (;; a++, b++) {
        int ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb)
            return 0;
        if (!ca)
            return 1;
    }
}

/* Register writes are spread in time like on the 26K board, where PMD polls the OPN BUSY
 * flag before every write, so consecutive writes are at least one chip sample (18 us)
 * apart. The sequencer issues a tick's writes at one instant; the queue below delivers them
 * to the core one chip sample apart while rendering. This matters for q=0 notes
 * (pmd-sequencer.md §6.2): key-off and key-on of the same channel come back to back and
 * must reach the core in separate samples to re-attack. */
#define WQ_SIZE 1024                 /* a song start writes ~200 registers */
static struct { uint8_t reg, val; } g_wq[WQ_SIZE];
static unsigned g_wq_head, g_wq_tail;
static uint32_t g_wq_acc;            /* 16.16 samples until the bus is free */
static uint32_t g_wq_cost;           /* 16.16 core samples per write (= rate / 55 467) */

static void wq_apply_due(void)
{
    while (g_wq_head != g_wq_tail && g_wq_acc < 0x10000) {
        if (g_core)
            g_ops->write(g_core, g_wq[g_wq_head].reg, g_wq[g_wq_head].val);
        g_wq_head = (g_wq_head + 1) % WQ_SIZE;
        g_wq_acc += g_wq_cost;
    }
}

static void chip_write(void *ctx, uint8_t reg, uint8_t val)
{
    unsigned next = (g_wq_tail + 1) % WQ_SIZE;
    (void)ctx;
    if (next == g_wq_head) {         /* overflow: flush everything at once */
        while (g_wq_head != g_wq_tail) {
            if (g_core)
                g_ops->write(g_core, g_wq[g_wq_head].reg, g_wq[g_wq_head].val);
            g_wq_head = (g_wq_head + 1) % WQ_SIZE;
        }
        g_wq_acc = 0;
    }
    g_wq[g_wq_tail].reg = reg;
    g_wq[g_wq_tail].val = val;
    g_wq_tail = next;
}

void snd_lock(void)   { SND_LOCK(); }
void snd_unlock(void) { SND_UNLOCK(); }

static void render_cb(void *ctx, int n)
{
    int16_t **pp = ctx;
    while (n > 0) {
        int m = n;
        wq_apply_due();
        if (g_wq_head != g_wq_tail) {
            int due = (int)(g_wq_acc >> 16);
            if (due < m)
                m = due;
        }
        if (g_core && !g_skip) {
            g_ops->render(g_core, *pp, m);
            if (g_gain != 256) {
                int i;
                for (i = 0; i < m; i++)
                    (*pp)[i] = (int16_t)(((int32_t)(*pp)[i] * g_gain) >> 8);
            }
        } else
            memset(*pp, 0, sizeof(int16_t) * (size_t)m);
        *pp += m;
        n -= m;
        g_wq_acc = g_wq_acc > ((uint32_t)m << 16) ? g_wq_acc - ((uint32_t)m << 16) : 0;
    }
}

static void wq_init(uint32_t rate)
{
    g_wq_head = g_wq_tail = 0;
    g_wq_acc = 0;
    g_wq_cost = rate ? (uint32_t)(((uint64_t)rate << 16) / (PMD_CLOCK / 72)) : 0;
}

/* cfg.music_volume, or the default: lower while the voice is on, which the FM music masks
 * (docs/voice.md §6). voice_init calls this again once it knows. */
void snd_update_gain(void)
{
    int v = xe.cfg.music_volume;
    int32_t g;

    if (v <= 0 || v > 100)
        v = voice_enabled() ? VOICE_MUSIC_VOLUME : 100;
    if (xe.hal && xe.hal->set_volume && xe.hal->set_volume(XE_VOL_MUSIC, v))
        g = 256;                    /* the backend's output applies it */
    else
        g = v * 256 / 100;
    SND_LOCK();
    g_gain = g;
    SND_UNLOCK();
}

void snd_init(void)
{
    const struct xpak_entry *e;
    uint32_t size;
    uint8_t *data;

    g_music_name[0] = 0;
    g_music_started = 0;
    g_music = sys_alloc(MUSIC_BUF);
    snd_update_gain();

    e = xpak_find(&xe.pak, "xen_se", XPAK_T_EFC);
    if (e) {
        data = xpak_load(&xe.pak, e, &size);
        if (data) {
            g_efc = data;
            g_efc_size = size;
        }
    } else {
        sys_log(XE_LOG_WARN, "sound: XEN_SE.EFC missing");
    }
    e = xpak_find(&xe.pak, "ssgefc", XPAK_T_SSGEFC);
    if (e)
        g_ssgtab = xpak_load(&xe.pak, e, &g_ssgtab_size);

#ifdef XE_HAVE_YMFM
    g_ops = (xe.cfg.opn_core == XE_OPN_LITE) ? &opn_lite_ops : &opn_ymfm_ops;
#else
    g_ops = &opn_lite_ops;
#endif
    if (xe.cfg.audio_rate > 0) {
        uint32_t want = (uint32_t)xe.cfg.audio_rate;
        g_core = g_ops->create(PMD_CLOCK, want, &g_rate);
        if (g_core) {
            int got = xe.hal->audio_open((int)g_rate);
            /* a pull-mode backend may already be calling xe_audio_render: from here on
             * every change to the core and the sequencer happens under the lock (the
             * renderer outputs silence until g_live is set below) */
            SND_LOCK();
            if (got > 0) {
                g_audio = 1;
                if ((uint32_t)got != g_rate) {
                    /* the backend could not take the core's rate: re-create at the backend's */
                    g_ops->destroy(g_core);
                    g_core = g_ops->create(PMD_CLOCK, (uint32_t)got, &g_rate);
                }
            } else {
                g_ops->destroy(g_core);
                g_core = 0;
            }
            SND_UNLOCK();
        }
    }
    g_pull = g_audio && xe.hal->audio_lock != 0;
    g_seq_rate = g_rate ? g_rate : 44100;
    g_silent_acc = 0;
    SND_LOCK();
    wq_init(g_rate);
    pmd_init(&g_pmd, chip_write, 0, g_seq_rate);
    if (g_efc)
        pmd_set_efc(&g_pmd, g_efc, g_efc_size);
    if (g_ssgtab)
        pmd_set_ssgtab(&g_pmd, g_ssgtab, g_ssgtab_size);
    g_live = 1;
    SND_UNLOCK();
    sys_log(XE_LOG_INFO, "sound: core %s, %lu Hz%s", g_ops->name, (unsigned long)g_rate,
            !g_audio ? " (no audio output)" : g_pull ? " (pull: backend thread)" : " (push)");
}

void snd_shutdown(void)
{
    SND_LOCK();
    g_live = 0;
    SND_UNLOCK();
    if (g_audio)
        xe.hal->audio_close();
    if (g_core)
        g_ops->destroy(g_core);
    g_core = 0;
    if (g_efc)
        xpak_free(&xe.pak, g_efc, g_efc_size);
    if (g_ssgtab)
        xpak_free(&xe.pak, g_ssgtab, g_ssgtab_size);
    sys_free(g_music, MUSIC_BUF);
    g_efc = 0; g_ssgtab = 0; g_music = 0;
}

/* Render as much as the sink can take now (bounded per call). Without a sink the sequencer
 * still has to run: scripts wait on its state (music_wait_status for the ST00 cue in the
 * opening, music_fade_stop_wait, song end), so one frame's worth of samples is advanced
 * per tick and the output discarded. */
void snd_pump(void)
{
    int space;
    if (g_pull)
        return;                      /* the backend's audio thread pulls (xe_audio_render) */
    if (!g_audio) {
        /* 16.16 samples per 56.42 Hz frame, in integers: rate * 65536 * 100 / 5642 */
        g_silent_acc += (uint32_t)(((uint64_t)g_seq_rate << 16) * 100u / 5642u);
        while (g_silent_acc >= 0x10000u) {
            int n = (int)(g_silent_acc >> 16);
            int16_t *p = g_buf;
            if (n > RENDER_MAX)
                n = RENDER_MAX;
            pmd_advance(&g_pmd, n, render_cb, &p);
            voice_mix(g_buf, n, g_seq_rate);    /* no sink: the voice still runs in time */
            g_silent_acc -= (uint32_t)n << 16;
        }
        return;
    }
    space = xe.hal->audio_space();
    while (space > 0) {
        int n = space > RENDER_MAX ? RENDER_MAX : space;
        int16_t *p = g_buf;
        pmd_advance(&g_pmd, n, render_cb, &p);
        voice_mix(g_buf, n, g_rate);
        xe.hal->audio_push(g_buf, n);
        space -= n;
    }
}

/* Pull mode entry (xe.h): the backend's audio thread. */
void xe_audio_render(int16_t *pcm, int frames)
{
    int16_t *p = pcm;
    SND_LOCK();
    if (g_live) {
        pmd_advance(&g_pmd, frames, render_cb, &p);
        voice_mix(pcm, frames, g_rate);
    } else {
        memset(pcm, 0, sizeof(int16_t) * (size_t)frames);
    }
    SND_UNLOCK();
}

/* Pull mode, overloaded backend (xe.h): the sequencer and the register writes advance by
 * `frames` samples, the chip core does not run, pcm gets silence. */
void xe_audio_skip(int16_t *pcm, int frames)
{
    int16_t *p = pcm;
    SND_LOCK();
    g_skip = 1;
    if (g_live)
        pmd_advance(&g_pmd, frames, render_cb, &p);
    else
        memset(pcm, 0, sizeof(int16_t) * (size_t)frames);
    g_skip = 0;
    SND_UNLOCK();
}

/* ---- music (sound.md §3) ----------------------------------------------------------------- */

static void music_stop_nolock(void)
{
    pmd_mstop(&g_pmd);
    g_music_name[0] = 0;
}

void music_load(const char *name)
{
    const struct xpak_entry *e;
    uint32_t size;
    uint8_t *data;
    if (!name || name_eq(g_music_name, name)) {
        g_music_started = 1;            /* same song keeps playing, no restart */
        return;
    }
    g_music_started = 0;
    /* the pack read happens before the lock: the audio thread must not wait on file I/O */
    e = name[0] ? xpak_find(&xe.pak, name, XPAK_T_MUSIC) : 0;
    data = e ? xpak_load(&xe.pak, e, &size) : 0;
    if (name[0] && !e)
        sys_log(XE_LOG_WARN, "music: %s.M not found", name);
    SND_LOCK();
    music_stop_nolock();
    if (data) {
        /* the engine fread()s the file into PMD's buffer starting at "music data - 1" */
        if (size > MUSIC_BUF)
            size = MUSIC_BUF;
        memcpy(g_music, data, size);
        pmd_load_music(&g_pmd, g_music + 1, size - 1);
        strncpy(g_music_name, name, sizeof g_music_name - 1);
        g_music_name[sizeof g_music_name - 1] = 0;
    }
    SND_UNLOCK();                       /* missing file: nothing loaded, name stays cleared */
    if (data)
        xpak_free(&xe.pak, data, size);
}

void music_play(void)
{
    if (g_music_started != 1 && g_music_name[0]) {
        g_music_started = 1;
        SND_LOCK();
        pmd_mstart(&g_pmd);
        SND_UNLOCK();
    }
}

void music_stop(void)
{
    SND_LOCK();
    music_stop_nolock();
    SND_UNLOCK();
}

void music_fade(int speed)
{
    if (g_music_name[0]) {
        SND_LOCK();
        pmd_fade(&g_pmd, (uint8_t)speed);
        SND_UNLOCK();
    }
}

/* music_fade_stop_wait 0B2C:0427: fade 25, wait until fully faded, stop (≈1.6 s) */
void music_fade_stop_wait(void)
{
    int guard = 0;
    music_fade(0x19);
    sys_trace(XE_TR_WAIT, 0, 0, XE_WAIT_MUSIC, 0, -1, 0);
    for (;;) {
        uint8_t fv;
        SND_LOCK();
        fv = pmd_get_fadevol(&g_pmd);
        SND_UNLOCK();
        if (!g_music_name[0] || fv == 0xFF || guard++ >= 600)
            break;
        sys_frame_wait();
        if (xe.quit_requested || xe.emergency_quit)
            break;
    }
    sys_trace(XE_TR_WAIT, 0, 0, XE_WAIT_MUSIC, 1, XE_LEAVE_TIMEOUT, guard);
    music_stop();
}

void music_set_fadevol(int v)
{
    SND_LOCK();
    pmd_set_fadevol(&g_pmd, (uint8_t)v);
    SND_UNLOCK();
}

/* music_wait_status 0B2C:04D8: wait while status1 == n (no timeout in the original) */
void music_wait_status(int n)
{
    uint8_t s1, s2;
    int guard = 0;
    if (!g_music_name[0])
        return;
    sys_trace(XE_TR_WAIT, 0, 0, XE_WAIT_MUSIC, 0, n, 0);
    for (;;) {
        SND_LOCK();
        pmd_get_status(&g_pmd, &s1, &s2);
        SND_UNLOCK();
        if (s1 != n || s2 == 0xFF)
            break;
        if (xe.quit_requested || xe.emergency_quit || guard++ > 56 * 120)
            break;
        sys_frame_wait();
    }
    sys_log(XE_LOG_INFO, "music_wait_status(%d): %s after %d ticks (status %u/%u)", n,
            guard > 56 * 120 ? "gave up" : "released", guard, s1, s2);
    sys_trace(XE_TR_WAIT, 0, 0, XE_WAIT_MUSIC, 1, XE_LEAVE_TIMEOUT, guard);
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
    SND_LOCK();
    pmd_fm_effect_on(&g_pmd, n);
    SND_UNLOCK();
}

void se_stop(void)
{
    SND_LOCK();
    pmd_fm_effect_off(&g_pmd);
    SND_UNLOCK();
}

/* for tests and tools */
struct pmd *snd_sequencer(void)
{
    return &g_pmd;
}

/* ---- offline render (xe.h) ----------------------------------------------------------------- */

int xe_render_music(const struct xe_hal *hal, const struct xe_config *cfg, const char *name,
                    double seconds, int se, double se_at, uint32_t *rate,
                    void (*sink)(void *ctx, const int16_t *pcm, int frames), void *ctx)
{
    uint64_t total, done = 0, se_sample;
    int se_done = 0;

    memset(&xe, 0, sizeof xe);
    xe.hal = hal;
    xe.cfg = *cfg;
    if (xpak_open(&xe.pak, hal, XE_PACK_MAIN) != 0)
        return -1;
    xe.lang = XPAK_L_ANY;
    xe.cfg.audio_rate = cfg->audio_rate > 0 ? cfg->audio_rate : 44100;
    g_audio = 0;
    g_pull = 0;
    g_music_name[0] = 0;
    g_music_started = 0;
    g_music = sys_alloc(MUSIC_BUF);
    snd_update_gain();
    {
        const struct xpak_entry *e = xpak_find(&xe.pak, "xen_se", XPAK_T_EFC);
        if (e) g_efc = xpak_load(&xe.pak, e, &g_efc_size);
        e = xpak_find(&xe.pak, "ssgefc", XPAK_T_SSGEFC);
        if (e) g_ssgtab = xpak_load(&xe.pak, e, &g_ssgtab_size);
    }
#ifdef XE_HAVE_YMFM
    g_ops = (cfg->opn_core == XE_OPN_LITE) ? &opn_lite_ops : &opn_ymfm_ops;
#else
    g_ops = &opn_lite_ops;
#endif
    g_core = g_ops->create(PMD_CLOCK, (uint32_t)xe.cfg.audio_rate, &g_rate);
    if (!g_core)
        return -1;
    wq_init(g_rate);
    pmd_init(&g_pmd, chip_write, 0, g_rate);
    if (g_efc) pmd_set_efc(&g_pmd, g_efc, g_efc_size);
    if (g_ssgtab) pmd_set_ssgtab(&g_pmd, g_ssgtab, g_ssgtab_size);
    *rate = g_rate;
    if (name && name[0]) {
        music_load(name);
        music_play();
    }
    total = (uint64_t)(seconds * g_rate);
    se_sample = (uint64_t)(se_at * g_rate);
    while (done < total) {
        int n = (int)((total - done) > RENDER_MAX ? RENDER_MAX : (total - done));
        int16_t *p = g_buf;
        if (se > 0 && !se_done && done >= se_sample) {
            se_play(se);
            se_done = 1;
        } else if (se > 0 && !se_done && done + (uint64_t)n > se_sample) {
            n = (int)(se_sample - done);
        }
        pmd_advance(&g_pmd, n, render_cb, &p);
        sink(ctx, g_buf, n);
        done += (uint64_t)n;
    }
    g_ops->destroy(g_core);
    g_core = 0;
    xpak_close(&xe.pak);
    return 0;
}
