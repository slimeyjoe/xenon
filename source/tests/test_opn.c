/* OPN core driver for tests/check_opn.py.
 *
 *   test_opn lite|lite2|ymfm RATE FILE.vgm OUT.raw [MAX_SECONDS [all|fm|ssg]]
 *
 * Replays the YM2203 register stream of a VGM (44 100 Hz event timing) into the chosen core
 * created for RATE and writes the rendered mono 16-bit PCM (native byte order) to OUT.raw.
 * The rate the core really produced is printed on stdout. The optional mask drops the SSG
 * (fm) or the FM (ssg) register writes so the two parts can be compared separately.
 * Writes that the VGM puts at one instant are spaced one chip sample (1/55 467 s) apart, as
 * PMD's BUSY polling spaces them on hardware and as the engine's write queue does. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "snd/opn_core.h"

#define CLOCK 3993600u
#define BUF 4096

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
    buf = malloc((size_t)n);
    if (fread(buf, 1, (size_t)n, f) != (size_t)n)
        exit(2);
    fclose(f);
    *len = (size_t)n;
    return buf;
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int main(int argc, char **argv)
{
    const struct opn_core_ops *ops;
    struct opn_core *core;
    uint32_t rate = 0;
    uint8_t *vgm;
    size_t len, pos;
    uint64_t t44 = 0, done = 0, limit;
    uint64_t bus = 0;                /* core sample position (16.16) at which the bus is free */
    uint64_t cost;                   /* 16.16 core samples per write */
    double max_seconds = 1e9;
    int mask = 0;                    /* 0 all, 1 fm only, 2 ssg only */
    int16_t buf[BUF];
    FILE *out;

    if (argc < 5)
        return 2;
    if (!strcmp(argv[1], "ymfm")) {
#ifdef XE_HAVE_YMFM
        ops = &opn_ymfm_ops;
#else
        fprintf(stderr, "ymfm not built\n");
        return 2;
#endif
    } else {
        ops = &opn_lite_ops;
        if (!strcmp(argv[1], "lite2"))
            opn_lite_fm_oversample = 2;
    }
    core = ops->create(CLOCK, (uint32_t)atoi(argv[2]), &rate);
    if (!core)
        return 2;
    vgm = read_file(argv[3], &len);
    out = fopen(argv[4], "wb");
    if (!out) {
        perror(argv[4]);
        return 2;
    }
    if (argc > 5)
        max_seconds = atof(argv[5]);
    if (argc > 6)
        mask = !strcmp(argv[6], "fm") ? 1 : !strcmp(argv[6], "ssg") ? 2 : 0;
    limit = (uint64_t)(max_seconds * rate);
    cost = ((uint64_t)rate << 16) / (CLOCK / 72);

    pos = 0x34 + rd32(vgm + 0x34);
    while (pos < len && done < limit) {
        uint8_t cmd = vgm[pos];
        uint32_t wait = 0;
        if (cmd == 0x55 || (cmd >= 0x51 && cmd <= 0x5f)) {
            if (cmd == 0x55) {
                uint8_t reg = vgm[pos + 1];
                int ssg = reg < 0x10;
                if (!(mask == 1 && ssg) && !(mask == 2 && !ssg && reg != 0x27)) {
                    uint64_t now = done << 16;
                    if (bus > now) {
                        /* bus busy: render up to the sample in which it frees */
                        uint64_t target = bus >> 16;
                        if (target > limit)
                            target = limit;
                        while (done < target) {
                            int n = (int)((target - done) > BUF ? BUF : (target - done));
                            ops->render(core, buf, n);
                            fwrite(buf, sizeof(int16_t), (size_t)n, out);
                            done += (uint64_t)n;
                        }
                        now = done << 16;
                        if (bus < now)
                            bus = now;
                    } else {
                        bus = now;
                    }
                    ops->write(core, reg, vgm[pos + 2]);
                    bus += cost;
                }
            }
            pos += 3;
        } else if (cmd == 0x61) {
            wait = (uint32_t)vgm[pos + 1] | ((uint32_t)vgm[pos + 2] << 8);
            pos += 3;
        } else if (cmd == 0x62) {
            wait = 735; pos++;
        } else if (cmd == 0x63) {
            wait = 882; pos++;
        } else if ((cmd & 0xf0) == 0x70) {
            wait = (cmd & 15) + 1u; pos++;
        } else if (cmd == 0x66) {
            break;
        } else if (cmd == 0x4f || cmd == 0x50) {
            pos += 2;
        } else if (cmd == 0x67) {
            pos += 7 + rd32(vgm + pos + 3);
        } else {
            fprintf(stderr, "unknown VGM command %02X at %lu\n", cmd, (unsigned long)pos);
            return 2;
        }
        if (wait) {
            uint64_t target;
            t44 += wait;
            target = (t44 * rate + 22050) / 44100;
            if (target > limit)
                target = limit;
            while (done < target) {
                int n = (int)((target - done) > BUF ? BUF : (target - done));
                ops->render(core, buf, n);
                fwrite(buf, sizeof(int16_t), (size_t)n, out);
                done += (uint64_t)n;
            }
        }
    }
    fclose(out);
    ops->destroy(core);
    printf("%lu %lu\n", (unsigned long)rate, (unsigned long)done);
    return 0;
}
