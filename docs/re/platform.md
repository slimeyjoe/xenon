# Platform layer: input, timing, interrupts, hardware checks, low-level library

Addresses are `SEG:OFF` in the shared-listing convention (file offset = 0xE00 + SEG*16 + OFF); data globals
are `DS:xxxx` (DGROUP 0F9F). **verified** = read in code; **inferred** = reasoned. Names are in
`listing/names.tsv`. Sound/music is in `sound.md`.

## 1. Segment map (platform-relevant)

| segment | contents | notes |
|---|---|---|
| 0000 | Borland C++ RTL (PC-98 flavour) | conio via `ESC[>` sequences + INT DCh CL=10h (0000:2779 `_setcursortype`, 0000:2953/294C function-key row off/on), `delay` 0000:28FF (PC-98 timing loops on GDC status / ports 5Ch/5Eh), `sound`/`nosound` (8253 + port 37h beeper), `getvect`/`setvect` twice (0000:0626/0635 and 0000:0753/0762), `int86` 0000:2081, `int86x` 0000:20B2, `intr` 0000:214B, `spawnl` 0000:25EC, `malloc` 0000:1CF0 / `farmalloc` 0000:1CFA (full list: functions.md) |
| 0450 | `main` | core area |
| 0843 | drive/disk helpers (`xfopen`, `disk_sense`, …) | engine.md §5 |
| 0AF8 | keyboard + input abstraction | §3 |
| 0B2C | music / SE driver interface | sound.md |
| 0C2A:0A20–19FF | dead "MUSIC LALF"/"ANNEX" YM2203 driver (own frame 0CCC) | never referenced; graphics code of 0C2A ends at 0A19 |
| 0DCA | display / palette / VSYNC-wait library (in-house asm+C) | §6 |
| 0E1A | init / shutdown / checks | §2 |
| 0E9B | PC-98 bus-mouse driver (INT 15h) | §3.4 |
| 0ECB | VSYNC interrupt (INT 0Ah) service | §4.2 |
| 0F16 | kanji-ROM / font access (ports 68h, A1h/A3h/A5h/A9h, INT 18h AH=14h/1Ah) | text-printer area — see §7 |

## 2. Startup and shutdown

### 2.1 `engine_init` 0E1A:007F (**verified**, called from `main`)

| step | call | effect |
|---|---|---|
| 1 | 0DCA:00D9 `graph_off` | INT 18h AH=41h (stop graphics display) |
| 2 | 0E1A:0001 `check_free_memory` | `coreleft()` (0000:16C2 → `farcoreleft` 0000:2018) < 6C278h (443 000 bytes) → print "\n約550KBytes程度の空きメモリが必要です。\n" and `exit(1)`. (The message rounds up: PMD and DOS are already resident; **inferred**) |
| 3 | 0000:2779 `_setcursortype(0)` | `ESC[>5h` – hide text cursor |
| 4 | 0000:2953 `fnkey_line_off` | `ESC[>1h` – hide function-key row |
| 5 | 0DCA:0041 `video_init` | see §6 |
| 6 | 0B7D:0A27 `text_layer_clear` | `ESC*` |
| 7 | 0AF8:0086 `bios500_set_bit5` | save byte 0000:0500, set bit 5 (restored at exit). Bit 5 of BIOS_FLAG = do not beep when the keyboard buffer overflows (**inferred**, memsys.txt 0000:0500h; §6.1 *Sources*) |
| 8 | 0AF8:0022 `kbd_hook_copy_stop` | INT 05h (COPY key) and INT 06h (STOP key) → 0AF8:000A (`push all … iret`) — disables hard-copy and STOP/break |
| 9 | 0F16:0009 | font cache: kanji-ROM dot mode (port 68h←0Bh), 96× INT 18h AH=14h for codes 2920h+i → DS:1B66 (16 B each), port 68h←0Ah; once only (DS:0B7C) |
| 10 | 0B7D:0A27, `set_access_page(1)`, `gvram_clear`, `set_access_page(0)`, `gvram_clear`, `set_display_page(0)`, `graph_on` | clear both graphics pages |
| 11 | 0F16:0841(DS:09B2, 7621h) | INT 18h AH=1Ah: define user (gaiji) character 7621h from the pattern at DS:09B2 |
| 12 | 0B7D:09A5(1) | text GDC CSRFORM (cmd 4Bh → port 62h, params → 60h): cursor shape/blink off (argument unused) |
| 13 | 0E9B:000F `mouse_init` | §3.4 |
| 14 | 0B2C:006F `snd_init(1)` | sound.md §1.1 |
| 15 | 0C2A:000E, 0B7D:0A27 | graphics module init (graphics area), clear text |

### 2.2 `engine_shutdown` 0E1A:011C (**verified**)

`graph_off`, 0C2A:009B, 0ED8:0217, 0ED8:03CB (graphics/animation teardown), `se_stop`, `music_stop`,
`mouse_shutdown`, 0B7D:09A5(0), clear both graphics pages, `set_access_page(0)`, `kbd_unhook_copy_stop`,
`bios500_restore`, `_setcursortype(1)` (`ESC[>5l`), `fnkey_line_on` (`ESC[>1l`), `graph_on`. It does **not**
unload PMD (XENON.BAT does `pmd /r`). Called from the normal quit path and from the emergency-quit key
combination (§3.3).

### 2.3 Hardware checks

* Memory: §2.1 step 2 (**verified**). The program is otherwise run from the Borland far heap: `malloc`
  (0000:1CF0, = `farmalloc(0:size)` 0000:1CFA; blocks are returned as `seg:0004`) is called by the script loader (088D:00F8, 088D:0135), the GDT loader (0C2A:03DB, size =
  `filelength`) and the ADT loader (0ED8:018B); freed with `farfree` 0000:1BBD. No direct INT 21h AH=48h.
* `check_dipsw_gdc` 0E1A:0041 (**unused**, no caller; verified): tests BIOS byte 0000:054D bit 5 and, if set,
  prints "DIP SW 2-8 を OFF にして下さい。" / "GDC クロックが 5MHz での動作は保証されません。" and exits. So the
  bit was meant to mean "GDC runs at 5 MHz". Confirmed by the documentation: 054Dh bit 5 is the boot-time
  copy of DIP SW 2-8 (ON = 5.0 MHz GDC clock in 640×400). The *current* clock is bit 2, tested by `gdc_scroll`
  (§6.1). 0F16:010C tests 0000:054D bit 6, documented as "EGC present". The font code uses it as a proxy for
  "kanji ROM readable through the CG window at A400h" (font.md §2) (**inferred**, memsys.txt).
* No CPU / machine-type / sound-board check other than the PMD signature and PMD AH=09h board check
  (sound.md). `delay()` in the RTL selects its calibration from BIOS bytes 0000:0501 bit 3 (hi-reso / CPU
  clock group), 054Ch bit 5, 0459h bit 3, 0458h bit 7, 045Bh bit 2 (Borland PC-98 RTL behaviour; **verified**
  reads, meaning **inferred**).

## 3. Input

### 3.1 Keyboard primitives (seg 0AF8, **verified**)

Polling only; no keyboard interrupt hook (INT 09h untouched). BIOS work area: 0000:0528 key-buffer count,
0000:052A..0539 key-status bitmap (bit n%8 of byte 52Ah+n/8 = scan code n held), 0000:053A shift status
(bit0 SHIFT, 1 CAPS, 2 KANA, 3 GRPH, 4 CTRL).

| address | name | behaviour |
|---|---|---|
| 0AF8:013C | `kbd_read_cmd` | if 0000:0528 == 0 → 7 (no key). Else INT 18h AH=00h (read key), map **scan code** (AH) via the switch at 0AF8:01D4 (table below). |
| 0AF8:00BD | `kbd_getch_nowait` | INT 21h AH=06h DL=FFh; returns 0 if none. For codes < 20h: 08h with BS held (52Bh bit6) → 0Fh; for 0Dh, or when XFER/ROLL UP/ROLL DOWN (530h & E0h) or INS…HELP (531h) are held, AH = (INT 18h AH=02h shift status) & 19h. Used only to **drain** type-ahead (menus, `kbd_flush`). |
| 0AF8:0133 | `kbd_flush` | repeat `kbd_getch_nowait` until 0 |
| 0AF8:0108 | `kbd_kana_locked` | INT 18h AH=02h, bit 2 → 1 if KANA lock is on |
| 0AF8:02AB, 0DCA:0004 | `kbd_ctrl_held`, `ctrl_held` | 0000:053A & 10h |

Scan code → command (`kbd_read_cmd`):

| code | keys (PC-98 scan code) | meaning in menus / waits |
|---|---|---|
| 0 | ESC (00h), numpad 0 (4Eh) | cancel (menu → −1 → system menu); in text waits also advances |
| 1 | ↑ (3Ah), numpad 8 (43h) | up |
| 2 | ↓ (3Dh), numpad 2 (4Bh) | down |
| 3 | ← (3Bh), numpad 4 (46h) | left (ignored by menus) |
| 4 | → (3Ch), numpad 6 (48h) | right (ignored by menus) |
| 5 | RETURN (1Ch), SPACE (34h) | OK / advance text |
| 6 | any other key | ignored |
| 7 | (buffer empty) | — |
| 8 | HELP (3Fh) | menu returns −2 |
| 9–13 | f·1–f·5 (62h–66h), SHIFT+f·1–5 (82h–86h), CTRL+f·1–5 (92h–96h) | function keys (no consumer found in menus; **open**) |
| 14 | BS (0Eh) | — |

Key repeat is the keyboard's own typematic repeat feeding the BIOS buffer; the engine has no repeat logic.

### 3.2 `input_get_cmd` 0AF8:02C7 — unified blocking input (**verified**)

```
mouse_set_pos(100,100)
loop:
  k = kbd_read_cmd(); if k != 7 { g_input_from_mouse = 0; return k }
  g_input_from_mouse = 1
  (x,y) = mouse_get_pos()
  if y > 120 return 2 ; if y < 80 return 1 ; if x > 120 return 4 ; if x < 80 return 3
  if mouse_left_clicked()  return 5
  if mouse_right_clicked() return 0
```
So moving the mouse more than 20 px from the re-centred point yields one cursor step (vertical has priority),
left click = OK, right click = cancel. `g_input_from_mouse` (DS:13B6) makes `select_menu` (0B7D:05A1..)
**clamp** at the first/last item instead of wrapping (keyboard input wraps). (An earlier name `g_menu_wrap`
had the meaning inverted; names.tsv now uses `g_input_from_mouse`.)

`select_menu` 0B7D:048C uses only 1/2 (move), 5 (select → index), 0 (→ −1), 8 (→ −2); it drains the DOS
keyboard buffer first (0B7D:0548).

### 3.3 `wait_ok_key` 0AF8:0254 — "press a key" wait used by the text printer (**verified**)

```
kbd_flush()
loop:
  if CTRL held: return                         ; skip
  if (0040:0130 & E0h) == E0h:                 ; XFER + ROLL UP + ROLL DOWN held together
      engine_shutdown(); gvram_clear(); text_layer_clear(); puts("GAME END"); exit(0)
  k = input_get_cmd(); if k == 5 || k == 0: return
```
Callers: `text_print` page/paragraph waits (0B7D:0257/0404/0448), `text_print_big`, `msg_box`, `error_box`,
`opening_title`. Advance keys: RETURN, SPACE, ESC, numpad 0, left/right mouse click. **CTRL held when a wait
starts skips it**; together with `wait_vsync` (below) holding CTRL fast-forwards text, waits and fades.
The CTRL check happens only at entry / after a non-OK key, because `input_get_cmd` blocks.

Other input-related behaviour: KANA lock state is read for every message statement (opcode FDh, 088D:0745)
and passed (KANA off → 1, on → 0) as the last argument of `text_print` → `kanji_draw` 0F16:03D7, which does
`wait_vsync(flag)` before every full-width glyph: KANA lock on = instant text (font.md §2.1). INT 05h/06h are nulled (§2.1), PMD's ESC/GRPH hot keys are disabled by `/K`.

### 3.4 Mouse driver, seg 0E9B (**verified**)

PC-98 bus mouse (8255 at 7FD9h/7FDBh/7FDDh/7FDFh, interrupt IR13 = INT 15h).

* `mouse_init` 0E9B:000F: limits Y 0..18Fh (DS:166C/166E), X 0..27Fh (DS:1670/1672); disable; 8255 mode
  word 93h → 7FDFh; port BFDBh ← 1 (mouse interrupt rate; 1 = 60 Hz per PC-98 docs, **inferred**); save
  INT 15h (DS:167E), set to `isr_mouse` 0E9B:009C; enable (`mouse_irq_enable` 0E9B:007C: 7FDFh ← 08h = PC4
  reset = interrupt enabled, unmask slave PIC IR5 via port 0Ah bit 5).
* `mouse_shutdown` 0E9B:005D: disable, BFDBh ← 0, restore INT 15h.
* `mouse_irq_disable` 0E9B:008C writes 09h to **port 7FD7h** (sic) — almost certainly a typo for 7FDFh; the
  PIC mask (port 0Ah bit 5) is still set, so it is harmless.
* `isr_mouse`: re-entrancy guard DS:0AFC, private stack SS=DS SP=166Ch; reads X and Y deltas as two 4-bit
  nibbles each (7FDDh ← 20h/00h for X hi/lo, 60h/40h for Y hi/lo, data in 7FD9h low nibble), then
  7FDDh ← 80h, 00h (latch). Adds signed deltas to DS:0AF2 (x) / DS:0AF4 (y), clamps. Buttons: 7FD9h bit 7
  (left) / bit 5 (right), active low. A press edge sets DS:0AFA / DS:0AFB (if neither latch is pending); a
  release edge clears the latch again. EOI to slave (port 08h) and, if the slave ISR is empty, master (00h).
* API: `mouse_get_pos` 0E9B:024B, `mouse_set_pos` 0E9B:0262, `mouse_left_clicked` 0E9B:02A0 / `mouse_right_clicked`
  0E9B:02B4 (test-and-clear latch), `mouse_get_buttons` 0E9B:0273 (unused).

No mouse cursor is drawn; the mouse only emulates cursor keys/OK/cancel.

## 4. Timing

### 4.1 `wait_vsync(n)` 0DCA:0012 — the engine's only general timer (**verified**)

```
for i in 0..n-1:
   while (in(60h) & 20h) ;        ; wait end of VSYNC   (text GDC status, bit 5 = VSYNC)
   while !(in(60h) & 20h) ;       ; wait start of VSYNC
   if (0000:053A & 10h) break     ; CTRL held -> abort remaining wait
```
Polling, no interrupt. One unit = one display frame: 56.42 Hz in the 640×400 24.83 kHz mode of a standard
machine, 70.10 Hz on a PC-9821 set to 31 kHz (**inferred** from documentation, not measured; §6.1). Used by script op 20h (`WAIT n`), palette fades (one step per `delay`
frames), text printer pacing (0B7D:0457), menu highlight, transitions (0C2A fx_*), the opening and staff roll.

0DCA:006C (`gdc_wait_vsync_idle`, near): graphics GDC status port A0h — wait VSYNC (bit 2) then wait until
drawing idle (bit 3 clear); used before page flips (0DCA:0174, unused).

### 4.2 VSYNC interrupt (INT 0Ah, IR2) — seg 0ECB (**verified**)

Installed only by the eye-blink animator of seg 0ED8 (`blink_start` 0ED8:036F calls `vsync_irq_install` at 0ED8:03B6
and sets the hook to `blink_tick` at 0ED8:03C1; `blink_stop` 0ED8:03CB clears the hook at 03DB and removes it
0ED8:03E3). `vsync_irq_install` 0ECB:004F: hook ← NULL, mask IR2 (port 02h |= 04h), save INT 0Ah (DS:1686),
set 0ECB:0001, unmask, `out 64h,0` (CRT interrupt re-arm). `isr_vsync` 0ECB:0001: guard DS:0B02, EOI
(port 00h ← 20h), `++g_vsync_ticks` (DS:0AFE, 32-bit), `call far [DS:168A]` if non-NULL, `out 64h,0`.
`vsync_irq_remove` 0ECB:0090 masks IR2 again and restores the vector (IR2 stays masked).
Nothing reads DS:0AFE outside the hook (graphics analysts: see 0ED8).

### 4.3 Other timing
* RTL `delay(ms)` 0000:28FF + `sound(Hz)`/`nosound` — used only by `error_box` 088D:13DF (500 Hz beep,
  500 ms).
* No INT 08h/1Ch hooks, no 8253 reprogramming. Music timing is entirely inside PMD (OPN timers, sound.md §2).

## 5. Interrupt vectors touched (complete list, **verified** by all getvect/setvect callers)

| vector | when | handler | restored |
|---|---|---|---|
| INT 05h (COPY) | engine_init | 0AF8:000A null ISR | engine_shutdown |
| INT 06h (STOP) | engine_init | 0AF8:000A null ISR | engine_shutdown |
| INT 15h (mouse, IR13) | engine_init | 0E9B:009C | engine_shutdown |
| INT 0Ah (VSYNC, IR2) | eye-blink animator only (`blink_start`, script op 2Eh) | 0ECB:0001 | `blink_stop` (op 2Fh, shutdown) |
| INT 60h / 61h | read only (signature) | PMD / MMD | — |

Software interrupts called: INT 18h (BIOS: keyboard AH=00/02, CRT/graphics AH=40h/41h/42h/4Ah (0Ch/0Dh wrappers exist but are unused),
font AH=14h/1Ah), INT 1Bh (disk sense, core), INT DCh (DOS console/drive extensions), INT 21h, INT 60h (PMD).

## 6. Library function reference — seg 0DCA (display / palette / wait) (**verified**)

All far cdecl. Palette shadow `g_pal_shadow` DS:1438 (16 × {r,g,b}, 0..15 each).

| address | name | args | behaviour |
|---|---|---|---|
| 0DCA:0004 | `ctrl_held` | – | 0000:053A & 10h |
| 0DCA:0012 | `wait_vsync` | n | §4.1 |
| 0DCA:0041 | `video_init` | – | out 6Ah,01h (16-colour analog mode); INT 18h AH=42h CH=C0h (640×400 colour graphics); INT 18h AH=4Ah CH=06h (GDC drawing mode, no effect here); out 6Ah,83h (GDC CLOCK-1 = 5 MHz, no effect alone); out 6Ah,41h (text/graphics alignment: plasma/LCD mode); access page 0, display page 0. Details: §6.1 |
| 0DCA:006C | `gdc_wait_vsync_idle` | – (near) | §4.1 |
| 0DCA:008B / 00A5 | `text_on` / `text_off` | – | INT 18h AH=0Ch / 0Dh (unused) |
| 0DCA:00BF / 00D9 | `graph_on` / `graph_off` | – | INT 18h AH=40h / 41h |
| 0DCA:00F3 / 0112 | `gdc_mode_4a_16` / `gdc_mode_4a_06` | – | INT 18h AH=4Ah with CH=16h (unused) / 06h |
| 0DCA:0131 | `graph_mode_640x400` | – | INT 18h AH=42h CH=C0h |
| 0DCA:0150 | `set_access_page` | p | out A6h,p; DS:1468 = p (CPU-visible VRAM page) |
| 0DCA:0162 | `set_display_page` | p | out A4h,p; DS:146A = p |
| 0DCA:0174 | `set_pages_vsync` | a, d | GDC wait, then both (unused) |
| 0DCA:0196 | `get_pages` | int far *a, int far *d | (unused) |
| 0DCA:01AD | `set_modeff2` | v | out 6Ah,v |
| 0DCA:01B9 | `pal_set` | i, r, g, b | out A8h,i; AAh,g; ACh,r; AEh,b; shadow[i] = {r,g,b} |
| 0DCA:0200 | `pal_set_rgb` | i, {r,g,b} by value (3 bytes on stack) | same |
| 0DCA:023A | `pal_get_rgb` | i, far *dst | copy shadow entry (unused) |
| 0DCA:0258 | `pal_set_all` | far rgb[16] | 16 × `pal_set_rgb` |
| 0DCA:0288 | `pal_set_all_but8` | far rgb[16] | skips colour 8 (unused) |
| 0DCA:02E0 | `pal_get_all` | far *dst | copy 48-byte shadow |
| 0DCA:02F7 | `pal_fade_to` | rgb[16] (48 bytes by value), delay | loop: `wait_vsync(delay)`; move every component of a working copy 1 step toward the target, `pal_set_all`; ends when all 16 entries match. ≤15 steps; CTRL makes it instant (wait skipped) |
| 0DCA:0445 | `pal_fade_black` | delay | `pal_fade_to(all 0)` |
| 0DCA:0494 | `pal_fade_white` | delay | `pal_fade_to(all 15)` |
| 0DCA:04E3 | `gvram_clear` | – | GRCG TDW mode (7Ch←80h, tile regs 7Eh ← 0 ×4), `rep stosw` 3E80h words at A800:0000 → clears all 4 planes of the access page; 7Ch←0 |

It is a small in-house library (C wrappers over INT 18h + inline port I/O), not a known third-party PC-98
library (no copyright strings, Borland-style `enter`/cdecl code) (**inferred**).

### 6.1 `video_init` step by step

The sequence of the code is **verified** (0DCA:0041–006B; called once, from `engine_init`). The hardware
meaning of each step is **inferred** from the documentation and emulator sources listed under *Sources*.
None of it depends on the machine's DIP switches except where noted.

| # | code | effect |
|---|---|---|
| 1 | `set_modeff2(1)` → `out 6Ah,01h` | mode F/F 2 item 0 = 1: 16-colour (analog palette) mode |
| 2 | `graph_mode_640x400` → INT 18h AH=42h CH=C0h | 640×400 colour graphics area. The BIOS also programs the graphics GDC for the GDC clock chosen by DIP SW 2-8 (0000:054Dh bit 5): 2.5 MHz → PITCH 40 and SCROLL IM bit clear; 5 MHz → PITCH 80, IM bit set and 0000:054Dh bit 2 = 1 (NP2kai `bios0x18_42`; memsys 054Dh bit 2 table) |
| 3 | `gdc_mode_4a_06` → INT 18h AH=4Ah CH=06h | "set drawing mode" (描画モードの設定): CH becomes parameter P1 of the graphics GDC SYNC command, and 0000:054Dh bit 3 = NOT (CH bit 4) (NP2kai). In µPD7220 terms (P1 = `0 0 C F I D G S`) 06h is graphics mode with DRAM refresh, non-interlaced, F = 0: the GDC may draw during the active display ("flash drawing"). The unused `gdc_mode_4a_16` (16h, F = 1) restricts GDC drawing to the blanking interval ("flashless"). **No visible effect in this game:** the setting only concerns drawing commands executed by the GDC, and the only graphics-GDC command AGS.EXE ever sends is SCROLL (70h at 0B7D:0A84; all drawing is done by the CPU and the GRCG). All machines except the PC-9801 original/E/F/M/U/VM2/VF draw flashless regardless (054Dh bit 4). The call fills only AX and CX of the `REGPACK`; BX/DX are stack garbage, which AH=4Ah does not read (NP2kai) |
| 4 | `out 6Ah,83h` | mode F/F 2 item 41h, **GDC CLOCK-1 = 5.0 MHz**. The GDC runs at 5 MHz only if GDC CLOCK-2 (84h/85h) is also set to 5 MHz; either one at 2.5 MHz gives 2.5 MHz (io_disp.txt). AH=42h sets both bits together, and the engine never writes 84h/85h. So on its own this write cannot change the clock: it is a no-op when the BIOS chose 5 MHz, and leaves the GDC at 2.5 MHz otherwise. The BIOS state (PITCH, IM bit, 054Dh bit 2) therefore still matches the real clock, which `gdc_scroll` relies on (opcodes.md op 32h). Why the write is there is unknown; it contradicts the dead `check_dipsw_gdc` (§2.3), which refuses a 5 MHz GDC |
| 5 | `out 6Ah,41h` | mode F/F 2 item 20h (undocumented by NEC), **text/graphics display mode = plasma display / LCD mode** (40h = CRT mode). In CRT mode the text layer appears one dot left of the graphics layer; in this mode both line up. The effect: the text-layer ▼ page prompt and the error box sit exactly on the graphics pixel grid. Neither emulator models the shift: MAME drops 6Ah values ≥ 10h (`pc9801rs_video_ff_w`), NP2kai stores the flag (`GDCDISP_PLAZMA`) but its renderer never reads it |
| 6 | `set_access_page(0)`, `set_display_page(0)` | ports A6h / A4h |

Port 6Ah is written nowhere else, and `engine_shutdown` does not restore it (§2.2). After the game exits, the
machine stays in 16-colour mode with plasma-mode alignment and CLOCK-1 = 5 MHz; all three are harmless.

**Refresh rate** (0000:054Ch bit 5 table, memsys.txt): the 640×400 mode is 24.83 kHz / **56.42 Hz** on a
standard normal-mode machine. As a cross-check, NP2kai's 24 kHz master-GDC SYNC defaults give 400 + 8 (VS) + 7 (VFP) + 25 (VBP) =
440 lines, and 24.83 kHz / 440 = 56.43 Hz. PC-9821 models (and the PC-9801BA2/BS2/BX2/BA3/BX3/BX4) set to 31 kHz
with GRPH+2+RESET output 640×400 at 31.47 kHz / **70.10 Hz**. The engine never reprograms CRT sync, so there
every frame-based duration (§4.1) runs 1.24× faster. The 15.98 kHz / 61.47 Hz signal (DIP SW 1-1 OFF,
PC-8001 monitor) is documented for 640×200 only.

**Sources**
* *Undocumented 9801/9821 Vol.2* (webtech.co.jp/company/doc/undocumented_mem/): `io_disp.txt` (I/O 006Ah
  mode F/F 2 table), `memsys.txt` (0000:0500h, 054Ch, 054Dh).
* Neko Project II kai (github.com/AZO234/NP2kai): `io/gdc.c` (`gdc_o6a`, sync tables `defsyncm24`),
  `bios/bios18.c` (INT 18h AH=42h/4Ah).
* MAME (github.com/mamedev/mame): `src/devices/video/upd7220.cpp` (`update_graphics`: an IM area uses half the
  pitch), `src/mame/nec/pc9801.cpp`.

## 7. Notes for other areas

* **Graphics**: 0C2A:0A20..19FF is not graphics code (dead LALF FM driver). Palette and page functions are
  all in 0DCA (table above). VSYNC IRQ is used only by the blink animator (0ED8:036F/03CB); ADT playback polls `wait_vsync`.
* **Text**: 0F16 is the kanji-ROM/font layer (port 68h kanji access mode, A1h/A3h code, A5h/A9h line read,
  CG window at A400h when 0000:054D bit 6) — documented in [font.md](font.md). 0F16:0841 = INT 18h AH=1Ah
  user character 7621h (the page-overflow prompt glyph). 0B7D:09A5 = text-cursor form. KANA lock flag (§3.3).
* **Core**: DS:13B6 meaning (§3.2). XFER+ROLL UP+ROLL DOWN emergency exit (§3.3).
* **Script**: op 20h = `wait_vsync(n)` (frames, CTRL-skippable); sound ops in sound.md §3.1.

## 8. Open questions

* ~~Meaning of BIOS byte 0000:0500 bit 5~~ — resolved: suppresses the key-buffer-overflow beep (§2.1 step 7).
* ~~INT 18h AH=4Ah CH=06h/16h and port 6Ah values 83h/41h in `video_init`~~ — resolved (§6.1). Not checked
  on real hardware: the 1-dot text/graphics offset that 41h removes, and whether any BIOS sets 2.5 MHz by
  clearing only CLOCK-1. In that case the 83h write would switch the GDC to 5 MHz behind the BIOS's back.
* Consumers of key commands 9–14 (f·1–f·5, BS): none found. `title_menu_select` (0ABA:01A5) uses only
  1/2/5 (no cancel, no wrap); `omake_menu` (0ABA:014C) dispatches 0..5 through the jump table at 0ABA:0184
  (core area); `select_menu` uses 0/1/2/5/8.
* Whether port BFDBh ← 1 means 60 Hz on all models.
