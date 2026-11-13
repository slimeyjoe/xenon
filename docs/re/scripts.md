# Xenon scenario scripts (.CC): container, bytecode and interpreter

Engine: `AGS.EXE` (C's Ware, 1994, PC-98). Script code lives in segment **088D**. Addresses are
`SEG:OFF` in the shared-listing convention (load base 0; file offset = 0xE00 + SEG*16 + OFF;
DGROUP = 0F9F, written `DS:xxxx`). **verified** = read in the disassembly and/or confirmed on
all 40 shipped scripts; **inferred** = plausible reading that I did not fully trace.

Related documents: [opcodes.md](opcodes.md) (per-opcode reference),
[script-catalog.md](script-catalog.md) (what every script does, the route graph).
Tools: `source/tools/lzss.py` (codec), `re-tools/ccdis.py` (disassembler/decompiler).
Extracted data: `resources/scripts/` (decompressed bytecode, listings, strings, cross references).

## 1. Container: the LCZ file

| Offset | Size | Content | Engine use |
|-------:|-----:|---------|------------|
| 0x00 | 3 | `"LCZ"` magic | **not checked** (verified: 088D:00C6 reads the first 0x14 bytes into a stack buffer `[bp-0x9C]` that is never examined) |
| 0x03 | 17 | filler | **not read**. The bytes differ between files and look like uninitialised memory of the packer (16-bit near pointers such as `0FC0`, `0BB4`, `08AA`, `144D`, ... at varying alignment) - inferred: stack garbage of the authoring tool |
| 0x14 | 4 | u32 LE decompressed size | only the low 16 bits are used (088D:012C: `DS:128E = [bp-0x22]`); it is also the `malloc` size of the bytecode buffer (088D:00F8; `malloc` 0000:1CF0 = far-heap allocation) |
| 0x18 | .. | LZSS stream | decoded until the input is exhausted |

### 1.1 Loader `cc_load` 088D:0000 (verified)

1. `fnsplit(name)`; if the name has no extension, `strcat(name, ".cc")` (DS:0238).
2. `xfopen(name, "rb")` (0843:0384: current directory, then every drive that has the file). Failure -> return 0.
3. `setvbuf(fp, NULL, _IOFBF, 0x2800)` (0000:3CFD).
4. `filelength(fileno(fp))` (0000:2C2B). If the **compressed file** is > 0xFFFF bytes:
   `fprintf(stderr, "Error 0022: command file is too large.\n")`, return 0. There is no check of the
   decompressed size besides it being a 16-bit value (largest shipped script: S0106, 39 717 bytes).
5. `fread(hdr, 0x14, 1)`, `fread(&size, 4, 1)`; `g_script_buf` (DS:00A8, far) = `malloc(size & 0xFFFF)`;
   on failure prints "メモリ確保に失敗しました" + "couldn't alocate for source data." and returns 0.
6. A second buffer of `filelength` bytes receives the rest of the file (`fread(.., filelength, 1)` -
   asks for 0x18 bytes more than exist; harmless).
7. LZSS decode (088D:0174..0274) into `g_script_buf`, ring buffer on the stack (`[bp-0x10AE]`).
8. Free the input buffer, `fclose`, return 1. `cc_free` (088D:030E) frees `g_script_buf` after the run.

### 1.2 LZSS parameters (verified in code and by round trip)

Okumura LZSS: N = 4096 ring, F = 18, THRESHOLD = 2, start position r = 0xFEE (088D:0181
`mov si,0FEEh`), flag byte LSB first, set bit = literal, clear bit = 2-byte match
`pos = b0 | (b1 & 0xF0) << 4`, `len = (b1 & 0x0F) + 3` (088D:0232..0270).

Difference from Okumura's `LZSS.C`: the ring is **zero-filled** (088D:0174 `rep stosw` of 0x7F7 words =
positions 0..0xFED), not space-filled. The input counter is `filelength - 0x18 + 1`, and decoding stops
when it reaches 0, i.e. after the last input byte (no output-size limit is enforced; the output buffer
is exactly the header size).

Round trip: `source/tools/lzss.py --check` decompresses each file and re-compresses with a faithful Python port
of Okumura's binary-tree encoder. **All 40 shipped files recompress byte-identically with a zero-filled
ring** (with a space-filled ring M.CC differs), so the original packer was Okumura's encoder with the
fill changed to 0. Decompressing with either fill gives identical output for all shipped files, and the
output matches the translation project's `scripts_cc/*.U.CC` byte for byte.

## 2. Bytecode token encoding (verified, `cc_skip_token` 088D:0514)

| First byte | Token | Size | Meaning |
|-----------:|-------|------|---------|
| `FB lo hi` | integer | 3 | signed 16-bit immediate |
| `FE lo hi` | flag | 3 | flag number (bit index into DS:00F2) |
| `FF lo hi` | label | 3 | menu option target / case label `D<n>` |
| `FD len text.. 00` | string | 2+len+1 | cp932 text; `len` = byte length without the NUL. The engine never uses `len`: it skips to the NUL scanning from the len byte, and reads text at `FD+2` |
| `FC` | - | 1 | listed in the skip table (088D:054F) as a 1-byte token, never used |
| any other | opcode | 1 | statement or structure byte |

In all 15 676 strings of the shipped scripts `len` equals the real length (ccdis `--check`).

## 3. Grammar

```
file    := ignored* page*              ; execution starts at the first 01 token (088D:02CB)
page    := 01 FB<n>  stmt*  02         ; PAGE n ... PEND
stmt    := FD text 00                  ; message
         | FE<f> 12 FB<v>              ; flag assignment  F<f> = v
         | 13 cond 14 0A stmt* 0B      ; IF cond THEN ... END
         | 1B 04 option* 05 case*      ; SEN ... SENEND, then the labelled cases
         | op operands                 ; see opcodes.md
cond    := FE<f> 12 FB<v> ( 15 FE<f> 12 FB<v> )*
option  := [ 13 cond 14 ] FD text 00 FF<id>
case    := FF<id> stmt* 08             ; label D<id> ... END
```

Every shipped script parses with this grammar with **zero unknown opcodes and 100 % byte coverage**
(every byte belongs to exactly one token of the tree; `re-tools/ccdis.py --check`). Bytes that are not in
the dispatch table (`01 04 05 0A 12 14 15 1B FF ...`) are only ever used structurally.

`1B` always appears immediately before `04` (200/200 occurrences). It is not an opcode: it is the
terminator byte that `cc_run_page`/`cc_menu` pass to `cc_exec` to run "everything up to the menu"
(see 4.3). Presumably the compiler emitted it automatically before every `SEN` (inferred).

### 3.1 Original keyword names

AGS.EXE contains **no script compiler** (verified: no keyword table, no lexer; the only keyword
strings are inside the error messages, and `debug1`/`debug2` are plain `printf`s in `cc_run_page`).
The error table `g_cc_errmsg` (DS:00B0, 16 far pointers, codes 0..15) was evidently shared with the
authoring compiler and gives the source keywords:

| Code | Message (DS:) | Raised at runtime by | Implied keyword |
|-----:|---------------|----------------------|-----------------|
| 0 | 構文が正しくありませんぜ、旦那 (02CF) "syntax error" | `debug1` (no 01), missing FB operand of int-opcodes | - |
| 1 | ページにはちゃんとページ番号をつけて頂戴! (02EE) | page/JMP without FB, `cc_find_page` | page number after `01` |
| 2 | ENDやPENDをきちんと対応させていませんよ (0318) | stray 02/08/0B (088D:08F4), `cc_skip_block` | **END** (08, 0B), **PEND** (02) |
| 3 | SEN～SENEND間に構文エラーがありますぜ (0340) | `cc_build_options`, `cc_find_label` | **SEN** (04), **SENEND** (05) |
| 4 | IF構文がちょっとおかしいぞなもし (036C) | IF handler, `cc_eval_cond` | **IF** (13) |
| 5 | MUSIC_LOADの後にFILE名を忘れずに! (0398) | op 0E without string | **MUSIC_LOAD** (0E) |
| 6 | G_SABUN_LOADの後にFILE名が必要でっせ (03BA) | ops 16, 2A, 33, 34 without string | **G_SABUN_LOAD** (16) |
| 7 | G_LOADの後にファイル名が必要でっせ (03DF) | ops 0C, 28 without string | **G_LOAD** (0C) |
| 8 | フラグへの代入文がちょっとおかしいぞ! (0402) | assignment syntax, flag >= 0x800 | flag assignment |
| 9 | JMP命令だけど、飛び先のページが見つからないよ～ (0428) | `cc_find_page` | **JMP** (06) |
| 10 | PENDがないよん (045D) | `cc_find_label` (no 02 after SEN) | PEND |
| 11 | SEN～SENEND間で指定しているラベルがみつからないの (047E) | option id without case | labels `D<n>` (the `LABEL=D%d` in the error format) |
| 12 | SEN～SENEND間の選択肢が、多すぎるわ (04B0) | more than 9 options | - |
| 13 | ブロック終了前に次のブロックがはじまっちゃたよ (04D4) | 0A or FF inside a skipped IF body | - |
| 14 | ラベルとENDが１対１に対応していないようでっせ (0503) | case without 08 | label ... END |
| 15 | JMP命令でジャンプ先がちょっとおかしいよん (0537) | never raised by AGS.EXE | (compiler only) |

`"命令FILEがLOADされてないのだ"` (DS:02A4, printed by `cc_run` when `g_script_buf` is NULL) means
"the command file is not loaded" (cf. "Error 0022: command file is too large"), not an opcode name.

`cc_error` (088D:0559) prints `"%s page=%d, LABEL=D%d, %s\n"` (DS:0561) with the script name
(DS:137E), the current page (DS:1294), the current label (DS:1296, omitted with format DS:057C when
-1) and the message, sets `g_leave_script` and `g_quit_game` (DS:13A6 = DS:13A8 = 1) and
`longjmp(DS:12D4, 1)` back into `cc_run`. **Every script error is fatal: the game exits to DOS**
(0ABA:03D6). `debug1` / `debug2` (DS:02C1/02C8) are printed just before error 0/1 when a page does not
start with `01` / `01 FB`.

## 4. Interpreter architecture (seg 088D, verified)

```
game_main 0ABA:01DC ── flags_clear_all 088D:1258 (new game / L / K / M)
  loop while !DS:13A6 && !DS:13A8:
     cc_load(DS:137E) 088D:0000 ─ cc_run 088D:029A ─ cc_free 088D:030E
cc_run:  setjmp(DS:12D4); pos = cc_find_byte(0, 01)   (or DS:12FC if DS:00AE==1, after LOAD)
         do { DS:1292 = 0; pos = cc_run_page(pos) } while (!DS:1290 && !DS:13A6)
cc_run_page(pos):  check 01 FB n; DS:1294 = n; DS:1296 = -1
         if next byte is 1B:            cc_exec(pos, 1B) then cc_menu
         else scan statements for the first 02 or 04:
            04 -> cc_exec(pos, 1B); unless JMP/FILE happened: cc_menu
            02 -> cc_exec(pos, 02)                      (plain page)
cc_exec(pos, term) 088D:05FC: while byte(pos) != term: dispatch(byte(pos))
```

### 4.1 `cc_exec` and the dispatch table

`cc_exec(pos, term)` loops until the byte at `pos` equals `term`, then skips it and returns the
position after it. If `g_force_load` (DS:06E6) is 1 the opcode is replaced by `1C` (LOAD) (088D:0607;
nothing sets DS:06E6 to 1 — a byte search of the whole code image, including code the disassembler
does not reach, finds only the three tests 088D:0607/0935/1457 and the clear at 088D:146F: dead path). Dispatch: linear search of the 53-entry
word table at **088D:094C** (values `02 03 06 07 08 0B 0C ... 48 FD FE`), jump through
`cs:[bx+0x6A]` = handler table **088D:09B6**. A byte not in the table is skipped as one statement
(088D:091C, `cc_skip_stmt`) **without an error**.

Handlers return the new position. The following leave `cc_exec` immediately (return without looking
for the terminator): **JMP** (06; returns the target page offset), **FILE** (07), **LOAD** (1C),
an **IF whose condition was true** (13, after running its body), and the terminator check itself.
The terminator test is done at the loop head (088D:0924) before dispatching, so the shared
handler of `02`/`08`/`0B` (088D:08E6) is only reached for a structure byte that is *not* the
current terminator and then raises error 2 (fatal) - e.g. a stray `08` in a page body. (Its own
"equal" branch is reachable only when `g_force_load` makes the loop dispatch at the terminator.)

`cc_skip_stmt` (088D:04A7) is the statement-level scanner used for searching: for `13` it skips up to
and including `14` (the condition, not the IF body); for `06 07 0C 0E` it skips two tokens
(opcode + operand); otherwise one token. Because searches (`cc_find_byte`, `cc_find_page`,
`cc_find_label`) walk tokens, operand bytes (e.g. an FB value 0x0001) can never be mistaken for
structure bytes.

### 4.2 Pages and control flow

* **PAGE n ... PEND** (`01 FB n` ... `02`). Pages are the only jump targets. A page that ends
  normally (reaches its `02` without a transfer) **falls through to the next page in the file**
  (`cc_exec` returns the offset after `02`, which `cc_run_page` then expects to be `01`). Falling off
  the end of the file runs `cc_run_page` on the byte after the buffer -> `debug1` + fatal error 0;
  in the shipped scripts every last page ends with FILE/EXIT/JMP.
* **JMP n** (`06 FB n`): `DS:1292 = 1`, `cc_find_page(n)` (088D:0ECD) scans the file from offset 0 for
  the first token `01` followed by `FB n` and returns its offset. Missing page -> error 9 (fatal).
  With duplicate page numbers the **first** one wins (K.CC has two `PAGE 23`; the second, showing
  `r&m03`, is unreachable - see the catalog).
* **FILE "name"** (`07 FD name`): `DS:1290 = 1`, `strcpy(DS:137E, name)`. `cc_run` returns,
  `game_main` frees the buffer and loads `name.cc`, starting at its first page. Flags are kept.
* **EXIT** (`1F`): `DS:13A6 = 1`: the interpreter stops; `game_main` returns to the title screen.
* Page numbers are arbitrary 16-bit values and need not be ordered (e.g. S0102: 0,1,2,100,300,301,
  4,...); they are listed in each listing file.

### 4.3 IF (verified, handler 088D:063C)

`13 cond 14 0A body 0B`. `cc_eval_cond` (088D:1104) evaluates `FE f 12 FB v` terms separated by `15`;
each term is `flag_test(f) == v` (flag_test returns 0/1, so only v = 0/1 are meaningful); the terms are
**AND**ed; there is no OR, NOT, `!=` or arithmetic. Any other byte in a condition is error 4.

* true: skip to `0A` (error 4 if missing), `cc_exec(body, 0B)`, then **return from the enclosing
  `cc_exec`** (088D:0680..0692). The body must therefore end with a transfer (JMP/FILE/EXIT):
  at page level a non-transferring true IF would make `cc_run_page` restart in the middle of the
  page and die with `debug1`; inside a case body it would silently end the case (menu re-shown).
  All 36 IF bodies in the shipped scripts end with JMP or FILE (ccdis `--check` verifies this).
* false: `cc_skip_block(body, 0B)` (088D:0DFB) skips to the matching `0B` (nested IFs are tracked;
  `0A`/`FF` inside -> error 13; `02`/`08` -> error 2) and execution continues after it.
* after both paths a following `1B` is skipped (088D:0688/06D7); never occurs in the data.
* Idiom: `IF F0 == 0 THEN ... END` is an unconditional block ("else" after a preceding IF that
  jumped): F0 is never written by any script and the flag array is zeroed at new game.

### 4.4 Menus: SEN ... SENEND and labels (verified, `cc_menu` 088D:0A20, `cc_submenu` 088D:0B99)

```
1B 04  [13 cond 14] FD text FF id  ...  05     options (max 9)
FF id  case-body  08                           one case per label, in any order
...                                            (cases follow SENEND up to PEND)
```

* `cc_build_options` (088D:2092), called **every time the menu is shown**, walks from `04` to `05`;
  options whose condition is false are omitted. It fills `g_menu_texts` (DS:1298, far `char*[10]`,
  NULL terminated) and `g_menu_ids` (DS:12C0, `int[10]`) and returns the index of the last option.
  Error 12 is raised only when a 10th option is encountered (check `count > 8` before adding).
* `select_menu(DS:1298, cursor)` (0B7D:048C, documented by the text/UI analysis) draws the list at
  the menu origin (DS:07E0/07E2, set by TEXTPOS) and returns the chosen index, or -1/-2 for
  cancel/other. The top-level cursor is remembered in `g_menu_cursor` (DS:00F0) while the menu loops
  and reset to 0 when the menu is left (088D:0B6B); nested menus remember their cursor in
  `g_submenu_cursor` DS:12FE `[8][8]` indexed by the parent selection and nesting depth.
* The chosen id becomes the current label (`DS:1296`, the `LABEL=D%d` of error messages).
  `cc_find_label` (088D:0F2E) searches token-wise from SENEND (`05`) to the next PEND (`02`) for
  `FF id` (error 10 if there is no PEND, error 11 if the label is missing). Labels must be unique
  within the page, including labels of nested menus (they are: outer menus use 1,2,..., nested
  save menus 3..6, etc.).
* The case body is executed with `cc_exec(.., 08)`. If the body contains another `1B 04` (nested
  menu, found by scanning for `04` before `08`), the text up to `1B` is executed and `cc_submenu`
  takes over; the nested cases are followed by the `08` of the enclosing case.
* **Menus loop**: after a case body ends at its `08` without a transfer, the menu is rebuilt
  (conditions re-evaluated) and shown again (088D:0B53 loop). The loop ends only when JMP, FILE,
  EXIT, LOAD or a script error sets DS:1292 / DS:1290 / DS:13A6, or when a LOAD is pending
  (DS:00AE). Typical use: "examine" menus where each option sets a flag that hides it
  (`IF F120 == 0: "..." -> D1`, case D1 ends with `F120 = 1  END`), with one option that leaves.
  In the data: 324 top-level cases end in a transfer, 55 fall through (re-show), 29 end in a nested
  menu; all 116 nested cases end in a transfer.
* A nested menu always ends with `longjmp(DS:12E8, 1)` back into `cc_menu`'s `setjmp` at 088D:0A2D
  (088D:0DE6), i.e. after a nested case **without** a transfer the *top-level* menu is shown again;
  the result position of a transfer is passed through `g_menu_result_pos` (DS:00AC). Cancel (-1) in
  a nested menu goes back one level (depth 2 -> top-level menu); -2 opens the system menu.
* Cancel/-1 or -2 in a top-level menu opens the **system menu** `cc_system_menu` (088D:223D): items
  DS:021C "キャンセル / 最初に戻る / ゲーム終了". "Return to start" sets DS:13A6 (back to title);
  "Quit" asks "このままゲームを続ける / 本当にゲームを終了する" and on yes sets DS:13A8 and DS:13A6
  (exit to DOS). Cancel returns to the menu.
* There is no free save: save points are scripted menus (typically PAGE 0 of a chapter file:
  `SEN "セーブする / ゲームを続ける"`, nested `SEN "データ１..４"`, cases `SAVE "saveN"  SE_PLAY 9
  MSG "データｎにセーブしました"  JMP 1`).

### 4.5 Flags (verified)

* `g_flags` DS:00F2, **0x100 bytes = 2048 one-bit flags** F0..F2047 (flag n = bit `n & 7` of byte
  `n / 8`, `bitmask8` 088D:22C6). Index >= 0x800 -> error 8.
* `F<n> = v` (`FE n 12 FB v`, `cc_assign` 088D:108D): v = 0 clears, any other value sets.
* Only IF and option conditions read flags; there are no counters, no arithmetic, no other variable
  type.
* Cleared (`flags_clear_all` 088D:1258 = `memset(DS:00F2, 0, 0x100)`) only by `game_main` before a
  script is started from the title screen (0ABA:039F), i.e. for New Game and before L/K/M. FILE keeps
  them, so flags carry across chapter files (e.g. F100-F102 set in S00* are tested in S0102; F139,
  F151-F154 set in S0301..S0305B decide the DREAM_03 ending in S0306A).
* Saved: the whole 0x100-byte array is written to the save file by `op_save` (088D:126A, fwrite at
  088D:1351) together with the script name DS:137E, the page number DS:1294, the current image and
  music names (save-format details are documented in save-format.md).
* Shipped scripts use F0 (read only) and F100..F180 (78 flags). Full table with reads/writes per
  file: `resources/scripts/flags.tsv`.

### 4.6 Persistent progress (XEN.GDT)

`COMPLETE n` (opcode 37, `op_set_progress` 088D:1C19) ORs `1 << (n-1)` into the one byte stored in
`XEN.GDT` (read at startup into DS:06E8). Shipped uses: S0111 -> 1, S0207 -> 2, S0306A -> 3,4,5,6
(five endings), S0403 -> 7. `game_main` (0ABA:02C7) picks the prologue: bit0 clear -> `s00`;
bit0 set and (bit1 clear or no bit of 0x3C) -> `s00b`; otherwise `s00c`. The omake (K/M) requires
`(DS:06E8 & 0x7F) == 0x7F`. See script-catalog.md.

### 4.7 Interpreter globals (DGROUP)

| Address | Name | Meaning |
|---------|------|---------|
| DS:00A8 | g_script_buf | far pointer to bytecode |
| DS:00AC | g_menu_result_pos | position returned by a case body (passed across longjmp) |
| DS:00AE | g_resume_load | 1 after LOAD: start at DS:12FC |
| DS:00B0 | g_cc_errmsg | 16 far `char*` error messages |
| DS:00F0 | g_menu_cursor | top-level menu cursor |
| DS:00F2 | g_flags | 0x100 bytes of flags |
| DS:06E6 | g_force_load | executes LOAD instead of the current opcode when 1 |
| DS:07D8/07DA | text window x (bytes), y (pixels) | set by TEXTPOS |
| DS:07DC/07DE | text colours | set by TEXTCOLOR |
| DS:07E0/07E2 | menu x, y | set by TEXTPOS |
| DS:128E | g_script_size | bytecode size |
| DS:1290 | g_file_jump | FILE executed |
| DS:1292 | g_page_jump | JMP executed |
| DS:1294 | g_cur_page | current page number |
| DS:1296 | g_cur_label | current case label (-1 = none) |
| DS:1298 | g_menu_texts | far `char*[10]` |
| DS:12C0 | g_menu_ids | `int[10]` |
| DS:12D4 | g_jmp_run | `jmp_buf` (errors -> cc_run) |
| DS:12E8 | g_jmp_menu | `jmp_buf` (nested menu -> cc_menu) |
| DS:12FC | g_resume_pos | resume offset after LOAD |
| DS:12FE | g_submenu_cursor | `int[8][8]` |
| DS:137E | g_cur_script | script name (40 bytes) |
| DS:13A6 | g_leave_script | stop interpreter, back to title |
| DS:13A8 | g_quit_game | exit to DOS |
| DS:13EC | palette target | 16 x RGB (4-bit), written by PALETTE, G_LOAD, G_SABUN_LOAD; used by FADEIN |
| DS:1422 | current image name | written by GDT loads, cleared by WINDOW (saved) |

## 5. Text display semantics

### 5.1 Messages (opcode FD, handler 088D:0745, verified)

Every `FD` string that appears as a statement is one message: `text_print(str, flag)`
(0B7D:019B) with `flag = !kbd_kana_locked()` (0AF8:0108 = INT 18h AH=02h bit 2, カナ lock; the flag is passed
through to `kanji_draw` 0F16:03D7, which waits `flag` VSYNCs before every full-width glyph: KANA lock off =
typewriter speed, on = instant; font.md §2.1).
The printer (documented with seg 0B7D):

* window origin DS:07D8 (byte column) / DS:07DA (pixel row), 18-pixel rows, 60 byte columns, 4 rows;
  wraps at column 60 (or absolute column 80) with the kinsoku table 0B7D:046C
  (、。？！）」』 may hang once past column 60; 々 at a line start is replaced by the preceding kanji);
* after the 4th row it shows the blinking prompt `ESC[5;37m` + SJIS EB9F (DS:08B1; EB9F = user character
  JIS 7621h, a "return key" symbol the engine defines at start-up, font.md §4) at text position (73, 24), waits for a key, clears and continues;
* at the end of the string, by default, it shows the blinking `▼` (DS:08D2) at (73, 24), waits for a
  key (0AF8:0254), erases the prompt, and then **clears the message window** (0B7D:0783).

Speaker names are plain text: `［涼子］：...` (full-width brackets and colon); there is no speaker
opcode. A message directly before a SEN (43 cases) never carries `{KEEP}`, so it is acknowledged with
the ▼ key wait and cleared, and the options are then drawn into the empty window (verified from the
data + the clear logic above).

### 5.2 Control codes inside strings (verified in 0B7D:019B; survey of all scripts)

| Byte(s) | ccdis | Meaning | Count in data |
|---------|-------|---------|---------------|
| `0A` | `\n` | new line (column 0, next row) | 205 |
| `04 d` | `{Wd}` | d = ASCII digit: replace the end-of-message ▼ key wait by a timed wait of (d-'0')*60 vsyncs (`wait_vsync` 0DCA:0012, returns early while CTRL is held); `{W0}` = continue immediately. Non-digit -> printf "数字ではない" | 233 |
| `05` | `{KEEP}` | do not clear the message window after this message | 162 |
| `0C` | `{KEY}` | wait for a key at this point (no prompt glyph; `wait_ok_key` 0AF8:0254) | 118 |
| `03 .. 03` | `{BIG}` | text between two 03 drawn with the double-width routine 0F16:051D | 0 |
| `@` + 1 byte | - | insert the string at DS:06EA (`text_print_big` 0B7D:000C) | 0 |

Combinations seen: `{W0}{KEEP}` (title cards, lines composed over several messages), `{KEY}{W0}`
(music mode: wait for key, no ▼), `{KEY}{KEY}{KEEP}{W1}` etc. No other control bytes occur.

### 5.3 Menus on screen

Options are FD strings drawn by `select_menu` at the menu origin (TEXTPOS args 3/4); the bullet
`◆` and leading full-width spaces are part of the option text. In play TEXTPOS is (10,313,10,313):
text and menu share the origin. Option texts are never wrapped (see the text-printer notes).

## 6. Quirks and engine bugs found

* K.CC defines `PAGE 23` twice (0x0840 `r&m02`, 0x0893 `r&m03`); JMP always reaches the first, so the
  plain `r&m03` CG is never shown in the gallery (PAGE 24 shows `r&m03` + diff `r&m03b`, and its
  "back" option jumps to the first PAGE 23). Probably a copy/paste numbering slip (inferred).
* The option limit is 9 (088D:20B7 tests `count > 8` before adding; arrays of 10 entries incl. the
  NULL terminator). The largest shipped menu has 7 options.
* `cc_find_label` tests the wrong variable for error 3 (088D:0F5B `cmp di,-1` again instead of `si`),
  so a SEN without SENEND is not diagnosed there (harmless for valid data).
* `cc_skip_block` recursion discards its own return value (088D:0E6E..0E74) and re-scans - harmless.
* Unknown opcodes are silently skipped as 1-token statements (no error).
* SAVE (19) and DISK (1D) are followed by two `cc_skip_stmt` calls (088D:077E + 088D:091C) - opcode
  plus the FD operand; this is correct, not a bug, but it is why their handlers take the position of
  the opcode rather than of the string (DISK reads the string at `pos+3`).

## 7. Open questions

* ~~DS:06E6 (`g_force_load`)~~ — resolved: never set to 1 anywhere in the code image (byte search;
  see §4.1), so the forced-LOAD path is dead.
* ~~Effect of the `text_print` flag~~ — resolved: per-glyph VSYNC wait (font.md §2.1).
* ~~`MUSIC_WAIT n` marker~~ — resolved in sound.md §3.1: XENON_01 sets status1 with `~+1` at clock 384
  (≈9.5 s), which ends `MUSIC_WAIT 0`.
* `{BIG}` (03) and `@` codes are implemented but unused; DS:06EA (the `@` insert string) is never
  written (byte search of the code image: its only reference is the read at 0B7D:022C), so it
  always holds its initial value.
