/* opnsum: the host side of "opnbench sum" - the same replay of a song log through the C
 * opn_lite at the same rates, printing the same hash of every rendered sample, so the
 * Amiga build (vbcc, and the 68k assembly render loop) is checked bit for bit:
 *   make opnsum-XENON_24 && ./opnsum-XENON_24        vs. opnbench-XENON_24 sum on the Amiga */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "snd/opn_core.h"
#include <opnbench_log.h>

#define CLOCK 3993600UL
#define BUF 1024

static unsigned long sum;

static void render(struct opn_core *c, short *buf, int n)
{
    int k;
    opn_lite_ops.render(c, buf, n);
    for (k = 0; k < n; k++)
        sum = (sum * 31 + (unsigned short)buf[k]) & 0xffffffffUL;
}

static unsigned long run_log(struct opn_core *c, unsigned long rate, short *buf)
{
    unsigned long i, done = 0, target;
    for (i = 0; i < OPNLOG_COUNT; i++) {
        target = (unsigned long)(((unsigned long long)opnlog[i].t * rate + 22050) / 44100);
        while (done < target) {
            int n = (int)((target - done) > BUF ? BUF : (target - done));
            render(c, buf, n);
            done += n;
        }
        opn_lite_ops.write(c, opnlog[i].reg, opnlog[i].val);
    }
    target = (unsigned long)(((unsigned long long)OPNLOG_SAMPLES * rate + 22050) / 44100);
    while (done < target) {
        int n = (int)((target - done) > BUF ? BUF : (target - done));
        render(c, buf, n);
        done += n;
    }
    return done;
}

int main(void)
{
    static const unsigned long rates[] = { 22050, 11025, 27500, 44100 };
    short buf[BUF];
    int i, os;
    for (i = 0; i < 4; i++) {
        for (os = 1; os <= (rates[i] <= 22050 ? 2 : 1); os++) {
            struct opn_core *c;
            uint32_t rate = 0;
            unsigned long samples;
            opn_lite_fm_oversample = os;
            c = opn_lite_ops.create(CLOCK, rates[i], &rate);
            sum = 0;
            samples = run_log(c, rate, buf);
            printf("rate %5lu os %d: %7lu samples, sum %08lx\n", (unsigned long)rate, os, samples, sum);
            opn_lite_ops.destroy(c);
        }
    }
    return 0;
}
