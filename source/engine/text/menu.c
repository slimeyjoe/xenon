/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Choice menu select_menu 0B7D:048C (engine.md §7). Options are drawn 15 on 0 inside the
 * message window at the menu origin, one row (18 px) per option; the current row is
 * highlighted by XOR-inverting all four planes over 16 lines. */
#include <string.h>
#include "xe.h"
#include "text/text.h"

#define ROW_PITCH 18
#define EXIT_CLEAR_WIDTH 0x3C       /* bytes: the message window width (0B7D:05BC) */

/* menu_clear 0B7D:0716: the option area (width = longest item) in all planes */
static void menu_clear(int width, int count)
{
    struct lrv_surface *pg = lrv_screen_access(&xe.screen);
    int p, y;
    for (p = 0; p < 4; p++)
        for (y = 0; y < count * ROW_PITCH; y++) {
            int off = (xe.menu_y + y) * XE_STRIDE + xe.menu_x;
            if (off + width <= XE_PLANE_BYTES)
                memset(pg->plane[p] + off, 0, (size_t)width);
        }
    lrv_screen_mark(&xe.screen, xe.screen.access_page, xe.menu_x, xe.menu_y, xe.menu_x + width, xe.menu_y + count * ROW_PITCH);
}

/* menu_draw_item 0B7D:0627: sprintf("%-*s") padded to the width, drawn with text_draw_string.
 * The original pads to `width` bytes, which are columns in cp932; the port pads in columns so
 * UTF-8 items get the same layout. */
static void menu_draw_item(const uint8_t *s, int i, int width)
{
    uint8_t buf[256];
    int n = (int)strlen((const char *)s), k, pad;
    if (n > (int)sizeof buf - 1)
        n = (int)sizeof buf - 1;
    memcpy(buf, s, (size_t)n);
    buf[n] = 0;
    pad = width - text_cols(buf);
    for (k = n; pad > 0 && k < (int)sizeof buf - 1; k++, pad--)
        buf[k] = ' ';
    buf[k] = 0;
    text_draw_string(xe.menu_x, xe.menu_y + i * ROW_PITCH, 15, 0, buf, 0);
}

/* menu_hilite 0B7D:066B: XOR-invert the row (4 planes, 16 lines) */
static void menu_hilite(int i, int width)
{
    struct lrv_surface *pg = lrv_screen_access(&xe.screen);
    int p, y, x;
    for (p = 0; p < 4; p++)
        for (y = 0; y < 16; y++) {
            int off = (xe.menu_y + i * ROW_PITCH + y) * XE_STRIDE + xe.menu_x;
            if (off + width > XE_PLANE_BYTES)
                break;
            for (x = 0; x < width; x++)
                pg->plane[p][off + x] ^= 0xFF;
        }
    lrv_screen_mark(&xe.screen, xe.screen.access_page, xe.menu_x, xe.menu_y + i * ROW_PITCH, xe.menu_x + width,
                   xe.menu_y + i * ROW_PITCH + 16);
}

/* the menu's trace event: its options, the cursor, and whether the engine shows it */
static void trace_menu(const uint8_t *const *items, int count, int cur)
{
    struct lrv_trace_event ev;
    if (!lrv_sys_trace_begin(&xe.sys, &ev, XE_TR_MENU))
        return;
    ev.items = items;
    ev.n = count;
    ev.a0 = cur;
    ev.a1 = xe.in_sysmenu;
    lrv_sys_trace_send(&xe.sys, &ev);
}

int select_menu(const uint8_t *const *items, int count, int initial)
{
    int width = 0, i, cur = initial;
    if (count <= 0) {
        msg_box(lang_str(LS_MENU_EMPTY));
        return -1;
    }
    for (i = 0; i < count; i++) {
        int c = text_cols(items[i]);
        if (c > width)
            width = c;
    }
    if (cur < 0 || cur >= count)
        cur = 0;
    menu_clear(width, count);
    for (i = 0; i < count; i++)
        menu_draw_item(items[i], i, width);
    menu_hilite(cur, width);
    kbd_flush();                                    /* 0B7D:0548 drains type-ahead */
    trace_menu(items, count, cur);

    for (;;) {
        int k = input_get_cmd();
        int next = cur;
        if (quitting()) {
            lrv_sys_trace(&xe.sys, XE_TR_MENU_SEL, 0, 0, -1, cur, 0, 0);
            return -1;
        }
        switch (k) {
        case XE_CMD_UP:
            if (cur > 0) next = cur - 1;
            else if (!xe.input_from_mouse) next = count - 1;
            break;
        case XE_CMD_DOWN:
            if (cur < count - 1) next = cur + 1;
            else if (!xe.input_from_mouse) next = 0;
            break;
        case XE_CMD_OK:
        case XE_CMD_CANCEL:
        case XE_CMD_HELP:
            /* 0B7D:05BB: on the way out the option rows are wiped over the full window
             * width (menu_clear(0x3C, count)), so nothing of this menu survives into the
             * next page's menu or message */
            menu_clear(EXIT_CLEAR_WIDTH, count);
            lrv_sys_trace(&xe.sys, XE_TR_MENU_SEL, 0, 0, k == XE_CMD_OK ? cur : k == XE_CMD_CANCEL ? -1 : -2, cur, 0, 0);
            return k == XE_CMD_OK ? cur : k == XE_CMD_CANCEL ? -1 : -2;
        default:
            break;
        }
        if (next != cur) {
            menu_hilite(cur, width);
            cur = next;
            menu_hilite(cur, width);
        }
    }
}
