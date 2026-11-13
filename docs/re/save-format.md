# Save slots (`SAVEn.DAT`) and progress (`XEN.GDT`)

Writer: `op_save` 088D:126A (script opcode 0x19). Reader: `op_load` 088D:1441 (opcode 0x1C).
Decoder: `re-tools/savedump.py`. Decoded originals: `resources/saves/*.json`.
All statements below are **verified** in code and on the four original saves from floppy A
(`resources/original/floppy_only/SAVE1-4.DAT`, 391 bytes, 1994-12-22/23) and on HDD-mode saves
written by the engine under emulation (381 bytes).

## 1. File name

The opcode's string operand (at bytecode offset `arg+3`) is the base name; the engine appends
`.dat` (`strcpy` + `strcat(".dat")` when saving, `sprintf("%s.dat")` when loading). The save/load
scripts use `save1` … `save4` (see script-catalog.md for L.CC and the in-story save points).

Files are opened with `xfopen` (engine.md §5.2): current directory first, then any drive. When
loading, a missing file gives 「データがセーブされていません」 (no data saved) and the opcode returns
without effect.

In floppy mode the writer first checks drive A (`drive_to_daua('A')` + `INT 1Bh` sense):
status `10h` → 「ディスクへ書き込みができません」 (write-protected), `60h` → 「ディスクをセットして下さい」,
retry until writable. Saves therefore always go to disk A in floppy mode. HDD mode skips the check.

## 2. Layout

| Offset (floppy) | Offset (HDD) | Size | Source | Content |
|---|---|---|---|---|
| 0x000 | 0x000 | 1 | `fputc(0x0B)` | magic `0B` — the loader aborts silently on anything else |
| 0x001 | — | 10 | `get_volume_label(1)` | **floppy mode only**: volume label of the disk in drive 2 (`_ADISK_`…`_EDISK_`, NUL, 2 junk bytes) |
| 0x00B | 0x001 | 256 | DS:00F2 | the whole flag block: 2048 one-bit flags, flag n = bit `n & 7` of byte `n >> 3` (scripts.md §4.5) |
| 0x10B | 0x101 | 40 | DS:137E | current script file name, e.g. `s0305a.cc` |
| 0x133 | 0x129 | 2 | DS:1294 | current page number, u16 LE |
| 0x135 | 0x12B | 2 | opcode arg | bytecode offset of the save opcode — written, read back, then **discarded** |
| 0x137 | 0x12D | 40 | DS:13C8 | music state block; starts with the current `.M` name |
| 0x15F | 0x155 | 40 | DS:1422 | graphics state block; starts with the current base GDT name |
| 0x187 | 0x17D | — | | end: 391 bytes (floppy) / 381 bytes (HDD) |

The two variants are **not interchangeable**: the loader reads the 10-byte label only when running
in floppy mode, so a floppy save loaded by the HDD version (or vice versa) is misaligned by 10 bytes.

## 3. Load procedure (088D:1441)

1. Name: from the operand. (A second path takes the name from DS:13AC when DS:06E6 == 1, but
   nothing in the binary ever sets DS:06E6 to 1 — it is only tested and cleared — so that path is dead.)
2. `fgetc != 0x0B` → close and return.
3. Floppy mode: read the stored label, verify it is one of `_ADISK_`…`_EDISK_`
   (table DS:01F2) — else 「ゲームディスクではありません」; then loop asking for that disk in drive 2
   (`prompt_insert_disk` 0B7D:0808: 「Ｘディスクをドライブ２へセットして下さい」) until
   `get_volume_label(1)` matches (else 「ディスクが違います」). All label and script-name comparisons in
   `op_load` use `stricmp` (0000:4237), so they are case-insensitive.
4. Read the flag block straight into DS:00F2, the script name, the page, the (ignored) offset.
5. If the saved script name differs from the running one: unload (`088D:030E`), load the saved
   script (`088D:0000`), copy the name to DS:137E.
6. Read the music and graphics blocks into locals. If the graphics block's first byte is non-zero,
   load and show that GDT (`0C2A:0315` + `0C2A:0649`). If the music block's first byte is non-zero,
   load and play that music (`0B2C:01C4` + `0B2C:02E5`).
7. Resume: `pc = find_page(DS:1294)` (`088D:0ECD`), store it in DS:12FC and set DS:00AE = 1
   (interpreter "jump pending"). Execution restarts **at the beginning of the saved page**, not at
   the save opcode.

## 4. The zeroed first byte

In every save examined the first byte of both state blocks is `00`, leaving remnants such as
`\0enon_22` (= `xenon_22`) and `\0aku2` (= `waku2`). Cause (**verified**): the screen-clear/window
opcode handler 088D:1901 and the game start (0ABA:0395) store `0` into DS:1422[0], and the music
code does the same for DS:13C8[0] at 0B2C:00DF and 0B2C:037E (music stop / fade paths, see sound.md). The save points in the
scripts sit after such a clear, so nothing is restored from the blocks and the resumed page redraws
its own background and music. The remaining 39 bytes of each block are unrelated state that happens
to follow the name buffers in DGROUP (music block: `g_music_started` at DS:13D7, then the far pointers `0000:2A7B`, `0000:324D`,
`0000:3A1F`, `0000:41F1` of the four 32 KB graphics plane buffers at DS:13DC.., so the values depend on
where DOS loaded the program; see sound.md / graphics.md).

## 5. Original floppy saves

| Slot | Disk label | Script | Page | Flags set |
|---|---|---|---|---|
| SAVE1 | `_DDISK_` | s0305a.cc | 2 | F100 F124–F129 F132–F145 F147–F149 F151 F152 F155–F158 |
| SAVE2 | `_DDISK_` | s0302a.cc | 0 | F100 F124–F129 F132–F138 F152 |
| SAVE3 | `_EDISK_` | s0305a.cc | 0 | same as SAVE1 |
| SAVE4 | `_CDISK_` | s0202.cc | 3 | F100 |

(flag meanings: resources/scripts/flags.tsv and script-catalog.md).

## 6. `XEN.GDT` — progress bitmask

Not an image. One byte, read at every return to the title (`game_main` 0ABA:0227) into DS:06E8 and
OR-ed by opcode 0x37 `<int n>` (088D:1C19): `value |= 1 << (n-1)`, n = 1..7 (out-of-range n leaves
the value unchanged but still rewrites the file). The read buffer is pre-set from DS:021A (0). If the
file cannot be opened for reading or for writing ("wb", e.g. write-protected disk A) the opcode shows
「ファイルがオープンできません」 via `error_box` and returns FFFFh as the next script position
(088D:1C7A) — the interpreter then continues from a bogus offset (**inferred** consequence; never
happens with a writable disk).

| Use | Condition |
|---|---|
| New game starts `S00` | bit 0 clear |
| New game starts `S00B` | bit 0 set and (bit 1 clear or bits 2–5 all clear) |
| New game starts `S00C` | bit 0 set, bit 1 set, any of bits 2–5 set |
| Omake (CG gallery `K.CC`, music mode `M.CC`) | all bits 0–6 set (`& 0x7F == 0x7F`) |

The HDD image ships `00`; the original owner's floppy A holds `63h` (bits 0, 1, 5, 6). Which story
events set which bit: see script-catalog.md.
