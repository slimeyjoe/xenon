# Xenon ～夢幻の肢体～

A new engine for *Xenon ～夢幻の肢体～* ("Xenon: the limbs of phantasmagoria"), a Japanese visual
novel published by C's Ware in 1994 for the NEC PC-9801. It runs the original game's scripts,
pictures and music unchanged, and it reproduces the PC-98 version faithfully, timing and
controls included. You can play it in English (the default), Polish or the original Japanese.
The voices of the 1997 Windows release are optional. The engine builds for desktops with SDL3
(Linux, macOS), for AmigaOS 3.x (68040), for WarpOS (PowerPC) and for AROS (x86_64).

**Adults only.** The game contains explicit sexual scenes, some of them non-consensual.

This is an unofficial fan port. It contains none of the original program code, and **it does
not include the game itself**. You need your own copy of the PC-98 release; the build turns it
into the data file the engine plays.

## What you need

* **The original PC-98 game**: the five floppy disk images (D88, FDI, ...), a hard-disk image
  (HDI, NHD) or the installed files. Every file is checked against the release's inventory, so
  any complete copy works. [docs/game-data.md](docs/game-data.md) lists what is accepted.
* **Python 3** (the standard library is enough to import the game and build the pack).
* **A C99 and C++14 compiler, make, pkg-config and SDL3** for the desktop build.
* *Optional:* the Windows 95 release's `XENON.DAT` and **ffmpeg**, for the voices.

## Quick start

All `make` commands run in `source/`, and everything they build goes to the top-level `build/`.

```sh
cd source
make data SRC=~/xenon/disks     # import your copy of the game (a directory, images or a zip)
make pack                       # build/xenon.pak: the game's data, fonts and all translations
make TARGET=sdl3                # build/sdl3/xenon
cd ..
build/sdl3/xenon --pak build/xenon.pak
```

Without `--pak`, the game looks for `xenon.pak` next to the executable, then in the current
directory. Saved games go to `$XDG_DATA_HOME/xenon`, by default `~/.local/share/xenon`,
on macOS as well.

`make data` also derives pictures, script listings and recordings for the tests and the
reverse-engineering notes. It needs extra Python modules for those (Pillow, unicorn), and it
skips a part with a note when one is missing. You don't need them to play.

### Voices (optional)

```sh
cd source
make voice-pack WINDAT=/path/to/windows/XENON.DAT    # build/xenon_voice.pak, ~48 MB (Opus)
```

The game finds `xenon_voice.pak` next to `xenon.pak` and plays the Japanese voices with every
language. The 68k Amiga build can't decode Opus and needs `CODEC=ldp6` (261 MB). See
[docs/voice.md](docs/voice.md).

## Options

The most useful options of the desktop build:

| Option | What it does |
|---|---|
| `--lang en\|pl\|ja` | language of the text (default `en`); Japanese needs no system fonts |
| `--fullscreen` | start in fullscreen |
| `--scale integer\|aspect\|fit\|stretch` | `integer` (default): sharp whole-number scaling. `aspect`: the same with the PC-98 monitor's 4:3 proportions. `fit`: the largest 4:3 picture that fits, smoothed. `stretch`: fill the window |
| `--window WxH` | initial window size (default `1280x800`) |
| `--pak FILE`, `--storage DIR` | where the data file is, where saves go |
| `--no-voice`, `--voice-volume N`, `--music-volume N` | voices off, or the balance (1..100). The music plays at 50 while voices are on |
| `--voice-strict` | Return waits for the voice to finish instead of cutting it (as on Windows) |
| `--no-audio` | silent |

`build/sdl3/xenon --help` lists all of them. The comment at the top of
[source/backends/sdl3/main.c](source/backends/sdl3/main.c) explains each.

## Controls

| Key | Action |
|---|---|
| Cursor keys, numpad 8 / 2 | move the highlight in a menu |
| Return, Space | choose; turn the page |
| Esc, numpad 0 | cancel in a menu; on a page of text, turn the page |
| F1 | the PC-98's HELP key: opens the system menu from a story choice |
| Ctrl (hold) | fast-forward: pages, pauses and voices are skipped until the next choice |
| Caps Lock | Japanese text appears a page at once instead of letter by letter (the PC-98's KANA lock) |
| F11, Alt+Return | toggle fullscreen |
| F10 | cycle the scale modes |
| Shift+Esc | quit at once, without asking (unsaved progress is lost) |

**Mouse:** as on the PC-98, only movement counts, not position: moving up or down steps the
highlight. Left button = Return, right button = Esc.

The original game has a few habits that surprise new players, and the port keeps them:

* You can save only where the story offers it, usually at the start of a chapter (four slots).
* The system menu (back to the title, quit) opens only while a choice is on screen. Press
  Esc, right-click or press F1 there.
* SPECIAL MODE (gallery, music player) unlocks after all seven endings.

The player's guide in
[source/backends/amiga/readme/controls.txt](source/backends/amiga/readme/controls.txt) explains
the menus, saving and these habits in full. It is written for the Amiga, but the game behaves
the same everywhere.

## Other platforms

| Target | Build | Notes |
|---|---|---|
| AmigaOS 3.x, 68040 | `make TARGET=amiga` | vbcc 0.9h + vasm; Paula or AHI; AGA, ECS or Picasso96 |
| WarpOS (PowerPC) | `make TARGET=warpos` | vbcc 0.9h, WarpUP 5.x |
| AROS x86_64 | `make TARGET=aros` | the AROS cross gcc |
| host (no display) | `make TARGET=host` | the headless backend the tests drive |

`make TARGET=amiga|warpos|aros dist` builds the release archive (`.lha`, needs `lha`, plus
`make data` first for the icons). The archive holds the program, its icons and the player's
ReadMe, but not `xenon.pak`. The cross toolchains are not part of this repository. Each
`source/config/<target>.mk` names what it expects. The Amiga-family backend, its options and its
design are in [source/backends/amiga/README.md](source/backends/amiga/README.md). The player's
ReadMe (requirements, installation, tool types) is assembled from
[source/backends/amiga/readme/](source/backends/amiga/readme/).

## Tests

```sh
cd source
make TARGET=host test     # conformance tests (Python 3 with numpy and Pillow)
make TARGET=host e2e      # plays the game's routes headless, a few minutes
```

[source/tests/e2e/README.md](source/tests/e2e/README.md) covers the end-to-end tests, including
the SDL3, Amiga (WinUAE) and AROS runs.

## Where things are

| Path | What |
|---|---|
| `source/engine/` | the portable engine: script VM, graphics, text, PMD music and FM synthesis, voice |
| `source/hal/`, `source/backends/` | the platform interface and its implementations (SDL3, Amiga family, null) |
| `source/tools/` | game import, pack and voice-pack builders, font converter |
| `source/lang/` | per-language engine strings and how languages are packed ([README](source/lang/README.md)) |
| `text/` | the English and Polish translations, translators' guides and tools ([README](text/README.md)) |
| `docs/re/` | reverse-engineering notes on the original engine and data ([README](docs/re/README.md)) |
| `resources/` | our own tables (file inventory, music and voice cue indexes); `make data` writes the derived data here |
| `re-tools/` | research tools: disassemblers, decoders, the PMD emulator |

`make clean` removes `build/`. `make data-clean` removes everything made from the game outside
`build/`. `make distclean` does both and leaves only the committed files.

## Credits

The story, text, pictures, music and sound effects of *Xenon* are © 1994 C's Ware. This port is
not affiliated with or endorsed by C's Ware. The English translation comes from the
[Xenon PC-98 translation project](https://github.com/OldCorpo/Xenon-PC98-Tools). The Polish
translation and the engine were made for this port. Bundled third-party code is in
`source/third_party/`: [ymfm](source/third_party/ymfm/LICENSE) and
[libopus](source/third_party/opus/COPYING). The bundled fonts are in `source/fonts/x11/`. Full
credits are in
[source/backends/amiga/readme/credits.txt](source/backends/amiga/readme/credits.txt).
