# AmigaOS backend

The Amiga build of the engine (`make TARGET=amiga`, vbcc 0.9h, plain C) for
AmigaOS 3.0 and newer, aimed at a 68040/25 with 2 MB chip RAM and 4 MB fast
RAM, native AGA/ECS display or a Picasso96 graphics card. It is an ordinary
Intuition program: its own screen, a window for input, no hardware taken away
from the system except the Paula channels it allocates from audio.device.
The same backend also builds for the PowerPC under WarpOS (`make
TARGET=warpos`, see [WarpOS](#warpos-powerpc)), and its parts are arranged so
that other members of the family (AROS, later MorphOS and AmigaOS 4) share
the code ([Layout](#layout-parts-and-flavours)).

## Layout: parts and flavours

The backend is a set of directories ("parts"); each target's
`config/<TARGET>.mk` lists the ones it is built from (`BACKEND_DIRS`, all on
the include path), so nothing is selected by `#ifdef`:

| part | what | amiga | warpos |
|---|---|---|---|
| `core/` | everything that is plain Amiga API: options, storage, pack (`main.c`), screen and present (`video.c`), the Workbench window (`video_win.c`), the uploads `rtg`, `chunky` and `scaled` (`upload_*.c`), input, the frame clock (`timer.c`, `frame.c`, `frameos.c`), the audio task and the AHI driver, snapshots | x | x |
| `exec/` | system glue for one CPU running exec (`sys_exec.c`): tasks, semaphores, `Wait`, caches, the E-clock profile clock, the direct frame call | x | |
| `warpos/` | system glue for WarpOS instead (`sys_wos.c`: PPC tasks, `WaitPPC`, PPC semaphores, cache flushes, the time base clock, `Run68K`), the OS layout guard | | x |
| `chipset/` | the classic Amiga hardware: the Paula driver and its interrupt handler, the planar chip RAM uploads (plain and through a row map) | x | x |
| `os3/`, `warpos/`, `aros/` | the flavour (`flavour.c`): default mode, rate, preload memory and frame clock timer unit, the list of audio drivers and uploads, `$VER`, the Picasso96API call for snapshots | x | x |

The interfaces between them:

* `core/amsys.h`: the system glue, implemented by `exec/` or `warpos/`;
* `core/flavour.h`: what sets a flavour apart, one `flavour.c` per flavour.
  `AUDIO=` and `UPLOAD=` pick from its lists by name (the first is the
  default), so a flavour without Paula simply does not list it;
* `core/audio.h` (output drivers) and `core/upload.h` (how the page gets
  into a screen buffer: `planar`, `rtg`, `chunky`, see "Display");
* `core/amcompat.h`: the only file that tests compiler macros (`IPTR` where
  the headers lack it, vbcc's `__reg`, the startup's Workbench message);
  `core/os_begin.h`/`os_end.h` bracket every block of OS headers (the
  alignment pragma on WarpOS's PPC side, empty elsewhere).

`TARGET=aros` is built from `core/`, `exec/` and `aros/` (see
[AROS](#aros-x86_64)). Exactly one flavour macro is set by the config
(`XE_AMIGA_OS3`, `XE_AMIGA_WARPOS`, `XE_AMIGA_AROS`); code tests a flavour
only in those two headers.
Pointers never pass through `ULONG` (`IPTR` tag data and `ReadArgs`
results, `uintptr_t` alignment), so a 64-bit flavour compiles the same core.
A new flavour is a config file, a `flavour.c`, and the system part it
runs on (`exec/` or its own).

## Starting it

From a shell:

    xenon [PAK=file] [LANG=ja|en|pl] [MODE=0xID] [AUDIO=paula|ahi|none] [RATE=hz]
          [AUDIOBUF=frames] [AHIUNIT=n] [PAULA14] [OPN=lite|lite2] [QUIET]
          [AUTOPLAY=spec|@file] [MAXTICKS=n] [FAST] [SNAPDIR=dir] [PRESENTTEST]
          [PRELOAD=auto|yes|no] [AUDIOCPU=percent] [UPLOAD=rtg|chunky]
          [VOICEPAK=file] [NOVOICE] [VOICEMIX] [VOICEVOL=percent] [MUSICVOL=percent] [VOICESTRICT]
          [WINDOW] [PUBSCREEN=name] [SCALE=1|2|3|auto] [ASPECT]

From Workbench the same names are tool types of the program's icon.
`xenon.info` (built next to the executable by `tools/mkicon.py` from the
opening's XENON lettering, on a transparent background; not committed, it is
the game's art: the build makes it from `resources/anim/0100B/28.png`, which
`make data` derives from your copy of the game, and leaves it out without)
carries
them at their defaults - `AUDIO=paula`, `RATE=11025`, `AUDIOCPU=75`,
`OPN=lite`, `PRELOAD=auto`, `MODE=0x8004` - plus `(LANG=en)`,
`(PAK=PROGDIR:xenon.pak)`, `(AUDIOBUF=1024)`, `(AHIUNIT=0)`, `(PAULA14)`,
`(QUIET)` switched off in parentheses, the Information window's convention
(icon.library ignores them: `FindToolType("LANG")` is NULL until the
parentheses go). The icon's stack size is 8 KB (the main task uses about
2.4 KB). The file holds a classic 3-plane image for Workbench 2.x/3.1 and
an OS 3.5 colour image for icon.library 44 and later (3.5/3.9, 3.2, 4.x,
AROS, MorphOS); how it looks on a stock Workbench 3.1 with 8 and 4 colours
is in `probes/amiga/README.md` "The Workbench icon". `drawer.info` is the
same picture as a drawer icon (`mkicon.py --drawer`): it belongs next to
the game's directory as `<directory>.info` (`a1200.sh install` puts it at
`Stuff:xenon.info`), so the drawer shows up without "Show All Files".

`make TARGET=amiga dist` packs all of it - `Xenon.info` plus `Xenon/` with
`xenon`, `xenon.info` and `ReadMe.txt`, but not `xenon.pak`, the game's
data, which the player makes from the original release or obtains elsewhere
(the ReadMe's section 3; `docs/game-data.md`) (`ReadMe.txt` is the player's
introduction: requirements, controls, tool types, credits; plain ASCII for
More/MultiView; the Makefile puts it together from `readme/`: the parts all
flavours share - `title`, `intro`, `controls`, `credits` - and each
flavour's own, `README_PARTS` in its config; update them when an option or
a key changes) - into
`build/amiga/xenon-amiga-<version>.lha` (`-lh5-`, level-1 headers), which
LhA 2.x on the Amiga unpacks with `lha x xenon-amiga-1.4.lha`; the executable comes
out with its `e` bit set. `tests/amiga/run-dist.sh` unpacks and runs it
in WinUAE and compares the files (`tests/amiga/README.md` "The release
archive").

**Version.** `engine/version.h` holds the release version (`XE_VERSION`,
1.0 = the first release, git tag `v1.0`) and its date. The Makefile names
the archive after it, and each `flavour.c` builds the AmigaOS version string from
it: `version xenon` prints `xenon 1.4 (13.11.2026)`, and the game's log
starts with the same line. A new release bumps both values in that header,
the version line in `readme/intro.txt` and the archive names in
`readme/install-*.txt`, builds the three archives and checks them, then
gets its own (annotated) tag:

    . ~/amiga-sdk/env.sh
    make TARGET=amiga dist  && tests/amiga/run-dist.sh    # WinUAE, LhA 2.x
    make TARGET=warpos dist                                # same layout, PPC executable
    make TARGET=aros dist   && tests/aros/run-dist.sh      # hosted AROS, AROS's lha

1.0 (9.11.2026) was the first release, 1.1 (10.11.2026) added Polish, 1.2
(11.11.2026) the WarpOS and AROS builds, 1.3 (12.11.2026) the `WINDOW`,
`SCALE` and `ASPECT` options ("Window, scale, aspect"), 1.4 (13.11.2026) the
voices of the Windows release ("Voice"), quitting at once ("Keys"), and
archives without `xenon.pak`: the player makes it from the original game
(`docs/game-data.md`).

| option | default | meaning |
|---|---|---|
| `PAK` | the program file's own trailer, else `PROGDIR:xenon.pak` | asset pack (`tools/mkpack.py`) |
| `LANG` | pack default | language |
| `MODE` | `0x8004` (HiRes Laced, 640x400) | display mode id; any 640x400-capable mode works, Picasso96 promotion of the default is fine |
| `AUDIO` | `paula` | output driver, see below; `none` runs silent (the sequencer still runs for the script cues) |
| `RATE` | 11025 | output sample rate asked of the chip core; the driver reports what it really plays. 22050 sounds better but costs more than a 68040/25 has (see "Audio") |
| `AUDIOBUF` | 93 ms | frames per audio block (two blocks in flight = the latency); default 1024 at 11 025 Hz, 2048 at 22 050 Hz |
| `AUDIOCPU` | 75 | percent of real time the audio task may spend rendering, 10-100; over it, blocks are skipped (silence) instead of starving the machine |
| `AHIUNIT` | 0 | ahi.device unit for `AUDIO=ahi` |
| `PAULA14` | off | 14-bit Paula output on four channels (the voice is then mixed into the music) |
| `VOICEPAK` | `xenon_voice.pak` next to `PAK=`, else `PROGDIR:`, else the current directory | the voice pack (docs/voice.md); found = voice on. See "Voice" |
| `NOVOICE` | off | no voice even with a voice pack |
| `VOICEMIX` | off | the engine mixes the voice into the music (for an AHI unit with one channel) instead of a stream of its own |
| `VOICEVOL`, `MUSICVOL` | 100; 50 with voice, else 100 | percent; Paula's `AUDxVOL` / AHI's `ahir_Volume` where the voice has its own stream, else the engine scales the samples |
| `VOICESTRICT` | off | OK does not cut a clip (the Windows release's rule, docs/voice.md §4.1) |
| `OPN` | `lite` | `lite2` oversamples the FM part 2x (about 1.6x the CPU, for a 68060) |
| `QUIET` | off | no console output |
| `WINDOW`, `PUBSCREEN` | off, the default public screen | the game in a window on that screen; falls back to the own screen where it cannot be (see "Window, scale, aspect") |
| `SCALE` | 1 | page pixels shown 1-3 times as large; `auto` = the largest that fits; on the own screen a graphics card mode |
| `ASPECT` | off | the page as a 4:3 picture (rows repeated: 640x400 -> 640x480) |
| `UPLOAD` | `rtg` | how a graphics card screen is drawn: `rtg` = `BltBitMap` from the engine's planes, `chunky` = converted here and written with `WriteChunkyPixels` (see "Display"); a planar native screen always takes the CPU copy |
| `PRELOAD` | `auto` | read the whole pack into fast RAM at start: `auto` when it fits in one block with 1.5 MB of fast RAM to spare, `yes` whenever it fits, `no` never (each asset is then read from the file when needed); see "Memory" |
| `AUTOPLAY`, `MAXTICKS` | | test automation (`backends/common/autoplay.h`); quote the spec: `AUTOPLAY="at:650=down,750=ok"`, or `AUTOPLAY=@file` for a long one; its `snap:` ticks pause the game for a host snapshot (`snap.c`) |
| `FAST`, `SNAPDIR`, `PRESENTTEST` | | tests: no frame pacing; where snapshot requests go (default `PROGDIR:`); run the synthetic presentation test instead of the game (`tests/e2e/README.md` "Amiga in WinUAE") |

Saves go to `PROGDIR:saves/` (`ENVARC:Xenon/` when the program directory is
not writable, e.g. a CD). Ctrl-C in the shell asks the game to quit.

## Keys

Cursor keys or numpad 8/2/4/6, Return / Enter / Space = OK, Esc or numpad 0 =
cancel, Help = HELP, F1-F5 = f·1-f·5, Ctrl held = skip waits, Caps Lock =
instant text (the KANA lock of the PC-98), Shift+Esc = emergency quit. Mouse:
motion steps the cursor as on the PC-98 (the pointer is hidden and irrelevant),
left button = OK, right = cancel. In a window (`WINDOW`): F10 = the next size,
the close gadget = quit (Ctrl-C), the pointer stays visible.

All three ways to quit (Shift+Esc, the close gadget, Ctrl-C; `MAXTICKS` too) take effect at
the next frame tick: the engine (`sys_quitting`, engine/sys.c) ends every wait at once,
stops presenting and waiting for ticks, and ends the script after the statement in
progress, so only the CPU work already started (an image being decoded) delays the exit.
Up to 1.3 a quit outside a menu played the script on to its next menu: up to 73 s of
the prologue on a 68040/25. The HAL's `input_flush` keeps quit presses (`xe_hal.h`).

## Switching between the game and the Workbench

The game screen is a normal Intuition screen, so the system shortcuts work:
**LAmiga+N** brings the Workbench to the front, **LAmiga+M** cycles screens
(the game screen comes back). The backend watches the screen order every
frame: when its screen goes to the back it restores the mouse pointer and
stops seeing input; when it comes to the front again it re-activates its
window and hides the pointer. The game keeps running in the background
(music plays on, timers run) the way a real PC-98 would; nothing is paused.

## Display

`video.c`. A 640x400x4 custom screen with two V39 `ScreenBuffer`s. The engine
owns its planes in fast RAM; each frame the changed rows/byte-columns are
copied into the *displayed* buffer - the original wrote straight into the
displayed VRAM page, and this keeps the 56.42 Hz game tick independent of the
50 Hz display (no `WaitTOF` per frame). When the engine switches its display
page, the whole new page goes into the hidden buffer and `ChangeScreenBuffer`
flips. Which copy ("upload", `core/upload.h`) is used is decided per buffer
from `BMF_STANDARD` (`probes/amiga/README.md`): planar chip-RAM bitmap =
`planar`, the CPU copy (`chipset/upload_planar.c`; the blitter cannot read
fast RAM); anything else = the flavour's graphics card upload, by default
`rtg`, `BltBitMap` from fast RAM (`core/upload_rtg.c`; Picasso96 converts
planar to chunky in software), or with `UPLOAD=chunky` the page converted
to pixels here (`engine/gfx/p2c.h`) and written with one `WriteChunkyPixels`
per rectangle (`core/upload_chunky.c`; the path that worked in MorphOS's
QEMU, `probes/morphos/README.md`, and the one for AROS). Both pass the
presentation test on promoted and uaegfx screens. Palette changes (fades) are `LoadRGB32`s;
on a depth-8 screen that is true colour underneath (its bitmap deeper than
the screen) they also re-upload the whole page, since nothing recolours the
pixels already drawn.
`G_SCROLL` shakes upload the whole rotated page (slow on RTG, rare); dirty
updates while scrolled go to the rotated rows.

The overscan border is blanked (`VTAG_BORDERBLANK_SET`), so it stays black as
on the PC-98 instead of showing colour 0 (a fade to white would whiten it).
On a graphics card screen, including a promoted native mode, the screen is
reopened with depth 8 and `BltBitMap` gets a source with four constant extra
planes (plane 4 all ones, 5-7 zero): the engine's colours are pens 16-31, pen
0 stays black for the board rows outside the 400 lines, and every byte of
the chunky buffer is written (a depth-4 blit leaves the upper four bits of
uncleared video memory in place, which showed as wrong pens). The probe for
this happens before any screen buffer exists: freeing the buffers of a
never-flipped promoted screen crashed the emulated machine.

## Window, scale, aspect

Three options on top of the default screen, all chosen once when the display
opens. **The default (none of them) runs exactly the code it always did:**
the objects that draw the plain 640x400 screen (`upload_planar`, `upload_rtg`,
`upload_chunky`, `frameos`) and the whole engine were byte-identical to the
1.2 release in 1.3 (after 1.3 the quit fix, "Keys", changed `sys`, `cc_vm` and
`game`: two tests per frame and one per script statement), which
`tests/amiga/fastpath-check.sh check` verifies (`record`
writes the checksums, `ref REV` compares with a git revision). The default
present costs one more test (`g_window`) per frame, and the exit line now
says how long all presents took (`video: ... N presents took M ms`), the
number to compare on the real A1200. What the options rely on was measured
first: `probes/amiga/README.md` "The Workbench window" (`winprobe.c`).

**`SCALE` and `ASPECT` on the game's own screen** (`video.c` `open_scaled`).
The page goes through a row map (`engine/gfx/scale.h`: display row r on
rows `r*h/400 .. (r+1)*h/400-1`, nearest neighbour; `tests/test_scale.c`)
and, on a graphics card, is drawn sx times as wide:

* a native screen takes no larger picture (AGA's wide modes are planar and
  no faster); `ASPECT` there uses the mode's pixel shape from its
  `DisplayInfo` `Resolution`: PAL HiRes Laced is 22:22 (square), so the
  page becomes 640x480 on a 640x480 laced screen, copied by the CPU through
  the row map (`chipset/upload_planar_map.c`, about a fifth more chip
  writes than the plain copy); NTSC HiRes Laced (22:26 as Picasso96 has it)
  is already 4:3 and stays plain (`xe_aspect_height` takes anything within
  2 % as the plain height);
* a graphics card: square pixels (Picasso96's `Resolution` assumes a 4:3
  monitor for every mode, wrong for 16:9 and 5:4 LCDs); the screen is the
  mode given with `MODE=`, else the smallest board mode of at least 640·sx
  by 400·sx (480·sx with `ASPECT`; `board_mode` skips the native monitor
  ids below 0x00100000 where the flavour has a planar display), stepping
  down to smaller factors when none is. `SCALE` on a native default mode
  also looks for a board mode. The upload is `scaled`
  (`core/upload_scaled.c`): the changed rectangle expanded a strip of 16
  rows at a time into a buffer of at most a few hundred KB (never a whole
  scaled frame) and written with `WriteChunkyPixels`, pens 16..31 as on
  the plain depth-8 screen.

When neither applies (a factor of 1 and nothing to stretch), the plain path
opens as usual. Snapshot requests carry `sx= dsth= dx= dy=` for a scaled
screen.

**`WINDOW`** (`core/video_win.c`): a window on the public screen
`PUBSCREEN=` (default: the default public screen). The engine's colours
are 16 exclusive pens (`ObtainPen`); the page is drawn with
`WriteChunkyPixels` into the window's RastPort through the same strips
(`am_scaled_setup`/`am_scaled_draw`), so the layers clip it against other
windows - one path for every screen depth, as the probe showed:

| public screen | result |
|---|---|
| 8 or 16 colours (0 / 8 free pens) | no window: the own screen opens, with a log line |
| 256 colours (AGA, 8-bit card) | pens changed in place by `SetRGB32`: fades as cheap as on the own screen |
| 15/16/24/32-bit | `WriteChunkyPixels` converts the pens; a changed colour does not reach pixels already drawn, so a palette change redraws the page - only the last of a run while the frame clock is behind (`late_us`), so a fade shows fewer steps instead of stalling (a full 16-bit redraw takes several ticks on a 68040) |
| narrower than 640 + borders (648x413) | a borderless window (every native 640-wide Workbench); smaller than 640x400: the own screen |

`SCALE`/`ASPECT` choose the window's size (the native screen's pixel shape,
square on a card); F10 steps through the sizes that fit (1x, 1x 4:3, 2x,
...) with `ChangeWindowBox` and redraws on `IDCMP_CHANGEWINDOW`. There is
one buffer: a page switch redraws the window in place (it can tear), and
`IDCMP_REFRESHWINDOW` (`BeginRefresh`/`EndRefresh`) redraws the page at the
next present. Input (`input.c` `am_input_attach`): the game window is the
input window; the pointer stays visible (the active window's pointer is the
whole screen's); the click that activates the window is delivered to it
too, so mouse buttons are ignored for two polls after `IDCMP_ACTIVEWINDOW`;
`IDCMP_DELTAMOVE` keeps reporting outside the window, so the mouse steps
the cursor as on the own screen; the close gadget asks to quit. While
Intuition holds the screen's layers (a window being dragged in outline
mode, a menu open) the drawing waits and the game stops; the audio task
plays on and the frame clock catches up. Snapshot requests carry
`window=1 pens=...` and the window's inside as `dx dy`.

Tests: `make TARGET=amiga e2e E2E_ARGS="present --configs scaled"` and
`--configs window` (`tests/e2e/README.md` "Amiga in WinUAE"); AROS with `aros.py present
--guest-args`, WarpOS with `tests/warpos/run-wos.sh` (`tests/e2e/README.md` "AROS").
Verified 2026-11-12, all in emulation: WinUAE presentation test 4/4 scaled and 5/5 window
configurations plus the 4 default ones; game replays title, prologue and save-load on
native+promote and title+prologue in a 16-bit RTG and an AGA 256-colour window; hosted AROS
4 option sets; WarpOS window on AGA 256 and 16-bit RTG, `SCALE=2 ASPECT`, default. Not
verified: anything on the real A1200 (the speed of the new modes, and the A/B of the default
path against 1.2, which the byte-identical objects make a formality), F10 (by hand only).

## Audio

`audio.c` runs one audio task (priority 5) that pulls mono 16-bit blocks from
the sequencer (`xe_audio_render`, the HAL's pull mode) and feeds an output
driver behind the small interface in `audio.h` (`open`, `submit_block`,
`start`, `wait_block`, `close`, all called from the audio task). Two blocks
are in flight; the task has one block duration (93 ms with the default
`AUDIOBUF`) to render the next one. The engine's own sound calls and the
renderer share a `SignalSemaphore`.

**The CPU budget.** `opn_lite` cannot render every song in real time on a
68040/25: measured on the real A1200 (`probes/amiga/README.md` "Probe 3"),
XENON_24 needs 154 % of the CPU at 22 050 Hz and 102 % at 11 025 Hz, XENON_01
(the new game's first song) 128 % and 71 %. A priority-5 task that never
sleeps starves the game, Workbench and every shell: the first new game on
the A1200 hung there (the sequencer runs in the render, so S00's
`MUSIC_WAIT` waited for music that played slower than real time). Hence the
11 025 Hz default, and the budget: each rendered block adds its render time
minus `AUDIOCPU` percent of the block duration to a debt; while in debt a
block is skipped with `xe_audio_skip` (silence, but the sequencer and the
chip registers advance, so music cues stay on time). The audio task's
average share stays within the budget whatever the song; a song that fits
plays unchanged, one that does not plays with gaps. At exit the log gives
the average and peak render load (percent of real time) and the skipped
blocks. On the A1200, a new game's first 5000 ticks at 11 025 Hz: average
46 %, peak 106 %, 447 of 1150 blocks skipped, and the music had gaps.
Since then `opn_lite` skips what cannot be heard and its per-sample loop
is 68040 assembly (`engine/snd/opn_lite_68k.s`, built when `OPNASM=1`, the
default in `config/amiga.mk`; its header and `probes/amiga/README.md`
"Probe 3" have the steps, the profile and the numbers): on the board
XENON_24 fell from 154 % to 65 % at 22 050 Hz and from 102 % to 42 % at
11 025 Hz, XENON_01 from 128 % to 71 % and from 71 % to 37 %, with the
output bit-identical to the C core. The same `play-test new` then gives,
at 11 025 Hz: average 46 %, peak 69 %, **0 blocks skipped** (the music
plays without gaps); at 22 050 Hz (`RATE=22050`): average 51 %, peak
123 %, 385 of 1155 blocks skipped - the average fits but the peaks (the
sequencer, sound effects and the game's own cache traffic come on top of
the bench figures) still exceed the budget, so 11 025 Hz stays the
default on a 68040/25. `RATE=22050 AUDIOCPU=90 AUDIOBUF=4096` does not
rescue it (average 53 %, peak 120 %, 360 of 992 blocks skipped): the heavy
stretch needs 105-115 % of real time in the game against 71 % on the bench,
and the 186 ms blocks starve the main task for 200 ms at a time (the game
dropped to 56 Hz effective). 22 kHz on a 68040 is a nice-to-have;
`probes/amiga/README.md` "After phase C" has the remaining options and
their estimated gains.

* **`paula`** (`audio_paula.c`, default). Channels come from audio.device
  (`ADCMD_ALLOCATE` at priority 100, an `ADCMD_LOCK` request reports theft,
  after which the game goes silent); then the hardware is driven directly: DMA
  from two chip-RAM blocks, the block interrupt of the first allocated channel
  (`SetIntVector` `INTB_AUDn`, written in C with vbcc register parameters, restored on exit)
  re-points the DMA at the other block and signals the task. Both stereo sides
  play the same mono data. 8-bit: one left and one right channel share a
  buffer. `PAULA14`: all four channels, per side one at volume 64 with the top
  byte and one at volume 1 with the next six bits (uncalibrated 14-bit). The
  rate is rounded to a Paula period (11 025 -> 11 015 Hz) and the engine
  re-creates its core at that rate. The LED filter is switched off.
* **`ahi`** (`audio_ahi.c`). `ahi.device` is opened at run time only when
  asked for, so AHI is not a dependency. Device interface: two linked
  `CMD_WRITE` requests (`AHIST_M16S` at the engine rate; AHI resamples and
  mixes to the unit's mode). For a sound card, or for AHI's 14-bit calibrated
  Paula modes.

### AHI hardware mixing and `render_split`

AHI's low-level API (`AHI_AllocAudio`, channels with their own frequency,
volume and panning, `AHIST_DYNAMICSAMPLE` buffers) lets a driver with its own
mixer (a DSP card such as the Delfina, or a card that mixes in hardware) take
several voices and mix them itself; on ordinary cards and on Paula the mixing
happens on the CPU inside ahi.device. To make that possible without changing
the engine, the chip core has an optional second output: `render_split()` in
`engine/snd/opn_core.h` renders the three FM channels and the SSG as four
separate streams (same scale and DC blocking as the mono mix;
`tests/test_opn_split` shows the sum differs from the mono render by at most
5 LSB). An `ahi-ll` driver would allocate four channels, load each stream as a
double-buffered dynamic sample and refill from the `SoundFunc` hook; it is
not written yet - the device-interface driver covers the "sound card" case
and costs AHI one mono channel. Worth doing once someone has a card with a
hardware mixer to test it on. On Paula it would only cost more CPU, except
in AHI 6's "Paula:DMA 8 bit stereo" mode, which plays each AHI channel on
one Paula channel without software mixing - that is the four-voice case, but
with the voices hard-panned (two per side), which is not how the game sounds.

## Timing, input, startup

* `timer.c`: timer.device `UNIT_WAITUNTIL` with absolute deadlines advanced by
  17 724.193 us (no drift); a stall of more than ten ticks resynchronises.
  Measured in WinUAE: 2801 frames in 2821 ticks of E-clock time including the
  start-up. Each stall prints one line saying where the time went since the
  clock was last on time: pack reads (with the time in `Seek`, the first
  and the largest offset read: `tools/xpak.py` maps offsets to entries),
  presents, the rest, and how much of it the audio task (higher priority,
  inside all the other numbers) ran. For example, on the real A1200 from FFS:

      stall at frame 502: 12 ticks late, 2 frames behind in 201 ms: pack 1 reads
      7145 bytes 105 ms (seeks 96 ms; first 0x284d3c, largest 7145 at 0x284d3c),
      present 2 (0 full) 55 ms, other 41 ms; audio task ran 89 ms of it

* **Pack on FFS.** FFS `Seek` walks the file's extension-block chain (one
  block per 36 KB on a 512-byte-block volume), forward from the current
  position or from the start of the file for a backward seek. With the
  default 30 buffers nothing of the 6 MB pack's chain stays cached: a seek
  costs ~25 ms per MB walked, up to 155 ms, and the game hitches on asset
  loads (measured on the real A1200, `probes/amiga/README.md` "Probe 4").
  This is why the pack is preloaded when memory allows ("Memory"). Without
  the preload, `AddBuffers <volume> 200` (~100 KB) brings every seek under
  2.5 ms.
* `input.c`: backdrop borderless window, `IDCMP_RAWKEY` (typematic repeat
  passes through as repeated presses, like the PC-98 BIOS buffer),
  `IDCMP_MOUSEMOVE` with `IDCMP_DELTAMOVE`, `IDCMP_MOUSEBUTTONS` with
  `WFLG_RMBTRAP`, ACTIVE/INACTIVEWINDOW for the pointer.
* `main.c`: works from the shell (`ReadArgs`) and from Workbench (the startup's message,
  icon tool types). The pack is found through the program file's trailer
  (`probes/amiga/mktrailer.py` wraps it in a `HUNK_DEBUG` block so `LoadSeg`
  accepts it), else `PROGDIR:xenon.pak`. Fatal errors close the screen and
  show an `EasyRequest`.

### Voice

The Windows release's voice clips (docs/voice.md) from `xenon_voice.pak`: the engine decodes
them on the main task (`voice_pump`, every frame) and hands the samples to the backend
(`XE_VOICE_CHANNEL`, `audio.c`): a FIFO the audio task drains into two voice blocks, as long
as the music's so a music render never outlasts one, refilled as the driver reports them
played. The engine keeps `voice_lead_ms` (four blocks) queued ahead; a cut silences both
blocks at once. Commands (open, close) go to the audio task by signal, so the drivers keep
their "audio task only" rule.

* **`paula`**: the voice takes the left and right channel the 8-bit music leaves free, a
  second audio.device allocation with its own chip blocks (2 x 2 KB at 22 kHz) and its own
  block interrupt (`INTB_AUDn` of its first channel; the music's too is now taken from its
  allocation instead of assuming channel 0). It plays at the clip's rate (period 161 =
  22 030 Hz for 22 050 Hz; the driver reports 22 050 so the engine copies instead of
  resampling: 0.09 % slow, inaudible), 8-bit, with `AUDxVOL` as its volume and as the music's
  (no bits lost to a software gain). Measured on WinUAE's cycle-exact 68040/25
  (`probes/amiga/voicebench.c`): ldp6 decoding 18 % of the CPU, the copy and 8-bit conversion
  about 5 %. If another program takes the voice channels, the voice falls silent and the game
  goes on. `PAULA14` uses all four channels: the voice is then mixed into the music.
* **`ahi`**: a second linked `CMD_WRITE` pair on the same unit at the clip's rate; ahi.device
  mixes it with the music (on the CPU, or on a card that mixes in hardware) and applies
  `ahir_Volume`. A unit mixes as many streams as its "Channels" preference allows (4
  without preferences); with one, the voice's requests go to AHI's silent list (they still
  complete), which is what `VOICEMIX` is for.
* **Codecs**: the 68k build decodes ldp6 only (Opus costs 229 % of a 68040/25 in WinUAE,
  and adds ~260 KB); WarpOS and AROS also have Opus (`config/warpos.mk`, `config/aros.mk`;
  vbcc needs the `QCONST` change in `third_party/opus/celt/fixed_generic.h`, docs/voice.md
  §5.3). A pack whose codec the build lacks logs that and the game runs without voice.
* **Tested** (2026-11-13) in WinUAE, cycle-exact 68040/25, a new game's first 7000 frames with
  OK every 600 ticks (`run-game.sh native 25`, the voice pack in `GUEST_FILES`, the SDL3 WinUAE
  build for the audio interrupts): Paula and AHI (8-bit stereo 22 kHz, 4 channels) both play
  9 clips, 6 to their end and 3 cut, 277 voice blocks with sound. The music task's load is
  unchanged (Paula 41 %, AHI 45 % average); what the voice costs shows as frame stalls on
  the main task where the heaviest music and a clip's decoding meet: Paula 9 stalls, 116
  ticks lost (1 and 14 without voice), AHI 20 stalls, 259 ticks (1 and 18) - AHI mixes a
  second channel on the CPU. Hosted AROS (Opus, AHI at 44.1 kHz): 5 clips, no stalls, and the
  clips are in its recorded output (cross-correlation 0.74 for RYO0705 against 0.02-0.03 for
  clips not played). WarpOS: `test_voice` built for the PPC decodes Opus within 79-85 dB of
  the host in WinUAE's Blizzard PPC emulation (no audio DMA there, so no output test), and the
  game played Opus voice through AHI on a real Sam460ex (the user's own AmigaOS 3.1 port with
  a WarpOS-compatible powerpc.library, virt68k; its sm502.audio AHI
  driver): 11 clips, 0 underruns, 17 ms per frame; the user listened ("it works"). AHI mixes
  every stream at the unit's frequency (12 kHz on that board's unit 0): below 24 kHz the
  voice loses its top band, so the unit wants 44.1 or 48 kHz. An
  output that makes no progress (that emulator, a stuck driver) ends the clip after 2 s
  (`voice_wait`) instead of holding the game.
* **Disk**: the voice pack (261 MB ldp6) is never preloaded. The decoders read through a
  16 KB read-ahead (`engine/voice/vdec.c`), a read that continues the previous one skips the
  `Seek` (`p_read`), and `mkvoice.py` lays the clips out in the order of play, so playing
  through the story reads forward in short steps (backward seeks between consecutive cues:
  798 with a median of 102 MB before, 57 with a median of 2.5 MB now). A backward seek on FFS
  still walks the file from its start ("Pack on FFS"): the ReadMe recommends PFS3 or SFS.

## Memory

Measured with the `memory:` line the game prints at exit (engine allocations
by where they landed, the largest single block, and the system's free-memory
low-water mark below what was free at start, so screen buffers, audio
buffers and libraries loaded for the game are included). The program itself
(155 KB: 123 KB code, 32 KB BSS) is loaded before that and comes on top.
Heaviest content: route-end7 (all routes, both languages, checked on the
host, `XE_MEMSTAT=1 tests/e2e/run.py`); the real A1200 ran the title, the load
menu and a new game's first 5000 ticks (engine peak 662 KB there).

| | chip RAM | other RAM (fast, or chip if there is none) |
|---|---|---|
| screen (two 640x400x4 buffers) + Paula blocks | 254 KB (+4 KB `PAULA14`) | |
| engine, peak (route-end7) | | 843 KB, largest block 375 KB |
| rest (sound core, I/O, libraries) | | ~37 KB, +105 KB with `AUDIO=ahi` (ahi.device, paula.audio) |
| program | | 155 KB |
| **without preload** | **254 KB** | **~1.04 MB** (1.14 MB with AHI) |
| **with preload**: + the pack, one block, fast RAM only | 254 KB | **~7.1 MB** (7.2 MB with AHI) |

On a graphics card screen (or a promoted native mode) the screen buffers are
card memory: the game then takes next to no chip RAM. Engine allocations ask
for any memory, so without fast RAM everything above goes to chip: about
1.3 MB free chip RAM for a 2 MB machine without the preload (not tested, and
far below the 68040 the game is aimed at). `PRELOAD=auto` loads the pack only
when the largest free fast block holds it and 1.5 MB of fast RAM stays free
(the measured need above is at most 985 KB after the program is loaded):
about 7.5 MB free fast RAM at start, i.e. an 8 MB card with a light
Workbench. On less, the assets are read from the file when needed; on FFS,
run `AddBuffers <volume> 200` first (see the FFS note above) or the game
hitches on every scene change. Loading the pack takes 3.4 s from the A1200's
CF card (1.8 MB/s) before the screen opens; after that no asset load ever
touches the disk.

## Testing in WinUAE

`make TARGET=amiga e2e` runs the presentation conformance test (plan §10,
`tests/e2e/README.md` "Amiga in WinUAE"): a synthetic present sequence and
replays of the end-to-end scenarios, checked by screenshot and by reading the
displayed bitmap from guest memory, on native AGA, the native mode with
Picasso96 loaded, a promoted screen and a uaegfx board mode.

`tests/amiga/run-game.sh [native|rtg|promote] [MHz]` boots the Workbench
tree with the game, runs it with scripted input in real time on an emulated
68040 and takes screenshots through the gdbserver (`t25.png` should show the
title menu with DATA LOAD selected, `t40`/`t55` the load menu); `out.txt`
holds the game's log with the frame-timing and audio statistics. Audio
interrupts need a WinUAE built with the SDL3 backend (the null-video build
disables the audio state machine); see the script header and `tests/amiga/README.md`
(the rig and its machine settings, `tests/rig.conf`).

## Testing on the real A1200

`tests/amiga/a1200.sh install` puts the build on the user's A1200
(`Stuff:xenon`), `a1200.sh play-test load|new` runs a timed autoplay there
(title -> load menu, or a new game into S00) and prints the log; `tests/amiga/README.md` "The real A1200" has the
details and the results.

## WarpOS (PowerPC)

`make TARGET=warpos` (config/warpos.mk) builds this backend for the PowerPC
under WarpOS (WarpUP 5.x, `powerpc.library`): a WarpOS executable (68k
startup + PPC code + a few 68k objects) that runs everything on the PPC
and reaches the OS through amiga.lib's stubs, each a `Run68K` context
switch to the 68k. Toolchain: `vc +warpos` (vbccppc, vasmppc_std, the
WarpUP 5.1 headers; `~/amiga-sdk/README`). Release archive:
`make TARGET=warpos dist` -> `xenon-warpos-<version>.lha`. The facts the
design rests on were checked with `probes/warpos/wosprobe.c`
(`probes/warpos/README.md`).

* **OS structure layout.** vbccppc aligns `int` and pointers to 4 bytes,
  the OS structures are laid out for the 68k (2 bytes), and neither the NDK
  nor the WarpOS headers say so: 51 of 65 sizes/offsets the backend uses
  came out wrong. Every backend file reads its OS headers between
  `core/os_begin.h` and `core/os_end.h` (`#pragma amiga-align` on the PPC,
  nothing elsewhere); the C library and engine headers stay outside (vc.lib
  is built with natural alignment). The WarpOS build compares the layout
  with the 68k compiler's (`warpos/oslayout.h`, `warpos/oslayout68k.c`) at
  startup and refuses to run on a mismatch.
* **68k code.** Three files are compiled by the 68k compiler
  (`M68K_SRCS` in `config/warpos.mk`) and linked into the PPC executable:
  the Paula interrupt handler (`chipset/paula_irq.c`, it runs on the 68k),
  the once-per-frame OS call (`core/frameos.c`, below) and the layout table.
  The 68k build compiles the same files as ordinary code. The handler
  shares its state with the driver through a chip-RAM block split by writer
  (`audio_paula_irq.h`: one cache line written by the handler, the rest by
  the task before the interrupt is enabled).
* **Few 68k calls.** Every OS call from the PPC is a context switch to the
  68k and back, so the per-frame and per-block paths make as few as
  possible, as WarpOS software of the time did:
  * the main task makes **one per frame**: `core/frameos.c` does the frame
    clock's timer wait, the IDCMP gather (copied into `struct am_frameos`,
    `frameos.h`; `input.c` decodes it), the front-screen and Ctrl-C checks
    and the free-memory sample in a single `Run68K` (the 68k build calls the
    same code directly). Palette loads, page flips and RTG blits stay
    separate calls, made only when they happen;
  * the profile clock (`am_prof_now`: stall profile, the audio CPU budget)
    reads the PPC time base (`mftb`, bits 4-35: about 1 MHz) instead of the
    E-clock;
  * the audio task waits with `WaitPPC` and the engine's audio lock is a
    PPC semaphore (`am_wait`, `am_sem_*` in `core/amsys.h`), so a Paula block costs
    no 68k call at all; AHI keeps its one `SendIO` per block.
* **The audio task** is a PPC task (`CreateTaskPPC` with
  `TASKATTR_INHERITR2`: r2 is the TOC pointer, without it the task hangs on
  its first global). Its OS calls run in a 68k mirror process, which is what
  `FindTask(NULL)` returns in it; the main task's `Signal()` and the Paula
  interrupt signal the mirror, and the task's `WaitPPC` receives that
  (signals are shared between a PPC task and its mirror). `Forbid()` on the 68k does not
  hold the PPC scheduler, so at the end the main task waits until the task
  has left WarpOS's task list (`am_task_wait_gone`) instead of relying on
  the 68k build's `Forbid()` before the last signal.
* **Caches.** Where the PPC writes what DMA or the 68k reads, the backend
  flushes the PPC data cache explicitly (`am_cache_flush`/`am_xcpu_flush`:
  sample blocks for Paula and AHI, the chip planes after a CPU upload, the
  engine's pages before a `BltBitMap`); where it reads what the handler
  wrote, it invalidates that cache line first. `Run68K` also does "cache
  management automatically", so some of these may be redundant. None of it
  has been tested on real hardware (the emulator has no caches).
* **System glue** is `warpos/sys_wos.c` behind `core/amsys.h` (the 68k
  build has `exec/sys_exec.c` there): cache flush/invalidate, task
  start/end, waiting and the semaphore, the task's stack (`am_stack_paint`
  paints the PPC stack), the profile clock, the frame call, the layout
  check; `warpos/wos.h` adds 68k calls by address or library offset through
  `Run68K` (`warpos/flavour.c` calls Picasso96 that way for snapshots).
* The 68k build is unchanged in behaviour (the same OS calls, now partly in
  `core/frameos.c`; input is gathered at the end of the frame wait instead of a few
  microseconds later at the poll; `make TARGET=amiga e2e` and the Paula
  block/interrupt counts as before).

Defaults are the 68k build's (RATE 11025, AUDIOCPU 75 %): whether a PPC
plays 22 kHz is unmeasured, the emulator says nothing about real speed.

**Tested in WinUAE's Blizzard PPC emulation only** (`tests/warpos/run-wos.sh`,
`probes/warpos/README.md`): the game starts, the layout check passes, it draws the
opening and the title menu correctly and exits cleanly. Every 68k call from
the PPC costs about one PAL frame (20 ms) in that emulator. A frame took
380-420 ms there before the per-frame calls were batched, 65 ms after, and
20-21 ms once two emulator bugs behind its 259 s stalls were fixed
(`probes/warpos/README.md`). Those runs check function, not speed. Since then
the build has run on a real Sam460ex under the user's own AmigaOS port with a
WarpOS-compatible powerpc.library (`AUDIO=ahi`; the music and, with a voice pack,
the voice: "Voice" above), at the full 56.42 Hz. Not tested: a Blizzard or
CyberStorm PPC board, Paula output from the PPC.

## AROS (x86_64)

`make TARGET=aros` (`config/aros.mk`) builds the backend from `core/`,
`exec/` and `aros/` with the AROS cross gcc for x86_64 ABIv11 (deadwood's
AROS, release 2026.09, the base of AROS One 1.3; toolchain and SDK in
`~/amiga-sdk/README`, `. ~/amiga-sdk/env.sh` first). What the flavour
changes, each found by `probes/aros` (README there):

* sound only through AHI, at 44 100 Hz (opn_lite uses about 1 % of a PC);
* no Amiga display modes: `BestModeID` for 640x400 at depth 8, and the page
  centred on a larger screen (vmwaresvga gives 640x480, VESA its single
  1024x768). Depth-8 screens are true colour underneath, so `LoadRGB32`
  does not recolour drawn pixels and every palette change re-uploads the
  page; the `chunky` upload is the default;
* the frame clock on `UNIT_MICROHZ` (the time left to the same absolute
  deadline): `UNIT_WAITUNTIL` returns in 20 ms steps there;
* preload into `MEMF_ANY` (natively all RAM is `MEMF_CHIP`);
* F11 also works as Help (PC keyboards have none);
* a 256 KB stack whatever the start gave (`aros/stack_aros.c`,
  `NewStackSwap`; hosted AROS's X11 driver runs Xlib on the caller's stack
  and crashed the system with the 64 KB the first icon gave), the audio
  process 256 KB too;
* icons with AROS tool types and a 256 KB stack (`aros/*.info`,
  `mkicon.py --flavour aros`).

Tested in hosted AROS (`tests/aros/aros.sh hosted`, headless) and on the
2026.09 live CD in QEMU/KVM (`aros.sh qemu`, VMware SVGA and VESA, ES1370):
real time, the title and the prologue drawn and played correctly, 0 bytes
leaked. `make TARGET=aros e2e` checks the presentation and replays e2e
scenarios in hosted AROS, the guest reading its screen back for the
snapshots (`aros/snap_aros.c`, `tests/e2e/README.md` "AROS"). `make TARGET=aros dist` -> `xenon-aros-x86_64-<version>.lha`
(named like the other flavours' archives; stripped executable) plus its upload
description `.readme` (`aros/upload.readme`, author fields to fill in);
`tests/aros/run-dist.sh` unpacks it inside AROS with AROS's lha, compares
the files and runs it. Not tested: real hardware, a Workbench start by hand,
the keyboard and mouse by hand.

## Not done / ideas

* `opn_lite` fast enough for a 68040/25 (the budget above only keeps the
  machine alive); `ahi-ll` driver (above); a `SCREENMODE` requester (ASL)
  instead of `MODE=`.
* Pausing when the screen is in the back (tool type), interleaved bitmaps and
  a chip-RAM shadow for faster native uploads, `ScrollVPort` for `G_SCROLL`.
* Bundle: one executable with the pack appended (phase 5).
* Window, scale, aspect: measure on the real A1200 (`video:` present total, default path
  and `ASPECT` on native PAL, a window on an AGA 256-colour Workbench); an automated F10
  test (`probes/amiga/keyprobe` injects raw keys through input.device while the game runs:
  `kXX` steps, `run-game.sh` `PRE_CMD`; probes README "Keyboard input"); `WaitBOVP` before a full window redraw on a native screen (less tearing); the
  activating-click guard for the own screen too (its backdrop window has the same
  behaviour after a click from another screen); `SCALE=auto` as the AROS default; a
  `SetRGB32CM` + one `MakeScreen` instead of 16 `SetRGB32`s per palette change in a window
  on AGA.
