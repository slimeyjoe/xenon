/* Conformance tool for the voice clip decoders (engine/voice/vdec*.c), driven by
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
#include "pack/pack.h"
#include "voice/vdec.h"
#include "xe_hal.h"

static FILE *g_file;
static uint32_t g_size;
static unsigned long g_reads, g_bytes;   /* pack reads, reported on stderr */

static void *h_pack_open(int which, uint32_t *size)
{
    (void)which;
    *size = g_size;
    return g_file;
}

static size_t h_pack_read(void *h, uint32_t off, void *dst, size_t n)
{
    g_reads++;
    g_bytes += n;
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
    static struct vdec d;
    const struct xpak_entry *e;
    int16_t buf[4096];
    int chunk, n, i;

    if (argc != 4)
        return 2;
    chunk = atoi(argv[3]);
    if (chunk < 1 || chunk > 4096)
        return 2;
    g_file = fopen(argv[1], "rb");
    if (!g_file) {
        perror(argv[1]);
        return 2;
    }
    fseek(g_file, 0, SEEK_END);
    g_size = (uint32_t)ftell(g_file);

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
    e = xpak_find(&pk, argv[2], XPAK_T_VOICE);
    if (!e || vdec_open(&d, &pk, e) != 0) {
        fprintf(stderr, "%s: no such clip, or its format is not built in\n", argv[2]);
        return 1;
    }
    while ((n = vdec_read(&d, buf, chunk)) > 0)
        for (i = 0; i < n; i++) {
            putchar(buf[i] & 0xFF);
            putchar((buf[i] >> 8) & 0xFF);
        }
    fprintf(stderr, "%s: %lu pack reads, %lu bytes (entry %lu bytes)\n", argv[2], g_reads, g_bytes,
            (unsigned long)e->size);
    vdec_close(&d);
    xpak_close(&pk);
    return 0;
}
