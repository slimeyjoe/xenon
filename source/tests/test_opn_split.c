/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The light OPN core's render_split check (lrvne/opn.h): the sum of the split streams must equal the mono
 * render up to the rounding of the per-stream filters.
 *
 *   test_opn_split FILE.vgm RATE
 *
 * Replays the YM2203 register stream of the VGM (writes applied at their VGM instants, the
 * same way into both cores) through two opn_lite instances, one rendered mono and one split,
 * for os = 1 and os = 2, and reports the peak and RMS difference. Exit status 1 when the
 * peak difference exceeds the tolerance. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lrvne/opn.h"
#include "test_hal.h"

#define CLOCK 3993600u
#define BUF 1024
#define MAX_SECONDS 20.0
#define TOLERANCE 16                 /* LSB: 4 streams x (filter + output truncation) */

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

static int16_t mono[BUF], sp[LRV_OPN_SPLIT_STREAMS][BUF];

static int run(const uint8_t *vgm, size_t len, uint32_t want_rate, int os, int *peak_out, double *rms_out,
               double *level_out)
{
    const struct lrv_opn_ops *ops = &lrv_opn_lite_ops;
    struct lrv_opn *a, *b;
    uint32_t rate, rate2;
    uint64_t t44 = 0, done = 0, limit;
    size_t pos;
    int peak = 0;
    double sq = 0, lv = 0;
    uint64_t count = 0;
    int16_t *streams[LRV_OPN_SPLIT_STREAMS] = { sp[0], sp[1], sp[2], sp[3] };

    unsigned flags = os == 2 ? LRV_OPN_OVERSAMPLE : 0u;
    struct lrv_hal hal;
    th_hal(&hal, 0);
    a = ops->create(&hal, CLOCK, want_rate, flags, &rate);
    b = ops->create(&hal, CLOCK, want_rate, flags, &rate2);
    if (!a || !b || rate != rate2 || !ops->render_split)
        return -1;
    limit = (uint64_t)(MAX_SECONDS * rate);

    pos = 0x34 + rd32(vgm + 0x34);
    while (pos < len && done < limit) {
        uint8_t cmd = vgm[pos];
        uint32_t wait = 0;
        if (cmd == 0x55 || (cmd >= 0x51 && cmd <= 0x5f)) {
            if (cmd == 0x55) {
                ops->write(a, vgm[pos + 1], vgm[pos + 2]);
                ops->write(b, vgm[pos + 1], vgm[pos + 2]);
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
            fprintf(stderr, "unknown VGM command %02X\n", cmd);
            return -1;
        }
        if (wait) {
            uint64_t target;
            t44 += wait;
            target = t44 * rate / 44100;
            if (target > limit)
                target = limit;
            while (done < target) {
                int n = (int)((target - done) > BUF ? BUF : (target - done)), i;
                ops->render(a, mono, n);
                ops->render_split(b, streams, n);
                for (i = 0; i < n; i++) {
                    int s = sp[0][i] + sp[1][i] + sp[2][i] + sp[3][i];
                    int d;
                    if (s > 32767) s = 32767;
                    if (s < -32768) s = -32768;
                    d = s - mono[i];
                    if (d < 0) d = -d;
                    if (d > peak) peak = d;
                    sq += (double)d * d;
                    lv += (double)mono[i] * mono[i];
                    count++;
                }
                done += (uint64_t)n;
            }
        }
    }
    ops->destroy(a);
    ops->destroy(b);
    *peak_out = peak;
    *rms_out = count ? sqrt(sq / (double)count) : 0;
    *level_out = count ? sqrt(lv / (double)count) : 0;
    return 0;
}

int main(int argc, char **argv)
{
    uint8_t *vgm;
    size_t len;
    int os, rc = 0;
    if (argc < 3) {
        fprintf(stderr, "usage: test_opn_split FILE.vgm RATE\n");
        return 2;
    }
    vgm = read_file(argv[1], &len);
    for (os = 1; os <= 2; os++) {
        int peak;
        double rms, level;
        if (run(vgm, len, (uint32_t)atoi(argv[2]), os, &peak, &rms, &level) != 0) {
            fprintf(stderr, "render_split unavailable\n");
            return 1;
        }
        printf("opn_lite split vs mono, rate %s, os %d: peak diff %d LSB, rms diff %.2f LSB, "
               "signal rms %.0f -> %s\n", argv[2], os, peak, rms, level,
               peak <= TOLERANCE ? "OK" : "FAIL");
        if (peak > TOLERANCE)
            rc = 1;
    }
    free(vgm);
    return rc;
}
