/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Xenon ～夢幻の肢体～ (C's Ware, 1994): the native modules of the game, the routines of its
 * AGS.EXE that are no interpreted script (engine/mod/mod.h). Its profile (profile.txt)
 * enables them: modules=xenon_logo,xenon_opening,xenon_staff_roll,xenon_frame_close. */
#ifndef XENON_GAME_H
#define XENON_GAME_H

/* registers the modules with the engine */
void xenon_register(void);

/* sequences.c: segment 0E31 */
void xenon_logo(int arg);
void xenon_opening(int n);
void xenon_staff_roll(int arg);
/* frame.c: WINDOW 1 */
void xenon_frame_close(int arg);

#endif
