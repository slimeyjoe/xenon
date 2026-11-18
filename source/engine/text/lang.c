/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Language string table: the engine's own messages (system menu, error boxes, omake notice).
 * The pack entry "strings" of the current language holds `key\0value\0` pairs in the
 * language's encoding (enc.c), built by mkpack.py from lang/<lang>/strings.txt. */
#include <string.h>
#include "xe.h"
#include "text/text.h"

static const char *const keys[LS_COUNT] = {
    "sysmenu_cancel", "sysmenu_restart", "sysmenu_quit",
    "quit_continue", "quit_really",
    "omake_locked", "no_save_data", "file_open_error", "disk_insert", "disk_wrong",
    "disk_not_game", "disk_write_protect", "disk_prompt", "menu_empty", "script_error"
};

static const uint8_t *values[LS_COUNT];

static void lang_init(void)
{
    const struct lrv_pack_entry *e = lrv_pack_find(&xe.pack, "strings", LRV_PT_STRINGS);
    uint32_t i = 0;
    int k;
    for (k = 0; k < LS_COUNT; k++)
        values[k] = (const uint8_t *)"?";
    if (!e) {
        lrv_log(xe.hal, LRV_LOG_WARN, "lang: no strings table for the language");
        return;
    }
    xe.strings_blob = lrv_pack_load(&xe.pack, e, &xe.strings_size);
    if (!xe.strings_blob)
        return;
    while (i < xe.strings_size) {
        const char *key = (const char *)xe.strings_blob + i;
        size_t kl = strlen(key);
        const uint8_t *val;
        if (i + kl + 1 >= xe.strings_size)
            break;
        val = xe.strings_blob + i + kl + 1;
        for (k = 0; k < LS_COUNT; k++)
            if (strcmp(key, keys[k]) == 0)
                values[k] = val;
        i += (uint32_t)(kl + 1 + strlen((const char *)val) + 1);
    }
}

static void lang_shutdown(void)
{
    if (xe.strings_blob)
        lrv_pack_free(&xe.pack, xe.strings_blob, xe.strings_size);
    xe.strings_blob = 0;
}

const uint8_t *lang_str(int id)
{
    return (id >= 0 && id < LS_COUNT) ? values[id] : (const uint8_t *)"?";
}

void text_init(void)
{
    if (lrv_font_load(&xe.font16, &xe.pack, "font16", 16, LRV_GLYPH_ROWS) != LRV_OK)
        lrv_fatal(xe.hal, "font16 missing from the pack");
    if (lrv_font_load(&xe.font8, &xe.pack, "font8", 8, LRV_GLYPH_ROWS) != LRV_OK)
        lrv_fatal(xe.hal, "font8 missing from the pack");
    text_enc_init();
    lang_init();
    xe.text_x = 10;
    xe.text_y = 313;
    xe.text_fg = 15;
    xe.text_bg = 0;
    xe.menu_x = 10;
    xe.menu_y = 313;
    xe.overlay_count = 0;
}

void text_shutdown(void)
{
    lang_shutdown();
    lrv_font_free(&xe.font16);
    lrv_font_free(&xe.font8);
}
