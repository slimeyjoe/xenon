/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Engine sound API (sound.md §3) - the layer above lrvne's PMD module (snd/snd.c). Calls into
 * the driver's state are bracketed with the mixer's lock (lrvne/mixer.h) inside. */
#ifndef XE_SND_SND_H
#define XE_SND_SND_H

void snd_init(void);                    /* the sound path, after the pack is open */
void snd_shutdown(void);
void snd_update_gain(void);             /* --volume or its default (voice on or off) */

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

#endif
