/* Voice playback (docs/voice.md §4): the Windows release's clips on the PC-98 messages.
 *
 * Everything is a no-op unless the backend supports voice (hal->voice_mode) and has a voice
 * pack. The cue tables come from the main pack (XPAK_T_VOICECUE, one per script and language),
 * the clips from the voice pack. */
#ifndef XE_VOICE_VOICE_H
#define XE_VOICE_VOICE_H

#include <stdint.h>

/* The music's default volume (percent) while the voice is on: the FM music masks speech.
 * Chosen by listening, 2026-11-13; cfg.music_volume overrides it. */
#define VOICE_MUSIC_VOLUME 50

void voice_init(void);              /* after snd_init */
void voice_shutdown(void);
int  voice_enabled(void);

void voice_script(const char *name);    /* the script was loaded: its cue table */
void voice_script_unload(void);

/* text_print: a message starts. `src` is the text the printer was given (a cue exists only
 * when it points into the current script); `timed` = the message ends in a timed wait ({W n}).
 * Starts the part-0 clip, unless CTRL is held. */
void voice_message(const uint8_t *src, int timed);
/* text_print: the k-th {KEY} wait of the message has returned; skipped = by CTRL */
void voice_part(int k, int skipped);
/* text_print: before a key prompt or a timed wait: wait while the clip plays. CANCEL cuts it,
 * OK too unless cfg.voice_strict, CTRL held cuts it at once. The clip is over on return. */
void voice_wait(void);

void voice_stop(void);              /* cut whatever plays (script change, menus, quit) */
int  voice_playing(void);

void voice_pump(void);              /* each tick (sys_frame_wait): decode ahead, feed output */
/* XE_VOICE_MIX: add the voice to `frames` samples at `rate`; called by the sound renderer with
 * the audio lock held (pull mode: on the backend's audio thread). */
void voice_mix(int16_t *pcm, int frames, uint32_t rate);

#endif
