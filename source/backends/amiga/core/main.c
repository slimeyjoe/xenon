/* The Amiga family's backend: startup, options, storage, pack, HAL table.
 *
 *   xenon [PAK=file] [LANG=ja|en|pl] [MODE=0xID] [AUDIO=paula|ahi|none] [RATE=hz]
 *         [AUDIOBUF=frames] [AHIUNIT=n] [PAULA14] [OPN=lite|lite2] [QUIET]
 *         [AUTOPLAY=spec|@file] [MAXTICKS=n] [FAST] [SNAPDIR=dir] [PRESENTTEST]
 *         [PRELOAD=auto|yes|no] [AUDIOCPU=percent] [UPLOAD=rtg|chunky]
 *         [VOICEPAK=file] [NOVOICE] [VOICEMIX] [VOICEVOL=percent] [MUSICVOL=percent]
 *         [VOICESTRICT]
 *
 * Test options (plan §10, tests/e2e/amiga.py): the autoplay spec's snap: ticks make the game
 * pause and hand a snapshot to the host (snap.c); FAST drops the frame pacing (the engine is
 * tick-deterministic, so the snapshots do not change); PRESENTTEST runs the synthetic
 * presentation test (backends/common/ptest.h) instead of the game.
 *
 * The same names work as tool types of the program icon when started from Workbench.
 * Pack: the program file's own trailer (HUNK_DEBUG-wrapped, probes/amiga/mktrailer.py),
 * else PROGDIR:xenon.pak, else PAK=. Saves: PROGDIR:saves/ (ENVARC:Xenon/ when PROGDIR is
 * not writable). PRELOAD: the whole pack is read into fast RAM at start when it fits with
 * PRELOAD_RESERVE to spare (auto), whenever it fits (yes) or never (no); otherwise each
 * asset is read from the file when needed. Voice (docs/voice.md): xenon_voice.pak next to
 * the pack (or VOICEPAK=) turns the voice on; it is always read from the file. See README.md. */
#include "os_begin.h"
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <dos/rdargs.h>
#include <workbench/startup.h>
#include <workbench/workbench.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/icon.h>
#include "os_end.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "amiga.h"
#include "gfx/scale.h"
#include "pack/pack.h"
#include "snd/opn_core.h"
#include "autoplay.h"
#include "ptest.h"
#include "amsys.h"
#include "flavour.h"
#include "frameos.h"

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *IconBase;

struct am_opts am_opts;
struct Task *am_main_task;
int am_from_wb;

static char g_storage_prefix[64];
/* a pack source: the asset pack, or the voice pack (never preloaded: hundreds of MB) */
struct am_pak {
    BPTR fh;
    ULONG size;
    UBYTE *mem;                     /* the whole pack file in fast RAM (PRELOAD), else 0 */
    ULONG pos;                      /* the file position after the last read, ~0 = unknown */
};
static struct am_pak g_pak, g_vpak;

/* Fast RAM PRELOAD=auto leaves free after loading the pack: the game's own peak (engine
 * allocations, screen and audio buffers, libraries; README "Memory") plus a margin. */
#define PRELOAD_RESERVE (1536UL * 1024UL)  /* measured need: <= 985 KB (README "Memory") */

/* memory statistics: engine allocations by where they landed ([1] = chip), and the system's
 * free memory low-water mark ([0] = fast) */
static ULONG g_mem_cur[2], g_mem_peak[2], g_mem_peak_sum, g_mem_peak_frame, g_mem_big;
static ULONG g_free0[2], g_free_min[2];
static void mem_note(ULONG chip, ULONG fast);
static void mem_sample(void);
static struct autoplay g_ap;
static int g_autoplay;
static uint32_t g_frames;
static uint32_t g_snap_hash, g_snap_hash_tick;  /* hash of the frame presented for a snap tick */

/* ---- console ------------------------------------------------------------------------------ */

void am_log(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    int n;
    if (am_opts.quiet || am_from_wb || FindTask(NULL) != am_main_task)
        return;
    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n > 0) {
        BPTR out = Output();
        if (out)
            Write(out, buf, n < (int)sizeof buf ? n : (int)sizeof buf - 1);
    }
}

int am_strieq(const char *a, const char *b)
{
    for (;; a++, b++) {
        int ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb)
            return 0;
        if (!ca)
            return 1;
    }
}

/* ---- options ------------------------------------------------------------------------------ */

#define TEMPLATE "PAK/K,LANG/K,MODE/K,AUDIO/K,RATE/N/K,AUDIOBUF/N/K,AHIUNIT/N/K,PAULA14/S," \
                 "OPN/K,QUIET/S,AUTOPLAY/K,MAXTICKS/N/K,FAST/S,SNAPDIR/K,PRESENTTEST/S,PRELOAD/K," \
                 "AUDIOCPU/N/K,UPLOAD/K,SCALE/K,ASPECT/S,WINDOW/S,PUBSCREEN/K," \
                 "VOICEPAK/K,NOVOICE/S,VOICEMIX/S,VOICEVOL/N/K,MUSICVOL/N/K,VOICESTRICT/S"
enum { O_PAK, O_LANG, O_MODE, O_AUDIO, O_RATE, O_AUDIOBUF, O_AHIUNIT, O_PAULA14, O_OPN, O_QUIET,
       O_AUTOPLAY, O_MAXTICKS, O_FAST, O_SNAPDIR, O_PRESENTTEST, O_PRELOAD, O_AUDIOCPU, O_UPLOAD,
       O_SCALE, O_ASPECT, O_WINDOW, O_PUBSCREEN, O_VOICEPAK, O_NOVOICE, O_VOICEMIX, O_VOICEVOL,
       O_MUSICVOL, O_VOICESTRICT, O_COUNT };

static ULONG parse_ulong(const char *s)
{
    ULONG v = 0;
    if (!s)
        return 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        while (*s) {
            int c = *s++;
            v <<= 4;
            if (c >= '0' && c <= '9') v |= (ULONG)(c - '0');
            else if (c >= 'a' && c <= 'f') v |= (ULONG)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (ULONG)(c - 'A' + 10);
            else break;
        }
        return v;
    }
    while (*s >= '0' && *s <= '9')
        v = v * 10 + (ULONG)(*s++ - '0');
    return v;
}

/* SCALE=1|2|3|auto: 0 = auto (the largest that fits) */
static int parse_scale(const char *s)
{
    ULONG v;
    if (!s || am_strieq(s, "auto"))
        return 0;
    v = parse_ulong(s);
    return v < 1 ? 1 : v > XE_SCALE_MAX ? XE_SCALE_MAX : (int)v;
}

static char *dupstr(const char *s)
{
    char *d = s ? malloc(strlen(s) + 1) : 0;
    if (d)
        strcpy(d, s);
    return d;
}

static void defaults(void)
{
    memset(&am_opts, 0, sizeof am_opts);
    am_opts.rate = am_flavour.rate;  /* 68k: 11 025 Hz, opn_lite at 22 050 needs 128-154 % of a 68040/25 */
    am_opts.audiobuf = 0;           /* 93 ms at the rate (audio.c) */
    am_opts.audio_cpu = 75;
    am_opts.scale = 1;
}

static struct RDArgs *g_rda;

static int parse_cli(void)
{
    IPTR args[O_COUNT];
    memset(args, 0, sizeof args);
    g_rda = ReadArgs(TEMPLATE, AM_RDARGS_ARRAY(args), NULL);
    if (!g_rda) {
        PrintFault(IoErr(), "xenon");
        return -1;
    }
    am_opts.pak = (const char *)args[O_PAK];
    am_opts.lang = (const char *)args[O_LANG];
    am_opts.mode_id = parse_ulong((const char *)args[O_MODE]);
    am_opts.audio = (const char *)args[O_AUDIO];
    if (args[O_RATE]) am_opts.rate = *(LONG *)args[O_RATE];
    if (args[O_AUDIOBUF]) am_opts.audiobuf = *(LONG *)args[O_AUDIOBUF];
    if (args[O_AHIUNIT]) am_opts.ahi_unit = *(LONG *)args[O_AHIUNIT];
    am_opts.paula14 = args[O_PAULA14] != 0;
    am_opts.opn = (const char *)args[O_OPN];
    am_opts.quiet = args[O_QUIET] != 0;
    am_opts.autoplay = (const char *)args[O_AUTOPLAY];
    if (args[O_MAXTICKS]) am_opts.max_ticks = *(LONG *)args[O_MAXTICKS];
    am_opts.fast = args[O_FAST] != 0;
    am_opts.snapdir = (const char *)args[O_SNAPDIR];
    am_opts.present_test = args[O_PRESENTTEST] != 0;
    am_opts.preload = (const char *)args[O_PRELOAD];
    if (args[O_AUDIOCPU]) am_opts.audio_cpu = *(LONG *)args[O_AUDIOCPU];
    am_opts.upload = (const char *)args[O_UPLOAD];
    if (args[O_SCALE]) am_opts.scale = parse_scale((const char *)args[O_SCALE]);
    am_opts.aspect = args[O_ASPECT] != 0;
    am_opts.window = args[O_WINDOW] != 0;
    am_opts.pubscreen = (const char *)args[O_PUBSCREEN];
    am_opts.voicepak = (const char *)args[O_VOICEPAK];
    am_opts.novoice = args[O_NOVOICE] != 0;
    am_opts.voicemix = args[O_VOICEMIX] != 0;
    if (args[O_VOICEVOL]) am_opts.voice_volume = *(LONG *)args[O_VOICEVOL];
    if (args[O_MUSICVOL]) am_opts.music_volume = *(LONG *)args[O_MUSICVOL];
    am_opts.voice_strict = args[O_VOICESTRICT] != 0;
    return 0;
}

static void parse_tooltypes(void)
{
    struct WBArg *wa = am_wbmsg->sm_ArgList;
    struct DiskObject *dobj;
    BPTR old;
    const char *v;
    if (!IconBase || !wa->wa_Name)
        return;
    old = CurrentDir(wa->wa_Lock);
    dobj = GetDiskObject(wa->wa_Name);
    CurrentDir(old);
    if (!dobj)
        return;
#define TT(name) FindToolType((UBYTE **)dobj->do_ToolTypes, (UBYTE *)name)
    if ((v = (const char *)TT("PAK")) != 0) am_opts.pak = dupstr(v);
    if ((v = (const char *)TT("LANG")) != 0) am_opts.lang = dupstr(v);
    if ((v = (const char *)TT("MODE")) != 0) am_opts.mode_id = parse_ulong(v);
    if ((v = (const char *)TT("AUDIO")) != 0) am_opts.audio = dupstr(v);
    if ((v = (const char *)TT("RATE")) != 0) am_opts.rate = (LONG)parse_ulong(v);
    if ((v = (const char *)TT("AUDIOBUF")) != 0) am_opts.audiobuf = (LONG)parse_ulong(v);
    if ((v = (const char *)TT("AHIUNIT")) != 0) am_opts.ahi_unit = (LONG)parse_ulong(v);
    if (TT("PAULA14")) am_opts.paula14 = 1;
    if ((v = (const char *)TT("OPN")) != 0) am_opts.opn = dupstr(v);
    if (TT("QUIET")) am_opts.quiet = 1;
    if ((v = (const char *)TT("AUTOPLAY")) != 0) am_opts.autoplay = dupstr(v);
    if ((v = (const char *)TT("MAXTICKS")) != 0) am_opts.max_ticks = (LONG)parse_ulong(v);
    if ((v = (const char *)TT("PRELOAD")) != 0) am_opts.preload = dupstr(v);
    if ((v = (const char *)TT("AUDIOCPU")) != 0) am_opts.audio_cpu = (LONG)parse_ulong(v);
    if ((v = (const char *)TT("UPLOAD")) != 0) am_opts.upload = dupstr(v);
    if ((v = (const char *)TT("SCALE")) != 0) am_opts.scale = parse_scale(v);
    if (TT("ASPECT")) am_opts.aspect = 1;
    if (TT("WINDOW")) am_opts.window = 1;
    if ((v = (const char *)TT("PUBSCREEN")) != 0) am_opts.pubscreen = dupstr(v);
    if ((v = (const char *)TT("VOICEPAK")) != 0) am_opts.voicepak = dupstr(v);
    if (TT("NOVOICE")) am_opts.novoice = 1;
    if (TT("VOICEMIX")) am_opts.voicemix = 1;
    if ((v = (const char *)TT("VOICEVOL")) != 0) am_opts.voice_volume = (LONG)parse_ulong(v);
    if ((v = (const char *)TT("MUSICVOL")) != 0) am_opts.music_volume = (LONG)parse_ulong(v);
    if (TT("VOICESTRICT")) am_opts.voice_strict = 1;
#undef TT
    FreeDiskObject(dobj);
}

/* ---- HAL: video / time / input ----------------------------------------------------------- */

static void v_present(const struct xe_planes *page, const struct xe_rect *dirty, int page_switched,
                      int scroll_line, const uint8_t rgb4[48], int graphics_on)
{
    /* the frame shown at the next tick: hash it when that tick takes a snapshot (the text
     * overlay is in the page only during the present) */
    if (g_autoplay && autoplay_snap_due(&g_ap, g_frames + 1)) {
        g_snap_hash = xe_frame_hash32(page, scroll_line, rgb4, graphics_on);
        g_snap_hash_tick = g_frames + 1;
    }
    ULONG t0 = am_prof_now();
    am_video_present(page, dirty, page_switched, scroll_line, rgb4, graphics_on);
    am_prof_present(t0, page_switched || scroll_line != 0);
}

static void t_frame_wait(void)
{
    /* the frame clock, the input gather and the memory sample: one OS call (frameos.h) */
    am_frame_wait((am_opts.fast ? 0 : AM_F_WAIT) | AM_F_INPUT | AM_F_MEM);
    g_frames++;
    mem_note(am_fr->avail_chip, am_fr->avail_fast);
    if (g_autoplay && autoplay_snap_due(&g_ap, g_frames)) {
        char name[24];
        snprintf(name, sizeof name, "t%lu", (unsigned long)g_frames);
        am_snap(name, g_frames, g_snap_hash, g_snap_hash_tick == g_frames);
    }
    if (am_opts.max_ticks > 0 && (LONG)g_frames > am_opts.max_ticks) {
        am_log("max ticks reached\n");
        /* a quit request lets the engine shut down normally on its next input poll */
        am_opts.max_ticks = -1;
    }
}

static void i_poll(struct xe_input *in)
{
    am_input_poll(in);
    if (g_autoplay)
        autoplay_poll(&g_ap, g_frames, in);
    if (am_opts.max_ticks < 0)
        in->pressed |= XE_IN_EMERGENCY;
}

static void i_flush(void)
{
    am_input_flush();
    if (g_autoplay)
        autoplay_flush(&g_ap, g_frames);
}

/* ---- synthetic presentation test (PRESENTTEST) ---- */

static void pt_snap(int index, const char *name, const struct xe_planes *page, int scroll,
                    const uint8_t rgb4[48], int on, uint32_t hash)
{
    char nm[48];
    (void)page; (void)scroll; (void)rgb4; (void)on;
    snprintf(nm, sizeof nm, "%02d-%s", index, name);
    am_snap(nm, (uint32_t)index, hash, 1);
}

static int present_test(void)
{
    struct xe_video_cfg vc;
    struct pt_io io;
    int n;
    memset(&vc, 0, sizeof vc);
    vc.title = "Xenon present test";
    vc.mode_id = am_opts.mode_id;
    if (am_video_open(&vc) != 0)
        return 20;
    io.present = am_video_present;
    io.snap = pt_snap;
    n = pt_run(&io);
    am_video_close();
    am_log("present test: %d snapshots\n", n);
    return n > 0 ? 0 : 20;
}

/* ---- storage ------------------------------------------------------------------------------ */

static int dir_usable(const char *path)
{
    BPTR lock = Lock((STRPTR)path, SHARED_LOCK);
    if (lock) {
        UnLock(lock);
        return 1;
    }
    lock = CreateDir((STRPTR)path);
    if (lock) {
        UnLock(lock);
        return 1;
    }
    return 0;
}

static void storage_init(void)
{
    if (dir_usable("PROGDIR:saves"))
        strcpy(g_storage_prefix, "PROGDIR:saves/");
    else if (dir_usable("ENVARC:Xenon"))
        strcpy(g_storage_prefix, "ENVARC:Xenon/");
    else
        strcpy(g_storage_prefix, "T:");
    am_log("storage: %s\n", g_storage_prefix);
}

static int s_read(const char *key, void *buf, size_t cap, size_t *len)
{
    char path[128];
    BPTR fh;
    LONG n;
    snprintf(path, sizeof path, "%s%s.dat", g_storage_prefix, key);
    fh = Open((STRPTR)path, MODE_OLDFILE);
    if (!fh)
        return -1;
    n = Read(fh, buf, (LONG)cap);
    Close(fh);
    if (n < 0)
        return -1;
    *len = (size_t)n;
    return 0;
}

static int s_write(const char *key, const void *buf, size_t len)
{
    char path[128], tmp[128];
    BPTR fh;
    LONG n;
    snprintf(path, sizeof path, "%s%s.dat", g_storage_prefix, key);
    snprintf(tmp, sizeof tmp, "%s%s.tmp", g_storage_prefix, key);
    fh = Open((STRPTR)tmp, MODE_NEWFILE);
    if (!fh)
        return -1;
    n = Write(fh, (APTR)buf, (LONG)len);
    Close(fh);
    if (n != (LONG)len) {
        DeleteFile((STRPTR)tmp);
        return -1;
    }
    DeleteFile((STRPTR)path);
    return Rename((STRPTR)tmp, (STRPTR)path) ? 0 : -1;
}

/* ---- pack ---------------------------------------------------------------------------------- */

static BPTR open_sized(const char *path, ULONG *size)
{
    BPTR fh = Open((STRPTR)path, MODE_OLDFILE);
    LONG n;
    if (!fh)
        return 0;
    Seek(fh, 0, OFFSET_END);
    n = Seek(fh, 0, OFFSET_BEGINNING);
    if (n < 0) {
        Close(fh);
        return 0;
    }
    *size = (ULONG)n;
    return fh;
}

static int has_trailer(BPTR fh, ULONG size)
{
    UBYTE tr[XPKT_TRAILER_SIZE];
    if (size < XPKT_TRAILER_SIZE)
        return 0;
    Seek(fh, (LONG)(size - XPKT_TRAILER_SIZE), OFFSET_BEGINNING);
    if (Read(fh, tr, XPKT_TRAILER_SIZE) != XPKT_TRAILER_SIZE)
        return 0;
    return memcmp(tr, "XPKT", 4) == 0 && memcmp(tr + 12, "XPKT", 4) == 0;
}

/* the program file: CLI name relative to the current directory, or the Workbench argument */
static BPTR open_program(ULONG *size)
{
    char name[256];
    BPTR fh = 0;
    if (am_wbmsg) {
        struct WBArg *wa = am_wbmsg->sm_ArgList;
        BPTR old;
        if (!wa->wa_Name)
            return 0;
        old = CurrentDir(wa->wa_Lock);
        fh = open_sized(wa->wa_Name, size);
        CurrentDir(old);
    } else if (GetProgramName((STRPTR)name, sizeof name)) {
        fh = open_sized(name, size);
    }
    return fh;
}

/* PRELOAD: read the whole pack file into fast RAM and close it (FFS Seek walks the file's
 * extension blocks, up to 150 ms per asset on a CF card with the default 30 buffers) */
static void preload(struct am_pak *pk)
{
    const char *o = am_opts.preload;
    int force = o && am_strieq(o, "yes");
    ULONG fast, big, got = 0, t0;
    if (o && am_strieq(o, "no"))
        return;
    fast = AvailMem(am_flavour.preload_memf);
    big = AvailMem(am_flavour.preload_memf | MEMF_LARGEST);
    if (big < pk->size || fast < pk->size + (force ? 0 : PRELOAD_RESERVE)) {
        am_log("pack: read from disk when needed (%lu KB fast RAM free, %lu KB to preload)\n",
               (unsigned long)(fast >> 10),
               (unsigned long)((pk->size + (force ? 0 : PRELOAD_RESERVE)) >> 10));
        return;
    }
    pk->mem = AllocMem(pk->size, am_flavour.preload_memf);
    if (!pk->mem)
        return;
    t0 = am_prof_now();
    if (Seek(pk->fh, 0, OFFSET_BEGINNING) >= 0) {
        while (got < pk->size) {
            LONG n = Read(pk->fh, pk->mem + got, (LONG)(pk->size - got));
            if (n <= 0)
                break;
            got += (ULONG)n;
        }
    }
    if (got != pk->size) {
        am_log("pack: preload failed after %lu bytes, reading from disk\n", (unsigned long)got);
        FreeMem(pk->mem, pk->size);
        pk->mem = 0;
        pk->pos = ~0UL;
        return;
    }
    Close(pk->fh);
    pk->fh = 0;
    am_log("pack: %lu KB in memory, loaded in %lu ms\n", (unsigned long)(pk->size >> 10),
           (unsigned long)am_prof_ms(am_prof_now() - t0));
}

/* The voice pack (docs/voice.md §5.1): VOICEPAK=, else xenon_voice.pak in the directory of
 * PAK=, else PROGDIR:xenon_voice.pak, else the current directory's. NOVOICE: none. */
static BPTR open_voice_pack(ULONG *size)
{
    static const char name[] = "xenon_voice.pak";
    char path[256];
    BPTR fh;
    if (am_opts.voicepak)
        return open_sized(am_opts.voicepak, size);
    if (am_opts.pak && strlen(am_opts.pak) < sizeof path) {
        strcpy(path, am_opts.pak);
        *PathPart((STRPTR)path) = 0;
        if (AddPart((STRPTR)path, (STRPTR)name, sizeof path) && (fh = open_sized(path, size)) != 0)
            return fh;
    }
    if ((fh = open_sized("PROGDIR:xenon_voice.pak", size)) != 0)
        return fh;
    return open_sized(name, size);
}

static void *p_open(int which, uint32_t *size)
{
    if (which == XE_PACK_VOICE) {
        if (am_opts.novoice)
            return 0;
        memset(&g_vpak, 0, sizeof g_vpak);
        g_vpak.fh = open_voice_pack(&g_vpak.size);
        if (!g_vpak.fh)
            return 0;
        if (am_opts.voicepak)
            am_log("voice pack: %s\n", am_opts.voicepak);
        *size = g_vpak.size;
        return &g_vpak;
    }
    if (which != XE_PACK_MAIN)
        return 0;
    memset(&g_pak, 0, sizeof g_pak);
    if (am_opts.pak) {
        g_pak.fh = open_sized(am_opts.pak, &g_pak.size);
    } else {
        g_pak.fh = open_program(&g_pak.size);
        if (g_pak.fh && !has_trailer(g_pak.fh, g_pak.size)) {
            Close(g_pak.fh);
            g_pak.fh = 0;
        }
        if (!g_pak.fh)
            g_pak.fh = open_sized("PROGDIR:xenon.pak", &g_pak.size);
        if (!g_pak.fh)
            g_pak.fh = open_sized("xenon.pak", &g_pak.size);
    }
    if (!g_pak.fh)
        return 0;
    g_pak.pos = ~0UL;                   /* has_trailer read near the end */
    *size = g_pak.size;
    preload(&g_pak);
    return &g_pak;
}

/* A read that continues where the last one ended skips the Seek: on FFS even a short one
 * costs milliseconds (README "Pack on FFS"); the voice pack is read in sequence. */
static size_t p_read(void *h, uint32_t off, void *dst, size_t n)
{
    struct am_pak *pk = h;
    LONG got;
    ULONG ts, t0 = am_prof_now();
    if (pk->mem) {
        if (off >= pk->size)
            return 0;
        if (n > pk->size - off)
            n = pk->size - off;
        memcpy(dst, pk->mem + off, n);
        am_prof_pack(t0, t0, (ULONG)off, (ULONG)n);
        return n;
    }
    if (off != pk->pos && Seek(pk->fh, (LONG)off, OFFSET_BEGINNING) < 0) {
        pk->pos = ~0UL;
        return 0;
    }
    ts = am_prof_now();
    got = Read(pk->fh, dst, (LONG)n);
    pk->pos = got < 0 ? ~0UL : off + (ULONG)got;
    am_prof_pack(t0, ts, (ULONG)off, got < 0 ? 0 : (ULONG)got);
    return got < 0 ? 0 : (size_t)got;
}

static void p_close(void *h)
{
    struct am_pak *pk = h;
    if (pk->mem)
        FreeMem(pk->mem, pk->size);
    else if (pk->fh)
        Close(pk->fh);
    pk->mem = 0;
    pk->fh = 0;
}

/* ---- memory and diagnostics -------------------------------------------------------------- */

#define STACK_PAINT 0x5354414BUL     /* "STAK" */
static struct am_stack g_main_stack;

void am_stack_paint(struct am_stack *st)
{
    void *lower, *upper;
    uintptr_t here = (uintptr_t)&lower;
    ULONG *p;
    am_task_stack(&lower, &upper);  /* the PPC stack on WarpOS */
    st->lower = (ULONG *)(((uintptr_t)lower + 3) & ~(uintptr_t)3);
    st->upper = (ULONG *)upper;
    if (here <= (uintptr_t)st->lower + 512 || here > (uintptr_t)st->upper) {
        st->lower = st->upper = 0;
        return;
    }
    for (p = st->lower; (uintptr_t)p < here - 256; p++)
        *p = STACK_PAINT;
}

ULONG am_stack_used(const struct am_stack *st)
{
    const ULONG *p = st->lower;
    if (!p)
        return 0;
    while (p < st->upper && *p == STACK_PAINT)
        p++;
    return (ULONG)((const UBYTE *)st->upper - (const UBYTE *)p);
}

ULONG am_stack_size(const struct am_stack *st)
{
    return (ULONG)((const UBYTE *)st->upper - (const UBYTE *)st->lower);
}

static void mem_note(ULONG c, ULONG f)
{
    if (c < g_free_min[1]) g_free_min[1] = c;
    if (f < g_free_min[0]) g_free_min[0] = f;
}

static void mem_sample(void)
{
    mem_note(AvailMem(MEMF_CHIP), AvailMem(MEMF_FAST));
}

static void *m_alloc(size_t n, unsigned flags)
{
    void *p = AllocMem((ULONG)n, (flags & XE_MEM_CHIP) ? MEMF_CHIP : MEMF_ANY);
    if (p) {
        int k = (TypeOfMem(p) & MEMF_CHIP) != 0;
        g_mem_cur[k] += (ULONG)n;
        if (g_mem_cur[k] > g_mem_peak[k]) g_mem_peak[k] = g_mem_cur[k];
        if (g_mem_cur[0] + g_mem_cur[1] > g_mem_peak_sum) {
            g_mem_peak_sum = g_mem_cur[0] + g_mem_cur[1];
            g_mem_peak_frame = g_frames;
        }
        if ((ULONG)n > g_mem_big) g_mem_big = (ULONG)n;
        mem_sample();
    }
    return p;
}

static void m_free(void *p, size_t n)
{
    g_mem_cur[(TypeOfMem(p) & MEMF_CHIP) != 0] -= (ULONG)n;
    FreeMem(p, (ULONG)n);
}

static void l_log(int level, const char *msg)
{
    if (level >= XE_LOG_INFO)
        am_log("%s\n", msg);
}

static void l_fatal(const char *msg)
{
    struct EasyStruct es;
    am_log("fatal: %s\n", msg);
    am_video_close();                       /* the requester must be visible */
    es.es_StructSize = sizeof es;
    es.es_Flags = 0;
    es.es_Title = (UBYTE *)"Xenon";
    es.es_TextFormat = (UBYTE *)"%s";
    es.es_GadgetFormat = (UBYTE *)"Quit";
    if (IntuitionBase)
        EasyRequest(NULL, &es, NULL, msg);
    exit(20);
}

/* ---- main ---------------------------------------------------------------------------------- */

static void close_libs(void)
{
    am_audio_exit();
    am_timer_close();
    am_frame_free();
    if (IconBase) CloseLibrary(IconBase);
    if (GfxBase) CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
    IconBase = 0;
    GfxBase = 0;
    IntuitionBase = 0;
}

static int run(void)
{
    struct xe_hal hal;
    struct xe_config cfg;
    int rc;

    am_main_task = FindTask(NULL);
    am_stack_paint(&g_main_stack);
    g_free_min[1] = g_free0[1] = AvailMem(MEMF_CHIP);
    g_free_min[0] = g_free0[0] = AvailMem(MEMF_FAST);
    am_from_wb = am_wbmsg != 0;
    defaults();
    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39);
    GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 39);
    IconBase = OpenLibrary("icon.library", 37);
    if (!IntuitionBase || !GfxBase) {
        if (!am_from_wb)
            PutStr("xenon: needs AmigaOS 3.0 or newer (intuition/graphics v39)\n");
        close_libs();
        return 20;
    }
    if (am_from_wb)
        parse_tooltypes();
    else if (parse_cli() != 0) {
        close_libs();
        return 10;
    }
    am_log("%s\n", am_version + 6);
    if (am_sys_check() != 0) {          /* WarpOS: OS structures as the 68k lays them out */
        if (g_rda)
            FreeArgs(g_rda);
        close_libs();
        return 20;
    }
    /* the once-per-frame call's struct before anything opens the window (input.c fills it in;
     * the present test opened it without one and wrote through a null pointer) */
    if (am_frame_init() != 0)
        l_fatal("out of memory");
    if (am_opts.present_test) {
        rc = present_test();
        if (g_rda)
            FreeArgs(g_rda);
        close_libs();
        return rc;
    }
    if (am_timer_open() != 0) {
        l_fatal("cannot open timer.device");
    }
    am_audio_init();
    storage_init();
    if (am_opts.autoplay) {
        char err[64];
        if (autoplay_parse(&g_ap, am_opts.autoplay, err, sizeof err) != 0)
            l_fatal(err);
        g_autoplay = 1;
        am_log("autoplay: %d presses (first at %lu), %d ranges, %d snapshots\n", g_ap.at_count,
               (unsigned long)(g_ap.at_count ? g_ap.at[0].tick : 0), g_ap.rng_count, g_ap.snap_count);
    }

    memset(&cfg, 0, sizeof cfg);
    cfg.video.title = "Xenon";
    cfg.video.mode_id = am_opts.mode_id;
    cfg.language = am_opts.lang ? xpak_lang_id(am_opts.lang) : 0;
    cfg.audio_rate = (am_opts.audio && am_strieq(am_opts.audio, "none")) ? 0 : (int)am_opts.rate;
    cfg.opn_core = XE_OPN_LITE;
    cfg.music_volume = (int)am_opts.music_volume;
    cfg.voice_volume = (int)am_opts.voice_volume;
    cfg.voice_strict = am_opts.voice_strict;
    if (am_opts.opn && am_strieq(am_opts.opn, "lite2"))
        opn_lite_fm_oversample = 2;

    memset(&hal, 0, sizeof hal);
    hal.video_open = am_video_open; hal.video_close = am_video_close; hal.video_present = v_present;
    hal.frame_wait = t_frame_wait; hal.ticks = am_ticks;
    hal.input_poll = i_poll; hal.input_flush = i_flush;
    hal.audio_open = am_audio_open; hal.audio_close = am_audio_close;
    hal.audio_lock = am_audio_lock; hal.audio_unlock = am_audio_unlock;
    hal.set_volume = am_audio_set_volume;
    /* voice: the driver's own stream (Paula's free channels, a second AHI stream), or mixed
     * into the music by the engine (PAULA14, VOICEMIX); README.md "Voice" */
    hal.voice_mode = cfg.audio_rate > 0 ? am_audio_voice_mode() : XE_VOICE_NONE;
    if (hal.voice_mode == XE_VOICE_CHANNEL) {
        hal.voice_open = am_voice_open; hal.voice_close = am_voice_close;
        hal.voice_space = am_voice_space; hal.voice_push = am_voice_push;
        hal.voice_queued = am_voice_queued; hal.voice_flush = am_voice_flush;
        /* two blocks in flight, as long as the music's (AUDIOBUF, 93 ms by default), and as
         * much again in the FIFO */
        hal.voice_lead_ms = (int)(4 * 1000L * (am_opts.audiobuf > 0 ? am_opts.audiobuf
                                                                    : (LONG)am_opts.rate * 2048 / 22050)
                                  / (am_opts.rate > 0 ? am_opts.rate : 1));
    }
    hal.storage_read = s_read; hal.storage_write = s_write;
    hal.pack_open = p_open; hal.pack_read = p_read; hal.pack_close = p_close;
    hal.mem_alloc = m_alloc; hal.mem_free = m_free; hal.log = l_log; hal.fatal = l_fatal;

    rc = xe_run(&hal, &cfg);
    {
        ULONG t = am_ticks();
        am_log("exit %d after %lu frames in %lu ticks of real time (56.42 Hz), %lu ms per frame\n",
               rc, (unsigned long)g_frames, (unsigned long)t,
               (unsigned long)(g_frames ? (unsigned long long)t * 17724ULL / 1000ULL / g_frames : 0));
    }
    am_log("memory: engine peak %lu KB at frame %lu (fast %lu, chip %lu), largest block %lu KB, "
           "%lu bytes not freed; free low-water: chip %lu KB, fast %lu KB below the start "
           "(of %lu, %lu KB)\n",
           (unsigned long)(g_mem_peak_sum >> 10), (unsigned long)g_mem_peak_frame,
           (unsigned long)(g_mem_peak[0] >> 10), (unsigned long)(g_mem_peak[1] >> 10),
           (unsigned long)(g_mem_big >> 10), (unsigned long)(g_mem_cur[0] + g_mem_cur[1]),
           (unsigned long)((g_free0[1] - g_free_min[1]) >> 10),
           (unsigned long)((g_free0[0] - g_free_min[0]) >> 10),
           (unsigned long)(g_free0[1] >> 10), (unsigned long)(g_free0[0] >> 10));
    am_log("stack: main %lu of %lu bytes used, audio %lu of %lu\n",
           (unsigned long)am_stack_used(&g_main_stack), (unsigned long)am_stack_size(&g_main_stack),
           (unsigned long)am_stack_used(&am_audio_stack),
           (unsigned long)am_stack_size(&am_audio_stack));
    if (g_rda)
        FreeArgs(g_rda);
    close_libs();
    return rc;
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    return am_main_stack(run);
}
