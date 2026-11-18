# The engine on the Amiga family

The Amiga programs (`make TARGET=amiga`, `warpos`, `aros`) are this engine on lrvne's Amiga
backend: [source/lrvne/backends/amiga/README.md](../source/lrvne/backends/amiga/README.md)
describes the backend (screens, window, scaling, Paula and AHI, the audio task's CPU budget,
WarpOS, AROS, options and tool types, testing). This page has what is the engine's.

## Builds

| target | FM core | voice codecs | notes |
|---|---|---|---|
| `amiga` | the light core, its render loop in 68k assembly | ldp6 | no Opus: it costs 229 % of a 68040/25 (WinUAE) and 260 KB |
| `warpos` | the light core in C | ldp6, Opus | |
| `aros` | the light core in C | ldp6, Opus | |

`OPN=lite2` oversamples the FM part (about 1.6 times the CPU: a 68060). The program carries
`$VER: xenon <version> (<date>)` (`engine/main.c`, from `engine/version.h`).

`make TARGET=amiga|warpos|aros dist` makes the release archive
`build/<target>/xenon-<version>-<target>.lha`: the drawer `Xenon` with the program, its icon
(the XENON lettering of the opening, made by `source/tools/mkicon.py` from a frame `make data`
derives; the tool types are the flavour's defaults) and `ReadMe.txt`, put together from
[source/games/xenon/dist/amiga/](../source/games/xenon/dist/amiga/) (the parts every flavour shares and each
flavour's own). No pack is in it.

## Releases

`source/engine/version.h` holds the release version (`XE_VERSION`) and its date; the Makefile
names the archives after it and `version xenon` prints `xenon 1.5 (18.11.2026)`. A release
bumps both values, the version line in `dist/amiga/intro.txt` and the archive names in
`dist/amiga/install-*.txt`, builds and checks the three archives, and gets an annotated tag
`v<version>` on `master` naming them:

    make TARGET=amiga dist  && make TARGET=amiga lrvne-dist-check    # unpacked and run in WinUAE
    make TARGET=warpos dist                                          # no rig for the archive
    make TARGET=aros dist   && make TARGET=aros lrvne-dist-check     # hosted AROS

1.0 (9.11.2026) was the first release, 1.1 (10.11.2026) added Polish, 1.2 (11.11.2026) the
WarpOS and AROS builds, 1.3 (12.11.2026) the `WINDOW`, `SCALE` and `ASPECT` options, 1.4
(13.11.2026) the voices of the Windows release, quitting at once and archives without
`xenon.pak`, 1.5 (18.11.2026) the engine as a generic AGS engine on lrvne 0.2.0 (below).

## What changed in 1.5, with the move to lrvne

* Saves are in `PROGDIR:saves/xenon/`, one file per record named by its key (`save1` ..
  `save4`, `progress`); 1.4 kept them in `PROGDIR:saves/` (or `ENVARC:Xenon/`) as `save1.dat`
  .. `progress.dat`: move them there and drop the `.dat` (the ReadMe says so too).
* Tool types: `MUSICVOL` is `VOLUME`, `VOICEVOL` is `VOICEVOLUME`.
* Keys: Help (and F1) is the PC-98's HELP key, F2-F6 are f.1-f.5 (F11 is no longer Help).
  Caps Lock only is caps; it and the kana lock (right Amiga+K) both show the Japanese text
  a page at once. Shift+Esc leaves at once, Ctrl+Shift+R starts the game again.

## Voice

Which line plays which clip is the engine's (`engine/voice/voice.c`, docs/voice.md); the
player, the decoders and the output (a FIFO the audio task drains into blocks of the second
stream; Paula's two free channels, or a second AHI stream) are lrvne's.

Tested (2026-11-13, version 1.4) in WinUAE, cycle-exact 68040/25, a new game's first 7000 frames with
OK every 600 ticks (the rig's game runner, then `run-game.sh native 25`, now lrvne's `run-program.sh` with MHz 25; the voice pack in `GUEST_FILES`, the SDL3 WinUAE
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

## Memory (measured with version 1.4, before the move to lrvne)

Measured with the `memory:` line the game prints at exit (engine allocations
by where they landed, the largest single block, and the system's free-memory
low-water mark below what was free at start, so screen buffers, audio
buffers and libraries loaded for the game are included). The program itself
(155 KB: 123 KB code, 32 KB BSS) is loaded before that and comes on top.
Heaviest content: route-end7 (all routes, both languages, checked on the
host, `--memstat` today); the real A1200 ran the title, the load
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
run `AddBuffers <volume> 200` first (lrvne's README, "Preload") or the game
hitches on every scene change. Loading the pack takes 3.4 s from the A1200's
CF card (1.8 MB/s) before the screen opens; after that no asset load ever
touches the disk.

