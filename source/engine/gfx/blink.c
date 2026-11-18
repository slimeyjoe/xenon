/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Eye-blink animator 0ED8:0270 / 036F / 03CB (graphics.md §3.5). The original hooks the
 * VSYNC interrupt; here blink_tick() is a hook of the frame (lrv_sys), which keeps it
 * running during script waits, typing and menus exactly as the interrupt did. */
#include "xe.h"
#include "gfx/gfx.h"
#include "gfx/blink.h"

/* DS:0B14: frames to wait before each copy, state 0..13 (after 13 -> 2) */
static const int blink_table[14] = { 0, 1, 1, 6, 3, 300, 1, 6, 3, 3, 1, 6, 3, 300 };

void blink_start(int sx, int sy, int dx, int dy, int w, int h)
{
    struct xe_blink *b = &xe.blink;
    b->sx = sx; b->sy = sy; b->dx = dx; b->dy = dy; b->w = w; b->h = h;
    b->step = 0;
    b->counter = 0;
    b->frame = 0;
    b->dir = 1;
    b->active = 1;
    lrv_sys_trace(&xe.sys, XE_TR_BLINK, 0, 0, -1, 0, 0, 0);
}

void blink_stop(void)
{
    if (xe.blink.active)
        lrv_sys_trace(&xe.sys, XE_TR_BLINK, 0, 0, -2, 0, 0, 0);
    xe.blink.active = 0;
    xe.blink.frame = 0;
    xe.blink.step = 0;
}

void blink_tick(void *unused)
{
    struct xe_blink *b = &xe.blink;
    int src, dst;
    (void)unused;
    if (!b->active)
        return;
    if (blink_table[b->step] > b->counter) {
        b->counter++;
        return;
    }
    /* copy frame `frame` of the strip (rows sy + frame*h ..) to (dx, dy) on the access page */
    src = (b->sy + b->frame * b->h) * XE_STRIDE + b->sx;
    dst = b->dy * XE_STRIDE + b->dx;
    {
        struct lrv_surface *pg = lrv_screen_access(&xe.screen), *buf = XE_GBUF;
        int p, y;
        for (p = 0; p < 4; p++) {
            for (y = 0; y < b->h; y++) {
                int s = src + y * XE_STRIDE, d = dst + y * XE_STRIDE;
                int x;
                if (s + b->w > XE_PLANE_BYTES || d + b->w > XE_PLANE_BYTES)
                    break;
                for (x = 0; x < b->w; x++)
                    pg->plane[p][d + x] = buf->plane[p][s + x];
            }
        }
        lrv_screen_mark_off(&xe.screen, xe.screen.access_page, dst, b->w, b->h);
    }
    lrv_sys_trace(&xe.sys, XE_TR_BLINK, 0, 0, b->frame, b->step, b->dx, b->dy);
    b->counter = 0;
    b->step++;
    if (b->step > 13)
        b->step = 2;
    b->frame += b->dir ? 1 : -1;
    if (b->frame > 2) {
        b->dir = 0;
        b->frame = 1;
    }
    if (b->frame < 0) {
        b->dir = 1;
        b->frame = 1;
    }
}
