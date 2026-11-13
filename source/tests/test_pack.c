/* Conformance tool for the pack reader, driven by tests/check_pack.py.
 *
 *   test_pack FILE list                 -> one line per entry: name type lang offset size
 *   test_pack FILE get LANG TYPE NAME   -> entry contents on stdout (LANG = 0/1/2, TYPE = int)
 *
 * Uses a minimal stdio HAL; FILE may be a bare pack or an executable with a trailer. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pack/pack.h"
#include "xe_hal.h"

static FILE *g_file;
static uint32_t g_size;

static void *h_pack_open(int which, uint32_t *size)
{
    if (which != XE_PACK_MAIN)
        return 0;
    *size = g_size;
    return g_file;
}

static size_t h_pack_read(void *h, uint32_t off, void *dst, size_t n)
{
    if (fseek((FILE *)h, (long)off, SEEK_SET) != 0)
        return 0;
    return fread(dst, 1, n, (FILE *)h);
}

static void h_pack_close(void *h) { (void)h; }
static void *h_alloc(size_t n, unsigned f) { (void)f; return malloc(n); }
static void h_free(void *p, size_t n) { (void)n; free(p); }
static void h_log(int level, const char *msg) { fprintf(stderr, "[%d] %s\n", level, msg); }
static void h_fatal(const char *msg) { fprintf(stderr, "fatal: %s\n", msg); exit(1); }

int main(int argc, char **argv)
{
    struct xe_hal hal;
    struct xpak pk;
    long n;

    if (argc < 3)
        return 2;
    g_file = fopen(argv[1], "rb");
    if (!g_file) {
        perror(argv[1]);
        return 2;
    }
    fseek(g_file, 0, SEEK_END);
    n = ftell(g_file);
    g_size = (uint32_t)n;

    memset(&hal, 0, sizeof hal);
    hal.pack_open = h_pack_open;
    hal.pack_read = h_pack_read;
    hal.pack_close = h_pack_close;
    hal.mem_alloc = h_alloc;
    hal.mem_free = h_free;
    hal.log = h_log;
    hal.fatal = h_fatal;

    if (xpak_open(&pk, &hal, XE_PACK_MAIN) != 0)
        return 1;
    if (strcmp(argv[2], "list") == 0) {
        uint16_t i;
        for (i = 0; i < pk.count; i++) {
            const struct xpak_entry *e = &pk.entries[i];
            printf("%.20s %d %d %lu %lu\n", e->name, e->type, e->lang,
                   (unsigned long)e->offset, (unsigned long)e->size);
        }
    } else if (strcmp(argv[2], "get") == 0 && argc == 6) {
        const struct xpak_entry *e;
        uint8_t *data;
        uint32_t size;
        pk.lang = (uint8_t)atoi(argv[3]);
        e = xpak_find(&pk, argv[5], atoi(argv[4]));
        if (!e) {
            fprintf(stderr, "not found\n");
            return 1;
        }
        data = xpak_load(&pk, e, &size);
        if (!data)
            return 1;
        fwrite(data, 1, size, stdout);
        xpak_free(&pk, data, size);
    } else {
        return 2;
    }
    xpak_close(&pk);
    return 0;
}
