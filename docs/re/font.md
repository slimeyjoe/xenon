# Font and glyph layer (seg 0F16)

All in-game text (messages, menu options, title cards) is drawn by AGS.EXE into **graphic** VRAM
with the routines of segment `0F16`. The glyphs come from the PC-98 kanji (CG) ROM; nothing is
loaded from disk. The text layer (text VRAM) is used only for the page prompts and error boxes,
through DOS console escape sequences (engine.md §5.4, §6).

Conventions as in the other documents: `SEG:OFF` in the shared-listing convention, `DS:xxxx` =
DGROUP 0F9F. **verified** = read in the code; **inferred** = PC-98 hardware/BIOS meaning taken from
standard documentation, not from the binary. Names are in `docs/re/listing/names.tsv`.

## 1. Function map

| Address | Name | Args | Behaviour (**verified**) |
|---|---|---|---|
| 0F16:0009 | `font_ank_cache_init` | – | Runs once (flag `DS:0B7C`). Port 68h ← 0Bh; for i = 0..95: INT 18h AH=14h, DX = 2920h+i, BX:CX = stack buffer; copy the 16 pattern bytes to `DS:1B66 + i*16`; then `DS:0B7C = 1`, port 68h ← 0Ah. Called by `engine_init` (0E1A) and lazily by `ank_draw` |
| 0F16:0077 | `sjis_to_jis` | near; far \*hi, far \*lo | standard Shift-JIS → JIS conversion in place |
| 0F16:00D0 | `kanji_rom_read` | sjis_hi, sjis_lo, far \*dst (32 B) | converts to JIS; port 68h ← 0Bh; `out A3h, jis_hi-20h`; `out A1h, jis_lo`; if BIOS byte `0000:054D` bit 6 is set: `rep movsw` 16 words from `A400:0000` (CG window); else for line 0..15: `out A5h, line|20h` / `in A9h` → byte 0, `out A5h, line` / `in A9h` → byte 1; port 68h ← 0Ah |
| 0F16:0170 | `glyph16_bold` | far \*g (16 rows × 16 bit) | emboldening, see §3 |
| 0F16:01B1 | `glyph16_double_height` | near; far \*dst, far \*src | each of the 16 rows written twice (16×16 → 16×32) |
| 0F16:0200 | `glyph16_to_32` | near; far \*dst (128 B), far \*src | double height, then every bit doubled horizontally → 32×32 |
| 0F16:02CE / 02EE | `glyph16_or` / `glyph16_andnot` | near; far \*dst, far \*src | 16 words: `dst \|= src` / `dst &= ~src` |
| 0F16:0310 / 0335 | `vram_put16` / `vram_put32` | near; far \*src, seg, off | 16 rows × 2 bytes / 32 rows × 4 bytes into `seg:off`, stride 80 |
| 0F16:0369 / 03A0 | `vram_put_glyph16` / `vram_put_glyph32` | near; far \*planes, off, wait | `wait_vsync(wait)` (0DCA:0012), then one `vram_put*` per plane with the segments of `DS:09AA` (A800h, B000h, B800h, E000h) |
| 0F16:03D7 | `kanji_draw` | x (bytes), y (lines), fg, bg, sjis_hi, sjis_lo, wait | 16×16 full-width glyph, §2 |
| 0F16:051D | `kanji_draw_big` | x, y, fg (ignored), sjis_hi, sjis_lo, wait | 32×32 double-size glyph, §2 |
| 0F16:05AA | `glyph8_bold` | far \*g (16 bytes) | `row \|= row << 1` |
| 0F16:05C9 / 05EE | `vram_andnot8` / `vram_or8` | seg, off, far \*src | 16 rows of one byte, stride 80 |
| 0F16:0611 | `vram_put_glyph8` | off, colour, far \*g | per plane p: colour bit p set → OR glyph, else AND NOT glyph |
| 0F16:06BC | `ank_draw` | x, y, fg, bg, ch | 8×16 half-width glyph, §2 |
| 0F16:07D5 | `text_draw_string` | x, y, fg, bg, far char \*s, wait | walks a C string: Shift-JIS lead byte (ctype table `DS:0F4D` bit 2) → `kanji_draw(…, wait)`, x += 2; other byte → `ank_draw`, x += 1 |
| 0F16:0841 | `gaiji_define` | seg, off, code | INT 18h AH=1Ah, DX = code, BX:CX = seg:off (define a user character), §4 |
| 0F16:0871 | `fill32` | near; far \*dst, value | `memset(dst, value, 32)` |

`0F16:0889..088F` is zero padding. Segment 0F16 has no data of its own; its globals are
`DS:0B7C` (cache-ready flag, byte), `DS:09AA` (4 × u16 VRAM plane segments), `DS:1B66..2165`
(ANK cache, 96 × 16 bytes) and `DS:09B2` (the user character pattern, §4).

## 2. Drawing model (**verified**)

Coordinates: `x` in byte columns (8 px), `y` in pixel lines; the VRAM offset is `y*80 + x` in the
**current access page** (port A6h; the routines write the plane segments directly). Every glyph is
drawn as an **opaque cell**: the cell is first filled with the background colour, then the glyph
pixels get the foreground colour. Per plane p (B, R, G, I):

```
cell[p]  = (bg >> p) & 1 ? 0xFF.. : 0x00..        ; fill32 / memset
cell[p]  = (fg >> p) & 1 ? cell[p] | glyph : cell[p] & ~glyph
```

| Routine | Cell | Glyph source | Colour | Wait |
|---|---|---|---|---|
| `kanji_draw` | 16×16 at (x, y) | kanji ROM via `kanji_rom_read`, then `glyph16_bold` | fg / bg (4 planes built in a 128-byte stack buffer, written by `vram_put_glyph16`) | `wait_vsync(wait)` before writing |
| `ank_draw` | 8×16 at (x, y) | ANK cache `DS:1966 + ch*16` (space 20h is special-cased as blank), then `glyph8_bold` | fg / bg, written plane by plane (`vram_put_glyph8`) | **none** |
| `kanji_draw_big` | 32×32 at (x, y−16) | ROM glyph, bold, then `glyph16_to_32` written identically into all four planes | **always colour 15 on colour 0**; the `fg` argument is not used | `wait_vsync(wait)` |

Callers and the arguments they pass:

| Caller | Routine | fg | bg | wait |
|---|---|---|---|---|
| `text_print` 0B7D:01FF (message SJIS) | `kanji_draw` | `DS:07DC` | `DS:07DE` | its 2nd argument = `!kbd_kana_locked()` |
| `text_print` 0B7D:0356 (other bytes) | `ank_draw` | `DS:07DC` | `DS:07DE` | – |
| `text_print` 0B7D:02CF (`03…03` big text) | `kanji_draw_big` | `DS:07DC` (ignored) | – | 2nd argument |
| `text_print_big` 0B7D:0067 / 0104 | `kanji_draw` / `kanji_draw_big` | `DS:07DC` | `DS:07DE` | 2nd argument |
| `menu_draw_item` 0B7D:0661 | `text_draw_string` | 15 | 0 | 0 |

`DS:07DC` / `DS:07DE` are set by script opcode 39h TEXTCOLOR `a b` (088D:1D92/1D98): `a` is the
foreground palette index, `b` the **background** colour of every glyph cell (all shipped scripts
use `b = 0`). Menu options ignore TEXTCOLOR and are always 15 on 0.

### 2.1 The KANA-lock argument (resolves an open question of scripts.md / engine.md)

The interpreter calls `text_print(str, !kbd_kana_locked())` (088D:0745). `text_print` passes that
value unchanged as the last argument of `kanji_draw` / `kanji_draw_big`, which use it as
`wait_vsync(wait)` before each glyph (0F16:036D, 0F16:03A7). Therefore (**verified**):

* KANA lock **off** (normal): one VSYNC (≈17.7 ms at 56.4 Hz) per full-width character — the
  "typewriter" effect, ≈56 characters/s. Holding CTRL makes `wait_vsync` return at once.
* KANA lock **on**: no wait — messages appear at full drawing speed.

Single-byte (ANK) characters are never delayed: `ank_draw` takes no wait argument, so text made
of ASCII (the English patch) always appears at full speed regardless of the KANA key.

## 3. Glyph processing

* **ROM access** (`kanji_rom_read`): the classic PC-98 CG-ROM port sequence. Port 68h ← 0Bh /
  0Ah switches mode flip-flop 1 to "dot access" / back to "code access" (**inferred** meaning of
  the 68h values). The code is written as first byte − 20h to A3h and second byte to A1h; each
  pattern line is read from A9h after selecting the line and half with A5h (bit 5 set for the first
  byte of the line — left half per the hardware documentation, **inferred**). When `0000:054D`
  bit 6 is set the routine instead copies 32 bytes from the CG window at `A400:0000`. That bit is
  documented as "EGC present" (*Undocumented 9801* memsys.txt), so the engine uses EGC presence as
  its test for a machine with a memory-mapped CG window (**inferred**).
* **Emboldening** (`glyph16_bold`, every full-width glyph): for each 16-bit row `r` (bit 15 =
  leftmost pixel), `r' = r | (r << 1)` (each pixel also sets its left neighbour), except that a pixel
  which is a one-pixel gap between two set pixels in the original row (`1 0 1`) stays clear. Half-
  width glyphs use the simple `row | (row << 1)` (`glyph8_bold`). The game's text is therefore
  always "bold" ROM type, one pixel wider than the ROM glyph.
* **Double size** (`glyph16_to_32`): rows duplicated, then each bit duplicated → 32×32, drawn
  16 lines above the baseline row so that the big glyph covers the current and the previous text row.

### 3.1 ANK (half-width) cache

`font_ank_cache_init` asks the BIOS (INT 18h AH=14h) for the codes 2920h…297Fh, i.e. JIS row 09h of
the PC-98 kanji ROM, which holds the 8×16 half-width glyphs of the ASCII range 20h–7Fh
(**inferred** meaning of row 29h; the 16 bytes copied per code are the pattern following the
2-byte size header the BIOS writes, **verified** copy). `ank_draw` indexes the cache as
`DS:1966 + (signed char)ch * 16` (`cbw` at 0F16:07A5), so:

* `20h`–`7Fh`: correct glyphs (20h is drawn blank without a lookup);
* `00h`–`1Fh`: 16 bytes of whatever lies at `DS:1966..1B55`;
* `80h`–`FFh` (including half-width katakana A1h–DFh): the index is negative and the glyph is read
  from unrelated DGROUP data below `DS:1966` — **half-width kana cannot be displayed** (**verified**
  from the code; no shipped script contains them).

## 4. User-defined character 7621h (the page-overflow prompt)

`engine_init` calls `gaiji_define(DS, 09B2h, 7621h)` (0E1A:00DD..00F3): INT 18h AH=1Ah defines JIS
code 7621h — the first PC-98 user-definable ("gaiji") code — from the 34 bytes at `DS:09B2`
(2-byte header `00 00`, then 16 rows × 2 bytes; header layout as for AH=14h, **inferred**). JIS 7621h
is Shift-JIS **EB9Fh**, exactly the glyph that `text_print` prints on the text layer when a message
overflows the 4-row window (string `DS:08B1` = `ESC[5;37m` EB 9F `ESC[0m`, blinking white at
row 24 col 73). The pattern is a "return key" symbol:

```
row  0..2   ................
row  3      .###########....
row  4      #...........#...
row  5..10  #........##.#...
row 11      #....#...##.#...
row 12      #...##...##.#...
row 13      #..########.#...
row 14      #.########..#...
row 15      #...........#...   (+ closing row .###########.... at DS:09D2)
```

So EB9Fh is not a missing or exotic character: it is the game's own glyph, defined at start-up.
A port needs to draw this pattern (or a "↵ in a box" equivalent) for the overflow prompt; the
end-of-message prompt is the ordinary `▼` (DS:08D2).

## 5. Notes for porting

* Fonts: 16×16 kanji and 8×16 ANK from the PC-98 CG ROM, emboldened as in §3; the text window is
  60 byte columns × 4 rows of 18 lines (engine.md §6).
* Cells are opaque (background = TEXTCOLOR's second value, 0 in practice); the window graphic below
  the text is overwritten cell by cell, not alpha-blended.
* Timing: 1 VSYNC per full-width glyph unless KANA lock is on or CTRL is held; half-width glyphs
  have no delay.
* `{BIG}` (03h) text is always white on black, irrespective of TEXTCOLOR (unused by the scripts).
