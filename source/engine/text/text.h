/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Text: fonts, glyph drawing (seg 0F16), the message printer and menus (seg 0B7D) and the
 * text-layer overlay (prompts, error box). docs/re/font.md, engine.md §5.4, §6, §7. */
#ifndef XE_TEXT_TEXT_H
#define XE_TEXT_TEXT_H

#include <stdint.h>
#include "lrvne/font.h"

void text_init(void);                     /* fonts, settings and strings of the language */
void text_shutdown(void);

/* The character stream of the language's encoding (enc.c). Strings everywhere in the engine
 * are bytes in that encoding: cp932 or UTF-8 (lrv_text_next_sjis / _utf8). */
#define XE_U_RETURN 0xE000u                /* UTF-8 fonts: the return glyph (JIS 7621h) */
void text_enc_init(void);                 /* the language's encoding and wrap mode */
int text_next(const uint8_t *s, struct lrv_text_ch *ch);    /* s[0] != 0; returns ch->len */
int text_cols(const uint8_t *s);          /* display width in columns */
const uint8_t *text_prompt_str(int overflow);          /* ▼ (0) or the return glyph (1) */

/* glyphs (font.md §2): x in byte columns, y in lines, into the access page; `code` is the
 * font key (JIS X 0208 / JIS X 0201 for cp932 languages, Unicode for UTF-8 languages) */
void kanji_draw(int x, int y, int fg, int bg, unsigned code, int wait);
void kanji_draw_big(int x, int y, unsigned code, int wait);
void ank_draw(int x, int y, int fg, int bg, unsigned code);
void text_draw_string(int x, int y, int fg, int bg, const uint8_t *s, int wait);

/* message printer (engine.md §6); flag = 1 -> one frame per full-width glyph */
void text_print(const uint8_t *s, int flag);
void text_window_clear(void);             /* 0B7D:0783 */

/* choice menu (engine.md §7): returns index, -1 cancel, -2 HELP */
int select_menu(const uint8_t *const *items, int count, int initial);

/* text layer (80x25 cells over the graphics): an overlay of the screen (lrvne/gfx.h) */
void text_layer_init(void);               /* adds the overlay and its blink to the frame */
void text_layer_clear(void);
void text_overlay_put(int col, int row, const uint8_t *s, int colour, int attr);
/* same, with the original's 1-based ESC[row;colH coordinates */
void text_locate_put(int col1, int row1, const uint8_t *s, int colour, int attr);

/* boxes on the text layer */
void error_box(const uint8_t *msg);       /* 088D:13DF: beep, red reverse on row 10, key */
void msg_box(const uint8_t *msg);         /* 0B7D:07DF: row 22 col 24, key, clear */

/* language strings (engine messages) */
enum lang_id {
    LS_SYSMENU_CANCEL, LS_SYSMENU_RESTART, LS_SYSMENU_QUIT,
    LS_QUIT_CONTINUE, LS_QUIT_REALLY,
    LS_OMAKE_LOCKED, LS_NO_SAVE_DATA, LS_FILE_OPEN_ERROR, LS_DISK_INSERT, LS_DISK_WRONG,
    LS_DISK_NOT_GAME, LS_DISK_WRITE_PROTECT, LS_DISK_PROMPT, LS_MENU_EMPTY, LS_SCRIPT_ERROR,
    LS_COUNT
};
const uint8_t *lang_str(int id);          /* language encoding, NUL terminated */

#endif
