/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * seekprobe FILE: what a dos.library Seek costs on the file's filesystem, as a function of
 * the target offset and of where the file position was (the game reads its pack with
 * Seek+Read per asset; on the real A1200 the Seeks, not the transfers, made the pauses).
 *
 *   A  from position 0 to offset X (absolute), cold then again
 *   B  forward chain: absolute seeks to increasing offsets, each from the previous one
 *   C  relative +64 KB steps far into the file (OFFSET_CURRENT)
 *   D  backward: from the end region to a slightly lower offset
 *   R  a 512-byte Read after the seek (whether the cost moved from Seek to Read)
 *
 * Times in microseconds from the E-clock. Prints the filesystem's DosType too.
 */
#include <exec/types.h>
#include <dos/dos.h>
#include <exec/memory.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>
#include <stdio.h>

struct Device *TimerBase;
static ULONG efreq;
static char buf[512];

static ULONG now(void)
{
    struct EClockVal ev;
    ReadEClock(&ev);
    return ev.ev_lo;
}

static ULONG us(ULONG e)
{
    return (ULONG)((unsigned long long)e * 1000000ULL / efreq);
}

static ULONG seek_us(BPTR fh, LONG off, LONG mode)
{
    ULONG t0 = now();
    if (Seek(fh, off, mode) < 0)
        printf("  seek %ld mode %ld failed\n", (long)off, (long)mode);
    return us(now() - t0);
}

static ULONG read_us(BPTR fh)
{
    ULONG t0 = now();
    Read(fh, buf, sizeof buf);
    return us(now() - t0);
}

int main(int argc, char **argv)
{
    struct MsgPort *port;
    struct timerequest *tr;
    struct EClockVal ev;
    struct InfoData *id;
    BPTR fh, lock;
    LONG size, off, step;
    int pass;

    if (argc < 2) {
        printf("usage: seekprobe FILE\n");
        return 10;
    }
    port = CreateMsgPort();
    tr = (struct timerequest *)CreateIORequest(port, sizeof *tr);
    if (!tr || OpenDevice(TIMERNAME, UNIT_MICROHZ, (struct IORequest *)tr, 0) != 0) {
        printf("no timer.device\n");
        return 20;
    }
    TimerBase = tr->tr_node.io_Device;
    efreq = ReadEClock(&ev);

    if ((lock = Lock((STRPTR)argv[1], ACCESS_READ)) != 0) {
        id = AllocVec(sizeof *id, MEMF_PUBLIC);
        if (id && Info(lock, id)) {
            ULONG t = (ULONG)id->id_DiskType;
            printf("filesystem %c%c%c\\%ld, block %ld bytes\n", (int)(t >> 24), (int)(t >> 16 & 255),
                   (int)(t >> 8 & 255), (long)(t & 255), (long)id->id_BytesPerBlock);
        }
        FreeVec(id);
        UnLock(lock);
    }
    fh = Open((STRPTR)argv[1], MODE_OLDFILE);
    if (!fh) {
        printf("cannot open %s\n", argv[1]);
        return 20;
    }
    Seek(fh, 0, OFFSET_END);
    size = Seek(fh, 0, OFFSET_BEGINNING);
    printf("size %ld, E-clock %lu Hz\n", (long)size, (unsigned long)efreq);
    step = size / 8;

    for (pass = 1; pass <= 2; pass++) {
        printf("A pass %d: from 0 to X (us seek / us read 512)\n", pass);
        for (off = step; off < size; off += step) {
            ULONG s, r;
            Seek(fh, 0, OFFSET_BEGINNING);
            s = seek_us(fh, off, OFFSET_BEGINNING);
            r = read_us(fh);
            printf("  X=%8ld  %7lu  %6lu\n", (long)off, (unsigned long)s, (unsigned long)r);
        }
    }
    printf("B forward chain, each from the previous offset\n");
    Seek(fh, 0, OFFSET_BEGINNING);
    for (off = step; off < size; off += step) {
        ULONG s = seek_us(fh, off, OFFSET_BEGINNING);
        printf("  X=%8ld  %7lu\n", (long)off, (unsigned long)s);
    }
    printf("C relative +65536 from %ld\n", (long)(step * 6));
    Seek(fh, step * 6, OFFSET_BEGINNING);
    for (pass = 0; pass < 4; pass++)
        printf("  +64K  %7lu\n", (unsigned long)seek_us(fh, 65536, OFFSET_CURRENT));
    printf("D backward, from %ld\n", (long)(step * 7));
    Seek(fh, step * 7, OFFSET_BEGINNING);
    printf("  to %ld (-64K)  %7lu\n", (long)(step * 7 - 65536),
           (unsigned long)seek_us(fh, step * 7 - 65536, OFFSET_BEGINNING));
    Seek(fh, step * 7, OFFSET_BEGINNING);
    printf("  to %ld (-1 MB)  %7lu\n", (long)(step * 7 - 1048576),
           (unsigned long)seek_us(fh, step * 7 - 1048576, OFFSET_BEGINNING));

    Close(fh);
    CloseDevice((struct IORequest *)tr);
    DeleteIORequest((struct IORequest *)tr);
    DeleteMsgPort(port);
    return 0;
}
