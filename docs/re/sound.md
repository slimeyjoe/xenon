# Sound: music and sound effects (AGS.EXE + PMD 4.7g)

Scope: how AGS.EXE drives the resident music driver, the on-disk formats `*.M` and `XEN_SE.EFC`,
the alternative MIDI path, and the inventory of every music file and effect.
Addresses are `SEG:OFF` in the shared listing convention (file offset = 0xE00 + SEG*16 + OFF).
PMD.COM addresses are COM offsets (`PMD:xxxx`, ORG 100h) from `ndisasm -o 0x100 PMD.COM`.
**verified** = read in code or round-tripped on data; **inferred** = reasoned, not proven.

Tools / data produced:
* `re-tools/pmdinfo.py` — dumps .M structure (`--tsv` for inventory) and EFC tables (`--efc FILE [--split DIR]`).
* `resources/music/index.tsv` — one row per .M file (parts, tempo, length, loop, status marks, memo strings, usage).
* `resources/sfx/xen_se_efc.tsv` — EFC effect table (127 slots) + usage; `resources/sfx/blobs/se_NN.bin` raw
  effect streams (+ `voices.bin`, the effect voice table).
* No audio renderer is available locally (`pmdplay`, `pmdmini`, `mdxplay`, fmgen: none found); nothing was rendered.

## 1. Driver set-up

`XENON.BAT`: `pmd /m10 /v0 /e4 /F /k`, `ags`, `pmd /r`. Option meanings from PMD.COM's own help text
(**verified**, PMD.COM usage strings): `/M10` music buffer 10 KB, `/V0` separate voice buffer 0 KB (voices are
embedded in every .M), `/E4` FM-effect buffer 4 KB, `/F` *do not stop the song after a fade-out completes*
(song keeps running at volume 0), `/K` disable PMD's own ESC/GRPH stop/fast-forward hot keys. PMD.COM banner:
"PMD for PC9801/88VA Version 4.7g, Copyright (C)1989,Jul.17th 1994 by M.Kajihara(KAJA)"; supports YM2203
(PC-9801-26K) and YM2608 (PC-9801-86), optional PPSDRV (INT 64h).

PMD resident signature: INT 60h vector points at `PMD:0103` = `EB 11 'P' 'M' 'D' ...` so `vector+2 == "PMD"`.

### 1.1 Engine sound initialisation — `snd_init` 0B2C:006F (**verified**)

Called once from `engine_init` 0E1A:010A as `snd_init(1)`. Mode 2 code exists but is never selected.

```
snd_init(mode):
  mode 1: if snd_detect_pmd() { g_sound_mode=1; intno=60h }        ; 0B2C:003D: getvect(60h)+2 == "PMD" (strncmp 3)
  mode 2: if snd_detect_pmd() && snd_detect_mmd() {                ; 0B2C:000B: getvect(61h)+2 == "MMD"
            g_sound_mode=2; intno=61h
            printf("音色データーを転送中です")                       ; 0B2C:050F  "transferring tone data"
            if spawnl(P_WAIT, "PUTCM6", "", "LITRA", NULL) == -1 -> g_sound_mode=0, return
            text_layer_clear() }
  otherwise g_sound_mode=0 (DS:076C), return                       ; no message: game runs silent
  g_music_name[0]=0 ; music_stop()
  intr(intno, AH=06h) -> g_pmd_music_buf (DS:13C0/13C2) = DS:DX      ; music data address
  intr(60h,   AH=0Bh) -> g_pmd_efc_buf   (DS:13C4/13C6) = DS:DX      ; FM effect buffer (always PMD)
  fp = xfopen("XEN_SE.EFC","rb") ; fread(g_pmd_efc_buf, 1, filelength(fp), fp) ; fclose
       on failure: puts("効果音ファイルをオープンできません") and g_sound_mode=0
```
`intr` = Borland `intr(int, struct REGPACK far*)` at 0000:214B (r_ax +0, r_dx +6, r_ds +14), used for all
calls that need DS:DX; `int86` 0000:2081 for the two status calls.

So **the engine copies files directly into PMD's resident buffers** — it never asks PMD to load a file.

### 1.2 Alternative MIDI path (MMD / PUTCM6 / LITRA) — present but dead

* `"%s.MMD"` (DS:07B8) is used instead of `"%s.M"` when `g_sound_mode==2` (0B2C:0245); all music calls then go
  to INT 61h, while FM effects stay on INT 60h (0B2C:043C/0461 hard-code 60h) — i.e. MIDI music + OPN effects.
  The MMD driver is expected to implement the same AH function numbers as PMD (00/01/02/06/08) (**verified**
  from the calls; that a real driver does so is **inferred**).
* `PUTCM6` is run with argument `LITRA` before use (**verified**). From the name and the "tone data transfer"
  message it is a utility that uploads a tone/patch bank called LITRA to a Roland CM-64/CM-32L-class module
  (**inferred**; neither PUTCM6 nor LITRA nor any `.MMD` file ships on the HDD image or floppies — checked
  `resources/original/game/` and `floppy_only/`).
* `snd_init` is only ever called with 1 (0E1A:0108 `push 1`), so the MIDI path is unreachable in this build.
* **"MUSIC LALF" / "ANNEX" / "FM-SOUND"**: these strings sit inside an embedded YM2203 music driver linked
  into the region 0C2A:0A20–0C2A:19FF (its own segment frame 0CCC:0000, CS-relative data, OPN writes to ports
  188h/18Ah at 0C2A:19A1, "FM-SOUND" at 0C2A:15F2, copyright "MUSIC LALF Copyright TOKUMASHOTEN INTERMEDIA INC.
  1991 / ANNEX Copyright 1987..91 by Ana." at 0C2A:0BDB). No relocation and no far call targets this region
  (all 35 relocated references to segment 0C2A point at offsets ≤ 0962) — **dead library code** (**verified**).
  Graphics analysts: segment 0C2A's real code ends at 0C2A:0A19.

## 2. PMD INT 60h interface used by the engine

The full driver-side reference (all 22h functions, gating, deferral, IRQ, timers, memory map) is
[pmd-driver.md](pmd-driver.md) §4; the music command set and sound math are in
[pmd-sequencer.md](pmd-sequencer.md). This section lists only what the engine uses.

Function table of PMD 4.7g: dispatcher `PMD:0116` → `PMD:232F` (board present) / `PMD:28E8` (no board,
stub table at `PMD:2948`), main jump table `PMD:2411`, 23h entries. Registers are saved to CS:309E (AH),
CS:309F (AL), CS:309C (DX), CS:309A (DS) and handlers return values by overwriting these (**verified**).

| AH | PMD handler | meaning (from PMD.COM code) | engine wrapper |
|---|---|---|---|
| 00h | 035A mstart | start song in music buffer; resets fade vol (3115h)=0. If PMD is inside its timer interrupt the request is deferred (30B8h bit0) and AH=FFh returned | `music_play` 0B2C:02E5 |
| 01h | 0569 mstop | stop; sets status2 (3112h)=FFh, fade vol=FFh. Busy → deferred, AH=FFh | `music_stop` 0B2C:033C (retries while AH==FFh) |
| 02h | 0326 fout | AL = fade speed → 3114h | `music_fade` 0B2C:0386 |
| 06h | 2462 get_musdat_adr | DS:DX = music buffer **−1** (byte 0 of the .M file goes there) | `snd_init` 0B2C:00E9 |
| 08h | 2487 get_fout_vol | AL = fade-out volume 3115h (0 normal … FFh silent) | `music_get_fadevol` 0B2C:03D4 |
| 09h | 2492 / 299B board check | AL=00h FM board present, AL=FFh none; AH=47h, DX=4767h ("4.7g") | `pmd_board_present` 0B2C:04AD |
| 0Ah | 24A3 get_status | AH = status1 (3111h, set by MML `~n` = cmd DCh, `~+n`/`~-n` = cmd DBh), AL = status2 (loop counter, FFh = stopped) | `music_wait_status` 0B2C:04D8 |
| 0Bh | 247B get_efcdat_adr | DS:DX = FM effect buffer | `snd_init` 0B2C:0109 |
| 0Ch | 29A8 fm_effect_on | AL = effect number | `se_play` 0B2C:043C |
| 0Dh | 2A1D fm_effect_off | | `se_stop` 0B2C:0461 |
| 14h | 24FB set_efc_int | DS:DX = far hook called in effect interrupt (0:0 = none) | `snd_clear_efc_hook` 0B2C:019B (unused) |
| 19h | 248E set_fout_vol | AL → fade-out volume 3115h directly | `music_set_fadevol` 0B2C:0482 |

Not used by the engine: 03h/04h SSG effect on/off, 05h measure, 07h voice buffer, 0Eh PCM addr, 0Fh PCM effect,
10h work addr, 11h/12h/15h effect numbers, 13h FM-int hook, 16h joystick, 17h/18h PCM86 flags, 1Ah/1Bh pause,
1Ch FF, 1Dh memo pointer (see §4.4), 1Eh part mask, 1Fh–22h misc.

PMD timing (**verified** register values, real-time durations **inferred** assuming the 26K/86 OPN clock
3.9936 MHz): Timer B runs the song: one Timer-B interrupt = one PMD clock, period `1152*(256-TB)/3993600` s
(`T`-command value TB; e.g. TB=170 → 24.8 ms). Timer A is programmed with NA=0 (`PMD:2226`: regs 24h/25h ← 0)
→ 18.46 ms; the Timer-A handler (`PMD:2BA7`) runs the FM/SSG effect parts every interrupt and the fade
step (`PMD:21CE`) every 8th interrupt (≈147.7 ms). Fade step: `vol += speed`; on carry vol=FFh, speed=0, and
only if /F was not given the song is stopped. Hence a fade with speed s takes `floor(255/s)+1` steps
(s=20: 13 steps ≈ 1.9 s; s=25: ≈1.6 s; s=1: ≈37.8 s).

## 3. Engine sound API (seg 0B2C) — for the script analyst

All functions are far cdecl, no-ops when `g_sound_mode` (DS:076C) == 0. Music state: `g_music_name`
(DS:13C8, current song base name, strcpy'd — buffer is effectively 15 bytes because DS:13D7 follows),
`g_music_started` (DS:13D7, word).

| address | name | args | behaviour (**verified**) |
|---|---|---|---|
| 0B2C:01C4 | `music_load` | `char far *name` (base name, no extension) | If `stricmp(g_music_name,name)==0` (0000:4237, case-insensitive) or name==NULL: `g_music_started=1`, return (**same song keeps playing, no restart**). Else `g_music_started=0`, `music_stop()`; if name empty return; `sprintf("%s.M")` (or `.MMD`), `xfopen(…,"rb")` (cwd, else any drive that has it — 0843:0384), `setvbuf(fp,NULL,_IOFBF,0x1800)`, `strcpy(g_music_name,name)`, `fread(g_pmd_music_buf,1,filelength,fp)`. Does not start playback. Missing file: silently nothing loaded (name stays cleared). No size check against the 10 KB buffer (largest file 5601 B). |
| 0B2C:02E5 | `music_play` | – | if `g_music_started!=1` and name non-empty: `g_music_started=1`, INT AH=00h. |
| 0B2C:033C | `music_stop` | – | INT AH=01h, repeated while returned AH==FFh (driver busy); `g_music_name[0]=0`. |
| 0B2C:0386 | `music_fade` | `int speed` | if name non-empty: INT AH=02h AL=speed. Returns immediately. |
| 0B2C:03D4 | `music_get_fadevol` | – | returns `(signed char)` AL of AH=08h → −1 when fully faded (FFh); −1 also if no driver / no song. |
| 0B2C:0427 | `music_fade_stop_wait` | – | `music_fade(0x19)`; busy-loop until `music_get_fadevol()==-1`; `music_stop()`. Blocks ≈1.6 s. |
| 0B2C:0482 | `music_set_fadevol` | `int v` | INT 60h AH=19h AL=v&FFh. 0 = full volume, larger = quieter (FFh silent). Scripts use 0/64/127. |
| 0B2C:04D8 | `music_wait_status` | `int n` | if `pmd_board_present()`: loop { int86(60h, AH=0Ah); } while (out.AH == n). I.e. **waits until the song's `~` status changes away from n** (no timeout, no key check). |
| 0B2C:043C | `se_play` | `int n` | INT 60h AH=0Ch AL=n (always INT 60h, even in MIDI mode). |
| 0B2C:0461 | `se_stop` | – | INT 60h AH=0Dh. |
| 0B2C:02DF | `music_get_name` | – | returns `DS:13C8` (used by save, 088D:13A6, which writes **40 bytes** from there: name + g_music_started + the following graphics globals; load 088D:16DA only uses it as a string → `music_load(name); music_play()`). |
| 0B2C:006F | `snd_init` | `int mode` | see §1.1 |
| 0B2C:019B | `snd_clear_efc_hook` | – | unused (no callers). |

### 3.1 Script opcodes that reach the sound API (**verified**, dispatch table 088D:094C, handlers in 088D)

| op | handler | operand | calls | notes |
|---|---|---|---|---|
| 0Eh | 088D:1DA1 | `FD` string (song base name, e.g. `xenon_17`) | `music_load` | MUSIC_LOAD (name from the compiler error strings) |
| 0Fh | 088D:1DE6 | – | `music_play` | usually right after 0Eh |
| 10h | 088D:1DF8 | – | `music_stop` | |
| 30h | 088D:1EEB | `FB` speed | `music_fade` | non-blocking; speeds used: 1,2,3,5,10,**20** (121×),25,60,230,250 |
| 31h | 088D:089F (inline) | – | `music_fade_stop_wait` | blocking fade+stop; not used by any shipped script (engine uses it on return to title, 0ABA:0203/0272) |
| 3Ah | 088D:1EA6 | `FB` v | `music_set_fadevol` | music "distance"/muffle: 64 = attenuated (e.g. behind a door), 127, 0 = restore |
| 45h | 088D:1E0A | `FB` n | `se_play` | effect number 1..50 |
| 46h | 088D:1E4F | – | `se_stop` | |
| 48h | 088D:1E61 | `FB` n | `music_wait_status` | used 3× (`48 0` in S00/S00B/S00C with xenon_01): blocks until xenon_01 reaches its `~+1` mark at clock 384 (≈9.5 s) — text is synchronised to the intro. (script names file calls this `op48_music_waitloop`; it is a *status* wait, not a loop wait) |
| 20h | 088D:1F42 | `FB` n | `wait_vsync(n)` 0DCA:0012 | n VSYNCs (56.42 Hz at 24.83 kHz; 70.10 Hz on a PC-9821 set to 31 kHz – platform.md §6.1); CTRL skips |
| 47h | 088D:08FD | – | `staff_roll` 0E31:05D2 | loads/plays `xenon_02`, waits 120 VSYNC, shows st00, `music_wait_status(0)` (xenon_02 `~+1` at clock 768 ≈ 16.8 s at T180), then st01..st09 on fixed waits |

Typical script idiom (M.CC music mode): `30 20` (fade, speed 20) / `20 60` (wait 60) / `0E "xenon_NN"` / `0F`.

Engine-internal uses: opening 0E31:04AE (`music_fade(20)`, load+play `xen_03a`, …, load `xen_03b`, play,
`music_fade(20)`, `music_stop`), logo 0E31:0478 `se_play(46)`, shutdown 0E1A:0130 `se_stop`+`music_stop`,
title loop 0ABA:0203/0272 `music_fade_stop_wait`+`se_stop`. The title screen itself plays no music
(no `music_load` in 0ABA) (**verified** by caller list).

## 4. `.M` file format (PMD / MC.EXE 4.7 compiled music)

All 31 files: header byte 00h, MC version byte 47h ("4.7"), voices embedded. Offsets below are relative to
**data start = file offset 1** ("D+x"). `re-tools/pmdinfo.py` parses every part of every file and checks that
each part ends (80h) exactly where the next one starts — true for all 31 files, which validates the command
length table (**verified**).

| file offset | size | field |
|---|---|---|
| 0 | 1 | sound-source type, 00h (PMD reads it into 3110h; 0 = OPN/OPNA data) |
| D+00 … D+14 | 11 × u16 | part start offsets: A B C (FM 1–3), D E F (FM3 extended-mode slot parts on channel 3), G H I (SSG 1–3), J (ADPCM — never processed by this PMD build), K (rhythm via SSG drums) |
| D+16 | u16 | R-pattern table offset (rhythm patterns referenced by part K) |
| D+18 | u16 | voice table offset — present when part A offset ≠ 18h (PMD `PMD:039E`; see pmd-sequencer.md) |
| … | | part streams (bytecode), R table, then: |
| voice−4 | u16 | memo-table offset |
| voice−2 | u8 | MC version (47h) |
| voice−1 | u8 | FEh marker (PMD AH=1Dh accepts word 0040h, or FEh with version ≥ 41h) |
| voice | n × 26 | FM voices: `num, DT/ML×4, TL×4, KS/AR×4, AM/DR×4, SR×4, SL/RR×4, FB/ALG` |
| | 2 | `00 FF` (end of voice list; PMD itself looks voices up by linear search, `PMD:1D62`) |
| | | memo strings (NUL-terminated, cp932) |
| memo table | u16… 0000 | offsets to strings: [0] PPZ file, [1] PCM file, [2] title, [3] composer, [4] arranger, [5..] memo lines (≥42h layout; AH=1Dh AL=n returns entry n+1) |

Part bytecode (**verified** from PMD dispatcher `PMD:0A77`, tables 0A8Eh FM / 0B24h / 0BBAh):
* `00h–7Fh`: note — high nibble octave, low nibble 0–11 note, 0Fh rest; followed by 1 length byte (clocks).
  In part K a byte < 80h is an R-pattern number (no length).
* `80h`: part end (if an `L` was seen, PMD loops there and increments status2).
* `81h–B4h`: invalid → treated as end. `C0 n` with n = 02h..F6h is also invalid (part end); a C1h directly after
  a note's length byte means "gate 0" for that note. The complete 75-command reference is
  [pmd-sequencer.md](pmd-sequencer.md).
* `B5h–FFh`: commands; operand byte counts implemented in `pmdinfo.py OPLEN`
  (FF `@`1, FE `q`1, FD `V`1, FC tempo 1 or 2 (FC FF n = `t`, FC FE/FD = relative, else FC n = Timer-B value `T`),
  FB `&`0, FA `D`2, F9 `[`2 (offset of the matching F8's count byte), F8 `]`4 (count, counter, back offset;
  count 0 = infinite loop), F7 `:`2, F6 `L`0, F5 `_`1, F4 `)`0, F3 `(`0, F2 `M`4, F1 `*`1, F0 `E`4, EF `y`2,
  DC `~`1 (status1 = n), DB `~±`1 (status1 += n), DA portamento 3, D4 `n` SSG-effect 1, D3 `N` FM-effect 1, …).
* R patterns: FFh end, `<80h len` rest, `80h–BFh xx len` drum bits, `C0h+` command.

### 4.1 Loop behaviour
PMD loops a part at its `L` (F6h) point when it reaches 80h, forever; status2 (loop counter) increments each
time every present part has looped or ended (wraps of an infinite `[ … ]0` count as loops). Songs without `L` either end (part stays silent: XEN_03A, XENON_27, XENON_28) or loop
internally with an infinite `[ … ]0` (short ambience/SFX songs 07, 08, 14, 18, 19, 20). The engine never
looks at the loop counter; it never stops a song except via `music_stop`/fade.

### 4.2 Inventory (`resources/music/index.tsv`)

The `approx_sec_*` columns are tick counts × the song's *first* tempo; XENON_04 (part H) and XENON_10
(part G) also change the tempo relatively (`FC FE`, 16× / 4×), so their real loop lengths are 115.25 s and
170.30 s (measured from the real driver, pmd-sequencer.md §11.1), not 114.3 / 166.7 s. All other songs agree
with the real driver to ≈ 0.1 %.

Parts list shows parts that contain notes. No file uses ADPCM (J) or rhythm (K) — the music is pure OPN
(FM A–C + SSG G–I). Parts D/E/F are **FM3 extended-mode parts**: PMD.COM loads them into the FM3-extended
part structures (32B4h/3310h/336Ch) and plays them on the slots of FM channel 3 on *both* YM2203 and
YM2608 — this PMD build only uses the OPN-compatible half of the YM2608, so Xenon sounds the same on 26K
and 86 boards (**verified**, pmd-driver.md §3; an earlier version of this note wrongly said part D was lost
on a 26K board).
"≈ s" = Timer-B estimate for the longest part (**inferred** clock); composer and arranger fields are "Ｒyu Ｕmemoto."
for every file; every memo ends with `(C)Ｃ's ware office.` / `(R)Ｓtaff "Ｙ-arm".`.

| file | size | parts (FM/SSG) | T | ticks total / loop | loop | ≈ s intro + loop | voices | `~` marks | title (memo) | memo line | music-mode title | used by |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| XEN_03A.M | 350 | ABC/GH | T200 | 30 / 0 | none | 0.5 + 0.0 | 2 |  | `Ｘenon Ｎo.03` | = Ｓound effect = |  | engine: opening (op 29h) 0E31:0503 |
| XEN_03B.M | 382 | ABC/G | T200 | 288 / 192 | L | 1.6 + 3.1 | 3 |  | `Ｘenon Ｎo.03` | = Ｓound effect = |  | engine: opening 0E31:0543 |
| XENON_01.M | 980 | ABCD/GHI | T170 | 3480 / 2712 | L | 19.1 + 67.3 | 3 | ~+1@tick384 | `Ｘenon Ｎo.01 "Ｄesert Ｓtorm"` | = Ｏpening Ｂridge = (Prototype) | Desert Storm | S00* use op 0x48 0 (waits for ~+1); 4 scripts |
| XENON_02.M | 4042 | ABCD/GHI | T180 | 8457 / 6144 | L | 50.7 + 134.7 | 9 | ~+1@tick768 | `Ｘenon Ｎo.02 "Ｘenon"` | = Ｍain Ｔheme = (Prototype) | XENON | engine: staff roll 0E31:05EC (waits for ~+1) |
| XENON_03.M | 2962 | ABCD/GHI | T160 | 6336 / 6336 | L | 0.0 + 175.5 | 2 |  | `Ｘenon Ｎo.03 "Ｒanphar Ⅰ"` | = Ｔheme of "Ｍai" For Ｎight = | Lanhua | 4 scripts |
| XENON_04.M | 2239 | ABCD/GHI | T170 | 4608 / 4608 | L | 0.0 + 114.3 | 3 |  | `Ｘenon Ｎo.04 ""` | = = | 蝶 | 4 scripts |
| XENON_05.M | 5601 | ABCD/GHI | T170 | 5376 / 4608 | L | 19.1 + 114.3 | 9 |  | `Ｘenon Ｎo.05 ""` | = = | << QUADRA >> | 4 scripts |
| XENON_06.M | 4599 | ABCD/GHI | T170 | 4608 / 4608 | L | 0.0 + 114.3 | 9 |  | `Ｘenon Ｎo.06 ""` | = = | Ending II | 4 scripts |
| XENON_07.M | 500 | ABC/G | T170 | 579 / 0 | [ ]0 | 14.4 + 0.0 | 2 |  | `Ｘenon Ｎo.07 "Ｓound Ｅffect No.01"` | = シャトル発進 = |  | 2 scripts |
| XENON_08.M | 297 | A/ | T170 | 204 / 6 | [ ]0 | 4.9 + 0.1 | 1 |  | `Ｘenon Ｎo.08 "Ｓound Ｅffect No.02"` | = シャワー = |  | 2 scripts |
| XENON_09.M | 3279 | ABCD/GHI | T170 | 5760 / 5376 | L | 9.5 + 133.4 | 8 |  | `Ｘenon Ｎo.09 ""` | = = | Synaps | 4 scripts |
| XENON_10.M | 2544 | ABCD/GHI | T170 | 6720 / 6720 | L | 0.0 + 166.7 | 7 |  | `Ｘenon Ｎo.10 "Ｄesert Ｓtorm"` | = Ｅnding = | Ago (M.CC references nonexistent "xenon_102") | 1 scripts |
| XENON_11.M | 1029 | ABCD/GHI | T160 | 1920 / 1920 | L | 0.0 + 53.2 | 2 |  | `Ｘenon Ｎo.11 ""` | = = | Trust | 12 scripts |
| XENON_12.M | 888 | ABCD/GHI | T170 | 3168 / 3168 | L | 0.0 + 78.6 | 2 |  | `Ｘenon Ｎo.12 ""` | = = | Silence | 14 scripts |
| XENON_13.M | 2150 | AB/GHI | T170 | 6912 / 6144 | L | 19.1 + 152.4 | 3 |  | `Ｘenon Ｎo.13 ""` | = = | Encoder | 10 scripts |
| XENON_14.M | 406 | A/ | T200 | 96 / 48 | [ ]0 | 0.8 + 0.8 | 2 |  | `Ｘenon Ｎo.14 ""` | = = |  | 1 scripts |
| XENON_15.M | 3549 | ABC/GHI | T200 | 3264 / 2496 | L/[ ]0 | 12.4 + 40.3 | 8 |  | `Ｘenon Ｎo.15 ""` | = = | Emergency | 3 scripts |
| XENON_16.M | 1622 | ABCD/GHI | T170 | 5568 / 5568 | L | 0.0 + 138.1 | 5 |  | `Ｘenon Ｎo.16 ""` | = = | Moon | 5 scripts |
| XENON_17.M | 2137 | ABCD/GHI | T170 | 7680 / 7680 | L | 0.0 + 190.5 | 2 |  | `Ｘenon Ｎo.17 ""` | = = | Dream | 24 scripts |
| XENON_18.M | 482 | AB/GHI | T170 | 204 / 6 | [ ]0 | 4.9 + 0.1 | 2 |  | `Ｘenon Ｎo.18 ""` | = = |  | 4 scripts |
| XENON_19.M | 325 | AB/ | T170 | 192 / 96 | [ ]0 | 2.4 + 2.4 | 1 |  | `Ｘenon Ｎo.07 "Ｓound Ｅffect No.01"` | = シャトル発進 = |  | 2 scripts |
| XENON_20.M | 276 | A/ | T190 | 320 / 160 | [ ]0 | 3.0 + 3.0 | 1 |  | `Ｘenon Ｎo.20 ""` | = = |  | 1 scripts |
| XENON_21.M | 3294 | ABCD/GHI | T170 | 7680 / 7680 | L | 0.0 + 190.5 | 7 |  | `Ｘenon Ｎo.21 ""` | = = | Towering Past | 2 scripts |
| XENON_22.M | 1795 | ABCD/GHI | T200 | 6180 / 6144 | L | 0.6 + 99.2 | 2 |  | `Ｘenon Ｎo.22 ""` | = = | 7th | 4 scripts |
| XENON_23.M | 854 | ABC/GHI | T160 | 1548 / 1536 | L | 0.3 + 42.5 | 1 |  | `Ｘenon Ｎo.28 ""` | = = |  | 3 scripts |
| XENON_24.M | 4842 | ABCD/GHI | T193 | 4224 / 4224 | L | 0.0 + 76.8 | 8 |  | `Ｘenon Ｎo.24 ""` | = = |  | 9 scripts |
| XENON_25.M | 3985 | ABCD/GHI | T185 | 5400 / 5376 | L | 0.5 + 110.1 | 3 |  | `Ｘenon Ｎo.27 ""` | = = |  | 7 scripts |
| XENON_26.M | 4054 | ABCD/GHI | T180 | 4668 / 4608 | L | 1.3 + 101.0 | 8 |  | `Ｘenon Ｎo.26 ""` | = = | Mood | 7 scripts |
| XENON_27.M | 379 | ABC/GHI | T180 | 384 / 0 | none | 8.4 + 0.0 | 1 |  | `Ｘenon Ｎo.27 ""` | = = |  | 4 scripts |
| XENON_28.M | 1482 | ABC/GHI | T170 | 1536 / 0 | none | 38.1 + 0.0 | 6 |  | `Ｘenon Ｎo.15 ""` | = = |  | — |
| XENON_29.M | 3452 | AB/GHI | T200 | 2880 / 2496 | L | 6.2 + 40.3 | 7 |  | `Ｘenon Ｎo.28 ""` | = = |  | 2 scripts |

Observations (**verified** from the data / script scan of all 40 .CC files):
* "Sound-effect songs" (memo `Ｓound Ｅffect`, short `[ ]0` loops, 1–2 parts): XENON_07 (シャトル発進 "shuttle
  launch"), XENON_08 (シャワー "shower"), XENON_19 (carries XENON_07's memo header verbatim), and the short
  ambience loops XENON_14, 18, 20. XENON_19's `[ ]0` body is a single note tied to itself (`F9 … 54 60 FB F8 00`
  in parts A and B): it is struck once and then held, never re-keyed (pmd-sequencer.md §11.1a). XEN_03A/XEN_03B are the jingles of the OPENING sequence, op 29h (memo `Ｓound effect`; engine.md §4.2 — the C's Ware logo itself plays only SE 46).
* Memo numbering does not always match the file name (XENON_19 "No.07", 23 "No.28", 25 "No.27", 28 "No.15",
  29 "No.28") — copy-paste in the MML headers; irrelevant to the engine.
* XENON_28 is referenced by no script (and not by M.CC) → unused. XENON_02 is used only by the engine (staff
  roll 0E31:05EC) and the music mode. XENON_10 is used by S0111 only.
* **Data bug**: the music-mode script M.CC entry "Ago" loads `xenon_102` (string length 9 at M.CC+0x211 after
  decompression); no such file exists, so `music_load` stops the current song and loads nothing — that menu
  entry is silent. The intended file is presumably XENON_10 (position in the list 01..06,09,?,11..)
  (**inferred**).
* Only XENON_01 and XENON_02 contain a status command (`~+1`, cmd DBh), exactly the two songs the engine/script
  waits on with `music_wait_status(0)`.
* Music-mode titles (M.CC): 01 Desert Storm, 02 XENON, 03 Lanhua, 04 蝶, 05 << QUADRA >>, 06 Ending II,
  09 Synaps, 10(?) Ago, 11 Trust, 12 Silence, 13 Encoder, 15 Emergency, 16 Moon, 17 Dream, 21 Towering Past,
  22 7th, 26 Mood.

## 5. Sound effects — `XEN_SE.EFC` (PMD FM effect file)

Loaded verbatim into PMD's FM-effect buffer (AH=0Bh address) by `snd_init` (§1.1). Played by
`se_play(n)` → PMD `fm_effect_on` (`PMD:29A8`), which (**verified**):
1. stops a running effect, stores the number (311Dh) and sets the "effect active" flag (311Eh);
2. masks FM channel 3 of the music (sets bit 1 of the part flags of the four FM3 part structures 3258h, 32B4h,
   3310h, 336Ch) — **the music's parts C, D, E and F (everything on FM3) are silenced while an effect plays**;
   there is no priority: a new effect simply replaces the running one;
3. clears the effect part work (3594h, 5Ch bytes), sets its pointer to `buf + word[buf + 2*n]`, volume
   defaults, and saves/overrides OPN register 27h (value 3Fh);
4. the effect part is then interpreted by the same FM bytecode interpreter as music parts, on channel 3,
   **every Timer-A interrupt (≈18.46 ms per clock, independent of the song tempo)** (`PMD:2BDF` → `PMD:032B`);
   voices are looked up in the effect file's own voice table; when the part reaches 80h the effect turns
   itself off (`PMD:034B`) and restores register 27h. Effect-off does **not** clear the mask bit: each
   music part retakes the channel at its own next note boundary (FM `PMD:0796`), so the music can stay
   silent on FM3 for up to one note length after the effect (**verified**, pmd-driver.md §7).

### 5.1 Format (**verified** against PMD:032B/29EF and the file)

| offset | size | field |
|---|---|---|
| 0000h | 127 × u16 | effect n stream offset (relative to **file start**), n = 0..126 |
| 00FEh | u16 | voice-table offset (here 0800h) |
| … | | effect streams: FM-part bytecode identical to §4 (same opcodes/operands), each ends with 80h. `[`/`]`/`:` operands are offsets relative to the file start |
| 0800h | 38 × 26 | voices (same 26-byte layout as .M), numbers 1–26, 28–34, 40, 61, 71, 84, 85 |
| 0BDCh | 2 | `00 FF` |

File is 3038 bytes. Effects 1–50 contain data; 0 and 51–126 point to single `80h` bytes (07B4h…, padded with
80h up to 0800h). Effect numbers ≥ 127 would read the voice pointer as an offset (no range check in PMD or
the engine). `resources/sfx/blobs/se_NN.bin` are the raw streams; because loop operands are file-relative they
are **not relocatable** on their own — keep them at their original offsets (or re-base F9/F8/F7 operands).
"≈ s" = clocks × 18.46 ms (**inferred** clock). `n` = command D4h (triggers a PMD built-in SSG effect from
inside the FM effect; Xenon reaches built-in SSG effects 9, 11, 12, 13, 16, 24 and 37 this way — the
built-in table, its priorities and frame format are in [pmd-driver.md](pmd-driver.md) §7.2).

| # | offset | bytes | ticks | ≈ s | voices (@) | V | commands | scripts using it |
|---|---|---|---|---|---|---|---|---|
| 1 | 0101 | 9 | 36 | 0.66 | 1 | 125 | @ V {} | 24 scripts |
| 2 | 010A | 37 | 24 | 0.44 | 2 | 125 | @ q V [ ] | S00 S00B S00C S0203 |
| 3 | 012F | 21 | 27 | 0.50 | 3 | 127 | @ V ) ( | S00 S00B S00C S0104 |
| 4 | 0144 | 61 | 156 | 2.88 | 3 | 127 | @ V [ ] DD | 10 scripts |
| 5 | 0181 | 45 | 342 | 6.31 | 4,5,6 | 119 | @ V & M * {} CB | S0101 S0201 S0301 S0301B |
| 6 | 01AE | 47 | 170 | 3.14 | 7 | 117 | @ V [ ] ) ( | S0101 S0201 S0301 S0301B |
| 7 | 01DD | 27 | 138 | 2.55 | 8 | 122 | @ V [ ] | S0101 S0301 S0301B |
| 8 | 01F8 | 27 | 156 | 2.88 | 9 | 127 | @ V [ ] _ | S0101 S0201 |
| 9 | 0213 | 34 | 56 | 1.03 | 10 | 122 | @ q V [ ] _ ( | 24 scripts |
| 10 | 0235 | 11 | 36 | 0.66 | 11 | 127 | @ V | 7 scripts |
| 11 | 0240 | 9 | 102 | 1.88 | 12 | 122 | @ V | 9 scripts |
| 12 | 0249 | 21 | 120 | 2.22 | 13 | 127 | @ V & M * {} CB | 5 scripts |
| 13 | 025E | 16 | 24 | 0.44 | 9 | 127 | @ V _ M * | S0103 S0105 S0109 S0402 |
| 14 | 026E | 47 | 33 | 0.61 | 14 | 127 | @ V ) ( | S0103 |
| 15 | 029D | 21 | 21 | 0.39 | 15 | 122 | @ V & _ {} | — |
| 16 | 02B2 | 139 | 67 | 1.24 | 16 | 122 | @ V | S0105 S0107 |
| 17 | 033D | 11 | 18 | 0.33 | 3 | 127 | @ V | 7 scripts |
| 18 | 0348 | 11 | 50 | 0.92 | 17 | 125 | @ V n | S0402 |
| 19 | 0353 | 20 | 90 | 1.66 | 18 | 122 | @ V [ ] ( | S0106 S0301A S0301B |
| 20 | 0367 | 17 | 9 | 0.17 | 4,14 | 111 | @ V | 7 scripts |
| 21 | 0378 | 27 | 320 | 5.91 | 19 | 114 | @ V [ ] | S0107 S0206 |
| 22 | 0393 | 17 | 120 | 2.22 | 20 | 114 | @ V [ ] | S0107 |
| 23 | 03A4 | 9 | 10 | 0.18 | 3 | 127 | @ V | S0107 S0203 |
| 24 | 03AD | 27 | 13 | 0.24 | 21,22 | 127 | @ V ) ( {} n | 13 scripts |
| 25 | 03C8 | 39 | 160 | 2.95 | 19 | 114 | @ V [ ] | 9 scripts |
| 26 | 03EF | 21 | 6 | 0.11 | 3,18 | 127 | @ V | 5 scripts |
| 27 | 0404 | 25 | 193 | 3.56 | 31 | 111 | @ V & M * {} CB | S0108 S0110 |
| 28 | 041D | 25 | 31 | 0.57 | 23 | 119 | @ V & M * {} CB | S0108 |
| 29 | 0436 | 9 | 145 | 2.68 | 24 | 125 | @ V | — |
| 30 | 043F | 209 | 592 | 10.93 | 14,19,25 | 114 | @ V [ ] ) ( | S0109 S0202 |
| 31 | 0510 | 11 | 10 | 0.18 | 3 | 127 | @ V n | S0107 S0109 |
| 32 | 051B | 21 | 31 | 0.57 | 22 | 127 | @ V ) ( n | — |
| 33 | 0530 | 23 | 41 | 0.76 | 9,18,26 | 127 | @ V | S0207 |
| 34 | 0547 | 48 | 270 | 4.98 | 5,9,22 | 127 | @ V & [ ] ) ( M * {} CB | S0111 S0207 S0301 S0301B |
| 35 | 0577 | 57 | 76 | 1.40 | 22,40 | 114 | @ V & [ ] | S0303A S0304B |
| 36 | 05B0 | 24 | 122 | 2.25 | 61,71 | 125 | @ V & {} n | 5 scripts |
| 37 | 05C8 | 36 | 348 | 6.42 | 24,32 | 125 | @ V [ ] M * n CB | S0107 S0111 S0201 S0306A |
| 38 | 05EC | 9 | 192 | 3.54 | 28 | 125 | @ V n | 5 scripts |
| 39 | 05F5 | 30 | 90 | 1.66 | 29 | 119 | @ V ) ( | S0202 S0203 |
| 40 | 0613 | 202 | 246 | 4.54 | 3 | 127 | @ q V [ ] ) ( | S0203 |
| 41 | 06DD | 14 | 144 | 2.66 | 30 | 127 | @ V M * | S0203 S0206 S0301A |
| 42 | 06EB | 34 | 104 | 1.92 | 4,9,22 | 122 | @ V M * CB | S0306B |
| 43 | 070D | 40 | 378 | 6.98 | 4,22,24,33 | 122 | @ V & {} | S0106 |
| 44 | 0735 | 20 | 72 | 1.33 | 34 | 122 | @ V & [ ] {} | S0104 S0107 |
| 45 | 0749 | 21 | 288 | 5.32 | 85 | 122 | @ V & _ M * CB | 5 scripts |
| 46 | 075E | 7 | 96 | 1.77 | 84 | 122 | @ V | engine: opening logo 0E31:0478 |
| 47 | 0765 | 7 | 144 | 2.66 | 33 | 119 | @ V | S0101 S0301 S0301B |
| 48 | 076C | 17 | 8 | 0.15 | 2 | 119 | @ q V | S0101 S0201 S0301 S0301B |
| 49 | 077D | 16 | 96 | 1.77 | 71 | 127 | @ V M * n | S0108 S0303A S0304B |
| 50 | 078D | 39 | 80 | 1.48 | 3 | 127 | @ V [ ] DD | S0104 S0109 |

Script usage summary (all 37 story scripts): most frequent effects are 1 (24 scripts), 9 (24), 24 (13),
4 (10). Effects 15, 29 and 32 are never used; 46 is used only by the engine (opening logo, 0E31:0478).

## 6. Porting notes

* Music state that must be emulated: current song name (save files store the 40 bytes starting at DS:13C8, written by 088D:13A6), "already started" flag, fade-out volume, status1 (for op 48h / staff roll).
* `music_load` of the currently playing name is a no-op and `music_play` then does nothing: scripts rely on
  this to keep music running across scene changes and after loading a save.
* Fade (op 30h) is non-blocking; only `music_fade_stop_wait` blocks. With `/F` PMD keeps the faded song
  running silently; a later op 0Fh without a new 0Eh will *not* restart it (`g_music_started` is still 1).
* SEs pre-empt music channel FM3 (parts C–F) for their duration and until each music part's next note.
* Fade volume set with AH=19h (op 3Ah) is applied per part at its next note or LFO update, not instantly.
* FM volume includes PMD's default FM volume-down (/DF=16, i.e. V·240/256): TL = clamp(TLvoice + 127 − V′, 0, 127).
* Every Xenon part enables Timer-A–driven LFO/envelopes (MX/EX), so vibrato and SSG envelopes step at the
  Timer-A rate (≈54.17 Hz), not per music clock. All of this is in pmd-sequencer.md; reference register
  logs of every song and effect are `resources/music/vgm/*.vgm` and `resources/sfx/vgm/*.vgm`.
* Register writes are not instantaneous on the 26K board: PMD polls the OPN BUSY flag before every write
  (pmd-driver.md, `opn_write`), so consecutive writes are at least one chip sample (≈18 µs at 55 467 Hz)
  apart. A q=0 note writes key-off and key-on back to back (pmd-sequencer.md §6.2) and the chip sees
  off→on. An emulation that delivers a whole timer tick's writes at one instant to a core that samples
  the key state once per chip clock (ymfm) loses the re-trigger; the engine's sound layer therefore
  spaces the writes one chip sample apart (`source/engine/snd/snd_api.c`). The VGM recordings have the
  same coarse timing (writes of one tick share a 1/44100 s slot), so players replaying them with such a
  core suffer the same loss.

### 6.1 Options for the audio component of a reimplementation (assessment, not a decision)

**Data format.** Play the original `.M` files and `XEN_SE.EFC` through a sequencer, not the VGMs. The engine
uses PMD as a live driver, and a pre-rendered stream cannot reproduce: effects taking FM3 (parts C–F)
from the running song until each part's next note; fades and the AH=19h "muffle" applied per part at the
next note or LFO update, with `/F` keeping a faded song running silently; `music_wait_status` blocking on
`~` status marks (XENON_01 clock 384, XENON_02 clock 768) and the loop counter / stopped state; and the
pass-to-pass variation of 3 songs (01 07 17; pmd-sequencer.md §11.1a), which a sequencer clocked by the chip's timers
produces by itself. The VGMs are 2–10× larger than the 0.3–4 KB `.M` files they are derived from.
Their use is as golden test data and listening reference.

**Sequencer.**
1. A port of `re-tools/pmdmodel.py` — the only implementation verified against this exact PMD 4.7g build
   (0 differing clocks, §11.2 of pmd-sequencer.md), including its defaults and quirks (/DF 16 FM
   volume-down, FM3 `C5` rotation quirk, TB=C8h first period, the XENON.BAT options). This is what
   was done: first in the engine, now as lrvne's PMD module (`source/lrvne/docs/pmd.md`), which
   also has rhythm part K and the measure counter for other games of the driver.
2. A third-party PMD reimplementation (PMDWin, pmdmini, …): mostly based on later PMD versions with
   different behaviour, and their licences need checking; would have to pass the same register-log tests.
3. PMD.COM itself in an 8086 interpreter (as `pmdemu.py` does under unicorn): exact by construction, but
   ships the driver binary and a CPU core; best kept as a debugging reference.

The sequencer must be ticked by the emulated chip's Timer A and Timer B (periods from registers 24h–26h,
reloads at the next overflow, as in `pmdemu.py`), not by a host timer, or tempo, effects and LFO phase drift.
Engine-level semantics (load-same-name no-op, play-while-started no-op, stop retry, blocking
`music_fade_stop_wait`) belong in the engine layer above the driver API (§3). If the audio thread renders
ahead, status values become visible to scripts up to one buffer before they are heard (harmless for the
intro / staff-roll syncs at ≤ 20 ms). A DC-blocking high-pass belongs in the output stage (libvgm's
unipolar SSG output showed a DC offset up to ≈ −23 dBFS); the 26K board's analog FM/SSG balance and
filtering are unknown (§7).

**YM2203 emulation cores** (licence as stated in the files, 2026-11-08):

| core | YM2203 coverage | licence |
|---|---|---|
| ymfm (Aaron Giles; current MAME OPN code) | FM + SSG + timers, register-level C++ API | BSD-3-Clause (`LICENSE` in github.com/aaronsgiles/ymfm) |
| libvgm `emu/cores/fmopn.c` (old MAME OPN) + `ay8910.c` (MAME) or `emu2149.c` (Okazaki) | FM + SSG; used by VGMPlay for the checks in pmd-sequencer.md §11.1a | fmopn.c GPL-2.0+; ay8910.c BSD-3-Clause; emu2149.c no licence line in its header |
| fmgen 008 (cisc, 1998–2003; written for the M88 emulator; also the basis of PC-98 music players such as PMDWin — **inferred**, not checked here) | `FM::OPN` (and `OPNA`), SSG via `PSG`, timers via `Timer::Count/GetNextEvent` | custom, non-commercial — below |

**fmgen / fmgon licence.** github.com/nobuyukinyuu/fmgon (examined at commit 86a2ead, 2019-11-14; 46
commits, 2015-12-01 … 2019-11-14) is katahiromz's ("片山博文MZ") 2015–2016 "fmgon" — fmgen 008 with
cosmetic changes (types → `<cstdint>`, formatting, `FM_SAMPLETYPE`/`PSG_SAMPLETYPE` int32 → int16, which
fmgen supports as an option and saturates per store; an unused LFO-table variable commented out; `default:`
cases) plus a note/timbre-level `YM2203` wrapper — which in fact drives fmgen's **OPNA** class at 8 MHz —
an OpenAL test player, and a bundled freealut (LGPL v2). nobuyukinyuu's 2019 commit added English comments
and custom OPNA timbres for BambooCopy; the fork has no LICENSE file of its own. katahiromz's ReadMe says to
use it under the 【著作権、免責規定】 of `OldReadMe.txt`, which is cisc's original fmgen notice (also
"Copyright (C) 2015-2016 Katayama Hirofumi MZ / okrt. All Rights Reserved"; `soundplayer.cpp` and the
wrapper files carry only copyright lines). cisc's terms, translated:
* The author (cisc@retropc.net) owns the copyright. Provided as is, without any express or implied
  warranty; no liability for damage from using or not using it.
* The source may be freely modified, embedded, distributed and used as long as: (1) its origin (author,
  copyright) is stated; (2) distributions are free software (フリーソフト, i.e. freeware); (3) modified
  source distributed is marked with what was changed; (4) source distributions include this text
  unmodified.
* The author would appreciate being told when it is published.
* Embedding part or all of it in commercial software (including shareware) requires the author's prior
  agreement.

Consequences (interpretation, not legal advice): usable in a port distributed free of charge, with
attribution and the notice; not usable in anything sold without cisc's consent; and not combinable with
GPL code (GPL forbids the extra "no commercial distribution" restriction), so it excludes libvgm's GPL
fmopn.c in the same program and a GPL-licensed port. For a register-driven sequencer only fmgen's core
(`FM::OPN`) is needed, best taken from cisc's original 008 distribution rather than fmgon, whose additions
(wrapper, player, OpenAL) are not useful here. ymfm is the core without licence constraints.

**Fidelity, ymfm vs fmgen/fmgon** (assessment from design and documentation; neither core has been run on
Xenon data, and no hardware recording exists to judge against). ymfm is preferred: written 2021 and MAME's
OPN implementation since 0.230, it incorporates later chip research and states as its goal "extremely high
(audibly indistinguishable) accuracy" (not bit-exact — it defers that to the decap-based Nuked cores, none
of which covers the YM2203). It runs at the chip's native rate (3.9936 MHz / 72 ≈ 55.47 kHz) and leaves
resampling to the host. fmgen 008 (2003) uses its own sine/exponent tables and envelope stepping, renders
directly at the output rate by scaling its counters, and its notes list chip behaviour the author did not
understand (e.g. OPNB CSM). fmgon is fmgen with 16-bit mixing (FM and SSG summed and clamped in 16 bits,
less headroom than fmgen's int32 mode) and is unmaintained. What matters for Xenon: the YM2203 has no
hardware LFO (PMD's LFOs and SSG envelopes are software), so the relevant parts are FM operator/EG
behaviour, FM3 extended mode (parts D–F), SSG tone/noise/volume steps and the FM/SSG balance — all present in
both. fmgen's one advantage is familiarity: it is the sound of older PC-98 emulators and players, i.e. what
many listeners heard, not necessarily the hardware. A useful next check (not done): render the same VGMs
through ymfm, fmgen `FM::OPN` and libvgm and compare levels, FM/SSG balance, DC and where they diverge.

## 7. Open questions

* Exact identity of the MMD driver and of PUTCM6/LITRA (no binaries available). Inferred Roland CM-64-class
  MIDI support that was compiled in but disabled.
* Whether the FM Towns version uses the same song set (not examined).
* (Resolved: timer/prescaler behaviour on 26K vs 86 boards is covered in pmd-driver.md §6.)
* PMD AH=0Ah AH/AL meaning confirmed from code; behaviour when the driver is busy (deferred path `PMD:23BE`)
  returns the caller's AH unchanged (0Ah) — would end a `music_wait_status(0)` early; not reachable for AH=0Ah
  (its busy-mask byte at `PMD:23EF+0Ah` is 0).
* The PC-9801-26K's analog output stage — FM/SSG level balance, filtering, DC coupling — is not known; it
  needs a recording from real hardware (VGM does not describe it; emulators differ).
* Effects started while a song plays (FM3 / SSG masking and unmasking at each part's next note) are
  described from code (§6, pmd-driver.md) but not covered by `pmdmodel.py --check` (pmd-sequencer.md §11.2);
  a reimplementation needs mixed song+effect scenarios recorded with `pmdemu.py`.
