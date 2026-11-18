/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The frame and the player's input as the original reads them (platform.md §3, §4).
 *
 * The original's keyboard handler turns keys into commands (OK, cancel, the cursor, HELP,
 * f.1-f.5, "other") and its mouse handler turns relative motion and the buttons into the same
 * commands. Here lrvne's frame (lrv_sys) delivers host-neutral keys by position; which key is
 * which command is decided in key_cmd below, the same for every backend:
 *
 *   cursor keys, keypad 8 2 4 6    up, down, left, right
 *   Return, keypad Enter, Space    OK
 *   Esc, keypad 0                  cancel
 *   Help, F1                       HELP (the PC-98's HELP key)
 *   F2 .. F6                       f.1 .. f.5
 *   any other key                  "other" (read and ignored, as by the original)
 *   Ctrl held                      skip: waits end at once
 *   Caps or kana lock on           the text of a page appears at once (the PC-98's KANA lock)
 *
 * The mouse counts by its motion only: 20 pixels up or down step the highlight, left button
 * OK, right button cancel. */
#include "xe.h"

struct xe_state xe;

/* the command of a key press, XE_CMD_NONE for the keys that are only levels */
static int key_cmd(int key)
{
    switch (key) {
    case LRV_K_UP: case LRV_K_KP8:                          return XE_CMD_UP;
    case LRV_K_DOWN: case LRV_K_KP2:                        return XE_CMD_DOWN;
    case LRV_K_LEFT: case LRV_K_KP4:                        return XE_CMD_LEFT;
    case LRV_K_RIGHT: case LRV_K_KP6:                       return XE_CMD_RIGHT;
    case LRV_K_RETURN: case LRV_K_KP_ENTER: case LRV_K_SPACE: return XE_CMD_OK;
    case LRV_K_ESC: case LRV_K_KP0:                         return XE_CMD_CANCEL;
    case LRV_K_HELP: case LRV_K_F1:                         return XE_CMD_HELP;
    case LRV_K_F2: case LRV_K_F3: case LRV_K_F4: case LRV_K_F5: case LRV_K_F6:
        return XE_CMD_F1 + (key - LRV_K_F2);
    case LRV_K_SHIFT: case LRV_K_CTRL: case LRV_K_ALT: case LRV_K_CAPS: case LRV_K_KANA:
        return XE_CMD_NONE;
    default:
        return XE_CMD_OTHER;
    }
}

int quitting(void)
{
    return lrv_sys_quitting(&xe.sys);
}

int ctrl_held(void)
{
    return (lrv_sys_mods(&xe.sys) & LRV_MOD_CTRL) != 0;
}

void frame_wait(void)
{
    lrv_sys_frame_wait(&xe.sys);
    if (quitting()) {
        /* leaving: the loop that called us (a fade, an effect, a timed wait) runs out at CPU
         * speed, and the script stops after the statement it is in */
        xe.vm.leave = xe.vm.quit = 1;
        return;
    }
}

int kbd_read_cmd(void)
{
    struct lrv_keypress k;
    while (lrv_sys_key_read(&xe.sys, &k)) {
        int c = key_cmd(k.key);
        if (c != XE_CMD_NONE)
            return c;
    }
    return XE_CMD_NONE;
}

void kbd_flush(void)
{
    lrv_sys_key_flush(&xe.sys);
    xe.sys.motion_x = xe.sys.motion_y = 0;
}

/* input_get_cmd 0AF8:02C7 without its wait: the keyboard first, then the mouse */
static int get_cmd_now(void)
{
    int k = kbd_read_cmd();
    if (k != XE_CMD_NONE) {
        xe.input_from_mouse = 0;
        return k;
    }
    xe.input_from_mouse = 1;
    if (xe.sys.motion_y > 20)  { xe.sys.motion_y = 0; return XE_CMD_DOWN; }
    if (xe.sys.motion_y < -20) { xe.sys.motion_y = 0; return XE_CMD_UP; }
    if (xe.sys.motion_x > 20)  { xe.sys.motion_x = 0; return XE_CMD_RIGHT; }
    if (xe.sys.motion_x < -20) { xe.sys.motion_x = 0; return XE_CMD_LEFT; }
    if (lrv_sys_mouse_pressed(&xe.sys, LRV_EV_MOUSE_L))
        return XE_CMD_OK;
    if (lrv_sys_mouse_pressed(&xe.sys, LRV_EV_MOUSE_R))
        return XE_CMD_CANCEL;
    return XE_CMD_NONE;
}

int poll_cmd(void)
{
    return get_cmd_now();
}

/* input_get_cmd 0AF8:02C7 (platform.md §3.2) */
int input_get_cmd(void)
{
    int k, waited = 0;
    xe.sys.motion_x = xe.sys.motion_y = 0;  /* mouse_set_pos(100,100) */
    lrv_sys_mouse_pressed(&xe.sys, LRV_EV_MOUSE_L | LRV_EV_MOUSE_R);
    for (;;) {
        k = get_cmd_now();
        if (k != XE_CMD_NONE)
            break;
        if (quitting()) {
            k = XE_CMD_CANCEL;
            break;
        }
        if (!waited) {
            waited = 1;
            lrv_sys_trace(&xe.sys, LRV_TR_WAIT, 0, 0, XE_WAIT_KEY, 0, 0, 0);
        }
        frame_wait();
    }
    if (waited)
        lrv_sys_trace(&xe.sys, LRV_TR_WAIT, 0, 0, XE_WAIT_KEY, 1,
                      quitting() ? XE_LEAVE_QUIT : XE_LEAVE_KEY, 0);
    lrv_sys_trace(&xe.sys, XE_TR_KEY, 0, 0, k, 0, 0, 0);
    return k;
}

/* wait_ok_key 0AF8:0254 (platform.md §3.3). Returns 1 when the wait was skipped by CTRL. */
int wait_ok_key(void)
{
    int r = 0;
    kbd_flush();
    lrv_sys_trace(&xe.sys, LRV_TR_WAIT, 0, 0, XE_WAIT_OK, 0, 0, 0);
    for (;;) {
        int k;
        if (ctrl_held()) {
            r = 1;
            break;
        }
        if (quitting())
            break;
        k = input_get_cmd();
        if (k == XE_CMD_OK || k == XE_CMD_CANCEL)
            break;
    }
    lrv_sys_trace(&xe.sys, LRV_TR_WAIT, 0, 0, XE_WAIT_OK, 1,
                  r ? XE_LEAVE_CTRL : quitting() ? XE_LEAVE_QUIT : XE_LEAVE_KEY, 0);
    return r;
}

/* wait_vsync 0DCA:0012: n frames; CTRL held ends the wait early. */
void wait_vsync(int n)
{
    int i;
    int loud = n > 1 && !xe.trace_quiet;
    if (loud)
        lrv_sys_trace(&xe.sys, LRV_TR_WAIT, 0, 0, XE_WAIT_TICKS, 0, n, 0);
    for (i = 0; i < n && !quitting(); i++) {
        frame_wait();
        if (ctrl_held())
            break;
    }
    if (loud)
        lrv_sys_trace(&xe.sys, LRV_TR_WAIT, 0, 0, XE_WAIT_TICKS, 1,
                      quitting() ? XE_LEAVE_QUIT : i < n ? XE_LEAVE_CTRL : XE_LEAVE_TIMEOUT, i);
}
