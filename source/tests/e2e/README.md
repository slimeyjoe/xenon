# End-to-end tests

These tests play the game. The engine runs headless in control mode, a Python driver feeds it
input exactly when it waits for a key, and three oracles check what it displayed and when.
Nothing here needs a display, real time or an emulator; a whole route to an ending takes about
15 seconds on the null backend. The same scenarios run on the SDL3 backend rendering offscreen,
where a fourth oracle compares what the renderer produced with the engine's planes. The Amiga
build is checked in WinUAE by replaying the scenarios' input ("Amiga in WinUAE" below; that
one does need the emulator rig), the AROS build the same way in hosted AROS ("AROS" below).

    make TARGET=host e2e                         # all scenarios, English pack, null backend
    make TARGET=host e2e E2E_ARGS="-k route"     # scenarios whose name contains "route"
    make TARGET=host e2e E2E_ARGS="--lang ja"    # the Japanese scripts
    make TARGET=host e2e E2E_ARGS="--list"
    make TARGET=amiga e2e                        # Amiga build in WinUAE: presentation test
    make TARGET=sdl3 e2e                         # SDL3 backend offscreen + presentation checks
    make TARGET=sdl3 e2e E2E_ARGS="--scale aspect --window 1920x1080 -k title"
    python3 -I tests/e2e/run.py --help           # --transcripts DIR: input transcripts (amiga.py)

The scenarios run in parallel, one per CPU (`--jobs N`; `--jobs 1` runs them one after another
in the driver's own process). Each language and each part of a scenario is a unit of its own
(`@scenario(..., parts=3)` calls the function once per `ctx.part`: `explore`'s three walks); the
results are printed in scenario order, the same whichever unit finishes first.

The scenarios are written for `en` and `ja`; the Polish scripts (`pl`) have none yet and are
checked by play-review (`lang/README.md`). The SDL3 run takes about 30 minutes per language
(every snapshot reads the window back).

Needs Python 3 with numpy and Pillow, `build/host/xenon` (or `build/sdl3/xenon`),
`build/xenon.pak` and the blink timing table `make data` derives
(`resources/anim/blink/blink_timing.tsv`; `../../../docs/game-data.md`).

## How a run works

1. `run.py` starts `xenon --control` for every scenario (`xe2e/engine.py`). The backend
   (`lrvne/backends/control/control.c`, built into the null and SDL3 backends) writes every engine
   event as a JSON line and stops at the end of any tick in which the engine is blocked waiting
   for a key, at a breakpoint event, or after a tick budget. The driver answers with `press`,
   `run`, `snap`, `record`, `break`. The scenarios press the engine's commands (`ok`, `cancel`,
   `help`, `f1`..`f5`, `other`) and hold its levels (`ctrl`, `instant`); `xe2e/engine.py` sends
   them as the host keys the engine reads them from (`KEY_NAMES`: Return, Esc, F1, F2..F6, A;
   Ctrl and the kana lock), which is also how `amiga.py` and `aros.py` write the replays.
2. The engine reports its events through `hal->trace` (`engine/xe.h`, `enum xe_trace_kind`, from
   lrvne's `LRV_TR_ENGINE`; waits, script errors and the quit are lrvne's own kinds):
   script and page entered, every opcode with its operands, message start and end, menus with
   their options, title menu, image loads, G_DISP, effect and fade bounds, ADT frames, blink
   copies, waits entered and left, keys consumed, save/load, progress, boxes, sequences. Each
   event carries the offset of the script statement being executed.
3. `xe2e/game.py` turns that into a driver with `advance()`, `choose()`, `choose_title()`,
   `play_until()`, `golden()` and runs the oracles:
   * **script** (`xe2e/script.py`): every event is matched against the bytecode the engine
     loaded (parsed with `re-tools/ccdis.py`): message text, visible menu options under the current
     flags, opcode and operands, flag writes, page numbers, label taken, assets present in the
     pack. Flags are tracked and re-read from the save file after LOAD.
   * **images** (`xe2e/images.py`): the off-screen buffers are replayed with `re-tools/gdt.py`
     (byte-exact against the original decoder) and, after every G_DISP or effect, the CG area
     of the displayed frame must equal them. The text window, the choice menu, the blink strip,
     the title highlight and the lower-screen clear are masked; the wipes' uncovered rows
     (h not a multiple of 8) are left out. Checked at every blocked wait.
   * **timeline** (`xe2e/timeline.py`): from the per-tick frame hashes and the events: WAIT n
     lasts n ticks, a fade with delay d steps every d ticks one palette step at a time, blind =
     2·h/8 ticks, centre-open = h/8, waku2 close = 37, dissolve = 16, one tick per full-width
     glyph of typing (none with instant text), ADT frame i after delay[i] on the other page,
     blink copies as in `resources/anim/blink/blink_timing.tsv`.
   * **presentation** (`xe2e/present.py`, `--backend sdl3` only): at every snapshot the driver
     also asks the SDL3 backend for a `readback` of what its renderer drew (one RGB sample per
     source pixel, the destination rectangle, and two counters it computed over the whole
     drawable: destination pixels differing from their source pixel and lit pixels outside the
     rectangle). The rectangle must be the one the scale mode prescribes for the drawable
     (plan §4.1), the border black, and in the integer modes every sample must equal
     palette[index] (black with graphics off); in the filtered modes (fit, stretch) only pixels
     whose eight neighbours share their colour are compared. Mismatches leave
     `<game>-<snap>-<tick>.{planes,rendered,diff}.png` under `build/sdl3/e2e/diff/<lang>/`.
4. **Goldens** cover what has no independent oracle: the title, menus, boxes, the rendered
   text box. Goldens are curated (a scenario calls `g.golden(name)` at the screens it cares
   about); `g.check_image()` runs the image oracle anywhere without a golden. The reference is
   `golden.tsv` (committed): a sha256 per golden over what the frame shows (colour indices,
   palette, graphics on, scroll). The pictures are the game's art and are not committed:
   `golden/<lang>/<name>.png` (4-bit palette PNGs with the palette, graphics-on and scroll in
   text chunks) is a local copy, saved by a run whose frame matches the hash, so the first
   `make e2e` of a fresh checkout makes them. A failure then writes `<name>.actual.png`,
   `.golden.png` and `.diff.png` under `build/host/e2e/diff/<lang>/`; without a local copy
   (the first run already fails) only `.actual.png`: run the scenario once on an unchanged tree
   (`git stash`, or a worktree of master) to make the copy. A local copy whose hash no longer
   matches `golden.tsv` (it changed in a pull) is deleted and made again. `run.py --update`
   rewrites both the PNGs and their rows; review the images under `build/host/e2e/diff/`
   first.

## Scenarios (`scenarios/*.py`)

| Name | What it plays |
|---|---|
| `logo`, `title` | logo hold, title cursor, locked omake notice, L.CC menus, no-data box, system menu, quit |
| `sysmenu` | cancel at a script menu: cancel / quit confirmation / return to title |
| `quit-in-story` | quit (window close, Ctrl-C) and Shift+Esc in the prologue, at a key wait and inside a fade or wait: the engine leaves within a tick (a pending quit switches the timing rules off, like CTRL) |
| `prologue`, `instant-text`, `ctrl-skip` | S00 to DREAM_01 with goldens; KANA-lock instant text; CTRL cutting every wait |
| `route-end1` … `route-end7`, `route-detour-b` | whole-game routes to the seven endings (menu rules in `routes.py`, everything else by rotation); checks COMPLETE n, the staff roll, the script chain and the progress byte |
| `omake-cg`, `omake-music` | CG gallery (every page) and music mode with all endings done |
| `save-load` | save at the first save point, load it in a fresh engine, both runs continue alike |
| `explore` | random walks from several progress states, oracles only |
| `present-geometry` | SDL3 only: the title at twelve drawable sizes and scale modes (1:1, 2x, 1080p, 4K, too small, filtered) |

Menus the scenario does not decide are played by `game.Rotation`: every option once, the ones
whose case body sets a flag some condition asks for first, then always that one. That gets
through the S0102 quiz loop without knowing the answers.

Known data bugs of the original files (`script.KNOWN_DATA_BUGS`) are reported as notes.

## Adding a scenario

```python
@scenario('my-scene')
def my_scene(ctx):
    g = ctx.game(progress=0x01)            # progress byte written before start
    g.until_blocked()
    g.choose_title(0)
    g.play_until(lambda g: g.script_name == 'S0301' and g.menu is not None)
    g.golden('s0301-first-choice')
    g.choose('Tracy')                      # substring of the option text, or an index
```

`ctx.game()` accepts `saves={'save1': path}` fixtures. Scenarios run in parallel, so a game
keeps its files to itself: its storage is `build/host/e2e/storage/<lang>/<game>/`, and golden
and game names are unique. Problems from `g.expect()`, the oracles and goldens are collected
and printed per scenario; the raw event log of every game is in `build/host/e2e/logs/<lang>/`.

## SDL3 backend

`xenon --control` on the SDL3 build (`lrvne/backends/sdl3/main.c`) renders into an offscreen window
(SDL's `offscreen` video driver; `SDL_VIDEO_DRIVER` in the environment overrides it, and
`SDL_RENDER_DRIVER=software` picks the software renderer), opens no audio device and runs as
fast as the driver lets it. `--window WxH` sets the drawable size (default 1280x800),
`--scale` the mode, `--storage DIR` keeps the saves out of `$XDG_DATA_HOME`. `run.py --backend
sdl3` (what `make TARGET=sdl3 e2e` passes) adds the presentation oracle; `--window` and
`--scale` apply to every scenario, `present-geometry` sets its own. The planes snapshots and the
goldens are the same as on the null backend, so a golden failure on SDL3 only is an engine
difference between the two builds, and a presentation failure is the backend.

## Amiga in WinUAE

`tests/e2e/amiga.py` checks what the Amiga build (`build/amiga/xenon`) really puts on the
screen of an emulated Amiga (plan §10). It runs on lrvne's WinUAE rig
(`lrvne/rigs/README.md`; the machine settings in `tests/rig.conf`) and needs `build/host/xenon`
for the reference frames.

    make TARGET=amiga e2e                                            # stage A, all configurations
    make TARGET=amiga e2e E2E_ARGS="game --scenario prologue"        # stage B, native only
    make TARGET=amiga e2e E2E_ARGS="game --scenario route-end7 --configs native,promote --max-snaps 60"
    python3 -I tests/e2e/amiga.py check --kind present --ref REF.jsonl RUN/snaps   # re-check

Configurations (each its own emulator, work dir and gdb port, in parallel): `native` (AGA,
no Picasso96), `rtg` (Picasso96 loaded, native mode), `promote` (the default mode promoted to a
uaegfx screen with `FAKENATIVEMODES`), `uaegfx` (`MODE=0x50031000`, a 640x480x8 board mode).
Not in the default set (`--configs scaled`, `--configs window`, or by name), the options of
`lrvne/backends/amiga/README.md` "Window, scale, aspect": `native-aspect` (`ASPECT` on PAL HiRes
Laced: 640x480), `promote-aspect`, `uaegfx-x2` (`SCALE=2`: a 1280x1024 board mode found by the
game), `uaegfx-x2a` (`SCALE=2 ASPECT`: 1280x960); `wb-rtg8`, `wb-rtg16` (`ASPECT`), `wb-rtg32x2`
(`SCALE=2`, borderless) and `wb-aga256` (borderless) put the game in a window on a Workbench
of that mode (`run-program.sh WB=MODEID:DEPTH`), `wb-aga8` checks the fall back to the own screen
on an 8-colour Workbench. The expected image of a scaled screen is the reference expanded
through the same row map (lrvne's `lrvrig/oracle.py` `expand`); a window is found at the screen's position
plus the window's inside from the request (no registration image on a Workbench), its bitmap
read through its pens (`pens=`), its surroundings not checked, and 15/16-bit screens compared
within 8 per channel.

* **Snapshots.** At each snapshot the game (or the present test) waits four vertical blanks
  and writes `snap_<name>.req` (`lrvne/backends/amiga/core/snap.c`) with the displayed buffer's address,
  layout and the frame hash (`lrvne/backends/common/ptest.h`). lrvne's `rigs/snapd.py`, started for
  every run by `rigs/winuae/run-lib.sh`, attaches to the WinUAE gdbserver, waits for the stop
  reply (the CPU has stopped), takes a screenshot, reads the displayed bitmap from guest
  memory (planar chip RAM, or the graphics card memory via `p96GetBitMapAttr`), detaches and
  acknowledges. The game waits for the ack as long as it takes.
* **Time.** The runs go in warp (`--real-time` to throttle them): FAST and PRESENTTEST step
  by ticks, so nothing in them counts real time and the results are the same; a busy host
  only makes a run longer. `--timeout` (600 s per run) is the one backstop, and a run it
  ended is reported as `emulator timeout` (`emu.rc`). One `amiga.py` at a time: a second
  waits for the first (a lock in the work directory).
* **Oracles** (lrvne's `rigs/lrvrig/oracle.py`): the frame hash must equal the null backend's (else
  the 68k build computed a different frame); the screenshot, cropped where the registration
  image says the 640x400 picture is, must equal palette x 17 (black with graphics off) and be
  black everywhere else (overscan border, board rows below the screen); the bitmap must hold
  the frame's colour indices (graphics card: pens 16..31).
* **Stage A, `present`.** `xenon --present-test` (null) and `xenon PRESENTTEST` (Amiga) run
  the same synthetic sequence of 25 snapshots through every present path: first upload, dirty
  rectangles at odd columns and the edges, palette only, graphics off, page flips, scroll
  changes, redraws while scrolled, colour 0 red and an all-white palette, a random soak. About
  90 s for the four configurations. The calibration (image position per configuration) is
  saved in `build/amiga/e2e/calibration.json` for stage B.
* **Stage B, `game`.** Plays the scenario on the host with `run.py --transcripts`, turns the
  first-poll input transcript into an autoplay spec (`at:` presses, `ctrl:`/`instant:` holds,
  `snap:` ticks: every golden, evenly spread image checks, a few shake frames, `--max-snaps`
  in all; mouse input cannot be replayed), replays it on the null backend and requires the
  same frames as the control-mode run, then runs the Amiga build with `AUDIO=none FAST
  AUTOPLAY=@... MAXTICKS=...` and the scenario's starting saves. Without audio the sequencer
  runs on the per-tick silent clock, so the replay is tick for tick the same as the host's.

Results: `FAIL`/`PASS` per configuration; diffs (`*.expected.png`, `*.screenshot.png`,
`*.diff.png`, `*.bitmapdiff.png`) under `build/amiga/e2e/diff/<config>/` (stage A) or
`build/amiga/e2e/game/<game>/diff/<config>/` (stage B); emulator work dirs under
`$RIG_WORKTOP/{present-xenon,game-<game>}-<config>/` (lrvne's rigs: default `$EMU/aos/work/rig`).

What it found when it was built (2026-11-08): the native overscan border showed colour 0;
dirty updates while scrolled went to unrotated rows (latent: no scenario redraws during a
shake); on graphics card screens the board rows below the image showed colour 0 and stale
upper bits of uncleared video memory showed as wrong pens; the engine cleared the dirty
rectangle after the overlay restore had marked it, so a vanished prompt or message box stayed
on screen on a dirty-rectangle backend; and the Amiga build linked objects compiled against an
older engine state layout (vbcc writes no dependency files; the Makefile now rebuilds every
object when a header changes), which made the game quit at the title.

## AROS

`tests/e2e/aros.py` runs the same two stages on the AROS build (`build/aros/xenon`) in hosted
AROS (lrvne's `rigs/aros/aros.sh`, headless; `tests/aros/README.md`). There is no emulator debugger
to read the screen: the guest reads its shown page back itself (`lrvne/backends/amiga/aros/snap_aros.c`,
`ReadPixelArray` of the true colour bitmap) and writes a 640x400 PPM next to each snapshot
request in SNAPDIR, a host directory in hosted AROS, and carries on. Each snapshot is checked
for the frame hash and, pixel by pixel, the colours read back against the reference frame
(lrvne's `lrvrig/oracle.py`; the spec and the reference come from `amiga.py`). FAST runs take
seconds.

    make TARGET=aros e2e                                          # stage A, the present test
    make TARGET=aros e2e E2E_ARGS="game --scenario prologue"      # stage B
    make TARGET=aros e2e E2E_ARGS="game --scenario save-load"

Results 2026-11-11: present 25/25, prologue 40/40, save-load (both games) 40/40 each.

The window, scale and aspect modes (`lrvne/backends/amiga/README.md` "Window, scale, aspect"):
`aros.py present --guest-args "WINDOW" --name e2e-present-window` adds program options (the
run name keeps runs apart); the guest then reads back the window's inside or the scaled page
(`sx=`/`dsth=` in the request, expanded by the oracle's `expand`). Results 2026-11-12, 25/25
each: `WINDOW` (1024x768 true colour Wanderer screen), `WINDOW ASPECT` (640x480), `SCALE=2`
(a 1280x800 screen), `SCALE=auto ASPECT` (1920x1440).

A hosted AROS run ends when its commands are done (a marker the host waits for); `--timeout`
is the backstop, and a run that did not finish (AROS ended early, the X server failed, the
backstop) is a failure saying so (`aros.sh` exit status).

WarpOS: `make TARGET=warpos lrvne-present` runs the presentation test on the PPC build
(lrvne's `present.py warpos`: `run-wos.sh`, one configuration at a time, real time;
`LRVNE_PRESENT_ARGS="--configs native,wb-aga256"` for more; `probes/warpos/README.md` "Window,
scale, aspect").

## Protocol reference

Commands: `press k[,k]`, `hold ctrl|instant|anykey 0|1`, `mouse DX DY`, `run [N]`,
`break KIND|all`, `unbreak KIND|all`, `record 0|1|2`, `snap NAME`, `readback NAME`, `quit`.
Events: trace events (`ev` = kind name), `stop` (`why` = start / blocked / break / ticks),
`frame` (per tick while recording, only when the frame changed), `snap`, `present` (answer to
`readback`: `win`, `dest`, `mode`, `linear`, `nonuniform`, `border_lit`, `data` = base64 RGB
640x400), `ok`, `error`, `exit`. Frame data is base64 of 640x400 nibble-packed colour indices
(scroll applied).
