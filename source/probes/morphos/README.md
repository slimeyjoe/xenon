# MorphOS probes (QEMU pegasos2)

> These probes were written while the platform layer was this engine's own (before the move to
> lrvne, `source/lrvne`); what they measured is lrvne's Amiga backend now
> (`source/lrvne/backends/amiga`, its README). Paths into the old tree (`engine/snd/opn_lite.c`,
> `config/*.mk`) name files that are in git history only: rebuild such a probe from a checkout
> of that history. The results stay valid as records.

Does the Amiga build of Xenon (a 68k AmigaOS binary) run on MorphOS? Tested on MorphOS 3.21
in QEMU's `pegasos2` machine. `sam460ex` is not used: MorphOS does not boot there since QEMU
8.2 (TCG commit 18a536f1f8, qemu-ppc 2024-02, still listed as broken on the maintainer's page
https://zero.eik.bme.hu/~balaton/qemu/amiga/). The runner (`mos.sh`) and the environment it needs
are the test rig's: `../../tests/morphos/README.md`.

## Results (2026-11-10, QEMU 10.2.2 Fedora package, MorphOS 3.21)

The game itself works: `xenon lang=en audio=none maxticks=2400` ran 2401 frames in 2420
ticks at 56.42 Hz (real time), one 15-tick stall at start-up, 0 bytes not freed
(`out-game-ati-qemu10.2.txt`). The default `HIRESLACE_KEY` mode is available on MorphOS and is
mapped to a graphics-card mode (0x41120000 = 640x480x8 on ati-vga), so the RTG depth-8 path of
`lrvne/backends/amiga/core/video.c` is taken.

But nothing is visible: the 640x480 screen is black except the mouse pointer (a hardware
cursor, drawn in the screen's pens 17-19, so the palette is right). `rtgprobe` reproduces it
without the game (`out-rtgprobe-ati-qemu10.2.txt`): even a RectFill into the screen's own
RastPort does not show, and ReadPixel returns garbage. With `-d unimp` QEMU logs
`blt outside vram not implemented` for each blit. Cause, from the QEMU source: 10.2's
`hw/display/ati_2d.c` adds `crtc_offset` (the displayed start address) to the blit destination
on a Rage 128 Pro; the 1920x1080 Ambient screen sits at offset 0, the 640x480 game screen does
not, so its blits land "outside VRAM" and are dropped. QEMU 11 rewrote the 2D engine
(`setup_2d_blt_ctx`, no `crtc_offset` term, plus HOST_DATA blits); see the next section.

`sm501` instead of ati-vga is worse (`out-rtgprobe-sm501-qemu10.2.txt`): the screen opens
(mode 0x41f20000) but is never shown as its own display mode, and `AllocScreenBuffer` fails
for both buffers - the error path where `close_screen()` used to recurse (fixed 2026-11-10).

## Results on QEMU 11.1.3 (built from source, 2026-11-10)

Built into `$MOS_DIR/qemu-11.1.3` (`configure --target-list=ppc-softmmu --disable-docs
--disable-werror --enable-png`; no libslirp here, so no user networking); use it with
`QEMU=$MOS_DIR/qemu-11.1.3/bin/qemu-system-ppc tests/morphos/mos.sh ...`.

Better, but still not enough for the game (`out-rtgprobe-ati-qemu11.1.3.txt`,
`out-blitprobe-ati-qemu11.1.3.txt`, `shots/`): no more `blt outside vram`; RectFill shows,
ReadPixel is right, both ScreenBuffers allocate and flip. But on the depth-8 screen:

| method (blitprobe band) | result |
|---|---|
| BltBitMap, InitBitMap 8-plane source as the game (0), separate planes (1) | pen 0 everywhere |
| BltBitMapRastPort, same source (3) | pen 0 |
| BltBitMap, 4-plane source (6) | partly drawn, wrong pens |
| BltBitMap chunky -> chunky from AllocBitMap(friend = screen) (7) | pen 0 |
| WriteChunkyPixels (4), WritePixelArray8 (5) | correct |

Even a chunky-to-chunky BltBitMap fails, so this is QEMU's ati-vga 2D emulation on 8-bit
screens (nothing logged by `-d unimp,guest_errors`), not the planar source. Also the palette is
wrong: MorphOS writes 8-bit DAC values, QEMU shows them as 6-bit (`(v & 63) << 2`, e.g. 0x40 ->
0, 0x20 -> 0x80). AllocBitMap without a friend is not BMF_STANDARD on MorphOS (flags 0).

So in QEMU (10.2 and 11.1.3) the game cannot be checked visually as it is; real MorphOS
hardware is expected to do BltBitMap normally (unverified).

## Status and open items (parked 2026-11-10)

Parked by decision: the game starts, runs at full speed and exits cleanly on MorphOS 3.21, but
QEMU cannot show its 8-bit screen, so nothing visual was verified. Not tested at all:

- AHI sound: pegasos2 has the VIA AC97 (MorphOS drives it; `mos.sh` already records it to
  `run/NAME/audio.wav` via `-audiodev wav`), so `AUDIO=ahi` + a non-silent WAV is a cheap check.
- `AUDIO=paula`, the default: expected to fail (direct Paula registers, `SetIntVector`), which
  would argue for defaulting to AHI when running on MorphOS.
- Input (keyboard/mouse through Intuition), Workbench start, the icon.
- Real hardware (Pegasos II, Sam460, PowerMac): BltBitMap onto an 8-bit screen should just work.

Options considered, none taken:
1. A second graphics-card upload path in `lrvne/backends/amiga/core/video.c`: CPU planar-to-chunky of the
   dirty rectangle into a chunky buffer + `WriteChunkyPixels` (works in QEMU 11.1.3, see
   blitprobe band 4). Would make the picture visible in QEMU, though with wrong colours there
   (6-bit DAC); possibly faster on real MorphOS too (unmeasured).
2. Patch QEMU's ati-vga (8-bit 2D blits, 8-bit DAC) and upstream it.
3. Proposed, not implemented: BestModeID fallback in `am_video_open` - when `ModeNotAvailable`
   or the first OpenScreen fails (default mode or an explicit `MODE=`), log it and use
   `BestModeID(NominalWidth 640, NominalHeight 400, Depth 8, DIPFMustNotHave SPECIAL_FLAGS)`,
   depth 4 if that gives INVALID_ID; if the depth-4 probe open fails on a graphics card, go
   straight to depth 8; e2e case = uaegfx with a bogus `MODE=`. Not needed on MorphOS (the
   default mode exists there), only for stale tooltypes / other RTG-only systems.

Disk: `$MOS_DIR` holds the ISO (489 MB), the QEMU 11.1.3 install, and its source + build tree
(`qemu-src`, 2.1 GB, deletable once the install is kept).
