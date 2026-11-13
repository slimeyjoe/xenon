/* Eye-blink strip animator (graphics.md §3.5), driven by the frame tick. */
#ifndef XE_GFX_BLINK_H
#define XE_GFX_BLINK_H

void blink_start(int sx, int sy, int dx, int dy, int w, int h);   /* ANIM_START */
void blink_stop(void);                                             /* ANIM_STOP */
void blink_tick(void);                                             /* once per frame */

#endif
