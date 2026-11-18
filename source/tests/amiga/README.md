# The Amiga build in WinUAE, and a real A1200

How the AmigaOS builds are run and checked. The WinUAE rig is lrvne's
(`../../lrvne/rigs/README.md`: `winuae/run-program.sh` runs a program, `run-lib.sh` is the
plumbing, `snapd.py` answers the guest's snapshot requests, `present.py` is the presentation
test; nothing there waits on the clock), and so is the release archive check (`make
TARGET=amiga lrvne-dist-check`). `make TARGET=amiga e2e` drives it through
`tests/e2e/amiga.py` (the presentation test and the scenario replays, `../e2e/README.md`).
What is xenon's own here: the fullscreen fast path's object guard (`fastpath-check.sh`) and a
real A1200 over the network (`a1200.sh`). The experiments these scripts started from, and their
findings, are in `../../probes/amiga/README.md`.

| file | what |
|---|---|
| `fastpath-check.sh` | guard of the default fullscreen path: records / compares the objects that draw the custom screen and the whole engine (`record`, `check`, `ref REV`) |
| `a1200.sh`, `aostelnet.py` | a real A1200 with AmiTCP: install, run, timed play-tests over telnet; see "The real A1200" |

## Settings

The machine's pieces are named in `tests/rig.conf` (copy `lrvne/rigs/rig.conf.example` and add
the A1200's keys below; git ignores the copy), or in the environment, which wins; `RIG_CONF=path`
reads another file instead. lrvne's rig reads `EMU`, `WINUAE`, `KICKSTART`, `WBDISK`,
`RIG_WORKTOP` (the work dirs, default `$EMU/aos/work/rig`); `a1200.sh` reads `A1200`,
`A1200_WWW`, `A1200_URL` (the A1200's address; a directory the host's web server serves, and
its URL as the A1200 sees it). `make TARGET=amiga lrvne-rig-tools` builds the guest helpers
(`uaequit`, `waitwb`; vbcc's `vc` on `PATH`).

## Running the game (phase 4)

    GUEST_FILES=../build/xenon.pak PROGRAM_ARGS='LANG=en AUTOPLAY="at:650=down,750=ok,1000=ok snap:1250,2000,2750" MAXTICKS=2800' \
        lrvne/rigs/winuae/run-program.sh ../build/amiga/xenon native     # or rtg, promote; [MHz]

copies the game and its pack into a Workbench tree, boots it (SetPatch, 68040 model, real
time; with MHz: cycle exact at that clock) and runs it; the snapshots at ticks 1250, 2000 and
2750 (25, 40 and 55 s at 50 Hz) land in `snaps/` of the work dir (a snapshot's pause is not
counted as a stall). `out.txt` ends with the game's statistics: blocks rendered vs device
interrupts (equal up to the two pre-filled blocks means no underrun), frames vs E-clock ticks,
frame-clock stalls. The real-time runs measure timing: that is their subject, not a pass/fail
check; the conformance runs (`tests/e2e/amiga.py`) use warp. `PROGRAM_ARGS2` runs the game a
second time (to compare `Avail`), `GUEST_SAVES` and `SAVES_DIR=saves/xenon` put a scenario's
saves into the drawer, `WARP=1`, `WB=MODEID:DEPTH`, `PRE_CMD`, `AHI=1`, `RUN`, `GDB_PORT`: the
script's header. Results so far (all with the title menu correct in the first screenshot):

| run | result |
|---|---|
| native, 68040 max speed | 2801 frames in 2804 ticks, 0 stalls; Paula 532 interrupts / 534 blocks |
| native, cycle-exact 68040/25 | 2801 frames in 2818 ticks, 1 stall (12 ticks, the title decode); 535 / 537 - the 22 kHz opn_lite render keeps up |
| promote (P96 FakeNative 640x400 on a 640x480 board mode) | BltBitMap path, 2829 ticks |
| `PAULA14` | 533 / 535 |
| `AHI=1`, `AUDIO=ahi` (AHI 6.6, paula.audio 4.30, unit 0 = 8-bit stereo 22050) | 535 / 537 |

**Audio needs a WinUAE with a sound backend.** The null-video build
(`$EMU/obj-winuae`) has no sound driver, sets `produce_sound = 0`
and then never runs the audio state machine: no DMA, no AUD0 interrupts,
`sound_output=interrupts` notwithstanding.  `$EMU/obj-winuae-sdl3/`
is the same source configured with `-DWINUAE_UNIX_WITH_SDL3=ON` (plus the
null build's other options, `CMAKE_BUILD_TYPE=Release`; the target is
`winuae_unix`, the binary `winuae`); `run-program.sh` exports
`SDL_AUDIO_DRIVER=dummy`, so no sound device is needed.  Use it as
`WINUAE=$EMU/obj-winuae-sdl3/winuae lrvne/rigs/winuae/run-program.sh ...` (or `WINUAE=` in
`tests/rig.conf`).

**AHI in the guest**: `AHI=1` installs `$EMU/aos/ahi/` (ahi.device
6.6 from `m68k-amigaos-ahiusr.lha`, `paula.audio` and `AudioModes/PAULA`
from `m68k-amigaos-ahidrv.lha`, both from Aminet) and writes
`ENVARC:Sys/ahi.prefs` with `mkahiprefs.py` (unit 0 = 0x00020009
"Paula:8 bit stereo", 22050 Hz; the other Paula ids are listed in that
script's docstring area of the session notes: 0x20001-0x20006 14-bit
calibrated, 0x20007-0x2000c 8-bit, 0x2000d-0x20012 HiFi, 0x20018 DMA).

**Hangs**: a program that opens `ahi.device` from a bare exec Task (not a
Process) hangs in OpenDevice (the device loads its driver through
dos.library) - the game's audio task is a `CreateNewProc` process for that
reason.  The harness survives such hangs: `EMU_TIMEOUT` ends the run, and `emu_run` traps
EXIT/INT/TERM and stops the emulator (the guest watchdog that once quit it after
EMU_TIMEOUT - 20 emulated seconds is gone: those seconds follow the host's load).
`ReadArgs` splits on an unquoted `=`, so `AUTOPLAY="at:650=down,..."` must
be quoted on the Amiga command line.

## The real A1200 (`a1200.sh`)

The user's A1200 (ACA1240 68040/25, 2 MB chip, ~255 MB fast, AGA only,
KS 40.68 / WB 40.42 with FBlit, AHI 6.6, PCMCIA NE2000, SanDisk CF on
Gayle) runs AmigaOS with AmiTCP; it is shared with other projects, so it
must be idle in AmigaOS before a run. From the host (`A1200`, `A1200_WWW`,
`A1200_URL` in `tests/rig.conf`):

    tests/amiga/a1200.sh install           # build/amiga/xenon + build/xenon.pak -> Stuff:xenon
    tests/amiga/a1200.sh play-test load    # title -> DATA LOAD menu, 2800 ticks, Avail FLUSH around it
    tests/amiga/a1200.sh play-test new     # NEW GAME, OK every 60 ticks, 5000 ticks (S00, xenon_01)
    tests/amiga/a1200.sh run LANG=en AUDIO=ahi MAXTICKS=5000
    tests/amiga/a1200.sh sh "List Stuff:xenon"

`install` puts the files into the host's web server (`$A1200_WWW`, sudo)
and fetches them with the Amiga's `wget` from `$A1200_URL` (`aostelnet.py`
drives the shell on telnet port 2323 at `$A1200`); if wget hangs, the host's
firewall blocks port 80 from the A1200. The Paula serial port goes to a USB
serial adapter on the host (9600 8N1). There is no remote reset besides `SYS:p9/coldboot` over telnet;
a hang with interrupts off needs a hand on the reset keys. No screenshots or
gdbserver: evidence is the game's log (stall lines, `memory:` line, audio
counters) and someone's eyes on the monitor.

First runs (2026-11-08, `play-test load`: title -> DATA LOAD menu, 2800
ticks; earlier notes called this "title -> new game", which it is not): at
22 050 Hz, Paula, `PAULA14` and `AUDIO=ahi` (the board's unit 0 = Paula DMA
mode 0x20018 at 13 855 Hz) all without underruns - the title music is light;
memory returned to the byte (`Avail FLUSH`). Pack on the CF card without
preload: two stalls per run, both FFS seeks (`probes/amiga/README.md` "Probe 4"); with the pack
preloaded (3.4 s) none after the start-up.

The first **new game** on the board (by hand, 22 050 Hz) hung the machine at
S00's "FLECTERE..." quote: `opn_lite` could not render xenon_01 in real time,
the priority-5 audio task never slept, the game, Workbench and the shells
starved, and S00's `MUSIC_WAIT` waited for music that played slower than
real time. Measured with `opnbench` on the board: `probes/amiga/README.md` "Probe 3". Since then the
default is 11 025 Hz and the audio task has a CPU budget
(`lrvne/backends/amiga/README.md` "Audio"); `play-test new` at 11 025 Hz, 22 050 Hz
and with AHI all get through S00 (average render load 46-55 %, peaks
106-178 %, 39-58 % of the blocks skipped).

**The release archive on the board (2026-11-09):** `xenon-amiga.lha` fetched
with `a1200.sh install`, unpacked with the board's `System:C/lha` into a
scratch drawer (`lha -q x`, all five files at their sizes, `----rwed`), the
game started from the unpacked `Xenon/` (`LANG=en MAXTICKS=600`: pack
preloaded in 3.5 s, Paula at 11015 Hz, 0 blocks skipped, memory returned),
then the scratch drawer and the archive deleted again. See "The release
archive" for the WinUAE side.

**The v1.0 release installed on the board (2026-11-09):** the game drawer
now holds exactly the unpacked `xenon-amiga-1.0.lha` plus the player's
saves. `DEST=RAM:xdl a1200.sh install ../build/amiga/xenon-amiga-1.0.lha`
fetched the archive; then on the board the old build and every probe left
in the drawer (`seekprobe`, the `opnbench*`/`opnprof*` binaries, the
`prof*.txt` profiles, kept in `probes/amiga/bench-XENON_24/a1200-prof*.txt`) were
deleted, `saves/` kept, the drawer renamed to `Stuff:Xenon` (the
archive's spelling; `DEST=Stuff:xenon` still finds it, names are
case-insensitive), `lha -q x` unpacked the archive in `Stuff:` and the
RAM copy was deleted. `Version Stuff:Xenon/xenon` prints `xenon 1.0`;
`a1200.sh run MAXTICKS=600`: pack preloaded in 3.4 s, Paula at 11015 Hz,
0 blocks skipped, memory returned. Probes run later should go to a
separate drawer, so the game drawer stays a clean release install.

## The release archive (2026-11-09; now `make TARGET=amiga lrvne-dist-check`)

`make TARGET=amiga dist` stages `build/amiga/dist/` - `Xenon.info` (the
drawer icon) and `Xenon/` with `xenon`, `xenon.info` and the
player's `ReadMe.txt` (put together from `source/games/xenon/dist/amiga/`; since 2026-11-13 no
`xenon.pak`: the check copies `build/xenon.pak` into the unpacked drawer before it runs
the game) - and packs it with
the host's LhA for UNIX 1.14i into `build/amiga/xenon-<version>-amiga.lha`
(the version from `engine/version.h`; 4.3 MB for the 6.4 MB tree when it held the pack: `-lh5-`,
level-1 headers; `LHA_LEVEL=` and `LHA=` override). `make TARGET=amiga lrvne-dist-check`
(lrvne's `rigs/winuae/check-dist.sh`) unpacks the archive in WinUAE (68040 CPU: the tree's
`C/lha` is the 68040 build of LhA 2.12), lists it, starts the game from it
(`AUDIO=none FAST MAXTICKS=200`) and then compares every unpacked
file with the staging directory on the host, byte for byte. Findings:

* LhA 2.12 unpacks all three header levels of lha 1.14i (`-0`, `-1`, `-2`)
  with identical contents, every file `----rwed`, so the executable needs
  no `Protect`. With level-2 headers the files come out dated 1980 (the
  Unix time stamp of that header form is not understood); levels 0 and 1
  keep the real date. Hence level 1.
* the unpacked game starts: pack preloaded (6073 KB), title drawn, 502
  frames, "max ticks reached", memory returned.
* two requesters stop LhA dead in the minimal guest tree: a missing
  `mathieeedoubbas.library` (copied from the tree now) and no `ENV:` volume
  (LhA reads its options from there; the script assigns `ENV:` to
  `RAM:ENV`). The second one only showed on a gdbserver screenshot
  taken while the run looked hung - the first thing to do when a guest
  program stops producing output.
* the same archive unpacked on the real A1200 with its `System:C/lha`, see
  "The real A1200".

## Emulator recipe

Host: `$EMU/obj-winuae/winuae` (null video), `-f a.uae`, output in
`emu.log`.  The gdbserver pauses the CPU on attach; lrvne's `rigs/snapd.py` (`lrvrig/gdb.py`) sends
`?` and waits for the stop reply, then `qRcmd,<hex "screenshot /path.png">`, gets `OK`,
sends `D` (detach resumes). A probe asks for a screenshot by creating
`DH0:shot_<tag>_<n>.req` and waits (polling `Lock()`) for `.ack`, which the server creates
after the PNG is written to `shots/`.  RTG screens are captured too (`rtg_*.png` are 640x480).

Config keys that worked for RTG (what `emu_config rtg` in lrvne's `rigs/winuae/run-lib.sh` writes):

```
use_gui=no
kickstart_rom_file=$KICKSTART
chipset=aga
chipset_compatible=A1200
cpu_model=68020
cpu_compatible=false
cpu_24bit_addressing=false
cpu_speed=max
cachesize=0
cycle_exact=false
immediate_blits=true
warp=true
chipmem_size=4
fastmem_size=8
nr_floppies=1
floppy0type=0
filesystem2=rw,DH0:dh0:<workdir>/dh0,10
debugging_features=gdbserver
gdb_port=2345
z3mem_size=16
gfxcard_type=ZorroIII
gfxcard_size=16
```

Guest side (DH0: = selective copy of the Workbench tree, `$WBDISK`:
C, Libs, Devs, S, L, Prefs, Classes, Locale, Fonts, System, Utilities,
Tools, WBStartup, Rexxc, plus `*.info`; ~6 MB):

```
C:SetPatch QUIET
C:MakeDir RAM:T RAM:ENV RAM:ENV/Sys RAM:Clipboards
C:Copy >NIL: ENVARC: RAM:ENV ALL NOREQ
Assign ENV: RAM:ENV
Assign T: RAM:T
Assign CLIPS: RAM:Clipboards
Assign LIBS: SYS:Classes ADD
DEVS:Monitors/PAL
DEVS:Monitors/uaegfx
C:IPrefs
... probes, with stdout appended to DH0:out.txt ...
C:LoadWB          (wb variant only)
C:uaequit
```

`Libs/Picasso96/{rtg.library 40.3945, uaegfx.card, emulation.library,
fastlayers.library}`, `Libs/Picasso96API.library`, `Devs/Monitors/uaegfx`
(Picasso96 40.42) come from that tree; WinUAE logs `Unix uaegfx.card 3.4
init`, `Unix RTG FindCard: … mem=41000000 size=16777216`, `InitCard: 10
modes`, and `SetGC: 640x480x8` when the probe opens its screen; each
ChangeScreenBuffer shows up as `Unix RTG SetPanning: addr=41000000|4104B010`.

P96 promotion of native modes needs no GUI: the uaegfx monitor icon gets
the tool type `FAKENATIVEMODES=Yes` (`settooltype.py`, documented in
`Picasso96.readme` for rtg.library 2.0 as "FakeNativeModes=Yes") and
`DEVS:Monitors/PAL` is not started.  Then OpenScreen(0x8004) yields the
mode "FakeNative:HighRes-Lace", a 640x400 P96 chunky screen
(`probes/amiga/out-rtg-promote.txt`, `shots/promote_2.png`).

