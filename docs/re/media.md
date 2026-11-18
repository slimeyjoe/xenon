# Distribution media and resource extraction

This document covers the two source images that every resource in `resources/original/` was taken
from. They are the five original 2HD floppies (D88 images) and the hard-disk image `xenon_j.hdi`
(Anex86 HDI). It also gives the extraction procedure and how it was verified. Engine-side disk
handling (drive mapping, volume-label checks, disk-change opcode) is described in
[engine.md §5](engine.md).

**verified** means decoded from the actual bytes with `source/tools/d88.py` (since replaced by
lrvne's `diskimg`, `source/lrvne/tools`; the commands below are kept as they were run) or mtools.
**inferred** means it follows the usual PC-98 or D88 conventions but was not checked against a
specification.

Images used (all read only):

| Image | Location | Size | md5 |
|---|---|---|---|
| Floppy set (zip of D88, HFE, MFI and SCP versions) | `~/Downloads/Xenon - Mugen no Shitai (C's Ware) (1994-12-09) [PC98] [3.5''] [SCP+MFI+HFE+D88].zip` | 146 007 418 | `e29a78a47c121680b487b1cb056feebc` |
| `d88/Xenon - Mugen no Shitai (Disk A).d88` … `(Disk E).d88` | members of that zip; not extracted (they are read straight from the zip) | 1 281 968 each | A `d4fb7953…c6ba`, B `850a5b82…c3ee`, C `3e48d3f1…b6d3`, D `78397643…caed`, E `a388ae99…51a5df9b` |
| HDD image | `~/repos/Xenon-PC98-Tools/game/xenon_j.hdi` (the untouched Japanese image of the translation project) | 7 315 456 | `2e6536faaa38eec20808c268f7590adc` |

The zip also contains HFE, MFI and SCP (flux) dumps of the same disks. Only the D88 files were
used.

## 1. D88 floppy images

### 1.1 D88 container format (**verified** on all five files)

All fields are little-endian. The header is 0x2B0 bytes long:

| Off | Size | Field | Value in all five images |
|---|---|---|---|
| 0x00 | 17 | disk name, NUL padded | `HxCFE` (the images were converted by the HxC Floppy Emulator software, **inferred** from the name) |
| 0x11 | 9 | reserved | zeros |
| 0x1A | 1 | write protect (00 = no, 10h = yes) | 00 |
| 0x1B | 1 | media type (00 2D, 10h 2DD, 20h 2HD) | **20h = 2HD** |
| 0x1C | u32 | disk size (whole file, header included) | 1 281 968 = the file length |
| 0x20 | 164 × u32 | track offsets (from file start; 0 = track absent), index = cylinder × 2 + head | 154 used (77 cylinders × 2 heads); the first is 0x2B0, each next one is 8 320 (8 × (16 + 1024)) bytes further on |

Each track is a run of sectors, and each sector has a 16-byte header followed by its data:

| Off | Size | Field | Value (all 1 232 sectors of every disk) |
|---|---|---|---|
| 0 | 1 | C (cylinder) | = track index / 2 |
| 1 | 1 | H (head) | = track index & 1 |
| 2 | 1 | R (sector id) | 1..8, stored in ascending order (no interleave) |
| 3 | 1 | N (size code, 128 << N) | 3 = 1024 bytes |
| 4 | u16 | sectors in this track | 8 |
| 6 | 1 | density (00 = MFM, 40h = FM) | 00 |
| 7 | 1 | deleted-data mark | 00 |
| 8 | 1 | FDC status | 00 (no errors) |
| 9 | 5 | reserved | zeros |
| 14 | u16 | data size that follows | 1024 |

Geometry: 77 cylinders × 2 heads × 8 sectors × 1024 bytes = 1 261 568 bytes. This is the standard
PC-98 1.2 MB 2HD format. The disks have no copy protection: there are no odd sector sizes,
deleted marks or error statuses (**verified**).

Converting to a raw sector image means concatenating the sector data in (C, H, R) order.
`source/tools/d88.py raw` does this. Its output is byte-identical to the raw dumps
that the extraction used (temporary files of the original session, `A.img … E.img`) (**verified**: all five compared).

### 1.2 File system (**verified**)

All five disks are FAT12 with 1024-byte logical sectors:

| BPB field | Value |
|---|---|
| bytes/sector | 1024 |
| sectors/cluster | 1 (cluster = 1 KB) |
| reserved sectors | 1 (boot sector) |
| FATs × sectors/FAT | 2 × 2 (FAT 1 at 0x400, FAT 2 at 0xC00) |
| root entries | 192 (6 sectors, at 0x1400) |
| data area | 0x2C00 (sector 11), 1 221 clusters |
| total sectors | 1232 |
| media byte | FEh (FAT starts `FE FF FF`) |
| sectors/track, heads | 8, 2 |

Boot sectors:

* **Disk A**: OEM `SPS PC98`, jump `E9 0C 01`. This is the boot loader of the minimal DOS on the
  disk. BPB fields up to offset 0x1A are standard. The bytes from 0x1C on are loader parameters,
  not an extended BPB: they hold the names `IO98    SYS` and `MEGDOS  SYS` (**verified**). Their
  exact layout is **not analysed**.
* **Disks B–E**: OEM `NEC  5.0`, which is the format written by NEC MS-DOS 5.0. They have an
  extended BPB (signature 29h): serial numbers `101B-19CD`, `1A1B-19D0`, `221B-19D3`, `2A1B-19D6`,
  BPB labels `_BDISK_`, `_CDISK_`, `_DDISK ` (sic, with no trailing underscore), `_EDISK_`, and
  type `FAT12`. They are not bootable data disks.

**Volume labels.** These are the root-directory entries with attribute 28h (label + archive). They
are `_ADISK_`, `_BDISK_`, `_CDISK_`, `_DDISK_`, `_EDISK_`. The engine reads these root entries with
`findfirst("B:\_?DISK_", FA_LABEL)` (`get_volume_label` 0843:0404, engine.md §5.3). It never reads
the BPB copy, so the BPB typo on disk D has no effect (**verified**: mtools reports `_DDISK_` from
the root entry).

All files are in the root directory; there are no subdirectories.

| Disk | Files (excl. label) | Bytes used | Free | Contents |
|---|---:|---:|---:|---|
| A | 123 | 1 173 715 | 9 216 | boot system (`IO98.SYS`, `MEGDOS.SYS`, `STARTUP.COM`, `CONFIG.SYS`), `AGS.EXE`, `PMD.COM`, `XENON.BAT`, `HDINST.BAT`, `HDINST2.BAT`, 32 `.CC` (all except `S0302A..S0305B`), all 31 `.M`, `XEN_SE.EFC`, 44 GDT, `XEN.GDT`, `SAVE1-4.DAT`, markers `XEN_A.DAT` / `XEN_A_E.DAT` |
| B | 52 | 1 195 603 | 25 600 | 48 GDT, 2 ADT (`0100B`, …) |
| C | 59 | 1 218 720 | 1 024 | 56 GDT, 1 ADT |
| D | 58 | 1 184 349 | 30 720 | 45 GDT, 3 ADT, `S0302A/B`, `S0303A/B`, `S0304A/B`, `S0305A/B.CC` |
| E | 63 | 1 180 747 | 38 912 | 58 GDT, 2 ADT, `S0305A.CC` |

Each data disk also holds its `XEN_x.DAT` and `XEN_x_E.DAT` markers. These are 7-byte files,
`"x_disk\x1A"`, used only by `HDINST2.BAT` (engine.md §2.1). The 355 directory entries name 305
distinct files. 43 files sit on more than one disk (for example `1010B.GDT` on C and D, and
`S0305A.CC` on D and E), presumably so that a scene's data is on the disk requested by the preceding
`DISK` opcode (**inferred**). All duplicate copies are byte-identical (**verified**). The per-file disk assignment is the
`floppy` column of `resources/inventory.tsv`; a multi-letter value means the file is on several
disks.

Attributes on disk A: `IO98.SYS` and `MEGDOS.SYS` are 27h (read-only, hidden, system, archive).
`STARTUP.COM` is 22h (hidden, archive). Every other file is 20h. `HDINST2.BAT` copies with
`copy *.*`, which skips hidden files, so an installed HDD never receives the three boot files
(**inferred** from DOS `copy` semantics; this matches the HDI, which has none of them).

### 1.3 Boot disk A (**verified** contents; boot flow in engine.md §2.2)

| File | Size | Date | Role |
|---|---:|---|---|
| `IO98.SYS` | 8 755 | 1992-04-15 | kernel I/O module of the minimal DOS ("MEGDOS") |
| `MEGDOS.SYS` | 17 528 | 1992-04-15 | DOS kernel |
| `CONFIG.SYS` | 42 | 1994-06-25 | `files=10` / `buffers=40` / `shell=startup.com` |
| `STARTUP.COM` | 355 | 1994-11-23 | shell: INT DCh hook, runs `PMD.COM /K /M10 /V0 /F` and `AGS.EXE F` |
| `XEN.GDT` | 1 | 1994-12-25 | progress byte, `63h` on this copy (save-format.md §6) |
| `SAVE1-4.DAT` | 391 each | 1994-12-22/23 | the original owner's saves (save-format.md §5) |

`IO98.SYS` and `MEGDOS.SYS` were not extracted: they are third-party system files with no role in
the game. The other floppy-only files are in `resources/original/floppy_only/`.

## 2. HDI hard-disk image (`xenon_j.hdi`)

### 2.1 Anex86 HDI header (**verified**)

The header is the first 4096 bytes of the file: 8 little-endian u32 fields, and the rest is zero.

| Off | Field | Value |
|---|---|---|
| 0x00 | reserved | 0 |
| 0x04 | HDD type | 0 |
| 0x08 | header size (offset of sector 0) | 4096 |
| 0x0C | data size | 7 311 360 (= 210 × 4 × 17 × 512; file = 4096 + data) |
| 0x10 | sector size | 512 |
| 0x14 | sectors per track | 17 |
| 0x18 | heads | 4 |
| 0x1C | cylinders | 210 |

The image is a 7 MB SASI/IDE-style geometry. 1 cylinder = 68 sectors = 34 816 bytes.

### 2.2 IPL and partition table (**verified** values; field meanings per the PC-98 convention, **inferred**)

* **Physical sector 0** (file offset 0x1000): the IPL. It starts with `EB 0A 90 90` and the
  signature `IPL1` at offset 4, and ends with `55 AA`.
* **Physical sector 1** (0x1200): the PC-98 partition table. It has 16 entries of 32 bytes, and
  only entry 0 is used:

| Off | Size | Field | Entry 0 |
|---|---|---|---|
| 0 | 1 | mid: bit 7 = bootable, low bits = system (20h/21h DOS) | A1h (bootable DOS) |
| 1 | 1 | sid: bit 7 = active, low bits = FS type (01h FAT12, 11h/21h FAT16) | 81h (active, FAT12); consistent with the BPB |
| 2 | 2 | reserved | 0 |
| 4 | 1+1+2 | IPL start sector, head, cylinder | 0, 0, 1 |
| 8 | 1+1+2 | partition start sector, head, cylinder | 0, 0, 1 |
| 12 | 1+1+2 | partition end sector, head, cylinder | 0, 0, 203 (the last cylinder, inclusive) |
| 16 | 16 | name | `MS-DOS 6.20     ` |

The partition spans cylinders 1–203, which is 203 × 34 816 = 7 067 648 bytes and exactly equals
the BPB size below. It starts at LBA 68, i.e. file offset 4096 + 68 × 512 = **38 912**.
Cylinders 204–209 (208 896 bytes) are outside the partition. They contain non-zero data, which
was not examined. The rest of cylinder 0 (LBA 2–67) holds further IPL/menu data (not examined).

### 2.3 DOS partition (**verified**)

| BPB field | Value |
|---|---|
| OEM | `NEC  6.2` (NEC MS-DOS 6.20) |
| bytes/sector (logical) | 1024 (two physical 512-byte sectors) |
| sectors/cluster | 4 (cluster = 4 KB) |
| reserved / FATs × size | 1 / 2 × 3 |
| root entries | 768 |
| total sectors | 6 902 (× 1024 = 7 067 648) |
| media | F8h |
| sectors/track, heads, hidden | 17, 4, 68 (hidden is counted in physical sectors) |
| extended BPB | serial `2409-07EE`, label `NO NAME`, `FAT12` |
| clusters | 1 717 → FAT12 |

There is no volume label entry (mtools: "has no label"). The root directory holds 302 files
(4 999 118 bytes, 1 372 160 bytes free):

* the DOS system files `IO.SYS` (65 536, 1995-05-31), `MSDOS.SYS` (39 644) and `COMMAND.COM`
  (54 988), not extracted;
* `CONFIG.SYS` (84 bytes, dated **2001-10-07**: `FILES=8`, `BUFFERS=2`, `FCBS=1`,
  `shell=\COMMAND.COM /P`, `rem DEVICE=\HIMEM.SYS`, `DOS=HIGH`);
* `AUTOEXEC.BAT`, byte-identical to `XENON.BAT` (md5 `0ee17790…`): `echo off`, `cls`,
  `pmd /m10 /v0 /e4 /F /k`, `ags`, `pmd /r > nul`, `cls`;
* all 297 other game files.

**The game is in the root directory, not in `\XENON`.** `HDINST.BAT` would have created
`\XENON` (§3.1). So the HDI was not made with the shipped installer. It is a later hand-made
install: the CONFIG.SYS date and the MS-DOS 6.20 system point to the image's creator, not to
C's Ware (**inferred**). The directory order supports this: `SAYA01_M.GDT` comes first and
`0010.GDT` sits between `SAYA01` and `SAYA02`, which suggests files were re-copied after the
initial copy (**inferred**). The engine needs no particular directory, because `xfopen` searches
the current directory first and then every drive (engine.md §5.2).

HDD vs floppy contents (**verified** by md5, every file):

* 296 files are identical to the floppy copies.
* 2 files differ: `CONFIG.SYS`, and `XEN.GDT` (`00` on HDD, `63h` on floppy A).
* 1 file is HDD-only: `AUTOEXEC.BAT`.
* 7 files are floppy-only: `IO98.SYS`, `MEGDOS.SYS`, `STARTUP.COM`, `SAVE1-4.DAT`.

## 3. Installation and extraction

### 3.1 The original installer (`HDINST.BAT` / `HDINST2.BAT`, **verified** text)

Usage, as printed by `HDINST.BAT`: `hdinst c: a:`. The first argument is the **source** floppy
drive, the second the **destination** HDD drive. When a PC-98 boots from the hard disk, the HDD is
`A:` and the floppies are `C:`/`D:`.

1. `HDINST.BAT %1 %2`: switch to `%2`, `cd\`, `md xenon`, `cd xenon`; copy `%1hdinst2.bat` to
   `%2` (the current directory, `\xenon`); run `hdinst2 %1 %2`.
2. `HDINST2.BAT`: for each disk A–E, prompt 「ドライブ %1 に【ＸＥＮＯＮ】Ｘディスクを入れて、何かキーを押して下さい」.
   It loops until `%1xen_x.dat` exists, runs `copy %1*.* %2`, then checks `%2xen_x_e.dat`. On
   failure it prints 「ＨＤＤへのインストールに失敗しました」.

The installer writes no `AUTOEXEC.BAT` and does not change `CONFIG.SYS`. The user runs
`XENON.BAT` from `\XENON`.

### 3.2 Extraction procedure (reproduces `resources/original/`)

`resources/original/` is not committed. `source/tools/gamedata.py import` (run by `make data
SRC=...`, [../game-data.md](../game-data.md)) fills it from any of these images or from plain
files, with its own FAT12/16 reader (1024-byte logical sectors included, so no
`MTOOLS_SKIP_CHECK`); D88 → raw goes through `source/tools/d88.py`, an HDI's or NHD's DOS partitions
are found through the PC-98 partition table as in §2.2. It keeps a file only when name and md5
are in `inventory.tsv` and gives it the inventory's date:

```sh
cd source
python3 -I tools/gamedata.py import ~/repos/xenon-releases/pc98/d88      # the five D88s
python3 -I tools/gamedata.py import ~/repos/Xenon-PC98-Tools/game/xenon_j.hdi
```

The procedure below is how the files were first extracted, with mtools; it remains a
cross-check of the importer (§3.3).

Tools: mtools 4.x (`mdir`, `mcopy`, `mtype`) and `source/tools/d88.py` (stdlib Python). Nothing is
written to the images. `MTOOLS_SKIP_CHECK=1` is needed because the 1024-byte-sector PC-98 BPBs
fail mtools' sanity checks.

```sh
export MTOOLS_SKIP_CHECK=1
Z="$HOME/Downloads/Xenon - Mugen no Shitai (C's Ware) (1994-12-09) [PC98] [3.5''] [SCP+MFI+HFE+D88].zip"
OUT=/path/to/scratch           # 5 x 1.2 MB

# floppies: D88 -> raw sector image -> files
for L in A B C D E; do
  python3 -I source/tools/d88.py info "$Z::d88/Xenon - Mugen no Shitai (Disk $L).d88"
  python3 -I source/tools/d88.py raw  "$Z::d88/Xenon - Mugen no Shitai (Disk $L).d88" $OUT/$L.img
  mdir -i $OUT/$L.img -a ::/                         # listing incl. hidden files
done
mcopy -n -m -i $OUT/A.img ::/STARTUP.COM ::/CONFIG.SYS ::/XEN.GDT '::/SAVE*.DAT' resources/original/floppy_only/

# HDD: the DOS partition starts at byte 38912 (source/tools/d88.py hdi prints it)
python3 -I source/tools/d88.py hdi ~/repos/Xenon-PC98-Tools/game/xenon_j.hdi
H=~/repos/Xenon-PC98-Tools/game/xenon_j.hdi@@38912
mcopy -n -m -i $H '::/*' resources/original/game/     # then delete IO.SYS, MSDOS.SYS, COMMAND.COM
```

`-m` keeps the DOS timestamps, which `resources/inventory.tsv` records.

### 3.3 Verification (**verified**, 2026-11-07)

* `source/tools/d88.py raw` output is identical to the raw images used (`cmp`).
* All 299 files in `resources/original/game/` match the HDI copies byte for byte
  (`mtype -i xenon_j.hdi@@38912 ::/NAME | md5sum`, 299/299).
* Every file on floppies A–E matches its copy in `resources/original/game/` or
  `resources/original/floppy_only/`. The only exceptions are the documented differences
  (`CONFIG.SYS`, `XEN.GDT`) and the two unextracted system files.
* The md5 of every extracted file is in the `md5` column of `resources/inventory.tsv`.

`gamedata.py import` (2026-11-13, **verified**):

* From the five D88s (a directory, a zip of them, single members): 303 files, byte-identical to
  the mtools extraction (296 `game` + 7 `floppy_only`); it reports the three HDD-only files
  missing, which no tool reads. `make pack` from them is byte-identical to the pack made from
  the HDI's files (md5 `5fc2689e…`).
* From `xenon_j.hdi` (partition at byte 38 912): the 299 `game` files. The same data rewrapped
  in an NHD header, and read with `--offset`, gives the same 299.

## 4. Open questions

* Disk A boot-sector loader parameters (bytes 0x1C–0x3D) and the identity and version of the
  "SPS"/MEGDOS system were not analysed.
* The contents of HDI cylinder 0 after the partition table, and of cylinders 204–209, were not
  examined.
* The flux dumps (SCP/MFI/HFE) were not compared with the D88 files. The D88 files show no read
  errors, so this was not needed for the game data.
