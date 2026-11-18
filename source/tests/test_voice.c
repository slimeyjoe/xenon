/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Conformance tool for the voice clip decoders (lrvne/vdec.h) on the game's clips, driven by
 * tests/check_voice.py.
 *
 *   test_voice VOICEPAK CLIP CHUNK   -> the decoded clip as raw s16le on stdout, decoded in
 *                                       reads of CHUNK samples (runs cross the read boundaries);
 *                                       the number of pack reads on stderr
 *
 * Uses a minimal stdio HAL with the voice pack as the main pack. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lrvne/vdec.h"
#include "test_hal.h"

static unsigned long g_reads, g_bytes;   /* pack reads, reported on stderr */

static size_t counted_read(void *h, uint32_t off, void *dst, size_t n)
{
    g_reads++;
    g_bytes += n;
    return th_pack_read(h, off, dst, n);
}

int main(int argc, char **argv)
{
    struct lrv_hal hal;
    struct lrv_pack pk;
    static struct lrv_vdec d;
    const struct lrv_pack_entry *e;
    int16_t buf[4096];
    int chunk, n, i;

    if (argc != 4)
        return 2;
    chunk = atoi(argv[3]);
    if (chunk < 1 || chunk > 4096 || th_hal(&hal, argv[1]) != 0)
        return 2;
    hal.pack_read = counted_read;
    if (lrv_pack_open(&pk, &hal, LRV_PACK_MAIN) != LRV_OK)
        return 1;
    lrv_vdec_init(&d);
    e = lrv_pack_find(&pk, argv[2], LRV_PT_VOICE);
    if (!e || lrv_vdec_open(&d, &pk, e) != LRV_OK) {
        fprintf(stderr, "%s: no such clip, or its format is not built in\n", argv[2]);
        return 1;
    }
    while ((n = lrv_vdec_read(&d, buf, chunk)) > 0)
        for (i = 0; i < n; i++) {
            putchar(buf[i] & 0xFF);
            putchar((buf[i] >> 8) & 0xFF);
        }
    fprintf(stderr, "%s: %lu pack reads, %lu bytes (entry %lu bytes)\n", argv[2], g_reads,
            g_bytes, (unsigned long)e->size);
    lrv_vdec_close(&d);
    lrv_pack_close(&pk);
    return 0;
}
