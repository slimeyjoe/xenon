/* SDL3 backend (Linux/Wayland and other SDL3 hosts).
 *
 *   xenon [--pak FILE] [--lang ja|en|pl] [--fullscreen] [--scale integer|aspect|fit|stretch]
 *         [--tick fixed|vsync] [--no-audio] [--opn ymfm|lite|lite2] [--rate HZ]
 *         [--autoplay SPEC]  (backends/common/autoplay.h)
 *         [--control] [--storage DIR] [--window WxH] [--max-ticks N]
 *         [--voice-pak FILE] [--no-voice] [--voice-volume 1..100] [--voice-strict]
 *         [--music-volume 1..100]   (the FM chip: music and sound effects; default 50 with voice, else 100)
 *
 * Voice (docs/voice.md): xenon_voice.pak next to the asset pack (or the executable), or in the
 * current directory, is used when present; --voice-pak names another, --no-voice turns it off.
 *
 * --control is the test-automation protocol (backends/control/control.h): the engine runs as
 * fast as the driver lets it, with no audio, input from stdin and events on stdout, and the
 * window is rendered offscreen (SDL's "offscreen" video driver unless SDL_VIDEO_DRIVER says
 * otherwise). The `readback` command returns the pixels the renderer produced, so the test
 * harness can check this presentation path against the engine's planes.
 *
 * Keys: cursor keys / numpad = up/down/left/right, Return/Space = OK, Esc/numpad 0 = cancel,
 * F1 = HELP, Ctrl held = skip waits, Caps Lock = instant text (KANA lock), F11 / Alt+Return =
 * fullscreen, F10 = cycle scale mode, Shift+Esc = emergency quit. Mouse: motion steps the
 * cursor as on the PC-98, left = OK, right = cancel. */
#include <SDL3/SDL.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>                /* _NSGetExecutablePath: macOS has no /proc/self/exe */
#endif
#include "xe.h"
#include "pack/pack.h"
#include "snd/opn_core.h"
#include "autoplay.h"
#include "control.h"

#define TICK_NS 17724193ull           /* 1e9 / 56.42 */

static SDL_Window *g_win;
static SDL_Renderer *g_ren;
static SDL_Texture *g_tex;
static uint32_t *g_rgba;              /* 640x400 converted page */
static int g_scale_mode = XE_SCALE_INTEGER;
static int g_tick_mode = XE_TICK_FIXED;
static uint64_t g_next_tick;
static struct xe_input g_in;          /* accumulated since the last poll */
static int g_fullscreen;
static SDL_AudioStream *g_audio;
static SDL_AudioDeviceID g_adev;       /* opened by us so that the voice can bind a stream too */
static int g_audio_rate;
static SDL_Mutex *g_alock;            /* pull-mode audio: engine sound calls vs the audio thread */
static int16_t g_abuf[4096];
static char g_storage_dir[1024];
static FILE *g_pak;
static uint32_t g_pak_size;
static FILE *g_vpak;
static char g_pak_dir[1024];            /* directory the main pack was found in ("" = cwd) */
static const char *g_voice_arg;         /* --voice-pak */
static int g_no_voice;                  /* --no-voice */
static struct autoplay g_ap;          /* --autoplay (tests); g_frames counts frame_waits */
static int g_autoplay;
static uint32_t g_frames;
static int g_control;                 /* --control: headless, driven over stdin/stdout */
static long g_max_ticks;              /* --max-ticks (control mode), 0 = unlimited */
static int g_win_w = 1280, g_win_h = 800;
static const char *g_storage_arg;

/* ---- video ------------------------------------------------------------------------------- */

/* Page -> texture. g_rgba mirrors the texture; a present converts and uploads only what changed
 * (the engine's dirty rectangle, in byte columns x lines), everything when the page, the scroll
 * line or the palette changed, and nothing at all when the screen is unchanged: then the
 * renderer is not even presented unless the window needs a redraw (expose, resize, scale mode)
 * or the ticks are paced by vsync. At the title menu that is most frames. */
static uint64_t g_bits[4][256];       /* plane k byte -> bit k in each of the 8 pixel lanes */
static uint32_t g_lut[16];
static uint8_t g_last_rgb4[48];
static int g_last_on = -1, g_last_scroll = -1;
static int g_tex_valid;               /* g_rgba / the texture hold the last presented page */
static int g_redraw = 1;              /* present again without new content */

static void conv_init(void)
{
    int k, b, i;
    for (k = 0; k < 4; k++)
        for (b = 0; b < 256; b++) {
            uint64_t v = 0;
            for (i = 0; i < 8; i++)          /* lane i = pixel i from the left (MSB first) */
                if (b & (0x80 >> i))
                    v |= (uint64_t)(1u << k) << (8 * i);
            g_bits[k][b] = v;
        }
}

static int v_open(const struct xe_video_cfg *cfg)
{
    g_fullscreen = cfg->fullscreen;
    g_scale_mode = cfg->scale_mode;
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, cfg->title ? cfg->title : "Xenon");
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, g_win_w);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, g_win_h);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER,
                          SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                          (g_fullscreen ? SDL_WINDOW_FULLSCREEN : 0));
#ifdef __APPLE__
    /* On macOS SDL gives every window an OpenGL (or Metal) backend by default, and the offscreen
     * driver then needs EGL, which macOS does not have ("Could not initialize OpenGL / GLES
     * library"). Headless, the window gets no graphics backend of its own; the renderer (software,
     * see main) draws into it. */
    if (g_control)
        SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_EXTERNAL_GRAPHICS_CONTEXT_BOOLEAN, true);
#endif
    g_win = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);
    if (!g_win) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return -1;
    }
    g_ren = SDL_CreateRenderer(g_win, NULL);
    if (!g_ren) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        return -1;
    }
    SDL_SetRenderVSync(g_ren, g_tick_mode == XE_TICK_VSYNC ? 1 : 0);
    g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, 640, 400);
    if (!g_tex)
        return -1;
    g_rgba = malloc(640 * 400 * sizeof(uint32_t));
    conv_init();
    g_tex_valid = 0;
    g_redraw = 1;
    return g_rgba ? 0 : -1;
}

static void v_close(void)
{
    free(g_rgba);
    if (g_tex) SDL_DestroyTexture(g_tex);
    if (g_ren) SDL_DestroyRenderer(g_ren);
    if (g_win) SDL_DestroyWindow(g_win);
    g_tex = 0; g_ren = 0; g_win = 0;
}

/* destination rectangle for the current scale mode */
static void dest_rect(SDL_FRect *d, int *nearest)
{
    int W, H;
    SDL_GetWindowSizeInPixels(g_win, &W, &H);
    *nearest = 1;
    switch (g_scale_mode) {
    case XE_SCALE_INTEGER: {
        int f = W / 640 < H / 400 ? W / 640 : H / 400;
        if (f < 1) f = 1;
        d->w = (float)(640 * f); d->h = (float)(400 * f);
        break;
    }
    case XE_SCALE_INTEGER_ASPECT: {
        int maxfx = W / 640, maxfy = H / 400, fx, bx = 1, by = 1;
        for (fx = 1; fx <= maxfx; fx++) {
            int fy = (int)(fx * 1.2 + 0.5);
            if (fy <= maxfy && fy >= 1) { bx = fx; by = fy; }
        }
        d->w = (float)(640 * bx); d->h = (float)(400 * by);
        break;
    }
    case XE_SCALE_FIT: {
        float s = (float)W / 640.0f < (float)H / 480.0f ? (float)W / 640.0f : (float)H / 480.0f;
        d->w = 640.0f * s; d->h = 480.0f * s;
        *nearest = 0;
        break;
    }
    default:
        d->w = (float)W; d->h = (float)H;
        *nearest = 0;
        break;
    }
    d->x = ((float)W - d->w) / 2.0f;
    d->y = ((float)H - d->h) / 2.0f;
}

/* clear the window and draw the texture into the destination rectangle (no present) */
static void draw(SDL_FRect *d)
{
    int nearest;
    dest_rect(d, &nearest);
    SDL_SetTextureScaleMode(g_tex, nearest ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR);
    SDL_SetRenderDrawColor(g_ren, 0, 0, 0, 255);
    SDL_RenderClear(g_ren);
    SDL_RenderTexture(g_ren, g_tex, NULL, d);
}

/* convert display rows [y0,y1) x byte columns [x0,x1) into g_rgba and upload them */
static void convert(const struct xe_planes *page, int scroll, int x0, int y0, int x1, int y1)
{
    SDL_Rect r;
    int y, x;
    for (y = y0; y < y1; y++) {
        int sy = (y + scroll) % 400;
        const uint8_t *p0 = page->p[0] + sy * 80, *p1 = page->p[1] + sy * 80;
        const uint8_t *p2 = page->p[2] + sy * 80, *p3 = page->p[3] + sy * 80;
        uint32_t *dst = g_rgba + y * 640 + x0 * 8;
        for (x = x0; x < x1; x++, dst += 8) {
            uint64_t v = g_bits[0][p0[x]] | g_bits[1][p1[x]] | g_bits[2][p2[x]] | g_bits[3][p3[x]];
            dst[0] = g_lut[v & 15];         dst[1] = g_lut[(v >> 8) & 15];
            dst[2] = g_lut[(v >> 16) & 15]; dst[3] = g_lut[(v >> 24) & 15];
            dst[4] = g_lut[(v >> 32) & 15]; dst[5] = g_lut[(v >> 40) & 15];
            dst[6] = g_lut[(v >> 48) & 15]; dst[7] = g_lut[(v >> 56) & 15];
        }
    }
    r.x = x0 * 8; r.y = y0; r.w = (x1 - x0) * 8; r.h = y1 - y0;
    SDL_UpdateTexture(g_tex, &r, g_rgba + y0 * 640 + x0 * 8, 640 * 4);
}

static void v_present(const struct xe_planes *page, const struct xe_rect *dirty, int switched,
                      int scroll, const uint8_t rgb4[48], int on)
{
    int changed = 0;
    SDL_FRect d;

    if (g_control)
        ctl_present(page, scroll, rgb4, on, switched);
    if (!g_tex_valid || switched || !dirty || scroll != g_last_scroll || on != g_last_on ||
        memcmp(rgb4, g_last_rgb4, 48) != 0) {
        int i;
        for (i = 0; i < 16; i++) {
            uint32_t r = rgb4[i * 3] * 17u, g = rgb4[i * 3 + 1] * 17u, b = rgb4[i * 3 + 2] * 17u;
            g_lut[i] = on ? (0xFF000000u | (b << 16) | (g << 8) | r) : 0xFF000000u;
        }
        memcpy(g_last_rgb4, rgb4, 48);
        g_last_on = on;
        g_last_scroll = scroll;
        convert(page, scroll, 0, 0, 80, 400);
        g_tex_valid = changed = 1;
    } else if (!xe_rect_empty(dirty)) {
        int x0 = dirty->x0 < 0 ? 0 : dirty->x0, y0 = dirty->y0 < 0 ? 0 : dirty->y0;
        int x1 = dirty->x1 > 80 ? 80 : dirty->x1, y1 = dirty->y1 > 400 ? 400 : dirty->y1;
        if (x0 < x1 && y0 < y1) {
            /* page row r is displayed at row (r - scroll) mod 400: may wrap around the bottom */
            int d0 = (y0 - scroll + 400) % 400, n = y1 - y0;
            if (d0 + n <= 400) {
                convert(page, scroll, x0, d0, x1, d0 + n);
            } else {
                convert(page, scroll, x0, d0, x1, 400);
                convert(page, scroll, x0, 0, x1, d0 + n - 400);
            }
            changed = 1;
        }
    }
    if (changed || g_redraw || g_tick_mode == XE_TICK_VSYNC) {
        draw(&d);
        SDL_RenderPresent(g_ren);
        g_redraw = 0;
    }
}

/* Control-mode readback (control.h): draw the last uploaded frame again exactly as present()
 * did and read the renderer's pixels back. One sample per source pixel at the centre of its
 * destination block; every destination pixel inside the rectangle is compared with the sample
 * of the source pixel it maps to (nonuniform), everything outside must be black (border_lit). */
static int v_readback(struct ctl_readback *rb)
{
    SDL_FRect d;
    SDL_Surface *s, *c;
    int x, y;
    int x0, y0, w, h;

    draw(&d);
    s = SDL_RenderReadPixels(g_ren, NULL);
    if (!s)
        return -1;
    c = s->format == SDL_PIXELFORMAT_ABGR8888 ? s : SDL_ConvertSurface(s, SDL_PIXELFORMAT_ABGR8888);
    if (!c) {
        SDL_DestroySurface(s);
        return -1;
    }
    x0 = (int)SDL_floorf(d.x + 0.5f); y0 = (int)SDL_floorf(d.y + 0.5f);   /* may be negative */
    w = (int)SDL_floorf(d.w + 0.5f); h = (int)SDL_floorf(d.h + 0.5f);
    rb->win_w = c->w; rb->win_h = c->h;
    rb->dst_x = x0; rb->dst_y = y0; rb->dst_w = w; rb->dst_h = h;
    rb->scale_mode = g_scale_mode;
    rb->linear = g_scale_mode == XE_SCALE_FIT || g_scale_mode == XE_SCALE_STRETCH;
    rb->nonuniform = rb->border_lit = 0;
    for (y = 0; y < 400; y++) {
        int dy = y0 + (int)(((float)y + 0.5f) * (float)h / 400.0f);
        for (x = 0; x < 640; x++) {
            int dx = x0 + (int)(((float)x + 0.5f) * (float)w / 640.0f);
            uint8_t *o = rb->rgb + (y * 640 + x) * 3;
            if (dx >= 0 && dx < c->w && dy >= 0 && dy < c->h) {
                uint32_t px = *(const uint32_t *)((const uint8_t *)c->pixels + dy * c->pitch + dx * 4);
                o[0] = (uint8_t)px; o[1] = (uint8_t)(px >> 8); o[2] = (uint8_t)(px >> 16);
            } else {
                o[0] = o[1] = o[2] = 0;
            }
        }
    }
    for (y = 0; y < c->h; y++) {
        const uint32_t *row = (const uint32_t *)((const uint8_t *)c->pixels + y * c->pitch);
        for (x = 0; x < c->w; x++) {
            uint32_t px = row[x] & 0xFFFFFFu;
            if (x >= x0 && x < x0 + w && y >= y0 && y < y0 + h) {
                int sx = (int)((long)(x - x0) * 640 / w), sy = (int)((long)(y - y0) * 400 / h);
                const uint8_t *o = rb->rgb + (sy * 640 + sx) * 3;
                if (px != ((uint32_t)o[0] | ((uint32_t)o[1] << 8) | ((uint32_t)o[2] << 16)))
                    rb->nonuniform++;
            } else if (px != 0) {
                rb->border_lit++;
            }
        }
    }
    if (c != s)
        SDL_DestroySurface(c);
    SDL_DestroySurface(s);
    return 0;
}

/* ---- input -------------------------------------------------------------------------------- */

static void toggle_fullscreen(void)
{
    g_fullscreen = !g_fullscreen;
    SDL_SetWindowFullscreen(g_win, g_fullscreen ? true : false);
}

static void handle_key(const SDL_KeyboardEvent *k)
{
    SDL_Keymod mod = SDL_GetModState();
    switch (k->scancode) {
    case SDL_SCANCODE_UP: case SDL_SCANCODE_KP_8: g_in.pressed |= XE_IN_UP; break;
    case SDL_SCANCODE_DOWN: case SDL_SCANCODE_KP_2: g_in.pressed |= XE_IN_DOWN; break;
    case SDL_SCANCODE_LEFT: case SDL_SCANCODE_KP_4: g_in.pressed |= XE_IN_LEFT; break;
    case SDL_SCANCODE_RIGHT: case SDL_SCANCODE_KP_6: g_in.pressed |= XE_IN_RIGHT; break;
    case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER:
        if (mod & SDL_KMOD_ALT) { if (!k->repeat) toggle_fullscreen(); }
        else g_in.pressed |= XE_IN_OK;
        break;
    case SDL_SCANCODE_SPACE: g_in.pressed |= XE_IN_OK; break;
    case SDL_SCANCODE_ESCAPE:
        if (mod & SDL_KMOD_SHIFT) g_in.pressed |= XE_IN_EMERGENCY;
        else g_in.pressed |= XE_IN_CANCEL;
        break;
    case SDL_SCANCODE_KP_0: g_in.pressed |= XE_IN_CANCEL; break;
    case SDL_SCANCODE_F1: g_in.pressed |= XE_IN_HELP; break;
    case SDL_SCANCODE_F2: g_in.pressed |= XE_IN_F1; break;
    case SDL_SCANCODE_F3: g_in.pressed |= XE_IN_F2; break;
    case SDL_SCANCODE_F4: g_in.pressed |= XE_IN_F3; break;
    case SDL_SCANCODE_F5: g_in.pressed |= XE_IN_F4; break;
    case SDL_SCANCODE_F6: g_in.pressed |= XE_IN_F5; break;
    case SDL_SCANCODE_F10:
        if (!k->repeat) { g_scale_mode = (g_scale_mode + 1) % 4; g_redraw = 1; }
        break;
    case SDL_SCANCODE_F11:
        if (!k->repeat) toggle_fullscreen();
        break;
    case SDL_SCANCODE_LCTRL: case SDL_SCANCODE_RCTRL: case SDL_SCANCODE_CAPSLOCK:
    case SDL_SCANCODE_LSHIFT: case SDL_SCANCODE_RSHIFT: case SDL_SCANCODE_LALT:
    case SDL_SCANCODE_RALT:
        break;
    default:
        g_in.pressed |= XE_IN_OTHER;
        break;
    }
}

static void pump_events(void)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_EVENT_QUIT:
            g_in.pressed |= XE_IN_QUIT;
            break;
        case SDL_EVENT_WINDOW_EXPOSED: case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: case SDL_EVENT_WINDOW_RESTORED:
        case SDL_EVENT_WINDOW_ENTER_FULLSCREEN: case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
            g_redraw = 1;                 /* the unchanged page still has to be drawn again */
            break;
        case SDL_EVENT_RENDER_TARGETS_RESET: case SDL_EVENT_RENDER_DEVICE_RESET:
            g_tex_valid = 0;              /* texture contents may be gone: upload everything */
            g_redraw = 1;
            break;
        case SDL_EVENT_KEY_DOWN:
            handle_key(&e.key);
            break;
        case SDL_EVENT_MOUSE_MOTION:
            g_in.mouse_dx += (int)e.motion.xrel;
            g_in.mouse_dy += (int)e.motion.yrel;
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (e.button.button == SDL_BUTTON_LEFT) g_in.pressed |= XE_IN_MOUSE_L;
            if (e.button.button == SDL_BUTTON_RIGHT) g_in.pressed |= XE_IN_MOUSE_R;
            break;
        default:
            break;
        }
    }
}

static void i_poll(struct xe_input *in)
{
    SDL_Keymod mod;
    int nkeys;
    const bool *keys;
    pump_events();
    if (g_control) {
        ctl_input(in);
        return;
    }
    mod = SDL_GetModState();
    g_in.ctrl_held = (mod & SDL_KMOD_CTRL) ? 1 : 0;
    g_in.instant_text = (mod & SDL_KMOD_CAPS) ? 1 : 0;
    keys = SDL_GetKeyboardState(&nkeys);
    g_in.any_key_down = 0;
    if (keys) {
        int i;
        for (i = 0; i < nkeys; i++)
            if (keys[i]) { g_in.any_key_down = 1; break; }
    }
    *in = g_in;
    if (g_autoplay)
        autoplay_poll(&g_ap, g_frames, in);
    g_in.pressed = 0;
    g_in.mouse_dx = g_in.mouse_dy = 0;
}

static void i_flush(void)
{
    pump_events();
    g_in.pressed &= XE_IN_QUIT | XE_IN_EMERGENCY;    /* xe_hal.h input_flush: a quit stays */
    g_in.mouse_dx = g_in.mouse_dy = 0;
    if (g_control)
        ctl_input_flush();
    if (g_autoplay)
        autoplay_flush(&g_ap, g_frames);
}

/* ---- time ---------------------------------------------------------------------------------- */

static void t_frame_wait(void)
{
    uint64_t now;
    g_frames++;
    if (g_control) {                  /* no real time: the driver paces the engine */
        if (g_max_ticks && (long)g_frames > g_max_ticks)
            ctl_exit(3, "max ticks reached");
        ctl_tick(g_frames);
        return;
    }
    if (g_tick_mode == XE_TICK_VSYNC)
        return;                       /* RenderPresent already waited for the host vblank */
    now = SDL_GetTicksNS();
    if (g_next_tick == 0 || now > g_next_tick + 10 * TICK_NS)
        g_next_tick = now;            /* (re)synchronise after a stall */
    g_next_tick += TICK_NS;
    if (g_next_tick > now)            /* plain sleep: SDL_DelayPrecise spins the last ms of every tick; */
        SDL_DelayNS(g_next_tick - now); /* the deadlines are absolute, so the jitter does not add up */
}

static uint32_t t_ticks(void)
{
    if (g_control)
        return g_frames;
    return (uint32_t)(SDL_GetTicksNS() / TICK_NS);
}

/* ---- audio (pull mode, xe_hal.h) ----------------------------------------------------------- */

/* Runs on SDL's audio thread each time the device needs data: render exactly what is asked
 * for (`additional` bytes in the stream's input format, i.e. our mono s16), straight from the
 * sequencer. The engine's own sound calls take g_alock, so the game loop can stall for as
 * long as it likes (image decodes, window drags, compositor hiccups) without the music
 * stuttering, as PMD on its timer interrupt did on the PC-98. */
static void SDLCALL a_get(void *ud, SDL_AudioStream *st, int additional, int total)
{
    int frames = (additional + 1) / 2;
    (void)ud; (void)total;
    while (frames > 0) {
        int n = frames > (int)(sizeof g_abuf / sizeof g_abuf[0]) ? (int)(sizeof g_abuf / sizeof g_abuf[0]) : frames;
        xe_audio_render(g_abuf, n);
        SDL_PutAudioStreamData(st, g_abuf, n * 2);
        frames -= n;
    }
}

static int a_open(int rate)
{
    SDL_AudioSpec spec;
    if (rate <= 0)
        return 0;
    g_alock = SDL_CreateMutex();
    if (!g_alock)
        return 0;
    spec.format = SDL_AUDIO_S16;
    spec.channels = 1;
    spec.freq = rate;
    /* SDL_OpenAudioDeviceStream would be simpler, but a device opened that way takes no
     * further streams; the voice binds its own (vo_open) */
    g_adev = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL);
    g_audio = g_adev ? SDL_CreateAudioStream(&spec, NULL) : NULL;
    if (!g_audio || !SDL_SetAudioStreamGetCallback(g_audio, a_get, NULL) ||
        !SDL_BindAudioStream(g_adev, g_audio)) {
        SDL_Log("audio: %s", SDL_GetError());
        if (g_audio)
            SDL_DestroyAudioStream(g_audio);
        if (g_adev)
            SDL_CloseAudioDevice(g_adev);
        g_audio = 0;
        g_adev = 0;
        SDL_DestroyMutex(g_alock);
        g_alock = 0;
        return 0;
    }
    g_audio_rate = rate;
    return rate;
}

/* Shutdown watchdog. SDL's audio backends can hang for good when the default output changes
 * under them (seen with SDL 3.4.20 on PipeWire: the hotplug thread waits forever for the new
 * device while holding the device lock, so SDL_DestroyAudioStream and SDL_Quit block on it).
 * a_close is only called when the engine quits, and saves and progress are already written by
 * then, so if the rest of the shutdown does not finish in time the process just ends. */
#define SHUTDOWN_GRACE_MS 2000

static int SDLCALL shutdown_watchdog(void *unused)
{
    (void)unused;
    SDL_Delay(SHUTDOWN_GRACE_MS);
    fprintf(stderr, "xenon: shutdown still blocked after %d ms (audio device), exiting\n",
            SHUTDOWN_GRACE_MS);
    _exit(0);
}

static void a_close(void)
{
    SDL_Thread *wd = SDL_CreateThread(shutdown_watchdog, "xenon-exit-wd", NULL);

    if (wd)
        SDL_DetachThread(wd);
    if (g_audio)
        SDL_DestroyAudioStream(g_audio);  /* unbinds; stops the callbacks before returning */
    g_audio = 0;
    if (g_adev)
        SDL_CloseAudioDevice(g_adev);
    g_adev = 0;
    if (g_alock)
        SDL_DestroyMutex(g_alock);
    g_alock = 0;
}

static void a_lock(void)
{
    if (g_alock)
        SDL_LockMutex(g_alock);
}

static void a_unlock(void)
{
    if (g_alock)
        SDL_UnlockMutex(g_alock);
}

/* ---- voice (docs/voice.md §5.2: XE_VOICE_CHANNEL) ------------------------------------------ */

/* A second stream bound to the music's device: SDL mixes the bound streams and converts the
 * clips' 22050 Hz to the device rate. The engine pushes from its tick; streams are thread-safe. */
static SDL_AudioStream *g_voice;

static int vo_open(int rate)
{
    SDL_AudioSpec spec;
    SDL_AudioDeviceID dev = g_adev;
    if (!g_audio || rate <= 0)
        return 0;
    spec.format = SDL_AUDIO_S16;
    spec.channels = 1;
    spec.freq = rate;
    g_voice = SDL_CreateAudioStream(&spec, NULL);
    if (!g_voice)
        return 0;
    if (!dev || !SDL_BindAudioStream(dev, g_voice)) {
        SDL_Log("voice: %s", SDL_GetError());
        SDL_DestroyAudioStream(g_voice);
        g_voice = 0;
        return 0;
    }
    return rate;
}

static void vo_close(void)
{
    if (g_voice)
        SDL_DestroyAudioStream(g_voice);
    g_voice = 0;
}

static int vo_queued(void)
{
    int b = g_voice ? SDL_GetAudioStreamQueued(g_voice) : 0;
    return b > 0 ? b / 2 : 0;
}

static int vo_space(void)
{
    return g_voice ? 65536 - vo_queued() : 0;
}

static void vo_push(const int16_t *pcm, int frames)
{
    if (!g_voice)
        return;
    if (frames > 0)
        SDL_PutAudioStreamData(g_voice, pcm, frames * 2);
    else
        SDL_FlushAudioStream(g_voice);  /* end of clip: the resampler's held-back frames too
                                         * (without this SDL_GetAudioStreamQueued stays at 7 frames) */
}

static void vo_flush(void)
{
    if (g_voice)
        SDL_ClearAudioStream(g_voice);
}

/* push-model entry points: unused in pull mode, kept so the HAL table is complete */
static int a_space(void)
{
    return 0;
}

static void a_push(const int16_t *pcm, int frames)
{
    (void)pcm; (void)frames;
}

/* ---- storage (XDG) ------------------------------------------------------------------------- */

static void storage_init(void)
{
    const char *base = getenv("XDG_DATA_HOME");
    if (g_storage_arg)
        snprintf(g_storage_dir, sizeof g_storage_dir, "%s", g_storage_arg);
    else if (base && *base)
        snprintf(g_storage_dir, sizeof g_storage_dir, "%s/xenon", base);
    else
        snprintf(g_storage_dir, sizeof g_storage_dir, "%s/.local/share/xenon",
                 getenv("HOME") ? getenv("HOME") : ".");
    {
        /* mkdir -p */
        char tmp[1024];
        char *p;
        snprintf(tmp, sizeof tmp, "%s", g_storage_dir);
        for (p = tmp + 1; *p; p++)
            if (*p == '/') { *p = 0; mkdir(tmp, 0700); *p = '/'; }
        mkdir(tmp, 0700);
    }
}

static int s_read(const char *key, void *buf, size_t cap, size_t *len)
{
    char path[1200];
    FILE *f;
    snprintf(path, sizeof path, "%s/%s.dat", g_storage_dir, key);
    f = fopen(path, "rb");
    if (!f)
        return -1;
    *len = fread(buf, 1, cap, f);
    fclose(f);
    return 0;
}

static int s_write(const char *key, const void *buf, size_t len)
{
    char path[1200], tmp[1200];
    FILE *f;
    snprintf(path, sizeof path, "%s/%s.dat", g_storage_dir, key);
    snprintf(tmp, sizeof tmp, "%s/%s.tmp", g_storage_dir, key);
    f = fopen(tmp, "wb");
    if (!f)
        return -1;
    if (fwrite(buf, 1, len, f) != len || fclose(f) != 0)
        return -1;
    return rename(tmp, path) == 0 ? 0 : -1;
}

/* ---- pack ---------------------------------------------------------------------------------- */

static FILE *open_sized(const char *path, uint32_t *size)
{
    FILE *f = fopen(path, "rb");
    long n;
    if (!f)
        return 0;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    *size = (uint32_t)n;
    return f;
}

static int has_trailer(FILE *f, uint32_t size)
{
    uint8_t tr[XPKT_TRAILER_SIZE];
    if (size < XPKT_TRAILER_SIZE)
        return 0;
    fseek(f, (long)(size - XPKT_TRAILER_SIZE), SEEK_SET);
    if (fread(tr, 1, XPKT_TRAILER_SIZE, f) != XPKT_TRAILER_SIZE)
        return 0;
    return memcmp(tr, "XPKT", 4) == 0 && memcmp(tr + 12, "XPKT", 4) == 0;
}

static const char *g_pak_arg;
static FILE *g_wav;
static uint32_t g_wav_frames;

static void wav_sink(void *ctx, const int16_t *pcm, int frames)
{
    (void)ctx;
    fwrite(pcm, 2, (size_t)frames, g_wav);
    g_wav_frames += (uint32_t)frames;
}

static void wav_header(FILE *f, uint32_t rate, uint32_t frames)
{
    uint8_t h[44];
    uint32_t data = frames * 2;
    memcpy(h, "RIFF", 4);
    h[4] = (uint8_t)((36 + data)); h[5] = (uint8_t)((36 + data) >> 8);
    h[6] = (uint8_t)((36 + data) >> 16); h[7] = (uint8_t)((36 + data) >> 24);
    memcpy(h + 8, "WAVEfmt ", 8);
    h[16] = 16; h[17] = 0; h[18] = 0; h[19] = 0;
    h[20] = 1; h[21] = 0; h[22] = 1; h[23] = 0;
    h[24] = (uint8_t)rate; h[25] = (uint8_t)(rate >> 8); h[26] = (uint8_t)(rate >> 16); h[27] = (uint8_t)(rate >> 24);
    h[28] = (uint8_t)(rate * 2); h[29] = (uint8_t)((rate * 2) >> 8); h[30] = (uint8_t)((rate * 2) >> 16); h[31] = (uint8_t)((rate * 2) >> 24);
    h[32] = 2; h[33] = 0; h[34] = 16; h[35] = 0;
    memcpy(h + 36, "data", 4);
    h[40] = (uint8_t)data; h[41] = (uint8_t)(data >> 8); h[42] = (uint8_t)(data >> 16); h[43] = (uint8_t)(data >> 24);
    fseek(f, 0, SEEK_SET);
    fwrite(h, 1, 44, f);
}

static void set_pak_dir(const char *path)
{
    const char *slash = strrchr(path, '/');
    size_t n = slash ? (size_t)(slash - path + 1) : 0;
    if (n >= sizeof g_pak_dir)
        n = 0;
    memcpy(g_pak_dir, path, n);
    g_pak_dir[n] = 0;
}

/* the optional voice pack (docs/voice.md §5.1): --voice-pak, else xenon_voice.pak next to the
 * main pack (or next to the executable when the pack is bundled in it), else in the cwd */
static void *p_open_voice(uint32_t *size)
{
    char path[1100];
    if (g_no_voice)
        return 0;
    if (g_voice_arg) {
        g_vpak = open_sized(g_voice_arg, size);
        if (!g_vpak)
            fprintf(stderr, "voice: cannot open %s\n", g_voice_arg);
    } else {
        snprintf(path, sizeof path, "%sxenon_voice.pak", g_pak_dir);
        g_vpak = open_sized(path, size);
        if (!g_vpak && g_pak_dir[0])
            g_vpak = open_sized("xenon_voice.pak", size);
    }
    return g_vpak;
}

/* the executable's own file, symbolic links resolved (the pack may be bundled into it or lie
 * next to it); 0 when the system cannot tell */
static int exe_path(char path[PATH_MAX])
{
#ifdef __APPLE__
    char raw[PATH_MAX];
    uint32_t n = sizeof raw;
    return _NSGetExecutablePath(raw, &n) == 0 && realpath(raw, path) != 0;
#else
    ssize_t n = readlink("/proc/self/exe", path, PATH_MAX - 1);
    if (n <= 0)
        return 0;
    path[n] = 0;
    return 1;
#endif
}

/* the main pack: --pak, else the executable itself when the pack is bundled into it (the
 * tools/xpak.py trailer), else xenon.pak next to the executable, else in the cwd */
static void *p_open(int which, uint32_t *size)
{
    char exe[PATH_MAX], path[1100];
    if (which == XE_PACK_VOICE)
        return p_open_voice(size);
    if (g_pak_arg) {
        g_pak = open_sized(g_pak_arg, &g_pak_size);
        set_pak_dir(g_pak_arg);
    } else {
        if (exe_path(exe)) {
            set_pak_dir(exe);
            g_pak = open_sized(exe, &g_pak_size);
            if (g_pak && !has_trailer(g_pak, g_pak_size)) {
                fclose(g_pak);
                snprintf(path, sizeof path, "%sxenon.pak", g_pak_dir);
                g_pak = open_sized(path, &g_pak_size);
            }
        }
        if (!g_pak) {
            g_pak = open_sized("xenon.pak", &g_pak_size);
            g_pak_dir[0] = 0;
        }
    }
    if (!g_pak)
        return 0;
    *size = g_pak_size;
    return g_pak;
}

static size_t p_read(void *h, uint32_t off, void *dst, size_t n)
{
    if (fseek((FILE *)h, (long)off, SEEK_SET) != 0)
        return 0;
    return fread(dst, 1, n, (FILE *)h);
}

static void p_close(void *h)
{
    fclose((FILE *)h);
}

/* ---- misc ---------------------------------------------------------------------------------- */

static void *m_alloc(size_t n, unsigned f) { (void)f; return malloc(n); }
static void m_free(void *p, size_t n) { (void)n; free(p); }

static void l_log(int level, const char *msg)
{
    if (level >= XE_LOG_INFO)
        fprintf(stderr, "%s\n", msg);
}

static void l_fatal(const char *msg)
{
    fprintf(stderr, "fatal: %s\n", msg);
    if (g_control)
        ctl_exit(1, msg);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Xenon", msg, g_win);
    exit(1);
}

int main(int argc, char **argv)
{
    struct xe_hal hal;
    struct xe_config cfg;
    int i, rc;
    const char *render = 0, *render_song = "xenon_01";
    double render_seconds = 30.0, render_se_at = 0.0;
    int render_se = 0;

    memset(&cfg, 0, sizeof cfg);
    cfg.video.title = "Xenon";
    cfg.audio_rate = 44100;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--pak") && i + 1 < argc) g_pak_arg = argv[++i];
        else if (!strcmp(argv[i], "--lang") && i + 1 < argc) {
            cfg.language = xpak_lang_id(argv[++i]);
        } else if (!strcmp(argv[i], "--fullscreen")) cfg.video.fullscreen = 1;
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc) {
            const char *s = argv[++i];
            cfg.video.scale_mode = !strcmp(s, "aspect") ? XE_SCALE_INTEGER_ASPECT :
                                   !strcmp(s, "fit") ? XE_SCALE_FIT :
                                   !strcmp(s, "stretch") ? XE_SCALE_STRETCH : XE_SCALE_INTEGER;
        } else if (!strcmp(argv[i], "--tick") && i + 1 < argc) {
            g_tick_mode = !strcmp(argv[++i], "vsync") ? XE_TICK_VSYNC : XE_TICK_FIXED;
            cfg.tick_mode = g_tick_mode;
        } else if (!strcmp(argv[i], "--no-audio")) cfg.audio_rate = 0;
        else if (!strcmp(argv[i], "--opn") && i + 1 < argc) {
            const char *o = argv[++i];
            cfg.opn_core = !strncmp(o, "lite", 4) ? XE_OPN_LITE : XE_OPN_YMFM;
            if (!strcmp(o, "lite2"))
                opn_lite_fm_oversample = 2;     /* FM oversampled 2x, as on a fast Amiga */
        } else if (!strcmp(argv[i], "--rate") && i + 1 < argc) {
            if (cfg.audio_rate)
                cfg.audio_rate = atoi(argv[++i]);
            else
                i++;
        } else if (!strcmp(argv[i], "--render-wav") && i + 1 < argc) render = argv[++i];
        else if (!strcmp(argv[i], "--song") && i + 1 < argc) render_song = argv[++i];
        else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) render_seconds = atof(argv[++i]);
        else if (!strcmp(argv[i], "--se") && i + 1 < argc) render_se = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--se-at") && i + 1 < argc) render_se_at = atof(argv[++i]);
        else if (!strcmp(argv[i], "--control")) g_control = 1;
        else if (!strcmp(argv[i], "--voice-pak") && i + 1 < argc) g_voice_arg = argv[++i];
        else if (!strcmp(argv[i], "--no-voice")) g_no_voice = 1;
        else if (!strcmp(argv[i], "--voice-volume") && i + 1 < argc) cfg.voice_volume = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--voice-strict")) cfg.voice_strict = 1;
        else if (!strcmp(argv[i], "--music-volume") && i + 1 < argc) cfg.music_volume = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--autoplay") && i + 1 < argc) {
            char err[64];
            if (autoplay_parse(&g_ap, argv[++i], err, sizeof err) != 0) {
                fprintf(stderr, "%s\n", err);
                return 2;
            }
            g_autoplay = 1;
        }
        else if (!strcmp(argv[i], "--storage") && i + 1 < argc) g_storage_arg = argv[++i];
        else if (!strcmp(argv[i], "--max-ticks") && i + 1 < argc) g_max_ticks = atol(argv[++i]);
        else if (!strcmp(argv[i], "--window") && i + 1 < argc) {
            if (sscanf(argv[++i], "%dx%d", &g_win_w, &g_win_h) != 2 || g_win_w < 1 || g_win_h < 1) {
                fprintf(stderr, "--window: expected WxH\n");
                return 2;
            }
        } else {
            fprintf(stderr, "usage: xenon [--pak FILE] [--lang ja|en|pl] [--fullscreen] "
                            "[--scale integer|aspect|fit|stretch] [--tick fixed|vsync] "
                            "[--no-audio] [--opn ymfm|lite|lite2] [--rate HZ] [--autoplay SPEC]\n"
                            "             [--control] [--storage DIR] [--window WxH] [--max-ticks N]\n"
                            "             [--voice-pak FILE] [--no-voice] [--voice-volume 1..100] [--voice-strict]\n"
                            "             [--music-volume 1..100]\n");
            return 2;
        }
    }

    if (render) {
        uint32_t rate = 0;
        memset(&hal, 0, sizeof hal);
        hal.pack_open = p_open; hal.pack_read = p_read; hal.pack_close = p_close;
        hal.mem_alloc = m_alloc; hal.mem_free = m_free; hal.log = l_log; hal.fatal = l_fatal;
        g_wav = fopen(render, "wb");
        if (!g_wav) {
            perror(render);
            return 1;
        }
        fseek(g_wav, 44, SEEK_SET);
        rc = xe_render_music(&hal, &cfg, render_song, render_seconds, render_se, render_se_at,
                             &rate, wav_sink, 0);
        wav_header(g_wav, rate, g_wav_frames);
        fclose(g_wav);
        fprintf(stderr, "%s: %lu frames at %lu Hz\n", render, (unsigned long)g_wav_frames,
                (unsigned long)rate);
        return rc;
    }

    if (g_control) {
        /* headless: an offscreen window (the environment variable SDL_VIDEO_DRIVER still wins),
         * no audio device, the engine paced by the driver */
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
#ifdef __APPLE__
        /* the offscreen window has no Metal view; left to itself SDL would pick Vulkan through
         * MoltenVK, for the renderer and again for the window surface under a software one.
         * The software renderer into the driver's plain framebuffer needs no GPU (the
         * environment variables SDL_RENDER_DRIVER and SDL_FRAMEBUFFER_ACCELERATION still win). */
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
        SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "0");
#endif
        cfg.audio_rate = 0;
        cfg.tick_mode = g_tick_mode = XE_TICK_FIXED;
    }
    if (!SDL_Init(SDL_INIT_VIDEO | (g_control ? 0 : SDL_INIT_AUDIO))) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    storage_init();

    memset(&hal, 0, sizeof hal);
    hal.video_open = v_open; hal.video_close = v_close; hal.video_present = v_present;
    hal.frame_wait = t_frame_wait; hal.ticks = t_ticks;
    hal.input_poll = i_poll; hal.input_flush = i_flush;
    hal.audio_open = a_open; hal.audio_close = a_close; hal.audio_space = a_space; hal.audio_push = a_push;
    hal.audio_lock = a_lock; hal.audio_unlock = a_unlock;
    if (cfg.audio_rate > 0) {           /* voice needs the audio device (not in --control) */
        hal.voice_mode = XE_VOICE_CHANNEL;
        hal.voice_open = vo_open; hal.voice_close = vo_close; hal.voice_space = vo_space;
        hal.voice_push = vo_push; hal.voice_queued = vo_queued; hal.voice_flush = vo_flush;
    }
    hal.storage_read = s_read; hal.storage_write = s_write;
    hal.pack_open = p_open; hal.pack_read = p_read; hal.pack_close = p_close;
    hal.mem_alloc = m_alloc; hal.mem_free = m_free; hal.log = l_log; hal.fatal = l_fatal;
    if (g_control) {
        ctl_init();
        ctl_set_readback(v_readback);
        hal.trace = ctl_trace;
    }

    rc = xe_run(&hal, &cfg);
    SDL_Quit();
    if (g_control)
        ctl_exit(rc, "engine returned");
    return rc;
}
