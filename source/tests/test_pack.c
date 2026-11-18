/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Conformance tool for the pack as the engine reads it (lrvne/pack.h), driven by
 * tests/check_pack.py.
 *
 *   test_pack FILE list                 -> one line per entry: name type lang offset size
 *   test_pack FILE get LANG TYPE NAME   -> entry contents on stdout (LANG = a tag, "-" for
 *                                          neutral; TYPE = int)
 *
 * FILE may be a bare pack or an executable with a trailer. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lrvne/pack.h"
#include "test_hal.h"

int main(int argc, char **argv)
{
    struct lrv_hal hal;
    struct lrv_pack pk;

    if (argc < 3 || th_hal(&hal, argv[1]) != 0)
        return 2;
    if (lrv_pack_open(&pk, &hal, LRV_PACK_MAIN) != LRV_OK)
        return 1;
    if (strcmp(argv[2], "list") == 0) {
        uint16_t i;
        for (i = 0; i < pk.count; i++) {
            const struct lrv_pack_entry *e = &pk.entries[i];
            printf("%s %d %d %lu %lu\n", e->name, e->type, e->lang, (unsigned long)e->offset,
                   (unsigned long)e->size);
        }
    } else if (strcmp(argv[2], "get") == 0 && argc == 6) {
        const struct lrv_pack_entry *e;
        uint8_t *data;
        uint32_t size;
        lrv_pack_set_lang(&pk, strcmp(argv[3], "-") ? lrv_pack_lang_id(&pk, argv[3]) : 0);
        e = lrv_pack_find(&pk, argv[5], atoi(argv[4]));
        if (!e) {
            fprintf(stderr, "not found\n");
            return 1;
        }
        data = lrv_pack_load(&pk, e, &size);
        if (!data)
            return 1;
        fwrite(data, 1, size, stdout);
        lrv_pack_free(&pk, data, size);
    } else {
        return 2;
    }
    lrv_pack_close(&pk);
    return 0;
}
