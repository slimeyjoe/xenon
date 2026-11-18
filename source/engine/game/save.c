/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Save slots and progress. Own format (endian-explicit), the content of the original
 * SAVEn.DAT (save-format.md §2) plus a header:
 *   "XSAV" u16 version=1, u16 reserved, u8 flags[256], char script[40], u16 page,
 *   char music[16], u8 music_started, char image[40], u8 lang, u8 pad[2], u32 crc
 * Progress: record "progress", 1 byte (the original's progress file, a bitmask of the endings
 * seen). */
#include <string.h>
#include "xe.h"
#include "lrvne/endian.h"
#include "game/save.h"
#include "game/game.h"
#include "gfx/gfx.h"
#include "snd/snd.h"
#include "text/text.h"
#include "vm/cc.h"

#define SAVE_SIZE (8 + 256 + 40 + 2 + 16 + 1 + 40 + 1 + 2 + 4)

static uint32_t crc32(const uint8_t *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    size_t i;
    int k;
    for (i = 0; i < n; i++) {
        c ^= p[i];
        for (k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return ~c;
}

void save_write(const char *name, struct cc_vm *vm)
{
    uint8_t buf[SAVE_SIZE];
    size_t o = 0;
    memset(buf, 0, sizeof buf);
    memcpy(buf, "XSAV", 4);
    lrv_wr16le(buf + 4, 1);
    o = 8;
    memcpy(buf + o, vm->flags, 256); o += 256;
    memcpy(buf + o, vm->script, 39); o += 40;
    lrv_wr16le(buf + o, (uint16_t)vm->cur_page); o += 2;
    memcpy(buf + o, music_get_name(), 15); o += 16;
    buf[o++] = (uint8_t)music_started();
    memcpy(buf + o, xe.image_name, 39); o += 40;
    buf[o++] = xe.pack.lang;
    o += 2;
    lrv_wr32le(buf + o, crc32(buf, o));
    o += 4;
    if (xe.hal->storage_write(name, buf, o) != 0)
        error_box(lang_str(LS_FILE_OPEN_ERROR));
}

int save_read(const char *name, struct cc_vm *vm)
{
    uint8_t buf[SAVE_SIZE];
    size_t len = 0, o;
    char script[40], music[16], image[40];
    int page, started;

    if (xe.hal->storage_read(name, buf, sizeof buf, &len) != 0 || len != SAVE_SIZE ||
        memcmp(buf, "XSAV", 4) != 0 || lrv_rd16le(buf + 4) != 1 ||
        crc32(buf, SAVE_SIZE - 4) != lrv_rd32le(buf + SAVE_SIZE - 4)) {
        msg_box(lang_str(LS_NO_SAVE_DATA));
        return 0;
    }
    o = 8;
    memcpy(vm->flags, buf + o, 256); o += 256;
    memcpy(script, buf + o, 40); script[39] = 0; o += 40;
    page = lrv_rd16le(buf + o); o += 2;
    memcpy(music, buf + o, 16); music[15] = 0; o += 16;
    started = buf[o++];
    memcpy(image, buf + o, 40); image[39] = 0; o += 40;

    /* a different script: load it now (op_load 088D:1441 step 5) */
    if (strcmp(script, vm->script) != 0) {
        if (!game_load_script(script))
            return 0;
    }
    vm->cur_page = page;
    if (image[0]) {
        gdt_load(image);
        gfx_show();
    }
    if (music[0]) {
        music_load(music);
        music_play();
    }
    (void)started;
    return 1;
}

uint8_t progress_read(void)
{
    uint8_t b = 0;
    size_t len = 0;
    if (xe.hal->storage_read("progress", &b, 1, &len) != LRV_OK || len != 1)
        return xe.progress;                         /* missing file: keep the previous value */
    return b;
}

void progress_set_bit(int n)
{
    uint8_t b = progress_read();
    if (n >= 1 && n <= 7)
        b |= (uint8_t)(1u << (n - 1));
    if (xe.hal->storage_write("progress", &b, 1) != 0)
        error_box(lang_str(LS_FILE_OPEN_ERROR));
    xe.progress = b;
}
