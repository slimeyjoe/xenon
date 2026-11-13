/* Sequencer trace tool, driven by tests/check_pmd.py.
 *
 *   pmd_trace song FILE.M SAMPLES [EFC PMDCOM] [fade:AT:SPEED] [vol:AT:V] [se:AT:N]
 *   pmd_trace se   XEN_SE.EFC PMD.COM N SAMPLES
 *
 * Runs the PMD sequencer at 44 100 Hz for SAMPLES samples (events at AT samples) and prints
 * one line per register write: "sample reg val" (hex reg/val). Timer registers are printed
 * too; the checker ignores them like vgmverify does. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "snd/pmd.h"

#define SR 44100u

static uint64_t g_pos;      /* samples rendered so far */

static uint8_t *read_file(const char *path, uint32_t *len)
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
    buf = malloc((size_t)n + 64);
    if (fread(buf, 1, (size_t)n, f) != (size_t)n)
        exit(2);
    fclose(f);
    memset(buf + n, 0x80, 64);
    *len = (uint32_t)n;
    return buf;
}

static void log_write(void *ctx, uint8_t reg, uint8_t val)
{
    (void)ctx;
    printf("%lu %02X %02X\n", (unsigned long)g_pos, reg, val);
}

static void render(void *ctx, int n)
{
    (void)ctx;
    g_pos += (uint64_t)n;
}

struct event { uint64_t at; char kind; int arg; };

int main(int argc, char **argv)
{
    struct pmd p;
    uint32_t mlen, elen = 0, clen = 0;
    uint8_t *m, *efc = 0, *com = 0;
    uint64_t samples;
    struct event ev[16];
    int nev = 0, i;

    if (argc < 4)
        return 2;
    pmd_init(&p, log_write, 0, SR);
    if (strcmp(argv[1], "song") == 0) {
        m = read_file(argv[2], &mlen);
        samples = strtoull(argv[3], 0, 10);
        for (i = 4; i < argc; i++) {
            if (strncmp(argv[i], "fade:", 5) == 0 || strncmp(argv[i], "vol:", 4) == 0 ||
                strncmp(argv[i], "se:", 3) == 0) {
                const char *s = strchr(argv[i], ':') + 1;
                ev[nev].kind = argv[i][0];
                ev[nev].at = strtoull(s, 0, 10);
                ev[nev].arg = atoi(strchr(s, ':') + 1);
                nev++;
            } else if (!efc) {
                efc = read_file(argv[i], &elen);
            } else {
                com = read_file(argv[i], &clen);
            }
        }
        if (efc)
            pmd_set_efc(&p, efc, elen);
        if (com && clen > 0x3096 - 0x100)
            pmd_set_ssgtab(&p, com + 0x2CE9 - 0x100, 0x3096 - 0x2CE9);
        pmd_load_music(&p, m + 1, mlen - 1);
        pmd_mstart(&p);
    } else if (strcmp(argv[1], "se") == 0 && argc >= 6) {
        efc = read_file(argv[2], &elen);
        com = read_file(argv[3], &clen);
        pmd_set_efc(&p, efc, elen);
        if (clen > 0x3096 - 0x100)
            pmd_set_ssgtab(&p, com + 0x2CE9 - 0x100, 0x3096 - 0x2CE9);
        samples = strtoull(argv[5], 0, 10);
        pmd_fm_effect_on(&p, atoi(argv[4]));
    } else {
        return 2;
    }

    while (g_pos < samples) {
        uint64_t next = samples;
        int n;
        for (i = 0; i < nev; i++)
            if (ev[i].at > g_pos && ev[i].at < next)
                next = ev[i].at;
        for (i = 0; i < nev; i++) {
            if (ev[i].at == g_pos) {
                if (ev[i].kind == 'f') pmd_fade(&p, (uint8_t)ev[i].arg);
                else if (ev[i].kind == 'v') pmd_set_fadevol(&p, (uint8_t)ev[i].arg);
                else if (ev[i].kind == 's') pmd_fm_effect_on(&p, ev[i].arg);
                ev[i].at = (uint64_t)-1;
            }
        }
        n = (int)(next - g_pos);
        if (n > 4096)
            n = 4096;
        pmd_advance(&p, n, render, 0);
    }
    return 0;
}
