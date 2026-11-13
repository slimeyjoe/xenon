# PMD.COM 4.7g — driver core (resident structure, installer, hardware, INT 60h API, timing, effects)

Scope: everything in `resources/original/game/PMD.COM` (19 699 bytes, "PMD for PC9801/88VA Version 4.7g,
Copyright (C)1989,Jul.17th 1994 by M.Kajihara(KAJA)") **except** the per-part music-command interpreter
(note/volume/envelope/LFO/portamento maths, command bytes), which is documented in
[pmd-sequencer.md](pmd-sequencer.md). How AGS.EXE uses the driver is in [sound.md](sound.md).

Conventions: addresses are CS offsets of the COM image (`PMD:xxxx`, ORG 100h; file offset = address − 100h),
as in `ndisasm -b16 -o 0x100 PMD.COM` and the shared listing (`listing/pmd.asm`, made locally as in
README.md "Regenerating"; names in `listing/pmd-names.tsv`). **verified** = read in the code and, where marked *(emu)*, reproduced by running the
binary under unicorn with `re-tools/pmdcore_emu.py` (installer, INT 60h calls, FM IRQ handler; register
writes logged). **inferred** = reasoned, not proven. Chip/PC-98 facts (port meanings, timer formulas) are
general hardware knowledge, not derived from this binary. PMD source code exists publicly; it was not used.

## 1. File and memory structure

The program is a single COM image. Everything below `370Ah` stays resident; the installer (transient part)
starts at `370Ah` and is overwritten by the data buffers after `INT 21h AH=31h`.

| range | size | contents |
|---|---|---|
| 0000–00FF | 100h | PSP. At install the command tail at PSP:80h is replaced by `0Dh,"PMD ver.4.7g",0` (`PMD:3850`, string at `4DDE`) **verified (emu)** — memory-map tools show "PMD ver.4.7g" as the resident's command line. The environment block is freed (`3A19`). |
| 0100 | 3 | `E9 07 36` = `jmp 370A` (installer) |
| 0103 | 2 | `EB 11` = `jmp 0116`. **INT 60h vector points here** (`39C8`). |
| 0105 | 5 | signature `"PMD"`, then `47h 67h` ('4.7' / 'g'). Other programs test `vector+2 == "PMD"` (AGS.EXE does; so does PMD itself, `3728`). |
| 010A | 4 | saved previous INT 60h vector (restored by /R) |
| 010E | 4 | saved previous FM-IRQ vector (chained to when the IRQ was shared, restored by /R) |
| 0112 | 1 | IMR bit of the FM IRQ before installation (0 = it was already unmasked → PMD chains to the old handler) |
| 0113 | 2 | IVT address of the FM IRQ (2Ch/48h/50h/54h) |
| 0115 | 1 | IRQ number (3/10/12/13) |
| 0116–012E | | INT 60h entry (§4) |
| 0148–0316 | | SSG effect engine (§7.2) and its variables at 0309–0316 |
| 0317–22C1 | | song start/stop, the sequencer (pmd-sequencer.md), fade step 21CE, timer init 2214, silence 2248 |
| 22C2–2330 | | OPN register write/read routines (§3.2) |
| 232F–2A70 | | INT 60h dispatcher, gate table 23EF, jump table 2411, API handlers, no-board dispatcher 28E8 + table 2948, FM effect on/off |
| 2A71–2CA8 | | FM timer IRQ handler, Timer-A/B handlers, hot keys (§3.3, §5) |
| 2CA9–2CE8 | | sequencer tables (F-numbers, SSG periods, carrier masks) |
| 2CE9–3095 | | built-in SSG effect table and data (§7.2) |
| 3096–3187 | | work variables (§2) |
| 3188–319F | 18h | part work pointer table (12 words: 31A0 31FC 3258 32B4 3310 336C 33C8 3424 3480 34DC 3538 3594) — returned by AH=10h |
| 31A0–3593 | 3F4h | 11 part work areas × 5Ch (A B C D E F G H I J K; layout in pmd-sequencer.md) |
| 3594–35EF | 5Ch | FM sound-effect part work area |
| 35F0–36EF | 100h | IRQ stack (SP top = **36EEh**, `2A87`); in the file it holds KAJA's message "ここはＳＴＡＣＫエリアです。いつもＰＭＤをご愛用して下さっている方々、どうもありがとうございます(^^)。…by KAJA." AH=0Eh also returns this address (§4). |
| 36F0 | | **music buffer** byte 0 (= .M file byte 0; AH=06h returns this address). The file contains a valid empty song here: `00`, twelve words `0018h`, `80h` at 3709h — every part points to an end mark, so AH=00h before any song is loaded is harmless. |
| 370A–4DF2 | | installer code, messages, option tables (transient) |
| 4DF3–4DFA | | installer BSS (beyond end of file): 4DF3 "FD80 ROM present", 4DF4 /O port (FFh = auto), 4DF5 own IRQ-jumper bits, 4DF6 print-messages flag, 4DF7 saved SP for PMDOPT error recovery, 4DF9 resident paragraphs |

### 1.1 Buffer allocation and resident size (verified, `3876–3902`, emu)

```
musdat  (30EC) = 36F1h                 ; AH=06h returns 36F0h (= musdat-1)
tondat  (30EE) = 36F0h + M*400h        ; voice buffer, V KB
efcdat  (30F0) = tondat + V*400h       ; FM effect buffer, E KB
resident paragraphs = 370h + (M+V+E)*40h          ; DOS 31h, counted from the PSP
```
The voice buffer is zero-filled; the effect buffer is initialised to 128 words `0100h` followed by `80h` at
offset 100h (all 127 effects and the voice-table pointer point at an end mark). The installer refuses
`M < 8` ("曲データバッファは 8KB 以上にして下さい．", `382E`) — with M ≥ 8 the voice/effect buffers start at
≥ 56F0h, above the installer's own end (4DFBh), so initialising them cannot overwrite running code
(**inferred** reason) — and `M+V+E > 40` ("曲+音色+効果音データの合計は 40KB 以内にして下さい．", each addition
checked for carry, `3835–384B`). If the PSP's memory block (MCB size) is smaller than needed:
"メモリが確保出来ません。" (`38A9`).

| options | musdat−1 | tondat | efcdat | resident |
|---|---|---|---|---|
| Xenon HDD `/M10 /V0 /E4 /F /K` | 36F0 | 5EF0 (size 0) | 5EF0 | 6F0h paragraphs = **28 416 bytes** *(emu)* |
| Xenon floppy `/K /M10 /V0 /F` (E default 4) | 36F0 | 5EF0 | 5EF0 | 28 416 bytes *(emu)* |
| defaults (M16 V8 E4) | 36F0 | 76F0 | 96F0 | A70h paragraphs = 42 752 bytes *(emu)* |

With `/V0` the voice buffer has no storage and aliases the effect buffer. A song without an embedded voice
table would read its voices (32 bytes each, `1D4D`) from the effect file; all Xenon songs embed voices, so this
never happens (**inferred** from sound.md §4).

## 2. Work-area variables (3096–3187, plus 0309–0316)

Size b = byte, w = word, d = far pointer. "seq" = belongs to the sequencer (pmd-sequencer.md); listed for
completeness with the evident meaning only. Initial values: from the install code (`3759–37DC`, `3BD2`,
`0443`, `04B1–04F9`, `2214`) unless noted.

| addr | sz | name | meaning |
|---|---|---|---|
| 0309 | w | ssg_eff_ptr | SSG-effect stream pointer (§7.2) |
| 030B | w | ssg_eff_tone | current tone period of SSG ch3 |
| 030D | w | ssg_eff_dtone | tone delta per Timer-A tick (sign-extended byte) |
| 030F | b | ssg_eff_count | Timer-A ticks left in the current frame |
| 0310 | b | ssg_eff_noise | noise period |
| 0311 | b | ssg_eff_nsweep | noise sweep: high nibble signed step, low nibble period |
| 0312 | b | ssg_eff_ncount | noise sweep countdown |
| 0313 | b | ssg_eff_priority | priority of the running SSG effect (0 none, 1 drum, 2 effect) |
| 0314 | b | ssg_eff_number | running SSG effect number, FFh none, bit 7 set = PPSDRV sample |
| 0315 | b | ssg_eff_ppsmode | PPS call flags: bit0 pitch from part, bit1 volume from part |
| 0316 | b | pps_last | last PPS number|80h (repeat → stop first) |
| 3096 | w | cur_fm_addr_port | port pair used by `opn_write_fm` 22EA; always = 30F2 in this build (`04D2`, `3A35`) |
| 3098 | w | cur_fm_data_port | = 30F4 |
| 309A | w | api_ds | INT 60h: caller's DS, returned DS |
| 309C | w | api_dx | caller's DX, returned DX |
| 309E | b | api_ah | caller's AH, returned AH |
| 309F | b | api_al | caller's AL, returned AL |
| 30A0 | b | cur_channel | seq: channel number 1–3 of the part being processed |
| 30A1–30A2 | b | | seq |
| 30A3 | b | | FFh (`3BEE`): default rhythm-pattern pointer target (= empty pattern) |
| 30A4 | b | | seq (cleared at song start) |
| 30A5–30AA | 6b | | seq: per-channel key-on state |
| 30AB | b | song_end_acc | seq: AND of all parts' loop state, evaluated by music_tick (status2) |
| 30AC | b | pps_enable | PPSDRV use: FFh undecided (install), 0 no, 1 yes |
| 30AD | b | reg27_shadow | value written to OPN reg 27h on every timer acknowledge: FM3 mode bits 6–7 + 3Fh (default 3Fh) |
| 30AE | b | reg27_saved | 30AD saved while an FM effect plays |
| 30AF | w | efc_voice_table | voice table of the effect file (effect part lookups, `1D6C`) |
| 30B1, 30B3 | w | | cleared at song start, otherwise unused |
| 30B5 | w | | 8000h at song start, otherwise unused |
| 30B7 | b | timera_at_timerb | copy of 3119 taken at every Timer-B tick (seq, LFO timing) |
| 30B8 | b | deferred_req | bit0 = start song, bit1 = stop song, executed at the next Timer-B interrupt |
| 30B9, 30BA | b | | seq |
| 30BB | w | pic_cmd_port | 0000h (master) / 0008h (slave) |
| 30BD | b | pic_eoi_cmd | 60h + IR (specific EOI) |
| 30BE | w | pic_imr_port | 0002h / 000Ah |
| 30C0 | b | pic_irq_bit | IMR bit of the FM IRQ |
| 30C1 | b | pic_irq_bit_inv | inverted |
| 30C2, 30C4 | w | irq_saved_ss/sp | interrupted SS:SP |
| 30C6, 30C7, 30C9 | b | | seq |
| 30C8 | b | chip_type | 0 YM2203, 1 YM2608 (detection, §3.1) |
| 30CA–30EB | | | never referenced |
| 30EC | w | musdat_ptr | music buffer + 1 (start of the part-offset table) |
| 30EE | w | tondat_ptr | voice buffer |
| 30F0 | w | efcdat_ptr | FM effect buffer |
| 30F2 | w | opn_addr_port | OPN address/status port (088h/188h/288h/388h) |
| 30F4 | w | opn_data_port | +2 |
| 30F6 | w | opna_ext_addr_port | +4 (YM2608 second register bank) |
| 30F8 | w | opna_ext_data_port | +6 |
| 30FA | d | fmint_hook | far hook called after every Timer-B interrupt (AH=13h) |
| 30FE | d | efcint_hook | far hook called after every Timer-A interrupt (AH=14h) |
| 3102 | w | song_voice_table | seq: embedded voice table of the song |
| 3104 | w | rhythm_table_ptr | seq: R-pattern table |
| 3106 | w | rhythm_ptr | seq: current R pattern (default 30A3) |
| 3108 | b | | FFh at install; saved/cleared during skip and by AH=1Eh entry 0Eh; **never read** in this build |
| 3109 | b | board_present | 1 = sound board found → main API table; 0 → stub table |
| 310A | b | hotkeys_enable | 1 = ESC/GRPH keys active (default), `/K` → 0 |
| 310B–310E | b | voldown_fm/ssg/pcm/rhythm | current volume-down values (seq uses FM/SSG); reloaded from 3164–3167 at every song start |
| 310F | b | song_has_voices | 1 if the .M has an embedded voice table (header word 0 == 1Ah) |
| 3110 | b | song_type | .M byte 0 |
| 3111 | b | status1 | set by music `~` commands (AH=0Ah AH) |
| 3112 | b | status2 | loop counter, FFh = stopped / ended (AH=0Ah AL) |
| 3113 | b | timerb_value | current Timer-B value TB (default C8h at song start) |
| 3114 | b | fade_speed | AH=02h value; 0 = no fade |
| 3115 | b | fade_vol | 0 full … FFh silent (AH=08h/19h) |
| 3116 | b | timerb_base | TB as last set by the music (relative tempo base) |
| 3117 | b | measure_len | clocks per measure, 60h at song start (music can change it) |
| 3118 | b | measure_clock | clock within the measure |
| 3119 | b | timera_count | incremented by every Timer-A interrupt |
| 311A | b | | tested by the SSG-effect start (`0177`, ≠0 → ignore); never written → effects always allowed |
| 311B, 311C | b | | seq: SSG noise/mixer shadows |
| 311D | b | fm_eff_number | FFh = none (AH=11h) |
| 311E | b | fm_eff_active | 1 while an FM effect plays |
| 311F | b | opt_I | `/I`: 1 = keep interrupts disabled in API and timer code |
| 3120, 3121, 3123 | | | cleared, never used |
| 3125 | b | pcm_eff_number | FFh, never changed (AH=12h) |
| 312E, 3136 | | | seq (rhythm) |
| 3135, 3138, 313A | | | initialised data 3Ch / 2CE9h / 03ADh in the file, never referenced |
| 313C | b | playing | 1 = song running |
| 313D | b | paused | 1 = paused by AH=1Ah |
| 313E | b | stop_after_fade | 1 default; `/F` → 0 (song keeps running at fade volume FFh) |
| 313F | b | opt_N | `/N` value; only used inside the installer's PPS decision, no effect at run time |
| 3140 | b | in_timerb | 1 during Timer-B (music) processing |
| 3141 | b | in_timera | 1 during Timer-A (effect) processing |
| 3142 | b | int60_nest | INT 60h nesting count (inc on entry, dec on exit) |
| 3143 | b | int60_error | 0 = last call executed, FFh = refused by the gate or AH ≥ 23h |
| 3144 | b | | 1 after a board was found; never read |
| 3145 | b | opt_KE | modifier mask for ESC-stop (default 10h = CTRL) |
| 3146 | b | opt_KG | modifier mask for GRPH fast-forward (default 10h = CTRL) |
| 3147 | b | remove_lock | bit7 FM hook set, bit6 effect hook set → /R refused |
| 3148–314F | 4w | | seq |
| 3150 | w | opn_wait_loops | `loop` count between address and data write (`/W`, or calibrated) |
| 3152 | w | ns_per_loop | calibration result (ns per `loop` iteration) |
| 3154 | b | opt_G | Timer-B value while fast-forwarding (default FAh = 250) |
| 3156 | b | timerb_written | last value written to reg 26h (writes are skipped if unchanged) |
| 3157 | b | fade_from_music | 1 when a fade was started by the music command D2h; cleared by stop/ESC; never read |
| 3158 | b | opt_A | `/A0..2`; never read at run time |
| 315A | b | opt_S | current `/S` flag (seq command may set it); never read |
| 315B | w | measure_counter | measures since song start (AH=05h) |
| 315D | b | irq_depth | inc/dec around the FM IRQ handler; informative only |
| 315E | b | reg22_shadow | YM2608 LFO register shadow (set 0 at song start) |
| 315F | b | tempo | seq: tempo t = 4396/(256−TB) (`1468`) |
| 3160 | b | tempo_base | seq |
| 3161 | b | opt_KR | modifier mask for GRPH rewind (default 01h = SHIFT) |
| 3162 | b | hook_flags | bit0 FM hook active, bit1 effect hook active |
| 3163 | b | skip_state | 1 while skipping forward, 2 while rewinding (AH=1Ch / keys), 0 otherwise |
| 3164–3167 | b | def_voldown_* | `/DF /DS /DP /DR` values (defaults 16, 0, 0, 0) |
| 3168 | b | def_opt_S | `/S` default |
| 3169 | b | song_start_count | incremented by every song start; never read |
| 316A–3176 | 13b | | block returned by AH=21h; never read or written by this binary |
| 3177–3179 | b | bufsize_m/v/e | /M /V /E in KB (AH=22h) |
| 317A–3185 | 12b | | cleared at install, otherwise unused |
| 3186 | w | | contains 30ECh in the file; never referenced |

## 3. Hardware layer

### 3.1 Board detection (verified, `3A2B` → `3C2A`; emu with a model OPN/OPNA at 188h)

Ports probed: base `088h,188h,288h,388h` (address/status = base, data = base+2, OPNA bank-1 address/data =
base+4/+6), or only `0n88h` with `/On`. Three passes, first hit wins (`3C2A`):

1. **OPNA with active extended ports** (`3D26`): if port A460h exists (reads ≠ FFh) and the ROM word at
   FD80:0002 ≠ 2A27h or its byte FD80:0004 ≥ 6, write A460h ← A460h & FCh (`3D4B`). Then for each base:
   address ← FFh, read data: must be 01h (YM2608 ID register), and `in(base+4) & in(base+6)` must **not**
   be FFh. Found → chip_type = 1 (YM2608).
2. **PC-9801-86 style board** (`3C7F`): requires A460h to exist and (no FD80 signature or version ≥ 6);
   clears A460h bits 0–1, probes 188h and 288h only (or /O): ID 01h **and** extended ports reading FFh
   (bank 1 disabled). Then A460h ← (A460h & FCh) | 1, A66Eh ← A66Eh & FEh, and if the FD80 signature
   exists `out 6Eh,1`. chip_type = 1. (**inferred**: A460h bit0 enables the YM2608 extension on the 86
   board, A66Eh bit0 is the 86-PCM mute, FD80 is a PC-9821 built-in-sound ROM; the code only proves the
   bit operations.)
3. **YM2203 (26K)** (`3C34`): for each base write reg 0Bh ← AAh and read it back; AAh → found,
   chip_type = 0.

Probe I/Os are separated by 100-iteration `loop` delays; accesses to A460h/A66Eh are followed by `out 5Fh`
(PC-98 0.6 µs wait port).
On success: 30F2/30F4/30F6/30F8 = base, +2, +4, +6 (`3DA1`), 3096/3098 = 30F2/30F4, board_present = 1 and
the REG-DATA wait is calibrated (§3.2). On failure board_present = 0 and "WARNING:ＦＭ音源ボードが装着されて
いません．" is printed (`3A5A`); PMD still installs with the stub API (§4.3) *(emu)*.

**What the chip type changes** (verified): the wait threshold (§3.2), the board message, and the song-start
init `04FA`, which on YM2608 additionally writes B4h/B5h/B6h ← C0h (L+R output for FM1–3), 22h ← 0 (LFO off,
shadow 315E) and bank-1 reg 10h ← 80h then 18h (ADPCM flag reset, mask BRDY/ZERO). Nothing else ever touches
bank 1: **this PMD.COM drives only the OPN-compatible subset on both boards** (FM ch1–3 incl. FM3
extended mode, SSG); YM2608 FM4–6, rhythm and ADPCM stay unused, and part J's data is never processed
(`05A0` does not visit 34DC). (Which .M parts are audible is for pmd-sequencer.md.)

### 3.2 Register access (verified `22C2`, `22EA`, `2312`)

`opn_write` (DH = register, DL = value): `pushf/cli`; read the status port twice, then spin while bit 7
(BUSY) is set; `out addr,DH`; `loop` [3150h] times; `out data,DL`; `popf`. There is no wait after the data
write — the next write's BUSY poll covers it. `opn_write_fm` is identical via 3096/3098; `opn_read_reg07`
returns the SSG mixer (address 07h, wait, `in data`).

**REG-DATA wait (`/Wn`)**: `/W1..255` sets the loop count directly; `/W` without a number (and the default)
calibrates (`4316`): PIT channel 0 is programmed mode 3 with count 246 (2.4576 MHz clock) or 200
(1.9968 MHz, when 0:0501h bit 7 is set) = one IRQ0 every ≈100 µs, INT 08h temporarily points at a counter
ISR (`43CF`: `inc bx`, EOI), IMR = FEh, then 2 × 50 000 `loop` iterations run with interrupts on; the tick
count equals the duration of one iteration in ns (100 000 iterations × t ns = ticks × 100 µs). The PIT is then
reloaded with FFFFh, IMR and INT 08h restored. Loop count = smallest k with k·t ≥ threshold, threshold
**4257 ns (10A1h) for YM2203, 2128 ns (0850h) for YM2608** (`4316–43C9`). The installer prints
"FM音源LSI REG-DATA間 Waitloop回数: k回 (約 k·t ns)". `/W0` is rejected.

### 3.3 Interrupt line, PIC and vector (verified `3A75`, `3B51–3BAE`, `400C`, `39B4`; emu)

The board's IRQ jumper is read through the SSG I/O port: reg 07h ← 3Fh (ports A/B input), read reg 0Eh,
bits 7–6 index the table at `4D63` (4 bytes: INT-digit for the message, IRQ number, IVT address):

| reg 0Eh bits 7–6 | board INT | IRQ | vector | PIC |
|---|---|---|---|---|
| 00 | INT0 | 3 | 0Bh (IVT 2Ch) | master |
| 01 | INT6 | 13 | 15h (IVT 54h) | slave IR5 |
| 10 | INT4 | 10 | 12h (IVT 48h) | slave IR2 |
| 11 | INT5 | 12 | 14h (IVT 50h) | slave IR4 |

PIC parameters (`3B51`): master IRQ (bit 3 of the IRQ number clear): command port 00h, IMR port 02h;
slave: command 08h, IMR 0Ah; bit = 1 << (IRQ & 7); EOI command = 60h | (IRQ & 7) (specific EOI).
Message: "Int n,Port m88H(YM2203|YM2608)を使用します．" (digits patched into 44A2/44A9).

Before hooking (`3934–398F`): for each of the four base ports whose status BUSY clears within 256 polls and
whose reg 0Eh jumper bits equal ours, write 29h←0, 24h←0, 25h←0, 26h←0, 27h←3Fh (with fixed 256-loop
waits) — i.e. every board (ours included) on the same interrupt line is put into a known timer state
(**inferred** purpose: quiet other OPN boards sharing the line).

Hooking (`3991–39D4`, with `cli`): FM IRQ vector → `CS:2A71` (old saved at 010E), INT 60h → `CS:0103` (old
saved at 010A); then `inst_sound_reset` `3BD2` and `inst_pic_unmask` `400C`: the old IMR bit is saved in
0112h and cleared; for a slave IRQ also master IMR bit 7 (cascade) is cleared. Emulated with INT5: master
IMR ← 7Fh, slave IMR ← EFh *(emu)*. Without a board only INT 60h is hooked.

### 3.4 Chip initialisation (verified, emu register log)

Install (`3BD2`): clear part work 31A0–3593, 3108 = FFh, `0443` (work init), `04FA` (song init: 29h←83h,
YM2608 extras of §3.1), reg 07h←BFh, `0582` (stop: silence), `2214` (timers), unmask IRQ, 29h←83h. Emulated
complete write sequence for Xenon's options on a YM2608 at 188h:

```
07=3F (jumper read)                          29=00 24=00 25=00 26=00 27=3F (same-IRQ board reset)
29=83 B4=C0 B5=C0 B6=C0 22=00 [bank1 10=80 10=18] (04FA)  07=BF
80..8E=FF except 83/87/8B/8F (SL/RR max, FM1-3)  28=00 28=01 28=02 (key off)  07=BF   (silence_all 2248)
26=C8 25=00 24=00 27=3F (2214)               29=83
```
On a YM2203 the same without the B4–B6/22/bank-1 writes. Reg 29h on the YM2203 does not exist (harmless).
`29h = 83h` on the YM2608 = 6-channel mode + Timer A/B interrupt enable (datasheet; PMD.COM still uses only
FM1–3).

### 3.5 Interrupt handler (verified `2A71–2B5F`; emu)

```
fm_irq_handler 2A71:
  cli; cld; irq_depth++ ; push ax ; save SS:SP (30C2/30C4) ; SS:SP = CS:36EE ; push dx, ds ; DS = CS
  IMR |= own bit                          ; FM IRQ cannot re-enter while PMD works
  out pic_cmd, 60h|IR                     ; specific EOI immediately
  if slave: OCW3 0Bh -> port 08h, read slave ISR; if 0: out 00h, 67h (EOI for master IR7)
  loop: wait BUSY clear; al = status & 3 ; if al: timer_dispatch 2B13 ; repeat until no flag
  cli ; IMR &= ~own bit ; restore DS DX SS SP AX
  if [0112] != 0: irq_depth-- ; iret
  else (line was in use before PMD): pushf; call far [010E] (old handler); cli; irq_depth--;
       if status & 3 now set: jmp 2A71 again; else iret

timer_dispatch 2B13:
  select reg 27h, read status into AH, write reg 27h = [30AD]   ; 3Fh|mode: resets both flags, reloads
  sti unless /I ; save bx si di es ; ES = CS
  status 1 -> Timer A only ; 2 -> Timer B only ; 3 -> Timer B then Timer A
```
Because the EOI is sent at entry and interrupts are re-enabled (unless `/I`), all other interrupts —
including lower-priority ones and the system timer — run during PMD's music processing; only the FM IRQ
itself is held off by the IMR.

**Timer-B handler** (`2B60`): in_timerb = 1; if deferred_req bit0 → start song (`036E`), bit1 → stop song
(`0582`); if playing: `music_tick 05A0` (all parts, pmd-sequencer.md), `tempo_reg_write 2C3B`,
`measure_count 2C26`, 30B7 = 3119; in_timerb = 0; if hook bit0: `call far [30FA]`.

**Timer-A handler** (`2BA7`): in_timera = 1; timera_count++; every 8th tick (`count & 7 == 0`): `fade_step
21CE` and `rew_key_check 2C5E`; if an SSG effect runs and it is not a PPSDRV sample: `ssg_eff_tick 0206`;
if an FM effect runs: `efc_part_tick 032B`; if hot keys enabled and playing: ESC check (§5.3); in_timera = 0;
if hook bit1: `call far [30FE]`.

Hooks are called with DS = ES = PMD's CS, on PMD's stack (≈250 bytes total), interrupts enabled unless /I,
after the in_timer flag has been cleared (so a hook may call INT 60h without being gated). BP is not saved
by PMD's handler path; the hook must return with `retf`.

## 4. INT 60h API

### 4.1 Entry, dispatch, gating, deferral (verified `0116`, `232F`, `23BE`, `28E8`; emu)

```
0116: int60_nest++ ; if AH >= 23h -> 013B ; board_present ? 232F : 28E8
232F: cld ; push es bx cx si di ; save caller DS in 309A, AL 309F, AH 309E, DX 309C (old values pushed) ; DS=ES=CS
      g = gate[AH] (23EF)
      if g.bit0 and in_timerb   -> refused
      if g.bit1 and in_timera   -> refused
      if g.bit2 and int60_nest != 1 -> refused
      sti unless /I ; call [2411 + 2*AH]
      return AL=[309F] AH=[309E] DX=[309C] DS=[309A] ; restore 309A-309F ; pop di si cx bx es
      012E: int60_nest-- ; int60_error = 0 ; iret
refused (23BE): sti unless /I ; return the caller's AX/DX/DS unchanged ; 013B: int60_nest-- ; int60_error = FFh ; iret
```
BX, CX, SI, DI, ES, BP are preserved; DX/DS are returned unchanged unless the function sets them; the
caller's flags are restored by IRET. AH ≥ 23h returns AX unchanged with 3143 = FFh *(emu)*. Nothing in
PMD reads 3143; a refused call is silently dropped.

Gate table `23EF` (bits: 1 = refuse inside Timer-B processing, 2 = inside Timer-A processing, 4 = if
INT 60h is re-entered): AH 00/01 = 4; 03/04 = 6; 0C/0D = 7; 0F = 5; 18 = 7; 1A/1B = 5; 1C = 7; 1E = 7;
all others 0. The table has only 22h bytes: the gate of **AH=22h is the low byte of the jump table's first
entry, 5Ah** → bit1 (refused inside Timer-A processing).

Deferral: AH=00h/01h check in_timerb|in_timera themselves (`035A`, `0569`); if set they only set
deferred_req bit0/bit1, return **AH=FFh** and the real start/stop happens at the beginning of the next
Timer-B interrupt *(emu: mstart with 3140=1 → AX=FF00, 30B8=01; next Timer-B → playing)*. These flags are
only set while PMD's own timer processing runs (hooks run after they are cleared), so deferral only occurs
when some *other* interrupt handler calls INT 60h while it has interrupted PMD.

### 4.2 Function table (board present; jump table `2411`) — verified, getters *(emu)*

"→" = returned registers; registers not listed are returned unchanged.

| AH | handler | gate | function |
|---|---|---|---|
| 00h | 035A | 4 | **start song** in the music buffer (deferred → AH=FFh). `036E`: clear deferral, stop (`0582`), init work (`0443`), parse header (`0394`), fade_vol = 0, chip song-init (`04FA`), timer init (`2214`: TB=C8h, Timer A restarted, 27h←3Fh, measure/clock = 0, measure_len = 60h), playing = 1, 3169++. Running FM/SSG effects are not stopped. |
| 01h | 0569 | 4 | **stop song** (deferred → AH=FFh). 3157 = 0; `0582`: playing = paused = fade_speed = 0, status2 = FFh, fade_vol = FFh, `silence_all 2248`. |
| 02h | 0326 | 0 | **fade** AL → fade_speed. 1–7Fh fade out by AL per step; 80h–FFh fade **in** by 256−AL per step; 0 stops a fade. (§5.1) |
| 03h | 016E | 6 | **SSG effect on** AL (built-in table, §7.2; with PPSDRV and AL ≥ 80h: PPS sample AL&7Fh) |
| 04h | 0282 | 6 | **SSG effect off** |
| 05h | 2457 | 0 | → AX = measure counter (315B) |
| 06h | 2462 | 0 | → DS:DX = music buffer − 1 (byte 0 of the .M file) = CS:36F0 |
| 07h | 246F | 0 | → DS:DX = voice buffer (CS:5EF0 with Xenon's options) |
| 08h | 2487 | 0 | → AL = fade_vol |
| 09h | 2492 | 0 | board check → AL = 00h, AH = 47h, DX = 4767h ("4.7g") |
| 0Ah | 24A3 | 0 | → AH = status1, AL = status2 (FFh = stopped) |
| 0Bh | 247B | 0 | → DS:DX = FM effect buffer (CS:5EF0) |
| 0Ch | 29A8 | 7 | **FM effect on** AL (§7.1) |
| 0Dh | 2A1D | 7 | **FM effect off** |
| 0Eh | 24AE | 0 | → DS:DX = CS:35F0 ("PCM table address"; here the IRQ stack area — no PCM in this build) |
| 0Fh | 2993 | 5 | PCM effect: **no-op** in this build |
| 10h | 24BA | 0 | → DS:DX = CS:3188 (part work pointer table, 12 words) |
| 11h | 24C6 | 0 | → AL = FM effect number (FFh none) |
| 12h | 24CD | 0 | → AL = PCM effect number = FFh always |
| 13h | 24D4 | 0 | set **FM-interrupt hook** = DS:DX (called after each Timer-B interrupt); DS:DX = 0:0 disables. Sets/clears hook_flags bit0 and remove_lock bit7. |
| 14h | 24FB | 0 | set **effect-interrupt hook** = DS:DX (after each Timer-A interrupt); 0:0 disables; hook_flags bit1, remove_lock bit6. |
| 15h | 253C | 0 | → AH = SSG effect number (0314), AL = its priority (0313; 0 = none) |
| 16h | 2592 | 0 | joystick: reg 07h ← (reg07&3Fh)\|80h (port B output), reg 0Fh ← (reg0F&0Fh)\|80h, AL = reg 0Eh & 3Fh; reg 0Fh ← (…)\|C0h, AH = reg 0Eh & 3Fh. (**inferred**: 26K joystick port, two sticks selected by port B bit 6, bits active low) |
| 17h | 2549 | 0 | → AL = pps_enable |
| 18h | 2550 | 7 | pps_enable = AL if a PPSDRV signature is present at INT 64h, else 0 |
| 19h | 248E | 0 | fade_vol = AL (no fade started) |
| 1Ah | 2626 | 5 | **pause**: if playing: playing = 0, paused = 1, `silence_all` |
| 1Bh | 263B | 5 | **resume**: if paused: re-send the voices of FM parts A, B and (unless an FM effect plays) C–F (`0EA2`), paused = 0, playing = 1 |
| 1Ch | 26FD | 7 | **skip to measure DX** (§5.2) → AL = 0 reached, 1 song ended while skipping, 2 not playing |
| 1Dh | 26A0 | 0 | → DS:DX = memo string AL of the loaded .M (0:0 if none). Requires header word 0 = 1Ah; the word at voice_table−2 must be 0040h, or byte voice_table−1 = FEh with version byte (voice_table−2) ≥ 41h; version ≥ 42h skips one extra entry; the memo table (offset at voice_table−4) is walked until a 0 word. |
| 1Eh | 27ED | 7 | **part mask** AL (bit 7 set = unmask), table `28B8` below; AL&7Fh ≥ 10h ignored |
| 1Fh | 2522 | 0 | → DS:DX = FM-interrupt hook |
| 20h | 252F | 0 | → DS:DX = effect-interrupt hook |
| 21h | 2574 | 0 | → DS:DX = CS:316A (13-byte block, unused by this binary) |
| 22h | 257F | 5Ah* | → AL = /M, AH = /V, DL = /E (KB; DH unchanged) — Xenon: AX = 000Ah, DL = 04h *(emu)* |

Part-mask table `28B8` (3 bytes: part index into 3188, channel, type 0 FM / 2 SSG / 3 none / 4 rhythm):

| AL | part | ch | type | AL | part | ch | type |
|---|---|---|---|---|---|---|---|
| 0 | A | 1 | FM | 8 | I | 3 | SSG |
| 1 | B | 2 | FM | 9 | J | 1 | 3 (nothing) |
| 2 | C | 3 | FM | 0Ah | K | 3 | 4 |
| 3 | D | 3 | FM | 0Bh | D | 3 | FM |
| 4 | E | 3 | FM | 0Ch | E | 3 | FM |
| 5 | F | 3 | FM | 0Dh | F | 3 | FM |
| 6 | G | 1 | SSG | 0Eh | (FFh) | | 3108 ← 0 / FFh only |
| 7 | H | 2 | SSG | 0Fh | effect part 3594 | 3 | FM |

Masking sets bit 0 of the part's flag byte (+3Bh); only if the flags were 0 and a song plays is the channel
silenced at once (FM: `1D7B` mute voice; SSG: mixer bits off; type 4 also forgets a running drum, 0313 = 0).
Unmasking clears bit 0; when the flags become 0 an FM part gets its voice re-sent (`0EA2`). The flag byte
survives song starts (`0443` keeps bits 0–4), so a mask stays in force across songs.

### 4.3 No-board dispatcher (`28E8`, table `2948`; verified, emu)

Same register save/restore, no gating, no `sti`. Handlers: 06h, 07h, 0Bh, 0Eh, 10h, 1Dh, 1Fh–22h are the
real ones (buffers still exist); 05h, 08h, 11h, 12h, 16h, 17h → AL = FFh; 09h → AL = FFh, AH = 47h,
DX = 4767h; 0Ah, 15h → AX = FFFFh; all others (00–04, 0C, 0D, 0F, 13, 14, 18–1C, 1E) are no-ops returning
AX unchanged. AGS.EXE's `pmd_board_present` uses AH=09h AL to tell the cases apart (sound.md §2).

## 5. Fade, skip, hot keys

### 5.1 Fade (verified `21CE`, emu)

`fade_step` runs every 8th Timer-A interrupt (8 × 18.46 ms ≈ 147.7 ms) unless paused, also when no song
plays. speed s = fade_speed (signed):

```
s == 0: nothing
s  > 0: v = fade_vol + s ; if carry: fade_vol = FFh, fade_speed = 0, and if stop_after_fade: deferred_req |= 2
         else fade_vol = v
s  < 0: v = fade_vol + s (i.e. − (256−s)) ; if no carry (underflow): fade_vol = 0, fade_speed = 0 else fade_vol = v
```
A fade-out with speed s takes floor(255/s)+1 steps (s = 20: 13 steps = 104 Timer-A ticks ≈ 1.92 s *(emu)*).
With `/F` (Xenon) the song keeps running at fade_vol FFh *(emu)*; otherwise it is stopped at the next
Timer-B interrupt. How fade_vol attenuates FM/SSG output (and that SSG drums are not triggered while
fade_vol ≠ 0, `0A3A`) is in pmd-sequencer.md. Song start resets fade_vol to 0, stop sets FFh; AH=19h sets
it directly.

### 5.2 Skip / rewind (verified `26FD`, `2720`; emu)

`skip_to_measure(DX)` (AH=1Ch masks the FM IRQ in the IMR around it; the REW key calls it from inside the
interrupt): status2 = FFh → AL = 2. If DX ≤ current measure: skip_state = 2, stop a drum-priority SSG effect,
re-init the parts (`044E`, keeps fade), re-parse the header; DX = 0 → `silence_all`, done. Otherwise (or
after the restart) skip_state = 1: all parts get flag bit 7 (silenced), fade_vol is saved and set to FFh,
then `music_tick` + `measure_count` run in a tight loop until measure ≥ DX or status2 = FFh; fade_vol and
3108 are restored, parts unmasked (voices re-sent). Finally reg 26h is written with `opt_G − 1` (249) to
invalidate the Timer-B cache — the next Timer-B period is therefore ≈2 ms before the next tick rewrites the
song's value. AL = 0 / 1. *(emu: AH=1Ch DX=3 → AL=0, measure 3, slave IMR FFh during the skip.)*

### 5.3 Hot keys (verified `2BDF`, `2C3B`, `2C5E`, `2C81`; emu)

Active when hotkeys_enable (default; **`/K` disables — Xenon uses /K**) and a song plays. PMD reads the
PC-98 BIOS keyboard state: key bitmap 0:052A (bit 0 of 052Ah = ESC) and shift state 0:0538 (bit0 SHIFT,
1 CAPS, 2 KANA, 3 GRPH, 4 CTRL).

| action | keys (default) | checked | effect |
|---|---|---|---|
| stop | ESC + KE mask (CTRL) | every Timer-A tick | deferred stop (next Timer-B), 3157 = 0 *(emu: CTRL+ESC)* |
| fast forward | GRPH + KG mask (CTRL) | every Timer-B tick | reg 26h = opt_G (250) instead of TB while held *(emu: 26h=FA, back to AA on release)* |
| rewind | GRPH + KR mask (SHIFT) | every 8th Timer-A | if measure_clock ≥ 16: skip to the start of the current measure, else of the previous one (measure 0 stays 0) |

"Mask matches" means `(0:0538 & mask) == mask` (a mask of 0 = no modifier needed). Since the FF test also
needs GRPH and the REW test is a superset, holding CTRL+SHIFT+GRPH fast-forwards and rewinds.

## 6. Timing model

* OPN clock (hardware): 3.9936 MHz on the 26K (YM2203); the 86 board's YM2608 runs at 7.9872 MHz with an
  internal ÷2 relative to the OPN, giving the same timer rates (**inferred**, datasheet; PMD writes no
  prescaler register 2Dh–2Fh — none appears in the emulated logs).
* **Timer A** is programmed with NA = 0 (24h = 25h = 0, at install and every song start, `2226`):
  period = 72 × 1024 / 3 993 600 s = **18.46 ms** (54.17 Hz). It drives SSG and FM effects (one effect clock
  per interrupt), the ESC test, and every 8th interrupt the fade step and rewind test.
* **Timer B** = one PMD music clock: period = 1152 × (256 − TB) / 3 993 600 s = (256 − TB) × 288.46 µs.
  TB is C8h (16.15 ms) from song start until the song's first tempo command (XENON_01 sets AAh = 24.8 ms on
  its first tick *(emu)*). Reg 26h is only rewritten when the value changes (cache 3156). The music's tempo
  value t relates to TB by t = round(4396 / (256 − TB)) (`1468`; TB ≤ FAh; pmd-sequencer.md).
* Clock counting: each Timer-B tick of a playing song increments measure_clock; at measure_len (60h unless
  the song changes it) it wraps and measure_counter++ (`2C26`).
* Both timers run continuously from install on (27h = 3Fh); when no song plays Timer-B interrupts only call
  the FM hook. Both timers are restarted (reloaded, flags reset) at every song start.
* Every IRQ acknowledges with reg 27h ← 30AD (3Fh, or 7Fh while the music uses FM3 extended mode — seen
  with XENON_01 *(emu)*), i.e. both timers are reloaded/reset even if only one flag was set.

## 7. Sound effects

### 7.1 FM effects (EFC buffer; verified `29A8`, `2A1D`, `032B`, `0796`; emu with XEN_SE.EFC #1)

`fm_effect_on(AL=n)` (`pushf/cli`):
1. if an effect is active: `fm_effect_off` first — **no priorities**: a new effect always replaces the old;
2. fm_eff_number = n, fm_eff_active = 1, cur_channel = 3;
3. sets bit 1 ("channel taken by effect") in the flag bytes of the four structures that use FM channel 3:
   C (3258) and D/E/F (32B4/3310/336C);
4. clears the effect part (3594, 5Ch bytes), pointer = efcdat + word[efcdat + 2n] (offsets are relative to
   the buffer start; no range check), length counter 1, volume 6Ch, slot/carrier masks F0h/FFh (the same
   defaults the song parser gives FM parts A–C);
5. reg27_saved = 30AD; 30AD = 3Fh → the next IRQ acknowledge switches FM3 back to **normal mode** for the
   effect.

Every Timer-A interrupt `efc_part_tick 032B` sets the effect voice table (30AF = efcdat + word[efcdat+FEh]),
temporarily sets cur_channel = 3 and runs the **same FM part interpreter as the music** (`fm_part_tick
0643`, di = 3594; all commands and their semantics are in pmd-sequencer.md; voice lookup for this part uses
30AF, `1D66`). When the stream is at 80h and the length counter is 0 → `fm_effect_off`. An effect therefore
advances one clock per 18.46 ms regardless of the song tempo (emu: effect #1 ended after 37 Timer-A
interrupts = 36 clocks + 1; register log shows voice load on ch3 regs 32h–8Eh/B2h, A6h/A2h F-number, key-on
28h = F2h).

`fm_effect_off` (`pushf/cli`): fm_eff_number = FFh, active = 0, mute the effect channel (`1D7B`,
TL/SL/RR = 7Fh, key off); then **only if a song is playing** (`2A34`): re-send the voices of C, D, E, F
(`0EA2`, using each part's current voice number — also for parts that have no data, which then loads
whatever record the linear voice search hits, harmless because the channel stays keyed off *(emu)*), restore
30AD from 30AE and write reg 27h = 30AD & CFh (mode bits restored without resetting flags). With no song
playing 30AD stays 3Fh; the next song start sets it to 3Fh anyway (`04C2`).

Giving the channel back: the effect-taken bit 1 is **not** cleared by `fm_effect_off`. Each masked music
part keeps counting its note lengths silently (`fm_part_masked 0796`); at its next note boundary, if bit 1
is set and no effect is active, bit 1 is cleared and — if no other mask bit remains — the part resumes
normally with that note. So the music returns on part C at the first note *after* the effect ends, not
mid-note. Starting a song does not clear bit 1 either (`0443` keeps flag bits 0–4).

Music command D3h (pmd-sequencer.md) calls `fm_effect_on`/`fm_effect_off` from music data; AH=0Ch/0Dh are
gated (7): refused while PMD's timer processing runs or when INT 60h is re-entered.

### 7.2 SSG effects (built-in table; verified `0148–0308`, emu with effect 0)

Played on **SSG channel 3** (regs 04h/05h tone, 06h noise, 07h mixer bits 2/5, 0Ah volume, 0Bh–0Dh
envelope), taking it from music part I (3480). Entry points:

| entry | caller | PPS flags (0315) |
|---|---|---|
| 016E | INT 60h AH=03h | 0 |
| 0167 | music command D4h n (n=0 → stop `0282`) — also from inside FM effects | 1 (pitch) |
| 0148 | rhythm part K drum (`0A4F`), only while fade_vol = 0 | 3 (pitch + volume) |

`ssg_eff_start 0173`: if PPSDRV is in use and the number has bit 7 (always the case from 0148, which ORs
80h): if the current priority ≥ 2 ignore; else mark part I (flag bit 1), priority = 1, number = AL, and call
PPSDRV `INT 64h AH=01h AL=n&7Fh` with BH = pitch (low byte of the part's word +08h, if flag bit0) and BL =
15 − part volume (+12h, if < 16 and flag bit1) (**inferred** PPSDRV register meaning); a repeat of the
previous PPS number first calls `INT 64h AH=00h` (stop). Otherwise the internal table `2CE9` is used:
entry n = {priority byte, word offset from 2CE9h}; if the running effect's priority > the new one's, the
request is ignored (equal priority replaces); with PPSDRV active `INT 64h AH=00h` is called; part I gets
flag bit 1; the first frame is loaded; priority = entry priority. No range check (n ≥ 40 reads garbage).

Frame format (11 bytes, list terminated by FFh), loaded by `ssg_eff_frame 0217`:

| byte | meaning |
|---|---|
| 0 | duration in Timer-A ticks (FFh = end of effect) |
| 1–2 | tone period → regs 04h, 05h |
| 3 | noise period → reg 06h (also kept in 0310) |
| 4 | mixer: bit0 = tone off, bit3 = noise off (rotated into reg 07h bits 2/5; the rest of reg 07h kept) |
| 5 | volume → reg 0Ah (bit 4 = envelope mode) |
| 6–7 | envelope period → regs 0Bh, 0Ch |
| 8 | envelope shape → reg 0Dh (rewritten each frame = retrigger) |
| 9 | signed tone delta added every tick (regs 04h/05h rewritten every tick) |
| 10 | noise sweep: high nibble = signed delta, low nibble = ticks between steps (0 = off) |

Per Timer-A tick (`0206`): count−−; when 0 load the next frame, else tone += delta (write 04h/05h, rewrite
07h) and run the noise sweep. End (`0286`, also AH=04h): PPS stop if in use, reg 0Ah ← 0, reg 07h |= 24h
(tone/noise C off), priority = 0, number = FFh. Part I takes the channel back at its next non-rest note
(`095D`): a priority-1 effect (drum) is cut off by that note, a priority-2 effect keeps the channel until it
ends. The first frame is output immediately by the start call, so an effect ends exactly "sum of
durations" Timer-A ticks after it was started (effect 0: 1+8 = 9 ticks *(emu)*). With PPSDRV, samples are not ticked by PMD (0314 bit 7) and stay
"active" until part I's next note stops them.

Built-in table (40 entries; data 2D61–3095; T/N = tone/noise enabled in the first frame; ticks = sum of
durations). Names of 0–10 follow the PMD SSG-drum convention (K-part bit n triggers effect n:
1 BD, 2 SD, 4 LT, 8 MT, 16 HT, 32 RIM, 64 SD2, 128 HH-closed, 256 HH-open, 512 crash, 1024 ride) — the
names are **inferred** from general PMD documentation and the sound shapes; the numbers are verified.

| n | pri | frames | ticks | first frame: mix, tone, noise, vol/env, Δtone | name (inferred) | Xenon use |
|---|---|---|---|---|---|---|
| 0 | 1 | 2 | 9 | TN 5DCh n1F v0F Δ+127, then T 6A4h env 04B0 | bass drum | |
| 1 | 1 | 1 | 14 | TN 190h n07 env 0BB8 Δ+93, noise sweep F2 | snare | |
| 2 | 1 | 2 | 16 | TN 2BCh v0F Δ+100 | low tom | |
| 3 | 1 | 2 | 16 | TN 1F4h n05 Δ+60 | mid tom | |
| 4 | 1 | 2 | 16 | TN 12Ch Δ+50 | high tom | |
| 5 | 1 | 1 | 2 | T 037h env 012C Δ+100 | rim shot | |
| 6 | 1 | 1 | 16 | N n0F env 0BB8, sweep F1 | snare 2 | |
| 7 | 1 | 1 | 6 | TN 027h env 01F4 | hi-hat closed | |
| 8 | 1 | 1 | 32 | TN 027h env 1388 | hi-hat open | |
| 9 | 1 | 1 | 31 | TN 028h n1F env 1388, sweep F1 | crash | EFC #18 |
| 10 | 1 | 1 | 31 | TN 01Eh env 1388 | ride | |
| 11 | 2 | 2 | 5 | N n0F env 03E8, sweep 71 | | EFC #31 |
| 12 | 2 | 2 | 7 | T 12Ch env 012C shape D | | EFC #24 |
| 13 | 2 | 2 | 8 | N v0E sweep 51 | | EFC #37 |
| 14 | 2 | 2 | 11 | N env 01F4 shape D | | |
| 15 | 2 | 2 | 19 | N n0A env 0064 shape D | | |
| 16 | 2 | 1 | 2 | T 190h env 01F4 | | EFC #36, #49 |
| 17 | 2 | 1 | 4 | N n0F env 03E8 | | |
| 18 | 2 | 2 | 14 | N n1F v0F | | |
| 19 | 2 | 2 | 4 | T 190h → 0C8h | | |
| 20 | 2 | 2 | 12 | T 190h → 0C8h | | |
| 21 | 2 | 5 | 20 | T 190h/064h/0C8h/190h/064h | | |
| 22 | 2 | 1 | 16 | T 7D0h v0F | | |
| 23 | 2 | 2 | 12 | N n1F, then TN Δ+127 | | |
| 24 | 2 | 2 | 36 | N n19 → n14, sweep 13 | | EFC #32 |
| 25 | 2 | 1 | 6 | TN 0C8h Δ+20 | | |
| 26 | 2 | 2 | 20 | TN 028h n14 Δ+20 | | |
| 27 | 2 | 1 | 6 | T 258h | | |
| 28 | 2 | 2 | 20 | T 3E8h Δ+127, then TN Δ+64 | | |
| 29 | 2 | 1 | 4 | TN 3E8h n1F v0F | | |
| 30 | 2 | 1 | 4 | TN FFFh n1F v0F | | |
| 31 | 2 | 2 | 20 | T 1DDh Δ−50, then 0F2h Δ−8 | | |
| 32 | 2 | 2 | 8 | T 064h, TN 00Ah | | |
| 33 | 2 | 2 | 32 | N n05 shape D, TN 01Eh | | |
| 34 | 2 | 4 | 36 | T 12Ch/0B4h/0C8h/096h | | |
| 35 | 2 | 1 | 3 | T 0EEh v0E | | |
| 36 | 2 | 2 | 20 | T 0C8h → 064h | | |
| 37 | 2 | 2 | 32 | TN 000h shape D Δ+1, sweep 11 | | EFC #38 |
| 38 | 2 | 1 | 1 | T 0C8h v0E | | |
| 39 | 2 | 4 | 8 | T 0C8h/064h/032h/019h | | |

"Xenon use": SSG effects referenced by command D4h inside XEN_SE.EFC effects (scan of the 50 effect streams;
no .M file uses D4h, and no Xenon song has a K part with data). Volume 10h (envelope mode) is the common
case and omitted above. Full frame-by-frame dump: `re-tools/pmdcore_emu.py PMD.COM ssgtable`.

### 7.3 PCM effects

Not present: AH=0Fh is a no-op, AH=12h always returns FFh, AH=0Eh returns the stack area.

## 8. Installer and options

### 8.1 Flow (verified `370A–3A29`; emu)

1. Print banner `4420`. If the INT 60h vector has `"PMD"` at +2 (`3723`): resident copy found →
   if it has a board and its FM IRQ vector no longer points at 2A71h → "常駐時とFM割り込みベクトルが違います．"
   exit 1; else **option-change mode** (§8.3).
2. Defaults (`3762–37DC`): M=16 V=8 E=4, keys on, DF=16 (310B/3164), DS=DP=DR=0, /N=FFh, part work cleared,
   /I=0, removal lock 0, /A=0, /S=0, stop_after_fade=1, PPS=FFh (auto), KG=16, KR=1, KE=16, port auto,
   G=250, messages on.
3. `inst_board_init 3A2B` (detect board, wait calibration).
4. Environment variable `PMDOPT=` (`4290`) parsed with the install table; an error prints
   "WARNING:環境変数 PMDOPT の設定に誤りがあります．" and abandons the rest of PMDOPT (SP restored from 4DF7).
   Note: while PMDOPT is parsed DS = environment segment, so `/C` there writes into the environment block
   (harmless bug).
5. Command line (PSP:81h) with the install table; errors print the usage text and exit with code FFh.
6. Checks M ≥ 8, M+V+E ≤ 40; command tail replaced by "PMD ver.4.7g"; memory check; buffers set up (§1.1);
   effect numbers/states reset (0313=0, 0314=FFh, 311D=FFh, 3125=FFh, hooks 0).
7. If a board: `inst_read_irq_jumper 3A75` (jumper, board message, PPSDRV decision, PIC parameters),
   same-IRQ board reset, FM vector hook. INT 60h hook. `inst_sound_reset 3BD2`.
8. Messages (if not /C): wait count, "常駐しました．\r\nINT 60HでPMDを操作可能です．", and if keys enabled
   "ESC/GRPHキーで曲の停止、早送りが可能です．". Free environment, `INT 21h AX=3100h DX=paragraphs`.

PPSDRV decision (`3AF9`): signature = INT 64h vector + 2 equals `"PMP"` (`3BAF`). If `/P` was not given,
pps_enable = 1 if found else 0; `/N` default becomes 0 resp. 1. "PPSDRV(INT64H)に対応します．" if enabled.

### 8.2 Options (install table `4D73`; handlers `4095–421C`; parser `4061`)

Parser: options start with `/` or `-`, spaces are skipped, a control character ends the line, anything
else (and an unknown letter) → usage/error. Letters are case-insensitive. Numbers: decimal byte, wrapping
modulo 256, no digits = "missing" (some handlers ignore that and use 0).

| option | effect (variable) | notes |
|---|---|---|
| /Mn | music buffer KB (3177), def 16 | number required; ≥ 8 |
| /Vn | voice buffer KB (3178), def 8 | number required |
| /En | FM effect buffer KB (3179), def 4 | number required; M+V+E ≤ 40 |
| /On | port 0n88h only, n < 4 (4DF4), re-runs detection | number required |
| /DFn /DSn | FM / SSG volume-down (310B+3164 / 310C+3165), def 16 / 0 | number required |
| /DPn /DRn | PCM / rhythm volume-down (310D+3166 / 310E+3167) | undocumented in the usage text |
| /K | hot keys off (310A=0); `/K-` on | |
| /KEn /KGn /KRn | modifier masks for stop / FF / REW (3145 / 3146 / 3161), def 16/16/1; 1 SHIFT 2 CAPS 4 KANA 16 CTRL | missing number → 0 |
| /F | stop_after_fade = 0 (keep playing); `/F-` = 1 | |
| /I | 311F = 1: no `sti` in the INT 60h dispatcher and the timer dispatcher; `/I-` = 0 | |
| /P | PPSDRV off (30AC = 0); `/P-` = on if resident | |
| /Wn | wait loops (3150), 1–255; `/W` alone recalibrates | /W0 rejected |
| /Gn | Timer-B value for GRPH fast-forward (3154), def 250 | /G alone = 250; /G0 rejected |
| /R | (install mode) "ＰＭＤは常駐していません．" exit 1 | |
| /H | usage, exit FFh | |
| /N, /N- | 313F = 0 / 1 | undocumented; no run-time effect in this build |
| /An | 3158 = n (n < 3) | undocumented; never used |
| /S, /S- | 315A = 3168 = 1 / 0 | undocumented; never read |
| /C | suppress the later install messages (4DF6 = 0) | undocumented |

The usage text marks `*` options as changeable after installation.

### 8.3 Option change and status (resident table `4DA7`, `3DBE`; emu)

Running PMD again while resident parses the command line with the resident table, writing into the
**resident** copy: K, D, I, N, F, P, W, G, A, S, C as above; `/R` = removal; M, V, E, O and Y →
"変更出来ないオプションを指定しています．" exit 1. If any option was given "設定を変更しました．" is printed;
then always the current settings ("----- 現在の設定 -----": buffer sizes, DF, DS, keys on/off, KE, KG, KR,
stop after fade, /I, PPSDRV, wait count and ns, GRPH Timer-B value) and exit 0. A changed /DF etc. takes
effect at the next song start (values are copied from 3164–3167 by `04DB`).

### 8.4 Removal `/R` (verified `3F4F–400B`; emu)

1. If remove_lock ≠ 0 (an FM or effect hook is installed): "現在常駐解除は禁止されています．PMD関連TSRを先に
   解放して下さい．" exit 1.
2. `cli`; if PPSDRV is resident: INT 60h AX=1800h (PPS off); AH=11h → if an FM effect runs AH=0Dh; AH=15h →
   if an SSG effect runs AH=04h; AH=01h (stop song: FM release/key-off, SSG mixer BFh).
3. If the resident copy has a board: OR the saved IMR bit back into the IMR (re-mask only if it was masked
   before), write reg 27h ← 30h (timers stopped, flags reset), restore the FM IRQ vector from 010E.
   (Reg 29h, the YM2608 extras and the master cascade bit are left as they are.)
4. Restore INT 60h from 010A, free the resident block (INT 21h AH=49h, ES = resident PSP);
   "常駐解除しました．" exit 0, or "解放に失敗しました．" exit 1.

*(emu: removal writes SL/RR=FF ×12, 28h=00/01/02, 07h=BF, 27h=30; slave IMR ← FFh; INT 60h and IVT 50h
restored.)*

### 8.5 Messages

All `$`-terminated, Shift-JIS, at 43DB–4D62: "WARNING:" 43DB · no board 43E4 · removed 440B · banner 4420 ·
"Int  ,Port  88H(" 449E + ")を使用します．" 44AF, chip names 44C5/44CC · PPSDRV 44D3 · wait-count 44F2/4516/451E ·
removal forbidden 4524 · vector changed 4567 · M < 8 KB 4590 · total > 40 KB 45BC · free failed 45F3 ·
not resident 460A · unchangeable option 4627 · no memory 4652 · resident 466D · keys 469C · PMDOPT error
46C9 · usage 46F4 · settings changed 4B11 · settings block 4B2A–4D62.

## 9. Porting notes (driver level)

* Xenon runs PMD with /K (no hot keys) and /F (no stop after fade); timers: Timer A 18.46 ms for effects,
  Timer B from the song tempo; both continue while no song plays.
* Effects are independent of the song: FM effects take channel 3 with no priority (newest wins), SSG
  effects (only reachable through D4h inside FM effects in Xenon) take SSG channel 3 with priorities.
* The music part on channel 3 comes back at its first note after the effect ends; masks survive song
  changes.
* mstart/mstop never fail when called from the main program; AH=FFh only appears for calls from foreign
  interrupt handlers.
* The same driver behaviour applies on 26K and 86 boards: PMD.COM uses only the OPN-compatible part of the
  YM2608, so Xenon sounds the same (modulo analogue mixing) on both.

## 10. Verification

`re-tools/pmdcore_emu.py` (unicorn, model OPN/OPNA that only latches registers; wait calibration
short-circuited at 30 ns/loop) was run for: install with both Xenon command lines on YM2608 and YM2203
models, defaults, /M7, /M20 /V20 /E1, /H, no board; all getters; deferral and gate refusal; AH=00h with
XENON_01.M and Timer-B IRQs; AH=0Ch AL=1 with XEN_SE.EFC until the effect ended; AH=03h AL=0; fade speed 20;
CTRL+ESC, CTRL+GRPH; AH=1Ch DX=3; pause/resume; option change (/DF20 /KG4, /M4) and /R. All results quoted
*(emu)* above come from these runs. Not emulated: the real PIT calibration, the FD80/A460 86-board path,
PPSDRV.

## 11. Open questions

* Exact meaning of the FD80:0002 = 2A27h ROM signature / version byte and of port 6Eh in the 86-board
  enable path (PC-9821 built-in sound?).
* Why the same-IRQ board reset writes 27h = 3Fh (it enables both timers on those boards) — intent unclear.
* PPSDRV calling convention (BH/BL meaning) and why its signature is "PMP" — only the bytes are verified.
* `/N`, `/A`, `/S`, 3108, 3144, 3157, 3169, 316A–3185: stored but unused here — presumably shared with other
  PMD variants (PMDB2/PMD86) or external tools.
* YM2608 timer rate equality with the YM2203 (prescaler default) is datasheet-based, not measured.
