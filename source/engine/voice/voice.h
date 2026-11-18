/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Voice (docs/voice.md §4): the Windows release's clips on the PC-98 messages.
 *
 * The clips are played by lrvne's voice player (xe.voice, lrvne/voice.h); this module keeps
 * the cue table of the script (which clip belongs to which message, LRV_PT_VOICECUE entries of
 * the main pack, one per script and language) and decides when to play, wait and cut.
 * Everything is a no-op unless the backend has a voice output and a voice pack. */
#ifndef XE_VOICE_VOICE_H
#define XE_VOICE_VOICE_H

#include <stdint.h>

void voice_init(void);              /* after snd_init */
void voice_shutdown(void);
int  voice_enabled(void);

void voice_script(const char *name);    /* the script was loaded: its cue table */
void voice_script_unload(void);

/* text_print: a message starts. `src` is the text the printer was given (a cue exists only
 * when it points into the current script). Starts the part-0 clip, unless CTRL is held. */
void voice_message(const uint8_t *src);
/* text_print: the k-th {KEY} wait of the message has returned; skipped = by CTRL */
void voice_part(int k, int skipped);
/* text_print: before a key prompt or a timed wait: wait while the clip plays. CANCEL cuts it,
 * OK too unless --voice-strict, CTRL held cuts it at once. The clip is over on return. */
void voice_wait(void);

void voice_stop(void);              /* cut whatever plays (script change, menus, quit) */

#endif
