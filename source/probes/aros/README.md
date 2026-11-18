# AROS probes (x86_64, ABIv11)

> These probes were written while the platform layer was this engine's own (before the move to
> lrvne, `source/lrvne`); what they measured is lrvne's Amiga backend now
> (`source/lrvne/backends/amiga`, its README). Paths into the old tree (`engine/snd/opn_lite.c`,
> `config/*.mk`) name files that are in git history only: rebuild such a probe from a checkout
> of that history. The results stay valid as records.

The facts the AROS build (`make TARGET=aros`, lrvne's `mk/target/aros.mk`, the `aros/` flavour of
`lrvne/backends/amiga`, see its README "Layout") rests on. The rig that runs it (`aros.sh`, the
release archive check, the environment) is lrvne's (`../../lrvne/rigs/README.md`); the game's
AROS notes: `../../tests/aros/README.md`.

## arosprobe (hosted, 2026-11-11)

`arosprobe [STARTUP] [TIMER] [SCREEN] [AHI [RATE=hz] [SECONDS=n]]`:

* **startup**: `WBenchMsg` is the startup's symbol (NULL from the shell); `IPTR` is 8 bytes,
  `ULONG` 4, pointers 8; `ReadArgs` takes an `IPTR *` array (`AM_RDARGS_ARRAY` in
  `core/amcompat.h`); the shell's stack is 256 KB; `MEMF_FAST` exists (240 MB of it hosted).
  Library bases opened by hand coexist with the auto-opened ones. `timerequest.tr_time` is a
  `struct timeval32` (same layout as `struct timeval`, another type: assign the fields).
* **timer**: the E-clock runs at **200 Hz** hosted (the profile clock is coarse: the audio
  task's CPU budget had to be computed multiply-first). UNIT_WAITUNTIL at 56.42 Hz: 200 waits
  take 3.550 s for 3.545 s wanted, but each returns 0-20 ms late (average 10 ms): the
  frame rate holds, single frames jitter.
* **screen**: no Amiga modes; `BestModeID(640x400x8)` gives `0x00102400 "X11: 640x400x24"`.
  A depth-8 screen there has a **24-bit bitmap** (`BMA_DEPTH` 24, CGX pixel format 12), not
  BMF_STANDARD; `LoadRGB32` does **not** recolour what is drawn (pen 31 read back white
  after being set to red, red only after a redraw): `video.c` re-uploads the page on every
  palette change when the bitmap is deeper than the screen. Two ScreenBuffers allocate and
  flip. A full 640x400 `WriteChunkyPixels` takes about 0.5 ms (`WritePixelArray8` and cgx
  `WritePixelArray` LUT8 the same within the 5 ms clock). `WaitTOF` = 20 ms (50 Hz).
* **ahi**: `ahi.device` 6.12 unit 0 works without prefs; CMD_WRITE double buffering of 16-bit
  mono plays in real time (300 blocks of 100 ms in 30.005 s), the capture holds the 440 Hz
  tone at the written level.

## Native in QEMU/KVM (2026-11-11)

* **startup**: as hosted, but all 2 GB are `MEMF_CHIP` and `MEMF_FAST` is 0 (the flavour
  preloads into `MEMF_ANY`).
* **timer**: the E-clock is the PIT, 1 193 180 Hz. **UNIT_WAITUNTIL is coarse here too**
  (waits return 0-20 ms late, average 10 ms: 50 Hz steps), **UNIT_MICROHZ is exact**
  (200 waits 3.54485 s for 3.5448 s, average 0.3 ms late, max 0.5 ms). UNIT_WAITECLOCK
  returned at once with E-clock deadlines (not pursued). The AROS flavour therefore runs the
  frame clock on UNIT_MICROHZ: the same absolute deadlines from `GetSysTime`, waited for as
  the time left (`flavour.h` `timer_unit`, `frameos.c`).
* **screen**: vmwaresvga has no 640x400; BestModeID gives `0x00110000 "VMWareSVGA:640x480x24"`
  and the screen opens 640x480; std VGA (vesagfx) has only the boot mode, 1024x768. Both are
  24-bit underneath. `video.c` centres the page on a larger screen. Full-page
  `WriteChunkyPixels` 0.7 ms, `WritePixelArray8` 0.4 ms, cgx LUT8 0.3 ms; ChangeScreenBuffer
  0.3 ms; WaitTOF 20 ms. LoadRGB32 does not recolour drawn pixels (as hosted).
* **the game**: `video: mode 0x00110000, 640x480 depth 8 (true colour underneath), chunky
  upload`; with `AUTOPLAY="at:503=ok"` (a new game; quote the spec) 8195 frames in 8195 ticks
  of real time, 0 stalls, the audio task at 1 % of real time (opn_lite at 44 100 Hz under
  KVM), ES1370 output in the WAV, 0 bytes not freed. Saves fell back to `T:` (the CD and
  ENVARC: are read-only). The title is drawn correctly on vmware and std VGA (centred).

## The game (hosted, 2026-11-11)

`make TARGET=aros` (285 KB) runs: `video: mode 0x00102400, depth 8 (true colour underneath),
chunky upload`, `audio: ahi, 44100 Hz, 4096-frame blocks`; 3001 frames in 3004 ticks of real
time, no stalls, 0 bytes not freed, stack main 5.9 KB / audio 17.9 KB. The opening and the
title menu are drawn correctly (the title menu is silent in the original: the logo has a
short effect, the music starts with a new game). With the prologue autoplay spec
(`tests/e2e` prologue, OK at 503 = GAME START) the prologue runs with its music. Snapshots
need no host answer on AROS: the guest reads its screen back (`make TARGET=aros e2e`,
`tests/e2e/README.md` "AROS").

## Screen switching (QEMU, 2026-11-11)

With the game started after the boot (`Run >NIL: Execute` of a script that waits 20 s, so the
Workbench screen exists) and keys sent through the monitor (`sendkey meta_l-n`, `meta_l-m`;
std VGA, since vmware's screendump goes blank after a mode switch): Left Amiga+N brings the
Workbench screen to the front, Left Amiga+M brings the game back with its picture intact, a
second Left Amiga+M cycles on. AROS's Intuition has the usual screen keys; Left Amiga is the
left Windows key. Not checked: the pointer hiding/showing; hosted AROS under GNOME, which
probably keeps Super+N/M for itself.

## The 64 KB stack crash (2026-11-11)

Started from its icon (stack 64 KB then) the game took hosted AROS down after about four
minutes idling on a page: `[KRN] Task xenon went out of stack limits` with the saved SP a host
address, then `x11hidd task` failures and a trap. Reproduced headless with `Stack 65536` (crash at
6 min; the shell's default 256 KB ran 8 min clean). The game itself peaks at ~7 KB of stack; the
rest is hosted AROS running host code on the calling task's stack (Xlib through the X11 driver,
signal handling), which the X11 driver's own startup already works around ("some X servers need a
larger than normal stack", `arch/all-hosted/hidd/x11/disk_startup.c`). Fix: the AROS flavour swaps
itself onto 256 KB with `NewStackSwap` when started with less (`aros/stack_aros.c`), the icon asks
for 256 KB, the audio process gets 256 KB (it used 17.9 KB hosted, 2.7 KB natively). The same
`Stack 65536` run then went 30 196 frames (9 min) clean. Natively the crash was not seen (no host
code on task stacks there), but the guard applies everywhere on AROS.

## Not done yet

HDA audio; real hardware; keyboard/mouse by hand (F11 as Help
is untested); the e2e stages in QEMU (they run hosted, `tests/e2e/README.md` "AROS").
