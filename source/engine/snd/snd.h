/* Engine sound API (sound.md §3) - the layer above the PMD sequencer and the OPN core. */
#ifndef XE_SND_SND_H
#define XE_SND_SND_H

void snd_init(void);
void snd_shutdown(void);
void snd_pump(void);                    /* render as much audio as the sink can take */

void music_load(const char *name);      /* MUSIC_LOAD */
void music_play(void);                  /* MUSIC_PLAY */
void music_stop(void);                  /* MUSIC_STOP */
void music_fade(int speed);             /* MUSIC_FADE */
void music_fade_stop_wait(void);        /* MUSIC_FADEWAIT */
void music_set_fadevol(int v);          /* MUSIC_VOL */
void music_wait_status(int n);          /* MUSIC_WAIT */
const char *music_get_name(void);
int  music_started(void);
void se_play(int n);                    /* SE_PLAY */
void se_stop(void);                     /* SE_STOP */
struct pmd *snd_sequencer(void);        /* tests/tools */
/* The audio lock (xe_hal.h pull mode; no-ops in push mode) for other modules that share state
 * with the renderer (voice/voice.c). Never nested, never taken inside the renderer. */
void snd_lock(void);
void snd_unlock(void);
void snd_update_gain(void);              /* cfg.music_volume or its default (voice on or off) */

#endif
