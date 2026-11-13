# AGS.EXE script opcode reference

Companion to [scripts.md](scripts.md). Dispatch: value table **088D:094C** (53 words) and handler
table **088D:09B6** inside `cc_exec` (088D:05FC). "Handler" is the dispatch target, "Function" the
088D helper it calls, "Callee" the engine routine outside 088D. Mnemonics in **bold capitals** are
original keywords recovered from the engine's error messages (DS:02CF..0537); the others are my
names. Operands: `int` = `FB lo hi` (signed 16-bit), `[int]` = optional `FB` (default 1 when the next
token is not `FB`), `str` = `FD len text 00`, `flag` = `FE lo hi`, `label` = `FF lo hi`.
`n` = number of occurrences in the 40 shipped scripts.

Graphics/sound/keyboard callees are only described shallowly here (what they are for and which
arguments they get); their internals are documented by the respective analyses. All "verified"
statements about operand grammar and argument passing were read in the handlers; callee purposes
marked (inferred) are from a quick read.

## Summary table

| Byte | Mnemonic | Operands | Handler | Function | Callee(args) | n | Effect |
|-----:|----------|----------|---------|----------|--------------|--:|--------|
| 01 | **PAGE** (struct) | int | - | cc_run_page 088D:035A | - | 295 | page header, page number |
| 02 | **PEND** (struct) | - | 08E6 | - | - | 295 | end of page; terminator check |
| 03 | CLEAR_LOWER | - | 078A | 22C0 | clear_lower_screen 0B7D:0852() | 0 | clear graphics rows y >= 0x128 |
| 04 | **SEN** (struct) | options | - | cc_build_options 088D:2092 | - | 200 | start of option list |
| 05 | **SENEND** (struct) | - | - | - | - | 200 | end of option list |
| 06 | **JMP** | int page | 06E4 | cc_find_page 0ECD | - | 532 | DS:1292=1, continue at PAGE n |
| 07 | FILE | str script | 071A | - | strcpy(DS:137E,name) | 39 | DS:1290=1, chain to name.cc |
| 08 | **END** (struct) | - | 08E6 | - | - | 524 | end of case (label) block |
| 0A | THEN (struct) | - | - | - | - | 36 | separates IF condition and body |
| 0B | **END** (struct) | - | 08E6 | - | - | 36 | end of IF body |
| 0C | **G_LOAD** | str | 07B5 | 1710 | 0C2A:0315(name) | 606 | load name.gdt to off-screen planes, set palette |
| 0D | G_DISP | - | 07C7 | 17E1 | 0C2A:0649() | 383 | copy off-screen image rect to VRAM |
| 0E | **MUSIC_LOAD** | str | 0857 | 1DA1 | 0B2C:01C4(name) | 191 | load name.M into PMD |
| 0F | MUSIC_PLAY | - | 0860 | 1DE6 | 0B2C:02E5() | 191 | PMD INT 60h AH=00 (start) |
| 10 | MUSIC_STOP | - | 0869 | 1DF8 | 0B2C:033C() | 74 | PMD AH=01 (stop) |
| 11 | WINDOW | int | 07A3 | 1901 | n?fx_waku2_close 0C2A:07F0():gvram_clear 0DCA:04E3() | 333 | 1: load `waku2` and shutter-close the CG area; 0: clear graphics page; both clear DS:1422 |
| 12 | `=` (struct) | - | - | - | - | 361 | in assignments/conditions |
| 13 | **IF** (struct) | cond | 063C | cc_eval_cond 1104 | - | 146 | IF statement / option condition |
| 14 | `)` (struct) | - | - | - | - | 146 | end of condition |
| 15 | `&&` (struct) | - | - | - | - | 87 | AND in conditions |
| 16 | **G_SABUN_LOAD** | str | 07BE | 179C | 0C2A:04AB(name) | 132 | load difference image name.gdt + palette |
| 19 | SAVE | str | 0778 | op_save 126A | - | 116 | write name.dat |
| 1B | SEN marker (struct) | - | - | - | - | 200 | terminator before 04 |
| 1C | LOAD | str | 076F | op_load 1441 | - | 4 | read name.dat, resume |
| 1D | DISK | str | 0792 | op_disk_change 0F9C | - | 33 | floppy only: request disk letter |
| 1F | EXIT | - | 090C | - | - | 8 | DS:13A6=1: leave interpreter |
| 20 | WAIT | int | 08AE | 1F42 | wait_vsync 0DCA:0012(n) | 777 | wait n vsyncs (ends early while CTRL is held) |
| 21 | FADEIN | [int] | 08B6 | 1F87 | 0DCA:02F7(pal DS:13EC by value, n) | 377 | fade to target palette |
| 22 | FADEOUT | [int] | 08BE | 1FD5 | 0DCA:0445(n) | 590 | fade to black |
| 23 | G_CLEAR | - | 08C6 | 1F30 | 0C2A:05B2() | 0 | zero the 4 off-screen planes |
| 24 | G_EFFECT | int | 079A | 18B3 | n?fx_center_open 0C2A:075E():fx_blind 0C2A:0690() | 308 | show off-screen image with a wipe (1 = from centre, 0 = blinds) |
| 26 | GRAPH_OFF | - | 07D0 | 17F3 | 0DCA:00D9() | 286 | INT 18h AH=41h graphics display stop |
| 27 | GRAPH_ON | - | 07D9 | 1805 | 0DCA:00BF() | 285 | INT 18h AH=40h graphics display start |
| 28 | G_LOAD_OVL | str | 07AC | 1755 | gdt_load_buf 0C2A:00DB(name,1) | 158 | decode name.gdt into the buffers, no palette / rect update |
| 29 | OPENING | [int] | 0914 | 2053 | 0E31:04AE(n) | 2 | canned sequence (0100b image/ADT, music xen_03a/xen_03b; n=1 copyright strip 0100bb, else title xenon2) |
| 2A | ADT_LOAD | str | 07FD | 1999 | 0ED8:0078(name) | 11 | load name.adt |
| 2B | ADT_FREE | - | 07F4 | 19DC | 0ED8:0217() | 11 | free ADT |
| 2D | ADT_PLAY | - | 0806 | 19EE | 0ED8:0001() | 11 | play loaded ADT |
| 2E | ANIM_START | int sx,sy,dx,dy,w,h | 080F | 1A00 | blink_start 0ED8:036F(sx,sy,dx,dy,w,h) | 167 | start vsync-driven eye-blink/mouth strip animation |
| 2F | ANIM_STOP | - | 0818 | 1A6B | 0ED8:03CB() | 160 | stop it |
| 30 | MUSIC_FADE | int | 0896 | 1EEB | 0B2C:0386(n) | 184 | PMD AH=02 AL=n fade out |
| 31 | MUSIC_FADEWAIT | - | 089F | - | 0B2C:0427() | 0 | fade (25), wait until done, stop |
| 32 | G_SCROLL | int | 08CE | 1954 | 0B7D:0A68(n) | 262 | graphics GDC SCROLL start line (shake) |
| 33 | G_LOAD_BUF | str | 0821 | 1AE6 | gdt_load_buf 0C2A:00DB(name,0) | 0 | identical to 28 (the int argument is ignored by the callee) |
| 34 | G_LOAD_PARTS | str | 082A | 1B2B | gdt_load_page1 0C2A:0142(name) | 121 | decode name.gdt straight into VRAM page 1 |
| 35 | G_PARTS_COPY | - | 0833 | 1B6E | gfx_merge_page1 0C2A:01F9() | 121 | merge that rect from page 1 into the buffers (colour 8 transparent) |
| 36 | PALETTE | int idx,r,g,b | 083C | 1B80 | 0DCA:0200(idx,{r,g,b}) | 179 | set palette entry (and target DS:13EC) |
| 37 | COMPLETE | int n | 0904 | op_set_progress 1C19 | - | 8 | XEN.GDT byte \|= 1<<(n-1) |
| 38 | TEXTPOS | int x,y,mx,my | 08D6 | 1A7D | - | 24 | DS:07D8,07DA,07E0,07E2 |
| 39 | TEXTCOLOR | int a,b | 084E | 1D44 | - | 61 | DS:07DC, DS:07DE |
| 3A | MUSIC_VOL | int | 088D | 1EA6 | music_set_fadevol 0B2C:0482(n) | 30 | PMD AH=19h AL=n (0 = full .. FFh = silent) |
| 40 | MSG_CLEAR | - | 0845 | 1D32 | text_window_clear 0B7D:0783() | 89 | clear the message window |
| 42 | FADEWHITE | [int] | 08A6 | 2014 | pal_fade_white 0DCA:0494(n) | 103 | fade every entry to (15,15,15) |
| 43 | VRAM_WPAGE | int | 07EB | 1817 | 0DCA:0150(n!=0) | 58 | out A6h: drawing page |
| 44 | VRAM_DPAGE | int | 07E2 | 1865 | 0DCA:0162(n!=0) | 21 | out A4h: displayed page |
| 45 | SE_PLAY | int | 0872 | 1E0A | 0B2C:043C(n) | 484 | PMD AH=0Ch AL=n FM sound effect |
| 46 | SE_STOP | - | 087B | 1E4F | 0B2C:0461() | 43 | PMD AH=0Dh |
| 47 | STAFFROLL | - | 08FD | - | 0E31:05D2() | 8 | canned ending / staff roll |
| 48 | MUSIC_WAIT | int | 0884 | 1E61 | music_wait_status 0B2C:04D8(n) | 3 | if an FM board is present: wait while PMD AH=0Ah status1 == n |
| FD | message | text | 0745 | - | text_print 0B7D:019B(str, !kbd_kana_locked()) | 15 676* | show message (*all FD strings incl. operands/options) |
| FE | flag assignment | flag 12 int | 08DE | cc_assign 108D | - | 128 | F<f> = (v != 0) |
| FB/FF | int / label | - | - | - | - | - | operands only |

Byte values `09 17 18 1A 1E 25 2C 3B-3F 41` and `49-FA` are neither dispatched nor used; the engine
would skip them as one-byte statements. `03 23 31 33` are implemented but unused by the shipped
scripts.

## Structure and control flow

### 01 PAGE `int` / 02 PEND (verified)
`cc_run_page` (088D:035A) requires `01` (else prints `debug1`, error 0) followed by `FB` (else
`debug2`, error 1); `DS:1294 = n`, `DS:1296 = -1`. `02` ends the page; execution falls through to the
next page in the file if no transfer happened. See scripts.md 4.2.

### 06 JMP `int` (verified, 088D:06E4)
Sets `DS:1292 = 1`, error 1 if the operand is not `FB`, `cc_find_page(n)` (088D:0ECD: first `01 FB n`
from offset 0, error 9 if none) and leaves `cc_exec` returning that offset.

### 07 FILE `str` (verified, 088D:071A)
`DS:1290 = 1`; `strcpy(DS:137E, text)`; leaves `cc_exec`. The name is given without extension
(lower case in all scripts: `s0101` ... `s0403`); `game_main` reloads via `cc_load`, which appends
`.cc` and searches all drives. Flags are preserved.

### 08 END / 0B END / 1B / 04 SEN / 05 SENEND / FF label
See scripts.md 4.3/4.4. `08` closes a case `FF id ...`, `0B` closes an IF body. `02 08 0B`
share handler 088D:08E6, reached only when the byte is not the current terminator (loop-head test 088D:0924) -> error 2.

### 13 IF `cond` 0A `stmts` 0B (verified, 088D:063C)
`cond` = `FE f 12 FB v` terms joined by `15`, closed by `14`; AND of `flag(f) == v`. True: run body,
then return from the enclosing block (the body must end in a transfer). False: skip to the matching
`0B` (`cc_skip_block` 088D:0DFB). Inside a SEN, `13 cond 14` before an option text makes the option
conditional (`cc_build_options` 088D:20CA).

### FE flag assignment `FE f 12 FB v` (verified, 088D:108D)
Error 8 if `12` or `FB` is missing or `f >= 0x800`. `v == 0` -> `flag_clear(f)` (088D:11F1), else
`flag_set(f)` (088D:11C3). Flags live at DS:00F2 (2048 bits).

### 1F EXIT (verified, 088D:090C)
`DS:13A6 = 1`, skip, leave `cc_exec`. The menu loop, `cc_run` and `game_main` see DS:13A6 and return
to the title screen (`game_main` 0ABA:03C8). Used at the end of every route (after STAFFROLL), in
K/L/M to return to the title, and never mid-story.

### 19 SAVE `str`, 1C LOAD `str`, 1D DISK `str`, 37 COMPLETE `int`
Handled by the core/save analysis (`op_save` 088D:126A, `op_load` 088D:1441, `op_disk_change`
088D:0F9C, `op_set_progress` 088D:1C19). Script-level facts (verified):
* SAVE: handler `126A(pos)` gets the offset of the opcode; then two `cc_skip_stmt` skip opcode and
  string. Names used: `save1`..`save4` (-> `save1.dat`...). SAVE does not transfer; scripts follow
  it with `SE_PLAY 9`, a confirmation message and `JMP`.
* LOAD: `1441(pos)` returns the new position and `cc_exec` returns it (088D:076F -> 0944). Only L.CC
  uses LOAD (`save1`..`save4`).
* DISK: string is a full-width drive/disk letter `Ｂ`..`Ｅ`; the handler reads it at `pos+3`; returns
  immediately in HDD mode (DS:0072 != 0). Opcode and string are then skipped.
* COMPLETE n: `FB` required (error 0); n = 1..7 sets bit n-1 of the XEN.GDT progress byte, written
  back immediately ("wb"). Other n: file rewritten unchanged.

## Message

### FD `text` (verified, 088D:0745)
`text_print(far text, flag)` with `flag = (kbd_kana_locked() == 0)`; `kbd_kana_locked` 0AF8:0108 is
INT 18h AH=02h (shift-key status) bit 2 = カナ lock; the flag becomes the per-glyph `wait_vsync` of
`kanji_draw` (KANA lock on = text appears instantly, font.md §2.1). The printer handles wrapping, page overflow, `▼` key wait and window clear,
and the in-string control codes `0A`, `04 d`, `05`, `0C`, `03..03`, `@x` (scripts.md 5.2). Afterwards
the statement is skipped normally (088D:076C -> 091C).

## Graphics

### 0C G_LOAD `str` (verified args; callee inferred)
`0C2A:0315(name)`: empty name -> nothing; `0C2A:0388` reads `name.gdt` (sprintf "%s.gdt", xfopen),
`0C2A:0437(buf, 0)` decodes into the four off-screen plane buffers (DS:13DE..13EA segments), stores
the GDT palette into DS:13EC and the image rectangle (DS:141C start offset, DS:141E, DS:1420 lines),
then sets the hardware palette from DS:13EC (`0DCA:0258`) and copies the name to DS:1422 (current
image name, saved in save files). Error 7 if no string.

### 0D G_DISP (verified args)
`0C2A:0649()`: if the plane buffers exist, copies DS:1420 lines of 80 bytes starting at DS:141C from
the off-screen buffers to VRAM (`0C2A:05E6` per line). Typical sequence: `G_LOAD bg`,
`G_SABUN_LOAD diff`, `G_DISP` or `G_EFFECT 1`.

### 16 G_SABUN_LOAD `str` (verified args)
`gdt_load_diff` 0C2A:04AB(name): decode into the buffers keeping the display rect; only if the GDT
flags byte is 0xCF the palette is unpacked (`0C2A:0514`) and set. Used for expression/pose variants: `1010` + `1010b`, `ra02` + `ra02b`.
Error 6 if no string. ("差分" = difference image.)

### 28 G_LOAD_OVL `str`, 33 G_LOAD_BUF `str` (verified)
Both call `gdt_load_buf` 0C2A:00DB(name, 1 / 0); the callee ignores its second argument and always
decodes with `gdt_decode_buf(blob, 1)` (0C2A:010A `push 1`): no palette unpack, no display-rect
update; name saved to DS:1422. 28 is used for the `*_m` (eye/mouth strip) images, `bast_m01/02`,
`0010_2`, `0040` drawn into the current buffers; 33 is unused. Errors 7 / 6.

### 34 G_LOAD_PARTS `str`, 35 G_PARTS_COPY (verified args)
`gdt_load_page1` 0C2A:0142(name): select VRAM access page 1, decode `name.gdt` straight into VRAM
(`gdt_decode_vram` 0C2A:0199), remember its rect in DS:1431..1435, back to page 0.
`gfx_merge_page1` 0C2A:01F9(): merge that rect from VRAM page 1 into the off-screen plane buffers,
colour 8 being transparent. Always used as a pair with the bust images, followed by the diff image
and a show: `G_LOAD_PARTS "ra_bast"  G_PARTS_COPY  G_SABUN_LOAD "ra_ba_b"  G_DISP` - composes a
character bust over the current background (verified sequence; purpose from the gfx analysis).

### 23 G_CLEAR (verified)
`0C2A:05B2()`: `memset(plane, 0, 0x7D10)` for each of the 4 allocated plane buffers. Unused.

### 24 G_EFFECT `int` (verified args)
n != 0 -> `fx_center_open` 0C2A:075E() (vertical wipe from the centre outwards, 8 rows per vsync),
n == 0 -> `fx_blind` 0C2A:0690() (interlaced blinds wipe). Used as the "show" step
instead of G_DISP (`G_EFFECT 1` 300x).

### 11 WINDOW `int` (verified args)
n != 0 -> `fx_waku2_close` 0C2A:07F0() (loads `waku2` into the buffers and shutter-wipes the CG area
40,4 560x296 closed from top and bottom), n == 0 -> `gvram_clear` 0DCA:04E3() (GRCG TDW fill: clears
the 4 planes of the access page). Both clear the remembered image name DS:1422[0]. `WINDOW 1` is the
usual "scene change: close the picture" step, `WINDOW 0` a black screen.

### 26 GRAPH_OFF / 27 GRAPH_ON (verified)
`0DCA:00D9` = INT 18h AH=41h (graphics display stop), `0DCA:00BF` = INT 18h AH=40h (start). Used to
draw a screen invisibly and then show it with a fade.

### 32 G_SCROLL `int` (verified args)
`0B7D:0A68(n)`: programs the graphics GDC (ports A2h/A0h, command 70h SCROLL) so that display starts
at line n (area 1 = 400-n lines, the rest wraps). Parameters (`gdc_scroll`, verified): SAD1 = n·40 (words),
LEN1 = (400−n)·16, SAD2 = 0, LEN2 = n·16; 40h is added to both high LEN bytes when BIOS byte
`0000:054D` bit 2 is set; `out 5Fh` is used as an I/O delay. Bit 2 = graphics GDC currently at 5 MHz, and
40h is the µPD7220 SCROLL **IM** bit, which the BIOS also sets in 5 MHz mode (where it programs PITCH 80
instead of 40). An IM area uses half the pitch, so the line stride is 40 words at either clock. That is why
SAD = n·40 works for both and only the IM bit has to follow the clock (**inferred**: NP2kai INT 18h AH=42h,
MAME upd7220 `update_graphics`; platform.md §6.1). Sequences like `G_SCROLL 40  WAIT 2  G_SCROLL 0
WAIT 2  G_SCROLL 360 ...` shake the screen.

### 43 VRAM_WPAGE `int`, 44 VRAM_DPAGE `int` (verified)
`0DCA:0150(n!=0)`: `out A6h` (VRAM page accessed by the CPU), stored in DS:1468;
`0DCA:0162(n!=0)`: `out A4h` (displayed page), stored in DS:146A.

### 36 PALETTE `idx r g b` (verified)
Writes r,g,b to DS:13EC + idx*3 (fade target) and calls `0DCA:0200(idx, {r,g,b})`: `out A8h=idx`,
`AAh=g`, `ACh=r`, `AEh=b` (4-bit analog palette) and stores it in the current palette DS:1438+idx*3.
`PALETTE 15 15 15 15` (white text colour) appears in most pages.

### 21 FADEIN `[int]`, 22 FADEOUT `[int]`, 42 FADEWHITE `[int]` (verified args)
Speed defaults to 1 when no FB follows. `0DCA:02F7(48-byte copy of DS:13EC, n)` fades the current
palette towards the target; `0DCA:0445(n)` builds an all-black target and calls 02F7;
`0DCA:0494(n)` the same with (inferred) an all-white target. `FADEOUT 0` = immediate.

### 2A ADT_LOAD `str`, 2D ADT_PLAY, 2B ADT_FREE (verified args)
`0ED8:0078(name)` opens `name.adt` ("ADT file is not fount" on failure), `0ED8:0001()` plays it (with
vsync waits), `0ED8:0217()` frees it. Always used as `ADT_LOAD x ... ADT_PLAY ADT_FREE`. Names:
fa03, fa08, fa09, meg05, ra03. Error 6 if no string.

### 2E ANIM_START `a b c d e f`, 2F ANIM_STOP (verified args, purpose inferred)
`blink_start` 0ED8:036F(sx,sy,dx,dy,w,h) stores the values in DS:1B56..1B60, precomputes the byte
offsets `sy*80+sx` (source strip in the buffers, loaded with a `*_m` image) and `dy*80+dx` (VRAM
destination), installs the vsync interrupt (`vsync_irq_install` 0ECB:004F) and sets
`blink_tick` 0ED8:0270 as callback; `blink_stop` 0ED8:03CB removes it. Animates the eyes of the
displayed character while text runs (x and w in bytes = 8 pixels, y and h in lines).

### 29 OPENING `[int]`, 47 STAFFROLL (verified callees)
`0E31:04AE(n)`: canned animation using `0100b.gdt`/`0100b.adt`, music `xen_03a`/`xen_03b`, ending on the
copyright strip `0100bb` when n = 1 or the `xenon2` title logo otherwise, then a key wait (used once in
S0104 as `OPENING 1` and once in S0401 as `OPENING 2`, each before a JMP). `0E31:05D2()`: the
ending/staff-roll sequence (music `xenon_02`, `st00`..`st09` images); used after COMPLETE at every
ending. Step-by-step timelines: engine.md §4.2 / §4.3.

### 03 CLEAR_LOWER (verified)
`0B7D:0852()`: clears 80-byte rows from y=0x128 down in all four planes. Unused.

### 38 TEXTPOS `x y mx my`, 39 TEXTCOLOR `a b`, 40 MSG_CLEAR (verified)
Set message origin (DS:07D8 byte column, DS:07DA pixel row) and menu origin (DS:07E0/07E2); text
colours DS:07DC/07DE (passed to the glyph renderer; first value is the palette index, 15 = white,
second always 0); `MSG_CLEAR` = `text_window_clear` 0B7D:0783.

## Sound (PMD, INT 60h)

### 0E MUSIC_LOAD `str` (verified args)
`music_load` 0B2C:01C4(name): if a driver is present (DS:076C != 0) and the name differs from the
current one (DS:13C8, saved), stops the music, opens `name.M` (PMD) or `name.MMD` (MMD) and reads it
into the driver's music buffer. Error 5 if no string. Names: `xenon_01`..`xenon_29`,
`xenon_102`.

### 0F MUSIC_PLAY / 10 MUSIC_STOP / 30 MUSIC_FADE `n` / 31 MUSIC_FADEWAIT / 3A MUSIC_VOL `n` / 48 MUSIC_WAIT `n`
INT 60h (PMD; INT 61h for the MMD driver, per DS:076C) with AH = 00 (`music_play`: start unless
already started, DS:13D7), 01 (`music_stop`), 02 + AL=n (`music_fade`: fade-out speed n),
`music_fade_stop_wait` (fade 25, poll AH=08h until FFh, stop), 19h + AL=n (`music_set_fadevol`,
0 = full volume .. FFh = silent), and `music_wait_status` (AH=09h board check, then loop while AH=0Ah
status1 == n; status1 is set from the MML, so `MUSIC_WAIT 0` waits for a marker in the song).
MUSIC_LOAD of the name that is already loaded only sets DS:13D7 = 1, so the following MUSIC_PLAY
does not restart the song (verified 0B2C:01DC..01ED).

### 45 SE_PLAY `n` / 46 SE_STOP (verified args)
INT 60h AH=0Ch AL=n / AH=0Dh: PMD FM sound effect on/off (effect data = XEN_SE.EFC, loaded by
seg 0B2C). 484 uses; SE 9 always follows SAVE (save chime); other numbers by context (inferred).
