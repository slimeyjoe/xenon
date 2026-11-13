/* Xenon engine - hardware abstraction layer.
 *
 * The engine (engine/) is portable C99 and knows nothing about the host: no libc I/O, no
 * threads, no clocks. Everything it needs from the machine is one of the function pointers in
 * struct xe_hal, filled in by a backend (backends/<name>/). See the design notes in the project
 * plan and docs/re/graphics.md §7 for what the engine models.
 *
 * Threading model: none in the engine. The engine calls frame_wait() from every blocking wait;
 * that is where the backend pumps events, feeds audio and presents. A backend may still use
 * interrupts or threads internally (an audio DMA refill, for instance) as long as the HAL
 * calls remain callable from the engine's single context. The one sanctioned crossing is
 * audio in pull mode (see the audio section): a backend thread calls xe_audio_render() and
 * the engine serializes its own sound calls with audio_lock/audio_unlock. */
#ifndef XE_HAL_H
#define XE_HAL_H

#include <stddef.h>
#include <stdint.h>
#include "gfx/planes.h"

/* ---- video ------------------------------------------------------------------------------ */

struct xe_video_cfg {
    const char *title;
    int fullscreen;        /* SDL: start in borderless fullscreen */
    int scale_mode;        /* XE_SCALE_* (SDL); ignored on Amiga */
    uint32_t mode_id;      /* Amiga display mode id (0 = default) */
};

enum xe_scale_mode {
    XE_SCALE_INTEGER = 0,        /* same integer factor both axes, 1:1 pixels */
    XE_SCALE_INTEGER_ASPECT,     /* independent integer factors approximating the 1.2 CRT aspect */
    XE_SCALE_FIT,                /* fill the window keeping the aspect */
    XE_SCALE_STRETCH             /* fill the whole window */
};

/* ---- input ------------------------------------------------------------------------------ */

/* Edge events (set for one poll when they happened), modelled on platform.md §3.1. */
#define XE_IN_UP       (1u << 0)
#define XE_IN_DOWN     (1u << 1)
#define XE_IN_LEFT     (1u << 2)
#define XE_IN_RIGHT    (1u << 3)
#define XE_IN_OK       (1u << 4)   /* RETURN / SPACE */
#define XE_IN_CANCEL   (1u << 5)   /* ESC / numpad 0 */
#define XE_IN_HELP     (1u << 6)   /* HELP */
#define XE_IN_F1       (1u << 7)   /* f.1 .. f.5 */
#define XE_IN_F2       (1u << 8)
#define XE_IN_F3       (1u << 9)
#define XE_IN_F4       (1u << 10)
#define XE_IN_F5       (1u << 11)
#define XE_IN_OTHER    (1u << 12)  /* any other key press (the engine ignores it, see §3.1 code 6) */
#define XE_IN_QUIT     (1u << 13)  /* host asked to close (window close) */
#define XE_IN_EMERGENCY (1u << 14) /* XFER+ROLL UP+ROLL DOWN equivalent: immediate exit */
#define XE_IN_MOUSE_L  (1u << 15)  /* mouse button press edges */
#define XE_IN_MOUSE_R  (1u << 16)

struct xe_input {
    uint32_t pressed;       /* edge events since the previous poll (OR of XE_IN_*) */
    int ctrl_held;          /* level: CTRL (skip waits) */
    int instant_text;       /* level: KANA-lock equivalent (text appears instantly) */
    int any_key_down;       /* level: at least one key held (for release waits) */
    int mouse_dx, mouse_dy; /* relative motion since the previous poll, in screen pixels */
};

/* ---- trace (test automation) ------------------------------------------------------------ */

/* The engine reports what it is doing through hal->trace when a backend installs one (the
 * null backend's control mode; NULL otherwise, which costs one pointer test per event). The
 * events name engine states a test harness wants to synchronise with: waits for input, messages,
 * menus, asset loads, effect and fade bounds, animation frames. Every event carries the script
 * offset of the statement being executed (pos), so a driver can match it with the bytecode. */
enum xe_trace_kind {
    XE_TR_SCRIPT = 1,   /* name = script; a0 = page (at PAGE), -1 at load */
    XE_TR_PAGE,         /* a0 = page number */
    XE_TR_LABEL,        /* a0 = label id entered after a menu selection */
    XE_TR_FLAG,         /* a0 = flag, a1 = value */
    XE_TR_OP,           /* a0 = opcode, a1..a3 = first int operands, n = int count, name = str */
    XE_TR_MSG,          /* text = message (cp932), a0 = instant flag */
    XE_TR_MSG_END,      /* message fully printed (before the end wait) */
    XE_TR_PROMPT,       /* a0 = 1 overflow prompt, 0 end-of-message prompt */
    XE_TR_MENU,         /* items/n = options, a0 = cursor */
    XE_TR_MENU_SEL,     /* a0 = selection (-1 cancel, -2 help) */
    XE_TR_SYSMENU,      /* a0 = result (0 cancel, 1 title, 2 quit) */
    XE_TR_TITLE_MENU,   /* a0 = rows (3 title, 2 omake) */
    XE_TR_TITLE_SEL,    /* a0 = row (-1 cancel) */
    XE_TR_GDT,          /* name = image, a0 = kind (0 load, 1 buf, 2 diff, 3 page1), a1 = ok */
    XE_TR_GDISP,        /* G_DISP (buffers -> page) */
    XE_TR_EFFECT,       /* a0 = 0 blind, 1 centre-open, 2 waku2 close, 3 dissolve; a1 = 0 start, 1 end */
    XE_TR_FADE,         /* a0 = delay, a1 = 0 start, 1 end */
    XE_TR_ADT,          /* name = animation (load); a0 = -1 load, -2 play start, -3 play end, else frame index; a1 = page */
    XE_TR_BLINK,        /* a0 = -1 start, -2 stop, else strip frame copied */
    XE_TR_WAIT,         /* a0 = kind (XE_WAIT_*), a1 = 0 enter / 1 leave, a2 = ticks requested or reason */
    XE_TR_KEY,          /* a0 = command code consumed by the engine (sys_cmd) */
    XE_TR_SAVE,         /* name = slot */
    XE_TR_LOAD,         /* name = slot, a0 = loaded */
    XE_TR_PROGRESS,     /* a0 = bit */
    XE_TR_ERROR,        /* a0 = script error code */
    XE_TR_BOX,          /* text = error / message box text, a0 = 1 error box */
    XE_TR_SEQ,          /* a0 = 0 logo, 1 opening, 2 staff roll; a1 = 0 start, 1 end */
    XE_TR_QUIT,         /* engine leaving xe_run */
    XE_TR_CLEAR,        /* a0 = 0 VRAM page cleared (a1 = page), 1 off-screen buffers cleared */
    XE_TR_VOICE         /* name = clip, a0 = part, a1 = XE_VOICE_EV_* (docs/voice.md) */
};

enum xe_voice_ev { XE_VOICE_EV_START = 0, XE_VOICE_EV_CUT, XE_VOICE_EV_END };

enum xe_volume { XE_VOL_MUSIC = 0, XE_VOL_VOICE };    /* set_volume (docs/voice.md §6) */

enum xe_wait_kind { XE_WAIT_TICKS = 0, XE_WAIT_KEY, XE_WAIT_OK, XE_WAIT_MUSIC, XE_WAIT_VOICE };
/* reasons for leaving a key wait (a2 of the leave event) */
enum xe_wait_leave { XE_LEAVE_KEY = 0, XE_LEAVE_CTRL, XE_LEAVE_TIMEOUT, XE_LEAVE_QUIT };

struct xe_trace_event {
    int kind;                       /* XE_TR_* */
    uint32_t tick;                  /* engine tick */
    uint32_t pos;                   /* script offset of the current statement, ~0u = none */
    const char *script;             /* current script name */
    const char *name;               /* asset / script / slot name or NULL */
    const uint8_t *text;            /* cp932 text or NULL */
    const uint8_t *const *items;    /* menu options (n of them) or NULL */
    int n;
    int a0, a1, a2, a3;
    const int *ints;                /* XE_TR_OP: all int operands (nints of them) */
    int nints;
};

/* ---- the HAL ---------------------------------------------------------------------------- */

/* ---- packs and voice ------------------------------------------------------------------- */

/* Which pack pack_open is asked for. The voice pack is optional (docs/voice.md §5.1): a backend
 * returns NULL when it has none, does not want one, or the player said --no-voice. */
enum xe_pack_id { XE_PACK_MAIN = 0, XE_PACK_VOICE = 1 };

/* How a backend outputs the voice clips (docs/voice.md §5.2). */
enum xe_voice_mode {
    XE_VOICE_NONE = 0,      /* no voice: the voice pack is not even opened */
    XE_VOICE_MIX,           /* the engine mixes the voice into the music stream it renders */
    XE_VOICE_CHANNEL        /* the backend plays the voice on its own output (voice_* below) */
};

#define XE_MEM_ANY  0u
#define XE_MEM_CHIP 1u   /* Amiga: must be chip RAM (backend-internal use) */

enum xe_log_level { XE_LOG_DEBUG, XE_LOG_INFO, XE_LOG_WARN, XE_LOG_ERROR };

struct xe_hal {
    /* video: 640x400, 16 colours, two pages owned by the engine (gfx/planes.h) */
    int  (*video_open)(const struct xe_video_cfg *cfg);
    void (*video_close)(void);
    /* Present the displayed page. `dirty` is the region (byte columns x lines) changed since
     * the previous present, or NULL for everything; `page_switched` means a different page
     * is now displayed (upload everything). scroll_line rotates the rows (G_SCROLL);
     * rgb4 is 16 x {R,G,B} 0..15; graphics_on = 0 shows black (the overlay is still drawn
     * by the engine into the page before presenting). */
    void (*video_present)(const struct xe_planes *page, const struct xe_rect *dirty,
                          int page_switched, int scroll_line, const uint8_t rgb4[48],
                          int graphics_on);

    /* time: one tick = one PC-98 frame, 56.42 Hz. frame_wait blocks until the next tick. */
    void (*frame_wait)(void);
    uint32_t (*ticks)(void);

    /* input */
    void (*input_poll)(struct xe_input *in);
    /* drops the type-ahead (key and mouse edges, motion) but keeps XE_IN_QUIT and
     * XE_IN_EMERGENCY for the next poll: a quit must never be lost to a flush */
    void (*input_flush)(void);

    /* audio: mono 16-bit PCM. audio_open returns the rate the backend accepted (it may differ
     * from the request) or 0 when audio is unavailable. Two delivery models:
     *  push (audio_lock == NULL): the engine renders from its tick and calls audio_space /
     *    audio_push. Simple, but the sink starves whenever a tick is late, so the backend
     *    must buffer generously.
     *  pull (audio_lock != NULL): the backend's audio thread or interrupt calls
     *    xe_audio_render(pcm, frames) whenever the device needs data; this is how the original
     *    ran PMD, from a timer interrupt independent of the game loop. The engine brackets
     *    every call into the sequencer with audio_lock/audio_unlock (never nested), so the
     *    backend's lock need not be recursive. audio_space/audio_push are then unused. */
    int  (*audio_open)(int rate);
    void (*audio_close)(void);
    int  (*audio_space)(void);                          /* frames the sink can take now */
    void (*audio_push)(const int16_t *pcm, int frames);
    void (*audio_lock)(void);                           /* pull mode only, else NULL */
    void (*audio_unlock)(void);

    /* voice (docs/voice.md §5.2): voice_mode says which of the models below the backend
     * implements; 0 (XE_VOICE_NONE) when the fields are left zero. XE_VOICE_CHANNEL: mono
     * 16-bit PCM pushed from the engine's tick, the backend mixes and resamples as it likes.
     *   voice_open(rate) -> accepted rate (0 = cannot; the engine resamples to a different one)
     *   voice_space()    -> frames that can be pushed now
     *   voice_push(p, 0) -> end of the clip: play out anything held back (a resampler's
     *                       lookahead: SDL keeps a few frames until its stream is flushed)
     *   voice_queued()   -> frames pushed but not yet played (the clip has ended at 0)
     *   voice_flush()    -> drop everything queued at once (a clip is cut) */
    int  voice_mode;
    /* CHANNEL: how much the backend wants queued ahead, in ms (0 = the engine's default, about
     * 0.19 s); a backend that plays in large blocks needs more than its blocks in flight */
    int  voice_lead_ms;
    int  (*voice_open)(int rate);
    void (*voice_close)(void);
    int  (*voice_space)(void);
    void (*voice_push)(const int16_t *pcm, int frames);
    int  (*voice_queued)(void);
    void (*voice_flush)(void);

    /* Optional: the backend applies a volume in its output instead of the engine scaling the
     * samples (Paula's AUDxVOL, AHI's ahir_Volume: no CPU and, on an 8-bit output, no bits
     * lost). which = XE_VOL_MUSIC (the FM chip: music and effects) or XE_VOL_VOICE, percent
     * 1..100. Returns 1 if the backend applies it, 0 if the engine is to scale the samples.
     * May be called before audio_open / voice_open: the backend keeps the value. With
     * XE_VOICE_MIX the voice is inside the music stream, so neither can be done in hardware. */
    int  (*set_volume)(int which, int percent);

    /* storage: small named records (saves, progress, settings). Return 0 on success. */
    int  (*storage_read)(const char *key, void *buf, size_t cap, size_t *len);
    int  (*storage_write)(const char *key, const void *buf, size_t len);

    /* pack: opaque source the engine reads ranges from (pack/pack.h). pack_open returns a
     * handle and the byte size of the source (an executable with a trailer, or a .pak), or
     * NULL. `which` is XE_PACK_MAIN or XE_PACK_VOICE (optional, see above). */
    void *(*pack_open)(int which, uint32_t *size);
    size_t (*pack_read)(void *h, uint32_t off, void *dst, size_t n);
    void (*pack_close)(void *h);

    /* memory and diagnostics */
    void *(*mem_alloc)(size_t n, unsigned flags);
    void (*mem_free)(void *p, size_t n);
    void (*log)(int level, const char *msg);
    void (*fatal)(const char *msg);                     /* show the message, exit */

    /* test automation: engine events (see xe_trace_kind), or NULL */
    void (*trace)(const struct xe_trace_event *ev);
};

#endif
