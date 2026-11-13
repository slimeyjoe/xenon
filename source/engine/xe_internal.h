/* Engine-wide state shared by the engine modules (not for backends).
 *
 * The original engine is one program with global state (DGROUP); the port keeps that shape in
 * one structure, `xe`, so the modules can be read next to the RE documents. Names in comments
 * refer to the DGROUP globals of docs/re/globals.md. */
#ifndef XE_INTERNAL_H
#define XE_INTERNAL_H

#include <stddef.h>
#include <stdint.h>
#include "xe.h"
#include "xe_hal.h"
#include "gfx/planes.h"
#include "pack/pack.h"
#include "vm/cc.h"

#define XE_TICK_HZ   56.42

/* ---- fonts / text --------------------------------------------------------------------- */

struct xe_font {
    uint8_t *data;          /* XFNT blob (pack memory) */
    uint32_t size;
    int cell_w, cell_h;
    int count;
    const uint8_t *codes;   /* count x u16 LE, sorted */
    const uint8_t *glyphs;  /* count x glyph bytes */
    int glyph_bytes;
};

enum xe_wrap_mode { XE_WRAP_JA = 0, XE_WRAP_EN = 1 };

/* Text encoding of the current language (text/enc.c). cp932: the original bytecode; glyph
 * codes are JIS X 0201 bytes (font8) and JIS X 0208 codes (font16). UTF-8: both fonts of the
 * language are keyed by Unicode code point. */
enum xe_text_enc { XE_ENC_CP932 = 0, XE_ENC_UTF8 = 1 };

/* Text-layer overlay cell (the PC-98 text VRAM is used only for the prompts and error box). */
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

/* ---- blink animator (graphics.md §3.5) ------------------------------------------------- */
struct xe_blink {
    int active;
    int sx, sy, dx, dy, w, h;   /* DS:1B56.. (x/w in bytes, y/h in lines) */
    int step, counter, frame, dir;
};

/* ---- the state ------------------------------------------------------------------------ */

struct xe_state {
    const struct xe_hal *hal;
    struct xe_config cfg;
    struct xpak pak;
    int lang;

    /* graphics (graphics.md §1, §3) */
    uint8_t *planemem;                  /* 3 x 4 planes */
    struct xe_planes page[2];           /* VRAM pages 0 and 1 */
    struct xe_planes gbuf;              /* off-screen "da_data" buffers DS:13DC */
    struct xe_rect dirty[2];            /* per page, pending upload */
    int access_page;                    /* DS:1468 (port A6h) */
    int display_page;                   /* DS:146A (port A4h) */
    int page_switched;                  /* display page changed since last present */
    int graphics_on;                    /* INT 18h 40h/41h */
    int scroll_line;                    /* G_SCROLL */
    uint8_t pal[48];                    /* hardware palette shadow DS:1438 */
    uint8_t pal_target[48];             /* g_gdt_palette DS:13EC (fade-in target) */
    int rect_off, rect_wb, rect_h;      /* display rect DS:141C/141E/1420 (off = y*80+x) */
    int parts_off, parts_wb, parts_h;   /* G_LOAD_PARTS rect DS:1431/1433/1435 */
    char image_name[40];                /* DS:1422 */
    struct xe_blink blink;

    /* text (engine.md §6, §7) */
    struct xe_font font16, font8;
    int wrap_mode;
    int text_enc;                       /* enum xe_text_enc */
    int text_x, text_y;                 /* DS:07D8 (byte column), DS:07DA (line) */
    int text_fg, text_bg;               /* DS:07DC, DS:07DE */
    int menu_x, menu_y;                 /* DS:07E0, DS:07E2 */
    struct xe_overlay_cell overlay[XE_OVERLAY_MAX];
    int overlay_count;
    int overlay_blink_on;               /* current phase of blinking cells */
    int input_from_mouse;               /* DS:13B6 */

    /* input / timing */
    struct xe_input in;
    uint32_t tick;
    int in_sysmenu;                     /* trace: menus shown by the engine, not a script */
    int trace_quiet;                    /* trace: suppress tick-wait events (inside fades etc.) */
    int quit_requested;
    int emergency_quit;

    /* language strings */
    uint8_t *strings_blob;
    uint32_t strings_size;

    /* game */
    uint8_t progress;                   /* XEN.GDT byte DS:06E8 */
    struct cc_vm vm;
    uint8_t *script_buf;                /* current script (pack memory) */
    uint32_t script_size;
    int script_lang_missing;
};

extern struct xe_state xe;

/* sys.c: the single blocking point and presentation */
void sys_frame_wait(void);                       /* one tick: events, audio, blink, present */
void sys_wait_ticks(int n);                      /* wait_vsync(n): CTRL-held cuts it short */
int  sys_ctrl_held(void);
/* The player asked to quit (close gadget / window close, Ctrl-C, Shift+Esc). From then on every
 * wait returns at once, the frame ticks neither present nor wait, and the script stops after
 * the statement it is in: the game leaves within the work already started (sys.c). */
int  sys_quitting(void);
void sys_present_now(void);                      /* present without waiting (page flips) */
void sys_log(int level, const char *fmt, ...);
/* test automation events (xe_hal.h xe_trace_kind); no-ops without hal->trace */
void sys_trace(int kind, const char *name, const uint8_t *text, int a0, int a1, int a2, int a3);
void sys_trace_items(int kind, const uint8_t *const *items, int n, int a0);
#define XE_TRACING() (xe.hal->trace != 0)
void sys_fatal(const char *fmt, ...);
void *sys_alloc(size_t n);
void sys_free(void *p, size_t n);

#endif
