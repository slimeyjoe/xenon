/* Xenon engine - public entry point for the backends. */
#ifndef XE_XE_H
#define XE_XE_H

#include "xe_hal.h"

struct xe_config {
    struct xe_video_cfg video;
    int language;          /* XPAK_L_JA / _EN / _PL (xpak_lang_id); 0 = pack default */
    int audio_rate;        /* requested output rate, 0 = audio off */
    int opn_core;          /* XE_OPN_* */
    int tick_mode;         /* XE_TICK_* */
    int fix_timing;        /* 0 = 56.42 Hz reference (always, for now) */
    const char *autoplay;  /* test automation script or NULL */
    const char *screenshot_dir; /* where --screenshot-at images go (null backend) */
    int music_volume;      /* FM output (music and effects) in percent, 1..100; 0 = default:
                              VOICE_MUSIC_VOLUME while the voice plays, else 100 */
    int voice_volume;      /* voice gain in percent, 1..100; 0 = default (100) */
    int voice_strict;      /* 1 = only CANCEL cuts a playing clip, as in the Windows release */
};

enum xe_opn_core { XE_OPN_DEFAULT = 0, XE_OPN_YMFM, XE_OPN_LITE };
enum xe_tick_mode { XE_TICK_FIXED = 0, XE_TICK_VSYNC };

/* Offline tool: render `seconds` of music `name` (and optionally effect `se` started at `se_at`
 * seconds) through the sequencer and OPN core, calling sink(ctx, pcm, frames) with mono 16-bit
 * at *rate. Returns 0 on success. Needs only the pack and memory parts of the HAL. */
int xe_render_music(const struct xe_hal *hal, const struct xe_config *cfg, const char *name,
                    double seconds, int se, double se_at, uint32_t *rate,
                    void (*sink)(void *ctx, const int16_t *pcm, int frames), void *ctx);

/* Run the whole game: title screen to quit. Returns the process exit code. */
int xe_run(const struct xe_hal *hal, const struct xe_config *cfg);

/* Pull-mode audio (xe_hal.h): render `frames` mono 16-bit samples at the rate audio_open
 * accepted. Called by the backend from its audio thread/interrupt; takes audio_lock. */
void xe_audio_render(int16_t *pcm, int frames);

/* The same for a backend that cannot render in real time (Amiga CPU budget): the sequencer
 * and the chip's register state advance by `frames` samples, so the script's music cues stay
 * on time, but the chip core is not run and pcm gets silence. Takes audio_lock. */
void xe_audio_skip(int16_t *pcm, int frames);

#endif
