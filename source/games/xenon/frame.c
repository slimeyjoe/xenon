/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Xenon's WINDOW 1 (script opcode 11h): the CG frame closes over the picture, drawn from the
 * frame picture WAKU2 over the fixed CG rectangle of its AGS.EXE (graphics.md §3.2). A native
 * module of the hook XE_HOOK_WINDOW_CLOSE. */
#include "xe.h"
#include "gfx/gfx.h"
#include "xenon.h"

#define STRIDE XE_STRIDE

/* fx_waku2_close 0C2A:07F0: load WAKU2 into the buffers, then over the fixed CG rect
 * (x 40, y 4, 70 bytes, 296 lines) copy rows from the top and the bottom towards the centre,
 * 8 rows per frame (37 steps). */
void xenon_frame_close(int arg)
{
    const int off = 4 * STRIDE + 5, wb = 70, h = 296;
    int top = 0, bot = h - 1, s;
    (void)arg;
    gdt_load_buf("waku2");
    lrv_sys_trace(&xe.sys, XE_TR_EFFECT, 0, 0, 2, 0, 296, 0);
    for (s = 0; s < 37; s++) {
        int k;
        for (k = 0; k < 4; k++) {
            if (top <= bot) {
                gfx_show_rows(off + top * STRIDE, wb, 1);
                top++;
            }
            if (bot >= top) {
                gfx_show_rows(off + bot * STRIDE, wb, 1);
                bot--;
            }
        }
        wait_vsync(1);
    }
    lrv_sys_trace(&xe.sys, XE_TR_EFFECT, 0, 0, 2, 1, 0, 0);
}
