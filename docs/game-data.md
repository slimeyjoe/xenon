# The game's data

The repository holds the engine, the tools, the reverse-engineering notes and the translation.
It does not hold the game itself. The pictures, scripts, music and program files of "Xenon
～夢幻の肢体～" are C's Ware's (1994), and the status of their rights is unknown. So is everything
made from them: decoded pictures, script listings, music recordings, disassembly listings and
test screenshots. You supply them from your own copy of the original PC-98 release. The tools
check every file against the inventory of the release, and the pack built from them is the
same, byte for byte, whichever copy it came from. The alternative is to use a `xenon.pak`
someone else built for this version of the engine.

## Getting it in

```sh
cd source
make data SRC=~/xenon/disks            # a directory with the five floppy images, for example
make pack                              # build/xenon.pak
```

`SRC` can be any mix of these (several are combined; quote paths with spaces):

| Source | Notes |
|---|---|
| D88 floppy images (`*.d88`) | the usual dump of the five 2HD disks; also Anex86 FDI and raw images (`*.fdi`, `*.hdm`, `*.xdf`, `*.dup`) |
| hard-disk images (`*.hdi`, `*.nhd`) | Anex86 HDI, T98-Next NHD: the DOS partitions are found through the PC-98 partition table; the game may be in any directory. For any other image, `tools/gamedata.py import IMAGE --offset N` reads the FAT volume at byte N |
| a zip of such images | only its image members are read; one member: `ZIP::path/in/zip.d88` |
| a directory | the game's files anywhere below it, in any letter case (an installed copy, an emulator's host drive), and the images and zips in it |

The file systems (FAT12/16, the PC-98's 1024-byte sectors included) are read by
`source/tools/gamedata.py` itself, so you need Python 3 and nothing else. A file is taken only when its
name and md5 are in `resources/inventory.tsv`. It goes to `resources/original/game/` (or
`floppy_only/` for the boot files and saves of disk A) with the release's date. Files of other
releases or patched copies are named in the report and left out. The report:

```
  pack inputs       283/283
  other game files   13/13
  HDD install only    0/3  missing: AUTOEXEC.BAT CONFIG.SYS XEN.GDT
  floppy only         7/7
gamedata: .../resources/original: every file the pack is built from is there
```

**pack inputs** must all be there: the pictures, animations, scripts, music, effects,
`AGS.EXE` (its font) and `PMD.COM` (its effect table). **HDD install only** are files of
the hard-disk install the inventory was made from that no disk has. No tool needs them.
`make data` exits with an error unless the pack inputs are complete. `make data-check` (and
`make pack`, before it builds) verifies `GAMEDIR`, default `resources/original/game`.
`make data-clean` removes everything made from the game outside `build/` (the imported
files, the derived resources, `text/view/`, the e2e goldens' local pictures); `make distclean`
also does `make clean`, which leaves only the committed files.

The voice pack (`docs/voice.md`) comes from the Windows 95 release, given directly:
`make voice-pack WINDAT=…/XENON.DAT`.

## What `make data` also derives

After the import, `make data` makes the resources that the reverse-engineering documents
(`docs/re/`) refer to, and that the tests compare against. It uses the research tools in `re-tools/` and
writes into `resources/`, where git ignores them. The table is `DATA_PARTS`; the default is
all of them.

| Part | Output | Tool | Needs |
|---|---|---|---|
| `images` | `resources/images/` PNGs, `composite/` | `gdt.py` | Pillow |
| `anim` | `resources/anim/` GIFs, frames, `blink/blink_timing.tsv` (the e2e tests), `0100B/28.png` (the Amiga icons) | `adt.py` | Pillow |
| `scripts` | `resources/scripts/` bytecode, listings, strings, tables | `ccdis.py` | — |
| `sfx` | `resources/sfx/blobs/` | `pmdinfo.py --efc` | — |
| `music` | (checks `resources/music/index.tsv`) | `pmdinfo.py --tsv` | — |
| `saves` | `resources/saves/*.json` | `savedump.py` | the saves (floppy disk A) |
| `vgm` | `resources/music/vgm/`, `resources/sfx/vgm/`: the reference recordings of PMD | `pmdemu.py` | unicorn |

A part whose module is missing is skipped with a note. Give a Python that has the module with
`DATA_PY`:

```sh
python3 -m venv ~/.venvs/xenon-re && ~/.venvs/xenon-re/bin/pip install unicorn pillow numpy capstone
make data SRC=… DATA_PY=~/.venvs/xenon-re/bin/python       # about a minute
make data-derive DATA_PARTS="vgm" DATA_PY=…                # one part again, no import
```

What needs what:

* `make pack`, `make voice-pack`: the import only.
* `make test`: the import. The OPN and PMD comparisons also need the `vgm` part; without it
  they are skipped with a note.
* `make e2e`: the `anim` part (the blink timing).
* Translating (`text/`): the import. The dictionaries' keys point at the original script in your
  game's files (`source/tools/tlkeys.py`); `make text-view` writes them out with the Japanese
  into `text/view/`, and the translators' tools read the Japanese from the game the same way.
  `lint_translation.py --line` also uses the `scripts` part.
* The Amiga-family icons (`xenon.info`, `drawer.info`) are made by the build from
  `anim/0100B/28.png`. Without that frame the build leaves them out with a note, and
  `make dist` stops.

## What stays committed

In `resources/` only tables of our own remain:

* `inventory.tsv`: every file of the release (name, size, date, disk, kind, md5). The import
  checks against it.
* `music/index.tsv` and `sfx/xen_se_efc.tsv`: columns made by `pmdinfo.py` plus hand-made
  ones (which scripts use a song or an effect, music-mode titles, notes). `make data` compares
  the tool's columns with the tool's current output and reports them as stale when the two
  differ. In that case regenerate those columns and keep the others.
* `voice/voicemap.tsv` and `overrides.tsv`: the voice cue table. A cue names its line by
  script and offset, plus a CRC-32 of the line, not its text. `re-tools/voicemap.py --show`
  prints the lines from your scripts.

The translations in `text/translation/` follow the same rule: each entry points at its piece of
the original script (script, string offset, segment, CRC-32) instead of quoting it.

Elsewhere:

* `docs/re/listing/` keeps the naming files (`names.tsv`, `pmd-names.tsv`,
  `ghidra_seeds.tsv`). The disassembly listings made from them are local
  (`docs/re/README.md` "Regenerating").
* The e2e goldens are hashes (`source/tests/e2e/golden.tsv`). The pictures are local copies,
  made by the first run (`source/tests/e2e/README.md`).
* The release archives (`make TARGET=amiga|warpos|aros dist`) hold the program, its icons and
  the ReadMe, not the pack.

## Verified (2026-11-13)

* Importing the five D88s (as a directory, a zip, or member by member) gave 303 files,
  byte-identical to the mtools extraction of `docs/re/media.md` §3.2.
* Importing `xenon_j.hdi` gave the 299 files of the HDD install. The same data in an NHD
  header, and read with `--offset`, gave the same files.
* The main pack, the ldp6 voice pack and the Opus voice pack were built from the import. With
  the former cue table, each is byte-identical to the one built from the formerly committed
  files. With the current table they differ only in the cue table's md5 in their `meta`
  entries.
* The derived resources are identical to the formerly committed ones, or equal when decoded
  (PNG/GIF compression only). The one exception is the image index, which lacks the stub row of
  the HDD-only `XEN.GDT` after a floppy import.
* The Amiga, WarpOS and AROS icons are byte-identical to the formerly committed ones (Pillow 12
  and 13).
