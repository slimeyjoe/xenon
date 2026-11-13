/* Control mode (null and SDL3 backends): see control.h.
 *
 * Output: one JSON object per line on stdout.
 *   {"ev":"<kind>","t":tick,"pos":offset|null,"s":"script","name":..,"text":"<hex>",  (text bytes in the language encoding)
 *    "items":["<hex>",..],"n":N,"a":[a0,a1,a2,a3]}          engine trace event
 *   {"ev":"stop","t":tick,"why":"blocked|break|ticks","wait":kind|-1}
 *   {"ev":"frame","t":tick,"hash":"<16 hex>","pal":"<hex>","gon":0|1,"scroll":N,
 *    "data":"<base64 nibble-packed indices>"}               (data only with record 2)
 *   {"ev":"snap","name":..,"t":tick,"pal":..,"gon":..,"scroll":..,"data":..}
 *   {"ev":"present","name":..,"t":tick,"win":[W,H],"dest":[x,y,w,h],"mode":M,"linear":0|1,
 *    "nonuniform":N,"border_lit":N,"data":"<base64 RGB 640x400>"}   rendered frame (readback)
 *   {"ev":"ok"} after every command that does not resume the engine
 *   {"ev":"exit","code":N,"msg":..}
 * Input commands (one per line):
 *   press k[,k..]     edge presses at the next poll: ok cancel up down left right help f1..f5
 *                     other quit emergency mouse_l mouse_r
 *   hold ctrl|instant|anykey 0|1
 *   mouse DX DY
 *   run [N]           resume; stop when blocked on a key wait, at a breakpoint, or after N ticks
 *   break KIND|all    stop (after the tick) when an event of this kind was emitted
 *   unbreak KIND|all
 *   record 0|1|2      per-tick frame events when the frame changed (2 = with pixel data)
 *   snap NAME         emit the displayed frame (the engine's planes)
 *   readback NAME     emit what the backend rendered for it (backends with a real display)
 *   quit */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xe_hal.h"
#include "control.h"

#define NKINDS 64

static const char *const kind_names[NKINDS] = {
    0, "script", "page", "label", "flag", "op", "msg", "msg_end", "prompt", "menu", "menu_sel",
    "sysmenu", "title_menu", "title_sel", "gdt", "gdisp", "effect", "fade", "adt", "blink", "wait",
    "key", "save", "load", "progress", "error", "box", "seq", "quit", "clear", "voice"
};

static uint8_t g_planes[4][XE_PLANE_BYTES];
static uint8_t g_pal[48];
static int g_on, g_scroll, g_have_frame;

static uint32_t g_pending;                  /* XE_IN_* edges for the next poll */
static int g_ctrl, g_instant, g_anykey;
static int g_mdx, g_mdy;

static long g_budget = -1;                  /* ticks to run before stopping; -1 = until blocked */
static int g_breaks[NKINDS];
static int g_break_hit;
static int g_record;
static uint64_t g_last_hash;
static int g_in_key_wait;                   /* inside sys_input_get_cmd */
static int g_wait_kind = -1;
static uint32_t g_tick;
static int g_started;                       /* first run command seen */
static int (*g_readback)(struct ctl_readback *rb);

/* ---- output helpers --------------------------------------------------------------------- */

static void out_hex(const uint8_t *p, size_t n)
{
    static const char hx[] = "0123456789abcdef";
    size_t i;
    putchar('"');
    for (i = 0; i < n; i++) {
        putchar(hx[p[i] >> 4]);
        putchar(hx[p[i] & 15]);
    }
    putchar('"');
}

static void out_str(const char *s)
{
    putchar('"');
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { putchar('\\'); putchar(c); }
        else if (c < 0x20 || c >= 0x7F) printf("\\u%04x", c);
        else putchar(c);
    }
    putchar('"');
}

static void out_b64(const uint8_t *p, size_t n)
{
    static const char tb[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i;
    putchar('"');
    for (i = 0; i + 2 < n; i += 3) {
        uint32_t v = ((uint32_t)p[i] << 16) | ((uint32_t)p[i + 1] << 8) | p[i + 2];
        putchar(tb[v >> 18]); putchar(tb[(v >> 12) & 63]); putchar(tb[(v >> 6) & 63]); putchar(tb[v & 63]);
    }
    if (i + 1 == n) {
        uint32_t v = (uint32_t)p[i] << 16;
        putchar(tb[v >> 18]); putchar(tb[(v >> 12) & 63]); putchar('='); putchar('=');
    } else if (i + 2 == n) {
        uint32_t v = ((uint32_t)p[i] << 16) | ((uint32_t)p[i + 1] << 8);
        putchar(tb[v >> 18]); putchar(tb[(v >> 12) & 63]); putchar(tb[(v >> 6) & 63]); putchar('=');
    }
    putchar('"');
}

static void out_end(void)
{
    putchar('\n');
    fflush(stdout);
}

/* ---- frame access ----------------------------------------------------------------------- */

/* nibble-packed indices of the displayed frame (scroll applied), 128 000 bytes */
static void frame_indices(uint8_t *out)
{
    int y, x;
    for (y = 0; y < XE_SCREEN_H; y++) {
        int sy = (y + g_scroll) % XE_SCREEN_H;
        const uint8_t *p0 = g_planes[0] + sy * XE_PLANE_STRIDE, *p1 = g_planes[1] + sy * XE_PLANE_STRIDE,
                      *p2 = g_planes[2] + sy * XE_PLANE_STRIDE, *p3 = g_planes[3] + sy * XE_PLANE_STRIDE;
        uint8_t *o = out + y * (XE_SCREEN_W / 2);
        for (x = 0; x < XE_PLANE_STRIDE; x++) {
            uint8_t b0 = p0[x], b1 = p1[x], b2 = p2[x], b3 = p3[x];
            int k;
            for (k = 0; k < 4; k++) {
                int s1 = 7 - 2 * k, s0 = 6 - 2 * k;
                uint8_t hi = (uint8_t)(((b0 >> s1) & 1) | (((b1 >> s1) & 1) << 1) |
                                       (((b2 >> s1) & 1) << 2) | (((b3 >> s1) & 1) << 3));
                uint8_t lo = (uint8_t)(((b0 >> s0) & 1) | (((b1 >> s0) & 1) << 1) |
                                       (((b2 >> s0) & 1) << 2) | (((b3 >> s0) & 1) << 3));
                o[x * 4 + k] = (uint8_t)((hi << 4) | lo);
            }
        }
    }
}

static uint64_t frame_hash(void)
{
    uint64_t h = 1469598103934665603ull;
    int p, i;
    for (p = 0; p < 4; p++)
        for (i = 0; i < XE_PLANE_BYTES; i++)
            h = (h ^ g_planes[p][i]) * 1099511628211ull;
    for (i = 0; i < 48; i++)
        h = (h ^ g_pal[i]) * 1099511628211ull;
    h = (h ^ (uint8_t)g_on) * 1099511628211ull;
    h = (h ^ (uint8_t)g_scroll) * 1099511628211ull;
    h = (h ^ (uint8_t)(g_scroll >> 8)) * 1099511628211ull;
    return h;
}

static void out_frame_body(void)
{
    printf("\"pal\":");
    out_hex(g_pal, 48);
    printf(",\"gon\":%d,\"scroll\":%d,\"data\":", g_on, g_scroll);
    {
        static uint8_t idx[XE_SCREEN_W * XE_SCREEN_H / 2];
        frame_indices(idx);
        out_b64(idx, sizeof idx);
    }
}

static void emit_snap(const char *name)
{
    printf("{\"ev\":\"snap\",\"name\":");
    out_str(name);
    printf(",\"t\":%lu,", (unsigned long)g_tick);
    out_frame_body();
    putchar('}');
    out_end();
}

/* A snapshot line for a frame that is not the engine's (null backend: --present-test and
 * autoplay snap: ticks), with the frame hash of backends/common/ptest.h. The frame is the one
 * given to the last ctl_present. */
void ctl_emit_snap(const char *name, uint32_t tick, uint32_t hash)
{
    printf("{\"ev\":\"snap\",\"name\":");
    out_str(name);
    printf(",\"t\":%lu,\"hash\":\"%08lx\",", (unsigned long)tick, (unsigned long)hash);
    out_frame_body();
    putchar('}');
    out_end();
}

/* ---- events ----------------------------------------------------------------------------- */

void ctl_trace(const struct xe_trace_event *ev)
{
    const char *kn = (ev->kind > 0 && ev->kind < NKINDS && kind_names[ev->kind]) ? kind_names[ev->kind] : "?";
    int i;
    if (ev->kind == XE_TR_WAIT && ev->a0 == XE_WAIT_KEY)
        g_in_key_wait = ev->a1 == 0;
    if (ev->kind == XE_TR_WAIT)
        g_wait_kind = ev->a1 == 0 ? ev->a0 : -1;

    printf("{\"ev\":\"%s\",\"t\":%lu,\"pos\":", kn, (unsigned long)ev->tick);
    if (ev->pos == ~0u) printf("null"); else printf("%lu", (unsigned long)ev->pos);
    printf(",\"s\":");
    out_str(ev->script ? ev->script : "");
    if (ev->name) {
        printf(",\"name\":");
        out_str(ev->name);
    }
    if (ev->text) {
        printf(",\"text\":");
        out_hex(ev->text, strlen((const char *)ev->text));
    }
    if (ev->items) {
        printf(",\"items\":[");
        for (i = 0; i < ev->n; i++) {
            if (i) putchar(',');
            out_hex(ev->items[i], strlen((const char *)ev->items[i]));
        }
        putchar(']');
    }
    if (ev->ints) {
        printf(",\"ints\":[");
        for (i = 0; i < ev->nints; i++)
            printf("%s%d", i ? "," : "", ev->ints[i]);
        putchar(']');
    }
    printf(",\"n\":%d,\"a\":[%d,%d,%d,%d]}", ev->n, ev->a0, ev->a1, ev->a2, ev->a3);
    out_end();

    if (ev->kind > 0 && ev->kind < NKINDS && g_breaks[ev->kind])
        g_break_hit = ev->kind;
}

void ctl_present(const struct xe_planes *page, int scroll, const uint8_t rgb4[48], int on, int page_switched)
{
    int p;
    (void)page_switched;
    for (p = 0; p < 4; p++)
        memcpy(g_planes[p], page->p[p], XE_PLANE_BYTES);
    memcpy(g_pal, rgb4, 48);
    g_on = on;
    g_scroll = scroll;
    g_have_frame = 1;
}

/* ---- commands --------------------------------------------------------------------------- */

static const struct { const char *name; uint32_t bit; } keys[] = {
    { "ok", XE_IN_OK }, { "cancel", XE_IN_CANCEL }, { "up", XE_IN_UP }, { "down", XE_IN_DOWN },
    { "left", XE_IN_LEFT }, { "right", XE_IN_RIGHT }, { "help", XE_IN_HELP },
    { "f1", XE_IN_F1 }, { "f2", XE_IN_F2 }, { "f3", XE_IN_F3 }, { "f4", XE_IN_F4 }, { "f5", XE_IN_F5 },
    { "other", XE_IN_OTHER }, { "quit", XE_IN_QUIT }, { "emergency", XE_IN_EMERGENCY },
    { "mouse_l", XE_IN_MOUSE_L }, { "mouse_r", XE_IN_MOUSE_R }, { 0, 0 }
};

static int kind_by_name(const char *s)
{
    int k;
    for (k = 1; k < NKINDS; k++)
        if (kind_names[k] && !strcmp(kind_names[k], s))
            return k;
    return -1;
}

static void emit_ok(void)
{
    printf("{\"ev\":\"ok\"}");
    out_end();
}

static void emit_error(const char *msg)
{
    printf("{\"ev\":\"error\",\"msg\":");
    out_str(msg);
    putchar('}');
    out_end();
}

static void emit_readback(const char *name)
{
    static uint8_t rgb[XE_SCREEN_W * XE_SCREEN_H * 3];
    struct ctl_readback rb;
    if (!g_readback) {
        emit_error("no readback on this backend");
        return;
    }
    memset(&rb, 0, sizeof rb);
    rb.rgb = rgb;
    if (g_readback(&rb) != 0) {
        emit_error("readback failed");
        return;
    }
    printf("{\"ev\":\"present\",\"name\":");
    out_str(name);
    printf(",\"t\":%lu,\"win\":[%d,%d],\"dest\":[%d,%d,%d,%d],\"mode\":%d,\"linear\":%d,"
           "\"nonuniform\":%ld,\"border_lit\":%ld,\"data\":",
           (unsigned long)g_tick, rb.win_w, rb.win_h, rb.dst_x, rb.dst_y, rb.dst_w, rb.dst_h,
           rb.scale_mode, rb.linear, rb.nonuniform, rb.border_lit);
    out_b64(rgb, sizeof rgb);
    putchar('}');
    out_end();
    emit_ok();
}

/* Returns 1 when the engine should resume. */
static int do_command(char *line)
{
    char *cmd = strtok(line, " \t\r\n");
    char *a1 = strtok(0, " \t\r\n");
    char *a2 = strtok(0, " \t\r\n");
    if (!cmd)
        return 0;
    if (!strcmp(cmd, "press")) {
        char *tok;
        uint32_t bits = 0;
        for (tok = a1 ? strtok(a1, ",") : 0; tok; tok = strtok(0, ",")) {
            int i, found = 0;
            for (i = 0; keys[i].name; i++)
                if (!strcmp(keys[i].name, tok)) { bits |= keys[i].bit; found = 1; }
            if (!found) {
                emit_error("unknown key");
                return 0;
            }
        }
        g_pending |= bits;
        emit_ok();
    } else if (!strcmp(cmd, "hold") && a1 && a2) {
        int v = atoi(a2) != 0;
        if (!strcmp(a1, "ctrl")) g_ctrl = v;
        else if (!strcmp(a1, "instant")) g_instant = v;
        else if (!strcmp(a1, "anykey")) g_anykey = v;
        else { emit_error("unknown level"); return 0; }
        emit_ok();
    } else if (!strcmp(cmd, "mouse") && a1 && a2) {
        g_mdx += atoi(a1);
        g_mdy += atoi(a2);
        emit_ok();
    } else if (!strcmp(cmd, "run")) {
        g_budget = a1 ? atol(a1) : -1;
        g_break_hit = 0;
        g_started = 1;
        return 1;
    } else if (!strcmp(cmd, "break") || !strcmp(cmd, "unbreak")) {
        int v = cmd[0] == 'b';
        if (!a1) { emit_error("break: kind missing"); return 0; }
        if (!strcmp(a1, "all")) {
            int k;
            for (k = 0; k < NKINDS; k++) g_breaks[k] = v;
        } else {
            int k = kind_by_name(a1);
            if (k < 0) { emit_error("unknown event kind"); return 0; }
            g_breaks[k] = v;
        }
        emit_ok();
    } else if (!strcmp(cmd, "record")) {
        g_record = a1 ? atoi(a1) : 1;
        g_last_hash = 0;
        emit_ok();
    } else if (!strcmp(cmd, "snap")) {
        emit_snap(a1 ? a1 : "snap");
        emit_ok();
    } else if (!strcmp(cmd, "readback")) {
        emit_readback(a1 ? a1 : "readback");
    } else if (!strcmp(cmd, "quit")) {
        ctl_exit(0, "quit command");
    } else {
        emit_error("unknown command");
    }
    return 0;
}

static void read_commands(void)
{
    static char line[4096];
    for (;;) {
        if (!fgets(line, sizeof line, stdin))
            ctl_exit(4, "driver closed the pipe");
        if (do_command(line))
            return;
    }
}

static void emit_stop(const char *why)
{
    printf("{\"ev\":\"stop\",\"t\":%lu,\"why\":\"%s\",\"wait\":%d,\"pending\":%lu}",
           (unsigned long)g_tick, why, g_wait_kind, (unsigned long)g_pending);
    out_end();
}

/* ---- tick ------------------------------------------------------------------------------- */

void ctl_tick(uint32_t tick)
{
    const char *why = 0;
    g_tick = tick;

    if (g_record && g_have_frame) {
        uint64_t h = frame_hash();
        if (h != g_last_hash) {
            g_last_hash = h;
            printf("{\"ev\":\"frame\",\"t\":%lu,\"hash\":\"%016llx\"", (unsigned long)tick,
                   (unsigned long long)h);
            if (g_record >= 2) {
                putchar(',');
                out_frame_body();
            } else {
                printf(",\"pal\":");
                out_hex(g_pal, 48);
                printf(",\"gon\":%d,\"scroll\":%d", g_on, g_scroll);
            }
            putchar('}');
            out_end();
        }
    }

    if (!g_started) {
        /* the very first tick: wait for the driver before the engine runs at all */
        emit_stop("start");
        read_commands();
        return;
    }
    if (g_budget > 0)
        g_budget--;
    if (g_break_hit)
        why = "break";
    else if (g_budget == 0)
        why = "ticks";
    else if (g_budget < 0 && g_in_key_wait && !g_pending && !g_mdx && !g_mdy)
        why = "blocked";
    if (!why)
        return;
    emit_stop(why);
    read_commands();
}

void ctl_input(struct xe_input *in)
{
    memset(in, 0, sizeof *in);
    in->pressed = g_pending;
    in->ctrl_held = g_ctrl;
    in->instant_text = g_instant;
    in->any_key_down = g_anykey || g_pending != 0;
    in->mouse_dx = g_mdx;
    in->mouse_dy = g_mdy;
    g_pending = 0;
    g_mdx = g_mdy = 0;
}

void ctl_input_flush(void)
{
    g_pending &= XE_IN_QUIT | XE_IN_EMERGENCY;  /* type-ahead is dropped, as in the original; */
    g_mdx = g_mdy = 0;                          /* a quit is not (xe_hal.h input_flush) */
}

void ctl_init(void)
{
    memset(g_breaks, 0, sizeof g_breaks);
}

void ctl_set_readback(int (*fn)(struct ctl_readback *rb))
{
    g_readback = fn;
}

void ctl_exit(int code, const char *msg)
{
    printf("{\"ev\":\"exit\",\"code\":%d,\"msg\":", code);
    out_str(msg ? msg : "");
    putchar('}');
    out_end();
    exit(code);
}
