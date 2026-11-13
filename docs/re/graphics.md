# AGS.EXE graphics subsystem (Xenon ~Mugen no Shitai~, PC-98, 1994)

Scope: GDT image format and codec, ADT animations, eye-blink strips, off-screen buffers,
VRAM/palette handling, screen effects, and the graphics entry points used by the script
interpreter (seg 088D). Addresses are `SEG:OFF` in the shared-listing convention
(load base 0; DGROUP = 0F9F; file offset = 0xE00 + SEG*16 + OFF).

Markers: **[V]** = verified (read in code and/or round-tripped on data), **[I]** = inferred.

Tools: `re-tools/gdt.py` (GDT decoder, batch PNG export, engine-style compositor),
`re-tools/adt.py` (ADT player/exporter, blink-strip exporter). Output: `resources/images/`
(one indexed PNG per GDT + `index.tsv`, `composite/`), `resources/anim/`.

**Verification of the codec [V]:** the Python decoder was compared byte-for-byte against an
x86 emulation (unicorn) of the original routine `0456:0004` for all 203 DA1 files and all
89 ADT frames (292 blobs, 0 mismatches); every plane stream is consumed exactly to its
declared size. Images were also checked visually.

---------------------------------------------------------------------------------------

## 1. Hardware model used by the engine

| Item | Value | Where |
|---|---|---|
| Graphics mode | 640x400, 4 planes, 16 colours from 4096 (analog) | `0DCA:0041` |
| Plane segments | A800 (B, bit 0), B000 (R, bit 1), B800 (G, bit 2), E000 (I, bit 3) | `0C2A:01A9`, DS:0956 [V] |
| Pixel colour index | `B | R<<1 | G<<2 | I<<3`; MSB of each byte = leftmost pixel | [V] (images decode correctly) |
| Plane row stride | 80 bytes; VRAM offset = y*80 + x/8 | everywhere [V] |
| Pages | two VRAM pages; port **A6h** = access (drawing) page, **A4h** = display page | `0DCA:0150`, `0DCA:0162` [V] |
| Palette ports | A8h index, AAh green, ACh red, AEh blue (4 bits each) | `0DCA:01B9`, `0DCA:0200` [V] |
| VSYNC | polled via GDC status port 60h bit 5 (`0DCA:0012`), or IRQ2 / INT 0Ah handler (`0ECB:0001`) | [V] |
| GRCG | only used to clear a page: port 7Ch = 80h (TDW, all planes), tiles (7Eh) = 0, `rep stosw` 32000 bytes | `0DCA:04E3` [V] |
| EGC | not used [V] (no port 4A0h access in gfx code) |

`video_init` (`0DCA:0041`) [V code / I meaning]: `out 6Ah,01h` (analog 16-colour mode),
INT 18h AH=42h CH=C0h (640x400 colour graphics area), INT 18h AH=4Ah CH=06h (GDC drawing
mode; no effect, as the engine never draws with the GDC), `out 6Ah,83h` (GDC CLOCK-1 = 5 MHz,
no effect without CLOCK-2), `out 6Ah,41h` (plasma/LCD alignment: removes the CRT-mode 1-dot
shift of the text layer against graphics), then access page 0, display page 0. Details and
sources: platform.md §6.1. Refresh: 56.42 Hz (24.83 kHz); 70.10 Hz on a PC-9821 set to 31 kHz. Graphics layer on/off: INT 18h AH=40h / 41h (`0DCA:00BF` /
`0DCA:00D9`).

### Off-screen plane buffers ("da_data") [V]
`0C2A:000E` callocs four 0x7D10-byte buffers (far ptrs at DS:0944), and stores
paragraph-normalised pointers `seg:0000` at **DS:13DC** (`g_gfxbuf[4]`, plane order
B,R,G,I). Each buffer is an 80x400 plane image, i.e. a third "page" in main memory.
On failure it prints "メモリ確保に失敗しました / da_data..". Almost all image loading decodes into
these buffers; VRAM is then updated by explicit copy routines (G_DISP, wipes, blink
animator). Exceptions that decode straight into VRAM: `gdt_load_page1` (busts) and ADT
playback.

---------------------------------------------------------------------------------------

## 2. GDT ("DA1") image format

### 2.1 Header [V]
All fields little-endian. The decoder (`0456:00D5`) copies the first 16 bytes to CS:008C.

| Off | Size | Field | Notes |
|---|---|---|---|
| 00 | 4 | magic `44 41 31 00` "DA1\0" | not checked by the engine |
| 04 | u32 | file size | not used (loader uses `filelength`) |
| 08 | u8 | x in bytes (x_px = 8*x) | |
| 09 | u8 | width in bytes | |
| 0A | u16 | y (lines) | |
| 0C | u16 | height (lines) | **not used by the decoder** (columns end with terminators); used as display-rect height (`0C2A:0437`) and by effects |
| 0E | u16 | flags | byte 0E = 00 in all files; byte 0F = CFh or 4Fh in GDTs, 0Fh in ADT frames |
| 10 | 24 | palette | present only if flags & 0x8000 |
| 10/28 | 4 x u16 | compressed size of plane 0..3 | plane order B,R,G,I |
| 18/30 | ... | plane 0 stream, then plane 1, 2, 3 | contiguous |

Flag bits (word at 0E): **0x8000** palette present (`0456:00E1`); **0x4000** use the
1-byte column codec (`0456:0165`), otherwise the 2-byte codec; 0x0F00 is always set
(probably a 4-plane mask; never tested by the code [I]). The engine also tests the raw
byte `[0F] == 0xCF` in `gdt_load_diff` (`0C2A:04E0`).

Observed: 163 files `CF` (palette + 1-byte codec), 40 files `4F` (no palette, 1-byte
codec: sabun/diff overlays), ADT frames `0F` (no palette, 2-byte codec).
`XEN.GDT` (1 byte, both on HDD and floppy) is a stub/marker, not an image.

### 2.2 Palette [V]
24 bytes = 16 colours x 12 bit, two colours per 3 bytes (`0C2A:0514`, output order
R,G,B per entry into DS:13EC; port mapping `0DCA:0200`):

```
for n in 0,2,4..14:  b0,b1,b2 = next 3 bytes
  B[n]   = b0 >> 4    R[n]   = b0 & 15    G[n]   = b1 >> 4
  B[n+1] = b1 & 15    R[n+1] = b2 >> 4    G[n+1] = b2 & 15
```
8-bit RGB for export = value*17. The CG palettes share entries 0..~11 with WAKU.GDT
(window frame colours) [V by comparison of headers].

### 2.3 Decode driver `0456:00D5` [V]
- Destination: four segments set by `gdt_set_planes(s0,s1,s2,s3)` (`0456:001D`), stored at
  CS:0015..001B. Callers pass either VRAM (A800,B000,B800,E000) or `g_gfxbuf`.
- `pos = y*80 + x` (CS:00D2), `pairs = width>>1` (CS:00D4).
- Columns are processed left to right; for each column (or column pair) the four planes
  are decoded in order 0,1,2,3, each from its own stream pointer (streams are consumed
  incrementally, column after column).
- 1-byte codec (flags & 0x4000): every 8-px column uses `gdt_col1` (`0456:38EA`).
- 2-byte codec: column pairs (16 px) use `gdt_col2` (`0456:0240`). If `pos` is odd, the
  first column is decoded with the 1-byte codec; if `pos+width` is odd, the last column
  is too. Exact order (`0456:0120..015B`):
  - pos even: pairs; then (width odd) one 1-byte column.
  - pos odd, width even: 1-byte column, pairs-1 pairs, 1-byte column.
  - pos odd, width odd: 1-byte column, pairs.
- Before the first 2-byte pair, each plane pointer is rounded up to an even **address**
  (`0456:3E64`). Data is laid out assuming the blob starts at an even address (checked: an
  odd base misparses all ADT frames), so in file terms: pad to an even offset from the blob
  start.
- A column program writes downward from the column top; `di` advances 80 (or 160/320)
  per written line. Offsets are 16-bit and wrap within the 64 KiB segment.
- Unwritten bytes (skips) keep whatever the destination held: this is how diff images
  are transparent.

### 2.4 1-byte column codec (`gdt_col1`, jump table `0456:3902`) [V]
Stream of commands, one column (1 byte wide) of one plane, terminated by `FF`.
`n` = count; "(n=0: next byte)" means the low bits give the count, or if they are zero the
count is the following byte. A count of 0 in an explicit count byte would loop 65536
times (`loop` with CX=0) — never produced by the encoder. "write v" = `dst[di]=v; di+=80`.

| Byte | Handler | Meaning |
|---|---|---|
| `00-1F` | 3B2A | n=b&1F (n=0: next byte); write 00h n times |
| `20-3F` | 3B4B | n=b&1F (0: next byte); write FFh n times |
| `40-5F` | 3D42 | n=b&1F (0: next byte); copy n bytes from **plane 0** at same offset |
| `60-7F` | 3D60 | same, from plane 1 |
| `80-9F` | 3D7E | same, from plane 2 |
| `A0-AF` | 3D9C | n=b&0F (0: next byte); copy from same plane, 16 lines up (di-0x500), byte by byte (overlap allowed) |
| `B0-BF` | 3DC4 | same, 8 lines up (di-0x280) |
| `C0-CF` | 3DEC | same, 4 lines up (di-0x140) |
| `D0-DF` | 3E14 | same, 2 lines up (di-0xA0) |
| `E0-EF` | 3E3C | same, 2 bytes left (di-2, i.e. 16 px left) |
| `F0 n` | 3B0E | n literal bytes follow |
| `F1-F8` | 3B02 | b-F0 (1..8) literal bytes follow |
| `F9 n` | 3B1A | skip n lines (di += 80n) — leaves destination untouched |
| `FA n v` | 3B6D | write v n times |
| `FB n` | 3B7C | n<80h: write NOT plane0 n times; else NOT plane1, (n&7F) times |
| `FC n` | 3BD7 | n<80h: write NOT plane2 n times; else read q: 2-line pattern (dup(q&F), dup(q>>4)) repeated n&7F times |
| `FD n` | 3C07 | n<80h: read a,b: 2-line pattern (a,b) x n. 80h<=n<C0h: read q: lo=dup(q&F), hi=dup(q>>4): 4-line pattern (lo, hi, ror2(lo), ror2(hi)) x (n&3F). n>=C0h: read q,r: 4-line pattern (dup(q),dup(q>>4),dup(r),dup(r>>4)) x (n&3F) |
| `FE n` | 3CA7 | n<40h: read 4 bytes: 4-line pattern x n. 40-7F: write plane0 AND plane1, (n&3F) times; 80-BF: plane0 AND plane2; C0-FF: plane1 AND plane2 |
| `FF` | 3E63 | end of column |

`dup(x) = (x&15) | (x&15)<<4` (a 4-pixel pattern repeated); `ror2` = rotate byte right by 2.
"plane k at same offset" reads the destination buffer of plane k (already decoded for this
column because planes go 0→3; a plane may reference only lower planes in practice).

Pseudo-code:
```
def col1(src, si, planes, p, di):
    while (b := src[si++]) != 0xFF:
        if b < 0x40: n = b&31 or src[si++]; fill(0 if b<0x20 else 0xFF, n)
        elif b < 0xA0: n = b&31 or src[si++]; for n: put(planes[(b-0x40)>>5][di])
        elif b < 0xF0: n = b&15 or src[si++]; off={A:0x500,B:0x280,C:0x140,D:0xA0,E:2}[b>>4]
                       for n: put(planes[p][di-off])
        elif b <= 0xF8: n = (b==0xF0) ? src[si++] : b-0xF0; copy n literal bytes
        ... (F9..FE as in the table)
```
(`re-tools/gdt.py:_column1` is the reference implementation.)

### 2.5 2-byte column-pair codec (`gdt_col2`, jump table `0456:0258`) [V]
Used by ADT frames only. Each token is a **word** read with `lodsw`: `a` = first byte
(parameter), `op` = second byte (dispatch). Writes are 16-bit words (left byte = column
x, right byte = column x+1), one per line ("put w"). All bulk work is done by jumping into
unrolled instruction blocks (e.g. `0456:1003`, `1402`, `187E`, `1DAC`, `22AA`, `2BA8`,
`38A0`) at `end - n*unit`, so a count of 0 does nothing.

| op | Handler | Meaning |
|---|---|---|
| `00-7F` | 04B1 | 2-line pattern (dup(a&F)x2, dup(a>>4)x2) repeated `op` times |
| `80-BF` | 051A | lo=dup(a&F), hi=dup(a>>4): 4-line pattern (lo,hi,ror2(lo),ror2(hi)) (each byte doubled into a word) x (op&3F) |
| `C0-CF` | 047A | fill `a` lines with word dup(op&F)*0x0101 (C0 = 0000h, CF = FFFFh) |
| `D0-D1` | 0458 | n = a + 256*(op&1) literal words follow, one per line |
| `D2` | 046B | skip `a` lines |
| `D3-F2` | 0626.. | copy `a` words from the same plane at di+off, line by line (see table below) |
| `F3-F5` | 0A3B.. | copy `a` words from plane 0/1/2 at same offset |
| `F6-F8` | 0A98.. | write NOT plane 0/1/2, `a` lines |
| `F9-FB` | 0AE9.. | write plane0&plane1 / plane0&plane2 / plane1&plane2, `a` lines |
| `FC` | 049B | read word w; fill `a` lines with w |
| `FD` | 04DE | a<80h: read bytes p,q: 2-line pattern (pp, qq) x a. else read words w0,w1: 2-line pattern x (a&7F) |
| `FE` | 0556 | 4-line patterns, count a&3F. a<40h: read A,x,C,D: lines (AA),(DD),(D, swapnib(A)),(((D&F)<<4)|C>>4, ((C&F)<<4)|D>>4) — odd register reuse in the original, never used by data. 40-7F: read p,q: dup(p&F),dup(p>>4),dup(q&F),dup(q>>4) (bytes doubled). 80-BF: read 4 bytes, each doubled into a word. C0-FF: read 4 words |
| `FF` | 0B52 | end of column pair (word `xx FF`) |

Relative copy offsets (op → di offset; dy lines, dx bytes):

| op | off | (dy,dx) | op | off | (dy,dx) | op | off | (dy,dx) | op | off | (dy,dx) |
|---|---|---|---|---|---|---|---|---|---|---|---|
| D3 | -500h | (-16,0) | DB | -0A2h | (-2,-2) | E3 | -144h | (-4,-4) | EB | -146h | (-4,-6) |
| D4 | -3C0h | (-12,0) | DC | -052h | (-1,-2) | E4 | -0A4h | (-2,-4) | EC | -0A6h | (-2,-6) |
| D5 | -280h | (-8,0) | DD | -2 | (0,-2) | E5 | -054h | (-1,-4) | ED | -056h | (-1,-6) |
| D6 | -140h | (-4,0) | DE | +04Eh | (+1,-2) | E6 | -4 | (0,-4) | EE | -6 | (0,-6) |
| D7 | -0A0h | (-2,0) | DF | +09Eh | (+2,-2) | E7 | +04Ch | (+1,-4) | EF | +04Ah | (+1,-6) |
| D8 | -050h | (-1,0) | E0 | +13Eh | (+4,-2) | E8 | +09Ch | (+2,-4) | F0 | +09Ah | (+2,-6) |
| D9 | -282h | (-8,-2) | E1 | +27Eh | (+8,-2) | E9 | +13Ch | (+4,-4) | F1 | +13Ah | (+4,-6) |
| DA | -142h | (-4,-2) | E2 | -284h | (-8,-4) | EA | +27Ch | (+8,-4) | F2 | -8 | (0,-8) |

(dx in bytes: -2 = previous column pair.)

### 2.6 Image kinds and how the engine uses them
(`resources/images/index.tsv` lists all; usage from script scan of `*.U.CC`.)

| Kind | Files | Rect | Loaded by | Palette |
|---|---|---|---|---|
| CG / background | `0010`…`0130`, `1010`…`1070` (rooms), `RYO01`, `MAI01`… (event CGs), `ALL`, `R&M01`… | 40,4 560x296 (inside the window frame) | G_LOAD (op 0x0C) + G_DISP (0x0D) | own, set at load |
| Window frame | `WAKU` (16,0 608x397) | full frame + text box | G_LOAD+G_DISP | own |
| Empty CG area | `WAKU2` (40,4 560x296; dark "XENON" pattern) | CG area | `fx_waku2_close` / op 0x11 | not applied |
| Diff (sabun), no palette | `*B`,`*C`,`*D`,`*E` with flags 4F | sub-rect | op 0x16 (`gdt_load_diff`) | CG's |
| Diff with palette | `BI06B..E`, `FA09B`, `TO03B`, `MAI02C` (CF) | sub-rect | op 0x16 | own (set) |
| Bust portrait | `*_BAST`, `0040B`, `0110`, `0120`, `RA&TO03B/C` | right/left half | ops 0x34+0x35: decode to page 1, merge with colour 8 transparent | room's (own palette ignored) |
| Bust diff | `*_BA_B` (4F, tiny: mouth/eyes) | few bytes | op 0x16 after bust merge | room's |
| Blink strip | `*_M`, `SAYA06BM`, `BAST_M01/02` | 3 frames stacked vertically | op 0x28 into buffers + op 0x2E animator | not applied |
| System/opening | `ROGO`, `0100B`, `0100BB`, `XENON2`, `MENU1/2`, `MUSIC`, `ST00-09` | various | seg 0E31 / 0ABA code (engine.md §3, §4); `MUSIC` by M.CC | own |

Diff images are deltas against a specific previous buffer state: skip ops leave pixels,
copy ops read neighbouring/previous pixels and other planes, and partial-plane skips keep
individual bit-planes. Several C/D/E diffs are applied on top of B (e.g. RYO02 →
RYO02B → RYO02C; BI06 → B → C → D → E); applying RYO02C directly to RYO02 gives garbage
[V]. `gdt.py --composite-dir` reproduces the chains (`DIFF_CHAIN`).

Standalone PNG export (`resources/images/<NAME>.png`): cropped to the header rectangle,
indexed, own palette or the palette of the CG they are shown on (`palette` column of
index.tsv). Pixels not written by the image are transparent; pixels that depend on the
prior screen in only some planes are rendered as if decoded over colour 0 and counted in
`notes`. For bust portraits colour 8 is marked transparent (engine keying). The
`composite/` PNGs show what the engine actually displays.

---------------------------------------------------------------------------------------

## 3. Rendering pipeline and entry points

### 3.1 Loading [V]
`gdt_read_file(name)` `0C2A:0388`: `sprintf("%s.gdt")`, open via `0843:0384` (core file
open, may handle disk swap), `malloc(filelength)`, `fread` whole file. Callers free it after
decoding.

| Address | Name | Args | Behaviour |
|---|---|---|---|
| `0C2A:0315` | gdt_load (**G_LOAD**) | far char *name | decode into buffers; palette → DS:13EC (`g_gdt_palette`); display rect ← image rect (DS:141C off=y*80+x, 141E width bytes, 1420 height); **sets the hardware palette immediately** (`0DCA:0258`); `strcpy(DS:1422, name)` (saved in save games). Empty name → returns 1, no-op |
| `0C2A:00DB` | gdt_load_buf | far char *name, int (unused) | decode into buffers; no palette, rect unchanged; strcpy name → DS:1422 |
| `0C2A:04AB` | gdt_load_diff | far char *name | decode into buffers; rect unchanged; if byte[0F]==CFh unpack palette → 13EC and set it; name not recorded |
| `0C2A:0142` | gdt_load_page1 | far char *name | access page 1; decode straight into VRAM page 1; rect → DS:1431/1433/1435; access page 0 |
| `0C2A:01F9` | gfx_merge_page1 | – | for the DS:1431 rect: per byte, `keep = ~(p0|p1|p2) & p3` (colour-8 pixels of page 1); `buf[k] = (buf[k] & keep) | page1[k]` for planes 0-2, plane 3 `|= p3 & ~keep`. Reads VRAM through DS:0956 with access page 1 |
| `0C2A:0437` | gdt_decode_buf | far *blob, int keep | (internal) decode into buffers; keep==0 → palette+rect update |
| `0C2A:0199` | gdt_decode_vram | far *blob, int keep | (internal) decode into VRAM planes |
| `0C2A:05B2` | gfx_clear_buffers | – | memset buffers to 0 |
| `0C2A:000E` / `009B` | gfx_alloc_buffers / gfx_free_buffers | – | startup / shutdown |
| `0C2A:0008` | gfx_get_state_ptr | – | returns far ptr DS:1422 |

### 3.2 Display and effects (buffers → VRAM of the current access page) [V]
All copy whole bytes of all 4 planes (`gfx_copy_row` `0C2A:05E6`, rep movsw).
Each "step" waits one VSYNC via `wait_vsync(1)` (`0DCA:0012`); holding **CTRL** makes
every wait return immediately (`0DCA:0004` tests BIOS 0000:053A bit 4) [V].

| Address | Name | Behaviour |
|---|---|---|
| `0C2A:0649` | gfx_show (**G_DISP**) | copy display rect rows top→bottom, no waits |
| `0C2A:0690` | fx_blind | display rect, 8-line bands: pass 1 top→bottom copies band rows 0,1,4,5; pass 2 bottom→top rows 7,6,3,2; 1 vsync per band per pass (2*h/8 vsyncs) |
| `0C2A:075E` | fx_center_open | from the middle row (h/2-1) outwards alternately down/up, 8 rows per vsync, h/8 steps |
| `0C2A:07F0` | fx_waku2_close | `gdt_load_buf("waku2")`, then over the fixed CG rect (off 0x145 = x 40,y 4; 70 bytes; 296 lines) copies rows from top and bottom towards the centre, 8 rows per vsync (37 steps) |
| `0C2A:0962` | fx_dissolve | forces display rect = whole screen (0, 80, 400); 16 steps x 1 vsync; in step s, for every 8x8 cell copies one 4x1 piece: t = order[s] (DS:0966 = 0,10,4,14,8,2,12,6,13,7,1,11,5,15,9,3); row t&7, left nibble if t<8 else right nibble (`0C2A:087B`). Opening only |
| `0DCA:04E3` | gvram_clear | GRCG clear of current access page to colour 0 |

### 3.3 Palette [V]
Hardware palette shadow: DS:1438 (16 x R,G,B). `g_gdt_palette` DS:13EC holds the palette of
the last image that carried one (target of fade-in).

| Address | Name | Args | Behaviour |
|---|---|---|---|
| `0DCA:01B9` | pal_set | i, r, g, b | one entry + shadow |
| `0DCA:0200` | pal_set_rgb | i, {r,g,b} by value (4-byte struct) | one entry + shadow |
| `0DCA:0258` | pal_set_all | far *pal48 | 16 entries |
| `0DCA:02E0` | pal_get_all | far *out48 | copy shadow |
| `0DCA:02F7` | pal_fade_to | pal48 by value (48 bytes on stack), int delay | loop: wait `delay` vsyncs; for each entry and gun move current value one step (±1) toward target; count entries already equal; write all 16; stop when all 16 were equal at the start of a step (≤16 steps). delay 0 = no waiting |
| `0DCA:0445` | pal_fade_black | int delay | pal_fade_to(all 0) |
| `0DCA:0494` | pal_fade_white | int delay | pal_fade_to(all 15) |
| `0DCA:023A`, `0288` | pal_get_rgb, pal_set_all_but8 | | no callers |

### 3.4 Pages / layers [V]
| Address | Name | Args | Behaviour |
|---|---|---|---|
| `0DCA:0150` | set_access_page | int p | out A6h; DS:1468 |
| `0DCA:0162` | set_display_page | int p | out A4h; DS:146A |
| `0DCA:00BF` / `00D9` | graph_on / graph_off | – | INT 18h AH=40h / 41h |
| `0DCA:008B` / `00A5` | text_on / off | – | INT 18h AH=0Ch / 0Dh (no callers) |
| `0DCA:0012` | wait_vsync | int n | see 3.2 |

The engine is single-buffered for normal scenes (everything is drawn on page 0 while page 0
is displayed). Page 1 is used (a) as scratch for bust decoding (`gdt_load_page1`), (b) for
ADT double buffering, and (c) by scripts that draw the frame/CG on both pages
(`43 01 … 0D … 43 00 … 0D`) before ADT playback.

### 3.5 VSYNC interrupt and the blink animator [V]
`vsync_irq_install` `0ECB:004F` hooks INT 0Ah (IRQ2, CRT vsync), unmasks IRQ2 (port 02h) and
arms the CRT interrupt (`out 64h,0`). ISR `0ECB:0001`: EOI, `++DS:0AFE` (u32), calls far
ptr DS:168A if non-null (re-entrancy flag DS:0B02), re-arms. `vsync_set_hook(off,seg)`
`0ECB:00BD`; `vsync_irq_remove` `0ECB:0090`.

Blink animator (`blink_start(sx,sy,dx,dy,w,h)` `0ED8:036F`; x and w in bytes, y/h in lines;
callback `blink_tick` `0ED8:0270`; `blink_stop` `0ED8:03CB` resets frame=0, step=0):
```
tick: if T[step] > counter: counter++; return
      copy rows (sy + frame*h) .. +h-1, bytes sx..sx+w-1 of all 4 buffers -> VRAM (dy.., dx..)
      counter = 0; step++; if step > 13: step = 2
      frame += dir ? +1 : -1;  if frame > 2: dir=0, frame=1;  if frame < 0: dir=1, frame=1
T = DS:0B14 = [0,1,1,6,3,300,1,6,3,3,1,6,3,300]
```
Strip frame 0 = eyes open, 1 = half, 2 = closed [V visually]. Resulting display times
(vsyncs) from start: f0 2, f1 2, f2 7, f1 4, f0 301, then a repeating 12-copy period:
blink (f1 2, f2 7, f1 4), f0 4, blink, f0 301, … (`resources/anim/blink/blink_timing.tsv`).
The strip is first loaded into the off-screen buffers with op 0x28 (`gdt_load_buf`, which
overwrites that buffer area, not the screen) at its own header position; for CG strips
src == dst and frame 0 is placed exactly over the eyes. `BAST_M01` (x 0..64 bytes) holds 5
bust eye strips 13 bytes wide (src x = 0,13,26,39,52), `BAST_M02` likewise for the 10x0
busts; dst depends on bust placement.

### 3.6 ADT animations [V]
File layout (`adt_load` `0ED8:0078`; setvbuf 0x2800):

| Off | Size | Field | Global |
|---|---|---|---|
| 000 | 3 | "ADT" (not checked) | DS:0B04 |
| 003 | u16 | frame count N (≤120) | DS:1792 |
| 005 | 120 x u16 | frame blob sizes | DS:1794 |
| 0F5 | u16 | loop flag | DS:1884 |
| 0F7 | u16 | loop start | DS:0B08 |
| 0F9 | 120 x u16 | delay[i] in vsyncs before frame i | DS:1886 |
| 1E9 | … | N DA1 blobs (each malloc'd; ptrs DS:1976) | |

Each frame is a complete DA1 image with flags 0x0F00 (2-byte codec, no palette); a
640x400 frame of only `D2` skips is a pure pause. `adt_play` `0ED8:0001`:
```
page = 1
loop: wait delay[cur] vsyncs (cur may equal N: delay[N], 0 in all files)
      if cur >= N: if !loop_flag: break; cur = loop_start - 1     # loops forever
      out A6h,page; gdt_decode(frame[cur]) into VRAM; out A4h,page; page ^= 1; cur++
cur = 0
```
So frames alternate between pages and draw over that page's content from two frames
earlier; both pages must hold the base image. No file uses the loop flag.

| ADT | Frames | Base | Content |
|---|---|---|---|
| 0100B | 29, delay 1 | 0100B.GDT (opening, page 0+1) | "XENON" logo zoom over the ship |
| FA03 | 8, 8-9 | WAKU+FA03 | |
| FA08 | 34, 6-9 | WAKU+FA08 | |
| FA09 | 4 (incl. 1 pause) | WAKU+FA09 | wink |
| MEG05 | 6, 7-11 | WAKU+MEG05 | |
| RA03 | 8 (3 pauses) | WAKU+RA03 | |

Exports: `resources/anim/<NAME>.gif` and `<NAME>/NN.png` + `frames.tsv`; blink GIFs in
`resources/anim/blink/`.

### 3.7 Screen layout [V from image rects, I for the text box]
- Window frame WAKU at (16,0) 608x397; CG area (40,4) 560x296 (all full CGs and WAKU2);
  text box below (WAKU lower panel ≈ y 305-390); the text printer (seg 0B7D) starts at
  byte column 10 (x=80), y=313 (script op 0x38 sets it).
- Busts occupy the right or left part of the CG area (e.g. RYO_BAST 48,8 360x288).
- Opening/title (seg 0E31): ROGO at (160,2), 0100B at (24,0) 592x400 full height, XENON2
  title (128,268); menus MENU1/2 (184,137) 280x183; staff roll ST00-09 centred.

---------------------------------------------------------------------------------------

## 4. Script interpreter → graphics entry points

For the script-VM analyst: handlers in seg 088D (from the opcode table at 088D:094C/09B6)
and the graphics routine they reach. Argument parsing is the VM's business; listed here
only to map calls.

| VM handler | Calls | Args passed |
|---|---|---|
| 088D:1710 | `gdt_load` 0C2A:0315 | string |
| 088D:17E1 | `gfx_show` 0C2A:0649 | – |
| 088D:179C | `gdt_load_diff` 0C2A:04AB | string |
| 088D:1755 | `gdt_load_buf` 0C2A:00DB(name,1) | string |
| 088D:1AE6 | `gdt_load_buf` 0C2A:00DB(name,0) | string (identical effect) |
| 088D:1B2B | `gdt_load_page1` 0C2A:0142 | string |
| 088D:1B6E | `gfx_merge_page1` 0C2A:01F9 | – |
| 088D:1901 | int≠0: `fx_waku2_close` 0C2A:07F0; 0: `gvram_clear` 0DCA:04E3; then DS:1422[0]=0 | int |
| 088D:18B3 | int≠0: `fx_center_open` 0C2A:075E; 0: `fx_blind` 0C2A:0690 | int |
| 088D:1F30 | `gfx_clear_buffers` 0C2A:05B2 | – |
| 088D:17F3 / 1805 | `graph_off` / `graph_on` | – |
| 088D:1817 / 1865 | `set_access_page` / `set_display_page` (arg≠0 → 1) | int |
| 088D:1B80 | sets DS:13EC[i] and `pal_set_rgb(i,{r,g,b})` | 4 ints i,r,g,b |
| 088D:1F87 | `pal_fade_to(g_gdt_palette, d)` (fade in) | optional int d (default 1) |
| 088D:1FD5 | `pal_fade_black(d)` | optional int (default 1) |
| 088D:2014 | `pal_fade_white(d)` | optional int (default 1) |
| 088D:1F42 | `wait_vsync(n)` | int |
| 088D:1999 / 19EE / 19DC | `adt_load(name)` / `adt_play()` / `adt_free()` | string / – / – |
| 088D:1A00 | `blink_start(sx,sy,dx,dy,w,h)` | 6 ints |
| 088D:1A6B | `blink_stop()` | – |
| 088D:2053 | `0E31:04AE` opening/title sequence | – |
| 0x47 handler 088D:08FD | `0E31:05D2` staff roll | – |
| 088D:16C0 (load game) | `gdt_load(saved name)` + `gfx_show` | name from save |

Other callers: the title/omake code in seg 0ABA uses `gdt_load`+`gfx_show` for MENU1
(0ABA:027B, 034E, 0373) and `gdt_load_diff`+`gfx_show` for MENU2 (0ABA:010B) (MUSIC and ALL are
loaded by the scripts M.CC and K.CC); the sequences of seg 0E31 (engine.md §4); startup `0E1A:007F` calls `video_init`, clears both pages, and
`gfx_alloc_buffers`; shutdown `0E1A:011C` frees buffers/ADT, stops the blink animator and
clears both pages.

---------------------------------------------------------------------------------------

## 5. Open questions
- ~~INT 18h AH=4Ah and port 6Ah values 83h/41h in `video_init`~~ — resolved from documentation
  and emulator sources (platform.md §6.1); not checked on real hardware.
- Flag nibble 0x0F00 is never tested; assumed "4 planes".
- The 2-byte codec op `FE` with a<40h builds an odd pattern (register reuse); it never
  occurs in the data, so its intended meaning is unknown — reproduced literally.
- ~~`malloc` pointer alignment~~ — resolved: the Borland far heap returns `seg:0004` (4-byte block
  header at `seg:0000`; `farmalloc` 0000:1D6B/`CreateHeap` 0000:1C42 `mov ax,4`), so every loaded
  file starts at an even address, as the decoder's even-address rule assumes (functions.md).
- ~~Opening/staff code in seg 0E31~~ — resolved: the reveal helpers (`seq_slide_up_iplane`
  0E31:002D, `seq_slide_up` 0110, `logo_show_mark` 01D5, `logo_flash_white` 02E0,
  `logo_show_top` 038F) and the three timelines are documented in engine.md §4.
- Which bust the `BAST_M01/M02` strips belong to per x offset was not mapped individually.

## 6. Notes for other areas
- **Core/save**: DS:1422 (40 bytes) starts with the current CG name; on load `088D:16C0`
  calls `gdt_load` + `gfx_show` with it. `fx_waku2_close`/op 0x11 clears it.
  DS:13EC (`g_gdt_palette` in names.tsv) is the 16x(R,G,B) palette of the last
  loaded GDT; it is passed by value to `pal_fade_to`.
- **Script VM**: see section 4. Fades/effects take an optional int with default 1.
- **Text**: text and the menu cursor are drawn by seg 0B7D; the window frame is just
  WAKU.GDT; colours 0..~11 are common to WAKU and the CG palettes.
- **Sound**: seg 0C2A from 0A1A on is the embedded FM driver (OPN port 188h), not graphics.

---------------------------------------------------------------------------------------

## 7. Requirements for a faithful renderer

This section is derived from the sections above and from platform.md / font.md. It states what a
reimplementation must reproduce to match the original; it is not a design. It is the second
porting-related exception to the "original only" rule, after sound.md §6.1.

### 7.1 Output
* **640×400, 16 colours** at once from a 4096-colour palette (4 bits per gun; 8-bit = v·17).
* **Pixel aspect.** On the 4:3 CRTs of the time each pixel was 1.2× taller than wide, so the 4:3
  equivalent is 640×480 (or an integer multiple, e.g. 1280×960). Showing 640×400 at 1:1 is also
  common for PC-98 titles; either way scale nearest-neighbour.
* **Frame clock 56.42 Hz** (platform.md §4.1, §6.1). Every duration counts frames: `WAIT n`,
  fades, transitions, the one-glyph-per-frame typing, ADT delays and the blink animator. Run game
  logic on a fixed 56.42 Hz tick, independent of the host refresh rate. (On a PC-9821 set to 31 kHz
  the original runs at 70.10 Hz, 1.24× faster; 56.42 Hz is the reference.)

### 7.2 State to model (all small: about 300 KB)
| Item | Size | Reference |
|---|---|---|
| 2 VRAM pages × 4 bit-planes (B, R, G, I) × 80 bytes × 400 lines | 2 × 128 000 B | §1 |
| access-page and display-page selectors (independent) | 2 ints | §3.4 |
| off-screen buffer, same layout (4 × 0x7D10 bytes) | ≈128 KB | §1 |
| hardware palette (16 × RGB4) + fade target `g_gdt_palette` | 2 × 48 B | §3.3 |
| graphics-GDC scroll start line (`G_SCROLL`, wraps) | int | opcodes.md op 32h |
| graphics layer on/off | flag | §3.4 |
| text layer (80×25 cells, 16 px rows, with attributes) | – | §7.5 |

### 7.3 Keep the planar model and indexed colour
* **Planar.** The GDT/ADT codecs (§2.4, §2.5) copy from other planes and from rows above, write
  NOT/AND combinations of planes, and use skips that leave pixels untouched, sometimes in only some
  planes. Diff images are deltas against the current buffer state and chain (§2.6: RYO02 → B → C).
  Bust merging keys colour 8 with plane logic (`gfx_merge_page1`, §3.1). The simplest correct
  approach is byte arrays per plane plus the reference decoder (`re-tools/gdt.py`, byte-exact against
  the original on all files).
* **Indexed until presentation.** Fades, `PALETTE` and the opening's hidden colours (engine.md §4)
  only change palette entries, and the whole screen changes with them. Convert planes → index →
  RGB when presenting a frame (lookup table or shader). Pre-converting images to RGBA breaks this.
* **Single-buffered scenes.** Normal scenes draw on the displayed page, so partial updates are
  visible as they happen; wipes depend on this (§3.4). ADT playback alternates pages and draws
  each frame over that page's content from two frames earlier (§3.6).

### 7.4 Operations
* Row copies buffer → VRAM in whole bytes (8-pixel columns): G_DISP, blind, centre-open, `waku2`
  close, 16-step dissolve (§3.2). One step per frame.
* Palette fades: every gun moves 1 step per `delay` frames, ≤ 15 steps (§3.3).
* `G_SCROLL`: display start line with wrap-around. Apply as a row rotation when presenting, not by
  moving VRAM (opcodes.md op 32h).
* Blink animator: driven by the VSYNC interrupt, so it keeps ticking during script waits, typing
  and menus until `blink_stop` (§3.5). In a port it must run from the frame tick, not the script.
* GRCG page clear (`gvram_clear`). The EGC and GDC drawing commands are not used (§1,
  platform.md §6.1).

### 7.5 Text
* **Graphic-VRAM text** (all dialogue, menus, title cards): kanji-ROM glyphs drawn as opaque cells
  into the access page (font.md §2): 16×16 full-width with 1-px emboldening, 8×16 half-width,
  32×32 double-size, user character 7621h (font.md §4). The game files contain no font. A port
  needs a 16-dot JIS X 0208 bitmap font plus an 8×16 ANK font, from a ROM dump (licensing issue)
  or a free 16-dot font.
* **Text layer** (text VRAM, over graphics): used only for the blinking ▼ page prompt at row 24
  col 73 (`ESC[5;37m` + user character) and the error box (`ESC[7;31m`, centred on row 10)
  (engine.md §5.4, §6). It needs cells with colour, blink and reverse attributes. It is aligned with
  graphics with no 1-pixel shift, because `video_init` selects plasma/LCD mode (platform.md §6.1).
* Typing pace: one frame per full-width glyph with KANA lock off, none with it on. Holding CTRL
  skips every frame wait (platform.md §4.1, font.md §2.1).

### 7.6 Not needed
Sprites, scaling, rotation, alpha blending, more than 16 colours, hardware line drawing, and a mouse
cursor (the mouse only emulates cursor keys / OK / cancel, platform.md §3).
