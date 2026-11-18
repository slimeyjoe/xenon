# Xenon / the AGS engine

Reverse engineering of the PC-98 visual novel Xenon ～夢幻の肢体～ (C's Ware, 1994; engine
`AGS.EXE`, music driver PMD 4.7g) and a new portable engine for it: a re-implementation of
AGS.EXE (C prefix `xe_`/`XE_`), for which the game is a profile and a few native modules
(program `xenon`, game id `xenon`). The engine is built on **lrvne**, the shared low-level
library of this engine and its siblings (submodule `source/lrvne`, public, C prefix `lrv_`).

## Where things are

- `docs/re/README.md` — index of the research: engine, scripts, opcodes, graphics, sound, PMD,
  fonts, saves, media, other releases. **Read the relevant document before changing engine
  code; they are the specification.** `docs/profile.md` (the profile's keys, and which item is
  the engine's and which the game's), `docs/game-data.md`, `docs/voice.md`, `docs/amiga.md`.
- `source/engine/` — the engine, and only what is AGS.EXE's: the interpreter (`vm/`), pictures,
  animations, effects and palette (`gfx/`), the message printer, menus and text layer (`text/`),
  the sound API over lrvne's PMD module (`snd/`), the voice cues (`voice/`), the title flow,
  saves and the profile (`game/`), the module hooks (`mod/`), the frame and input (`input.c`),
  `app.c` (the `struct lrv_app`). One state struct, `xe` (`engine/xe.h`), embeds lrvne's instances.
- `source/games/<game>/` — a game: `profile.txt` (written into its pack), its native modules,
  `game.mk` (program name, release archive), `pack.py` (the pack builder's game facts).
  `source/games/main.c` registers the modules and runs the engine. The Makefile's `GAME`
  defaults to the only game there.
- `source/lrvne/` — everything below the engine, compiled in from source (`mk/lrvne.mk`): frame
  loop and input (`lrv_sys`), screen (`lrv_screen`), PC-98 video mode, OPN cores, write queue,
  mixer, the music path (`lrv_music`), the PMD 4.7g driver (`lrv_pmd`, switched on with
  `LRVNE_PMD`; `docs/pmd.md` there), voice player and decoders, fonts, pack, profile parser, the
  backends (headless `null` for `TARGET=host`, `sdl3`, the Amiga family) with command line,
  autoplay and the control protocol, and the generic Python tools. Its headers in `include/lrvne/`
  are its documentation; read `docs/architecture.md` and `docs/porting-an-engine.md` there. **It
  is a public, pseudonymous repository of its own**: never put a person's name, a host or home
  path, this engine's or a game's name into it, and nothing engine- or game-specific. A bug or a
  gap found from here is fixed there, generically, with a test there; the engine does not work
  around lrvne and does not wrap it (call `lrv_*` directly).
- `source/tools/` (stdlib only, on `lrvne/tools`): `gamedata.py` (import/check/derive: lrvne's
  inventory framework with this game's hooks), `mkpack.py`, `mkvoice.py`, the translation
  tools, `mkicon.py`, readers of the game's own formats.
- `re-tools/` — research tools and verified reference models (`gdt.py`, `adt.py`, `ccdis.py`,
  `pmdmodel.py`, `pmdemu.py` under unicorn, ...). `text/` — the translations.
- Build: `cd source && make data SRC=<your copy> && make pack && make TARGET=sdl3`.

## Tests (`cd source && make TARGET=host test`, about a minute)

- `tests/run.sh` runs everything; a test skips with a note without the imported game, the
  reference recordings (`make data` with `DATA_PY` = a Python with unicorn) or the packs.
- The headless backend is deterministic: `xenon --pak P --autoplay SPEC` (snapshots with frame
  hashes at `snap:` ticks), `--control` (the line protocol of `tests/e2e`), `--render-wav F
  --song NAME [--opn lite|lite2] [--se N --se-at S]`, `--present-test` (needs the pack;
  `--present-test-mode pc98` does not). Keys are lrvne's neutral names (`return`/`ok`,
  `esc`/`cancel`, `f1` = HELP, `kana` = instant text); a press also shows in the key-down
  bitmap for the poll that delivers it.
- Each check pairs a C test program (`tests/test_*.c`, linked with the engine alone, no games,
  HAL in `tests/test_hal.h`) with a Python comparer (`tests/check_*.py`) against a reference:
  the game's files, `re-tools/`' models, PMD under emulation, libopus. `check_core.py` runs the
  engine with no modules and with broken profiles.
- `make TARGET=host e2e` (en; `E2E_ARGS="--lang ja"`): every route to every ending under the
  script, image and timeline oracles; `make TARGET=sdl3 e2e` adds the rendered pixels;
  `TARGET=aros e2e` replays them in hosted AROS (`tests/aros/README.md`).
- `make TARGET=host SAN=1 test` runs the same under the sanitizers (objects in
  `build/host-san`). Rebuild the test binaries after engine changes (`make test` does).
- The engine's teardown: the engine is asked to quit at `--max-ticks` and must have freed
  everything it allocated through the HAL when `run` returns, or lrvne's headless backend ends
  with exit code 1 ("memory: N bytes in M blocks not freed by the engine"); the teardown run in
  `tests/run.sh` (to `--max-ticks`, expects exit code 3) keeps it so. Memory an engine part
  allocates on first use needs a shutdown function called at the end of the run.
- Hosted AROS with sound: lrvne's rig runs AROS without PipeWire's data-loop threads (its ALSA
  driver otherwise lets that thread take AROS's timer signal and AROS crashes now and then;
  `lrvne/rigs/README.md`, "Hosted AROS and PipeWire"; `AROS_PW_DATA_LOOPS=1` to see it), and fails
  any run whose AROS log shows a crash.

## Rules

- **No game data in the repository, ever**: no original files, decoded art, script dumps,
  disassembly listings or recordings. They live in the ignored paths (`.gitignore`) and are
  rebuilt from the user's copy with `make data SRC=...` (or `LRVNE_MEDIA=<dir>`). The location
  of the media is given on the command line or in the environment, never written into a
  tracked file. Docs quote the game only as far as needed to identify things.
- Generic engine, games as profiles: no game's name or file names in `source/engine` or in the
  Makefile's defaults. What differs between games is a key of the profile (typed, validated,
  few, documented in `docs/profile.md`) or code in a native module under `source/games/<game>/`
  that the profile enables.
- Layering: engine above lrvne, lrvne above the host; the engine keeps its global `xe` where
  that mirrors the original program and calls lrvne directly.
- Every test compares against data from the original game or a verified reference tool, never
  against the engine's own output.
- Licence GPL-3.0-or-later (SPDX header in every own source file); third-party code keeps its
  own.
- Research documents mark claims **verified** / **inferred** and cite code addresses; keep that
  when adding to them, and fix a document when the engine work proves it wrong.
- Python tools run as `python3 -I tool.py` (lrvne's as `python3 -I source/lrvne/tools/lrvne
  TOOL ...`).
- Commit after each coherent step; private repository (keep names, hosts and home paths out of
  tracked files).
- `source/lrvne` is an ordinary git submodule (a clone of the public repository, its own git
  directory in `.git/modules/source/lrvne`), pinned at a tagged lrvne release where there is one
  (now `v0.2.0`). It does **not** share the lrvne repository's settings: a fresh clone commits
  under this repository's identity. Before committing in it, give it lrvne's pseudonymous
  identity (`git config --local user.name/user.email`), its SSH push URL and key
  (`remote.origin.pushurl`, `core.sshCommand`), the private `.git/info/leak-patterns` in its git
  directory, and its hooks (`scripts/check-leaks.sh install`). Rather fix lrvne in its own
  repository and bring the commit in; either way run its `make test SAN=1` and
  `scripts/check-leaks.sh tree` first.
- Moving the pin: `git -C source/lrvne fetch --tags`, `git -C source/lrvne checkout <tag or
  commit>` (a local commit not yet on GitHub: fetch it from the local lrvne repository; a fresh
  clone of this one cannot get it until it is pushed), then `make TARGET=host test`, `make
  TARGET=host e2e` (control mode audits each frame's dirty rectangle) and the SAN run, and commit
  `source/lrvne` here as "lrvne at <commit>: ...".
