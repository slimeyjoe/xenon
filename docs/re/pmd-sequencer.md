# PMD.COM 4.7g — music sequencer (part interpreter, command set, sound math)

Scope: how the resident PMD.COM shipped with Xenon (19 699 bytes, "PMD for PC9801/88VA Version 4.7g",
Jul 17 1994) turns a compiled `.M` song (and the streams of `XEN_SE.EFC`) into OPN register writes, clock by
clock, precisely enough to re-implement it. The driver core (installer, options, memory map, board
detection, IRQ, INT 60h API, fade timing, effect start/stop, SSG effects) is in
[pmd-driver.md](pmd-driver.md); how AGS.EXE uses the driver is in [sound.md](sound.md).

Conventions: addresses are PMD.COM CS offsets (COM, ORG 100h; file offset = address − 100h), as in the shared
listing `$S/pmd/pmd.asm` (names in `names-pmdseq.tsv`). `[xx]` = byte/word at offset xx of the current part's
work area (register DI), `[30A0]` etc. = global variable. **verified** = read in the code *and* reproduced
bit-exactly by the independent model (§11.2) against the real driver running in the emulator (§11.1);
**code** = read in the code only (feature not used by any Xenon file and not covered by the synthetic tests);
**inferred** = interpretation. Unless marked otherwise everything below is **verified**.

Tools:
* `re-tools/pmdemu.py` — runs the real PMD.COM (installer + driver) under unicorn with a fake 26K board,
  loads songs/effects like AGS.EXE and records every register write → VGM (§11.1).
* `re-tools/vgmverify.py` — checks the VGM files against an uncut recording of the real driver (§11.1a).
* `re-tools/pmdmodel.py` — clean-room Python re-implementation of the sequencer written from this document;
  `--check` / `--check-se` compare it with `pmdemu` clock by clock (§11.2).
* `re-tools/pmdinfo.py` — structure dumper (command lengths re-checked, names updated).
* `resources/music/vgm/*.vgm` (31 songs), `resources/sfx/vgm/se_01..50.vgm` — reference recordings.

## 1. Key facts (summary)

1. **This PMD.COM is the OPN build: 3 FM channels + 3 SSG channels only.** Parts A, B, C = FM channels 1–3;
   **parts D, E, F are FM3-extended parts that play individual operators ("slots") of FM channel 3** (they are
   never FM channels 4–6); G, H, I = SSG 1–3; **J (ADPCM) is never processed**; K = rhythm part played with
   SSG drum effects. The same holds on an 86 board (YM2608): PMD.COM drives it OPN-compatibly (it only adds
   pan = L+R on B4h–B6h, reg 22h = 0 and an ADPCM reset, `051F–0566`; pmd-driver.md §3).
   *Consequence for Xenon:* 24 songs use part D with `s` (CF) slot masks: C owns slots 1+2, D slots 3+4 of
   channel 3, both audible on a 26K (sound.md §4.2's "part D lost on 26K" is wrong).
2. One Timer-B interrupt = one music clock; parts are processed in the order **G H I K A B C D E F**
   (`05A0`). Timer A (NA = 0, 18.46 ms) drives effects/fade and — because every Xenon part sets `MX`/`EX`
   (CA/C9/BB = 1) — also the *rate* of all software LFOs and SSG envelopes (§5.3).
3. Note = key-on at the clock the note is read; key-off at the first later clock where the remaining length
   ≤ gate (q); with q = 0 the key-off happens in the same clock as the next note's key-on (re-trigger), and
   `&` suppresses both (§6.3).
4. FM volume: TL(carrier) = clamp(TL_voice + 127 − V′, 0, 127) where V′ = V scaled by the FM volume-down
   value (default **16**, i.e. V′ = ⌊V·240/256⌋) and by the fade (§8.3). SSG: 0–15 plus envelope.
5. Everything not marked **code** was checked by the model against the real code: all 31 songs × 9000 clocks
   (1.6 million register writes), all 50 effects, fades, and two synthetic songs exercising the commands
   Xenon does not use — **0 differences** (§11.2).

## 2. Song start (`mstart 035A`)

INT 60h AH=00h → `mstart` (deferred via 30B8 bit0 if a timer interrupt is in progress, pmd-driver.md §4.1):

| step | address | action |
|---|---|---|
| 1 | `0582` (cli) | stop: 313C (playing) = 0, 313D = 0, fade speed 3114 = 0, status2 3112 = FFh, fade vol 3115 = FFh; `2248` silence: regs 80h–8Eh (SL/RR of all 12 operators, list `22AC`; without ch3 when an FM effect plays, list `22B9`) = FFh, key-off 28h ← 0,1,2 (0,1); SSG mixer reg 7 ← BFh (if no SSG effect) else (reg7 & 3Fh) \| 9Bh |
| 2 | `0443` | clear the 11 part work areas (5Ch bytes each, 31A0…3537) keeping flag byte [3B] & 1Fh and the note counter [5A]; [55] = FFh. Globals: 3115/3114/3157 = 0, tie 30A1 = 0, status1 3111 = 0, status2 3112 = 0, measure 315B = 0, 3118 = 0, Timer-A counter 3119 = 0, key-on shadows 30A5–30AA = 0, 30C6/30C7 = 0, slot detunes 3148–314F = 0, 30B9/30BA = 0, reg-27 cache 30AD = 3Fh, measure length 3117 = 60h, FM register port pair 3096/3098 ← 30F2/30F4, volume-down 310B–310E ← defaults 3164–3167 (/DF /DS /DP /DR; DF = 16 by default), 315A ← 3168 |
| 3 | `0394` | parse header (below) |
| 4 | `037F` | fade volume 3115 = 0 |
| 5 | `04FA` | reg 29h ← 83h (written even on the YM2203), noise 311B = 0; **reg 6 ← 0 only if an SSG effect is active** (sic, `0505`); OPNA only: B4–B6 ← C0h, reg 22h ← 0, ADPCM reset |
| 6 | `2214` | TB 3113 = 3116 = C8h (200), t = 4396/56 → 315F/3160 = 78 (`1468`); write reg 26h if ≠ cache 3156 (`2C3B`; normally already C8h from the installer); reg 25h, 24h ← 0 (Timer A NA = 0); reg 27h ← 3Fh; 3118 = 315B = 0, 3117 = 60h |
| 7 | `038A` | 313C = 1 (playing); 3169++ |

The first music clock happens at the next Timer-B overflow. Because the chip only reloads the timer at
overflow, the first two clocks use TB = C8h (16.15 ms) and the song's own `T` value applies from the third
(pmdemu §11.1).

### 2.1 Header (`0394`) — music data D = CS:[30EC] = file offset 1

| D+ | field | PMD use |
|---|---|---|
| (file byte 0) | sound-source type | → 3110. **≠ 0 makes music_tick skip G H I K** (`05A5`); Xenon: 00 |
| 00…14h | 11 × u16 part offsets A B C D E F G H I J K | part address = D + offset; if the byte there is 80h the part is marked absent (address 0) |
| 16h | u16 R-table offset | 3104 = D + offset (K part pattern table) |
| 18h | u16 voice table offset | used only **if part A's offset ≠ 18h** (`039E`): 3102 = D + offset, 310F = 1 (embedded voices). Offset 18h ⇒ 310F = 0, voices from the /V buffer (30EE, 32-byte records, `1D4D`) |

Per-part defaults written by `0394` (everything else is 0 from step 2):

| part | work | [04] len | [3C] | [51]–[55] | volume [12] | other |
|---|---|---|---|---|---|---|
| A B C | 31A0 31FC 3258 | 1 | FFh | FFh | 6Ch (108) | [2F] = C0h, slot mask [38] = F0h, voice-write mask [39] = FFh |
| D E F | 32B4 3310 336C | 1 | FFh | FFh | 6Ch | [2F] = C0h, [3B] \|= 20h ("no slots": silent until an `s`/CF command gives it slots) |
| G H I | 33C8 3424 3480 | 1 | FFh | FFh | 08h | mixer mask [30] = 07h (tone), envelope mode [21] = 3 (no envelope yet) |
| J | 34DC | 1 | FFh | FFh | – | never processed |
| K | 3538 | 1 | FFh | FFh | 0Fh | rhythm pointer 3106 = 30A3 (a byte FFh = "pattern ended") |

Work-area pointer table `3188`: A…K, then 3594 (FM effect part). FM channel = global `[30A0]` (1–3), set by
music_tick before each FM/SSG part.

## 3. Part work area (5Ch bytes per part; 31A0 + 5Ch·n; FM effect part 3594)

| off | size | name | meaning |
|---|---|---|---|
| 00 | w | addr | current stream pointer (0 = part absent) |
| 02 | w | loop | `L` address (0 = none) (F6 `151D`) |
| 04 | b | len | clocks left of the current note/rest |
| 05 | b | gate | key-off when len ≤ gate (`076F`) |
| 06 | w | freq | FM: block<<11 \| F-number of the note (`1717`); SSG: tone period (`174E`); 0 = no pitch (rest) |
| 08 | w | detune | `D` (signed) |
| 0A | w | lfo1 | current LFO1 output (signed) |
| 0C | w | porta | accumulated portamento offset |
| 0E | w | pstep | portamento step per clock |
| 10 | w | prem | portamento remainder (spent ±1 per clock) |
| 12 | b | vol | `V` |
| 13 | b | trans | transpose, signed semitones |
| 14–17 | 4b | lfo1 work | delay counter, speed counter, step (signed), times counter |
| 18–1B | 4b | lfo1 params | delay, speed, step, times (`M`) |
| 1C | b | lfoflags | b0 LFO1→pitch, b1 LFO1→volume, b2 LFO1 no key-on reset; b3 portamento active; b4–b6 same for LFO2 |
| 1D | b | tmpvol | one-note volume + 1 (0 = none) (DE/DD) |
| 1E–20 | 3b | md1 | depth-mod delta, speed counter, speed (D6) |
| 21 | b | envmode | SSG: 0 attack/1 decay/2 release (PMD envelope), 3 = initial, FFh = extended envelope |
| 22 | b | xphase | extended envelope phase 0 none/1 AR/2 DR/3 SR/4 RR |
| 23–28 | 6b | env params | normal: AL counter, DD, SR counter, RR counter; ext: AR, DR, SR, RR, SL′ (=15−SL), AL |
| 29–2C | 4b | env reload/ctr | normal: AL, –, SR, RR reloads; ext: AR, DR, SR, RR step counters |
| 2D | b | envlevel | normal: signed offset added to V; ext: level 0–15 |
| 2E | b | extflags | b0 DX (SSG detune/LFO ∝ period), b1 MX (LFO1 at Timer-A rate), b2 EX (envelope at Timer-A rate); b4–b6 = LFO2 copies |
| 2F | b | pan | C0h; never used for output in this build |
| 30 | b | mixmask | SSG tone/noise enable mask (`P`) |
| 31 | b | voice | FM voice number |
| 32 | b | loopstat | 0 playing, 1 looped (L taken or `[ ]0` wrapped), 3 ended |
| 33 | b | carriers | carrier slot bits (reg-28h format, slot1 = b4 … slot4 = b7) |
| 34–37 | 4b | tl | voice TL of slots 1, 3, 2, 4 (register order 40h/44h/48h/4Ch) |
| 38 | b | slots | slots this part owns (reg-28h format; F0h = all) |
| 39 | b | vmask | voice-register write mask: slot1 = 88h, slot3 = 44h, slot2 = 22h, slot4 = 11h (FFh = all) |
| 3A | b | wave1 | LFO1 waveform |
| 3B | b | maskflags | 0 = audible. b0 INT 60h AH=1Eh mask, b1 channel taken by an effect, b5 FM3 part without slots, b6 `C0 01` mask, b7 masked during skip (pmd-driver.md) |
| 3C | b | keystate | 0 keyed on, 2 tie pending (no key-off), FFh keyed off |
| 3D | b | lfo1slots | LFO1 slot mask (`MM`; low nibble F = explicit, else = carriers) |
| 3E | b | q | gate clocks |
| 3F | b | Q | gate ratio (/256) |
| 40–41 | | – | not referenced |
| 42 | w | lfo2 | LFO2 output |
| 44–4B | 8b | lfo2 work+params | as 14–1B |
| 4C–4E | 3b | md2 | as 1E–20 |
| 4F | b | wave2 | |
| 50 | b | lfo2slots | |
| 51/52 | b | md1 count / reload | B7 (FFh = forever) |
| 53/54 | b | md2 count / reload | |
| 55 | b | note | last note byte (FFh = rest / none) |
| 56/57/58 | b | keydelay | B5: delay, counter, mask of slots keyed immediately |
| 59 | b | fbalg | B0h-register shadow (FB<<3 \| ALG) |
| 5A | b | notecount | incremented per note (kept across song starts) |
| 5B | | – | not referenced |

LFO2 uses the same code as LFO1: `lfo_swap 15C8` exchanges 0A↔42, 14–1B↔44–4B, 1E/1F↔4C/4D, 20/3A↔4E/4F,
51/52↔53/54 and rotates [1C] and [2E] by 4 bits around every LFO2 operation.

Global sequencer variables: 30A0 channel; 30A1 tie flag; 30A2 one-note-volume countdown; 30A5–30A7 key-on
slot bits of FM ch1–3; 30AB all-parts loop accumulator; 30AD reg-27h value (3Fh normal, 7Fh FM3 special
mode); 30AE saved 30AD while an effect plays; 30B7 copy of 3119 after each music clock; 30B9 "some slot
detune ≠ 0"; 30BA FM3 parts needing special mode (C=1, D=2, E=4, F=8); 30C6 channel-3 FB/ALG shared by
C–F; 30C7 "voice restore" flag; 30C9 per-clock change flags; 3102 voice table; 3104/3106 R table / pattern
pointer; 310B/310C FM/SSG volume-down; 3111 status1; 3112 status2; 3113 TB; 3115 fade volume; 3114 fade
speed; 3117 measure length; 3119 Timer-A counter; 311B noise period; 3148/314A/314C/314E slot detune of
slots 1–4; 315F/3160 tempo t.

## 4. Interrupts and the clock

`timer_dispatch 2B13` (pmd-driver.md §3.5) acknowledges with reg 27h ← [30AD] and calls, by status bits:
B only → `2B60`; A only → `2BA7`; both → `2B60` then `2BA7`.

* **Timer B `2B60`** (one music clock): run a deferred mstart/mstop; if playing (313C): `music_tick 05A0`,
  `tempo_reg_write 2C3B` (reg 26h ← TB if changed), `measure_count 2C26`, then **30B7 ← 3119**; then the
  optional user hook (AH=13h).
* **Timer A `2BA7`**: 3119++; every 8th: fade step `21CE` and hot-key test; SSG effect `0206` if active; FM
  effect `032B` if active; ESC test.

### 4.1 music_tick `05A0`
```
30AB = 3
if type byte (3110) == 0: ssg_part_tick(G,ch1) (H,ch2) (I,ch3); rhythm_part_tick(K)
fm_part_tick(A,ch1) (B,ch2) (C,ch3) (D,ch3) (E,ch3) (F,ch3)          ; J is never called
; every processed part (address ≠ 0) does 30AB &= [32]   (0765)
if 30AB != 0:                                   ; every present part has looped or ended
    for all 11 parts: if [32] != 3: [32] = 0
    if 30AB == 3: status2 = FFh                 ; all parts ended (no L anywhere)
    else: status2 += 1 (FFh -> 1)
```
So status2 counts "every part has passed its `L` (or a `[ ]0`) at least once since the last count", not
the loop of one particular part. Parts that end without `L` (e.g. Xenon's empty E/F parts that contain only
`CA 01 BB 01`) are ignored by the count because 3 & 1 = 1.

### 4.2 Tempo (`FC`, `140F`; `1468`, `1487`)
* `FC n` (n < FBh): TB = n (3113 = 3116 = n); t = round(4396 / (256 − TB)) (t = FFh if 256−TB < 18).
* `FC FF t`: t = max(t, 18); TB = 256 − round(4396 / t).
* `FC FE d`: TB += d (signed; clamped 0…FAh). `FC FD/FC/FB d`: t += d (clamped 18…255), TB from t.
* Real time: one clock = 1152·(256 − TB)/3 993 600 s. With Xenon's note lengths (whole note = 96 clocks,
  the default measure length 60h) a quarter note = 24 clocks, so quarter-note BPM = 8666.7/(256 − TB) ≈
  1.97·t (**inferred** interpretation of t; the formulas are verified). Xenon uses T = AAh (170: 24.81 ms,
  100.8 BPM), B4h, A0h, BEh, C1h, C8h and relative `FC FE FE`/`FC FE F6`.

### 4.3 Timer-A–rate LFO/envelopes ("extend" modes)
If [2E] bit1 (`MX`, cmd CA) / bit5 (`MX` for LFO2, BB) / bit2 (`EX`, C9) is set, the per-clock LFO or
envelope step is executed **N = (3119 − 30B7) mod 256 times** = the number of Timer-A interrupts since the
previous music clock (0, 1, 2, …; N = 0 → no step and "no change"). Delays (`M` delay, D6 speed counters,
envelope counters) are still counted in steps, but the LFO *delay* is counted in music clocks (§8.6.1).
Every Xenon part sets CA 01 and BB 01, every SSG part C9 01 (and CC 01): Xenon's vibrato and SSG envelopes
run at 54.17 steps/s independent of tempo. At T=170 (24.81 ms) N alternates 1/2 (mean 1.344).
In an IRQ that has both flags, the B work runs first, so that A interrupt counts for the *next* clock.

## 5. Part interpreter

### 5.1 FM part (`fm_part_tick 0643`; also the FM effect part)
```
if addr == 0: return
if [3B] != 0: goto masked (5.4)
len -= 1
if !([3C] & 3) and len <= gate:  keyoff(); [3C] = FFh                       ; 065E
if len != 0: goto sustain
[1C] &= ~8                                                                  ; portamento off
loop: b = *si++
   b == 80h:  addr = si-1; [32] = 3; [55] = FFh
              if loop: si = loop; [32] = 1; continue        else: goto sustain (len stays 0 -> next note read 256 clocks later, again 80h)
   b >= 81h:  command (table 0A8E; 81h..B4h = invalid: the byte is overwritten with 80h, si--, = part end)
   b < 80h:   note:
     note_prep(b)       ; 1FBF: not rest: porta = 0; if tie flag: LFO step only, else lfo_keyon (5.5)
                        ;       rest: LFO step only
     b = transpose(b); freq = fnum(b) (rest: [55]=FFh, freq=0 unless an LFO→pitch flag is set)
     len = *si++; gate = calc_gate()           ; 076F, see 6.3
     one-note volume bookkeeping (6.4)
     fm_volume_write(); fm_freq_write(); fm_keyon()       ; 19DD, 177E, 1BBE (in that order)
     [5A]++; addr = si; tie = 0; 30A2 = 0; [3C] = (*si == FBh) ? 2 : 0
     return
sustain (06EA):
   if keydelay counter [57]: [57]--; if it reaches 0 and !([3C]&1): fm_keyon()   (now all slots)
   c = 0 | ([1C] & 8)
   if [1C] & 3  and lfo_tick(LFO1) changed: c |= [1C] & 3
   if [1C] & 30h and lfo_tick(LFO2) changed: c |= [1C] & 30h
   if c & 19h: if c & 8: porta_step(); fm_freq_write()
   if c & 22h or fade speed 3114 != 0: fm_volume_write()
```
Commands executed by the parse loop take effect for the note that follows; `@` writes the voice at once;
`y`, `O` (B8), `FB` (B6) and the FM3 mode write registers at once; volume/detune/transpose changes only reach
the chip at the next note (or LFO/fade update).

### 5.2 SSG part (`ssg_part_tick 07F7`)
Same skeleton with table 0B24, plus:
* key-off (`1C5D`): envelope → release ([21] = 2, or [22] = 4 for the extended envelope); nothing is written.
* note prep (`1F34`): not rest and not tied: **envelope key-on** (§8.5) then lfo_keyon; period from `174E`.
* note: `ssg_volume_write 1B15`, `ssg_freq_write 192A`, `ssg_keyon 1BEA` (mixer reg 7: this channel's tone
  and noise bits = enabled per [30]; reg 6 ← noise period 311B if it changed and no SSG effect owns the noise,
  test [0314] bit7).
* sustain: as FM, but frequency write is `192A`; then **envelope tick** (`205E`) and the volume is rewritten
  if the envelope level changed, an LFO→volume changed, or a fade is running.

### 5.3 Rhythm part K (`0991`, **code**; Xenon has no K data)
Each clock: len−−; when 0 read the R pattern at 3106: FFh → read the next K byte (< 80h: pattern number n,
3106 = D + word[R-table + 2n]; ≥ B5h: command via table 0BBA; 80h: end/loop as above); in a pattern
`00–7Fh len` = rest, `80–BFh xx len` = drums (mask = (b0 & 3Fh)<<8 \| xx → [3136]; for each set bit, lowest
first, start SSG effect number = bit index via `0148` — without PPSDRV only the lowest bit plays; nothing
plays while the fade volume ≠ 0 or the part is masked), `C0h–FEh` = command (table 0BBA).

### 5.4 Masked parts (`0796` FM, `0911` SSG)
While [3B] ≠ 0 the part only counts: len−−; at 0, if bit1 (effect) is set and no FM effect is active, bit1
is cleared and, if [3B] is now 0, the clock continues as a normal note read (`066E`); otherwise commands are
parsed and executed (voice numbers, volumes, LFO settings are tracked; `@` only copies TL/FB-ALG), a note is
consumed with freq = 0, [55] = FFh and its length. `DA n1 n2 len` in a masked part consumes only n1 and then
reads `n2 len` as a note (same duration). Handlers that change the mask (C0, CF) jump back into the right
loop, so e.g. Xenon's part D (`[3B]=20h` at start) is unmasked by its first byte `CF C0` and plays its first
note in the same clock. SSG part I with bit1 (SSG effect) also unmasks when a non-rest note arrives and the
running SSG effect has priority < 2 (it is stopped, `095D`).

### 5.5 LFO key-on (`1FF5`)
[57] = [56] (start key-on delay); for LFO1 if [1C]&3: if [1C]&4 (no key-on sync) one lfo_tick, else
lfo_reset (`203A`: value = 0, work ← params, MD count ← reload; if [1C]&4: delay+1 in both work **and
params** — a quirk that increments the stored delay each time M/F1/C2 is executed in that mode); LFO2 the
same with bits 4–6.

### 5.6 Loops and end
* `F9 w`: clears the counter byte at D+w+1 (w = offset of the matching F8's count byte).
* `F8 cnt ctr w`: cnt = 0 → infinite: [32] = 1 (counts as "looped" for status2), jump D+w+2. Else ctr++
  (in the data!), if ctr == cnt continue after the F8 else jump to D+w+2 (first byte after the F9).
* `F7 w`: if ctr at D+w+1 == cnt−1 jump to D+w+4 (past the F8) — the `:` exit of the last pass.
* For the FM effect part all these offsets are relative to the EFC buffer start (30F0).
* `80h` with `L` set: jump to L, [32] = 1; without: the part stays on the 80h ([32] = 3), its channel just
  keeps its last state (released by the gate).

## 6. Timing of a note

### 6.1 Length
Every note/rest is `byte len` (1–255 clocks; 0 = 256). `&` (FB) between notes ties them.

### 6.2 Key-on / key-off writes
* FM key-on `1BBE` (skipped for rests): 30A5[ch] \|= [38] (\& [58] while a key-on delay runs); write
  28h ← (ch−1) \| 30A5[ch]. Writing an already-set slot does not retrigger the operator, which is how ties work.
* FM key-off `1C38`: 30A5[ch] &= ~[38]; 28h ← (ch−1) \| 30A5[ch] (skipped if [55] = FFh).
  Per-channel shadows let parts C and D key their own slots of channel 3 independently.
* SSG: no key bit; key-on = mixer + envelope restart, key-off = envelope release.

### 6.3 Gate (`076F`, `065E`)
At note read: if the byte after the length is C1h it is skipped and gate = 0; else if Q ≠ 0:
gate = ⌊len·Q/256⌋ + q (8-bit), else gate = q. On each later clock, after len−−, the part is keyed off if
it is not tied ([3C] & 3 = 0) and len ≤ gate. Hence key-on time = max(1, len − gate) clocks; with q = 0 the
key-off comes in the clock where len reaches 0, immediately before the next note's key-on (the chip sees
off→on: re-attack). If the note is followed by `&` ([3C] = 2) no key-off happens at all, and the next note
(with the tie flag 30A1) is neither re-keyed nor does it restart LFOs or the SSG envelope; its frequency is
written (legato/slur).

### 6.4 One-note volume (`DE`/`DD`, `06A8`)
DE/DD set [1D] = clamped V±n + 1 and 30A2 = 1. At the next non-rest note 30A2 is decremented to 0 and [1D]
is used; at the note after that 30A2 0→FFh ≠ 0 clears [1D]. A rest in between clears 30A2 (`06D6`) so the
following note already loses [1D] (**code** detail).

## 7. Command reference

Handler addresses per dispatch table (FM / SSG / rhythm K). `[xx]` = part work field (§3).

| byte | MML | operands | FM handler | SSG handler | K handler | semantics | Xenon .M (files) / EFC |
|---|---|---|---|---|---|---|---|
| FF | @ | n | 13B6 | skip1 | skip1 | FM: voice n -> [31], load voice (§8.7) unless masked. SSG/K: ignored | 1030 (31) / 72 |
| FE | q | n | 1400 | 1400 | skip1 | gate [3E]=n: key-off when remaining length <= gate (§6.3). K: ignored | 526 (30) / 5 |
| FD | V | n | 140A | 140A | 140A | volume [12]=n (FM 0-127, SSG 0-15, K: PPS volume) | 343 (31) / 83 |
| FC | T / t | n · FF t · FE d · FD..FB d | 140F | 140F | 140F | n<FBh: Timer-B value TB=n; FF t: tempo t (TB=256-4396/t); FE d: TB+=d (0..FAh); FD/FC/FB d: t+=d (18..255) (§4.2) | 51 (31) / — |
| FB | & | – | 14AA | 14AA | 14AA | tie/slur: next note of this part is not re-keyed, no LFO/envelope restart; this note gets no key-off ([3C]=2) | 1507 (27) / 17 |
| FA | D | w | 14B0 | 14B0 | 14B0 | detune [08]=w (signed; FM: +fnum units, SSG: -period units or scaled with DX) | 161 (30) / — |
| F9 | [ | w | 14BA | 14BA | 14BA | loop start: clear loop counter at data+w+1 (w = offset of the matching F8 count byte) | 333 (23) / 24 |
| F8 | ] | cnt,ctr,w | 14D0 | 14D0 | 14D0 | loop end: ctr+=1; if ctr==cnt fall through else jump to data+w+2; cnt=0: infinite, sets loop status [32]=1 | 333 (23) / 24 |
| F7 | : | w | 14FC | 14FC | 14FC | loop break: if counter at data+w+1 == count-1, jump past the F8 (data+w+4) | 61 (12) / — |
| F6 | L | – | 151D | 151D | 151D | loop point [02]=address of the next byte | 141 (22) / — |
| F5 | _ | n | 1521 | 1521 | skip1 | transpose [13]=n (signed semitones, applied to every note) | 1005 (21) / 5 |
| F4 | ) | – | 152E | 1543 | 1543 | FM: V+=4 (max 127); SSG: V+=1 (max 15); K: V+=1 | 955 (18) / 74 |
| F3 | ( | – | 1556 | 156F | 156F | FM: V-=4 (min 0); SSG/K: V-=1 | 1099 (21) / 81 |
| F2 | M | delay,speed,step,times | 161F | 161F | skip4 | LFO1 parameters [18..1B]=[14..17]; LFO1 reset | 233 (31) / 11 |
| F1 | * | n | 165B | 1648 | 0F06 | LFO1 switch: [1C] bits0-2 = n (b0 pitch, b1 volume, b2 no key-on sync; n&F8h≠0 -> 1); LFO1 reset; FM3: mode update. K: PPSDRV INT 64h AH=5 | 1326 (31) / 12 |
| F0 | E | al,dd,sr,rr | skip4 | 1661 | skip4 | SSG: PMD software envelope (§8.5.1). FM/K: ignored | — / — |
| EF | y | reg,val | 1689 | 1689 | 1689 | write OPN register reg=val immediately (any part) | 11 (2) / — |
| EE | w | n | skip1 | 1691 | skip1 | SSG: noise period [311B]=n (written at the next SSG key-on). FM/K: ignored | 72 (8) / — |
| ED | P | n | skip1 | 16AB | skip1 | SSG: tone/noise mask [30] (07h tone, 38h noise, 3Fh both); applied at key-on | 17 (8) / — |
| EC | p | n | 16B0 | skip1 | skip1 | pan — ignored in this build | — / — |
| EB |  | n | 16B5 | 16B5 | 16B5 | (rhythm-source command) ignored | — / — |
| EA |  | n | 16B7 | 16B7 | 16B7 | (rhythm-source command) ignored | — / — |
| E9 |  | n | 16BC | 16BC | 16BC | (rhythm-source command) ignored | — / — |
| E8 |  | n | 16BE | 16BE | 16BE | (rhythm-source command) ignored | — / — |
| E7 | __ | n | 1526 | 1526 | skip1 | transpose [13]+=n | 112 (10) / — |
| E6 |  | n | 16C0 | 16C0 | 16C0 | (rhythm master volume) ignored | — / — |
| E5 |  | a,b | 16B9 | 16B9 | 16B9 | (rhythm volume) ignored | — / — |
| E4 |  | n | 13AF | skip1 | skip1 | (hardware LFO) FM: ignored | — / — |
| E3 | )n | n | 153D | 1550 | 1550 | V+=n (FM max 127, SSG max 15) | 228 (4) / — |
| E2 | (n | n | 1561 | 157A | 157A | V-=n (min 0) | 228 (4) / — |
| E1 |  | n | 13AB | skip1 | skip1 | (hardware LFO) FM: ignored | — / — |
| E0 |  | n | 13AD | skip1 | skip1 | (hardware LFO) FM: ignored | — / — |
| DF | Z | n | 13B1 | 13B1 | 13B1 | measure length [3117]=n clocks (measure counter for INT 60h; no audible effect) | — / — |
| DE | )^n | n | 136E | 1383 | 1383 | one-note volume: [1D]=min(V+n, 127 FM / 15 SSG)+1 for the next non-rest note | — / — |
| DD | (^n | n | 139D | 139D | 139D | one-note volume: [1D]=max(V-n,0)+1 | — / 16 |
| DC | ~ | n | 1360 | 1360 | 1360 | status1 [3111]=n (INT 60h AH=0Ah returns it in AH) | — / — |
| DB | ~+ | n | 1365 | 1365 | 1365 | status1 += n | 2 (2) / — |
| DA | { } | n1,n2,len | 12A6 | 1318 | skip1 | portamento from n1 to n2 over len clocks (§8.8) | 107 (16) / 13 |
| D9 |  | n | skip1 | skip1 | skip1 | ignored | — / — |
| D8 |  | n | skip1 | skip1 | skip1 | ignored | — / — |
| D7 |  | n | skip1 | skip1 | skip1 | ignored | — / — |
| D6 | MD | speed,delta | 1261 | 1261 | skip2 | LFO1 depth modulation: every speed LFO events step+=delta (§8.6 item 4) | — / — |
| D5 | DD | w | 14B5 | 14B5 | 14B5 | detune [08]+=w | — / — |
| D4 | n | n | 1210 | 1210 | 1210 | SSG effect n on (n=0: off) unless the part is masked | — / 9 |
| D3 | N | n | 122B | 122B | 122B | FM effect n on (n=0: off) unless the part is masked | — / — |
| D2 | F | n | 1258 | 1258 | 1258 | start fade-out with speed n ([3114]=n, [3157]=1) | — / — |
| D1 |  | n | skip1 | skip1 | skip1 | ignored | — / — |
| D0 | w± | n | skip1 | 1696 | skip1 | SSG: noise period += n (clamped 0..1Fh). FM/K: ignored | — / — |
| CF | s | n | 1145 | skip1 | skip1 | FM: slot mask [38]=n&F0h; n&0Fh≠0: carrier mask [33]=(n&0Fh)<<4 else from ALG; FM3: mode update. SSG/K: ignored | 46 (25) / — |
| CE |  | 6 bytes | skip6 | skip6 | skip6 | (PCM) ignored | — / — |
| CD | E (6-param) | ar,dr,sr,sl<<4\|rr,al | skip5 | 0FD8 | skip5 | SSG: extended envelope AR DR SR RR SL AL (§8.5.2); FM: ignored | 271 (27) / — |
| CC | DX | n | skip1 | 0FAC | skip1 | SSG: [2E] bit0 = n&1: detune and pitch-LFO scaled by the period (§8.4); FM: ignored | 93 (31) / — |
| CB | MW | n | 0FD3 | 0FD3 | skip1 | LFO1 waveform [3A]=n (0 tri,1 saw,2 square,3 random,4 one-sided tri,5 tri·\|step\|,6 one-shot) | 4 (4) / 8 |
| CA | MX | n | 0FB7 | 0FB7 | skip1 | [2E] bit1 = n&1: LFO1 steps once per Timer-A interrupt instead of per clock | 279 (31) / — |
| C9 | EX | n | skip1 | 0FC4 | skip1 | SSG: [2E] bit2 = n&1: envelope steps once per Timer-A interrupt | 93 (31) / — |
| C8 | sd | mask,w | 104C | skip3 | skip3 | FM3 only: slot detune of slots in mask (b0..b3 = slot1..4) = w; mode update. Others: skipped | 47 (25) / — |
| C7 | sdd | mask,w | 101C | skip3 | skip3 | FM3 only: slot detune += w | — / — |
| C6 |  | w,w,w | 0F64 | skip6 | skip6 | FM: set FM3-extended parts D,E,F start addresses (0 = unchanged). SSG/K: ignored | — / — |
| C5 | MM | n | 0F1A | skip1 | skip1 | FM: LFO1 slot mask [3D]=(n<<4)\|0Fh (n=0: carrier mask); FM3: mode update | 1 (1) / — |
| C4 | Q | n | 1405 | 1405 | skip1 | gate ratio [3F]=n: gate = len*n/256 + q | — / — |
| C3 |  | a,b | 16B2 | skip2 | skip2 | (pan extended) ignored | — / — |
| C2 | MA | n | 163E | 163E | skip1 | LFO1 delay [18]=[14]=n; LFO1 reset | — / — |
| C1 | && | – | nop | nop | nop | only meaningful right after a note: that note gets gate 0 (q/Q ignored); elsewhere a no-op | 2 (1) / — |
| C0 | m / DF etc. | 0 or 1 · F7..FF n | 0E32 | 0E5C | 0E8C | C0 00/01: part mask off/on. C0 FF/FE n: FM volume-down =n / +=n; FD/FC: SSG; FB/FA: PCM; F9/F8: rhythm; F7: [315A]=n&1. C0 02..F6: invalid = part end | — / — |
| BF | M (LFO2) | delay,speed,step,times | 1588 | 1588 | skip4 | LFO2 parameters; LFO2 reset | — / — |
| BE | * (LFO2) | n | 15C2 | 15AD | skip1 | LFO2 switch: [1C] bits4-6 = n&7; then resets LFO1 (sic); FM3: mode update | — / — |
| BD | MD (LFO2) | speed,delta | 1599 | 1599 | skip2 | LFO2 depth modulation | — / — |
| BC | MW (LFO2) | n | 159E | 159E | skip1 | LFO2 waveform [4F] | — / — |
| BB | MX (LFO2) | n | 15A3 | 15A3 | skip1 | [2E] bit5: LFO2 at Timer-A rate | 279 (31) / — |
| BA | MM (LFO2) | n | 0F36 | skip1 | skip1 | FM: LFO2 slot mask [50] | — / — |
| B9 | MA (LFO2) | n | 15A8 | 15A8 | skip1 | LFO2 delay; LFO2 reset | — / — |
| B8 | O | mask,n | 0D69 | skip2 | skip2 | FM: operator TL: mask b0..b3 = slot1..4 (∩ slot mask); b7=0: TL=n&7Fh, b7=1: TL+=n (0..7Fh); written at once | 52 (1) / — |
| B7 | MDC | n | 126D | 126D | skip1 | depth-modulation count: n<80h LFO1 [51]=[52]=n, else LFO2 [53]=[54]=n&7Fh (0 -> FFh = forever) | — / — |
| B6 | FB | n | 0CF2 | skip1 | skip1 | FM feedback: n<80h FB=n; n>=80h relative (b6 set: signed, else +n&7), clamp 0..7; writes B0h | — / — |
| B5 | sk | mask,n | 0CDA | skip2 | skip2 | FM: key-on delay: slots in mask&0Fh are keyed on n clocks after the others | — / — |

### 7.1 Notes on the table
* Dispatch: `0A77`: for b ≥ B5h `jmp cs:[table + 2·(FFh − b)]`; tables FM `0A8E`, SSG `0B24`, rhythm `0BBA`
  (75 words each; the FM effect part uses the FM table). `C0 n` with n ≥ F7h is dispatched again via the
  9-word table `0C6D` (`0C57`). `skipN` = generic handlers `0C50…0C56` (`inc si` ×N): the command is
  accepted and ignored. Ignored bytes are those an OPNA/PCM build would use (pan, rhythm source, hardware LFO,
  PCM) — in this OPN build they have no effect.
* Operand counts = `re-tools/pmdinfo.py OPLEN`; re-derived from every handler — all 75 agree (FC: 1 operand
  if < FBh else 2; C0: 1 if < 2, 2 if ≥ F7h, otherwise the byte is an invalid command = part end).
* "MML" spellings are the usual PMD ones and partly **inferred**; the semantics column is from the code.
* Commands that touch "FM3 mode update" call `fm3_special_mode_update 1095` (§8.2) when executed by a
  channel-3 music part (not the effect part).
* Counts: occurrences in the 31 songs' parts A–I/K (part J excluded: it holds `CA 01 BB 01 C9 01` in every
  file but is never executed), number of files, and occurrences in the 50 effect streams.

## 8. Sound math

### 8.1 FM pitch (`1717`, `177E`, `18E7`)
Note byte = octave<<4 | n (n = 0…11 C…B, 0Fh = rest). Transpose (`16C2`): n += [13] (signed), carried into
the octave in steps of 12. freq = (octave & 7)<<11 \| FNUM[n] — the octave is the OPN block directly.

| n | FNUM (`2CA9`) | Hz at block 4 | SSG P0 (`2CC1`) | TP octave 4 | Hz |
|---|---|---|---|---|---|
| C | 26Ah (618) | 261.52 | EE8h (3816) | 239 | 261.09 |
| C# | 28Fh (655) | 277.18 | E12h (3602) | 225 | 277.33 |
| D | 2B6h (694) | 293.68 | D48h (3400) | 213 | 292.96 |
| D# | 2DFh (735) | 311.04 | C89h (3209) | 201 | 310.45 |
| E | 30Bh (779) | 329.65 | BD5h (3029) | 189 | 330.16 |
| F | 339h (825) | 349.12 | B2Bh (2859) | 179 | 348.60 |
| F# | 36Ah (874) | 369.86 | A8Ah (2698) | 169 | 369.23 |
| G | 39Eh (926) | 391.86 | 9F3h (2547) | 159 | 392.45 |
| G# | 3D5h (981) | 415.14 | 964h (2404) | 150 | 416.00 |
| A | 410h (1040) | 440.10 | 8DDh (2269) | 142 | 439.44 |
| A# | 44Eh (1102) | 466.34 | 85Eh (2142) | 134 | 465.67 |
| B | 48Fh (1167) | 493.85 | 7E6h (2022) | 126 | 495.24 |

Hz (datasheet, **inferred** clock 3.9936 MHz, default prescaler — PMD writes no 2Dh–2Fh): FM
f = FNUM · 2^block · 3 993 600 / (144 · 2^20); SSG f = 3 993 600 / (64 · TP). Octave nibble 4 = A 440 Hz for
both FM and SSG.

Frequency write (`177E`; nothing if freq = 0 or [38] = 0):
```
x = (freq & 7FFh) + porta[0C] + detune[08]          (16-bit)
if channel-3 music part and 30AD != 3Fh: per-slot write (8.2), return
if [1C]&01: x += lfo1[0A];  if [1C]&10h: x += lfo2[42]
normalize(x, block):                                ; 18E7, fnum space with 26Ah per octave
   while x >= 4D4h:  if block == 7: x = min(x, 7FFh); break   else block++, x -= 26Ah
   while x < 26Ah (incl. negative): if block == 0: x = max(x, 8) (negative -> 8); break   else block--, x += 26Ah
write A4h+ch-1 <- block<<3 | x>>8 ;  A0h+ch-1 <- x & FFh
```
So detune and LFO are in F-number units of the note's octave (1 unit ≈ 1.6–2.8 cents), carried across
octave boundaries.

### 8.2 FM3 extended parts and special mode (`1145`, `104C`, `1095`, `17DC`)
* A part's slots are [38] (`s` / CF n: high nibble; slot1 = 10h, slot2 = 20h, slot3 = 40h, slot4 = 80h).
  D/E/F start with none ([3B] bit5); CF with a non-zero mask clears bit5. [39] (voice write mask) is derived:
  slot4 → 11h, slot3 → 44h, slot2 → 22h, slot1 → 88h. The carrier mask [33] is the low nibble of CF if
  non-zero, else the carriers of the current ALG (channel 3: from 30C6).
* Slot detunes (C8/C7, only channel-3 music parts): 3148/314A/314C/314E for slots 1–4; 30B9 = 1 if any ≠ 0.
* Special-mode decision `1095` for the calling part (bit C=1, D=2, E=4, F=8 in 30BA): part needs special
  mode if it has some but not all slots, or all slots plus an explicit LFO1 slot mask (C5) with LFO1→pitch
  (or, failing that, an explicit LFO2 mask with LFO2→pitch). 30AD = 7Fh if 30BA ≠ 0 or 30B9 = 1, else 3Fh. If
  the part is not effect-masked and the value changed: write 27h ← 30AD & CFh (4Fh / 0Fh: timers keep
  running, flags not reset) and, when switching to special mode, rewrite the frequency of the unmasked FM3
  parts processed before it (C, D, E). While an effect owns the channel the value only goes to 30AE.
  The IRQ acknowledge writes 27h ← 30AD (3Fh/7Fh) every interrupt, so the mode bit is re-sent constantly.
* Per-slot frequency write `17DC` (special mode): for each owned slot, in the order slot4, slot3, slot2, slot1:
  x_s = x + slotdetune_s (+ LFO1 if its slot-mask bit and [1C]&1, + LFO2 likewise), normalize, write
  slot4 → A6h/A2h, slot3 → ACh/A8h, slot2 → AEh/AAh, slot1 → ADh/A9h (high byte first).
  *Quirk (code):* the LFO slot-mask bytes are rotated only for owned slots, so slot s is tested against the
  mask bit of its *rank* among the owned slots (first owned slot ↔ bit7 …). Irrelevant unless an explicit
  C5/BA mask is used on an FM3 part (not in Xenon); the default mask F0h (used whenever the low nibble is 0)
  means "all".
* FB/ALG on channel 3 (`1C95`): a part owning slot 1 writes the voice's FB/ALG and stores it in 30C6; any
  other FM3 part writes (30C6 & 38h) \| own ALG (so FB stays with slot 1's owner). Xenon: C's voice sets
  FB, D's voice only the algorithm (both use ALG 4, i.e. two independent 2-op pairs 1→2 and 3→4).
* Key-on writes per part only its own slots (shared shadow 30A7).

### 8.3 FM volume → TL (`fm_volume_write 19DD`)
```
if [38] == 0: return
v = [1D] ? [1D]-1 : V
if not effect part:
    if FMvoldown (310B, default 16): v = v*(256-310B) >> 8
    if fade 3115 >= 2:               v = v*(256 - (fade>>1)) >> 8
att[slot] = 80h for all; att[s] = 255 - v for s in ([38] & carriers[33]);  W = [38] & [33]
if v != 0:
    if [1C]&02: for s in [3D] & [38]: att[s] = clamp(att[s] - lfo1, 0, 255) (signed lfo1); W |= those
    if [1C]&20h: same with LFO2 / [50]
for s in slot4, slot3, slot2, slot1 (regs 4Ch, 44h, 48h, 40h + ch-1) if s in W:
    TL = min(att[s] + tlvoice[s], 255) - 128, at least 0           -> = clamp(TLvoice + 127 - v, 0, 127)
```
Carriers: ALG 0–3 slot 4; ALG 4 slots 2,4; ALG 5,6 slots 2,3,4; ALG 7 all (table `2CD9`: 80 80 80 80 A0 E0
E0 F0). Modulators keep the voice TL (written at voice load), unless a volume LFO with an explicit slot mask
includes them. Examples with the default DF16: V = 108 → v = 101 → TL = TLvoice + 26; V = 127 → v = 119 →
+8. A positive LFO value lowers TL (louder).

### 8.4 SSG pitch (`174E`, `192A`)
Period TP = P0[n] >> octave, plus 1 if the last bit shifted out was 1 (round half up). Write (`192A`; none if
freq = 0):
```
x = TP + porta
normal: x -= detune; if [1C]&1: x -= lfo1; if [1C]&10h: x -= lfo2
DX ([2E]&1, cmd CC 1):  if detune: x -= ((x*detune) >> 12) + (+1 if >= 0 else -1)      (signed, floor)
                        d = (lfo1 if [1C]&1) + (lfo2 if [1C]&10h); if d: x -= ((x*d) >> 12) ± 1 likewise
x = clamp(x, 0, FFFh) (negative -> 0);  reg 2(ch-1) <- x & FFh;  reg 2(ch-1)+1 <- x >> 8
```
So positive detune/LFO raises the pitch on both FM and SSG; with DX (all Xenon SSG parts) the SSG offset is
proportional to the period (≈ detune/4096 of the frequency, i.e. pitch-uniform like FM).

### 8.5 SSG volume and envelopes (`1B15`, `1F34`, `205E`)
Volume write (`1B15`): skipped while [21] = 3 (no note yet) or ([21] = FFh and [22] = 0).
```
v = [1D] ? [1D]-1 : V;  if SSGvoldown 310C: v = v*(256-310C)>>8;  if fade: v = v*(256-fade)>>8
if v: normal envelope:   out = clamp(v + (signed)[2D], 0, 15)
      extended envelope: out = 0 if level == 0 else (v*(level+1) + 8) >> 4
      if out and [1C]&22h: out = clamp(out + lfo1(if b1) + lfo2(if b5), 0, 15)
reg 8+ch-1 <- out         (v == 0 -> 0)
```
**8.5.1 PMD envelope `E al,dd,sr,rr` (F0)** — [2D] is an offset to V. Key-on: phase 0, offset 0, AL counter =
al (al = 0: directly phase 1 with offset dd). Step (`2091`): phase 0: counter−−, at 0 → phase 1, offset = dd.
Phase 1: if sr ≠ 0, every sr steps offset −1. Phase 2 (after key-off): rr = 0 → offset = −15 at once, else
every rr steps offset −1. The offset is kept in −15…+14 (F1h…0Eh). `F0` received while the extended envelope
was active switches to phase 2 with offset −15.
**8.5.2 Extended envelope (CD ar,dr,sr,b4,al; b4 = sl<<4 \| rr)** — [2D] is a level 0–15 that scales V.
CD stores AR, DR, SR (&1Fh), RR (&0Fh), SL′ = 15 − sl, AL (&0Fh); first CD: [21] = FFh, phase 4, level 0.
Key-on: kA = AR−16, kD = DR−16 (doubled if < 0), kS = SR−16 (doubled if < 0), kR = 2·RR−16, level = AL,
phase 1, and **one step is executed immediately**. Step rule for the current phase with counter c (initial
c = k): if c ≥ 1, the level changes by c and c ← k; if c ≤ 0, c += 1 (no change); rate parameter 0 = hold.
Net rates: AR ≥ 17: +(AR−16)/step, AR 1–16: +1 every 18−AR steps; DR, SR ≥ 17: −(r−16)/step, 1–16: −1 every
34−2r steps; RR ≥ 9: −(2RR−16)/step, RR 1–8: −1 every 18−2RR steps. Transitions: attack reaching ≥ 15 → 15,
phase 2 (phase 3 if SL′ = 15); decay going below SL′ → SL′, phase 3; sustain and release floor at 0; key-off
→ phase 4. Xenon's commonest `CD 1F 19 01 3A 00`: instant attack to 15, decay −9 → clamped to SL′ 12, then
−1 every 32 steps; release −4/step.
Envelope tick per clock (`205E`): one step, or N steps in EX mode (§4.3); volume rewritten if the level
changed.

### 8.6 Software LFO (`1DAE`, `1DE5`, `1ECA`)
Parameters (`M delay,speed,step,times` = F2, LFO2 BF): work = params at every reset.
1. **Delay**: if the delay counter ≠ 0 it is decremented once per music clock and nothing else happens.
2. **Speed**: then one step (or N in MX mode): if the speed counter ≠ 1 it is decremented, else reloaded
   with `speed` and the waveform advances (speed 0 behaves like 256).
3. **Waveforms** ([3A] = `MW`, CB; value v, step s, times counter t, times param T):

| MW | name | per advance |
|---|---|---|
| 0 | triangle | v += s; if v == 0: MD event; if t ≠ FFh: t−−, at 0: t = 2T, s = −s (first half-period T, then 2T: symmetric around 0) |
| 1 | saw | v += s; if t ≠ FFh: t−−, at 0: v = −v, MD event, t = 2T |
| 2 | square | every clock/step v = s·t (signed product, t = times counter); on each speed reload: MD event, s = −s |
| 3 (and ≥ 7) | random | A = \|s\|·t; v = rnd(2A) − A, MD event; rnd: seed = (seed·259 + 3) & 7FFFh (seed `1F32`, initially 0, global), rnd(x) = x·seed/7FFFh |
| 4 | one-sided triangle | as 0 but t reloads to T (oscillates 0…T·s) |
| 5 | triangle, squared step | as 0 with increment s·\|s\| |
| 6 | one-shot ramp | while t ≠ 0: v += s, t−− (t = FFh: forever) |

4. **Depth modulation (MD)** (`D6 speed,delta` / BD; count B7): at each MD event the MD counter is
   decremented; at 0 it reloads with `speed` and, if the MD count ≠ 0 (count < 80h is decremented, FFh =
   forever), \|s\| += delta keeping the sign of s (clamped to 0…7Fh). Default speed counter 0 → effectively
   every 256 events, delta 0 → no effect.
5. **Key-on**: reset (value 0, delay restarts) unless the "no sync" bit ([1C] b2/b6) is set; ties don't reset.
6. **Application** (flags [1C], `*`/F1 and BE): b0/b4 pitch → added to the F-number (FM) or subtracted from
   the period (SSG; scaled with DX); b1/b5 volume → FM: subtracted from the carrier attenuation of the slots in
   [3D]/[50]; SSG: added to the 0–15 output. Frequency/volume are rewritten only on clocks where the value
   changed.
Xenon: LFO parameters on most parts (`M 18 01 0C 02` = delay 24 clocks, speed 1, step 12, times 2; `*` mostly
0 — set up but off — or 1 = vibrato), all with MX: a ±24 F-number triangle advancing 54 steps/s after a
24-clock delay.

### 8.7 FM voice record and loading (`1C74`, `1D40`, `1D7B`)
Lookup (`1D40`): embedded table (3102, or 30AF for the effect part): records of 26 bytes, linear search for the
first record whose byte 0 = voice number (**no end check** — a missing voice matches whatever byte equals the
number further on). Without an embedded table: 30EE + 32·n (records without the number byte).

| rec+ | bytes | registers (+ch−1) | content |
|---|---|---|---|
| 0 | 1 | – | voice number |
| 1 | 4 | 30h 34h 38h 3Ch | DT/MULTI, slots 1, 3, 2, 4 |
| 5 | 4 | 40h 44h 48h 4Ch | TL |
| 9 | 4 | 50h… | KS/AR |
| 13 | 4 | 60h… | (AM)/DR |
| 17 | 4 | 70h… | SR |
| 21 | 4 | 80h… | SL/RR |
| 25 | 1 | B0h | FB<<3 \| ALG |

Load sequence for a non-masked part with [39] ≠ 0 (all for the owned slots, i.e. [39]):
1. mute: per slot TL ← 7Fh and SL/RR ← 7Fh (40h/80h, 44h/84h, …), then key-off (28h, own slots);
2. B0h+ch−1 ← FB/ALG (channel 3: shared-FB rule §8.2) → [59]; carriers [33] from the ALG; [3D]/[50] =
   carriers unless explicitly set;
3. DT/ML for owned slots; TL **only for owned non-carrier slots** (table `2CE1`: EE EE EE EE CC 88 88 00);
   KS/AR, DR, SR, SL/RR (50h…8Ch) for owned slots;
4. [34..37] ← the 4 voice TLs. The carrier TLs reach the chip with the next volume write (next note).
A masked part only records the voice number, TLs and FB/ALG; on unmask (`C0 00`, end of an effect `0EA2`)
the voice is reloaded with the remembered FB/ALG and the TLs (incl. B8 changes) are restored.

### 8.8 Portamento (`DA n1,n2,len`, `12A6`/`1318`)
Prepares n1 like a note (porta reset, LFO key-on), computes freq(n1) and freq(n2) (both transposed):
FM Δ = (block2 − block1)·26Ah + (fnum2 − fnum1); SSG Δ = TP2 − TP1. len → [04], gate as for notes,
pstep = Δ / len (truncated toward 0), prem = remainder (same sign), [1C] \|= 8, then continues as a note
(n1 keyed on). Each sustain clock: porta += pstep, plus ±1 while prem ≠ 0 (prem moves toward 0). The note
ends (len−1)·Δ/len short of n2; the next note resets porta. Used in 16 Xenon songs and effects.

### 8.9 Fade-out application
The fade value 3115 (0 = full … FFh) is stepped by Timer A (pmd-driver.md §5.1). FM: v·(256 − fade/2)/256
(at FFh about half the V value, e.g. V 108 → v 101 → 50, i.e. carrier TL +77 instead of +26), SSG: v·(256 − fade)/256
(FFh → 0). While the fade speed 3114 ≠ 0 every part rewrites its volume each clock. **A volume set with
INT 60h AH=19h (AGS `music_set_fadevol`, scripts' 64/127/0 "muffle") changes only 3115: each part picks it
up at its next note or LFO-volume change** — the attenuation creeps in note by note (verified with
`pmdmodel --check --event 300:1940`).

### 8.10 Slot key-on delay (`B5 mask,n`, `0CDA`)
[58] = ((mask & 0Fh) xor 0Fh) << 4 (slots keyed at once), [56] = n. At each note: counter = n; key-on writes
only the [58] slots; after n clocks the full slot mask is keyed (unless the note was already keyed off).

## 9. FM effect streams (XEN_SE.EFC)

Effects use the FM table and the same `fm_part_tick` on work 3594 with channel 3, one clock per Timer-A
interrupt (start/stop in pmd-driver.md §7.1). Differences, all keyed on `di == 3594`: no volume-down or fade
scaling (`19F5`); never FM3 special mode or per-slot frequencies (the start sets 30AD = 3Fh); FB/ALG written
directly; `MM`/`s`/`*` don't call the mode update; voices from the EFC voice table (30AF); `[ ] :` offsets
relative to the EFC start (30F0); the work area is zero-filled (so [55] = 0, [3C] = 0 — the first clock writes a
key-off); the effect ends at the first clock where the stream is at 80h with len = 0 (`034B`), even with `L`.
Commands used by the 50 effects: @ q V & [ ] _ ) ( M * DD (one-note volume down) DA (portamento) D4 (start a
built-in SSG effect — 9 effects) CB (LFO waveform); all verified (§11.2).

## 10. What Xenon uses

* Parts: A, B, C (C as FM3 slots 1+2 in 24 songs: `CF 30 C8 03 0000`), D (FM3 slots 3+4: `CF C0` and slot
  detune `C8 0C 0005` = +5 on slots 3/4 in most songs, chorus against C), G H I. E/F contain only
  `CA 01 BB 01` (never sound); J contains `CA/BB/C9 01` (never executed); no K. XENON_15/23/27/28 (part C)
  and XENON_29 (part B) use `CF F0` + `C8 0F 0000` instead: all four slots, zero slot detune → FM3 stays in
  normal mode (on part B both are no-ops: CF only re-sets the full mask, C8 is ignored off channel 3).
* Every part starts with `CA 01 BB 01` (LFO1/2 at Timer-A rate), SSG parts additionally `CC 01 C9 01`
  (DX, EX). SSG envelopes: only the extended form CD (271×); no F0.
* Tempo `FC n` (T, in all songs), relative `FC FE`; no `t`.
* Volume: V, ( ), `(n` `)n` (E2/E3, 4 songs), DD in effects; q (FE) everywhere, no Q (C4); C1 twice
  (XENON_21 G).
* Pitch: transpose `_` (+1 in many songs) and `__`; detune D; portamento DA (16 songs).
* LFO: M, *, MW (CB) in 4 songs (XENON_07 random, XEN_03A/B square, XENON_24 triangle), MM (C5) once (XENON_24 A).
* Direct register writes `y` (EF) in XENON_01 (D1R/RR tweaks of ch1/ch3 slots: `y 58 04`, `y 5A 01`, `y 8A 07`…)
  and XENON_10; operator TL sweeps `O` (B8) in XENON_02 part A (52×).
* Status marks `~+1` (DB) in XENON_01/02 only (sound.md §3.1).
* SSG noise: ED (mixer mask) and EE (noise period) in 8 songs.
* Complete list of command bytes occurring in the songs: FF FE FD FC FB FA F9 F8 F7 F6 F5 F4 F3 F2 F1 EF EE ED
  E7 E3 E2 DB DA CF CD CC CB CA C9 C8 C5 C1 BB B8; additionally in the effects: DD D4. Everything else
  (C0, C4, C6, B5–B7, B9–BF except BB, D0–D3, D5–D9, DC, DE, DF, E0–E6, E8–EC, F0, …) is unused by Xenon.

## 11. Emulation harness and verification

### 11.1 `re-tools/pmdemu.py` (unicorn)
Runs the real PMD.COM: loaded at PSP 1000h with tail `/M10 /V0 /E4 /F /K`, the real installer runs to the TSR
call (minimal INT 21h). Fake 26K: YM2203 at 188h/18Ah (never busy, status b0/b1 = timer flags), SSG reg 0Eh
reads C0h (INT5 jumper → IRQ 12, vector 14h, handler `2A71`), no OPNA (reg FFh = 0), PIC stubs. One hack: the
installer's CPU calibration (`4316`) needs the PIT; at `43B7` BX is forced so the OPN wait count [3150] = 1.
The song is loaded like AGS.EXE (AH=06h buffer, copy, AH=0Bh EFC buffer, AH=00h). Timers are simulated from
the register values (A: 72·(1024−NA), B: 1152·(256−NB) master clocks; new values take effect at the next
overflow, as on the chip; both re-phased at song/effect start), the handler runs with the right status flags,
and each register write is stamped with the overflow time. Timer deadlines are integer master clocks: both
periods are multiples of 2304 clocks, so the two timers coincide exactly every 0.35–0.8 s, and such an overflow
is one interrupt with both flags set (the handler then does the Timer-B work first), as on the chip. (Until
2026-11-08 the periods were accumulated in floating point and a tie was recognised only within 1e-12 s; after
≈ 1400 Timer-A periods the rounding drift exceeded that and a coincident overflow was split into two
interrupts, A first, which changed the Timer-A count of the following music clock by ±1 and shifted the
MX-mode LFO steps of 19 songs from 26–47 s on. The song VGMs were re-recorded; the effects were unaffected.) Output VGM 1.51, YM2203 @ 3 993 600 Hz, 44.1 kHz
waits; songs are recorded until status2 = 2 and written as intro + pass 1 + pass 2, with the VGM loop point
at the start of the Timer-B tick in which status2 became 1 and the end just before the tick in which it became
2, so the loop body is pass 2 (songs that end: no loop). Pass 1 is not a valid loop body: it starts from the
intro's state, and for songs whose `L` is at the very start (03 04 06 10 11 12 16 17 21 24) it contains the
song-start initialisation and the first Timer-B period, which still runs at TB=C8h; looping it made every
pass 2–20 ms short and replayed the init writes (SSG volume 0, mixer, TL 7Fh). Effects run until PMD clears
311Eh (+0.3 s); looping effects (4 6 7 21 22 25 30 34 40 44 50) until their state after a Timer-A tick
repeats, and the loop starts at the first write of the Timer-A tick that followed the matching state (anchoring
it at the next write of any kind — often a Timer-B acknowledge in between — made each pass up to one Timer-A
period, 7–74 ms, too long). Redundant rewrites are pruned from the VGM only (`--log` TSV is complete). Files:
`resources/music/vgm/*.vgm` (4.3 MB), `resources/sfx/vgm/se_NN.vgm` (256 KB). Python API: `PMDEmu`
(`load_music`, `load_efc`, `mstart`, `se_on`, `step`, `run`, `writes`, `read_cs`, `int60`).

### 11.1a `re-tools/vgmverify.py` — VGM files vs. the real driver
Expands each VGM by its loop to 3 more passes and compares it with an uncut recording of the real PMD.COM
(pmdemu), after reducing both to effective chip-state changes (continuous register shadow; F-number pairs as
one commit; 28h only on slot-mask changes; 27h only bits 6–7; timer registers ignored), so pruning and the
explicit rewrites after the loop point do not count. Result for the current files (2026-11-08):

| class | files |
|---|---|
| identical events, timing ≤ 2 samples (loop-length rounding) | all 50 effects; songs XEN_03A/B, 02 03 04 05 06 08 09 10 11 12 13 14 15 16 18 19 20 21 22 23 24 25 26 27 28 29 |
| real PMD does not repeat itself (consecutive passes differ) — no one-pass loop can be exact | 01 07 17 |

In the second class the intro and the first two passes in the file are exact; later passes differ only by
the Timer-A-driven modulation (every differing event, matched within one Timer-A period, is an F-number,
SSG volume or SSG tone-period write; no key-on, voice or operator differences): LFO pitch (fm-freq), SSG software envelopes (ssg-vol) and SSG LFO (ssg-tone),
which step at the Timer-A rate (≈ 54 Hz) and whose phase against the Timer-B tempo clock shifts every pass
(XENON_01: fm-freq and ssg-vol from 158 s; XENON_17: ssg-tone from 381 s), plus XENON_07's random LFO (`MW`,
from the first repeat at 16.7 s). Before the timer-tie fix of 2026-11-08 (§11.1) seven more songs (02 15 22 24
25 26 29) fell into this class; that was the harness splitting coincident interrupts, not the driver. On real hardware every pass differs the same way. XENON_19's `[ … ]0`
loop is one note tied to itself (`F9 … 54 60 FB F8 00`: note 54h, length 96, tie; parts A and B): after the first 96 clocks PMD writes nothing
and the note just keeps sounding, so in the game it is heard once and then decays; the VGM loop is
correspondingly empty (an earlier cut re-struck it every 2.37 s).

Playback with libvgm (VGMPlay 0.52.1 / `vgm2wav`, YM2203 = MAME core, SSG via EMU2149 by default): all files
load, rendered lengths equal the headers, output is mono and loop seams are click-free. Two properties of
libvgm's mix, not of the files: libvgm's SSG output is unipolar, which adds a DC offset of up to ≈ −23 dBFS
(the 26K's output coupling removes it on hardware); with that offset and libvgm's YM2203 gain a few tracks
reach full scale and clip briefly (XENON_02/05/06/09/15/24/26/27/28/29, se_14/20/24/30/37/49; at most
≈ 1000 samples in a 2-pass render; use e.g.
`vgmplay -c General.Volume=0.7`). The 26K board's analog FM/SSG balance is not modelled by VGM and was
not checked against a hardware recording (none available).

### 11.2 `re-tools/pmdmodel.py` — formulas vs. the real driver
An independent re-implementation of §4–§9 (no PMD code is executed) producing the register writes per clock.
`--check` runs pmdemu, splits its writes per Timer-B clock (feeding the model the observed number of Timer-A
interrupts per clock for MX/EX) and compares the complete ordered write lists (IRQ acknowledges 27h = 3F/7F
excluded). Results:

| test | clocks | register writes | differing clocks |
|---|---|---|---|
| all 31 songs, 9000 clocks each (`--check resources/original/game/*.M --ticks 9000`) | 279 000 | 1 653 168 | **0** |
| XENON_01/02/15/24 with fade-volume 40h at clock 300, 0 at 900, fade speed 20 at 1500 | 10 000 | 66 620 | **0** |
| all 50 effects, FM registers, up to 4000 Timer-A clocks (`--check-se`) | – | 29 157 | **0** |
| synthetic SYNTH1/SYNTH2 (Q, B5, B6 abs/rel, B8, BF/BC/BE/BA/B9, C2, C5, C7, waves 1/4/5/6, D6+B7, C0 mask FM/SSG, C1, DE/DD, D5, E7, FC FF/FD/FE, C0 FF/FE/FD/FC volume-down, F0 envelope, CD with EX, ED/EE/D0 noise, DX on/off, SSG LFO volume, SSG square/random LFO, FM3 C5 rotation quirk) | 3 000 | 3 867 | **0** |

The synthetic songs are generated by `re-tools/pmd_mksynth.py` (prebuilt: `re-tools/testdata/SYNTH1.M`, `SYNTH2.M`). Not covered by the model: part K,
interaction with running effects (masking/unmasking), PPSDRV, skip/fast-forward, `C6`.

## 12. Corrections for sound.md (for the coordinator)
* §4: "D+00 … D+14 … A B C D E F (FM 1–6)": D/E/F are FM3-extended (slot) parts of channel 3; J is never played
  by this PMD build; the voice-table condition is "part A offset ≠ 18h".
* §4 bytecode: `F9` operand = offset of the matching F8's count byte (not "−1"); `C0 n` with 2 ≤ n ≤ F6h is an
  invalid command (part end); `C1` belongs right after a note's length (gate 0).
* §4.2: "part D is FM channel 4 … lose one FM voice on a 26K board" is wrong: part D plays FM3 slots 3+4 on a
  26K (and identically on an 86 board); all Xenon music is 3 FM channels (ch3 split C/D) + 3 SSG.
* §4.1: `[ ]0` wraps also count for status2 (they set the part's loop flag).
* §6 porting notes: music_set_fadevol (AH=19h) is applied by each part at its next note, not immediately;
  FM volume includes the default FM volume-down 16 (V·240/256); vibrato and SSG envelopes run at the
  Timer-A rate (54.17 Hz), not per music clock.

## 13. Open questions
* MML spellings of the rarely used commands (B5–BF, C5–C9, CC) are from PMD convention, not from the binary.
* Exact analogue output differences between 26K and 86 boards (SSG level, YM2608 FM DAC) are outside the
  driver; register streams are identical apart from the OPNA-only init writes.
* The K part / SSG drum path and the effect masking interplay are code-read only (no Xenon data uses K; the
  harness can play effects over a song but no automated comparison was done).
* Real tempo depends on the actual OPN clock of the board (3.9936 MHz assumed).
