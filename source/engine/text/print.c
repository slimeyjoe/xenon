/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Message printer text_print 0B7D:019B (engine.md §6) with two line-breaking modes:
 * the original byte-column algorithm with kinsoku for Japanese, and word wrap for English
 * (the translation's padding was removed at pack-build time). Row pitch 18 px, 4 rows. */
#include <string.h>
#include "xe.h"
#include "text/text.h"
#include "lrvne/sjis.h"
#include "voice/voice.h"

#define ROW_PITCH 18
#define ROWS 4
#define COLS 60

/* hanging punctuation table 0B7D:046C: 、。？！）」』 */
static const uint16_t hang[7] = { 0x8141, 0x8142, 0x8148, 0x8149, 0x816A, 0x8176, 0x8178 };

static int is_hanging(unsigned hi, unsigned lo)
{
    unsigned c = (hi << 8) | lo;
    int i;
    for (i = 0; i < 7; i++)
        if (hang[i] == c)
            return 1;
    return 0;
}

/* The ▼ end prompt (DS:08D2) and the ⏎ overflow prompt (DS:08B1, user glyph JIS 7621h =
 * SJIS EB9Fh): text_locate(73, 24) (0B7D:0430 / 03EC, 1-based cursor position = cell 72,23,
 * the bottom-right corner inside the text box), blinking white. */
static void show_prompt(int overflow)
{
    lrv_sys_trace(&xe.sys, XE_TR_PROMPT, 0, 0, overflow, 0, 0, 0);
    text_locate_put(73, 24, text_prompt_str(overflow), 15, XE_OVL_BLINK);
}

/* text_window_clear 0B7D:0783: 62 bytes x 72 lines at the window origin, all planes */
void text_window_clear(void)
{
    struct lrv_surface *pg = lrv_screen_access(&xe.screen);
    int p, y;
    for (p = 0; p < 4; p++)
        for (y = 0; y < ROWS * ROW_PITCH; y++) {
            int off = (xe.text_y + y) * XE_STRIDE + xe.text_x;
            if (off + 62 <= XE_PLANE_BYTES)
                memset(pg->plane[p] + off, 0, 62);
        }
    lrv_screen_mark(&xe.screen, xe.screen.access_page, xe.text_x, xe.text_y, xe.text_x + 62, xe.text_y + ROWS * ROW_PITCH);
}

struct printer {
    uint8_t *s;             /* scratch copy (the 々 replacement mutates the text) */
    int i;
    int col, row;
    int flag;               /* per-glyph wait */
    int timed_wait;         /* -1 = key wait at the end, else frames */
    int keep;               /* 0x05 seen */
    int hang_used;          /* one hanging character allowed per row */
};

static void new_row(struct printer *pr)
{
    pr->col = 0;
    pr->row++;
    pr->hang_used = 0;
    if (pr->row >= ROWS) {
        show_prompt(1);
        wait_ok_key();
        text_layer_clear();
        text_window_clear();
        pr->row = 0;
    }
}

/* columns of the word starting at s[i] (English wrap): until space, control code or end */
static int word_cols(const uint8_t *s, int i)
{
    struct lrv_text_ch ch;
    int cols = 0;
    while (s[i] && s[i] != ' ' && s[i] != 0x0A && s[i] != 0x0C && s[i] != 0x04 &&
           s[i] != 0x05 && s[i] != 0x03) {
        i += text_next(s + i, &ch);
        cols += ch.cols;
    }
    return cols;
}

/* line breaking after a glyph (engine.md §6). The kinsoku rules compare cp932 bytes: they
 * apply to cp932 text only (Japanese). */
static void after_glyph(struct printer *pr)
{
    uint8_t *s = pr->s;
    if (pr->col >= COLS) {
        if (xe.wrap_mode == XE_WRAP_JA && xe.text_enc == XE_ENC_CP932) {
            unsigned hi = s[pr->i], lo = s[pr->i + 1];
            if (hi && lrv_sjis_is_lead(hi) && is_hanging(hi, lo) && !pr->hang_used) {
                pr->hang_used = 1;
                return;                                 /* let it hang once */
            }
            if (hi == 0x81 && lo == 0x58 && pr->i >= 2) {    /* 々 may not start a line */
                s[pr->i] = s[pr->i - 2];
                s[pr->i + 1] = s[pr->i - 1];
            }
        }
        if (s[pr->i] && s[pr->i] != 0x0A)
            new_row(pr);
    } else if (xe.text_x + pr->col >= 80) {
        new_row(pr);
    }
}

void text_print(const uint8_t *src, int flag)
{
    uint8_t buf[1024];
    struct printer pr;
    int part = 0;
    size_t n = strlen((const char *)src);
    if (n >= sizeof buf)
        n = sizeof buf - 1;
    memcpy(buf, src, n);
    buf[n] = 0;
    pr.s = buf;
    pr.i = 0;
    pr.col = 0;
    pr.row = 0;
    pr.flag = flag;
    pr.timed_wait = -1;
    pr.keep = 0;
    pr.hang_used = 0;
    lrv_sys_trace(&xe.sys, XE_TR_MSG, 0, buf, !flag, 0, 0, 0);
    voice_message(src);                         /* the part-0 clip (docs/voice.md §4) */

    while (buf[pr.i]) {
        unsigned c = buf[pr.i];
        struct lrv_text_ch ch;
        int x = xe.text_x + pr.col, y = xe.text_y + pr.row * ROW_PITCH;

        if (xe.wrap_mode == XE_WRAP_EN && c == ' ' && pr.col > 0) {
            /* English word wrap: does the next word fit on this row? */
            int w = word_cols(buf, pr.i + 1);
            if (w > 0 && pr.col + 1 + w > COLS && w <= COLS) {
                pr.i++;
                new_row(&pr);
                continue;
            }
        }
        text_next(buf + pr.i, &ch);
        if (ch.cols == 2) {
            kanji_draw(x, y, xe.text_fg, xe.text_bg, ch.code, pr.flag);
            pr.i += ch.len;
            pr.col += 2;
            after_glyph(&pr);
        } else if (c == '@') {
            pr.i += buf[pr.i + 1] ? 2 : 1;              /* name insertion hook: unused */
        } else if (c == 0x0A) {
            pr.i++;
            new_row(&pr);
        } else if (c == 0x0C) {
            int skipped;
            pr.i++;
            voice_wait();                       /* the clip of this part plays out first */
            skipped = wait_ok_key();
            voice_part(++part, skipped);
        } else if (c == 0x03) {                         /* {BIG} ... 0x03 */
            pr.i++;
            while (buf[pr.i] && buf[pr.i] != 0x03) {
                text_next(buf + pr.i, &ch);
                pr.i += ch.len;
                if (ch.cols == 2) {                     /* only full-width glyphs are drawn */
                    kanji_draw_big(xe.text_x + pr.col, xe.text_y + pr.row * ROW_PITCH,
                                   ch.code, pr.flag);
                    pr.col += 4;
                    after_glyph(&pr);
                }
            }
            if (buf[pr.i] == 0x03)
                pr.i++;
        } else if (c == 0x04) {
            unsigned d = buf[pr.i + 1];
            pr.i += d ? 2 : 1;
            if (d >= '0' && d <= '9')
                pr.timed_wait = (int)(d - '0') * 60;
            else
                lrv_log(xe.hal, LRV_LOG_WARN, "text: 0x04 without a digit");
        } else if (c == 0x05) {
            pr.i++;
            pr.keep = 1;
        } else {
            ank_draw(x, y, xe.text_fg, xe.text_bg, ch.code);
            pr.i += ch.len;
            pr.col++;
            after_glyph(&pr);
        }
    }

    lrv_sys_trace(&xe.sys, XE_TR_MSG_END, 0, 0, pr.timed_wait, pr.keep, 0, 0);
    voice_wait();                               /* the prompt comes when the clip is over */
    if (pr.timed_wait < 0) {
        show_prompt(0);
        wait_ok_key();
        text_layer_clear();
    } else {
        wait_vsync(pr.timed_wait);
    }
    if (!pr.keep)
        text_window_clear();
}
