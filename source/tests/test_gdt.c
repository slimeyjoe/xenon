/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Conformance tool for the GDT/ADT decoder, driven by tests/check_gdt.py.
 *
 *   test_gdt FILL SPEC...
 *
 * FILL is 00 or ff (initial plane contents). Each SPEC is a GDT file, or FILE.ADT@N for frame N
 * of an ADT file; they are decoded in order into the same four planes (as the engine chains
 * diff images). The 128 000 bytes of the planes (B, R, G, I) are written to stdout; the
 * bounding box written by the last blob goes to stderr as "rect x0 y0 x1 y1". */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gfx/gdt.h"

#define PLANE_BYTES (GDT_STRIDE * 400)
#include "lrvne/endian.h"

static uint8_t *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *buf;
    long n;
    if (!f) {
        perror(path);
        exit(2);
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc((size_t)n + 1);
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        perror(path);
        exit(2);
    }
    fclose(f);
    *len = (size_t)n;
    return buf;
}

int main(int argc, char **argv)
{
    static uint8_t planes[4][PLANE_BYTES];
    struct lrv_surface pl;
    struct lrv_rect rect;
    int i, fill;

    if (argc < 3) {
        fputs("usage: test_gdt 00|ff SPEC...\n", stderr);
        return 2;
    }
    fill = strcmp(argv[1], "ff") == 0 ? 0xFF : 0x00;
    for (i = 0; i < 4; i++) {
        memset(planes[i], fill, PLANE_BYTES);
        pl.plane[i] = planes[i];
    }
    lrv_rect_clear(&rect);
    pl.format = LRV_FMT_PLANAR4;
    pl.w = 640;
    pl.h = 400;
    pl.stride = GDT_STRIDE;

    for (i = 2; i < argc; i++) {
        char path[1024];
        char *at;
        size_t len;
        uint8_t *data, *blob;
        size_t bloblen;
        struct gdt_header h;

        strncpy(path, argv[i], sizeof path - 1);
        path[sizeof path - 1] = 0;
        at = strrchr(path, '@');
        if (at) {
            int frame = atoi(at + 1), k;
            size_t off = 0x1E9;
            *at = 0;
            data = read_file(path, &len);
            for (k = 0; k < frame; k++)
                off += lrv_rd16le(data + 5 + 2 * k);
            blob = data + off;
            bloblen = lrv_rd16le(data + 5 + 2 * frame);
        } else {
            data = read_file(path, &len);
            blob = data;
            bloblen = len;
        }
        if (gdt_parse(blob, bloblen, &h) != 0) {
            fprintf(stderr, "%s: not a DA1 image\n", argv[i]);
            return 1;
        }
        if (gdt_decode(blob, bloblen, &h, &pl, &rect) != 0) {
            fprintf(stderr, "%s: stream overrun\n", argv[i]);
            return 1;
        }
        free(data);
    }
    for (i = 0; i < 4; i++)
        fwrite(planes[i], 1, PLANE_BYTES, stdout);
    fprintf(stderr, "rect %d %d %d %d\n", rect.x0, rect.y0, rect.x1, rect.y1);
    return 0;
}
