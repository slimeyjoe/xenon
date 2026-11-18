/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Eye-blink strip animator (graphics.md §3.5), driven by the frame tick. */
#ifndef XE_GFX_BLINK_H
#define XE_GFX_BLINK_H

void blink_start(int sx, int sy, int dx, int dy, int w, int h);   /* ANIM_START */
void blink_stop(void);                                             /* ANIM_STOP */
void blink_tick(void *unused);           /* once per frame: an lrv_hook_fn (LRV_HOOK_PUMP) */

#endif
