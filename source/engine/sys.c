/* System layer: the single blocking point of the engine (frame ticks), presentation with the
 * text overlay, the input queue and diagnostics. Everything here is a thin layer over the HAL. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "xe_internal.h"
#include "gfx/gfx.h"
#include "gfx/blink.h"
#include "text/text.h"
#include "snd/snd.h"
#include "voice/voice.h"
#include "sys.h"

struct xe_state xe;

/* ---- diagnostics ------------------------------------------------------------------------- */

void sys_log(int level, const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    xe.hal->log(level, buf);
}

void sys_trace(int kind, const char *name, const uint8_t *text, int a0, int a1, int a2, int a3)
{
    struct xe_trace_event ev;
    if (!xe.hal->trace)
        return;
    ev.kind = kind;
    ev.tick = xe.tick;
    ev.pos = xe.vm.buf ? xe.vm.pos : ~0u;
    ev.script = xe.vm.script;
    ev.name = name;
    ev.text = text;
    ev.items = 0;
    ev.n = 0;
    ev.a0 = a0; ev.a1 = a1; ev.a2 = a2; ev.a3 = a3;
    ev.ints = 0;
    ev.nints = 0;
    xe.hal->trace(&ev);
}

void sys_trace_items(int kind, const uint8_t *const *items, int n, int a0)
{
    struct xe_trace_event ev;
    if (!xe.hal->trace)
        return;
    ev.kind = kind;
    ev.tick = xe.tick;
    ev.pos = xe.vm.buf ? xe.vm.pos : ~0u;
    ev.script = xe.vm.script;
    ev.name = 0;
    ev.text = 0;
    ev.items = items;
    ev.n = n;
    ev.a0 = a0; ev.a1 = xe.in_sysmenu; ev.a2 = ev.a3 = 0;
    ev.ints = 0;
    ev.nints = 0;
    xe.hal->trace(&ev);
}

void sys_fatal(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    xe.hal->fatal(buf);
}

void *sys_alloc(size_t n)
{
    void *p = xe.hal->mem_alloc(n ? n : 1, XE_MEM_ANY);
    if (!p)
        sys_fatal("out of memory (%lu bytes)", (unsigned long)n);
    memset(p, 0, n);
    return p;
}

void sys_free(void *p, size_t n)
{
    if (p)
        xe.hal->mem_free(p, n ? n : 1);
}

/* ---- input queue --------------------------------------------------------------------------- */

#define INQ_SIZE 32
static int inq[INQ_SIZE];
static int inq_head, inq_tail;

static void inq_push(int cmd)
{
    int next = (inq_tail + 1) % INQ_SIZE;
    if (next == inq_head)
        return;                         /* full: drop (the BIOS buffer overflows too) */
    inq[inq_tail] = cmd;
    inq_tail = next;
}

static void poll_input(void)
{
    struct xe_input in;
    xe.hal->input_poll(&in);
    xe.in.ctrl_held = in.ctrl_held;
    xe.in.instant_text = in.instant_text;
    xe.in.any_key_down = in.any_key_down;
    xe.in.mouse_dx += in.mouse_dx;
    xe.in.mouse_dy += in.mouse_dy;
    xe.in.pressed |= in.pressed & (XE_IN_MOUSE_L | XE_IN_MOUSE_R | XE_IN_QUIT | XE_IN_EMERGENCY);
    /* keyboard edges become command codes (platform.md §3.1) in a fixed priority order */
    if (in.pressed & XE_IN_CANCEL) inq_push(SYS_CMD_CANCEL);
    if (in.pressed & XE_IN_UP)     inq_push(SYS_CMD_UP);
    if (in.pressed & XE_IN_DOWN)   inq_push(SYS_CMD_DOWN);
    if (in.pressed & XE_IN_LEFT)   inq_push(SYS_CMD_LEFT);
    if (in.pressed & XE_IN_RIGHT)  inq_push(SYS_CMD_RIGHT);
    if (in.pressed & XE_IN_OK)     inq_push(SYS_CMD_OK);
    if (in.pressed & XE_IN_HELP)   inq_push(SYS_CMD_HELP);
    if (in.pressed & XE_IN_F1)     inq_push(SYS_CMD_F1);
    if (in.pressed & XE_IN_F2)     inq_push(SYS_CMD_F1 + 1);
    if (in.pressed & XE_IN_F3)     inq_push(SYS_CMD_F1 + 2);
    if (in.pressed & XE_IN_F4)     inq_push(SYS_CMD_F1 + 3);
    if (in.pressed & XE_IN_F5)     inq_push(SYS_CMD_F1 + 4);
    if (in.pressed & XE_IN_OTHER)  inq_push(SYS_CMD_OTHER);
    if (in.pressed & XE_IN_QUIT)
        xe.quit_requested = 1;
    if (in.pressed & XE_IN_EMERGENCY)
        xe.emergency_quit = 1;
    if (sys_quitting())
        xe.vm.leave = xe.vm.quit = 1;           /* the script stops after this statement */
}

int sys_quitting(void)
{
    return xe.quit_requested || xe.emergency_quit;
}

int sys_kbd_read_cmd(void)
{
    int c;
    if (inq_head == inq_tail)
        return SYS_CMD_NONE;
    c = inq[inq_head];
    inq_head = (inq_head + 1) % INQ_SIZE;
    return c;
}

void sys_kbd_flush(void)
{
    xe.hal->input_flush();
    poll_input();
    inq_head = inq_tail = 0;
    xe.in.pressed = 0;
    xe.in.mouse_dx = xe.in.mouse_dy = 0;
}

int sys_ctrl_held(void)
{
    return xe.in.ctrl_held;
}

/* input_get_cmd 0AF8:02C7: blocking unified input (platform.md §3.2). */
static int get_cmd_now(void)
{
    int k = sys_kbd_read_cmd();
    if (k != SYS_CMD_NONE) {
        xe.input_from_mouse = 0;
        return k;
    }
    xe.input_from_mouse = 1;
    if (xe.in.mouse_dy > 20)  { xe.in.mouse_dy = 0; return SYS_CMD_DOWN; }
    if (xe.in.mouse_dy < -20) { xe.in.mouse_dy = 0; return SYS_CMD_UP; }
    if (xe.in.mouse_dx > 20)  { xe.in.mouse_dx = 0; return SYS_CMD_RIGHT; }
    if (xe.in.mouse_dx < -20) { xe.in.mouse_dx = 0; return SYS_CMD_LEFT; }
    if (xe.in.pressed & XE_IN_MOUSE_L) { xe.in.pressed &= ~XE_IN_MOUSE_L; return SYS_CMD_OK; }
    if (xe.in.pressed & XE_IN_MOUSE_R) { xe.in.pressed &= ~XE_IN_MOUSE_R; return SYS_CMD_CANCEL; }
    return SYS_CMD_NONE;
}

int sys_poll_cmd(void)
{
    return get_cmd_now();
}

int sys_input_get_cmd(void)
{
    int k, waited = 0;
    xe.in.mouse_dx = xe.in.mouse_dy = 0;        /* mouse_set_pos(100,100) */
    xe.in.pressed &= ~(XE_IN_MOUSE_L | XE_IN_MOUSE_R);
    for (;;) {
        k = get_cmd_now();
        if (k != SYS_CMD_NONE)
            break;
        if (xe.quit_requested || xe.emergency_quit) {
            k = SYS_CMD_CANCEL;
            break;
        }
        if (!waited) {
            waited = 1;
            sys_trace(XE_TR_WAIT, 0, 0, XE_WAIT_KEY, 0, 0, 0);
        }
        sys_frame_wait();
    }
    if (waited)
        sys_trace(XE_TR_WAIT, 0, 0, XE_WAIT_KEY, 1,
                  (xe.quit_requested || xe.emergency_quit) ? XE_LEAVE_QUIT : XE_LEAVE_KEY, 0);
    sys_trace(XE_TR_KEY, 0, 0, k, 0, 0, 0);
    return k;
}

/* wait_ok_key 0AF8:0254 (platform.md §3.3). Returns 1 when the wait was skipped by CTRL. */
int sys_wait_ok_key(void)
{
    int r = 0;
    sys_kbd_flush();
    sys_trace(XE_TR_WAIT, 0, 0, XE_WAIT_OK, 0, 0, 0);
    for (;;) {
        int k;
        if (sys_ctrl_held()) {
            r = 1;
            break;
        }
        if (xe.emergency_quit || xe.quit_requested)
            break;
        k = sys_input_get_cmd();
        if (k == SYS_CMD_OK || k == SYS_CMD_CANCEL)
            break;
    }
    sys_trace(XE_TR_WAIT, 0, 0, XE_WAIT_OK, 1,
              r ? XE_LEAVE_CTRL : (xe.quit_requested || xe.emergency_quit) ? XE_LEAVE_QUIT : XE_LEAVE_KEY, 0);
    return r;
}

/* ---- ticks and presentation ---------------------------------------------------------------- */

static void present(void)
{
    struct xe_rect *d = &xe.dirty[xe.display_page];
    text_overlay_apply(xe.display_page);         /* draw the text layer with save-under */
    xe.hal->video_present(&xe.page[xe.display_page], d, xe.page_switched, xe.scroll_line,
                          xe.pal, xe.graphics_on);
    xe_rect_clear(d);
    /* after the clear: the restored cells are dirty for the next present, so a prompt or box
     * that is gone by then is erased on backends that upload only dirty rectangles (found by
     * the Amiga presentation test, plan §10) */
    text_overlay_restore(xe.display_page);
    xe.page_switched = 0;
}

void sys_present_now(void)
{
    present();
}

void sys_frame_wait(void)
{
    if (sys_quitting()) {
        /* leaving: the loop that called us (a fade, an effect, a timed wait) runs out at CPU
         * speed; nothing more is shown or waited for */
        xe.tick++;
        xe.vm.leave = xe.vm.quit = 1;
        return;
    }
    blink_tick();                                /* VSYNC interrupt work (graphics.md §3.5) */
    snd_pump();
    voice_pump();
    present();
    xe.hal->frame_wait();
    xe.tick++;
    text_overlay_tick();
    poll_input();
}

/* wait_vsync 0DCA:0012: n frames; CTRL held ends the wait early. */
void sys_wait_ticks(int n)
{
    int i;
    int loud = n > 1 && !xe.trace_quiet;
    if (loud)
        sys_trace(XE_TR_WAIT, 0, 0, XE_WAIT_TICKS, 0, n, 0);
    for (i = 0; i < n && !sys_quitting(); i++) {
        sys_frame_wait();
        if (sys_ctrl_held())
            break;
    }
    if (loud)
        sys_trace(XE_TR_WAIT, 0, 0, XE_WAIT_TICKS, 1,
                  sys_quitting() ? XE_LEAVE_QUIT : i < n ? XE_LEAVE_CTRL : XE_LEAVE_TIMEOUT, i);
}
