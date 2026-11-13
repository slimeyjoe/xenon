/* Window, IDCMP input, pointer and the screen front/back policy.
 *
 * A borderless backdrop window covers the game screen and receives raw keys and mouse
 * events (delta mode, so the pointer position is irrelevant, as in the original). The
 * Intuition screen shortcuts work untouched: LAmiga+N brings the Workbench to the front
 * and LAmiga+M cycles screens. When the game screen goes to the back the pointer is shown
 * again and input stops being ours; when it comes back to the front the window is
 * re-activated and the pointer hidden. The game keeps running either way (music
 * continues, like a PC-98 would).
 *
 * Keys: cursor keys / numpad 8-2-4-6, Return/Enter/Space = OK, Esc / numpad 0 = cancel,
 * Help (F11 on a PC keyboard) = HELP, F1-F5 = f.1-f.5, Ctrl held = skip waits, Caps Lock = instant text,
 * Shift+Esc = emergency quit. Mouse: motion steps the cursor, left = OK, right = cancel.
 * Ctrl-C from the shell asks the game to quit.
 *
 * The OS side (GetMsg/ReplyMsg, the front-screen check, Ctrl-C) runs in the main task's
 * once-per-frame OS call (frameos.h): the frame tick gathers the messages, the next poll
 * decodes them here. A poll with no gather since the last one makes its own call.
 *
 * In a window on a public screen (WINDOW, video_win.c: am_input_attach) the game window is
 * the input window. The pointer stays visible (the active window's pointer is the pointer
 * of the whole screen) and the screen order does not matter. The click that activates the
 * window is delivered to it too (probes/amiga/README.md "The Workbench window"): mouse
 * buttons are ignored for two polls after an activation, or that click would be an OK. The
 * close gadget quits; F10 steps the window through its sizes. */
#include "os_begin.h"
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <devices/inputevent.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include "os_end.h"
#include <string.h>
#include "amiga.h"
#include "frameos.h"

static struct Window *g_win;
static struct Screen *g_scr;
static UWORD *g_blank;              /* 1-line empty sprite = invisible pointer */
static struct xe_input g_in;        /* accumulated since the last poll */
static uint8_t g_held[16];          /* raw key codes held (128 bits) */
static int g_ctrl, g_caps;
static int g_front = -1;            /* our screen is frontmost (tracked per tick) */
static int g_active;                /* our window is the active one */
static int g_pointer_hidden;
static int g_attached;              /* the window is video_win.c's (WINDOW) */
static int g_click_guard;           /* polls left in which mouse buttons are ignored */

static void hide_pointer(void)
{
    if (g_win && g_blank && !g_pointer_hidden) {
        SetPointer(g_win, g_blank, 1, 16, 0, 0);
        g_pointer_hidden = 1;
    }
}

static void show_pointer(void)
{
    if (g_win && g_pointer_hidden) {
        ClearPointer(g_win);
        g_pointer_hidden = 0;
    }
}

int am_input_open(struct Screen *scr)
{
    g_scr = scr;
    g_blank = AllocMem(12, MEMF_CHIP | MEMF_CLEAR);
    g_win = OpenWindowTags(NULL,
                           WA_CustomScreen, (IPTR)scr,
                           WA_Left, 0, WA_Top, 0, WA_Width, scr->Width, WA_Height, scr->Height,
                           WA_Flags, WFLG_BACKDROP | WFLG_BORDERLESS | WFLG_ACTIVATE | WFLG_RMBTRAP |
                                     WFLG_REPORTMOUSE | WFLG_SIMPLE_REFRESH | WFLG_NOCAREREFRESH,
                           WA_IDCMP, IDCMP_RAWKEY | IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE | IDCMP_DELTAMOVE |
                                     IDCMP_ACTIVEWINDOW | IDCMP_INACTIVEWINDOW,
                           TAG_DONE);
    if (!g_win) {
        am_log("input: cannot open window\n");
        return -1;
    }
    memset(&g_in, 0, sizeof g_in);
    memset(g_held, 0, sizeof g_held);
    g_ctrl = g_caps = 0;
    g_active = 1;
    g_front = 1;
    g_pointer_hidden = 0;
    hide_pointer();
    am_fr->ibase = IntuitionBase;
    am_fr->scr = scr;
    am_fr->n_ev = am_fr->gathered = am_fr->more = am_fr->brk = 0;
    am_fr->win = g_win;
    return 0;
}

int am_input_attach(struct Window *win)
{
    g_win = win;
    g_scr = win->WScreen;
    g_blank = 0;
    g_attached = 1;
    g_click_guard = 0;
    memset(&g_in, 0, sizeof g_in);
    memset(g_held, 0, sizeof g_held);
    g_ctrl = g_caps = 0;
    g_active = 1;
    g_front = 1;
    g_pointer_hidden = 0;
    am_fr->ibase = IntuitionBase;
    am_fr->scr = g_scr;
    am_fr->n_ev = am_fr->gathered = am_fr->more = am_fr->brk = 0;
    am_fr->win = g_win;
    return 0;
}

void am_input_close(void)
{
    am_fr->win = 0;
    am_fr->n_ev = am_fr->gathered = am_fr->more = 0;
    if (g_win) {
        struct IntuiMessage *m;
        show_pointer();
        while ((m = (struct IntuiMessage *)GetMsg(g_win->UserPort)) != NULL)
            ReplyMsg(&m->ExecMessage);
        if (!g_attached)
            CloseWindow(g_win);         /* an attached window is video_win.c's to close */
    }
    g_attached = 0;
    g_win = 0;
    if (g_blank)
        FreeMem(g_blank, 12);
    g_blank = 0;
}

static void key_event(UWORD code, UWORD qual)
{
    int down = !(code & 0x80);
    UWORD k = code & 0x7f;
    if (down)
        g_held[k >> 3] |= (uint8_t)(1 << (k & 7));
    else
        g_held[k >> 3] &= (uint8_t)~(1 << (k & 7));
    g_caps = (qual & IEQUALIFIER_CAPSLOCK) != 0;
    if (k == 0x63) {                              /* Ctrl: level from its own up/down */
        g_ctrl = down;
        return;
    }
    if (!down)
        return;
    switch (k) {
    case 0x4C: case 0x3E: g_in.pressed |= XE_IN_UP; break;
    case 0x4D: case 0x1E: g_in.pressed |= XE_IN_DOWN; break;
    case 0x4F: case 0x2D: g_in.pressed |= XE_IN_LEFT; break;
    case 0x4E: case 0x2F: g_in.pressed |= XE_IN_RIGHT; break;
    case 0x44: case 0x43: case 0x40: g_in.pressed |= XE_IN_OK; break;
    case 0x45:
        if (qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT))
            g_in.pressed |= XE_IN_EMERGENCY;
        else
            g_in.pressed |= XE_IN_CANCEL;
        break;
    case 0x0F: g_in.pressed |= XE_IN_CANCEL; break;
    case 0x5F: case 0x4B: g_in.pressed |= XE_IN_HELP; break;   /* Help; F11 (PC keyboards, AROS) */
    case 0x50: g_in.pressed |= XE_IN_F1; break;
    case 0x51: g_in.pressed |= XE_IN_F2; break;
    case 0x52: g_in.pressed |= XE_IN_F3; break;
    case 0x53: g_in.pressed |= XE_IN_F4; break;
    case 0x54: g_in.pressed |= XE_IN_F5; break;
    case 0x59:                                    /* F10: in a window, its next size */
        if (g_attached)
            am_win_cycle();
        else
            g_in.pressed |= XE_IN_OTHER;
        break;
    case 0x60: case 0x61: case 0x62: case 0x64: case 0x65: case 0x66: case 0x67:
        break;                                    /* modifiers */
    default:
        g_in.pressed |= XE_IN_OTHER;
        break;
    }
}

/* the messages of the last gather */
static void decode(struct am_frameos *f)
{
    ULONG i;
    for (i = 0; i < f->n_ev; i++) {
        const struct am_frame_ev *e = &f->ev[i];
        UWORD code = (UWORD)e->code;
        switch (e->cls) {
        case IDCMP_RAWKEY:
            key_event(code, (UWORD)e->qual);
            break;
        case IDCMP_MOUSEMOVE:
            g_in.mouse_dx += (WORD)e->mx;
            g_in.mouse_dy += (WORD)e->my;
            break;
        case IDCMP_MOUSEBUTTONS:
            if (g_click_guard)
                break;                            /* the click that activated the window */
            if (code == SELECTDOWN) g_in.pressed |= XE_IN_MOUSE_L;
            if (code == MENUDOWN) g_in.pressed |= XE_IN_MOUSE_R;
            break;
        case IDCMP_ACTIVEWINDOW:
            g_active = 1;
            if (g_attached)
                g_click_guard = 2;
            else
                hide_pointer();
            break;
        case IDCMP_CLOSEWINDOW:
            g_in.pressed |= XE_IN_QUIT;
            break;
        case IDCMP_REFRESHWINDOW:
            am_win_refresh();
            break;
        case IDCMP_CHANGEWINDOW:
            am_win_changed();
            break;
        case IDCMP_INACTIVEWINDOW:
            g_active = 0;
            memset(g_held, 0, sizeof g_held);     /* we will not see the key-ups */
            g_ctrl = 0;
            show_pointer();
            break;
        default:
            break;
        }
    }
    f->n_ev = 0;
    f->gathered = 0;
}

/* fresh: also gather what arrived since the frame tick's gather (a flush) */
static void pump(int fresh)
{
    struct am_frameos *f = am_fr;
    if (!g_win)
        return;
    if (fresh && f->gathered)
        decode(f);
    do {
        if (!f->gathered)
            am_frame_call(AM_F_INPUT);
        decode(f);
    } while (f->more);
    if (g_attached) {
        /* a window: the screen order is not ours to follow; the click guard runs down */
        if (g_click_guard)
            g_click_guard--;
    } else {
        /* screen front/back transitions (LAmiga+N / LAmiga+M, or another program's screen) */
        int front = f->front != 0;
        if (front != g_front) {
            g_front = front;
            if (front) {
                if (!g_active)
                    ActivateWindow(g_win);
                hide_pointer();
            } else {
                show_pointer();
            }
        }
    }
    if (f->brk) {
        f->brk = 0;
        g_in.pressed |= XE_IN_QUIT;
    }
}

void am_input_poll(struct xe_input *in)
{
    int i, any = 0;
    pump(0);
    for (i = 0; i < 16; i++)
        any |= g_held[i];
    g_in.ctrl_held = g_ctrl;
    g_in.instant_text = g_caps;
    g_in.any_key_down = any != 0;
    *in = g_in;
    g_in.pressed = 0;
    g_in.mouse_dx = g_in.mouse_dy = 0;
}

void am_input_flush(void)
{
    pump(1);
    g_in.pressed &= XE_IN_QUIT | XE_IN_EMERGENCY;
    g_in.mouse_dx = g_in.mouse_dy = 0;
}
