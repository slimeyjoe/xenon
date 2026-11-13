/* The Amiga family's backend - shared declarations of the core part (README.md "Layout").
 *
 * Files: main.c (startup, options, storage, pack, HAL table), video.c (screen, screen
 * buffers, present), input.c (window, IDCMP, pointer, screen front/back policy), timer.c
 * (56.42 Hz frame clock), frame.c + frameos.c (the once-per-frame OS call), audio.c (audio
 * task, pull-mode HAL audio) with the output drivers behind the interface in audio.h
 * (audio_ahi.c here, chipset/audio_paula.c), snap.c (test snapshots). What differs by
 * system is behind amsys.h (exec/ or warpos/), what differs by flavour behind flavour.h
 * (os3/, warpos/, aros/), what differs by compiler in amcompat.h. */
#ifndef XE_AMIGA_H
#define XE_AMIGA_H

#include "os_begin.h"
#include <exec/types.h>
#include <exec/execbase.h>
#include <dos/dos.h>
#include <intuition/intuitionbase.h>
#include <intuition/screens.h>
#include <graphics/gfxbase.h>
#include "os_end.h"
#include "amcompat.h"
#include "xe.h"

/* Options from the CLI template or the icon's tool types (same names). */
struct am_opts {
    const char *pak;        /* PAK=path: pack file (default: the trailer, else PROGDIR:xenon.pak) */
    const char *lang;       /* LANG=ja|en */
    ULONG mode_id;          /* MODE=0x...: display mode id (default HIRESLACE_KEY) */
    const char *audio;      /* AUDIO=paula|ahi|none (default paula) */
    LONG rate;              /* RATE=hz (default 11025: 22050 costs more than a 68040/25) */
    LONG audiobuf;          /* AUDIOBUF=frames per block (default: 93 ms at RATE) */
    LONG audio_cpu;         /* AUDIOCPU=percent of real time the renderer may use (default 75) */
    LONG ahi_unit;          /* AHIUNIT=n (default 0) */
    int paula14;            /* PAULA14: 14-bit output on four channels */
    const char *voicepak;   /* VOICEPAK=path: the voice pack (default: xenon_voice.pak next to the pack) */
    int novoice;            /* NOVOICE: no voice even when the voice pack is there */
    int voicemix;           /* VOICEMIX: the engine mixes the voice into the music (AHI, 1 channel) */
    LONG voice_volume;      /* VOICEVOL=percent (default 100) */
    LONG music_volume;      /* MUSICVOL=percent (default 50 with the voice, else 100) */
    int voice_strict;       /* VOICESTRICT: OK does not cut a clip (the Windows release's rule) */
    const char *opn;        /* OPN=lite|lite2 */
    int quiet;              /* QUIET: no console output */
    const char *autoplay;   /* AUTOPLAY=spec (tests, backends/common/autoplay.h) */
    LONG max_ticks;         /* MAXTICKS=n: exit after n frames (tests) */
    int fast;               /* FAST: no frame pacing, the engine runs as fast as it can (tests) */
    int present_test;       /* PRESENTTEST: run the synthetic presentation test (ptest.h) */
    const char *snapdir;    /* SNAPDIR=dir: where snapshot requests go (default PROGDIR:) */
    const char *preload;    /* PRELOAD=auto|yes|no: the whole pack into fast RAM (default auto) */
    const char *upload;     /* UPLOAD=name: the graphics card upload (video.c, upload.h) */
    int scale;              /* SCALE=1|2|3|auto (0 = auto): page pixels shown n x n (default 1) */
    int aspect;             /* ASPECT: show the page as a 4:3 picture (rows repeated) */
    int window;             /* WINDOW: in a window on a public screen (video_win.c) */
    const char *pubscreen;  /* PUBSCREEN=name: that screen (default: the default public screen) */
};
extern struct am_opts am_opts;
extern struct Task *am_main_task;
extern int am_from_wb;

/* main.c */
void am_log(const char *fmt, ...);          /* console output (CLI start, main task only) */
int  am_strieq(const char *a, const char *b); /* case-insensitive string equality */

/* video.c */
int  am_video_open(const struct xe_video_cfg *cfg);
void am_video_close(void);
void am_video_present(const struct xe_planes *page, const struct xe_rect *dirty, int page_switched,
                      int scroll_line, const uint8_t rgb4[48], int graphics_on);
struct Screen *am_video_screen(void);

/* What a snapshot reader needs to find the displayed image in memory (snap.c). */
struct am_snapinfo {
    int shown;              /* index of the displayed screen buffer */
    int standard;           /* BMF_STANDARD: planar chip RAM; 0 = graphics card bitmap */
    ULONG bpr, rows, depth;
    void *planes[8];
    ULONG mode;             /* display mode id of the screen */
    struct BitMap *bm;
    int pen_base;           /* pen of engine colour 0 (16 on a graphics card screen) */
    int dx, dy;             /* the page's top left in the bitmap (a screen larger than 640x400) */
    int sx, dst_h;          /* SCALE/ASPECT (upload.h am_geom); sx 0 = plain 640x400 */
    int window;             /* in a window (video_win.c): dx,dy = its inside on the screen */
    int pens[16];           /* the window's pens of the engine colours */
    int mouse_x, mouse_y;   /* the pointer on the screen (visible in a window) */
};
void am_video_snapinfo(struct am_snapinfo *si);

/* snap.c: tells the host (tests/e2e/amiga_snapd.py) to take a snapshot of the screen now and
 * waits until it has. hash = xe_frame_hash32 of what must be shown (have_hash 0: unknown). */
void am_snap(const char *name, uint32_t tick, uint32_t hash, int have_hash);

/* video_win.c: the game in a window on a public screen (WINDOW). am_win_open: 0 = open,
 * 1 = the screen cannot take it (the caller opens its own screen), -1 = failure */
int  am_win_open(const struct xe_video_cfg *cfg);
void am_win_close(void);
void am_win_present(const struct xe_planes *page, const struct xe_rect *dirty, int page_switched,
                    int scroll_line, const uint8_t rgb4[48], int graphics_on);
void am_win_snapinfo(struct am_snapinfo *si);
void am_win_refresh(void);      /* input.c: IDCMP_REFRESHWINDOW */
void am_win_changed(void);      /* input.c: IDCMP_CHANGEWINDOW */
void am_win_cycle(void);        /* input.c: F10, the next size */

/* input.c */
int  am_input_open(struct Screen *scr);
int  am_input_attach(struct Window *win);   /* the window of video_win.c */
void am_input_close(void);
void am_input_poll(struct xe_input *in);
void am_input_flush(void);

/* timer.c */
int  am_timer_open(void);
void am_timer_close(void);
void am_frame_wait(ULONG ops);           /* frameos.h AM_F_*: one OS call per frame */
uint32_t am_ticks(void);
/* Stall profile: time spent in pack reads and presents (main task) and in rendering
 * (audio task) since the frame clock was last on time; one log line per stall of more than
 * ten ticks says where the time went and which pack bytes were read. All main-task times are
 * wall time: the audio task (higher priority) runs inside them, so its share is listed apart. */
/* The profile clock (the system part's, amsys.h am_prof_clock_init): E-clock ticks (low 32
 * bits); on WarpOS PPC time base ticks / 16 (about 1 MHz, read without a 68k call). Any
 * task. */
ULONG am_prof_now(void);
extern struct Device *TimerBase;            /* timer.c, once timer.device is open */
ULONG am_prof_ms(ULONG eticks);
ULONG am_prof_freq(void);                   /* profile ticks per second */
void  am_prof_pack(ULONG t0, ULONG t_seek, ULONG off, ULONG n); /* t_seek: after the Seek */
void  am_prof_present(ULONG t0, int full);
ULONG am_prof_present_ms(ULONG *calls);    /* presents of the whole run: total ms, count */
extern volatile ULONG am_prof_audio;        /* profile ticks the audio task spent rendering */

/* main.c: stack high-water mark. am_stack_paint fills the unused part of the calling task's
 * stack with a pattern; am_stack_used (same task or later) finds the deepest word that is no
 * longer the pattern. size 0 = the stack bounds did not contain the current stack pointer. */
struct am_stack { ULONG *lower, *upper; };
void  am_stack_paint(struct am_stack *st);
ULONG am_stack_used(const struct am_stack *st);
ULONG am_stack_size(const struct am_stack *st);
extern struct am_stack am_audio_stack;     /* audio.c: the audio process's stack */

/* audio.c */
void am_audio_init(void);                   /* before xe_run: the lock must exist early */
void am_audio_exit(void);                   /* frees the lock (after xe_run) */
int  am_audio_open(int rate);
void am_audio_close(void);
int  am_audio_voice_mode(void);             /* XE_VOICE_* the options give (before xe_run) */
int  am_audio_set_volume(int which, int percent);   /* the HAL's set_volume */
int  am_voice_open(int rate);               /* the HAL's voice_* (main task) */
void am_voice_close(void);
int  am_voice_space(void);
void am_voice_push(const int16_t *pcm, int frames);
int  am_voice_queued(void);
void am_voice_flush(void);
void am_audio_lock(void);
void am_audio_unlock(void);

#endif
