/* Choice menu select_menu 0B7D:048C (engine.md §7). Options are drawn 15 on 0 inside the
 * message window at the menu origin, one row (18 px) per option; the current row is
 * highlighted by XOR-inverting all four planes over 16 lines. */
#include <string.h>
#include "xe_internal.h"
#include "gfx/gfx.h"
#include "text/text.h"
#include "sys.h"

#define ROW_PITCH 18
#define EXIT_CLEAR_WIDTH 0x3C       /* bytes: the message window width (0B7D:05BC) */

/* menu_clear 0B7D:0716: the option area (width = longest item) in all planes */
static void menu_clear(int width, int count)
{
    struct xe_planes *pg = gfx_access();
    int p, y;
    for (p = 0; p < 4; p++)
        for (y = 0; y < count * ROW_PITCH; y++) {
            int off = (xe.menu_y + y) * XE_PLANE_STRIDE + xe.menu_x;
            if (off + width <= XE_PLANE_BYTES)
                memset(pg->p[p] + off, 0, (size_t)width);
        }
    gfx_mark_dirty(xe.access_page, xe.menu_x, xe.menu_y, xe.menu_x + width, xe.menu_y + count * ROW_PITCH);
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
    struct xe_planes *pg = gfx_access();
    int p, y, x;
    for (p = 0; p < 4; p++)
        for (y = 0; y < 16; y++) {
            int off = (xe.menu_y + i * ROW_PITCH + y) * XE_PLANE_STRIDE + xe.menu_x;
            if (off + width > XE_PLANE_BYTES)
                break;
            for (x = 0; x < width; x++)
                pg->p[p][off + x] ^= 0xFF;
        }
    gfx_mark_dirty(xe.access_page, xe.menu_x, xe.menu_y + i * ROW_PITCH, xe.menu_x + width,
                   xe.menu_y + i * ROW_PITCH + 16);
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
    sys_kbd_flush();                                    /* 0B7D:0548 drains type-ahead */
    sys_trace_items(XE_TR_MENU, items, count, cur);

    for (;;) {
        int k = sys_input_get_cmd();
        int next = cur;
        if (xe.quit_requested || xe.emergency_quit) {
            sys_trace(XE_TR_MENU_SEL, 0, 0, -1, cur, 0, 0);
            return -1;
        }
        switch (k) {
        case SYS_CMD_UP:
            if (cur > 0) next = cur - 1;
            else if (!xe.input_from_mouse) next = count - 1;
            break;
        case SYS_CMD_DOWN:
            if (cur < count - 1) next = cur + 1;
            else if (!xe.input_from_mouse) next = 0;
            break;
        case SYS_CMD_OK:
        case SYS_CMD_CANCEL:
        case SYS_CMD_HELP:
            /* 0B7D:05BB: on the way out the option rows are wiped over the full window
             * width (menu_clear(0x3C, count)), so nothing of this menu survives into the
             * next page's menu or message */
            menu_clear(EXIT_CLEAR_WIDTH, count);
            sys_trace(XE_TR_MENU_SEL, 0, 0, k == SYS_CMD_OK ? cur : k == SYS_CMD_CANCEL ? -1 : -2, cur, 0, 0);
            return k == SYS_CMD_OK ? cur : k == SYS_CMD_CANCEL ? -1 : -2;
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
