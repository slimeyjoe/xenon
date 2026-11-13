/* Control mode (test automation), shared by the null and SDL3 backends. A line protocol on
 * stdin/stdout: the backend writes engine events as JSON lines and stops at well-defined points
 * (the engine blocked on a key wait, a breakpoint event, a tick budget), waiting for driver
 * commands. See tests/e2e/xe2e/engine.py for the driver side and control.c for the commands. */
#ifndef XE_CONTROL_H
#define XE_CONTROL_H

#include <stdint.h>
#include "xe_hal.h"

void ctl_init(void);
void ctl_trace(const struct xe_trace_event *ev);
/* Called from video_present every tick with the displayed page. */
void ctl_present(const struct xe_planes *page, int scroll, const uint8_t rgb4[48], int on, int page_switched);
/* Called from frame_wait after the tick counter advanced; may block reading commands. */
void ctl_tick(uint32_t tick);
/* Input state the driver set (levels and pending edges); pending edges are cleared. */
void ctl_input(struct xe_input *in);
void ctl_input_flush(void);
/* Prints a snap event for the frame of the last ctl_present, with a frame hash (ptest.h). */
void ctl_emit_snap(const char *name, uint32_t tick, uint32_t hash);
/* Engine finished (exit code) / fatal error. */
void ctl_exit(int code, const char *msg);

/* Presentation readback (the `readback` command): a backend that really renders fills in what
 * it put on the screen for the last presented frame, so the driver can compare the rendered
 * pixels with the engine's planes. rgb holds one sample per source pixel, taken at the centre
 * of the destination block of that pixel (640 x 400 x 3, row-major). nonuniform counts the
 * destination pixels inside the rectangle that differ from their source pixel's sample (zero
 * for exact integer scaling), border_lit the non-black pixels outside it. Returns 0 on success. */
struct ctl_readback {
    int win_w, win_h;                 /* drawable size in pixels */
    int dst_x, dst_y, dst_w, dst_h;   /* where the 640x400 image went */
    int scale_mode;                   /* XE_SCALE_* */
    int linear;                       /* 1 = filtered scaling: only flat areas are exact */
    long nonuniform;
    long border_lit;
    uint8_t *rgb;                     /* XE_SCREEN_W * XE_SCREEN_H * 3, provided by control.c */
};
void ctl_set_readback(int (*fn)(struct ctl_readback *rb));

#endif
