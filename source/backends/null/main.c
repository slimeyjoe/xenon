/* Null backend: headless engine driver for tests and automation.
 *
 *   xenon-null [--pak FILE] [--lang ja|en|pl] [--storage DIR] [--max-ticks N]
 *              [--autoplay SPEC] [--shot-every N] [--shot-dir DIR] [--trace] [--control]
 *   xenon-null --present-test
 *
 * --control turns on the test-automation protocol on stdin/stdout (control.c): the engine
 * reports its events, stops whenever it waits for a key, and takes input from the driver.
 *
 * No display, no audio, no real time: frame_wait returns at once. Input comes from the
 * autoplay spec (backends/common/autoplay.h, default "ok:30"; "@file" reads it from a file).
 * Its snap: ticks print snapshot lines (control-mode "snap" events with the frame hash of
 * ptest.h) on stdout: the reference for the Amiga game snapshots (tests/e2e/amiga.py).
 * Screenshots (PPM, palette applied, 640x400) are written every --shot-every ticks.
 * --present-test prints the snapshots of the synthetic presentation test (ptest.h) and exits.
 * XE_MEMSTAT=1 in the environment prints the engine's allocation peak at exit (stderr; the
 * content-dependent part of the Amiga memory table in backends/amiga/README.md "Memory"). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xe.h"
#include "pack/pack.h"
#include "autoplay.h"
#include "control.h"
#include "ptest.h"

static FILE *g_pak;
static uint32_t g_pak_size;
static const char *g_storage = ".";
static long g_max_ticks = 200000;
static uint32_t g_tick;
static struct autoplay g_ap;
static int g_have_ap;
/* the frame of the last present while snap: ticks are pending (the engine draws the text
 * overlay into the page only for the duration of the present) */
static uint8_t g_snap_planes[4][XE_PLANE_BYTES];
static struct xe_planes g_snap_page;
static uint8_t g_snap_pal[48];
static int g_snap_scroll, g_snap_on, g_snap_have;
static int g_shot_every;
static const char *g_shot_dir = ".";
static int g_trace;
static int g_pending;
static int g_control;

/* ---- video ---- */
static int v_open(const struct xe_video_cfg *c) { (void)c; return 0; }
static void v_close(void) {}

static void write_ppm(const struct xe_planes *pg, int scroll, const uint8_t *rgb4, int on)
{
    char path[512];
    FILE *f;
    int y, x;
    snprintf(path, sizeof path, "%s/shot_%08lu.ppm", g_shot_dir, (unsigned long)g_tick);
    f = fopen(path, "wb");
    if (!f)
        return;
    fprintf(f, "P6\n640 400\n255\n");
    for (y = 0; y < 400; y++) {
        int sy = (y + scroll) % 400;
        for (x = 0; x < 640; x++) {
            int off = sy * 80 + x / 8, bit = 7 - (x & 7);
            int idx = ((pg->p[0][off] >> bit) & 1) | (((pg->p[1][off] >> bit) & 1) << 1) |
                      (((pg->p[2][off] >> bit) & 1) << 2) | (((pg->p[3][off] >> bit) & 1) << 3);
            uint8_t px[3];
            if (on) {
                px[0] = (uint8_t)(rgb4[idx * 3] * 17);
                px[1] = (uint8_t)(rgb4[idx * 3 + 1] * 17);
                px[2] = (uint8_t)(rgb4[idx * 3 + 2] * 17);
            } else {
                px[0] = px[1] = px[2] = 0;
            }
            fwrite(px, 1, 3, f);
        }
    }
    fclose(f);
}

static void v_present(const struct xe_planes *page, const struct xe_rect *dirty, int sw,
                      int scroll, const uint8_t rgb4[48], int on)
{
    (void)dirty;
    if (g_control)
        ctl_present(page, scroll, rgb4, on, sw);
    else if (g_ap.snap_count) {
        int p;
        for (p = 0; p < 4; p++)
            memcpy(g_snap_planes[p], page->p[p], XE_PLANE_BYTES);
        memcpy(g_snap_pal, rgb4, 48);
        g_snap_scroll = scroll;
        g_snap_on = on;
        g_snap_have = 1;
    }
    if (g_shot_every && g_tick % (uint32_t)g_shot_every == 0)
        write_ppm(page, scroll, rgb4, on);
}

/* ---- time / input ---- */
/* --voice-out channel: a pretend XE_VOICE_CHANNEL device that plays what is queued in real
 * time (one tick = 1/56.42 s), so the control-mode tests exercise the SDL3 voice path too;
 * --voice-out stuck: one that never plays (the engine's no-progress guard, voice_wait) */
static int g_voice_channel;
static long g_vq;                       /* frames queued */
static long g_vheld;                    /* frames held back until the end-of-clip push, as SDL does */
static int g_vrate;
static uint32_t g_vacc;

static int vo_open(int rate) { g_vrate = rate; g_vq = g_vheld = 0; return rate; }
static void vo_close(void) { g_vq = g_vheld = 0; }
static int vo_space(void) { return g_vq < 65536 ? (int)(65536 - g_vq) : 0; }
static void vo_push(const int16_t *pcm, int frames)
{
    (void)pcm;
    if (frames > 0) {                   /* the last 7 frames stay held back (SDL's resampler) */
        g_vq += g_vheld + frames - 7;
        g_vheld = 7;
        if (g_vq < 0) {
            g_vheld += g_vq;
            g_vq = 0;
        }
    } else {
        g_vq += g_vheld;                /* end of clip */
        g_vheld = 0;
    }
}
static int vo_queued(void) { return (int)(g_vq + g_vheld); }
static void vo_flush(void) { g_vq = g_vheld = 0; }

static void vo_tick(void)
{
    if (g_voice_channel == 2)
        return;                                 /* --voice-out stuck: plays nothing */
    g_vacc += (uint32_t)g_vrate * 100u;         /* frames per tick = rate / 56.42 */
    g_vq -= (long)(g_vacc / 5642u);
    g_vacc %= 5642u;
    if (g_vq < 0)
        g_vq = 0;
}

static void t_frame_wait(void)
{
    g_tick++;
    if (g_voice_channel)
        vo_tick();
    if ((long)g_tick > g_max_ticks) {
        fprintf(stderr, "null: max ticks reached\n");
        if (g_control)
            ctl_exit(3, "max ticks reached");
        exit(3);
    }
    if (g_control)
        ctl_tick(g_tick);
    else if (g_snap_have && autoplay_snap_due(&g_ap, g_tick)) {
        char name[24];
        int p;
        for (p = 0; p < 4; p++)
            g_snap_page.p[p] = g_snap_planes[p];
        snprintf(name, sizeof name, "t%lu", (unsigned long)g_tick);
        ctl_present(&g_snap_page, g_snap_scroll, g_snap_pal, g_snap_on, 0);
        ctl_emit_snap(name, g_tick, xe_frame_hash32(&g_snap_page, g_snap_scroll, g_snap_pal, g_snap_on));
    }
}
static uint32_t t_ticks(void) { return g_tick; }

static void i_poll(struct xe_input *in)
{
    if (g_control) {
        ctl_input(in);
        return;
    }
    memset(in, 0, sizeof *in);
    autoplay_poll(&g_ap, g_tick, in);
    in->pressed |= (uint32_t)g_pending;
    g_pending = 0;
}
static void i_flush(void)
{
    if (g_control)
        ctl_input_flush();
    else
        autoplay_flush(&g_ap, g_tick);
}

/* ---- audio ---- */
static int a_open(int rate) { (void)rate; return 0; }
static void a_close(void) {}
static int a_space(void) { return 0; }
static void a_push(const int16_t *p, int n) { (void)p; (void)n; }

/* ---- storage ---- */
static int s_read(const char *key, void *buf, size_t cap, size_t *len)
{
    char path[512];
    FILE *f;
    snprintf(path, sizeof path, "%s/%s.dat", g_storage, key);
    f = fopen(path, "rb");
    if (!f)
        return -1;
    *len = fread(buf, 1, cap, f);
    fclose(f);
    return 0;
}
static int s_write(const char *key, const void *buf, size_t len)
{
    char path[512];
    FILE *f;
    snprintf(path, sizeof path, "%s/%s.dat", g_storage, key);
    f = fopen(path, "wb");
    if (!f)
        return -1;
    fwrite(buf, 1, len, f);
    fclose(f);
    return 0;
}

/* ---- pack ---- */
static const char *g_voice_pak;    /* --voice-pak: voice only when asked (keeps the e2e timing) */
static FILE *g_vpak;

static void *p_open(int which, uint32_t *size)
{
    if (which == XE_PACK_MAIN) {
        *size = g_pak_size;
        return g_pak;
    }
    if (!g_voice_pak || !(g_vpak = fopen(g_voice_pak, "rb")))
        return 0;
    fseek(g_vpak, 0, SEEK_END);
    *size = (uint32_t)ftell(g_vpak);
    return g_vpak;
}
static size_t p_read(void *h, uint32_t off, void *dst, size_t n)
{
    if (fseek((FILE *)h, (long)off, SEEK_SET) != 0)
        return 0;
    return fread(dst, 1, n, (FILE *)h);
}
static void p_close(void *h)
{
    if (h && h == g_vpak) {
        fclose(g_vpak);
        g_vpak = 0;
    }
}

/* ---- misc ---- */
/* engine allocation statistics, printed at exit when XE_MEMSTAT is set (the Amiga backend
 * prints the same numbers; this measures the content-dependent part over long scenarios) */
static size_t g_mem_cur, g_mem_peak, g_mem_big;
static unsigned long g_mem_peak_tick;
static void *m_alloc(size_t n, unsigned f)
{
    void *p = malloc(n);
    (void)f;
    if (p) {
        g_mem_cur += n;
        if (n > g_mem_big) g_mem_big = n;
        if (g_mem_cur > g_mem_peak) {
            g_mem_peak = g_mem_cur;
            g_mem_peak_tick = (unsigned long)g_tick;
        }
    }
    return p;
}
static void m_free(void *p, size_t n) { g_mem_cur -= n; free(p); }
static void mem_report(void)
{
    fprintf(stderr, "memory: engine peak %lu bytes at frame %lu, largest block %lu, %lu still allocated\n",
            (unsigned long)g_mem_peak, g_mem_peak_tick, (unsigned long)g_mem_big,
            (unsigned long)g_mem_cur);
}
static void l_log(int level, const char *msg)
{
    if (g_trace || level >= XE_LOG_WARN)
        fprintf(stderr, "[%d] %s\n", level, msg);
}
static void l_fatal(const char *msg)
{
    fprintf(stderr, "fatal: %s\n", msg);
    if (g_control)
        ctl_exit(1, msg);
    exit(1);
}

/* ---- synthetic presentation test (ptest.h) ---- */
static void pt_present(const struct xe_planes *page, const struct xe_rect *dirty, int sw,
                       int scroll, const uint8_t rgb4[48], int on)
{
    (void)page; (void)dirty; (void)sw; (void)scroll; (void)rgb4; (void)on;
}

static void pt_snap(int index, const char *name, const struct xe_planes *page, int scroll,
                    const uint8_t rgb4[48], int on, uint32_t hash)
{
    ctl_present(page, scroll, rgb4, on, 0);
    ctl_emit_snap(name, (uint32_t)index, hash);
}

int main(int argc, char **argv)
{
    struct xe_hal hal;
    struct xe_config cfg;
    const char *pak = "xenon.pak";
    int i;

    memset(&cfg, 0, sizeof cfg);
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--pak") && i + 1 < argc) pak = argv[++i];
        else if (!strcmp(argv[i], "--lang") && i + 1 < argc) {
            cfg.language = xpak_lang_id(argv[++i]);
        } else if (!strcmp(argv[i], "--storage") && i + 1 < argc) g_storage = argv[++i];
        else if (!strcmp(argv[i], "--max-ticks") && i + 1 < argc) g_max_ticks = atol(argv[++i]);
        else if (!strcmp(argv[i], "--autoplay") && i + 1 < argc) {
            char err[64];
            if (autoplay_parse(&g_ap, argv[++i], err, sizeof err) != 0) {
                fprintf(stderr, "%s\n", err);
                return 2;
            }
            g_have_ap = 1;
        } else if (!strcmp(argv[i], "--present-test")) {
            struct pt_io io = { pt_present, pt_snap };
            return pt_run(&io) > 0 ? 0 : 1;
        } else if (!strcmp(argv[i], "--shot-every") && i + 1 < argc) g_shot_every = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--shot-dir") && i + 1 < argc) g_shot_dir = argv[++i];
        else if (!strcmp(argv[i], "--trace")) g_trace = 1;
        else if (!strcmp(argv[i], "--control")) g_control = 1;
        else if (!strcmp(argv[i], "--voice-pak") && i + 1 < argc) g_voice_pak = argv[++i];
        else if (!strcmp(argv[i], "--voice-volume") && i + 1 < argc) cfg.voice_volume = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--voice-strict")) cfg.voice_strict = 1;
        else if (!strcmp(argv[i], "--music-volume") && i + 1 < argc) cfg.music_volume = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--voice-out") && i + 1 < argc) { i++; g_voice_channel = !strcmp(argv[i], "channel") ? 1 : !strcmp(argv[i], "stuck") ? 2 : 0; }
        else {
            fprintf(stderr, "unknown option %s\n", argv[i]);
            return 2;
        }
    }
    if (!g_have_ap)
        autoplay_parse(&g_ap, "ok:30", 0, 0);       /* default: OK every 30 ticks */
    g_pak = fopen(pak, "rb");
    if (!g_pak) {
        perror(pak);
        return 2;
    }
    fseek(g_pak, 0, SEEK_END);
    g_pak_size = (uint32_t)ftell(g_pak);

    memset(&hal, 0, sizeof hal);
    hal.video_open = v_open; hal.video_close = v_close; hal.video_present = v_present;
    hal.frame_wait = t_frame_wait; hal.ticks = t_ticks;
    hal.input_poll = i_poll; hal.input_flush = i_flush;
    hal.audio_open = a_open; hal.audio_close = a_close; hal.audio_space = a_space; hal.audio_push = a_push;
    hal.storage_read = s_read; hal.storage_write = s_write;
    hal.pack_open = p_open; hal.pack_read = p_read; hal.pack_close = p_close;
    hal.mem_alloc = m_alloc; hal.mem_free = m_free; hal.log = l_log; hal.fatal = l_fatal;
    hal.voice_mode = XE_VOICE_MIX;      /* into the (silent) music stream: in time, for tests */
    if (g_voice_channel) {
        hal.voice_mode = XE_VOICE_CHANNEL;
        hal.voice_open = vo_open; hal.voice_close = vo_close; hal.voice_space = vo_space;
        hal.voice_push = vo_push; hal.voice_queued = vo_queued; hal.voice_flush = vo_flush;
    }
    if (g_control) {
        ctl_init();
        hal.trace = ctl_trace;
    }

    if (getenv("XE_MEMSTAT"))
        atexit(mem_report);
    i = xe_run(&hal, &cfg);
    if (g_control)
        ctl_exit(i, "engine returned");
    return i;
}
