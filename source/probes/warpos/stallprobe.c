/*
 * stallprobe: repro for the 259 s emulator stalls (README "The stalls").
 *
 *   stallprobe [SECONDS] [WAITUS]
 *
 * The main PPC task loops on WaitTime(CTRL-C, WAITUS) for SECONDS of time base time
 * (default 600 s, 2000 us) and prints every wait that took longer than 1 s, measured on
 * the 64-bit time base (a 32-bit difference would hide a full 2^32 wrap).  No 68k calls
 * in the loop except the progress line every 30 s.
 */
#include "os_begin.h"
#include <exec/types.h>
#include <dos/dos.h>
#include <powerpc/powerpc.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/powerpc.h>
#include "os_end.h"
#include <stdio.h>
#include <stdlib.h>

#define TBHZ 16600000ULL        /* qemu-uae TBFREQ; the probe measured 15.96 MHz */

ULONG sp_mftbl(void) = "\tmftb\t3";
ULONG sp_mftbu(void) = "\tmftbu\t3";

static unsigned long long tb64(void)
{
    ULONG hi, lo, hi2;
    do {
        hi = sp_mftbu();
        lo = sp_mftbl();
        hi2 = sp_mftbu();
    } while (hi != hi2);
    return (unsigned long long)hi << 32 | lo;
}

int main(int argc, char **argv)
{
    unsigned long secs = argc > 1 ? strtoul(argv[1], 0, 10) : 600;
    unsigned long wait = argc > 2 ? strtoul(argv[2], 0, 10) : 2000;
    unsigned long long start = tb64(), end = start + secs * TBHZ, last = start, next_note;
    unsigned long n = 0, stalls = 0, longest = 0;

    setvbuf(stdout, NULL, _IOLBF, 512);     /* a hang still leaves the lines before it */
    printf("stallprobe: %lu s of WaitTime(%lu us), time base %08lx%08lx\n", secs, wait,
           (unsigned long)(start >> 32), (unsigned long)start);
    next_note = start + 30 * TBHZ;
    while (last < end) {
        unsigned long long t;
        unsigned long ms;
        if (WaitTime(SIGBREAKF_CTRL_C, wait) & SIGBREAKF_CTRL_C)
            break;
        t = tb64();
        n++;
        ms = (unsigned long)((t - last) * 1000ULL / TBHZ);
        if (ms > longest)
            longest = ms;
        if (ms > 1000) {
            stalls++;
            printf("  STALL wait %lu took %lu ms at %lu s (time base %08lx%08lx)\n", n, ms,
                   (unsigned long)((t - start) / TBHZ), (unsigned long)(t >> 32),
                   (unsigned long)t);
        }
        last = t;
        if (t >= next_note) {
            printf("  %lu s: %lu waits, longest %lu ms\n",
                   (unsigned long)((t - start) / TBHZ), n, longest);
            next_note += 30 * TBHZ;
        }
    }
    printf("stallprobe: %lu waits, %lu stalls, longest %lu ms\n", n, stalls, longest);
    return 0;
}
