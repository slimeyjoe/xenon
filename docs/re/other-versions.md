# Other releases

The analysis in this directory is of the **PC-9801 floppy release** (5 × 2HD, disk images dated
1994-12-09; engine `AGS.EXE` 1994-11-26), as installed on `xenon_j.hdi`. The HDD install is a
byte-identical copy of the floppy files (**verified**: every game file on the five D88 images
compares equal to the HDI copy, except `CONFIG.SYS`, `XEN.GDT` (progress byte) and the
floppy-only files `IO98.SYS`, `MEGDOS.SYS`, `STARTUP.COM`, `SAVE1-4.DAT`; the HDD adds
`AUTOEXEC.BAT` (identical to `XENON.BAT`) and the MS-DOS 6.20 system files, and holds the game in the
root directory rather than in the `\XENON` directory that `HDINST.BAT` would create — a hand-made
install, **inferred**).
Image formats (D88, HDI), file systems, per-disk contents and the extraction procedure: see
[media.md](media.md).

Two later releases exist and were examined from the dumps in `~/repos/xenon-releases`
(read only): an **FM Towns CD** (1995, which also carries a PC-9821 CD build) and a
**Windows 95 CD** (1997). Both are **voiced** and both re-encode every asset. The pictures are the
PC-98 pictures; the scripts carry the PC-98 text. What they add is voice acting, new music
arrangements, sampled sound effects and (Towns) two Red Book audio tracks.

| | PC-98 floppy (1994) | FM Towns / PC-9821 CD (1995) | Windows 95 (1997) |
|---|---|---|---|
| Engine | `AGS.EXE` (16-bit, Borland) | `AGS.EXP` (386, Phar Lap) / `AGS16.EXE` (16-bit) | `XENON.EXE` (Win32, Office Craftsman engine, a different code base) |
| Container | loose files | 4 archive files + 4 voice archives (§3.2) | one `arc3` archive `XENON.DAT` (§2.1) |
| Pictures | 203 GDT ("DA1") + 6 ADT | `CDBR` codec, same geometry and colours, ADT frames as single images (§3.4) | BMP; CGs cropped to 546×290 (§2.2) |
| Scripts | 40 `.CC`, LCZ/LZSS token bytecode | 43 scripts, XOR 0x7C, 1-byte kana, voice cues (§3.3) | 40 `.sco`, 16-bit word bytecode (§2.3) |
| Music | 31 PMD `.M` for YM2203 (OPN) | PMD re-arrangements: 30 for YM2608 (`XEN86_*`), 29 for the Towns YM3438 (`XENFM_*`) | 20 GS MIDI songs (§2.3) |
| Effects | `XEN_SE.EFC` (FM/SSG) | same EFC + 49 sampled `.PCM` | 51 numbered `ldp` clips: sampled effects 1–50 and 7 others |
| Voice | — | 4080 clips, 11 kHz 8-bit, 4.4 h (§3.6) | 4983 clips, 22 kHz log-DPCM, 5.0 h (§2.4) |
| Other | — | 2 CD audio tracks (§3.7) | 6 `tVid` videos (§2.5), new menu/omake UI |

Tools written for this (stdlib only, `python3 -I`): `source/tools/arc3.py`, `source/tools/ldp.py`,
`re-tools/townsarc.py`, `re-tools/cdbin.py`.

## 1. Sources

| Release | Files | Notes |
|---|---|---|
| FM Towns CD | `fm-towns/Xenon (19xx)(Cs Ware)(Jp).bin/.cue` | 326 584 608 bytes = 138 854 sectors of 2352 bytes. Track 1 MODE1 (sectors 0..94739), tracks 2–3 audio. The cue's track 3 start is wrong (§3.1). ISO 9660 volume `XENON_CD`, publisher `CSWARE`, all files dated 1995-03-16. |
| Windows 95 | `windows/XENON.EXE`, `XENON.DAT`, `XENON.HLP/.CNT`, `XENON.RTF` | A fan-made portable repack ("Patched by AyuanX, 2021-08-17", `_Readme.txt`): `XENON.EXE` (dated 2021) is patched for modern Windows and non-Japanese locales, menus translated to English, the help converted to RTF. `XENON.DAT` (415 035 392 bytes) is dated 1997-11-14, the archive's member timestamps 1997-10-14. Extras: manual scans, the C's Ware walkthrough, a wallpaper from the original CD, a `.reg` with a 100 % save (`nArrival` = 0xFF). |

## 2. Windows 95 release

`XENON.EXE` is an MSVC Win32 program built on Office Craftsman Arts' Windows engine (strings
"By Office Craftsman Arts", window classes `AGEFrameWnd` / `AGEMainWnd` / `AGEMenuBar`, error
texts mentioning `Eve%d.dat`: the code base of C's Ware's *EVE burst error* Windows port).
It has nothing in common with `AGS.EXE`. 640×480 window (or full screen), GDI palette/DIB drawing,
`winmm` MIDI and wave output, fonts MS Gothic / MS PGothic. Settings and saves live in the registry
under `HKCU\Software\C's ware\Xenon` (`bMidiEnabled`, `bWaveEnabled`, `idMidiDevice`,
`idWaveDevice`, `nMidiVolume`, `bFullScreen`, `bDelayedText`, `Disc%dPath`, `nArrival` = the
completion bits, save slots `Data %d`). The help file advertises a software MIDI synthesizer.

### 2.1 `XENON.DAT`: the `arc3` archive (verified, `source/tools/arc3.py`)

Reader: `XENON.EXE` 0x40A320 (open), 0x40A050 (name lookup), 0x40A750 (member open); all
integers big-endian.

* **Header** (0x2E bytes read): `"arc3"`, u32 version (must be ≤ 1; 1), u32 block size (0x800),
  u32 data base block (0x10), …, u32 directory block at +0x18 (1), u32 directory size at +0x1C
  (0x7445 bytes). The opener also notes the drive letter of the path to lock a CD drive.
* **Directory**: a byte stream; the key of a member is extension + base name in lower case
  (`bmp0010`, `ldpryo0041`, `scos0101`). For each byte `b`, `hi = b >> 4`, `lo = b & 15`:

  | Byte | Meaning |
  |---|---|
  | `hi` < 15 | name = name[:hi] + the next `lo` bytes; then u24 block |
  | `F0` | another copy of the current name at the following u24 block |
  | `FF` | increment the last character of the name; then u24 block |
  | `F1`..`FE` | group header: name = the next `lo` bytes; u24 block; u24 offset of the next group header (81 groups; the lookup walks the groups, then scans one linearly) |

  1360 extra copies exist (336 names have them, e.g. `ldp1` ×87, `bmpbast_m01` ×56): the CD
  stored hot files several times, and the lookup takes the copy whose block is nearest the current
  read position. **Copies are not always identical**: in 21 names (20 bust masks / CGs and
  `ldp4`) some copies differ from the rest by 1–4 isolated single-bit flips (136 of 137 flipped
  bytes differ in one bit, mostly bit 7). Same size and timestamp, so these are errors made when
  the archive was written; members stored once may carry flips that cannot be detected.
  `arc3.py extract` takes a per-byte majority over all copies (the first copy is the wrong one for
  7 of the 21 names; 5 bytes are 1:1 ties), and `arc3.py check` lists the affected names.
* **Member** at `(base + block) × 0x800`: a 32-byte header (u32 blocks, u32 size, u32 stored size,
  u32 time, u32 time (Unix, 1997-10), u32 method, padding) then the data. Methods 1 and 2 have
  decoders in the EXE (0x40AC10 …, not analysed); **every shipped member is method 0 (stored)**.

5377 members: 326 `bmp`, 4983 `ldp`, 22 `mid`, 40 `sco`, 6 `vid` (one `bmp`, `st3`, is empty).

### 2.2 Pictures (`bmp`)

Plain Windows BMPs (4-, 8-, 1- and one 24-bit). Compared with the PC-98 GDTs decoded by
`re-tools/gdt.py`:

* **127 CGs are 546×290 4-bit images that are crops of the PC-98 picture**: the PC-98 CG area is
  560×296 at (40,4); Windows keeps screen (47,7)–(592,296), dropping 7 columns left and right and 3
  lines top and bottom. Of the 124 with a PC-98 namesake, 105 match pixel for pixel under a 1:1
  palette-index remap (1 without any remap); the colours of 101 of those 105 are exactly the PC-98
  4-bit values ×16, 4 differ (e.g. `0100`, `FA03`, `FA11`). The other 19 (`0040B`, `1020_3`, `BI06C`..`E`, `MEG01C`,
  `R&M03C`, `R&SC`, …) are diff pictures stored pre-composited, on a base other than the obvious
  one (not matched). There is no higher-resolution or higher-colour art.
* Overlays (`*_M`, eyes/mouths) are 640×400 4-bit full screens; busts (`BI_BAST`…) keep the PC-98
  size (304×288 etc.) with 1-bit masks (`*_BASTM`, `*_BA_BM`); bust names are shortened to two
  letters (`KAI_BAST` → `KA_BAST`, `RYO_BAST` → `RY_BAST`, `RA&TO01` → `RATO01`).
* New: the 640×480 frames `waku*.bmp`, menus/buttons (`on*`, `of*`, `ondata*`, `select*`,
  `menu*`), digits for the save screen (`dt_*`), `title`, `tit_copy`, `start`, `op000`–`op002`,
  `cd01`–`cd04`, `crt01`/`crt02` (600×360), and the `hitball*` / `sball*` pictures of an omake
  game (not traced). Missing compared with PC-98: `0100B` (an animation, see §2.5), `0110`,
  `0120`, `FA10`, `RA04_M`, `RYO02_M`, `ROGO`, `NAME`, `ST00`–`ST09`, `WAKU2`, `XEN`.

### 2.3 Scripts (`sco`) and music (`mid`)

**Scripts**: the same 40 names as the PC-98 `.CC` files. A stream of 16-bit big-endian words, each
stored inverted (XOR 0xFFFF). Words ≥ 0x8000 are opcodes, the rest operands; the interpreter
(0x4066A7) dispatches `0x8000`..`0x804A` (75 opcodes) through the table at 0x41AC48 and reports
"invalid cmd code 0x%x in %s (0x%x)" otherwise. Text is cp932, also stored inverted, and is the
PC-98 text (e.g. S00 "さぁ、あなたはこれから、目を覚まします。"). Asset names appear as
NUL-terminated lower-case strings; voice clips as upper-case clip names (`SAY0141`, 5045
references in the 37 story scripts; none in `K`, `L`, `M`). Controls inside the text are words
`<char, 00>`: `s` key wait (PC-98 `{KEY}`), `k` keep window, `w` (+ digit) timed wait, `n` newline.
Voice and sound opcodes: `800B n` plays the sampled effect clip `n` once, `8039 n` loops it,
`800C` stops, `8047 name` plays a clip and waits (unused). The voice playback rules of the message
routine (0x409BA0) are in docs/voice.md §4.1. The other opcode semantics were not mapped.

**Music**: Standard MIDI files, 480 ticks per quarter, written for Roland GS / SC-88 (`init.mid`
is "GSGM_INI.MID" with a GS reset; `initf.mid` a second init). 20 songs: `xenon_01`–`06`,
`09`–`11`, `xenon12`, `13`, `15`–`17`, `21`–`26`. **Not present**: PMD `03A`, `03B`, `07`, `08`,
`14`, `18`, `19`, `20`, `27`, `28`, `29`.

### 2.4 Voice: `ldp4` / `ldp6` (verified, `source/tools/ldp.py`)

Player `XENON.EXE` 0x40E040 (header), block decoder 0x40DE75.

| Offset | Content |
|---|---|
| 0x00 | `"ldp"` + `'4'` or `'6'`: code width in bits, also selects the delta table |
| 0x04 | u32 BE sample rate (22 050 in every file) |
| 0x08 | u32 BE sample count |
| 0x0C | blocks: u16 BE byte count, u16 BE sample count, then the bytes of an MSB-first stream of codes |

Per block the accumulator and the bit buffer start at 0. Code ≠ 0: `acc = (acc + T[code]) & 0xFFFF`,
output `acc` as int16. Code 0: read one more code `n` and output `acc` `n + 1` times. `T` is a
logarithmic delta table, in the EXE at 0x41B628 (ldp4, 16 entries: 0, 8, 26, 86, 283, 927, 3043,
9986, −32768, then the negatives) and 0x41B648 (ldp6, 64 entries: 0–7, 10, 15, 20, 29, 40, …,
16754, 23431, −32768, then the negatives). Output is 16-bit mono; the engine shifts it right by
16 − volume.

All 4983 clips are `ldp6` at 22 050 Hz; in all of them the block sample counts add up to the header
count and the blocks end exactly at the end of the file. Decoded clips have sane levels and no drift
(spot-checked). 5.05 hours in total. Name prefixes (clip counts): `tra` 1142, `fal` 873, `ran` 737,
`ryo` 712, `kou` 360, `say` 315, `mai` 252, `bia` 229, `meg` 220, `koh` 91, `baa` 1, and 51
purely numeric names. The numeric clips `1`–`50` (no `15`) are sampled versions of the PC-98 sound
effects of the same numbers (the Towns `PCM` set is a different sampling); `666`, `1020` (= `21`),
`1027`, `2000`–`2003` were not traced. Comparison with the Towns voice: §4.

### 2.5 Video: `tVid` (header only)

Six members: `cslogo`, `title`, `lucid`, `fa03`, `fa08`, `meg05` (the last three replace the PC-98
ADT animations). Header parsed at 0x40E442: big-endian u32s `"tVid"`, ?, ?, ?, width (640), height (480 /
396), 30 000 / 15 000 (inferred: frame period in µs or a rate), frame count (40 / 49), audio rate
(22 050), audio code width (6: the ldp6 decoder with the table selected by 0x40E668), 0x800, 8, …,
then chunks tagged `vblt`, `data`, `vdat`, `atsa`, `end ` (0x4105EA). Only `title` and `cslogo`
carry audio; the four animations are silent. Not decoded further.

## 3. FM Towns CD release (1995)

### 3.1 Disc, boot and builds

`re-tools/cdbin.py info|iso|audio` splits the BIN. Track 1 is sectors 0..94739 (data, the last sector
with a sync pattern), track 2 audio from 94740, track 3 audio from **121156**: the cue says
INDEX 01 26:57:31 = 121306, but track 2 fades out into digital silence at 121156..121162 and the
music of track 3 starts at 121163, so the cue position would put the first 1.95 s of track 3 on
the end of track 2. `cdbin.py` snaps an audio track start back to the start of the preceding
digital-silence run. Track 2: 5:52.21, track 3: 3:55.97.

The disc boots on an FM Towns and **also contains a PC-9821 CD build**:

| Files | Build |
|---|---|
| `IO.SYS`, `CONFIG.SYS` (`DEVICE=\TBIOS.SYS /TBIOS.BIN`), `AUTOEXEC.BAT`, `RUN386.EXE`, `AGS.EXP`, `SEL.EXP`, `TMENU.ITM`, `INSTCD.EXP`, `INSTALL.DAT`, `XENONFM.BAT`, `SWIN.CBG` | FM Towns. `AUTOEXEC`: `RUN386 -NOCRT SEL`; on ERRORLEVEL 1 `RUN386 -NOCRT AGS.EXP CSWARE F`, else `… AGS.EXP CSWARE`. `AGS.EXP` and `SEL.EXP` are Phar Lap `P3` 386 executables. The Towns menu item "HDｲﾝｽﾄ-ﾙ" runs `INSTCD.EXP`, which follows `INSTALL.DAT`: copy `AGS.EXP CC M XENON.PMB XENONFM.BAT DISKA PCM GT SEL.EXP SWIN.CBG` to `D:\XENON` (~8 MB); the voice stays on the CD (`Q:CDFILE.F`). |
| `AGS16.EXE`, `SEL.EXE`, `PMD86.COM`, `XENON21.BAT`, `XENONFD.EXE`/`.BAT`, `XENONHD.EXE`/`.BAT`, `HDDXEN.EXE` | PC-9821 (16-bit MZ; `AGS16.EXE` contains the `CDBRer` loader, `Q:CDFILE.F`, and "ＭＩＤＩを使用できます"). `XENON21.BAT`: `sel`, then `pmd86 /v0 /e4 /F /k`, `ags16.exe [/f] csware`, `pmd86 /r`. `XENONFD.EXE` builds an MS-DOS boot floppy for NEC/Panasonic CD-ROM drives (`NECCD.SYS /D:CD_101`, `LASTDRIVE=Q`); `HDDXEN.EXE` is the HDD installer (~6 MB). |

The meaning of `SEL` and of the `F` / `/f` flag it selects was not determined. `SEL.EXP` names
`PPS1.GKF`–`PPS6F.GKF`, `PIA.GKF`, `PPTITLE.GKF` and `Q:CDFILE.P`; none of these exist on the disc.
`DISKA` is 9900 zero bytes (inferred: a save area; the engines ask for a writable data disk in
drive A). `XEN_SE.EFC` is byte-identical to the PC-98 file.

### 3.2 Archive format (verified, `re-tools/townsarc.py`)

`CC`, `GD`, `GT`, `M`, `PCM` and `CDFILE.F01`–`F04` share one layout: u16 LE directory size `D`,
then `D / 16` entries of `name[12]` (NUL padded) + u32 LE member offset; entry `k` starts at
`2 + 16k`; a member ends where the next starts. Members of the `CDFILE` voice archives are padded to
2048-byte sectors.

| Archive | Members | Contents |
|---|---|---|
| `CC` | 43 | scripts (§3.3) |
| `GD` | 296 | pictures for the PC-9821 build (§3.4) |
| `GT` | 295 | pictures for the Towns build (§3.4) |
| `M` | 59 | PMD songs (§3.5) |
| `PCM` | 49 | sampled effects `01`–`50` (no `15`), Towns `.SND` format |
| `CDFILE.F01`–`F04` | 1238 / 1286 / 1030 / 526 | 4080 voice clips, Towns `.SND` format (§3.6) |

### 3.3 Scripts (`CC`)

The 40 PC-98 scripts plus `CSWARE.CC` (logo), `XMENU.CC` (title menu), `XEND.CC` (ending: plays
`xen86_02` and shows the staff roll `st00`–`st09`). Members are uncompressed and obfuscated:

* The first 4 bytes are 0; every following byte is XOR 0x7C (`townsarc.py extract --xor`).
  Decoded: u16 0xB2F5, u16 0, then a table of u16 LE offsets (inferred: pages), then bytecode.
* Text is cp932 with **kana packed to one byte**: byte values index a table of 2-byte characters in
  the engine ("　　、。・？！（）「」０１…９あいう…んアイウ…", `AGS.EXP` file offset 0x16D0; the same
  table is in `AGS16.EXE`). Verified for の = 0x3D and を = 0x59 ("天上の神々を動かせれば").
* Inline NUL-terminated name operands after a type byte: `f` + voice clip (`fA0575`), `a` + song
  (`aXEN86_02`), `3` + picture (`3st00`), `Q` + script. Only `XEN86_*` song names occur;
  inferred: the Towns engine maps them to `XENFM_*`.
* The opcode set was not mapped.

### 3.4 Pictures: `CDBR` (header verified, codec not decoded)

Both archives hold the 203 PC-98 pictures (plus `CLKM`, `MCGD`, `FA09_3`), and the 89 ADT animation
frames as separate pictures (`0100B__0`…`__S` 29, `FA08` 34, `RA03` 8, `FA03` 8, `MEG05` 6,
`FA09` 4; GT lacks `XEN`). **For all 203 pictures, position, size and colours equal the PC-98
GDT** (checked against `re-tools/gdt.py` headers):

* **GD** (PC-9821): `"CDBRer 1.0"` + 6 zero bytes; at 0x10 16 × u16 LE colours `F000h | G<<8 | B<<4 | R`
  (4-bit, the PC-98 values); at 0x30 u16 VRAM byte address (`y × 80 + x / 8`), u16 width, u16
  height; compressed data from 0x40.
* **GT** (Towns): `"CDBR"`, u16 width, height, x, y; at 0x0C 16 × (B, R, G) bytes = the PC-98 4-bit
  values × 0x11 in the Towns palette-register order, followed by a second identical copy; then
  compressed data.

The codec (shared marker `CDBRer` in `AGS16.EXE`) was not analysed, so pixel identity with the GDTs
is unconfirmed. It is inferred from the identical geometry and palettes.

### 3.5 Music and effects

`M` holds three sets of PMD files (MC version 46–48), checked with `re-tools/pmdinfo.py`:

* `XEN86_01`–`29` (no `28`) + `XEN03A`, `XEN03B`: for YM2608 (PC-9801-86 board), started with
  `PMD86`. 20 of the 28 songs use all 6 FM + 3 SSG parts (A–I); on PC-98 the most common layout is
  4 FM + 3 SSG (17 of 29 songs). E.g. `XEN86_01` uses A–I where `XENON_01` uses A–D + G–I.
  ADPCM and rhythm parts are unused.
* `XENFM_01`–`29` (no `20`, `28`) + `XEN03A_T`, `XEN03B_T`: for the Towns (YM3438): no SSG; 12 of
  the 17 songs `pmdinfo.py` can read use 6 FM parts. `pmdinfo.py` fails on 10 `XENFM` files in its
  rhythm-pattern parser (not investigated; the Towns PMD plays rhythm from PCM). `XENON.PMB` is a drum sample bank for the Towns PMD (`BD24`, `SD10`, `CHH18`, `OHH03`,
  `JMTOM01A`, `KITCH10`, `CCYM03`).

`PCM`: 49 sampled effects in the `.SND` format of §3.6, rate code 1080 (45 files) or 1078 (4).

### 3.6 Voice: Towns `.SND`

4080 clips in `CDFILE.F01`–`F04` (180 MB), named by speaker letter + number (+ `A`/`B` for split
lines): H 1030, A 580, C 578, G 543, J 354, F 310, D 221, E 212, I 162, B 80, L 8, K 2. 32-byte
header: name[8], u32 id, u32 data length, u32 loop start, u32 loop length, u16 rate code (1080 in
every clip), u16 0, u8 base note (0x3C), …; 8-bit data follows. Rate code 1080 is
~11 020 Hz (the standard Towns `.SND` convention, rate = code × 1000 / 98; consistent with the
durations of the same lines in the Windows release, not checked in the engine), 4.44 hours in total.
Samples are **8-bit sign-magnitude** (the RF5C68 format: bit 7 = sign, bits 0–6 = magnitude):
decoded that way the clips have zero DC offset and lag-1 autocorrelation 0.95; read as two's
complement or unsigned they show a DC offset near −48 and 0.83. The polarity convention could not
be determined and is inaudible.

### 3.7 CD audio (tracks 2 and 3)

Both tracks are full-band stereo studio recordings, not synthesizer output: energy up to 16–22 kHz,
side/mid energy ratio 0.13–0.14, roughly 105–110 BPM by onset autocorrelation. Track 2 (5:52) opens
with a ~15 s quiet introduction and ends in a long fade; track 3 (3:56) ends in a fade too. What
they are (vocal songs, arrangements) was not established from the data. A web search found no track
listing.

`AGS.EXP` (file 0x6280) and `SEL.EXP` (0x4B1) both contain a CD-DA player using the Towns CD BIOS
(INT 93h with AX = 50C0h, 52C0h, 53C0h, 54C0h, 55C0h, 14C0h, called through the DOS extender's
real-mode interrupt service; inferred: 50C0h plays). The `SEL.EXP` routine at file 0x530 takes a
track number in AL and a pointer to start/end times relative to that track's TOC position. Their
callers and the track numbers used were not traced. The ending script `XEND.CC` plays the FM song
`xen86_02`, not CD audio.

## 4. Voice: Windows compared with Towns

**The Windows voice is a separate recording at twice the sample rate, of the same set of lines.**
It adds audio quality and a few takes, not coverage. Method: clips paired by the text of the line
they voice (Towns `f`-cues and Windows clip names, §2.3 / §3.3, in the 37 shared story scripts).

**Speakers** (mapping verified by line text):

| Towns | Windows | Character | Notes |
|---|---|---|---|
| A | `RYO` | 涼子 | |
| B | `KOH` | 宏治 (protagonist, present) | sparsely voiced in both (82 / 83 references) |
| C | `RAN` | ランファ | |
| D | `MAI` | 舞 | |
| E | `MEG` | 恵 | |
| F | `SAY` | 沙夜香 | |
| G | `FAL` | ファル | |
| H | `TRA` | トレイシー | |
| I | `BIA` | ビアンカ | |
| J | `KOU` | コウジ (protagonist, dream) | 353 / 360 references |
| K | — | 女の声 (nurse) | 2 lines; silent in Windows |
| L | one character's clip | group shouts | 8 lines |

**Quality** (80 random clips per release): the Windows audio is genuinely wide-band, not upsampled
from 11 kHz: the averaged spectrum falls smoothly past 5.5 kHz (−24 dB at 5.5 kHz, −33 dB at
10 kHz, no cliff), and the energy above 5.5 kHz follows the speech (frame high/low ratio median
0.003, 99th percentile 11.8: sibilants). Towns is 11 kHz 8-bit sign-magnitude PCM (§3.6); Windows
is 22 kHz 6-bit log-DPCM (§2.4).

**Different takes** (verified on 40 text-matched pairs): no 300 ms waveform segment correlates above
0.9 (median 0.30; a degraded copy of the same clip scores 0.99), aligned pitch contours agree within
half a semitone on a median 7 % of frames (91 % for a clip against itself), and the speech length
ratio Windows/Towns has median 1.06 with interquartile range 0.85–1.36. Mean pitch per character
matches for BIA, KOU, MAI, RAN, SAY but differs for RYO (+3.7 semitones), KOH (−5.4), TRA (−3.0),
FAL and MEG (−2.1): inferred, from a crude pitch tracker, that some roles were recast. Listening
check (2026-11-13, pair S0107 `A0002` / `RYO0212`): Ryōko is very likely a **different actress** —
the intonation differs, beyond the Towns clip's lower quality. The other pairs listened to sound
like they may be the same actors re-recorded, but this is uncertain (pairs in
`~/repos/xenon-releases/voice-compare/`).

**Coverage**: 4961 of the Towns-voiced lines are voiced in Windows and 4938 of the Windows-voiced
lines in Towns; 35 Towns and 62 Windows lines have edited text and could not be matched.
* Voiced only in Towns (~25 lines): Ranfa's epilogue in S0111 (C0111–C0117), Tracy's "［私］" lines
  in S0401 (H0390–H0397), S0202 H0087–H0089 and K0001–K0002, the hypnosis lines in S0107 (A0165,
  A0166, A0010), S0104 H0004, S0301A C0574, S0304A H0227.
* Voiced only in Windows (~10): KOU0001 (twice), KOU1075, RAN0629, RAN0632, FAL0543, TRA0956 and a
  few "‥‥？" grunts.
* Two Towns cues name clips that do not exist (B0407, B0952): silent in both.

**File counts**: Towns uses one clip per recording and repeats the cue: 4080 clips, all distinct,
4044 referenced. Windows stores a separately named copy for each occurrence of a repeated line
(695 of its 4932 voice clips are byte-identical copies, e.g. `BIA0016` = `BIA0063`), leaving 4237
distinct recordings; for 121 repeated lines it recorded separate takes (174 extra), which is most
of the ~170 more distinct recordings. 20 Windows clips are not referenced (`KOH5001`–`5008`,
`RAN5001`–`5007`, `MAI5001A`–`5003A`, `TRA11291`, `BAA`); 36 Towns clips are not referenced.

Neither release voices the menus, the omake or the ending.

## 5. Open questions

* `CDBR` picture codec (best route: `AGS16.EXE`, a 16-bit Borland binary close to `AGS.EXE`).
* Opcode sets of the Towns `CC` and the Windows `sco` bytecode.
* The unreferenced voice clips (Windows `*5001`…, Towns 36) and the Windows effect clips `666`,
  `1027`, `2000`–`2003`.
* Which game scenes play CD tracks 2 and 3, and what `SEL` / the `F` flag do.
* `arc3` compression methods 1 and 2 (unused) and the `tVid` video codec.
* The 19 Windows diff pictures not matched in §2.2.
