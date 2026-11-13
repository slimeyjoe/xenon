/* voicebench: CPU cost of the engine's voice decoders (engine/voice/vdec*.c) on a 68k.
 *
 *   voicebench PACK [SECONDS [DUMPFILE]]
 *
 * Loads PACK (a voice pack, or a small one made by mkbenchpak.py) into RAM, so disk I/O is
 * not measured, then decodes its clips in order, in reads of 512 samples as voice.c does,
 * until SECONDS of audio (default 60) have been decoded, and times that with the E clock.
 * A second pass times what the Paula voice output would add per sample: the engine's linear
 * resampler (voice.c next_out) from the clip rate to a Paula rate, and the 8-bit conversion.
 * Prints the share of the CPU each needs in real time and an FNV-1a hash of the decoded
 * samples (mkbenchpak.py prints the host's, so the 68k build is checked bit for bit).
 * DUMPFILE: afterwards, the first 5 clips decoded again as raw s16 big-endian (to compare
 * with the host when the hash differs: the 68k libopus multiplies differently). */
#include <exec/types.h>
#include <exec/memory.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pack/pack.h"
#include "voice/vdec.h"
#include "xe_hal.h"

struct Device *TimerBase;
static struct MsgPort *tport;
static struct timerequest *treq;
static ULONG eclock_freq;

static int timer_open(void)
{
    tport = CreateMsgPort();
    if (!tport)
        return 0;
    treq = (struct timerequest *)CreateIORequest(tport, sizeof(struct timerequest));
    if (!treq)
        return 0;
    if (OpenDevice("timer.device", UNIT_MICROHZ, (struct IORequest *)treq, 0) != 0)
        return 0;
    TimerBase = treq->tr_node.io_Device;
    return 1;
}

static void timer_close(void)
{
    if (TimerBase)
        CloseDevice((struct IORequest *)treq);
    if (treq)
        DeleteIORequest((struct IORequest *)treq);
    if (tport)
        DeleteMsgPort(tport);
}

static ULONG eclock_now(void)
{
    struct EClockVal ev;
    eclock_freq = ReadEClock(&ev);
    return ev.ev_lo;
}

/* ---- a minimal HAL: the pack in memory ---- */
static UBYTE *g_mem;
static ULONG g_size;

static void *h_pack_open(int which, uint32_t *size)
{
    (void)which;
    *size = g_size;
    return g_mem;
}

static size_t h_pack_read(void *h, uint32_t off, void *dst, size_t n)
{
    (void)h;
    if (off >= g_size)
        return 0;
    if (n > g_size - off)
        n = g_size - off;
    memcpy(dst, g_mem + off, n);
    return n;
}

static void h_pack_close(void *h) { (void)h; }
static void *h_alloc(size_t n, unsigned f) { (void)f; return AllocVec((ULONG)n, MEMF_ANY | MEMF_CLEAR); }
static void h_free(void *p, size_t n) { (void)n; FreeVec(p); }
static void h_log(int level, const char *msg) { printf("[%d] %s\n", level, msg); }
static void h_fatal(const char *msg) { printf("fatal: %s\n", msg); exit(20); }

#define CHUNK 512
#define PAULA_RATE 22030UL              /* PAL clock / period 161: the nearest to 22050 */

static int16_t buf[CHUNK];
static UBYTE out8[CHUNK];

int main(int argc, char **argv)
{
    static struct vdec d;
    struct xe_hal hal;
    struct xpak pk;
    BPTR fh;
    ULONG secs = 60, samples = 0, clips = 0, t0, t1, e_dec, e_out, i, rate = 0;
    unsigned long hash = 2166136261UL;
    int n, k;

    if (argc < 2) {
        printf("usage: voicebench PACK [SECONDS]\n");
        return 20;
    }
    if (argc > 2)
        secs = (ULONG)atol(argv[2]);
    if (!timer_open()) {
        printf("timer.device failed\n");
        return 20;
    }
    fh = Open((STRPTR)argv[1], MODE_OLDFILE);
    if (!fh) {
        printf("cannot open %s\n", argv[1]);
        return 20;
    }
    Seek(fh, 0, OFFSET_END);
    g_size = (ULONG)Seek(fh, 0, OFFSET_BEGINNING);
    g_mem = AllocVec(g_size, MEMF_ANY);
    if (!g_mem || Read(fh, g_mem, (LONG)g_size) != (LONG)g_size) {
        printf("cannot load %s (%lu bytes)\n", argv[1], (unsigned long)g_size);
        return 20;
    }
    Close(fh);

    memset(&hal, 0, sizeof hal);
    hal.pack_open = h_pack_open;
    hal.pack_read = h_pack_read;
    hal.pack_close = h_pack_close;
    hal.mem_alloc = h_alloc;
    hal.mem_free = h_free;
    hal.log = h_log;
    hal.fatal = h_fatal;
    if (xpak_open(&pk, &hal, XE_PACK_MAIN) != 0)
        return 20;
    printf("voicebench: %s loaded, %lu bytes, %u entries\n", argv[1], (unsigned long)g_size,
           (unsigned)pk.count);
    fflush(stdout);

    /* pass 1: decode */
    t0 = eclock_now();
    for (i = 0; i < pk.count && (rate == 0 || samples < secs * rate); i++) {
        const struct xpak_entry *e = &pk.entries[i];
        if (e->type != XPAK_T_VOICE)
            continue;
        if (vdec_open(&d, &pk, e) != 0) {
            printf("clip %s: cannot open\n", e->name);
            continue;
        }
        rate = d.rate;
        clips++;
        while ((n = vdec_read(&d, buf, CHUNK)) > 0) {
            for (k = 0; k < n; k++) {
                hash ^= (UWORD)buf[k];
                hash *= 16777619UL;
            }
            samples += (ULONG)n;
        }
    }
    t1 = eclock_now();
    e_dec = t1 - t0;
    /* the hash loop is not part of the decoder: time it alone and take it off */
    t0 = eclock_now();
    {
        unsigned long h2 = 0;
        ULONG s;
        for (s = 0; s < samples; s += CHUNK)
            for (k = 0; k < CHUNK; k++) {
                h2 ^= (UWORD)buf[k];
                h2 *= 16777619UL;
            }
        if (h2 == 1)
            printf(" ");
    }
    t1 = eclock_now();
    e_dec -= (t1 - t0) < e_dec ? (t1 - t0) : 0;

    /* pass 2: the Paula output step for the same number of samples, on the last chunk's
     * data: linear interpolation as voice.c next_out (clip rate -> PAULA_RATE) and >> 8 */
    t0 = eclock_now();
    {
        ULONG step = (ULONG)(((unsigned long long)rate << 16) / PAULA_RATE), frac = 0, s;
        int16_t s0 = buf[0], s1 = buf[1];
        int j = 2;
        for (s = 0; s < samples; s += CHUNK) {
            for (k = 0; k < CHUNK; k++) {
                int16_t v = (int16_t)(s0 + (int16_t)(((LONG)(s1 - s0) * (LONG)(frac >> 1)) >> 15));
                out8[k] = (UBYTE)(v >> 8);
                frac += step;
                while (frac >= 0x10000UL) {
                    frac -= 0x10000UL;
                    s0 = s1;
                    s1 = buf[j++ & (CHUNK - 1)];
                }
            }
        }
    }
    t1 = eclock_now();
    e_out = t1 - t0;

    {
        /* percent of real time, in tenths: elapsed / audio duration */
        unsigned long audio_ms = rate ? (unsigned long)((unsigned long long)samples * 1000 / rate) : 1;
        unsigned long dec_ms = (unsigned long)((unsigned long long)e_dec * 1000 / eclock_freq);
        unsigned long out_ms = (unsigned long)((unsigned long long)e_out * 1000 / eclock_freq);
        printf("voicebench: %s, %lu clips, %lu Hz, %lu samples = %lu.%03lu s of audio\n", argv[1],
               (unsigned long)clips, (unsigned long)rate, (unsigned long)samples,
               audio_ms / 1000, audio_ms % 1000);
        printf("  decode: %lu ms -> %lu.%lu %% of the CPU in real time\n", dec_ms,
               dec_ms * 100 / audio_ms, (dec_ms * 1000 / audio_ms) % 10);
        printf("  paula step (resample to %lu Hz + 8 bit): %lu ms -> %lu.%lu %%\n",
               PAULA_RATE, out_ms, out_ms * 100 / audio_ms, (out_ms * 1000 / audio_ms) % 10);
        printf("  hash %08lx\n", hash);
    }
    if (argc > 3) {
        BPTR o = Open((STRPTR)argv[3], MODE_NEWFILE);
        int c = 0;
        for (i = 0; o && i < pk.count && c < 5; i++) {
            if (pk.entries[i].type != XPAK_T_VOICE || vdec_open(&d, &pk, &pk.entries[i]) != 0)
                continue;
            c++;
            while ((n = vdec_read(&d, buf, CHUNK)) > 0)
                Write(o, buf, (LONG)n * 2);
        }
        if (o)
            Close(o);
    }
    vdec_close(&d);
    xpak_close(&pk);
    FreeVec(g_mem);
    timer_close();
    return 0;
}
