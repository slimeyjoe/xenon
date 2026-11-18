# Xenon ～夢幻の肢体～ (C's Ware, PC-9801, 1994) — reverse engineering notes

Scope: the original PC-98 engine `AGS.EXE` and all game data, documented as input for deciding
whether and how a faithful port could be made. No port work is done here.

Source material: the untouched Japanese HDD image `~/repos/Xenon-PC98-Tools/game/xenon_j.hdi` and the
five original 2HD floppies (D88 images from the 1994-12-09 dump archive in `~/Downloads`), which hold
byte-identical game files (media.md). The translation project
`~/repos/Xenon-PC98-Tools` (read only) was used for context.

## Documents

| Document | Contents |
|---|---|
| [engine.md](engine.md) | Binary layout and segment/module map, boot (HDD and floppy), `main`, init/shutdown, title / omake / run loop, XEN.GDT progress, built-in sequences, disk & file layer, message printer (control codes, kinsoku, paging), choice menus, system menu |
| [scripts.md](scripts.md) | `.CC` container (LCZ header + LZSS), token encoding, grammar (pages, IF, SEN menus, labels), interpreter architecture, flags, text semantics, quirks |
| [opcodes.md](opcodes.md) | Every opcode: byte, operands, semantics, handler address, callees; original command names where known |
| [script-catalog.md](script-catalog.md) | Every script file, the route graph S00 → DREAM_01..04 → endings, progress bits, K/L/M (gallery, load, music mode), flags overview |
| [graphics.md](graphics.md) | PC-98 video model, GDT ("DA1") format and both codecs, palettes, diff/bust/eye overlays, ADT animations, blink animator, effects/fades, screen layout, graphics API; §7 requirements for a faithful renderer |
| [sound.md](sound.md) | PMD 4.7g interface (INT 60h), engine sound API, `.M` format, `XEN_SE.EFC` effects, music and effect inventories, dead MIDI/FM paths; porting notes and an assessment of audio options (sequencer, YM2203 cores, fmgen/fmgon/libvgm/ymfm licences) |
| [platform.md](platform.md) | Input (keyboard, mouse, CTRL skip, emergency quit), vsync timing, interrupts, memory, hardware checks, seg 0DCA display library; §6.1 `video_init` hardware meaning, refresh rates (56.42 / 70.10 Hz), PC-98 references |
| [font.md](font.md) | Seg 0F16 glyph layer: kanji-ROM access, ANK cache, emboldening, opaque cells and colours, double-size glyphs, the KANA-lock typing delay, user character 7621h (page prompt) |
| [pmd-driver.md](pmd-driver.md) | PMD.COM 4.7g driver: resident/installer layout and memory map, work-area variables, board detection (26K/86), register access and waits, IRQ/PIC/vector, interrupt handler and Timer A/B, complete INT 60h API with gating/deferral, fade/skip/hot keys, timing model, FM effects and the built-in SSG effect table, all options (incl. undocumented), `/R` removal |
| [pmd-sequencer.md](pmd-sequencer.md) | PMD sequencer: song start and header parsing, the 5Ch-byte part work struct, clock order and loop counting, interpreter pseudo-code, gate/ties, all 75 command bytes (with Xenon usage counts), F-number/SSG period tables, volume→TL, SSG envelopes, LFOs, voice records, portamento, FM3 extended mode, fade, effect streams; verified register-for-register against the real driver |
| [save-format.md](save-format.md) | `SAVEn.DAT` layout (HDD 381 B / floppy 391 B), load procedure, `XEN.GDT` bitmask |
| [other-versions.md](other-versions.md) | Floppy vs HDD files; the FM Towns / PC-9821 CD (1995) and Windows 95 (1997) releases: disc layout, archive formats (`arc3`, Towns archives), scripts, `CDBR` / BMP pictures, PMD / MIDI music, `ldp` and Towns `.SND` voice, CD audio, voice comparison |
| [media.md](media.md) | Distribution media: D88 floppy format and the five disks (FAT12, volume labels, boot disk), Anex86 HDI header and partition, extraction procedure and verification |
| [functions.md](functions.md) | Index of all 472 functions (address, name, module, size, callers, purpose, covering document); Borland RTL module map of seg 0000; dead code |
| [globals.md](globals.md) | DGROUP (seg 0F9F) memory map: every global with size, initial value, meaning, readers/writers, save-file membership; all DGROUP strings; pointer tables |
| [listing/](listing/) | `names.tsv` — all function/global names of AGS.EXE with comments; `pmd-names.tsv` — the same for PMD.COM; `ghidra_seeds.tsv` — Ghidra's function starts. From them and the game's executables `re-tools/agsdis.py` makes (not committed: they are the game's code, "Regenerating" below) `ags.asm`, the complete annotated disassembly (19 767 instructions, 472 functions, all named), `funcs.tsv`, the function list with caller counts, and `pmd.asm` / `pmd-funcs.tsv` (7 084 instructions, 393 functions) |

## Extracted resources (`../../resources/`)

The game's files and everything made from them are not committed (C's Ware's work, 1994).
`make data SRC=...` (in `source/`; `source/tools/gamedata.py`, [../game-data.md](../game-data.md))
imports the files from your copy of the original release, checks each against `inventory.tsv`,
and derives the rest with the tools named here. Committed are only the tables of our own:
`inventory.tsv`, `music/index.tsv`, `sfx/xen_se_efc.tsv` (tool columns plus hand-made ones; `make
data` reports them when the tools' output has moved on) and `voice/voicemap.tsv`, `overrides.tsv`.

| Path | Contents | Produced by |
|---|---|---|
| `original/game/` | the 299 game files of the HDD install `xenon_j.hdi` (DOS system files omitted); the floppies give 296 of them, all but its own `AUTOEXEC.BAT`, `CONFIG.SYS`, `XEN.GDT`, which no tool needs | `gamedata.py import` |
| `original/floppy_only/` | `STARTUP.COM`, floppy `CONFIG.SYS`, floppy `XEN.GDT`, the original owner's `SAVE1-4.DAT` | `gamedata.py import` (from the floppies) |
| `inventory.tsv` | every file: size, date, floppy (A–E), kind, md5 (committed; the import checks against it) | — |
| `images/` | 203 PNGs (one per GDT, indexed colour, cropped to the image rectangle), `index.tsv`, `composite/` (60 overlays composed the way the engine draws them) | `re-tools/gdt.py` |
| `anim/` | the 6 ADT animations as GIF + frame PNGs + timing; `blink/` eye-blink animations | `re-tools/adt.py` |
| `scripts/` | `decompressed/*.bin` bytecode, `listing/*.txt` structured disassembly, `strings/*.tsv`, `assets.tsv`, `flags.tsv`, `graph.tsv`, `summary.tsv` | `re-tools/ccdis.py`, `source/tools/lzss.py` |
| `music/index.tsv` | 31 `.M` files: parts, tempo, length/loop, titles, usage | `re-tools/pmdinfo.py` |
| `music/vgm/` | reference YM2203 register logs (VGM: intro + pass 1 + pass 2, looping over pass 2) of all 31 songs, recorded from the real PMD.COM under emulation | `re-tools/pmdemu.py`, checked by `re-tools/vgmverify.py` |
| `sfx/` | `XEN_SE.EFC` effect table and split effect blobs; `sfx/vgm/` VGM recordings of effects 1–50 | `re-tools/pmdinfo.py --efc`, `re-tools/pmdemu.py` |
| `saves/` | the four original saves and the floppy `XEN.GDT` decoded to JSON | `re-tools/savedump.py` |

## Tools (`../../re-tools/`, Python 3 + Pillow/numpy; run with `python3 -I`)

The research tools. Four decoders the port's build uses too live with the build in
`../../source/tools/` (marked below); the research tools import them from there, and
`ccdis.py` takes its tokenizer from `source/tools/cctok.py`.

| Tool | Purpose | Verification |
|---|---|---|
| `agsdis.py` | AGS.EXE (MZ) and PMD.COM (.COM) recursive-descent disassembler (Borland switch tables, DGROUP strings, names) | — |
| `lzss.py` (source/tools) | `.CC` LCZ container + LZSS decompress/compress | all 40 scripts recompress byte-identically |
| `ccdis.py` | script disassembler / exporter / checker | 0 unknown opcodes, 100 % byte coverage on all 40 files |
| `gdt.py` | GDT decoder, composites | identical to the original decoder run under emulation on all 203 GDTs |
| `adt.py` | ADT player and blink animator export | identical on all 89 ADT frames |
| `pmdemu.py` | runs the real PMD.COM (installer + driver) under unicorn with a fake 26K board, loads songs like AGS.EXE, writes VGM + register TSV (needs `pip install unicorn`) | timer periods and loop lengths cross-checked with pmdinfo |
| `vgmverify.py` | expands each VGM by its loop and compares it with an uncut real-driver recording (effective register-state changes) | all 50 effects and 28 songs identical over 3 extra passes; the other 3 songs (01 07 17) vary pass to pass in the real driver too (pmd-sequencer.md §11.1a) |
| `pmdmodel.py` | clean-room Python re-implementation of the PMD sequencer from pmd-sequencer.md; `--check` compares with pmdemu | 0 differing clocks: 31 songs × 9000 clocks (1.65 M register writes), fades, all 50 effects, synthetic tests |
| `pmd_capture.py` | runs a song on the real PMD.COM (`pmdemu.py`) and writes it interrupt by interrupt: each interrupt's clock, kind, status and register writes, and INT 60h calls at chosen interrupts; optionally with another SSG-effect table in place of PMD's | the reference logs of lrvne's PMD module (`source/lrvne/tests/data/pmd`), which it matches exactly |
| `pmdcore_emu.py` | runs PMD.COM's installer, INT 60h API and IRQ handler under unicorn and logs OPN register writes (needs `pip install unicorn`) | installer/API/fade/effect/hot-key scenarios on YM2203 and YM2608 models |
| `pmdinfo.py` | `.M` / `.EFC` structure dump | every part ends where the next begins in all 31 songs |
| `savedump.py` | save slot / XEN.GDT decoder | original floppy saves and engine-written HDD saves |
| `d88.py` (source/tools) | D88 floppy image info / raw sector dump / HDI header (read only) | raw dumps byte-identical to the images used for extraction (media.md) |
| `pmd_mksynth.py` | builds the synthetic PMD test songs `re-tools/testdata/SYNTH1.M`, `SYNTH2.M` (commands Xenon never uses) | rebuilt files are byte-identical to the test data; model check 0 differences |
| `arc3.py` (source/tools) | Windows `XENON.DAT` (`arc3`) list / extract / duplicate-copy check | all 5377 members parse; extraction = per-byte majority over duplicate copies (other-versions.md §2.1) |
| `ldp.py` (source/tools) | Windows `ldp4`/`ldp6` voice clip → WAV | all 4983 clips: block sample counts = header count, blocks end at end of file |
| `townsarc.py` | FM Towns CD archives (`CC GD GT M PCM CDFILE.F0x`) list / extract (`--xor` for scripts) | in all 9 archives the offsets increase, the first member follows the directory and the last ends at the end of the file |
| `voicemap.py` | Windows voice clips → PC-98 messages: writes `resources/voice/voicemap.tsv` (cue table) and a review report (docs/voice.md §1) | 5046 cues; every clip reference of the Windows scripts placed (1 by override); speaker tags agree except 2 reviewed lines |
| `cdbin.py` | BIN/CUE: data track → ISO, audio tracks → WAV (corrects the dump's track 3 start) | ISO extracts with 7z; boundaries at digital-silence runs |
| `ghidra/SetDS.java`, `ghidra/ExportAll.java` | Ghidra headless pre/post scripts (pin DS to DGROUP; export functions, strings, decompilation) used to obtain `listing/ghidra_seeds.tsv` | — |

## The engine in one page

* **AGS.EXE**: a 72 KB Borland C++ 1991 real-mode program, written for this game. It has no
  scripting language source and no compiler built in. It runs 40 precompiled bytecode files and
  draws everything itself.
* **Video**: 640×400, 16 colours from a 4096-colour palette, 4-plane graphic VRAM, two pages,
  56.42 Hz frames (all timing is counted in frames). The
  text layer is used only for the ▼ prompt and error messages. Each GDT image is a rectangle with
  its own palette. Images are decoded into four 32 KB off-screen plane buffers and copied to VRAM.
  - Effects are palette fades (to the image palette, to black or to white), band/centre wipes, a
    dissolve, a shutter, a GDC scroll used as a screen shake, ADT frame animation and an eye-blink
    animator.
  - Screen layout: a frame (WAKU) around a 560×296 CG area at (40,4), with a 4×60-byte message
    window at (80,313).
* **Text**: Shift-JIS from the kanji ROM, drawn into graphic VRAM. Rows are 60 bytes and 18 px
  apart, 4 rows per page, with simple kinsoku. In-string control codes: newline, wait, timed
  auto-advance, keep window.
* **Script VM**: pages (`PAGE n … PEND`), `JMP` to a page, `FILE` to another script,
  `IF Fn==v && …`, and `SEN` choice menus with conditional options and nested menus. 2048 one-bit
  flags are the only state; there is no arithmetic. There are 53 opcodes for graphics, sound,
  waits, save/load, disk changes and progress bits.
* **Sound**: music and effects need the resident PMD driver. The engine copies `.M` / `.EFC` data
  into PMD's buffers and controls playback with INT 60h. All music is FM + SSG (no ADPCM or
  rhythm); the composer is Ryu Umemoto.
* **Input**: keyboard (BIOS INT 18h) or mouse. Commands are up/down/left/right, OK, cancel and
  HELP. Holding CTRL skips waits.
* **Persistence**: four save slots holding the flags, script name and page; loading resumes at the
  start of the saved page. `XEN.GDT` holds 7 completion bits that choose the prologue variant and
  unlock the omake.
* **Floppy support**: the engine searches every drive for each file. In floppy mode, disk changes
  are explicit script opcodes, checked against the volume labels `_ADISK_`…`_EDISK_`.

## Engine and data defects found (relevant to faithful reproduction)

* `M.CC` (music mode) loads `xenon_102`, which does not exist; that entry is silent (sound.md).
* `K.CC` defines PAGE 23 twice, so one CG (`R&M03`) is never shown in the gallery (scripts.md).
* The page-overflow cursor is SJIS `EB9Fh` = user-defined character JIS 7621h, which the engine
  defines at start-up from a 16×16 pattern in DGROUP (a "return key" symbol, font.md §4); a port must
  supply that glyph itself.
* The free-memory check says 550 KB but tests about 433 KB; the DIP-switch/GDC check is dead code.
* Saves are not portable between the HDD and floppy versions (10-byte label field).
* Dead code paths: MIDI (`%s.MMD`, `PUTCM6 LITRA` patch upload), an embedded YM2203 "MUSIC LALF"
  driver, the `@` name insertion, the 0x03 big-text escape, and the DS:06E6 forced-load path.

## Continuing this work

Everything needed to resume is in this repository; no state lives elsewhere except the read-only
inputs listed under "Source material" and an optional Ghidra project (`~/ghidra_projects/xenon`,
recreatable with the scripts in `re-tools/ghidra/`).

### Conventions

* AGS.EXE addresses are `SEG:OFF` relative to load base 0: file offset = `0xE00 + SEG*16 + OFF`;
  Ghidra shows `(SEG+0x1000):OFF`; `DS:xxxx` = DGROUP = segment `0F9F`.
* PMD.COM addresses are CS offsets with ORG 100h (`PMD:xxxx`; file offset = address − 100h).
* Every behavioural claim cites code addresses and is marked **verified** (read in code and/or
  reproduced on data or under emulation) or **inferred**. Unknowns go into each document's
  "Open questions" section and the collected list below.
* Names: `listing/names.tsv` (AGS.EXE) and `listing/pmd-names.tsv` (PMD.COM) are the canonical name
  files (`SSSS:OOOO<TAB>name<TAB>comment`; globals use segment `0F9F`). To rename or add something,
  edit the file and regenerate the listing (below); then grep the docs for the old name.
* Docs describe the original only; porting decisions are deliberately out of scope so far. The one
  exceptions are sound.md §6.1, an assessment of options (data format, sequencer, chip cores and their
  licences) for the audio component, and graphics.md §7, the list of what a renderer must reproduce.
  Both are input for a decision, not a decision.

### External references

* PC-98 hardware and BIOS: *Undocumented 9801/9821 Vol.2*, plain-text files (Shift-JIS) at
  `https://www.webtech.co.jp/company/doc/undocumented_mem/` (`io_disp.txt` display I/O incl. port
  6Ah, `memsys.txt` BIOS work area 0000:04xx–05xx, `io_*.txt` for other devices; `curl` works, convert
  with `iconv -f SHIFT_JIS -t UTF-8`). It has no INT 18h chapter; for BIOS calls use the emulator sources.
* Neko Project II kai (`github.com/AZO234/NP2kai`; a sparse clone of `io/` and `bios/` is enough):
  `io/gdc.c`, `bios/bios18.c` (INT 18h) model the hardware and BIOS.
* MAME (`github.com/mamedev/mame`; fetch single files from raw.githubusercontent.com):
  `src/devices/video/upd7220.cpp`, `src/mame/nec/pc9801*.cpp`.
* Sound-chip and PMD references are listed in sound.md §6.1 and pmd-driver.md.
* None of these are stored in the repository; download them to a scratch directory when needed.

### Environment

* Python 3 with `capstone`, `Pillow`, `numpy` (all tools); `mtools` for disk images; `ndisasm`.
* The PMD emulation tools (`pmdemu.py`, `pmdcore_emu.py`, `pmdmodel.py --check`) also need
  `unicorn`, which is not installed system-wide:
  `python3 -m venv ~/.venvs/xenon-re && ~/.venvs/xenon-re/bin/pip install unicorn capstone numpy pillow`,
  then run those tools with `~/.venvs/xenon-re/bin/python`.
* Ghidra 12 headless (`~/.local/ghidra_12.1.3_PUBLIC/support/analyzeHeadless`) was used only to
  obtain initial function starts (`listing/ghidra_seeds.tsv`): `analyzeHeadless ~/ghidra_projects/xenon
  ags -import AGS.EXE -scriptPath re-tools/ghidra -preScript SetDS.java -postScript ExportAll.java OUTDIR`
  (project paths must not contain a `.` path element).
* Listening to / rendering the VGMs: VGMPlay 0.52.1 with libvgm, built from `~/repos/vgmplay-libvgm`
  (libvgm first, installed to a local prefix, then vgmplay with `CMAKE_PREFIX_PATH` pointing at it;
  binaries in its git-ignored `build/`: `build/vgmplay/vgmplay`, `build/libvgm/bin/vgm2wav`).
  `vgmplay` needs a sound device; `vgm2wav --loops N --fade 0 in.vgm out.wav` renders headless.
  ffmpeg's libgme (Game Music Emu) decoder renders these files as silence (tested: no YM2203 support);
  mpv, VLC and Audacious' console plugin decode VGM through the same library.
* Disk space on `/home` is very tight (1–9 GB free, it fluctuates): do not extract the FM Towns CD image;
  render audio to WAV only in a scratch directory and delete it after analysis (a 2-loop render of all
  songs is ≈ 1.3 GB at 44.1 kHz 16-bit stereo).

### Regenerating / re-verifying

```sh
# media -> files (read-only on the images; see media.md), then everything below that make data
# derives (images anim scripts sfx music saves vgm; DATA_PY = the unicorn venv for vgm)
(cd source && make data SRC=path/to/the/images DATA_PY=~/.venvs/xenon-re/bin/python)
python3 -I source/lrvne/tools/lrvne diskimg --help    # the disk image reader (formerly d88.py)
# scripts
python3 -I source/tools/lzss.py --check resources/original/game/*.CC
python3 -I re-tools/ccdis.py --check resources/original/game/*.CC
python3 -I re-tools/ccdis.py --export resources/scripts resources/original/game/*.CC
# images / animations
python3 -I re-tools/gdt.py -o resources/images --composite-dir resources/images/composite resources/original/game/*.GDT
python3 -I re-tools/adt.py --help
# saves
python3 -I re-tools/savedump.py --json resources/saves resources/original/floppy_only/SAVE*.DAT
# music structure, VGM reference recordings, model check (unicorn venv)
python3 -I re-tools/pmdinfo.py --tsv resources/original/game/*.M
~/.venvs/xenon-re/bin/python re-tools/pmdemu.py --all-music resources/music/vgm
~/.venvs/xenon-re/bin/python re-tools/pmdemu.py --all-se resources/sfx/vgm
~/.venvs/xenon-re/bin/python re-tools/vgmverify.py          # exit 1 on any FAIL
~/.venvs/xenon-re/bin/python re-tools/pmdmodel.py --check resources/original/game/XENON_*.M
~/.venvs/xenon-re/bin/python re-tools/pmdmodel.py --check re-tools/testdata/SYNTH1.M re-tools/testdata/SYNTH2.M
# disassembly listings (not committed; both reproduced the formerly committed files byte for byte)
cat docs/re/listing/ghidra_seeds.tsv docs/re/listing/names.tsv \
  | awk -F'\t' '{a[$1]=$0} END{for(k in a) print a[k]}' | sort > /tmp/names-merged.tsv
python3 -I re-tools/agsdis.py resources/original/game/AGS.EXE /tmp/names-merged.tsv docs/re/listing/ags.asm docs/re/listing/funcs.tsv
python3 -I re-tools/agsdis.py resources/original/game/PMD.COM docs/re/listing/pmd-names.tsv docs/re/listing/pmd.asm docs/re/listing/pmd-funcs.tsv
```

`ghidra_seeds.tsv` holds the function starts found by Ghidra's auto-analysis. They are used only as
weak seeds (traced if no other code covers them); merged with `names.tsv` as shown, the command
reproduces `ags.asm` byte for byte. `functions.md` and `globals.md` were generated once from the name
files and code cross-references and are now maintained by hand: update them when names change.

## Open questions (collected; details in each document)

* Real-time durations assume the 3.9936 MHz OPN clock of the 26K/86 boards (not measured; sound.md,
  pmd-driver.md §6).
* PMD 86-board detection path: meaning of the FD80:0002 = 2A27h ROM signature and port 6Eh; why the
  same-IRQ reset writes reg 27h = 3Fh; PPSDRV register convention (pmd-driver.md §11).
* MML spellings given for rarely used PMD commands follow PMD convention, not the binary
  (pmd-sequencer.md).
* The identity of the dead MIDI tools PUTCM6 / LITRA (sound.md).
* GDT header flag nibble `0x0F00` is never tested (graphics.md).
* Real-hardware confirmation of the `video_init` / BIOS-bit meanings taken from *Undocumented 9801* and
  emulator sources (platform.md §6.1, §8), and of the 56.42 Hz frame rate.
* The 26K board's analog output (FM/SSG balance, filtering): needs a hardware recording (sound.md §7).
* Song + effect interaction (FM3/SSG masking) is not covered by the `pmdmodel.py` check (sound.md §7).
