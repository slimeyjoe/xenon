/* Scenario script VM - docs/re/scripts.md, opcodes.md.
 *
 * A 1:1 port of the interpreter in AGS.EXE segment 088D: the token encoding, page/IF/SEN
 * grammar, searches, menu loops and error behaviour are reproduced exactly, including the
 * quirks (page fall-through, a true IF leaving its block, first duplicate page wins, unknown
 * opcodes skipped silently, every script error fatal).
 *
 * The VM is host-independent: everything that touches the engine (messages, menus, graphics,
 * sound, saves, loading other scripts) goes through struct cc_host. With a NULL host every
 * callback is a no-op, which is what the static checker and tests use. */
#ifndef XE_VM_CC_H
#define XE_VM_CC_H

#include <stddef.h>
#include <stdint.h>

#define CC_FLAG_BYTES   0x100          /* 2048 one-bit flags */
#define CC_MAX_OPTIONS  9              /* error 12 on the 10th */
#define CC_NAME_LEN     40             /* DS:137E script name buffer */
#define CC_SUBMENU_DIM  8              /* DS:12FE int[8][8] */

/* Tokens (scripts.md §2) */
#define CC_T_INT    0xFB
#define CC_T_STR    0xFD
#define CC_T_FLAG   0xFE
#define CC_T_LABEL  0xFF
#define CC_T_FC     0xFC

/* Structural bytes */
#define CC_PAGE     0x01
#define CC_PEND     0x02
#define CC_SEN      0x04
#define CC_SENEND   0x05
#define CC_JMP      0x06
#define CC_FILE     0x07
#define CC_END      0x08
#define CC_THEN     0x0A
#define CC_IFEND    0x0B
#define CC_EQ       0x12
#define CC_IF       0x13
#define CC_CONDEND  0x14
#define CC_AND      0x15
#define CC_SENMARK  0x1B
#define CC_LOAD     0x1C
#define CC_EXIT     0x1F
#define CC_SAVE     0x19
#define CC_DISK     0x1D
#define CC_COMPLETE 0x37

/* Error codes = indexes into the engine's message table g_cc_errmsg (scripts.md §3.1). */
enum cc_error {
    CC_E_SYNTAX = 0, CC_E_PAGENUM = 1, CC_E_ENDPEND = 2, CC_E_SEN = 3, CC_E_IF = 4,
    CC_E_MUSIC_LOAD = 5, CC_E_SABUN_LOAD = 6, CC_E_G_LOAD = 7, CC_E_ASSIGN = 8, CC_E_JMP = 9,
    CC_E_NOPEND = 10, CC_E_LABEL = 11, CC_E_TOOMANY = 12, CC_E_BLOCK = 13, CC_E_LABELEND = 14,
    CC_E_JMPTARGET = 15, CC_E_NONE = -1
};

struct cc_vm;

/* Opcode operand specs for the generic handlers: 'i' = required int, 'o' = optional int
 * (default 1), 's' = required string; the error raised when a string is missing is per op. */
struct cc_opinfo {
    uint8_t op;
    const char *spec;
    const char *name;
    int8_t str_error;      /* error code when the string operand is missing (-1 = syntax) */
};

/* The dispatch table of 088D:094C in C form (53 entries, terminated by op 0). */
extern const struct cc_opinfo cc_optable[];

/* Services the VM needs from the engine. Any pointer may be NULL (no-op). */
struct cc_host {
    void *ctx;
    /* FD message statement: text is cp932/ASCII bytes, NUL terminated. */
    void (*message)(void *ctx, const uint8_t *text);
    /* select_menu: items[0..count-1] option texts; returns index, -1 cancel, -2 HELP. */
    int (*menu)(void *ctx, const uint8_t *const *items, int count, int cursor);
    /* cc_system_menu: returns 0 = cancel (back to the choice), 1 = return to title,
     * 2 = quit the game. */
    int (*system_menu)(void *ctx);
    /* Generic opcode with parsed operands (ints[0..nints-1] for int specs; str for 's'). */
    void (*op)(void *ctx, uint8_t op, const int *ints, int nints, const uint8_t *str);
    /* SAVE "name": write the current state. */
    void (*save)(void *ctx, struct cc_vm *vm, const uint8_t *name);
    /* LOAD "name": read a save. Returns 1 if a save was loaded (the host has already called
     * cc_vm_set_script if the script changed and cc_vm_restore to set flags/page), 0 if no
     * data (the opcode then does nothing). */
    int (*load)(void *ctx, struct cc_vm *vm, const uint8_t *name);
    /* DISK "X": floppy change request (no-op in HDD mode). */
    void (*disk)(void *ctx, const uint8_t *letter);
    /* COMPLETE n: progress bit. */
    void (*progress)(void *ctx, int n);
    /* Script error: the message (cp932) with the engine's "%s page=%d, LABEL=D%d, %s" context
     * already formatted; fatal in the original. The VM also stops itself. */
    void (*error)(void *ctx, int code, const char *script, int page, int label);
    /* Test automation: structural events. what = XE_TR_PAGE (a = page), XE_TR_LABEL
     * (a = label id), XE_TR_FLAG (a = flag, b = value). May be NULL. */
    void (*trace)(void *ctx, int what, int a, int b);
};

struct cc_vm {
    const struct cc_host *host;
    const uint8_t *buf;      /* current script bytecode (owned by the engine) */
    uint32_t size;
    char script[CC_NAME_LEN];           /* DS:137E current script name (without .cc) */
    uint8_t flags[CC_FLAG_BYTES];       /* DS:00F2 */
    int cur_page;                       /* DS:1294 */
    int cur_label;                      /* DS:1296 (-1 = none) */
    int page_jump;                      /* DS:1292 */
    int file_jump;                      /* DS:1290 */
    int leave;                          /* DS:13A6 g_leave_script */
    int quit;                           /* DS:13A8 g_quit_game */
    int resume_pending;                 /* DS:00AE: after LOAD, continue at resume_pos */
    uint32_t resume_pos;                /* DS:12FC */
    int menu_cursor;                    /* DS:00F0 */
    int submenu_cursor[CC_SUBMENU_DIM][CC_SUBMENU_DIM];   /* DS:12FE */
    const uint8_t *menu_texts[CC_MAX_OPTIONS + 1];        /* DS:1298, NULL terminated */
    int menu_ids[CC_MAX_OPTIONS + 1];                     /* DS:12C0 */
    int err;                            /* CC_E_NONE or the error raised (stops execution) */
    uint32_t pos;                       /* offset of the statement being executed (trace) */
};

void cc_vm_init(struct cc_vm *vm, const struct cc_host *host);
void cc_vm_clear_flags(struct cc_vm *vm);                   /* flags_clear_all 088D:1258 */
void cc_vm_set_script(struct cc_vm *vm, const char *name, const uint8_t *buf, uint32_t size);

int  cc_flag_test(const struct cc_vm *vm, int n);
void cc_flag_set(struct cc_vm *vm, int n, int v);

/* Run the loaded script from its first page (or from the resume position after LOAD) until
 * FILE / EXIT / error. Returns 0 normally, -1 on a script error (vm->err set). */
int cc_run(struct cc_vm *vm);

/* Searches (also used by save/load): offset of PAGE n, or -1. */
int32_t cc_find_page(struct cc_vm *vm, int n);

/* ---- static checker (ccdis --check equivalent), used by the tests and the pack builder */
struct cc_check_result {
    int pages;
    int unknown_ops;
    int uncovered;
    int overlap;
    int lenbyte_mismatch;
    int warnings;
    int parse_error;          /* 0, or 1 + byte offset of the first parse error */
    int strings;
};
int cc_check(const uint8_t *buf, uint32_t size, struct cc_check_result *r, uint8_t *coverage);

#endif
