/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Scenario script interpreter - port of AGS.EXE segment 088D (docs/re/scripts.md §4).
 * Function names follow the listing (names.tsv). Positions are byte offsets into vm->buf. */
#include <string.h>
#include "vm/cc.h"

/* The dispatch table 088D:094C. Structural bytes have an empty spec and are handled in
 * cc_exec itself; everything else is a generic opcode with parsed operands. */
const struct cc_opinfo cc_optable[] = {
    { 0x02, "",       "PEND",          -1 },
    { 0x03, "",       "CLEAR_LOWER",   -1 },
    { 0x06, "i",      "JMP",           -1 },
    { 0x07, "s",      "FILE",          -1 },
    { 0x08, "",       "END",           -1 },
    { 0x0B, "",       "END",           -1 },
    { 0x0C, "s",      "G_LOAD",        CC_E_G_LOAD },
    { 0x0D, "",       "G_DISP",        -1 },
    { 0x0E, "s",      "MUSIC_LOAD",    CC_E_MUSIC_LOAD },
    { 0x0F, "",       "MUSIC_PLAY",    -1 },
    { 0x10, "",       "MUSIC_STOP",    -1 },
    { 0x11, "i",      "WINDOW",        -1 },
    { 0x13, "",       "IF",            -1 },
    { 0x16, "s",      "G_SABUN_LOAD",  CC_E_SABUN_LOAD },
    { 0x19, "s",      "SAVE",          -1 },
    { 0x1C, "s",      "LOAD",          -1 },
    { 0x1D, "s",      "DISK",          -1 },
    { 0x1F, "",       "EXIT",          -1 },
    { 0x20, "i",      "WAIT",          -1 },
    { 0x21, "o",      "FADEIN",        -1 },
    { 0x22, "o",      "FADEOUT",       -1 },
    { 0x23, "",       "G_CLEAR",       -1 },
    { 0x24, "i",      "G_EFFECT",      -1 },
    { 0x26, "",       "GRAPH_OFF",     -1 },
    { 0x27, "",       "GRAPH_ON",      -1 },
    { 0x28, "s",      "G_LOAD_OVL",    CC_E_G_LOAD },
    { 0x29, "o",      "OPENING",       -1 },
    { 0x2A, "s",      "ADT_LOAD",      CC_E_SABUN_LOAD },
    { 0x2B, "",       "ADT_FREE",      -1 },
    { 0x2D, "",       "ADT_PLAY",      -1 },
    { 0x2E, "iiiiii", "ANIM_START",    -1 },
    { 0x2F, "",       "ANIM_STOP",     -1 },
    { 0x30, "i",      "MUSIC_FADE",    -1 },
    { 0x31, "",       "MUSIC_FADEWAIT", -1 },
    { 0x32, "i",      "G_SCROLL",      -1 },
    { 0x33, "s",      "G_LOAD_BUF",    CC_E_SABUN_LOAD },
    { 0x34, "s",      "G_LOAD_PARTS",  CC_E_SABUN_LOAD },
    { 0x35, "",       "G_PARTS_COPY",  -1 },
    { 0x36, "iiii",   "PALETTE",       -1 },
    { 0x37, "i",      "COMPLETE",      -1 },
    { 0x38, "iiii",   "TEXTPOS",       -1 },
    { 0x39, "ii",     "TEXTCOLOR",     -1 },
    { 0x3A, "i",      "MUSIC_VOL",     -1 },
    { 0x40, "",       "MSG_CLEAR",     -1 },
    { 0x42, "o",      "FADEWHITE",     -1 },
    { 0x43, "i",      "VRAM_WPAGE",    -1 },
    { 0x44, "i",      "VRAM_DPAGE",    -1 },
    { 0x45, "i",      "SE_PLAY",       -1 },
    { 0x46, "",       "SE_STOP",       -1 },
    { 0x47, "",       "STAFFROLL",     -1 },
    { 0x48, "i",      "MUSIC_WAIT",    -1 },
    { 0xFD, "",       "message",       -1 },
    { 0xFE, "",       "assign",        -1 },
    { 0, 0, 0, 0 }
};

static const struct cc_opinfo *op_lookup(uint8_t op)
{
    const struct cc_opinfo *o;
    for (o = cc_optable; o->op; o++)
        if (o->op == op)
            return o;
    return 0;
}

/* ---- helpers ------------------------------------------------------------------------------ */

/* the statement being executed (for the host's trace) */
static void set_pos(struct cc_vm *vm, uint32_t pos)
{
    vm->pos = pos;
    if (vm->host && vm->host->at)
        vm->host->at(vm->host->ctx, pos);
}

static uint8_t at(const struct cc_vm *vm, uint32_t pos)
{
    return pos < vm->size ? vm->buf[pos] : 0;
}

static int rd_s16(const struct cc_vm *vm, uint32_t pos)
{
    return (int)(int16_t)(at(vm, pos) | (at(vm, pos + 1) << 8));
}

static void cc_error(struct cc_vm *vm, int code)
{
    if (vm->err != CC_E_NONE)
        return;
    vm->err = code;
    vm->leave = 1;
    vm->quit = 1;
    if (vm->host && vm->host->error)
        vm->host->error(vm->host->ctx, code, vm->script, vm->cur_page, vm->cur_label);
}

#define FAILED(vm) ((vm)->err != CC_E_NONE)

/* cc_skip_token 088D:0514: size of the token at pos. */
static uint32_t cc_skip_token(const struct cc_vm *vm, uint32_t pos)
{
    uint8_t b = at(vm, pos);
    if (b == CC_T_INT || b == CC_T_FLAG || b == CC_T_LABEL)
        return pos + 3;
    if (b == CC_T_STR) {
        pos++;                              /* scan for the NUL from the length byte */
        while (pos < vm->size && vm->buf[pos] != 0)
            pos++;
        return pos + 1;
    }
    return pos + 1;
}

/* cc_skip_stmt 088D:04A7: statement-level skip used by the searches. */
static uint32_t cc_skip_stmt(const struct cc_vm *vm, uint32_t pos)
{
    uint8_t b = at(vm, pos);
    if (b == CC_IF) {
        pos++;
        while (pos < vm->size && at(vm, pos) != CC_CONDEND)
            pos = cc_skip_token(vm, pos);
        return pos + 1;
    }
    if (b == CC_JMP || b == CC_FILE || b == 0x0C || b == 0x0E)
        return cc_skip_token(vm, cc_skip_token(vm, pos));
    return cc_skip_token(vm, pos);
}

/* cc_find_byte: first statement starting with byte b at or after pos; -1 if none. */
static int32_t cc_find_byte(const struct cc_vm *vm, uint32_t pos, uint8_t b)
{
    while (pos < vm->size) {
        if (vm->buf[pos] == b)
            return (int32_t)pos;
        pos = cc_skip_stmt(vm, pos);
    }
    return -1;
}

/* cc_find_page 088D:0ECD: first `01 FB n` in the file. */
int32_t cc_find_page(struct cc_vm *vm, int n)
{
    uint32_t pos = 0;
    for (;;) {
        int32_t p = cc_find_byte(vm, pos, CC_PAGE);
        if (p < 0)
            return -1;
        if (at(vm, (uint32_t)p + 1) == CC_T_INT && rd_s16(vm, (uint32_t)p + 2) == n)
            return p;
        pos = (uint32_t)p + 1;
    }
}

/* cc_find_label 088D:0F2E: `FF id` between SENEND and the next PEND; returns the position
 * after the label token. */
static int32_t cc_find_label(struct cc_vm *vm, uint32_t pos, int id)
{
    while (pos < vm->size) {
        uint8_t b = vm->buf[pos];
        if (b == CC_PEND) {
            cc_error(vm, CC_E_LABEL);
            return -1;
        }
        if (b == CC_T_LABEL && rd_s16(vm, pos + 1) == id)
            return (int32_t)(pos + 3);
        pos = cc_skip_stmt(vm, pos);
    }
    cc_error(vm, CC_E_NOPEND);
    return -1;
}

/* cc_skip_block 088D:0DFB: skip a false IF body to its matching 0B; returns the position
 * after it. Nested IFs are skipped recursively. */
static uint32_t cc_skip_block(struct cc_vm *vm, uint32_t pos)
{
    while (pos < vm->size) {
        uint8_t b = vm->buf[pos];
        if (b == CC_IFEND)
            return pos + 1;
        if (b == CC_PEND || b == CC_END) {
            cc_error(vm, CC_E_ENDPEND);
            return pos;
        }
        if (b == CC_THEN || b == CC_T_LABEL) {
            cc_error(vm, CC_E_BLOCK);
            return pos;
        }
        if (b == CC_IF) {
            pos = cc_skip_stmt(vm, pos);          /* condition up to and including 14 */
            if (at(vm, pos) != CC_THEN) {
                cc_error(vm, CC_E_IF);
                return pos;
            }
            pos = cc_skip_block(vm, pos + 1);
            if (FAILED(vm))
                return pos;
            continue;
        }
        pos = cc_skip_token(vm, pos);
    }
    cc_error(vm, CC_E_ENDPEND);
    return pos;
}

/* cc_eval_cond 088D:1104: `FE f 12 FB v` terms joined by 15, closed by 14. Sets *result and
 * returns the position after 14. */
static uint32_t cc_eval_cond(struct cc_vm *vm, uint32_t pos, int *result)
{
    int r = 1;
    for (;;) {
        uint8_t b = at(vm, pos);
        if (b == CC_CONDEND) {
            *result = r;
            return pos + 1;
        }
        if (b == CC_AND) {
            pos++;
            continue;
        }
        if (b != CC_T_FLAG || at(vm, pos + 3) != CC_EQ || at(vm, pos + 4) != CC_T_INT) {
            cc_error(vm, CC_E_IF);
            *result = 0;
            return pos;
        }
        {
            int f = rd_s16(vm, pos + 1);
            int v = rd_s16(vm, pos + 5);
            if (f < 0 || f >= CC_FLAG_BYTES * 8) {
                cc_error(vm, CC_E_ASSIGN);
                *result = 0;
                return pos;
            }
            if (cc_flag_test(vm, f) != v)
                r = 0;
        }
        pos += 7;
    }
}

int cc_flag_test(const struct cc_vm *vm, int n)
{
    return (vm->flags[n >> 3] >> (n & 7)) & 1;
}

void cc_flag_set(struct cc_vm *vm, int n, int v)
{
    if (v)
        vm->flags[n >> 3] |= (uint8_t)(1u << (n & 7));
    else
        vm->flags[n >> 3] &= (uint8_t)~(1u << (n & 7));
}

/* ---- statements --------------------------------------------------------------------------- */

static uint32_t cc_menu(struct cc_vm *vm, uint32_t pos, int depth, int parent);

static int transferred(const struct cc_vm *vm)
{
    return vm->page_jump || vm->file_jump || vm->leave || vm->resume_pending || FAILED(vm);
}

/* Generic opcode: parse the operands per spec and call the host. Returns the new position. */
static uint32_t cc_generic(struct cc_vm *vm, const struct cc_opinfo *o, uint32_t pos)
{
    int ints[8];
    int n = 0;
    const uint8_t *str = 0;
    const char *s;

    pos++;
    for (s = o->spec; *s; s++) {
        if (*s == 'i' || *s == 'o') {
            if (at(vm, pos) == CC_T_INT) {
                ints[n++] = rd_s16(vm, pos + 1);
                pos += 3;
            } else if (*s == 'o') {
                ints[n++] = 1;
            } else {
                cc_error(vm, CC_E_SYNTAX);
                return pos;
            }
        } else if (*s == 's') {
            if (at(vm, pos) != CC_T_STR) {
                cc_error(vm, o->str_error < 0 ? CC_E_SYNTAX : o->str_error);
                return pos;
            }
            str = vm->buf + pos + 2;
            pos = cc_skip_token(vm, pos);
        }
    }
    if (vm->host && vm->host->op)
        vm->host->op(vm->host->ctx, o->op, ints, n, str);
    return pos;
}

/* cc_exec 088D:05FC: run statements until the terminator byte; returns the position after
 * it, or the position returned by a transferring statement. */
static uint32_t cc_exec(struct cc_vm *vm, uint32_t pos, uint8_t term)
{
    while (!FAILED(vm)) {
        uint8_t b = at(vm, pos);
        const struct cc_opinfo *o;

        if (vm->leave)                          /* the host ended the run (quit): no more */
            return pos;                         /* statements, see cc_run */

        if (pos >= vm->size) {
            cc_error(vm, CC_E_SYNTAX);          /* fell off the file (debug1) */
            return pos;
        }
        if (b == term)
            return pos + 1;

        set_pos(vm, pos);
        switch (b) {
        case CC_T_STR:
            if (vm->host && vm->host->message)
                vm->host->message(vm->host->ctx, vm->buf + pos + 2);
            pos = cc_skip_stmt(vm, pos);
            continue;

        case CC_T_FLAG: {                       /* F<f> = v (cc_assign 088D:108D) */
            int f = rd_s16(vm, pos + 1);
            if (at(vm, pos + 3) != CC_EQ || at(vm, pos + 4) != CC_T_INT || f < 0 ||
                f >= CC_FLAG_BYTES * 8) {
                cc_error(vm, CC_E_ASSIGN);
                return pos;
            }
            cc_flag_set(vm, f, rd_s16(vm, pos + 5) != 0);
            if (vm->host && vm->host->trace)
                vm->host->trace(vm->host->ctx, CC_TRACE_FLAG, f, rd_s16(vm, pos + 5) != 0);
            pos += 7;
            continue;
        }

        case CC_IF: {                           /* 088D:063C */
            int cond;
            pos = cc_eval_cond(vm, pos + 1, &cond);
            if (FAILED(vm))
                return pos;
            if (at(vm, pos) != CC_THEN) {
                cc_error(vm, CC_E_IF);
                return pos;
            }
            pos++;
            if (cond) {
                pos = cc_exec(vm, pos, CC_IFEND);
                if (!transferred(vm) && at(vm, pos) == CC_SENMARK)
                    pos++;
                return pos;                     /* a true IF leaves the enclosing block */
            }
            pos = cc_skip_block(vm, pos);
            if (at(vm, pos) == CC_SENMARK)
                pos++;
            continue;
        }

        case CC_JMP: {                          /* 088D:06E4 */
            int32_t target;
            vm->page_jump = 1;
            if (at(vm, pos + 1) != CC_T_INT) {
                cc_error(vm, CC_E_PAGENUM);
                return pos;
            }
            target = cc_find_page(vm, rd_s16(vm, pos + 2));
            if (target < 0) {
                cc_error(vm, CC_E_JMP);
                return pos;
            }
            return (uint32_t)target;
        }

        case CC_FILE: {                         /* 088D:071A */
            const uint8_t *name;
            size_t i;
            if (at(vm, pos + 1) != CC_T_STR) {
                cc_error(vm, CC_E_SYNTAX);
                return pos;
            }
            name = vm->buf + pos + 3;
            for (i = 0; i < CC_NAME_LEN - 1 && name[i]; i++)
                vm->script[i] = (char)name[i];
            vm->script[i] = 0;
            vm->file_jump = 1;
            return cc_skip_stmt(vm, pos);
        }

        case CC_EXIT:                           /* 088D:090C */
            vm->leave = 1;
            return pos + 1;

        case CC_PEND:
        case CC_END:
        case CC_IFEND:                          /* 088D:08E6: not the terminator */
            cc_error(vm, CC_E_ENDPEND);
            return pos;

        case CC_SAVE:                           /* op_save 088D:126A */
            if (at(vm, pos + 1) == CC_T_STR && vm->host && vm->host->save)
                vm->host->save(vm->host->ctx, vm, vm->buf + pos + 3);
            pos = cc_skip_stmt(vm, cc_skip_stmt(vm, pos));
            continue;

        case CC_LOAD:                           /* op_load 088D:1441 */
            if (at(vm, pos + 1) == CC_T_STR && vm->host && vm->host->load) {
                if (vm->host->load(vm->host->ctx, vm, vm->buf + pos + 3)) {
                    int32_t p = cc_find_page(vm, vm->cur_page);
                    if (p < 0) {
                        cc_error(vm, CC_E_JMP);
                        return pos;
                    }
                    vm->resume_pos = (uint32_t)p;
                    vm->resume_pending = 1;
                    return vm->resume_pos;
                }
            }
            pos = cc_skip_stmt(vm, cc_skip_stmt(vm, pos));
            continue;

        case CC_DISK:                           /* op_disk_change 088D:0F9C */
            if (at(vm, pos + 1) == CC_T_STR && vm->host && vm->host->disk)
                vm->host->disk(vm->host->ctx, vm->buf + pos + 3);
            pos = cc_skip_stmt(vm, cc_skip_stmt(vm, pos));
            continue;

        case CC_COMPLETE:                       /* op_set_progress 088D:1C19 */
            if (at(vm, pos + 1) != CC_T_INT) {
                cc_error(vm, CC_E_SYNTAX);
                return pos;
            }
            if (vm->host && vm->host->progress)
                vm->host->progress(vm->host->ctx, rd_s16(vm, pos + 2));
            pos += 4;
            continue;

        default:
            break;
        }

        o = op_lookup(b);
        if (!o) {
            pos = cc_skip_stmt(vm, pos);        /* unknown: skipped silently (088D:091C) */
            continue;
        }
        pos = cc_generic(vm, o, pos);
    }
    return pos;
}

/* cc_build_options 088D:2092: collect the visible options of the SEN at pos (04).
 * Returns the count, and the position of SENEND via *senend. */
static int cc_build_options(struct cc_vm *vm, uint32_t pos, uint32_t *senend)
{
    int count = 0;
    pos++;                                      /* 04 */
    for (;;) {
        uint8_t b = at(vm, pos);
        int show = 1;
        if (pos >= vm->size) {
            cc_error(vm, CC_E_SEN);
            return 0;
        }
        if (b == CC_SENEND) {
            *senend = pos;
            break;
        }
        if (b == CC_IF) {
            pos = cc_eval_cond(vm, pos + 1, &show);
            if (FAILED(vm))
                return 0;
        }
        if (at(vm, pos) != CC_T_STR) {
            cc_error(vm, CC_E_SEN);
            return 0;
        }
        if (show) {
            if (count > CC_MAX_OPTIONS - 1) {
                cc_error(vm, CC_E_TOOMANY);
                return 0;
            }
            vm->menu_texts[count] = vm->buf + pos + 2;
        }
        pos = cc_skip_token(vm, pos);
        if (at(vm, pos) != CC_T_LABEL) {
            cc_error(vm, CC_E_SEN);
            return 0;
        }
        if (show)
            vm->menu_ids[count++] = rd_s16(vm, pos + 1);
        pos += 3;
    }
    vm->menu_texts[count] = 0;
    return count;
}

/* Does the case body at pos contain a nested menu (04 before its 08)? */
static int has_nested_menu(const struct cc_vm *vm, uint32_t pos)
{
    while (pos < vm->size) {
        uint8_t b = vm->buf[pos];
        if (b == CC_END)
            return 0;
        if (b == CC_SEN)
            return 1;
        pos = cc_skip_stmt(vm, pos);
    }
    return 0;
}

/* Run one case body starting at pos (after `FF id`); returns the position reached. */
static uint32_t cc_run_case(struct cc_vm *vm, uint32_t pos, int depth, int sel)
{
    if (has_nested_menu(vm, pos)) {
        pos = cc_exec(vm, pos, CC_SENMARK);
        if (transferred(vm))
            return pos;
        return cc_menu(vm, pos, depth + 1, sel);
    }
    return cc_exec(vm, pos, CC_END);
}

/* cc_menu 088D:0A20 (depth 0) and cc_submenu 088D:0B99 (depth >= 1): pos is at 04.
 * Returns the position of the transfer that ended the menu, or, for a nested menu that was
 * cancelled / fell through, any position with no transfer flag set (the caller re-shows). */
static uint32_t cc_menu(struct cc_vm *vm, uint32_t pos, int depth, int parent)
{
    int *cursor = depth == 0 ? &vm->menu_cursor
                : &vm->submenu_cursor[parent & (CC_SUBMENU_DIM - 1)][depth & (CC_SUBMENU_DIM - 1)];

    for (;;) {
        uint32_t senend = 0;
        int count, sel, id;
        int32_t lpos;

        set_pos(vm, pos);
        count = cc_build_options(vm, pos, &senend);
        if (FAILED(vm))
            return pos;
        if (vm->host && vm->host->menu)
            sel = vm->host->menu(vm->host->ctx, vm->menu_texts, count, *cursor);
        else
            sel = count ? 0 : -1;
        if (sel < 0) {
            int r = 0;
            if (depth > 0 && sel == -1)
                return pos;                     /* cancel: back one level, no transfer */
            if (vm->host && vm->host->system_menu)
                r = vm->host->system_menu(vm->host->ctx);
            if (r == 1) {
                vm->leave = 1;
            } else if (r == 2) {
                vm->leave = vm->quit = 1;
            }
            if (vm->leave)
                break;
            continue;                           /* cancel: show the menu again */
        }
        *cursor = sel;
        id = vm->menu_ids[sel];
        vm->cur_label = id;
        if (vm->host && vm->host->trace)
            vm->host->trace(vm->host->ctx, CC_TRACE_LABEL, id, sel);
        lpos = cc_find_label(vm, senend, id);
        if (lpos < 0)
            return pos;
        {
            uint32_t p = cc_run_case(vm, (uint32_t)lpos, depth, sel);
            if (transferred(vm)) {
                pos = p;
                break;
            }
        }
        if (depth > 0)
            return pos;                         /* nested case without transfer: top menu again */
    }
    if (depth == 0)
        vm->menu_cursor = 0;                    /* 088D:0B6B */
    return pos;
}

/* cc_run_page 088D:035A */
static uint32_t cc_run_page(struct cc_vm *vm, uint32_t pos)
{
    int32_t p;

    if (at(vm, pos) != CC_PAGE) {
        cc_error(vm, CC_E_SYNTAX);              /* debug1 */
        return pos;
    }
    if (at(vm, pos + 1) != CC_T_INT) {
        cc_error(vm, CC_E_PAGENUM);             /* debug2 */
        return pos;
    }
    set_pos(vm, pos);
    vm->cur_page = rd_s16(vm, pos + 2);
    vm->cur_label = -1;
    if (vm->host && vm->host->trace)
        vm->host->trace(vm->host->ctx, CC_TRACE_PAGE, vm->cur_page, 0);
    pos += 4;

    if (at(vm, pos) == CC_SENMARK)
        return cc_menu(vm, pos + 1, 0, 0);

    /* find the first PEND or SEN of this page */
    p = (int32_t)pos;
    while ((uint32_t)p < vm->size && vm->buf[p] != CC_PEND && vm->buf[p] != CC_SEN)
        p = (int32_t)cc_skip_stmt(vm, (uint32_t)p);
    if ((uint32_t)p < vm->size && vm->buf[p] == CC_SEN) {
        pos = cc_exec(vm, pos, CC_SENMARK);
        if (transferred(vm))
            return pos;
        return cc_menu(vm, pos, 0, 0);
    }
    return cc_exec(vm, pos, CC_PEND);
}

int cc_run(struct cc_vm *vm)
{
    uint32_t pos;

    vm->err = CC_E_NONE;
    vm->file_jump = 0;
    if (vm->resume_pending) {
        pos = vm->resume_pos;
        vm->resume_pending = 0;
    } else {
        int32_t p = cc_find_byte(vm, 0, CC_PAGE);
        if (p < 0) {
            cc_error(vm, CC_E_SYNTAX);
            return -1;
        }
        pos = (uint32_t)p;
    }
    do {
        vm->page_jump = 0;
        vm->resume_pending = 0;
        pos = cc_run_page(vm, pos);
    } while (!vm->file_jump && !vm->leave && !FAILED(vm));
    return FAILED(vm) ? -1 : 0;
}

/* ---- state -------------------------------------------------------------------------------- */

void cc_vm_init(struct cc_vm *vm, const struct cc_host *host)
{
    memset(vm, 0, sizeof *vm);
    vm->host = host;
    vm->cur_label = -1;
    vm->err = CC_E_NONE;
}

void cc_vm_clear_flags(struct cc_vm *vm)
{
    memset(vm->flags, 0, sizeof vm->flags);
}

void cc_vm_set_script(struct cc_vm *vm, const char *name, const uint8_t *buf, uint32_t size)
{
    size_t i;
    for (i = 0; i < CC_NAME_LEN - 1 && name[i]; i++)
        vm->script[i] = name[i];
    vm->script[i] = 0;
    vm->buf = buf;
    vm->size = size;
}
