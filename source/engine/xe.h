/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The engine's state, shared by its modules.
 *
 * The original engine (AGS.EXE) is one program with global state (DGROUP); the port keeps that
 * shape in one structure, `xe`, so the modules can be read next to the research documents.
 * Names in comments refer to the DGROUP globals of docs/re/globals.md.
 *
 * Everything below the interpreter, the formats and the music driver is lrvne (source/lrvne,
 * its headers in lrvne/): the frame and the input queue (lrv_sys), the pages, the DAC and the
 * presentation (lrv_screen), the path from the music driver to samples (lrv_opnq, lrv_mixer),
 * the voice clip player (lrv_voice), the pack (lrv_pack) and the glyph tables (lrv_font).
 * Their instances are members of `xe`; the engine calls lrvne directly. */
#ifndef XE_H
#define XE_H

#include <stddef.h>
#include <stdint.h>
#include "lrvne/app.h"
#include "lrvne/core.h"
#include "lrvne/font.h"
#include "lrvne/gfx.h"
#include "lrvne/hal.h"
#include "lrvne/mixer.h"
#include "lrvne/opnq.h"
#include "lrvne/pack.h"
#include "lrvne/pc98.h"
#include "lrvne/profile.h"
#include "lrvne/sys.h"
#include "lrvne/music.h"
#include "lrvne/pmd.h"
#include "lrvne/voice.h"
#include "vm/cc.h"

/* The PC-98 graphics the original addresses: offsets y * 80 + x / 8 into four planes. */
#define XE_STRIDE      LRV_PC98_STRIDE
#define XE_SCREEN_H    LRV_PC98_H
#define XE_PLANE_BYTES (LRV_PC98_STRIDE * LRV_PC98_H)      /* 32 000 */

/* ---- trace events (test automation; lrvne/trace.h has the kinds every engine shares) ----- */

/* Every event carries the script offset of the statement being executed, so a driver can
 * match it with the bytecode. The names (main.c) are what the control protocol prints. */
enum xe_trace_kind {
    XE_TR_SCRIPT = LRV_TR_ENGINE,   /* name = script; a0 = -1 at load */
    XE_TR_PAGE,         /* a0 = page number */
    XE_TR_LABEL,        /* a0 = label id entered after a menu selection */
    XE_TR_FLAG,         /* a0 = flag, a1 = value */
    XE_TR_OP,           /* a0 = opcode, a1..a3 = first int operands, n = int count, name = str */
    XE_TR_MSG,          /* text = message, a0 = instant flag */
    XE_TR_MSG_END,      /* message fully printed (before the end wait) */
    XE_TR_PROMPT,       /* a0 = 1 overflow prompt, 0 end-of-message prompt */
    XE_TR_MENU,         /* items/n = options, a0 = cursor, a1 = 1 a menu of the engine's own */
    XE_TR_MENU_SEL,     /* a0 = selection (-1 cancel, -2 help) */
    XE_TR_SYSMENU,      /* a0 = result (0 cancel, 1 title, 2 quit) */
    XE_TR_TITLE_MENU,   /* a0 = rows (3 title, 2 omake) */
    XE_TR_TITLE_SEL,    /* a0 = row (-1 cancel) */
    XE_TR_GDT,          /* name = image, a0 = kind (0 load, 1 buf, 2 diff, 3 page1), a1 = ok */
    XE_TR_GDISP,        /* G_DISP (buffers -> page) */
    XE_TR_EFFECT,       /* a0 = 0 blind, 1 centre-open, 2 frame close, 3 dissolve; a1 = 0 start,
                           1 end */
    XE_TR_FADE,         /* a0 = delay, a1 = 0 start, 1 end */
    XE_TR_ADT,          /* name = animation (load); a0 = -1 load, -2 play start, -3 play end,
                           else frame index; a1 = page */
    XE_TR_BLINK,        /* a0 = -1 start, -2 stop, else strip frame copied */
    XE_TR_KEY,          /* a0 = command code consumed by the engine (enum xe_cmd) */
    XE_TR_SAVE,         /* name = slot */
    XE_TR_LOAD,         /* name = slot, a0 = loaded */
    XE_TR_PROGRESS,     /* a0 = bit */
    XE_TR_BOX,          /* text = error / message box text, a0 = 1 error box */
    XE_TR_SEQ,          /* a0 = 0 logo, 1 opening, 2 staff roll; a1 = 0 start, 1 end */
    XE_TR_CLEAR,        /* a0 = 0 VRAM page cleared (a1 = page), 1 off-screen buffers cleared */
    XE_TR_VOICE,        /* name = clip, a0 = part, a1 = enum lrv_voice_event (docs/voice.md) */
    XE_TR_KIND_END
};
/* script errors are LRV_TR_ERROR: name = script, a0 = code, a1 = page, a2 = label */

#define XE_TRACE_NAMES \
    "script", "page", "label", "flag", "op", "msg", "msg_end", "prompt", "menu", "menu_sel", \
    "sysmenu", "title_menu", "title_sel", "gdt", "gdisp", "effect", "fade", "adt", "blink", \
    "key", "save", "load", "progress", "box", "seq", "clear", "voice"

/* a0 of LRV_TR_WAIT */
enum xe_wait_kind { XE_WAIT_TICKS = 0, XE_WAIT_KEY, XE_WAIT_OK, XE_WAIT_MUSIC, XE_WAIT_VOICE };
/* reasons for leaving a key wait (a2 of the leave event) */
enum xe_wait_leave { XE_LEAVE_KEY = 0, XE_LEAVE_CTRL, XE_LEAVE_TIMEOUT, XE_LEAVE_QUIT };

/* ---- input: the commands of kbd_read_cmd / input_get_cmd (platform.md §3.1) ------------- */

enum xe_cmd {
    XE_CMD_CANCEL = 0, XE_CMD_UP = 1, XE_CMD_DOWN = 2, XE_CMD_LEFT = 3, XE_CMD_RIGHT = 4,
    XE_CMD_OK = 5, XE_CMD_OTHER = 6, XE_CMD_NONE = 7, XE_CMD_HELP = 8, XE_CMD_F1 = 9
    /* f.1 .. f.5 are XE_CMD_F1 .. XE_CMD_F1 + 4 */
};

/* ---- the pack ---------------------------------------------------------------------------- */

/* The engine's entry types (lrvne/pack.h; tools/mkpack.py writes them). A file of the game is
 * an entry under its base name (without the extension), byte for byte the original; scripts are entries of
 * a language, decompressed. Fonts, string tables, settings and voice cues are lrvne's types. */
enum xe_pack_type {
    XE_PT_GDT = LRV_PT_ENGINE,  /* DA1 picture */
    XE_PT_ADT,                  /* ADT animation */
    XE_PT_MUSIC,                /* PMD .M song */
    XE_PT_EFC,                  /* PMD FM effect bank (.EFC) */
    XE_PT_SSGEFC,               /* PMD's built-in SSG effect table (from PMD.COM) */
    XE_PT_SCRIPT                /* .CC bytecode, decompressed */
};

/* What makes the engine run one particular game: the pack's "profile" entry (lrvne/profile.h;
 * game/profile.c reads it, docs/profile.md lists the keys). */
#define XE_NAME_MAX   16                /* a game file's base name (8 characters) and a NUL */
#define XE_TITLE_ROWS 3                 /* the title menu: start, load, extras */
#define XE_RULES_MAX  8
struct xe_profile {
    struct lrv_profile_base base;       /* id, title, lang, modules */
    char title_image[XE_NAME_MAX];      /* the title menu's picture */
    int title_rows[XE_TITLE_ROWS];      /* the top line of each of its bars */
    int ntitle_rows;
    int title_bar[3];                   /* the bars: left byte column, width in bytes, lines */
    int ntitle_bar;
    char start[XE_NAME_MAX];            /* the script a new game starts with ... */
    char start_if_clear[96];            /* ... unless one of these MASK:SCRIPT rules says */
    char load_script[XE_NAME_MAX];      /* the title menu's load */
    char omake_image[XE_NAME_MAX];      /* the extras menu: its picture (a difference picture), */
    char omake_scripts[64];             /* its scripts, one per row, */
    int omake_unlock;                   /* the progress bits that unlock it (0: always open) */
    char se_bank[XE_NAME_MAX];          /* the FM sound effect bank, "" = none */
    int music_buffer;                   /* bytes of the driver's music buffer (PMD /M) */
    int music_keep_after_fade;          /* the driver plays on after a fade (PMD /F) */
    int voice_music_volume;             /* the music's volume while the voice is on, percent */

    /* from the above */
    struct { unsigned mask; char script[XE_NAME_MAX]; } rule[XE_RULES_MAX];
    int nrules;
    char omake[XE_TITLE_ROWS][XE_NAME_MAX];
    int nomake;
};

/* ---- text ------------------------------------------------------------------------------- */

enum xe_wrap_mode { XE_WRAP_JA = 0, XE_WRAP_EN = 1 };

/* The encoding of the current language's text (text/enc.c): cp932 (the original bytecode)
 * or UTF-8 (translations), with fonts keyed accordingly (lrvne/font.h). */
enum xe_text_enc { XE_ENC_CP932 = 0, XE_ENC_UTF8 = 1 };

/* A cell of the text layer (the PC-98 text VRAM, used only for the prompts and the boxes). */
#define XE_OVERLAY_MAX 128
struct xe_overlay_cell {
    uint8_t col, row;       /* 80 x 25 text cells (8 x 16 px) */
    uint16_t code;          /* glyph code in the font selected by `wide` */
    uint8_t wide;           /* 1 = 16-wide glyph (font16), 0 = 8-wide (font8) */
    uint8_t colour;         /* palette index of the glyph */
    uint8_t attr;           /* XE_OVL_* */
};
#define XE_OVL_BLINK   1
#define XE_OVL_REVERSE 2

/* ---- blink animator (graphics.md §3.5) -------------------------------------------------- */
struct xe_blink {
    int active;
    int sx, sy, dx, dy, w, h;   /* DS:1B56.. (x/w in bytes, y/h in lines) */
    int step, counter, frame, dir;
};

/* ---- the state -------------------------------------------------------------------------- */

/* What the command line chose, beside what every engine has (lrv_run_cfg). */
struct xe_options {
    int voice_strict;           /* 1 = only CANCEL cuts a playing clip (the Windows release) */
    int se;                     /* --render-wav: sound effect to start, 0 = none */
    double se_at;               /* ... at this many seconds */
};

struct xe_state {
    const struct lrv_hal *hal;
    struct lrv_run_cfg cfg;
    struct xe_options opt;
    struct lrv_sys sys;                 /* frames, the input queue, quit, traces */
    struct lrv_pack pack;               /* its language is the one played */
    struct xe_profile profile;

    /* graphics (graphics.md §1, §3): VRAM pages 0 and 1 (port A6h access, A4h display, the
     * DAC) and the off-screen "da_data" buffers DS:13DC are the screen's (buffer 0) */
    struct lrv_screen screen;
    uint8_t pal_target[48];             /* g_gdt_palette DS:13EC (fade-in target) */
    int rect_off, rect_wb, rect_h;      /* display rect DS:141C/141E/1420 (off = y*80+x) */
    int parts_off, parts_wb, parts_h;   /* G_LOAD_PARTS rect DS:1431/1433/1435 */
    char image_name[40];                /* DS:1422 */
    struct xe_blink blink;

    /* text (engine.md §6, §7) */
    struct lrv_font font16, font8;
    int wrap_mode;
    int text_enc;                       /* enum xe_text_enc */
    int text_x, text_y;                 /* DS:07D8 (byte column), DS:07DA (line) */
    int text_fg, text_bg;               /* DS:07DC, DS:07DE */
    int menu_x, menu_y;                 /* DS:07E0, DS:07E2 */
    struct xe_overlay_cell overlay[XE_OVERLAY_MAX];
    int overlay_count;
    int overlay_blink_on;               /* current phase of blinking cells */
    int overlay_blink_counter;
    int input_from_mouse;               /* DS:13B6 */

    /* input: what the engine reads of lrv_sys (input.c); the motion since mouse_set_pos is
     * lrv_sys's counter, sys.motion_x / motion_y */
    int in_sysmenu;                     /* trace: menus shown by the engine, not a script */
    int trace_quiet;                    /* trace: no tick-wait events (inside fades etc.) */

    /* language strings */
    uint8_t *strings_blob;
    uint32_t strings_size;

    /* sound (snd/snd.c): the driver, and the path behind it (write queue, chip, mixer) */
    struct lrv_pmd pmd;
    struct lrv_music music;

    /* voice (voice/voice.c): the clip player */
    struct lrv_voice voice;

    /* game */
    uint8_t progress;                   /* the progress file's byte DS:06E8 */
    struct cc_vm vm;
    uint8_t *script_buf;                /* current script (pack memory) */
    uint32_t script_size;
};

extern struct xe_state xe;

/* app.c: the engine as the program `program` (games/main.c registers the modules first) */
int xe_main(const char *program, int argc, char **argv);

/* game/game.c: the functions of struct lrv_app (app.c) */
int xe_run(void *ctx, const struct lrv_hal *hal, const struct lrv_run_cfg *cfg);
/* snd/snd.c */
int xe_render_music(void *ctx, const struct lrv_hal *hal, const struct lrv_run_cfg *cfg,
                    const char *name, double seconds, uint32_t *rate,
                    void (*sink)(void *sctx, const int16_t *pcm, int frames), void *sctx);

/* game/profile.c: the profile of xe.pack into xe.profile; LRV_OK, or what is wrong (logged) */
int profile_load(void);

/* input.c: the frame, the waits and the commands (platform.md §3, §4) */
void frame_wait(void);              /* one frame: lrv_sys_frame_wait, then the engine's input */
void wait_vsync(int n);             /* wait_vsync 0DCA:0012: n frames; CTRL held cuts it short */
int  ctrl_held(void);
int  quitting(void);                /* the player asked to quit: every wait returns at once */
int  kbd_read_cmd(void);            /* non-blocking: the next key command or XE_CMD_NONE */
void kbd_flush(void);
int  input_get_cmd(void);           /* blocking, keyboard or mouse */
int  poll_cmd(void);                /* non-blocking, keyboard or mouse; XE_CMD_NONE if none */
int  wait_ok_key(void);             /* blocking "press a key"; 1 if skipped by CTRL */

#endif
