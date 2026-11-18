/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Conformance tool for the script container (docs/re/scripts.md §1): an LCZ header of 18h bytes
 * whose u32 at 14h is the unpacked size (the engine uses its low 16 bits), then an LZSS stream
 * (lrvne/lzss.h). test_lzss FILE.CC -> decompressed bytes on stdout, compared by
 * check_lzss.py with tools/lzss.py. */
#include <stdio.h>
#include <stdlib.h>
#include "lrvne/endian.h"
#include "lrvne/lzss.h"

#define LCZ_HEADER_SIZE 0x18

int main(int argc, char **argv)
{
    static uint8_t ring[LRV_LZSS_RING];
    FILE *f;
    long n;
    uint8_t *raw, *out;
    uint32_t size;
    size_t got;

    if (argc != 2) {
        fputs("usage: test_lzss FILE.CC\n", stderr);
        return 2;
    }
    f = fopen(argv[1], "rb");
    if (!f) {
        perror(argv[1]);
        return 2;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    raw = malloc((size_t)n);
    if (n < LCZ_HEADER_SIZE || fread(raw, 1, (size_t)n, f) != (size_t)n)
        return 2;
    fclose(f);
    size = lrv_rd32le(raw + 0x14) & 0xFFFF;
    out = malloc(size ? size : 1);
    got = lrv_lzss_decode(raw + LCZ_HEADER_SIZE, (size_t)n - LCZ_HEADER_SIZE, out, size, ring);
    if (got != size) {
        fprintf(stderr, "%s: decoded %lu bytes, header says %lu\n", argv[1],
                (unsigned long)got, (unsigned long)size);
        free(raw);
        free(out);
        return 1;
    }
    fwrite(out, 1, got, stdout);
    free(raw);
    free(out);
    return 0;
}
