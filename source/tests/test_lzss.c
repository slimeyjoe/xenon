/* Conformance tool for the LZSS decoder: test_lzss FILE.CC -> decompressed bytes on stdout. */
#include <stdio.h>
#include <stdlib.h>
#include "pack/lzss.h"

int main(int argc, char **argv)
{
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
    if (fread(raw, 1, (size_t)n, f) != (size_t)n)
        return 2;
    fclose(f);
    size = lcz_size(raw, (size_t)n) & 0xFFFF;   /* the engine uses the low 16 bits */
    out = malloc(size ? size : 1);
    got = lzss_decode(raw + LCZ_HEADER_SIZE, (size_t)n - LCZ_HEADER_SIZE, out, size);
    if (got != size) {
        fprintf(stderr, "%s: decoded %lu bytes, header says %lu\n", argv[1],
                (unsigned long)got, (unsigned long)size);
        return 1;
    }
    fwrite(out, 1, got, stdout);
    return 0;
}
