# AmigaOS 3.1 probes (WinUAE, headless)

Small programs that de-risk the Xenon port's display, packaging and audio
plans, built with vbcc and run under the Linux WinUAE fork with no video
output.  Results are read back from `DH0:out.txt` and from PNG screenshots
taken through WinUAE's gdbserver (`monitor screenshot`).  The emulator
plumbing they run on, the game's own runs (`run-game.sh`, the release archive,
the real A1200) and the machine settings are the test rig's:
`../../tests/amiga/README.md`.

Files:

| file | what |
|---|---|
| `sbprobe.c`, `vbserver.s` | probe 1: ScreenBuffer double buffering + BltBitMap from a fast-RAM planar bitmap, native and Picasso96 |
| `trailerprobe.c`, `mktrailer.py`, `payload.bin` | probe 2: data appended after the last hunk, CLI and Workbench start |
| `opnbench.c`, `mkopnlog.py` | probe 3: CPU cost of the engine's `opn_lite` YM2203 core on a 68040 (register log of XENON_24, 20 s, generated into `opnbench_log.h`) |
| `iconprobe.c`, `run-icon.sh` | the Workbench icon (`tools/mkicon.py`): reads `xenon.info` with icon.library 40, lists the tool types, shows the image on an 8- or 4-colour Workbench 3.1 screen (`DEPTH=3|2`) and as an AppIcon drawn by Workbench; see "The Workbench icon" |
| `winprobe.c`, `run-win.sh` | the Workbench window (plan: window, scale, aspect): display modes and their `Resolution`, free pens on Workbench screens of each depth, drawing into a partly covered window (`WriteChunkyPixels`, Picasso96 and cybergraphics ARGB), the activating click and `IDCMP_DELTAMOVE` under injected mouse input; see "The Workbench window" |
| `keyprobe.c` | keyboard input injected through `input.device` from a background program while the game runs (`PRE_CMD` of `tests/amiga/run-game.sh`): Ctrl down/up, mouse floods, and the events Poseidon's USB boot keyboard class sends; see "Keyboard input" |
| `voicebench.c`, `mkbenchpak.py`, `run-voicebench.sh` | probe 5: CPU cost and correctness of the voice decoders (ldp6, Opus) on an emulated 68040; see "Probe 5" |
| `Makefile` | `make` (vbcc's `vc` on `PATH`) builds `sbprobe`, `trailerprobe{,.hunk,.raw}`, `seekprobe`, `iconprobe`, `winprobe`, `opnbench`; `make voicebench` the voice probe |
| `run-native.sh`, `run-rtg.sh`, `run-bench.sh` | emulator runs on the rig's `tests/amiga/run-lib.sh` (it needs `make -C ../../tests/amiga` for `uaequit`); work dirs under `$EMU/aos/work/xenon/<name>/` |
| `out-*.txt` | verbatim `DH0:out.txt` of each run |
| `shots/*.png` | screenshots (`<tag>_<n>.png`; n=1 after BltBitMap from fast RAM, 2 after CPU copy / second blit, 3 after BltBitMap from chip RAM, 4 end state) |

Runs: `./run-native.sh` (config 1), `./run-rtg.sh native` (config 2),
`./run-rtg.sh uaegfx` (config 3), `./run-rtg.sh promote` (P96 promotion),
`./run-rtg.sh wb` (Workbench start of trailerprobe), `./run-bench.sh [MHz]`
(opnbench on a 68040).  Each run is bounded by `timeout 300` (900 for the
benchmark).

## Verdicts

1. **ScreenBuffer + BltBitMap from a fast-RAM planar bitmap**
   * Native AGA, *no* Picasso96 loaded: **does not work**.  graphics.library
     BltBitMap() always uses the blitter, which cannot read fast RAM; the
     destination gets garbage (319/324 sampled pixels wrong, 70061/128000
     bytes wrong, see `shots/native_1.png`).  A CPU copy of the planes into
     `sb_BitMap->Planes[]` works (`native_2.png`), and BltBitMap from a
     *chip*-RAM planar bitmap works (`native_3.png`).
   * Native screen (0x8004 / 0x29004) *with* Picasso96's rtg.library loaded:
     **works** (rtg.library patches BltBitMap with CPU fallbacks for
     non-chip sources); the result is also pixel-identical (`rtgnative_*.png`).
   * uaegfx 640x480x8 screen (0x50031000): **works**, P96 converts planar to
     chunky in software (no BlitPlanar2Chunky in the WinUAE log, and
     uaegfx advertised "mode mask 212" without that blit); `rtg_1.png`.
     The CPU plane copy path must not be used there: `Planes[0]` is the
     board's VRAM (0x41000000), `Planes[1]`/`[2]` point at a P96 private
     structure, `Planes[3]` is NULL, `BytesPerRow` is 640 (chunky).
   * So the engine needs: planar back buffer in **chip** RAM when there is
     no RTG (then BltBitMap or a CPU copy both work), and BltBitMap from
     anywhere when the screen is an RTG one.  Deciding by BMF_STANDARD
     (below) covers this: standard => copy/blit into chip planes;
     not standard => BltBitMap only.
2. **BMF_STANDARD as the native-vs-RTG indicator: yes, reliable** in all
   runs.  `GetBitMapAttr(bm, BMA_FLAGS)` was 0xa (STANDARD|DISPLAYABLE)
   for every native screen bitmap and screen buffer (with and without P96
   loaded) and 0x0 for every P96 bitmap, including the P96-promoted
   0x8004 screen ("FakeNative:HighRes-Lace").  BMA_DEPTH is the *requested*
   depth (4) even on the 8-bit chunky uaegfx screen; BMA_WIDTH/HEIGHT are
   correct everywhere.
3. **ChangeScreenBuffer behaves**: never returned 0 in 504 calls across all
   configs, on native and RTG screens; AllocScreenBuffer(…, SB_SCREEN_BITMAP)
   returns the screen's own bitmap, AllocScreenBuffer(…, 0) a fresh one of
   the same geometry.  Message handling: with no ports set it returns
   immediately (20 flips in 2–3 ms, 0 VERTB), the SafeMessage port also
   does not throttle (20 safe messages within 1–2 frames on native, within
   0 frames on RTG), the **DispMessage port throttles to one flip per frame
   on native** (20 flips = 20 VERTB = 400 ms) but **not on RTG/P96** (3 ms,
   0 VERTB).  `WaitTOF()` after each ChangeScreenBuffer gives exactly one
   flip per frame on both.  So: use ChangeScreenBuffer + WaitTOF (or a
   DispMessage port plus WaitTOF for RTG).  Pending Safe/Disp messages must
   be drained before FreeScreenBuffer/CloseScreen (the probe does so and
   closes cleanly).  One P96 quirk: on a *promoted* native mode the content
   blitted into the second buffer before the *first* ChangeScreenBuffer was
   lost (the buffer was verified correct before the flip and all-zero after
   it; `promote_1.png` is black); every later blit+flip worked.  On the real
   uaegfx mode this did not happen.
4. **Appended trailer and finding the executable**
   * Raw bytes appended after the last hunk **break LoadSeg on Kickstart
     3.1**: `C:trailerprobe.raw: bad loadfile hunk` (`shots/cli_1.png`).
     dos.library keeps reading hunk types until EOF, it does not stop at the
     hunk count.  Wrapping payload + trailer in a HUNK_DEBUG (0x3F1) block
     (`mktrailer.py debug`) makes LoadSeg skip it: the program loads and
     finds the "XPKT" trailer as the last 16 bytes of the file.
   * CLI: `GetProgramDir()` + `GetProgramName()` give `dh0:C` +
     `C:trailerprobe`, opened relative to the program directory.
   * Workbench: vbcc's `startup.o` provides `_WBenchMsg` (symbol present in
     vbcc's `targets/m68k-amigaos/lib/startup.o`); started from
     `SYS:WBStartup` by LoadWB, `_WBenchMsg=0x40248076`, `sm_ArgList[0]`
     gives `wa_Lock` = `dh0:WBStartup` and `wa_Name` = `trailerprobe`,
     and the trailer was found.  (`shots/wb_1.png` is the Workbench on
     the uaegfx 800x600 screen; trailerprobe has no window.)

## The Workbench icon (`iconprobe.c`, `run-icon.sh`, 2026-11-08)

`tools/mkicon.py` writes `xenon.info` (now made at build time into
`build/amiga/`, no longer committed as `backends/amiga/xenon.info`): the red XENON
lettering of the last frame of the opening's logo zoom
(`resources/anim/0100B/28.png`) cut out by colour, scaled to 64 px wide
(64x21 with its one-pixel drop shadow), on a transparent background - a
first version kept the ship and the sky behind the letters and was too busy
to read on a 4-colour screen - with the game's options as tool types at
their defaults. One file, two renderings:

* the classic `struct Image` (3 planes, complement highlight) that
  icon.library 37-40 draws: lettering pen 7, shadow pen 1, the rest pen 0 =
  the backdrop. Only pens 0-3 (grey, black, white, blue) are the same on
  every Workbench; pens 4-7 of a **stock 8-colour Workbench 3.1 are red,
  green, blue, orange** (`ee4444 55dd55 0044dd ee9900`, IPrefs without
  Palette prefs - measured here, see below; MagicWB-style setups have dark
  grey, light grey, brown, salmon instead, and a custom screen without
  `SA_Colors` gets the old blue/magenta/cyan/white). So the letters are
  orange on a stock 3.1, salmon under MagicWB, and blue on a 4-colour
  Workbench, which drops the third plane (7 -> 3); the shadow is black
  everywhere.
* an OS 3.5 `FORM ICON` (FACE with the frameless flag + two IMAG chunks,
  run-length coded exactly as icon.library 44's `Encode35`, colour 0
  transparent, the letters in four reds shaded top to bottom, a brighter
  copy for the selected state) appended after the classic data. 3.1 never
  reads past the tool types; icon.library 44+ shows this one. `mkicon.py
  --check FILE.info` decodes any icon (ours or a GlowIcon from elsewhere)
  and `--preview DIR` writes PNGs of what it holds; the writer was validated
  by decoding AmiKit's GlowIcons pixel for pixel with the same code.

`run-icon.sh` (`DEPTH=3` default, or `2`) starts Workbench 3.1 on a HiRes
Laced 640x400 screen of that depth (a `screenmode.prefs` written into the
tree's `Prefs/Env-Archive/Sys/`; its `palette.prefs` is removed so the OS
defaults show) and runs `iconprobe`: `GetDiskObject("xenon")`, every tool
type, `FindToolType` on a few (the parenthesised ones are NULL, `(LANG` is
found - icon.library does no parsing, the parentheses simply change the
key), then a window with the icon 1:1, complemented and enlarged 4x, and an
`AddAppIcon` so that Workbench itself draws it on the backdrop.
Those two runs were saved as `shots/icon_wb8.png` and `shots/icon_wb4.png`, removed since,
as they show the game's lettering. Findings:

* icon.library 40.1 accepts the file: type 3, 64x21, stack 8192, free
  position, 12 tool types, `FindToolType(RATE)` = `11025`.
* the first attempt put the Workbench on the Intuition default pens of a
  *custom* screen (blue/magenta/cyan/white for 4-7) and the second on the
  tree's own Palette prefs; neither is what a stock 3.1 shows. Hence the
  prefs handling above and the pen choice.
* `screenmode.prefs` must go into the tree's existing `Sys/` drawer: a
  second `sys/` next to it is copied into `ENV:` as well and the old file
  wins.
* `drawer.info` (`mkicon.py --drawer`, `build/amiga/drawer.info`) is the same picture
  as a drawer icon: type 2, a `DrawerData` with the system drawers' window
  flags (`0x200127f`, min 96x64) and the six-byte `dd_Flags`/`dd_ViewModes`
  tail (default view) that OS 2.0+ stores after the tool types, no tool
  types. `ICON=../../../build/amiga/drawer.info ./run-icon.sh` checks it the
  same way (icon.library 40 reports type 2, Workbench draws the AppIcon).

## The Workbench window (`winprobe.c`, `run-win.sh`, 2026-11-11)

What a game window on the Workbench screen can count on, for the window,
scale and aspect modes (`backends/amiga/README.md` "Window, scale,
aspect"). `run-win.sh modes native|rtg` lists the display modes;
`run-win.sh wb MODEID DEPTH native|rtg` starts a Workbench 3.1 in that mode
(screenmode.prefs written into the tree, mousehack removed) and runs
`winprobe wb`: it reserves pens, opens a 640x400 window A (smaller where it
does not fit) and a window B over A's right part, draws A full of one
colour three ways and reads the screen back inside and under B, then
injects mouse input through `input.device` (`IND_WRITEEVENT`: an absolute
`IECLASS_NEWPOINTERPOS`, `IECLASS_RAWMOUSE` buttons and relative moves) and
lists what A receives. Verbatim results in `out-win.txt`; `shots/win_rtg16.png`
and `shots/win_aga256.png` show A and B. Timings are the fast non-cycle-exact
68020 config of `run-lib.sh`: compare them with each other only.

| Workbench | free exclusive pens | 640x400 + borders (648x413) | drawing |
|---|---|---|---|
| PAL HiRes 640x256, 8 colours | 0 | no | - |
| PAL HiRes Laced 640x512, 8 colours | 0 | no (640 wide) | - |
| the same, 16 colours | 8 (4-11) | no | pens drawn, clipped |
| the same, 256 colours (AGA) | 16 (236-251) | no | `WriteChunkyPixels` with the pens: ok, clipped |
| uaegfx 800x600, 8-bit | 16 (236-251) | yes | pens: ok, clipped, 25 ms |
| uaegfx 800x600, 16-bit | 16 | yes | pens 146 ms, P96 ARGB 219 ms, cgx ARGB 219 ms: all ok, clipped |
| uaegfx 800x600, 32-bit | 16 | yes | pens 146 ms, P96 ARGB 134 ms, cgx ARGB 134 ms: all ok, clipped |

Verdicts:

* **One drawing path is enough:** `WriteChunkyPixels` of 16 exclusive pens
  into the window's RastPort works on every Workbench that has the pens, from
  AGA 256 colours to 32-bit, and respects the layers (nothing drawn under B).
  On a true colour screen it is as fast as the ARGB calls; a pen's new colour
  does not recolour pixels already drawn there (the custom screen's
  `g_truecolour` rule: redraw after a palette change). The ARGB calls are not
  needed; on a palette screen they do nothing (cgx) or take seconds (P96).
* **Pens decide it:** 8 colours give none, 16 colours give 8 - the window
  needs a 256-colour or graphics card Workbench; anything else falls back to
  the custom screen.
* **A 640-wide Workbench is too narrow** for 640 pixels plus borders (8 more
  across, 13 down). On native AGA that is every standard HiRes Workbench, so
  the window there must go borderless (a 640x400 borderless window fits
  640x512 below the screen bar) or the game falls back to the custom screen.
* **The activating click is delivered:** a click into an inactive window
  gives `ACTIVEWINDOW` and then `MOUSEBUTTONS SELECTDOWN` in the same batch
  (its `MouseX/Y` are garbage under `DELTAMOVE`; only `MOUSEMOVE` carries
  deltas). The game must drop a button press that follows an activation in
  the same gather, or that click is an OK.
* **Deltas keep coming outside the window:** with A active, 100 moves of +20
  sent the pointer out of A and against the screen edge, and A still got 100
  `MOUSEMOVE`s summing to +2000 (also downwards). Intuition queues at most
  `WA_MouseQueue` (5) unreplied moves per window and drops the rest: 100
  moves read only at the end give 5 messages, -100 instead of -2000. The
  game gathers every frame, as it does on its own screen.
* **Pixel aspect (`modes`):** native PAL modes report `Resolution` 22:22 for
  HiRes Laced (640x512), 22:44 for HiRes, 44:44 LowRes - Commodore's
  convention that a PAL HiRes Laced pixel is square. Picasso96 reports each
  mode as if it filled a 4:3 monitor (640x400 22:26, 640x480 22:22, 1280x720
  11:15, 1280x1024 11:10), which is wrong for the wide and 5:4 LCDs these
  modes are used on today. So: native modes take `Resolution`, graphics card
  modes are taken as square pixels.

## Pitfalls hit

* **GetBitMapAttr "garbage"** was a probe bug (missing printf arguments);
  the function is fine (checked with a separate test).
* Mode IDs 0x8004 and 0x29004 are not in the display database when no
  monitor driver (`DEVS:Monitors/PAL`) has been started (plain Kickstart
  boot) but open fine anyway.
* P96 mode names here are `"UAE: 640x 480  8bit"` (no Picasso96Settings
  file), not "uaegfx…"; P96 modes do **not** set DIPF_IS_FOREIGN;
  `BestModeID(…, BIDTAG_DIPFMustHave, DIPF_IS_FOREIGN)` returns INVALID_ID.
  `BestModeID(640x480x8, BIDTAG_MonitorID = id & MONITOR_ID_MASK)` returns
  0x50031000 once the monitor is known.  Identify RTG modes by mode ID
  pattern (0x5xxxxxxx for P96) or, more robustly, by opening the screen and
  checking BMF_STANDARD.  ENVARC:Picasso96/ShowModes (Essential vs All)
  made no difference to the display database (101 modes, 33 RTG).
* The shell holds `DH0:out.txt` open for the `>>` redirect, so a program
  that opens the same file for appending fails ("object in use").
  trailerprobe writes to it itself only when started from Workbench.
* vbcc: `IntuitionBase`/`GfxBase` must be globals of the right struct type
  (`<intuition/intuitionbase.h>`, `<graphics/gfxbase.h>`, `<exec/execbase.h>`
  for `SysBase->LibList`); `DOSBase`/`SysBase` come from `-lamiga` startup.
  vbcc prints a warning for `%d` with UWORD arguments; harmless.
* A VERTB interrupt server cannot be plain C (it must RTS with Z set),
  hence `vbserver.s` assembled with `vasmm68k_mot -Fhunk`.
* `warp=true` makes the guest run as fast as the host allows; all timings
  in the output are guest E-clock/VERTB based, not wall clock.

## Results (verbatim excerpts)

### Config 1: native AGA, no RTG, mode 0x8004 (`out-native.txt`)

```
% C:sbprobe 0x8004 native
sbprobe: intuition 40.85 graphics 40.24 ChipRevBits0=0x13
sbprobe: AvailMem chip=2059048 fast=8765592
sbprobe: no rtg.library
sbprobe: timer.device open, E-clock 709379 Hz
sbprobe: Delay(50) = 1001 ms E-clock, 50 VERTB interrupts
sbprobe: opening screen mode 0x00008004 640x400x4 tag=native
sbprobe: mode 0x00008004 not in display database
source bitmap: InitBitMap 640x400x4 BytesPerRow=80 Planes[0]=0x223fd0 TypeOfMem=0x605 FAST
source: bm=0x21fe3c BMA_FLAGS=0xa STANDARD DISPLAYABLE BMA_DEPTH=4 BMA_WIDTH=640 BMA_HEIGHT=400
screen opened: 640x400, ViewPort modes 0xc004, GetVPModeID=0x00008004, BitMap 0x243414
screen: bm=0x243414 BMA_FLAGS=0xa STANDARD DISPLAYABLE BMA_DEPTH=4 BMA_WIDTH=640 BMA_HEIGHT=400
screen: struct: BytesPerRow=80 Rows=400 Flags=0x0 Depth=4 pad=0x0
screen: Planes[0]=0x28ad0 TypeOfMem=0x703 CHIP
LoadRGB32 done
AllocScreenBuffer: sb[0]=... (SB_SCREEN_BITMAP) sb[1]=... (0)
sb[0]: sb_BitMap=0x243414 (== screen bitmap) ...
sb[0]: bm=0x243414 BMA_FLAGS=0xa STANDARD DISPLAYABLE BMA_DEPTH=4 BMA_WIDTH=640 BMA_HEIGHT=400
sb[1]: sb_BitMap=... (!= screen bitmap) ...
sb[1]: bm=... BMA_FLAGS=0xa STANDARD DISPLAYABLE BMA_DEPTH=4 BMA_WIDTH=640 BMA_HEIGHT=400
sb[1]: struct: BytesPerRow=80 Rows=400 Flags=0x0 Depth=4 pad=0x0
sb[1]: Planes[0]=... TypeOfMem=0x703 CHIP
(a) BltBitMap(FAST source -> sb[1]) done
verify (a) hidden sb[1] before flip: first mismatch at 3,2 got 0 expected 15
verify (a) hidden sb[1] before flip: ReadPixel 319/324 sampled pixels differ -> WRONG
verify (a) hidden sb[1] after 2nd BltBitMap: ReadPixel 319/324 sampled pixels differ -> WRONG
(a) ChangeScreenBuffer(sb[1]) ok=1, zero-returns before success=0
verify (a) BltBitMap from FAST: ReadPixel 319/324 sampled pixels differ -> WRONG
verify (a) BltBitMap from FAST: plane compare 70061/128000 bytes differ -> WRONG
(b) CPU copy of planes into sb[0] (BMF_STANDARD set, dst BytesPerRow=80)
(b) ChangeScreenBuffer(sb[0]) ok=1, zero-returns before success=0
verify (b) CPU plane copy: ReadPixel 0/324 sampled pixels differ -> OK
verify (b) CPU plane copy: plane compare 0/128000 bytes differ -> OK
(c) BltBitMap(CHIP source -> sb[1]) done
(c) ChangeScreenBuffer(sb[1]) ok=1, zero-returns before success=0
verify (c) BltBitMap from CHIP: ReadPixel 0/324 sampled pixels differ -> OK
verify (c) BltBitMap from CHIP: plane compare 0/128000 bytes differ -> OK
flip test (no ports): 20 flips, 0 returned 0 on first call, 0 total zero-returns, 2 ms, 0 VERTB
flip test (SafeMessage port): 20 flips, 0 returned 0 on first call, 0 total zero-returns, 20 safe messages, 16 ms, 1 VERTB
flip test (DispMessage port): 20 flips, 0 returned 0 on first call, 0 total zero-returns, 20 disp messages, 400 ms, 20 VERTB
flip test (WaitTOF after each): 20 flips, 0 returned 0 on first call, 0 total zero-returns, 400 ms, 20 VERTB
verify end state: ReadPixel 0/324 sampled pixels differ -> OK
FreeScreenBuffer done
CloseScreen done
```

Mode 0x29004 (PAL HiRes Laced, tag `nativepal`): identical results
(GetVPModeID=0x00029004, ViewPort modes 0xd004).

Screenshots: `native_1.png` (garbage from the blitter reading fast RAM),
`native_2.png` (yellow rectangle, stripes, pink frame: CPU copy),
`native_3.png` (green rectangle: BltBitMap from chip), `native_4.png`.

### Config 2: RTG config, P96 loaded, probe on 0x8004 / 0x29004 (`out-rtg-native.txt`)

```
% Version
Kickstart 40.68, Workbench 40.42
% C:sbprobe 0x8004 rtgnative
sbprobe: rtg.library (Picasso96) is loaded
sbprobe: mode name "PAL:High Res Laced"
source bitmap: InitBitMap 640x400x4 BytesPerRow=80 Planes[0]=0x400788c8 TypeOfMem=0x405 FAST
screen opened: 640x400, ViewPort modes 0xc004, GetVPModeID=0x00008004, BitMap 0x4001be7c
screen: bm=0x4001be7c BMA_FLAGS=0xa STANDARD DISPLAYABLE BMA_DEPTH=4 BMA_WIDTH=640 BMA_HEIGHT=400
sb[0]: bm=0x4001be7c BMA_FLAGS=0xa STANDARD DISPLAYABLE BMA_DEPTH=4 BMA_WIDTH=640 BMA_HEIGHT=400
sb[1]: bm=0x4002ec3c BMA_FLAGS=0xa STANDARD DISPLAYABLE BMA_DEPTH=4 BMA_WIDTH=640 BMA_HEIGHT=400
verify (a) hidden sb[1] before flip: ReadPixel 0/324 sampled pixels differ -> OK
verify (a) BltBitMap from FAST: ReadPixel 0/324 sampled pixels differ -> OK
verify (a) BltBitMap from FAST: plane compare 0/128000 bytes differ -> OK
verify (b) CPU plane copy: ReadPixel 0/324 sampled pixels differ -> OK
verify (b) CPU plane copy: plane compare 0/128000 bytes differ -> OK
verify (c) BltBitMap from CHIP: ReadPixel 0/324 sampled pixels differ -> OK
verify (c) BltBitMap from CHIP: plane compare 0/128000 bytes differ -> OK
flip test (no ports): 20 flips, 0 returned 0 on first call, 0 total zero-returns, 2 ms, 0 VERTB
flip test (SafeMessage port): 20 flips, 0 returned 0 on first call, 0 total zero-returns, 20 safe messages, 33 ms, 2 VERTB
flip test (DispMessage port): 20 flips, 0 returned 0 on first call, 0 total zero-returns, 20 disp messages, 400 ms, 20 VERTB
flip test (WaitTOF after each): 20 flips, 0 returned 0 on first call, 0 total zero-returns, 400 ms, 20 VERTB
verify end state: ReadPixel 0/324 sampled pixels differ -> OK
```

Same for 0x29004 (`rtgpal`).  Screenshots `rtgnative_*.png`, `rtgpal_*.png`
(all correct, the orange/yellow/green rectangles).

### Config 3: RTG config, probe on the uaegfx 640x480x8 mode (`out-rtg-uaegfx.txt`)

```
% C:sbprobe uaegfx rtg
  mode 0x50031000 "UAE: 640x 480  8bit" nominal 640x480 maxdepth 8 propflags 0x00104380 RTG
  mode 0x50031100 "UAE: 640x 480 16bit PC" nominal 640x480 maxdepth 16 propflags 0x00104380 RTG
  mode 0x50031303 "UAE: 640x 480 32bit BGRA" nominal 640x480 maxdepth 24 propflags 0x00104380 RTG
display database: 101 modes, 33 RTG; chosen 640x480x8: 0x50031000
BestModeID(640x480x8, MonitorID 0x50031000) = 0x50031000
BestModeID(640x480x8, DIPFMustHave FOREIGN) = 0xffffffff
sbprobe: opening screen mode 0x50031000 640x480x4 tag=rtg
sbprobe: mode name "UAE: 640x 480  8bit"
source bitmap: InitBitMap 640x480x4 BytesPerRow=80 Planes[0]=0x40078b78 TypeOfMem=0x405 FAST
screen opened: 640x480, ViewPort modes 0x5000, GetVPModeID=0x50031000, BitMap 0x40022aa4
screen: bm=0x40022aa4 BMA_FLAGS=0x0 !STANDARD BMA_DEPTH=4 BMA_WIDTH=640 BMA_HEIGHT=480
screen: struct: BytesPerRow=640 Rows=480 Flags=0x0 Depth=4 pad=0xf8c1
screen: Planes[0]=0x41000000 TypeOfMem=0x0 (not RAM)
screen: Planes[1]=0x4009e37c TypeOfMem=0x405 FAST
screen: Planes[2]=0x4009e37c TypeOfMem=0x405 FAST
screen: Planes[3]=0 TypeOfMem=0x0 (not RAM)
LoadRGB32 done
AllocScreenBuffer: sb[0]=0x4001d49c (SB_SCREEN_BITMAP) sb[1]=0x4001d4ac (0)
sb[0]: sb_BitMap=0x40022aa4 (== screen bitmap) ...
sb[0]: bm=0x40022aa4 BMA_FLAGS=0x0 !STANDARD BMA_DEPTH=4 BMA_WIDTH=640 BMA_HEIGHT=480
sb[1]: bm=0x4002eca4 BMA_FLAGS=0x0 !STANDARD BMA_DEPTH=4 BMA_WIDTH=640 BMA_HEIGHT=480
sb[1]: Planes[0]=0x4104b010 TypeOfMem=0x0 (not RAM)
(a) BltBitMap(FAST source -> sb[1]) done
verify (a) hidden sb[1] before flip: ReadPixel 0/378 sampled pixels differ -> OK
(a) ChangeScreenBuffer(sb[1]) ok=1, zero-returns before success=0
verify (a) BltBitMap from FAST: ReadPixel 0/378 sampled pixels differ -> OK
(b) sb[0] is NOT BMF_STANDARD: skipped CPU plane copy, used BltBitMap from FAST instead
(b) ChangeScreenBuffer(sb[0]) ok=1, zero-returns before success=0
verify (b) BltBitMap from FAST: ReadPixel 0/378 sampled pixels differ -> OK
(c) BltBitMap(CHIP source -> sb[1]) done
(c) ChangeScreenBuffer(sb[1]) ok=1, zero-returns before success=0
verify (c) BltBitMap from CHIP: ReadPixel 0/378 sampled pixels differ -> OK
flip test (no ports): 20 flips, 0 returned 0 on first call, 0 total zero-returns, 3 ms, 0 VERTB
flip test (SafeMessage port): 20 flips, 0 returned 0 on first call, 0 total zero-returns, 20 safe messages, 3 ms, 0 VERTB
flip test (DispMessage port): 20 flips, 0 returned 0 on first call, 0 total zero-returns, 20 disp messages, 3 ms, 0 VERTB
flip test (WaitTOF after each): 20 flips, 0 returned 0 on first call, 0 total zero-returns, 337 ms, 20 VERTB
verify end state: ReadPixel 0/378 sampled pixels differ -> OK
FreeScreenBuffer done
CloseScreen done
```

Screenshots `rtg_1.png` (orange), `rtg_2.png` (yellow), `rtg_3.png`,
`rtg_4.png` (green), all 640x480 and correct.  WinUAE log: `Unix RTG SetGC:
640x480x8`, 84 `SetPanning` lines alternating between `addr=41000000` and
`addr=4104B010` (one per ChangeScreenBuffer).

### P96 promotion of 0x8004 (`out-rtg-promote.txt`)

```
% C:sbprobe 0x8004 promote
sbprobe: opening screen mode 0x00008004 640x400x4 tag=promote
sbprobe: mode name "FakeNative:HighRes-Lace"
screen opened: 640x400, ViewPort modes 0xc004, GetVPModeID=0x00008004, BitMap 0x4001bf2c
screen: bm=0x4001bf2c BMA_FLAGS=0x0 !STANDARD BMA_DEPTH=4 BMA_WIDTH=640 BMA_HEIGHT=400
screen: Planes[0]=0x41000000 TypeOfMem=0x0 (not RAM)
sb[1]: Planes[0]=0x4104b010 TypeOfMem=0x0 (not RAM)
(a) BltBitMap(FAST source -> sb[1]) done
verify (a) hidden sb[1] before flip: ReadPixel 0/324 sampled pixels differ -> OK
(a) ChangeScreenBuffer(sb[1]) ok=1, zero-returns before success=0
verify (a) BltBitMap from FAST: first mismatch at 3,2 got 0 expected 15
verify (a) BltBitMap from FAST: ReadPixel 319/324 sampled pixels differ -> WRONG
(b) sb[0] is NOT BMF_STANDARD: skipped CPU plane copy, used BltBitMap from FAST instead
verify (b) BltBitMap from FAST: ReadPixel 0/324 sampled pixels differ -> OK
verify (c) BltBitMap from CHIP: ReadPixel 0/324 sampled pixels differ -> OK
flip test (DispMessage port): 20 flips, ... 20 disp messages, 3 ms, 0 VERTB
flip test (WaitTOF after each): 20 flips, ... 337 ms, 20 VERTB
verify end state: ReadPixel 0/324 sampled pixels differ -> OK
```

Reproduced three times: the first flip on a promoted screen shows (and
leaves) a blank buffer, everything after is fine.  WinUAE log `Unix RTG
SetGC: 640x480x8` (the fake native mode is a 640x480 board mode).

### Probe 2 (`out-native.txt`, `out-rtg-wb.txt`, `shots/cli_1.png`)

```
% C:trailerprobe.hunk
trailerprobe: started from CLI (_WBenchMsg=0 argc=1)
CLI: GetProgramName="C:trailerprobe.hunk" argv[0]="C:trailerprobe.hunk"
CLI: GetProgramDir=0x86861 "dh0:C"
file "C:trailerprobe.hunk" size 9572
trailer read 16 bytes: 00 00 03 f2 -> NO TRAILER, payload size 3942842368
% C:trailerprobe.raw
                                  <- nothing: console shows "C:trailerprobe.raw: bad loadfile hunk"
% C:trailerprobe
trailerprobe: started from CLI (_WBenchMsg=0 argc=1)
CLI: GetProgramName="C:trailerprobe" argv[0]="C:trailerprobe"
CLI: GetProgramDir=0x86867 "dh0:C"
file "C:trailerprobe" size 13632
trailer read 16 bytes: 58 50 4b 54 -> XPKT FOUND, payload size 4036
payload at offset 9580, first 16 bytes: 58 45 4e 4f 4e 2d 50 41 59 4c 4f 41 44 2d 54 45  "XENON-PAYLOAD-TE"
```

Workbench start (`run-rtg.sh wb`: trailerprobe + a tool icon copied from
Utilities/Clock.info with tool type DONOTWAIT in SYS:WBStartup, then
`C:LoadWB`):

```
trailerprobe: started from WORKBENCH (_WBenchMsg=0x40248076 argc=0)
WB: sm_NumArgs=1 wa_Lock=0x10006ad1 wa_Name="trailerprobe"
WB: wa_Lock dir = "dh0:WBStartup"
file "trailerprobe" size 13632
trailer read 16 bytes: 58 50 4b 54 -> XPKT FOUND, payload size 4036
payload at offset 9580, first 16 bytes: 58 45 4e 4f 4e 2d 50 41 59 4c 4f 41 44 2d 54 45  "XENON-PAYLOAD-TE"
```

File layout produced by `mktrailer.py debug`: `[hunks][0x000003F1][n
longwords][payload, zero-padded to 4][XPKT][u32 LE size][8 zero bytes]`;
the trailer is still the last 16 bytes, so a reader only needs the file
size.  (`Why` after each run reported no failure; the shell's error for
the raw variant is only on the console.)

## Probe 3: `opn_lite` on a 68040 (phase 3)

`opnbench` links the engine's `engine/snd/opn_lite.c` as built for the game
(`-cpu=68040 -O2 -speed`), replays 20 s of XENON_24 (the busiest song:
516 key-ons, 5556 register writes) at several output rates and times each
run with the E clock.  `run-bench.sh` boots the Workbench tree with
`SetPatch` (68040.library, caches on) on WinUAE's 68040 model with
`cpu_cycle_exact=true cpu_memory_cycle_exact=true cpu_frequency=25000000`
("~cycle-exact" in the log).  `out-bench.txt`:

```
calibration: 10 M x (add, xor, shift, add, shift, add, loop) in 10.638 s; 10 M x (mul, add, loop) in 4.964 s
rate 22050 os 1:  441000 samples =    20.000 s audio in    29.445 s cpu -> 147.2% of the CPU
rate 22050 os 2:  441000 samples =    20.000 s audio in    46.560 s cpu -> 232.8% of the CPU
rate 11025 os 1:  220500 samples =    20.000 s audio in    19.498 s cpu ->  97.4% of the CPU
rate 11025 os 2:  220500 samples =    20.000 s audio in    28.115 s cpu -> 140.5% of the CPU
rate 27500 os 1:  550000 samples =    20.000 s audio in    34.335 s cpu -> 171.6% of the CPU
rate 44100 os 1:  882000 samples =    20.000 s audio in    49.219 s cpu -> 246.0% of the CPU
```

**How much to trust this.** WinUAE's 68040 timing is approximate, and its
own calibration numbers say it is pessimistic: the multiply-free loop
(about 7 instructions per iteration) runs at 3.8 cycles per instruction at
the configured 25 MHz, where a real 68040 executes such register
instructions in about one cycle each, while the `mulu.l` loop comes out
*faster* per instruction than the simple one (a real 68040 multiply costs
16-20 cycles).  An instruction count is a firmer basis: the same core
compiled with `m68k-linux-gnu-gcc -O2 -m68040` and traced under
`qemu-m68k -d exec,in_asm` (`source/build/prof/`, not committed; the trace
of 40 000 samples is ~0.5 GB) executes, for the same XENON_24 log:

| rate / mode | instructions per sample | per second |
|---|---|---|
| 22 050 Hz | 390 | 8.6 M |
| 22 050 Hz, 2x FM oversampling | 603 | 13.3 M |
| 11 025 Hz | 541 | 6.0 M |
| 22 050 Hz, XENON_29 (SSG-heavy) | 345 | 7.6 M |

(Before the phase-3 optimisation pass - per-operator envelope scheduling,
cached attenuation, per-algorithm operator wiring, SSG shortcuts - it was
821 per sample at 22 050 Hz.)  At a realistic 1.3-1.5 cycles per
instruction for cache-resident 68040 code, 22 050 Hz costs 45-55 % of a
68040/25 and 11 025 Hz about 30 %; vbcc's code is somewhat longer than
gcc's.  The verdict for the Amiga backend: 22 050 Hz without oversampling
is the target setting on a 68040/25, 11 025 Hz the fallback (a ToolType),
2x oversampling is for 68060 machines; the decision is taken with the real
backend running (phase 4), and the emulator figure is to be rechecked on
hardware.

Pitfalls: vbcc's `double` support needs an IEEE math library that is not
linked by default (`__ieeefltud`, `_MathIeeeDoubBasBase`), so the benchmark
prints with integer arithmetic only; `ReadEClock` needs `TimerBase` set
from the opened timer.device (`proto/timer.h` declares it).

### On the real A1200 (2026-11-08): the verdict above was wrong

`make opnbench SONG=XENON_nn` builds `opnbench-XENON_nn` (20 s of that
song's log; the Makefile keeps each song's log in `bench-<song>/`: before,
`opnbench.c` included `"opnbench_log.h"` and silently used XENON_24's log
from this directory whatever `-I` said). On the board
(`a1200.sh install opnbench-XENON_24`, then `a1200.sh sh "cd Stuff:xenon"
opnbench-XENON_24`):

```
calibration: 10 M x (add, xor, shift, add, shift, add, loop) in 6.919 s; 10 M x (mul, add, loop) in 11.382 s
XENON_24  rate 22050 os 1: 154.2 %   os 2: 247.6 %   11025 os 1: 102.1 %   os 2: 149.0 %   27500: 179.7 %   44100: 257.8 %
XENON_01  rate 22050 os 1: 127.8 %   os 2: 221.1 %   11025 os 1:  71.2 %   os 2: 118.2 %   27500: 155.6 %   44100: 240.4 %
```

WinUAE's cycle-exact 68040/25 (147 % for XENON_24 at 22 050 Hz) was within
5 % of the real machine for this code; the instruction-count estimate
(45-55 %) was about 3x too low. The real 68040 is faster than WinUAE on the
simple loop (6.9 s vs 10.6 s) and slower on `mulu.l` (11.4 s vs 5.0 s): the
core's multiplies cost more than the emulator charges, its other code less.
22 050 Hz is out of reach for now; 11 025 Hz fits XENON_01 but not
XENON_24, and the game needs CPU too.

### Where the time goes: `opnprof` (PC sampling on the board)

Goal: the busiest song at 11 025 Hz within ~60 % of a 68040/25 (XENON_24
needs 1.7x), better 22 050 Hz within ~60 % (2.6x), without losing what
`tests/test_opn*` and `check_pmd` hold the core to (`make TARGET=host test`).

`make opnprof-XENON_nn` builds the opnbench replay with a sampling profiler
around it (`opnprof.c`, `ciaprof.s`): a CIA timer interrupt (CIA-A timer B,
else CIA-B timer B, else the VERTB chain at 50 Hz) finds the interrupted
exception frame on the supervisor stack by its format/vector word
(`$0068`/`$006C`/`$0078` followed by a plausible SR) and counts the PC in a
histogram over the program's code hunk. The core is compiled to assembly
with `-Dstatic=` so its functions have names (the code is identical to the
static build except `cache_op`, +20 bytes) and assembled with a listing;
`symprof.py bench-<song>/opn_lite_prof.lst <dump>` prints the per-function
table, the hottest instructions and the annotated listing of the hottest
functions. `./run-prof.sh SONG RATE OS` runs it in WinUAE (there CIA-A/B
timer B are taken, so 50 Hz: about 1 000 samples per 20 s); on the board
(`a1200.sh install opnprof-XENON_24`, then `opnprof-XENON_24 11025 1
prof.txt 3` from `Stuff:xenon`, the last argument replays the log that
many times) CIA-B timer B is free: 1 000 samples/s, 1 % unresolved.
WinUAE's distribution matches the board's within 2 %.

Board, XENON_24, 2026-11-08 (`out-prof-a1200-*.txt`; 105 % and 159 % of the
CPU in these longer runs):

| function | 11 025 Hz | 22 050 Hz |
|---|---|---|
| `chan_output` (operators, algorithms) | 32.1 % | 42.3 % |
| `clock_env` | 21.1 % | 14.2 % |
| `step_envelopes` (polls 12 operators' `next` per EG clock) | 20.9 % | 14.8 % |
| `ssg_output` (the song never uses the SSG) | 9.4 % | 12.1 % |
| `lite_render` (loop, mix, DC blocker) | 8.7 % | 11.5 % |
| `update_att` + `chan_check_active` | 6.7 % | 4.3 % |

Cost model from the two rates: cycles/s = 1 216 x rate + 12.9 M; the
rate-independent part is the envelope generator (18 489 clocks/s, ~700
cycles each), half the CPU at 11 025 Hz. XENON_01: 1 292 x rate + 3.5 M.
Host counters on the same log explained it: 2.58 `clock_env` calls per EG
clock, 78 % of them for released operators already at 0x3ff (RR 15 gives
`next = egc + 1` for ever while an RR 0 operator keeps the channel active);
the polling loop alone costs ~190 cycles per clock (14 % of the CPU, even in
XENON_01 where almost nothing is due); 21 % of the channel renders have
every carrier quiet (output 0) with op1 still sounding; `ssg_output` does
its envelope/noise bookkeeping every sample with the mixer at 0x3F. vbcc
inlines nothing (each operator pass is a `jsr` + `movem` of 7-11
registers), spills inside `chan_output`, and the per-sample path has one
`muls.l` (20 clocks on a real 68040, DC blocker).

The three steps, measured on the board (every one leaves all 81 VGMs
bit-identical on the host; the 68k build is checked against the host with
`opnbench sum` / `opnsum`, below):

| step | XENON_24 22 050 / 11 025 Hz | XENON_01 22 050 / 11 025 Hz |
|---|---|---|
| before | 154 % / 102 % | 128 % / 71 % |
| A: skip what cannot be heard (parked released operators, per-channel `next`, quiet channels compute op1 only, SSG early-out) | 103 % / 62 % | 109 % / 59 % |
| B: one envelope deadline for the core, inlined EG/SSG entry checks | 93 % / 56 % | 100 % / 52 % |
| C: the per-sample loop in 68040 assembly (`engine/snd/opn_lite_68k.s`) | 65 % / 42 % | 71 % / 37 % |

XENON_01 has almost no envelope work (0.02 visits per clock), so A and B
barely moved it; C is where the per-sample operator path got its 1.4x. The
2x-oversampled path (`OPN=lite2`) stays in C and is still 68060 territory
(171 % / 96 %). vbcc's `-O3` made no difference (103.8 % vs 103.3 %).

**Bit-exactness of the 68k build.** `opnbench-<song> sum` prints a hash of
every rendered sample per configuration; `make opnsum-<song>` builds the
same replay with the host compiler, `./opnsum-<song>` prints the same six
hashes. `./run-bench.sh 25 sum` runs the Amiga side in WinUAE (both
XENON_24 and XENON_01 match on WinUAE and on the board). The first
assembly version failed this check (op4 was computed into the macro's own
scratch register); the fixed one matches.

### After phase C: what is left, and is more assembly worth it? (2026-11-08)

Board profile of the phase-C build (`bench-XENON_24/a1200-profC-*.txt`;
`symprof.py ... -a opn_lite_68k.lst` now heads each block of the assembly
loop with its share):

| where | 22 050 Hz | 11 025 Hz |
|---|---|---|
| `render_68k`, operator maths (`.chan`, the algorithms) | 55 % | 43 % |
| `render_68k`, per-sample overhead (EG check, channel dispatch, SSG park, DC blocker, clamp) | 14 % | 11 % |
| `render_68k`, EG clock stepping (`.eg`) | 4 % | 4 % |
| C envelope code: `eg_due` 9.5 / `clock_env` 7.7 / `chan_update_flags` 5.4 / `update_att` 1.3 | 24 % | 37 % |

Cost model: the assembly loop ~550 cycles per output sample, the C envelope
code ~4 M cycles/s at any rate (the EG clock is 18 489 Hz whatever the
output rate: 16 % of the CPU). Host event counts for XENON_24 (a copy of
`opn_lite.c` with counters, replaying `bench-XENON_24/opnbench_log.h`
through `opnsum.c`): 4 485 `eg_due` calls/s, 6 369 channel visits/s,
10 548 `clock_env` calls/s, 6 442 `chan_update_flags` calls/s; 2.47
`chan_output` per sample of which 0.52 quiet; 7.6 operator evaluations per
sample; algorithms 4 (71 %) and 5 (29 %), feedback always on. XENON_01:
algorithms 2 (44 %) and 4 (56 %), the SSG on for 52 % of the samples,
almost no envelope work. That makes ~870 cycles per `eg_due` call for work
a hand-written routine does in ~250, while the operator path measures 166
cycles per `chan_output` against ~150 counted from the 68040 timings: the
operator loop is at its instruction floor (log-sine lookup, power lookup,
variable shift, sign; two indexed loads and a register shift per operator).

Estimated next steps (XENON_24 bench, now 65 % / 42 %):

| step | gain | result |
|---|---|---|
| D: `eg_due` + `clock_env` + `chan_update_flags` + `update_att` in asm (bit-exact, `opnbench sum` checks it) | -10 points at both rates | ~55 % / ~32 % |
| E: inline the three channels (no `bsr`/`rts`, no jump table), interleave the independent operators of algorithms 2/4/5 to hide load-use stalls, EG accumulator in a register | -6 / -3 | ~49 % / ~29 % |
| F: DC blocker constant to a power of two (k = 840 / 1 681 now; 1 024 = 27 Hz at 11 025 Hz) replaces the `muls.l` by a shift; changes the output, so the before/after render comparison is lost | -2 / -1 | ~47 % / ~28 % |

**The game, not the bench, decides 22 kHz.** `a1200.sh play-test new
RATE=22050 AUDIOCPU=90 AUDIOBUF=4096` on the board (the zero-code attempt at
gapless 22 kHz): 992 blocks (the count includes skipped ones), average 53 %,
peak 120 %, 360 skipped - no better than 75 % / 2048 (51 %, 123 %, 385 of
1155). The timer log shows why: in the heavy stretch (S00, the xenon_01
song) every 186 ms block took ~200 ms to render, i.e. the renderer needs
105-115 % of real time **in the game** where `opnbench` charges XENON_01
71 %. A budget cannot help a stretch over 100 %, and the 186 ms blocks made
the audio task hog the CPU for 200 ms at a time: the main task was woken
~200 ms late every nine frames (361 stalls, 4 463 ticks lost, 5 913 frames
in 10 617 ticks). So `AUDIOBUF=4096` is worse for play, and D+E+F together
(~47 % bench) would land the in-game stretch around 70-80 %: marginal.

Decision: 11 025 Hz stays the default; 22 kHz on a 68040/25 is nice to
have, not a requirement, and no more assembly is planned for it. If it is
picked up again, measure the in-game 1.5x first (candidates: the SFX
channels and SSG the bench logs never exercise, the sequencer inside the
render, the audio lock held by the main task), e.g. by tracing the OPN
register writes of a `play-test new` and replaying them through `opnbench`
and `opnprof`; only if the extra load is inside `opn_lite` is step D worth
its day.

## Probe 4: FFS `Seek` cost on the real A1200 (`seekprobe.c`)

Why: the game's first runs on the real A1200 (ACA1240 68040/25, SanDisk CF on
Gayle, pack on `Stuff:` = FFS `DOS\3`, 512-byte blocks, default 30 buffers)
had two stalls per run that WinUAE (host directory as disk) never showed. The
stall log (`backends/amiga/timer.c`) put them in pack reads, and in those in
`Seek`, not in the transfer:

    stall at frame 1: 74 ticks late, 1 frames behind in 1318 ms: pack 9 reads 258715 bytes
      804 ms (seeks 636 ms; first 0x5ee5a1, largest 233864 at 0x1bce60), present 1 (1 full)
      19 ms, other 493 ms; audio task ran 82 ms of it
    stall at frame 502: 12 ticks late, 2 frames behind in 201 ms: pack 1 reads 7145 bytes
      105 ms (seeks 96 ms; first 0x284d3c, largest 7145 at 0x284d3c), present 2 (0 full)
      55 ms, other 41 ms; audio task ran 89 ms of it

(`0x5ee5a1` = the trailer check, `0x1bce60` = `font16`, `0x284d3c` = `MENU1`, the
title menu; with `PAK=RAM:xenon.pak` only frame 1 stalls, 27 ticks, 48 ms in pack
reads.) `seekprobe xenon.pak` on `Stuff:`, microseconds, 30 buffers:

    A: from 0 to X           X=0.78 MB 18908, 1.55 MB 18872, 2.33 MB 57705, 3.11 MB 76399,
                             3.89 MB 95857, 4.66 MB 114789, 5.44 MB 134889, 6.22 MB 154508
       second pass           the same within 1 %
    B: forward chain, steps of 0.78 MB from the previous offset: 19783..20715 each
    C: +64 KB OFFSET_CURRENT at 4.66 MB: 1535..3133
    D: from 5.44 MB back 64 KB: 132400; back 1 MB: 107599
    R: 512-byte Read after the seek: 3105..4439

Same with `AddBuffers Stuff: 200` (230 buffers; removed again afterwards with
`AddBuffers Stuff: -200`):

    A: 528, 940, 875, 1064, 1222, 1444, 1647, 2331 (second pass 525 .. 1800)
    B: 456..1311   C: 351..838   D: 1653, 1394   R: 274..2402

Verdict: FFS walks the extension-block chain on every `Seek` (from the current
position when seeking forward, from the file header when seeking backward),
about 25 ms per MB walked, and 30 buffers cannot hold the chain of a 6 MB file
(~172 extension blocks), so nothing is ever cached. With enough buffers for the
chain every seek is under 2.5 ms wherever it goes. Transfers themselves run at
~1.4 MB/s (`font16`, 233 KB in ~170 ms).

## Probe 5: the voice decoders on a 68040 (`voicebench.c`, 2026-11-13)

`mkbenchpak.py TEST_VOICE OUTDIR SECONDS PACK...` cuts the same clips (spread over all
speakers) out of a full ldp6 and a full Opus voice pack and prints the hash the host's
decoders give; `voicebench PACK [SECONDS [DUMP]]` loads a pack into RAM, decodes its clips as
`voice.c` does (reads of 512 samples) and times it with the E clock, then times the Paula
output step (linear resampling 22 050 -> 22 030 Hz and 8-bit conversion); DUMP writes the
first five clips as raw s16 BE. `run-voicebench.sh [MHz [CPU [SECONDS]]]` (BIN=, PAKS=) runs it
like `run-bench.sh`; `SETPATCH=0` for a 68060 (the 3.1 SetPatch loads 68040.library), `FAST=1`
for a functional run. Built from the engine's `pack.c`, `vdec*.c` and `third_party/opus` with
`vc +aos68k -c99 -cpu=68040 -O2 -speed` (libopus: `-DFIXED_POINT -DDISABLE_FLOAT_API
-DNONTHREADSAFE_PSEUDOSTACK`).

WinUAE cycle-exact 68040/25, 20 s of audio (5 clips):

```
ldp6 22050 Hz: decode  3626 ms -> 18.0 % of the CPU in real time; paula step 4.8 %; bit-exact
opus 24000 Hz: decode 45969 ms -> 229.1 %
```

The 68060 at 50 MHz printed the 68040/25's numbers unchanged: WinUAE does not model it here.
Opus adds ~260 KB to the program. Hence ldp6 only in the 68k build (docs/voice.md §5.3).

**vbcc 0.9h miscompiled libopus** (13 % of the samples wrong, 11.5 dB SNR) at every `-O` and
`-cpu`. The same source built with `m68k-linux-gnu-gcc -m68040` (freestanding, under
`qemu-m68k`) was bit-exact with the host, so the compiler was to blame. Tracing the decoder
stage by stage against the host (SILK output, the range coder's final state, then CELT's band
by band) ended in `haar1`'s constant `QCONST32(.70710678f,31)`: vbcc folds `(double)((long
long)1<<31)` to -2147483648 and `(int16_t)(.5+0.5f*(1<<15))` to -16383. Run-time conversions
are right. `third_party/opus/celt/fixed_generic.h` has variants for vbcc that avoid both; with
them the 68k output equals the host's 32-bit-multiply path exactly. A second vbcc bug turned up
in the game itself: `-O2` lost an `i += n` of a loop variable inside its loop (`voice.c`
`meta_get`), so the 68k build read the voice pack's codec as `codec=ldp6`.

## Keyboard input (`keyprobe.c`, 2026-11-12)

Report: with the WarpOS build in a window on virt68k, the game kept skipping
text after Ctrl was let go. `keyprobe` injects raw keys through
`input.device` (`IND_WRITEEVENT`) from a program started with `Run` before
the game, its steps timed by `Delay` (see its header), and logs them to a
file, not the console (a write to the shell's window activates it and takes
the keys from the game). A run:

    PRE_CMD="Run >NIL: DH0:Xenon/keyprobe oDH0:keys.txt w900 d w100 b w200 d w100 s w200 d w100 u w200" \
        RUN=keys GDB_PORT=2352 WB=0x29004:8 GUEST_FILES=probes/amiga/keyprobe \
        XENON_ARGS="LANG=en WINDOW MAXTICKS=3000" tests/amiga/run-game.sh native

What the game saw was logged by a temporary line in `am_input_poll`.

* With proper key-up events, the game in a window sees every Ctrl release at
  the next poll: on the 68k, in 709 polls held for 709 polls of injected
  holds; on WarpOS (`tests/warpos/run-wos.sh`, AGA Workbench) 18-37 ms after the
  injection, also with 200 mouse moves queued around it.
* The cause is virt68k's USB keyboard: Poseidon's boot keyboard class
  (`third_party/aros/rom/usb/classes/bootkeyboard`, `nParseKeys`) sends a
  modifier let go alone as `ie_Code` = its raw key *without*
  `IECODE_UP_PREFIX` (only the qualifier bit is cleared), sends no modifier
  event when the modifier changed in the same report as another key, and code
  0xFF when two modifiers changed at once. The game follows Ctrl's own
  up/down codes (`input.c`), as keyboard.device delivers them on Commodore's
  OS and AROS; steps `b` and `s` reproduce both USB cases, and the game kept
  Ctrl held through both (poll 998 to 1793, until a real `u`). Reported to
  virt68k (its `doc/BUG-nParseKeys.md`); the game is not
  changed to work around it.
