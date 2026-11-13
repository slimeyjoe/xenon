# DGROUP (segment 0F9F) memory map — globals, strings, tables

Scope: every byte of AGS.EXE's data group from DS:0000 to the end of BSS, plus the stack and heap
placement. Companion to [engine.md](engine.md) (architecture), [functions.md](functions.md) (code) and
the per-subsystem documents. Addresses: `DS:xxxx` = `0F9F:xxxx`; code addresses are `SEG:OFF` in the
shared-listing convention (file offset = `0xE00 + SEG*16 + OFF`). The initial values were read straight
from the file (DGROUP starts at file offset `0xE00 + 0xF9F0 = 0x107F0`). Names follow
`docs/re/listing/names.tsv`; names introduced here are marked in §2 by being absent from that file and
are collected in `names-globals.tsv` for merging. **[V]** = verified in code / file, **[I]** = inferred.

The access lists were produced mechanically from the shared listing (every `[disp16]` / `[reg+disp16]`
operand with DS as segment, plus every `push ds; push imm16` / `mov reg,imm16` pointer constant) and
then checked by hand for the engine segments; code that switches DS (the dead "MUSIC LALF" driver in
0C2A:0A1A–19FF runs with DS = CS, the RTL heap/`int86` code with DS = other segments) was excluded
**[V]**.

## 1. Bounds and layout

### 1.1 What the MZ header and C0 establish [V]

| Item | Value | Source |
|---|---|---|
| Load image | 0x10C7C bytes (`e_cp`=8Eh, `e_cblp`=7Ch, header 0xE00) | MZ header |
| DGROUP | paragraph 0F9Fh (C0 `mov dx,0F9Fh` 0000:0000, saved in `cs:[0266]`) | 0000:0000 |
| Initialised data in the file | DS:0000–128B (the file ends there; TLINK dropped the trailing zero bytes of the last record) | file size |
| `_INIT_` records | DS:1276–128D (4 × 6 bytes; last 2 bytes lie past the end of the file) | 0000:0117 `mov si,1276h; mov di,128Eh` |
| `_EXIT_` records | none (DS:128E–128E) | 0000:0146 |
| BSS | DS:128E–229D, zeroed by C0 (`rep stosb`, DI = 128Eh, CX = 229Eh − 128Eh) | 0000:00AE–00BE |
| `edata@` / end of BSS | 229Eh (also the initial value of DS:0064) | |
| Header SS:SP | 11C9:0080; SS = DGROUP + 22Ah paragraphs = **DS:22A0** | MZ header |
| `minalloc` | 10Ah paragraphs → load area ends at linear 11D1Ch = DS:232C | MZ header |
| Stack | `_stklen` (DS:0EE2) = 1000h; C0 keeps SS = 11C9 and sets SP = (1000h/16 + 1)·16 = **1010h**, i.e. the stack is 11C9:0000–100F = DS:22A0–32AF | 0000:0062–00A3 |
| Far heap | starts at paragraph 11C9 + 101h = **12CAh** (= DS:32B0); `_heapbase`/`_brklvl` segment words DS:0068/006C ← 12CAh, `_heaptop` segment DS:0070 ← PSP:[2]; the DOS block is shrunk to that size (INT 21h AH=4Ah, 0000:0096) | 0000:0083–0099 |

So DS ≠ SS (large/far-data model, SS = DS + 22A0h bytes); stack variables are addressed through SS,
globals through DS. Everything the engine allocates (script buffer, GDT file buffers, the four 32 000-byte
plane buffers, ADT frames, the environment copy) comes from the far heap above 12CA:0000.
engine.md's "BSS 0x128C–0x232B" is corrected in §7.

### 1.2 Region overview [V]

Object files contribute their data in link order, which is the same as the code-segment order
(C0, engine modules 0450 … 0F16, then the Borland library). This lets most bytes be attributed to a
module even where no code touches them.

| DS: range | Bytes | Owner | Contents |
|---|---:|---|---|
| 0000–0071 | 114 | C0 startup | null area, copyright, "Divide error", saved vectors, argc/argv/env, PSP, DOS version, errno, heap pointers |
| 0072–0091 | 32 | main (0450) | launch-mode flags, bad-option message |
| 0092–00A7 | 22 | disk (0843) | label wildcard and path format strings |
| 00A8–06E7 | 1600 | script (088D) | interpreter pointers, error-message table, flags (256 B), disk-label table, system-menu tables, all interpreter/save/disk strings |
| 06E8–0757 | 112 | game (0ABA) | progress byte, `@` insert string, title-menu row table, title/omake strings |
| 0758–0767 | 16 | input (0AF8) | "GAME END" pointer and string, BIOS 0000:0500 save |
| 0768–07D7 | 112 | sound (0B2C) | EFC name pointer, sound mode, driver/file strings |
| 07D8–0943 | 364 | text/menu (0B7D) | window/menu geometry, disk-name table, prompt and console escape strings |
| 0944–09A9 | 102 | graphics (0C2A) | plane-buffer pointers, VRAM plane pointers, dissolve order, strings |
| 09AA–09B1 | 8 | display lib (0DCA) **[I]** | plane segment table used by the font code |
| 09B2–0A69 | 184 | init (0E1A) | gaiji pattern 7621h, start-up messages |
| 0A6A–0AF1 | 136 | sequences (0E31) | staff-roll name table and opening/staff strings |
| 0AF2–0AFD | 12 | mouse (0E9B) | position, click latches, ISR guard |
| 0AFE–0B03 | 6 | VSYNC (0ECB) | tick counter, ISR guard |
| 0B04–0B7B | 120 | ADT/blink (0ED8) | ADT header scalars, blink table and state, strings |
| 0B7C–0B7D | 2 | font (0F16) | glyph-cache-ready flag |
| 0B7E–1275 | 1784 | Borland RTL | abort message, atexit count, `_ctype`, exit hooks, `_streams[20]`, handle table, errno map, spawn/tmp strings, `_stklen`, printf tables, `_mbctype` (SJIS), hankaku/zenkaku tables, conio state, float stubs |
| 1276–128D | 24 | C0 | `_INIT_` table |
| 128E–137D | 240 | script (088D) BSS | page/label state, menu arrays, two `jmp_buf`s, sub-menu cursors |
| 137E–13AB | 46 | script / game BSS | current script name, run-loop flags (13A6, 13A8, 13AA) |
| 13AC–13B5 | 10 | script BSS | dead load-name buffer |
| 13B6–13BF | 10 | input BSS | input source, saved INT 05h/06h |
| 13C0–13D8 | 25 | sound BSS | PMD buffer pointers, music name, started flag |
| 13D9–13DB | 3 | text/menu (0B7D) BSS | `g_vsync_spin_count` (13DA, dead code only) |
| 13DC–1437 | 92 | graphics BSS | plane buffers, GDT palette, display rect, image name, page-1 rect |
| 1438–146B | 52 | display lib BSS | palette shadow, page registers |
| 146C–1685 | 538 | mouse BSS | 512-byte ISR stack, limits, buttons, saved SS:SP, INT 15h, deltas |
| 1686–168D | 8 | VSYNC BSS | saved INT 0Ah, hook |
| 168E–1791 | 260 | — | unreferenced |
| 1792–1B65 | 980 | ADT/blink BSS | ADT header arrays, 120 frame pointers, blink parameters |
| 1B66–2165 | 1536 | font BSS | ANK glyph cache (96 × 16 B) |
| 2166–229D | 312 | RTL BSS | atexit table, spawn path buffers, fputc byte |
| 229E–229F | 2 | — | alignment to the stack paragraph |
| 22A0–32AF | 4112 | stack (SS = 11C9) | 1010h bytes (header value SP = 80h is replaced by C0) |
| 32B0– | | far heap | paragraph 12CAh up to the end of the DOS block |

## 2. Globals

Columns: **Initial** = value in the file (BSS = 0); far pointers are shown as `SEG:OFF` (`DS:` when the
segment is DGROUP, relocated at load). **Saved** = written to `SAVEn.DAT` by `op_save` 088D:126A
(save-format.md). **Accesses**: `function first-address +N more [kind]`, kinds `R` read, `W` write,
`&` address taken (pointer passed / array copied), `call` called through. Every row is **[V]** unless the
meaning says **[I]**. Rows marked "RTL, see note" belong to the Borland library; their names are best
identifications from the Borland C++ 3.x run-time layout and are being cross-checked by the RTL
analysis (§6).

"Initialiser image" rows are not variables: Borland keeps the initial contents of an initialised *local*
array in DGROUP and copies it to the stack on function entry with `SCOPY@` (0000:06FE); the copy is
what the code uses, so these bytes are never modified **[V]**. The two word constants DS:021A and DS:06FD
are the same thing for single-element local buffers.

#### C0 startup data (0000:0000 module c0; Borland C0.ASM `_DATA`)

| DS: | Size | Type | Name | Initial | Saved | Meaning | Accesses (function first-address [kind]) |
|---|---:|---|---|---|---|---|---|
| 0000 | 4 | `byte[4]` | `__null_area` | 0 | - | 4 zero bytes at DGROUP:0 (Borland null-pointer guard area; not checked in this memory model) **[I]** | — |
| 003E | 4 | `void far*` | `_Int0Vector` | 0 | - | RTL, see note. Saved INT 00h (divide error) vector; 0000:0171 saves INT 00/04/05/06 (AH=35h) and hooks INT 00h with 0000:015C ("Divide error", exit code 3); 0000:01B4 restores | SaveVectors 0000:0177 +1 [W]; __restorezero 0000:01B8 [R] |
| 0042 | 4 | `void far*` | `_Int4Vector` | 0 | - | RTL, see note. Saved INT 04h | SaveVectors 0000:0184 +1 [W]; __restorezero 0000:01C3 [R] |
| 0046 | 4 | `void far*` | `_Int5Vector` | 0 | - | RTL, see note. Saved INT 05h (later also replaced by the engine, see g_old_int05) | SaveVectors 0000:0191 +1 [W]; __restorezero 0000:01CE [R] |
| 004A | 4 | `void far*` | `_Int6Vector` | 0 | - | RTL, see note. Saved INT 06h | SaveVectors 0000:019E +1 [W]; __restorezero 0000:01D9 [R] |
| 004E | 2 | `int` | `__C0argc` | 0 | - | RTL, see note. argc (passed to main by 0000:0130) | _c0_startup 0000:0130 [R]; __setargv_nextch 0000:2435 [W] |
| 0050 | 4 | `char far* far*` | `__C0argv` | 0 | - | RTL, see note. argv | _c0_startup 0000:012C +1 [R]; __setargv_nextch 0000:2441 +1 [W] |
| 0054 | 4 | `char far* far*` | `__C0environ` | 0 | - | RTL, see note. envp (3rd argument of main) | _c0_startup 0000:0124 +1 [R]; _setenvp 0000:24B8 +1 [W] |
| 0058 | 2 | `unsigned` | `__envLng` | 0 | - | RTL, see note. environment length (0000:0046) | _c0_startup 0000:0046 [W]; _setargv 0000:232A [R] |
| 005A | 2 | `unsigned` | `__envseg` | 0 | - | RTL, see note. environment segment (PSP:2Ch, 0000:001D) | _c0_startup 0000:001D +1 [R,W]; _exec 0000:1859 +1 [R]; _setargv 0000:233A [R]; _setenvp 0000:2466 [R] |
| 005C | 2 | `unsigned` | `__envSize` | 0 | - | RTL, see note. environment size rounded to 16 (0000:0055) | _c0_startup 0000:0055 [W]; _setenvp 0000:246D [R] |
| 005E | 2 | `unsigned` | `__psp` | 0 | - | RTL, see note. PSP segment (0000:0019) | _c0_startup 0000:0019 +1 [R,W]; _exec 0000:1872 [R]; __brk_setblock 0000:1EE0 +4 [R]; _setargv 0000:2319 [R]; _spawn 0000:2580 +1 [R] |
| 0060 | 2 | `unsigned` | `__version` | 0 | - | RTL, see note. DOS version from INT 21h AH=30h (0060 = major, 0061 = minor); also read by count_dos_drives 0843:0119 | _c0_startup 0000:0016 +2 [R,W]; _exec 0000:18C7 [R]; _setargv 0000:2333 [R]; count_dos_drives 0843:011D [R] |
| 0062 | 2 | `int` | `errno` | 0 | - | RTL, see note. C errno (written by __IOerror 0000:0843, spawnl, 0000:222A) | __IOerror 0000:0870 [W]; _LoadProg 0000:224A +2 [W]; spawnl 0000:2632 [W] |
| 0064 | 2 | `unsigned` | (edata@) | 229Eh | - | RTL, see note. Initialised to 229Eh = end of BSS; Borland heap bookkeeping, exact symbol not pinned down **[I]** | — |
| 0066 | 4 | `void far*` | `__heapbase` | 0 | - | RTL, see note. far heap base (segment at 0068 set by C0 0000:0087 to SS+stack paragraphs) | __brk 0000:1F55 +1 [R]; __sbrk 0000:1FD8 +1 [R]; _c0_startup 0000:0087 [W] |
| 006A | 4 | `void far*` | `__brklvl` | 0 | - | RTL, see note. current break level (segment 006C set by C0 0000:008B) | __brk_setblock 0000:1EFA +1 [W]; __sbrk 0000:1F9D +5 [R]; farcoreleft 0000:2033 +1 [R]; _c0_startup 0000:008B [W] |
| 006E | 4 | `void far*` | `__heaptop` | 0 | - | RTL, see note. top of the memory block (segment 0070 = PSP:2, C0 0000:0021) | __brk_setblock 0000:1F41 +2 [R,W]; __brk 0000:1F68 +1 [R]; __sbrk 0000:1FE5 +1 [R]; farcoreleft 0000:202C +1 [R]; _c0_startup 0000:0021 +1 [W] |

#### main (0450)

| DS: | Size | Type | Name | Initial | Saved | Meaning | Accesses (function first-address [kind]) |
|---|---:|---|---|---|---|---|---|
| 0072 | 2 | `int` | `g_hdd_mode` | 0 | - | 1 when started without arguments (main 0450:00xx) | main 0450:0032 [W]; op_disk_change 088D:0FA6 [R] |
| 0074 | 2 | `int` | `g_floppy_mode` | 0 | - | 1 when started as `AGS F` (floppy boot via STARTUP.COM) | main 0450:0023 [W]; op_disk_change 088D:1083 [R]; op_save 088D:126F +1 [R]; op_load 088D:14F5 +1 [R] |

#### disk layer (0843) — strings only, see §3

| DS: | Size | Type | Name | Initial | Saved | Meaning | Accesses (function first-address [kind]) |
|---|---:|---|---|---|---|---|---|

#### script interpreter (088D)

| DS: | Size | Type | Name | Initial | Saved | Meaning | Accesses (function first-address [kind]) |
|---|---:|---|---|---|---|---|---|
| 00A8 | 4 | `unsigned char far*` | `g_script_buf` | 0 | - | far pointer to the decompressed bytecode (`malloc` 0000:1CF0 in cc_load — far heap in this model; freed by cc_free) | cc_load 088D:0102 +2 [R,W]; cc_run 088D:029A +1 [R]; cc_free 088D:030E +3 [R]; cc_find_byte 088D:0334 [R]; cc_run_page 088D:0367 +9 [R]; cc_skip_stmt 088D:04AD +1 [R]; … 45 more functions |
| 00AC | 2 | `int` | `g_menu_result_pos` | 0 | - | script position returned from a (nested) case body across the longjmp | cc_run_page 088D:049E [R]; cc_menu 088D:0B50 [W]; cc_submenu 088D:0DCE [W] |
| 00AE | 2 | `int` | `g_resume_load` | 0 | - | 1 = cc_run starts at g_resume_pos (set by LOAD) | cc_run 088D:02E5 +1 [R,W]; cc_menu 088D:0A38 [R]; op_load 088D:1704 [W] |
| 00B0 | 64 | `char far*[16]` | `g_cc_errmsg` | DS:02CF, DS:02EE, DS:0318, DS:0340, … (16) | - | script error messages, codes 0..15 (§4.1) | cc_error 088D:056F +3 [R]; _exec 0000:1960 +5 [R] |
| 00F0 | 2 | `int` | `g_menu_cursor` | 0 | - | top-level SEN cursor | cc_menu 088D:0A4B +10 [R,W]; _exec 0000:19A8 +1 [R,W] |
| 00F2 | 256 | `byte[256]` | `g_flags` | 0 | **yes** | 2048 one-bit script flags F0..F2047 | flag_set 088D:11EA [R,W]; flag_clear 088D:121A [R,W]; flag_test 088D:1248 [R]; flags_clear_all 088D:125E [&]; op_save 088D:134E [&]; op_load 088D:151F [&]; … 1 more functions |
| 01F2 | 40 | `char[5][8]` | `g_disk_labels` | "_ADISK_" | - | volume labels `_ADISK_`..`_EDISK_` (§4); checked by op_load — *initialiser image of a local array*, copied to the stack with `SCOPY@` (0000:06FE) by op_load 088D:144C | op_load 088D:144C [&] |
| 021A | 2 | `int` | `g_xen_default` | 0 | - | constant 0: initialiser of op_set_progress's local XEN.GDT byte (088D:1C50), i.e. the value used when the file exists but is empty | op_set_progress 088D:1C50 [R] |
| 021C | 16 | `char far*[4]` | `g_sysmenu_items` | DS:0683, DS:0692, DS:06A1, 0000:0000 | - | system menu items (§4) — *initialiser image of a local array*, copied to the stack with `SCOPY@` (0000:06FE) by cc_system_menu 088D:2248 | cc_system_menu 088D:2248 [&] |
| 022C | 12 | `char far*[3]` | `g_quitconfirm_items` | DS:06B0, DS:06CB, 0000:0000 | - | quit confirmation items (§4) — *initialiser image of a local array*, copied to the stack with `SCOPY@` (0000:06FE) by cc_system_menu 088D:228A | cc_system_menu 088D:228A [&] |
| 06E6 | 2 | `int` | `g_force_load` | 0 | - | if 1, cc_exec executes LOAD instead of the current opcode; tested in cc_exec/op1F_exit, cleared by op_load, never set to 1 (dead) | cc_exec 088D:0607 [R]; op1F_exit 088D:0935 [R]; op_load 088D:1457 +1 [R,W] |

#### game / title (0ABA)

| DS: | Size | Type | Name | Initial | Saved | Meaning | Accesses (function first-address [kind]) |
|---|---:|---|---|---|---|---|---|
| 06E8 | 2 | `int` | `g_progress` | 0 | - | XEN.GDT completion bitmask (read by game_main every return to the title) | omake_cg_gallery 0ABA:00BD [R]; omake_music_mode 0ABA:00D9 [R]; game_main 0ABA:0261 +3 [R,W] |
| 06EA | 13 | `char[13]` | `g_name_insert` | " " | - | string inserted for `@` in message text, initialised `" "` (13 bytes up to the next object, size **[I]**); never written | text_print 0B7D:022D [&] |
| 06F7 | 6 | `int[3]` | `g_title_menu_rows` | B0h, E0h, 110h | - | y (pixel rows) of the title-menu highlight bar: 176, 224, 272 (title_menu_hilite 0ABA:0056 `[bx+06F7]`) | title_menu_hilite 0ABA:0066 [R] |
| 06FD | 2 | `int` | `g_xen_read_default` | 0 | - | constant 0: initialiser of game_main's local read buffer (0ABA:0241) — used when XEN.GDT exists but is empty. A *missing* file skips the read and keeps g_progress unchanged (0ABA:023F) | game_main 0ABA:0241 [R] |

#### input (0AF8)

| DS: | Size | Type | Name | Initial | Saved | Meaning | Accesses (function first-address [kind]) |
|---|---:|---|---|---|---|---|---|
| 0758 | 4 | `char far*` | `g_msg_game_end` | DS:075F | - | -> "GAME END" (printed by the emergency exit in wait_ok_key 0AF8:027E) | wait_ok_key 0AF8:0286 +1 [R] |
| 075C | 1 | `byte` | `g_bios500_saved` | 03h | - | saved BIOS byte 0000:0500 (initial 03h in the file is overwritten by bios500_set_bit5) | bios500_set_bit5 0AF8:008E [W]; bios500_restore 0AF8:00AB [R] |
| 075D | 2 | `int` | `g_bios500_flag` | 0 | - | 1 = 0000:0500 was modified and must be restored | bios500_set_bit5 0AF8:009B [W]; bios500_restore 0AF8:00A2 +1 [R,W] |

#### sound (0B2C)

| DS: | Size | Type | Name | Initial | Saved | Meaning | Accesses (function first-address [kind]) |
|---|---:|---|---|---|---|---|---|
| 0768 | 4 | `char far*` | `g_se_file` | DS:076D | - | -> "XEN_SE.EFC" | snd_init 0B2C:0132 +1 [R] |
| 076C | 1 | `byte` | `g_sound_mode` | 0 | - | 0 = no driver (silent), 1 = PMD, 2 = MMD (MIDI) + PMD (never selected) | snd_init 0B2C:008E +2 [W]; snd_clear_efc_hook 0B2C:019F [R]; music_load 0B2C:01C8 +1 [R]; music_play 0B2C:02EA +1 [R]; music_stop 0B2C:0341 +1 [R]; music_fade 0B2C:038B +1 [R]; … 4 more functions |

#### text / menu (0B7D)

| DS: | Size | Type | Name | Initial | Saved | Meaning | Accesses (function first-address [kind]) |
|---|---:|---|---|---|---|---|---|
| 07D8 | 2 | `int` | `g_textwin_x` | Ah | - | message window left edge, byte columns (TEXTPOS arg 1) | op38_textpos 088D:1ACB [W]; text_print_big 0B7D:0060 +2 [R]; text_print 0B7D:01F8 +3 [R]; text_window_clear 0B7D:078B [R] |
| 07DA | 2 | `int` | `g_textwin_y` | 139h | - | message window top, pixel row (TEXTPOS arg 2) | op38_textpos 088D:1AD1 [W]; text_print_big 0B7D:0056 +1 [R]; text_print 0B7D:01F1 +2 [R]; text_window_clear 0B7D:0785 [R] |
| 07DC | 2 | `int` | `g_text_color` | Fh | - | text colour (palette index; TEXTCOLOR arg 1) | op39_textcolor 088D:1D92 [W]; text_print_big 0B7D:0049 +1 [R]; text_print 0B7D:01E7 +2 [R] |
| 07DE | 2 | `int` | `g_text_bg` | 0 | - | second TEXTCOLOR argument, passed to the glyph routines (always 0 in the scripts) | op39_textcolor 088D:1D98 [W]; text_print_big 0B7D:0045 [R]; text_print 0B7D:01E3 +1 [R] |
| 07E0 | 2 | `int` | `g_menuwin_x` | Ah | - | menu origin x, byte columns (TEXTPOS arg 3) | op38_textpos 088D:1AD7 [W]; menu_draw_item 0B7D:065D [R]; menu_hilite 0B7D:0682 [R]; menu_draw_item_near 0B7D:0708 [R]; menu_clear 0B7D:0725 [R] |
| 07E2 | 2 | `int` | `g_menuwin_y` | 139h | - | menu origin y, pixels (TEXTPOS arg 4) | op38_textpos 088D:1ADD [W]; menu_draw_item 0B7D:0656 [R]; menu_hilite 0B7D:0679 [R]; menu_draw_item_near 0B7D:0701 [R]; menu_clear 0B7D:071F [R] |
| 07E4 | 205 | `char[5][41]` | `g_disk_names` | "Ａディスク" | - | "Ａディスク".."Ｅディスク" (disk A..E), copied by prompt_insert_disk 0B7D:0808 (`push 0x7e4`, `mov cx,0xcd` = 205 bytes) and indexed by disk letter — *initialiser image of a local array*, copied to the stack with `SCOPY@` (0000:06FE) by prompt_insert_disk 0B7D:0817 | prompt_insert_disk 0B7D:0817 [&] |

#### graphics (0C2A)

| DS: | Size | Type | Name | Initial | Saved | Meaning | Accesses (function first-address [kind]) |
|---|---:|---|---|---|---|---|---|
| 0944 | 16 | `void far*[4]` | `g_gfxbuf_raw` | 0 | - | calloc'd plane buffers (0x7D10 bytes each) as returned by the allocator | gfx_alloc_buffers 0C2A:0028 +5 [R,W]; gfx_free_buffers 0C2A:00A3 +5 [R,W]; gdt_decode_vram 0C2A:019C +1 [R]; gdt_decode_buf 0C2A:043A +1 [R]; gfx_clear_buffers 0C2A:05BA +3 [R]; gfx_show 0C2A:0656 +1 [R]; … 4 more functions |
| 0954 | 2 | `-` | (unreferenced) | 0 | - | padding/unused | — |
| 0956 | 16 | `void far*[4]` | `g_vram_planes` | A800:0000, B000:0000, B800:0000, E000:0000 | - | A800:0000, B000:0000, B800:0000, E000:0000 (VRAM planes B,R,G,I; gfx_merge_page1) | gfx_merge_page1 0C2A:0228 +9 [R] |
| 0966 | 16 | `byte[16]` | `g_dissolve_order` | 00 0A 04 0E 08 02 0C 06 0D 07 01 0B 05 0F 09 03 | - | fx_dissolve cell order 0,10,4,14,8,2,12,6,13,7,1,11,5,15,9,3 | fx_dissolve_cell 0C2A:0893 [R] |

#### display library (0DCA) — data defined between 0C2A and 0E1A **[I]**

| DS: | Size | Type | Name | Initial | Saved | Meaning | Accesses (function first-address [kind]) |
|---|---:|---|---|---|---|---|---|
| 09AA | 8 | `unsigned[4]` | `g_plane_segs` | A800h, B000h, B800h, E000h | - | A800h, B000h, B800h, E000h — plane segments used by the font renderer (0F16:0369, 0F16:03A0 `[bx+09AA]`) | vram_put_glyph16 0F16:037F [R]; vram_put_glyph32 0F16:03B6 [R] |

#### init (0E1A)

| DS: | Size | Type | Name | Initial | Saved | Meaning | Accesses (function first-address [kind]) |
|---|---:|---|---|---|---|---|---|
| 09B2 | 34 | `byte[34]` | `g_gaiji_7621` | 00 00 00 00 00 00 00 00 7F F0 80 08 … | - | user-defined character pattern for JIS 7621h (SJIS EB9Fh): 2 header bytes + 16 rows x 16 bits; passed by engine_init (0E1A:00DD..00F3, BX:CX = DS:09B2, DX = 7621h) to 0F16:0841 = INT 18h AH=1Ah. Drawn: a "Return key" symbol (box with a hooked left arrow), see §4 | engine_init 0E1A:00E0 [&] |

#### sequences (0E31)

| DS: | Size | Type | Name | Initial | Saved | Meaning | Accesses (function first-address [kind]) |
|---|---:|---|---|---|---|---|---|
| 0A6A | 36 | `char far*[9]` | `g_staff_names` | DS:0AB7, DS:0ABC, DS:0AC1, DS:0AC6, … (9) | - | -> "st01".."st09" (staff_roll 0E31:05DD copies the table to the stack) — *initialiser image of a local array*, copied to the stack with `SCOPY@` (0000:06FE) by staff_roll 0E31:05DD | staff_roll 0E31:05DD [&] |

#### mouse (0E9B)

| DS: | Size | Type | Name | Initial | Saved | Meaning | Accesses (function first-address [kind]) |
|---|---:|---|---|---|---|---|---|
| 0AF2 | 2 | `int` | `g_mouse_x` | 0 | - | mouse x (0..639) | mouse_read_deltas 0E9B:0189 +3 [R,W]; mouse_get_pos 0E9B:0251 [R]; mouse_set_pos 0E9B:0268 [W] |
| 0AF4 | 2 | `int` | `g_mouse_y` | 0 | - | mouse y (0..399) | mouse_read_deltas 0E9B:0191 +3 [R,W]; mouse_get_pos 0E9B:025A [R]; mouse_set_pos 0E9B:026E [W] |
| 0AF6 | 4 | `-` | (unreferenced) | 0 | - | no code reference | — |
| 0AFA | 1 | `byte` | `g_mouse_lclick` | 0 | - | left button press latched (test-and-clear by mouse_left_clicked) | mouse_read_deltas 0E9B:01EF +2 [R,W]; mouse_left_clicked 0E9B:02A0 +1 [R,W] |
| 0AFB | 1 | `byte` | `g_mouse_rclick` | 0 | - | right button press latched | mouse_read_deltas 0E9B:01F6 +2 [R,W]; mouse_right_clicked 0E9B:02B4 +1 [R,W] |
| 0AFC | 2 | `int` | `g_mouse_isr_busy` | 0 | - | isr_mouse re-entrancy guard (FFFFh while running) | isr_mouse 0E9B:00AA +2 [R,W] |

#### VSYNC interrupt (0ECB)

| DS: | Size | Type | Name | Initial | Saved | Meaning | Accesses (function first-address [kind]) |
|---|---:|---|---|---|---|---|---|
| 0AFE | 4 | `unsigned long` | `g_vsync_ticks` | 0 | - | incremented by isr_vsync; no other reader | isr_vsync 0ECB:0021 +1 [R,W]; vsync_get_ticks 0ECB:00B9 +1 [R] |
| 0B02 | 2 | `int` | `g_vsync_isr_busy` | 0 | - | isr_vsync re-entrancy guard | isr_vsync 0ECB:000F +2 [R,W] |

#### ADT / blink animator (0ED8)

| DS: | Size | Type | Name | Initial | Saved | Meaning | Accesses (function first-address [kind]) |
|---|---:|---|---|---|---|---|---|
| 0B04 | 4 | `char[4]` | `g_adt_magic` | 0 | - | receives the 3-byte "ADT" magic (fread 3 bytes; not checked) | adt_load 0ED8:00FD [&] |
| 0B08 | 2 | `int` | `g_adt_loop_start` | 1 | - | ADT loop start frame (header +0F7h); initial 1 | adt_play 0ED8:0034 [R] |
| 0B0A | 2 | `-` | (unreferenced) | 0 | - | no code reference | — |
| 0B0C | 2 | `int` | `g_adt_cur` | 0 | - | current ADT frame | adt_play 0ED8:0018 +5 [R,W]; adt_free 0ED8:0243 [W] |
| 0B0E | 4 | `-` | (unreferenced) | 0 | - | no code reference | — |
| 0B12 | 2 | `int` | `g_adt_loaded` | 0 | - | 1 while an ADT is loaded | adt_load 0ED8:0091 [W]; adt_free 0ED8:0218 +1 [R,W] |
| 0B14 | 28 | `int[14]` | `g_blink_table` | 0, 1, 1, 6, 3, 12Ch, 1, 6, 3, 3, 1, 6, 3, 12Ch | - | blink step durations 0,1,1,6,3,300,1,6,3,3,1,6,3,300 vsyncs | blink_tick 0ED8:027C [R] |
| 0B30 | 2 | `int` | `g_blink_frame` | 0 | - | strip frame 0..2 | blink_tick 0ED8:0294 +5 [R,W]; blink_stop 0ED8:03CB [W] |
| 0B32 | 2 | `int` | `g_blink_step` | 0 | - | index into g_blink_table (wraps 13 -> 2) | blink_tick 0ED8:0276 +3 [R,W]; blink_stop 0ED8:03D1 [W] |
| 0B34 | 2 | `int` | `g_blink_tick` | 0 | - | tick counter | blink_tick 0ED8:0280 +2 [R,W] |
| 0B36 | 2 | `int` | `g_blink_dir` | 1 | - | 1 = frame++, 0 = frame--; initial 1 | blink_tick 0ED8:0338 +2 [R,W] |

#### font (0F16)

| DS: | Size | Type | Name | Initial | Saved | Meaning | Accesses (function first-address [kind]) |
|---|---:|---|---|---|---|---|---|
| 0B7C | 1 | `byte` | `g_font_cache_ready` | 0 | - | 1 once 0F16:0009 has read the 96 ANK glyphs into g_ank_font (checked at 0F16:000E and by the ANK renderer 0F16:06C5) | font_ank_cache_init 0F16:000E +1 [R,W]; ank_draw 0F16:06C5 [R] |
| 0B7D | 1 | `-` | (padding) | 0 | - |  | — |

#### Borland RTL initialised data (seg 0000 library modules)

| DS: | Size | Type | Name | Initial | Saved | Meaning | Accesses (function first-address [kind]) |
|---|---:|---|---|---|---|---|---|
| 0B9E | 2 | `int` | `_atexitcnt` | 0 | - | RTL, see note. number of atexit() entries (0000:0281 adds, 0000:066C runs them) | atexit 0000:0284 +2 [R,W]; __exit 0000:0679 +2 [R,W] |
| 0BA0 | 257 | `byte[257]` | `_ctype` | 00 20 20 20 20 20 20 20 20 20 21 21 … | - | RTL, see note. ctype class table, indexed `[c+0BA1]` (0BA0 = EOF entry); bits 1 = space-ish, 2 = digit, 4 = upper, 8 = lower, 10h = hex, 20h = control, 40h = punct (Borland layout) | __searchpath_copy 0000:0AC2 [R]; toupper 0000:0FC5 [R]; text_print 0B7D:02FA [R] |
| 0CA2 | 4 | `void (far*)()` | `_exitbuf` | 0000:066B | - | RTL, see note. stdio flush hook called at exit (default 0000:066B `_dummy_exithook` = retf; setvbuf sets 0000:44CA `_xfflush`) | __exit 0000:0695 [call]; _LoadProg 0000:22B9 [call]; setvbuf 0000:3DBE +1 [W] |
| 0CA6 | 4 | `void (far*)()` | `_exitfopen` | 0000:066B | - | RTL, see note. close-all-streams hook (fopen mode parser 0000:2CD9 sets 0000:448D `_xfclose`) | __exit 0000:06AD [call]; __fopen_mode 0000:2D7B +1 [W] |
| 0CAA | 4 | `void (far*)()` | `_exitopen` | 0000:066B | - | RTL, see note. close-handles hook (never changed: no close-all-handles hook linked) | __exit 0000:06B1 [call] |
| 0CAE | 400 | `FILE[20]` | `_streams` | 00 00 09 02 00 00 00 00 00 00 00 00 … | - | RTL, see note. 20-byte FILE structs: stdin 0CAE (flags 0209h, fd 0), stdout 0CC2 (020Ah, fd 1), stderr 0CD6 (0202h, fd 2), stdaux 0CEA (0243h, fd 3), stdprn 0CFE (0242h, fd 4), 15 free; token = own address. stdout used by printf, stderr by main / cc_load / cc_run / cc_error fprintf | _setupio 0000:0F66 +8 [&,R,W]; flushall 0000:2C7D [&]; __getfp 0000:2E6F [&]; __flushterm 0000:32FD [&]; getchar 0000:34B1 [&]; _xfclose 0000:4499 [&]; … 1 more functions |
| 0E3E | 2 | `unsigned` | `_nfile` | 14h | - | RTL, see note. 20 (C0 0000:00C0 extends the DOS handle table when > 20) | _c0_startup 0000:00C0 +1 [R]; _setupio 0000:0EF8 +1 [R]; close 0000:29EF [R]; eof 0000:2A38 [R]; flushall 0000:2C76 [R]; __getfp 0000:2E86 [R]; … 3 more functions |
| 0E40 | 40 | `unsigned[20]` | `_openfd` | 6001h, 6002h, 6002h, A004h, A002h, 0, 0, 0, 0, 0, 0, 0, 0, 0 … | - | RTL, see note. per-handle open flags: 6001h, 6002h, 6002h, A004h, A002h, 0... | lseek 0000:0949 [R,W]; _read 0000:0A60 [R]; _setupio 0000:0F02 [W]; close 0000:2A02 [W]; _close 0000:2A1F [W]; eof 0000:2A49 [R]; … 8 more functions |
| 0E68 | 2 | `unsigned` | `_fmode` | 4000h | - | RTL, see note. 4000h = O_TEXT | __fopen_mode 0000:2D63 [R]; open 0000:3647 [R] |
| 0E6A | 2 | `unsigned` | `_notUmask` | FFFFh | - | RTL, see note. FFFFh | open 0000:366C [R] |
| 0E6C | 2 | `int` | `_doserrno` | 0 | - | RTL, see note. last DOS error (__IOerror) | __IOerror 0000:0856 +1 [W]; open 0000:3684 +1 [R] |
| 0E6E | 89 | `signed char[]` | `_dosErrorToSV` | 00 13 02 02 04 05 06 08 08 08 14 15 … | - | RTL, see note. DOS error -> errno map used by __IOerror (`[si+0E6E]`) | __IOerror 0000:085A [R] |
| 0EE2 | 2 | `unsigned` | `_stklen` | 1000h | - | RTL, see note. stack size 1000h (read/written by C0 0000:0062..00A9; minimum 200h) | — |
| 0EEB | 96 | `byte[]` | `__vprinter_class` | 00 14 14 01 14 15 14 14 14 14 02 00 … | - | RTL, see note. character-class/state table of __vprinter (0000:103B `[bx+0EEB]`) | __vprinter_flush 0000:10CE [R] |
| 0F4C | 257 | `byte[257]` | `_mbctype` | 00 00 00 00 00 00 00 00 00 00 00 00 … | - | RTL, see note. Shift-JIS byte class table indexed `[c+0F4D]`: 04h = SJIS lead (81-9F, E0-FC), 08h = trail (40-7E, 80-FC), 01h/02h = half-width kana (A1-DF); tested by text_print (bit 2), 0F16:07D5, 0000:0A87, 0000:1502 | __searchpath_copy 0000:0A96 [R]; chkctype 0000:1516 +2 [R]; text_print_big 0B7D:001F [R]; text_print 0B7D:01BD [R]; text_draw_string 0F16:07EC [R] |
| 104E | 190 | `unsigned[95]` | `__hantozen_ascii` | 40 81 49 81 68 81 94 81 90 81 93 81 … | - | RTL, see note. full-width SJIS codes for ASCII 20h..7Eh (8140 8149 8168 ...), used by 0000:159A | hantozen 0000:15B1 [R]; zentohan_body 0000:1629 [&] |
| 110C | 128 | `unsigned[64]` | `__hantozen_kana` | 40 81 42 81 75 81 76 81 41 81 45 81 … | - | RTL, see note. full-width codes for half-width A0h..DFh (8142 8175 8176 8141 8145 8392 8340 ...) | hantozen 0000:15CB [R] |
| 118C | 87 | `byte[87]` | `__zentohan_kata` | A7 B1 A8 B2 A9 B3 AA B4 AB B5 B6 B6 … | - | RTL, see note. half-width byte for SJIS 8340h..8396h (katakana; 0 = none), used by 0000:159A | zentohan_body 0000:15F7 [R] |
| 11E3 | 18 | `unsigned[9]` | `__zentohan_punct` | 42 81 75 81 76 81 41 81 45 81 5B 81 … | - | RTL, see note. 9 SJIS punctuation words (8142 8175 8176 8141 8145 815B 814A 814B …) paired with the 9 half-width bytes at 11F5 | zentohan_body 0000:164C [R] |
| 11F5 | 9 | `char[9]` | (__zentohan_punct half-width bytes) | "｡｢｣､･ｰﾞﾟ" | - | RTL, see note. "｡｢｣､･ｰﾞﾟ" (A1..A5, B0, DE, DF) | zentohan_body 0000:1652 [R] |
| 1230 | 2 | `unsigned` | `__brk_lastfail` | 0 | - | RTL, see note. read/written by 0000:1ED8 (far-heap brk helper) | __brk_setblock 0000:1EEB +1 [R,W] |
| 1232 | 1 | `char` | `_cFlag` | 0 | - | RTL, see note. getch: ungetch pending flag (never set: no ungetch linked) | getch 0000:2068 +1 [R,W]; kbhit 0000:2218 [R] |
| 1233 | 1 | `char` | `_cChar` | 0 | - | RTL, see note. ungetch character | getch 0000:2074 [R] |
| 1234 | 2 | `int` | `_argc` | 0 | - | RTL, see note. set by _setargv (`__setargv_nextch` 0000:23E7) | __setargv_nextch 0000:23E7 +1 [R,W] |
| 1236 | 4 | `char far* far*` | `_argv` | 0 | - | RTL, see note. far argv (on the startup stack), set by _setargv | __setargv_nextch 0000:23FA +3 [R,W] |
| 123A | 10 | `word[5]` | `__setargv_save` | 0 | - | RTL, see note. _setargv temporaries (return address, BP, SI, DI) written by 0000:22FF | _setargv 0000:22FF +4 [W]; __setargv_nextch 0000:2444 +4 [R,call] |
| 1244 | 4 | `char far*` | `environ` | 0 | - | RTL, see note. malloc'd environment block, written by `_setenvp` 0000:2464, read by 0000:222A, 0000:34BC | _LoadProg 0000:227C +1 [R]; _setenvp 0000:247A +3 [R,W]; getenv 0000:34DD [R] |
| 1248 | 17 | `struct` | `_video` | 0 | - | RTL, see note. PC-98 conio text-info block filled by `_crtinit` 0000:263D from DOS work area 0000:0712/0714/071D: window, attribute, rows (1251), columns (1252 = 80), text-VRAM far ptr (1253: A000:0000, E000 hi-res), hi-res flag (1257, BIOS 0501h bit 3) | _crtinit 0000:263F +15 [R,W]; __update_rows_clip 0000:26DC +1 [R,W]; __update_rows 0000:26CC [W]; _setcursortype 0000:279D [R] |
| 1259 | 3 | `-` | (unknown) | 01 00 00 | - | RTL. 1259 = 01h in the file; no reference found | — |
| 125C | 6 | `char[]` | (cursor on seq) | "\e[>5l" | - | RTL, see note. "\e[>5l" (shown cursor) used by _setcursortype | _setcursortype 0000:2795 [&] |
| 1262 | 6 | `char[]` | (cursor off seq) | "\e[>5h" | - | RTL, see note. "\e[>5h" | _setcursortype 0000:278E [&] |
| 1268 | 2 | `char[2]` | (CR) | "\r" | - | RTL, see note. "\r" written by fputc before '\n' in text mode | fputc 0000:3971 [&] |
| 126A | 2 | `int` | `_stdinBufferedFlag` | 0 | - | RTL, see note. setvbuf bookkeeping | setvbuf 0000:3D3B +1 [R,W] |
| 126C | 2 | `int` | `_stdoutBufferedFlag` | 0 | - | RTL, see note. setvbuf bookkeeping | setvbuf 0000:3D25 +1 [R,W] |
| 126E | 2 | `near fn` | `_RealCvtVector` | 16C8h | - | RTL, see note. 16C8h `_fpnotlinked_printf` (used by __realcvt for %e/%f/%g) | __realcvt 0000:22F9 [call] |
| 1270 | 6 | `near fn[3]` | `_ScanVectors` | CD 16 CD 16 CD 16 | - | RTL, see note. 3 × 16CDh `_fpnotlinked_scanf` | — |
| 1276 | 24 | `InitRec[4]` | `_INIT_table` | 00 02 F5 0E 00 00 00 10 FF 22 00 00 … | - | startup records {type 0 = near, priority, offset, segment}: (2, 0000:0EF5 `_setupio`), (10h, 0000:22FF `_setargv`), (10h, 0000:2464 `_setenvp`), (10h, 0000:263D `_crtinit`, PC-98 conio init); run by 0000:01E1 (SI=1276h, DI=128Eh). The last 2 bytes (128C..128D) lie beyond the end of the file image (TLINK trims trailing zeros) — harmless because type 0 entries ignore the segment word. The exit table is empty (128E..128E, 0000:013F) | — |

#### BSS, engine part (zeroed by C0 0000:00B5..00BE)

| DS: | Size | Type | Name | Initial | Saved | Meaning | Accesses (function first-address [kind]) |
|---|---:|---|---|---|---|---|---|
| 128E | 2 | `unsigned` | `g_script_size` | 0 (BSS) | - | decompressed script size (low word of the LCZ header size) | cc_load 088D:012F [W]; cc_find_byte 088D:032E +1 [R]; cc_find_page 088D:0ED4 +1 [R] |
| 1290 | 2 | `int` | `g_file_jump` | 0 (BSS) | - | set by FILE (07) | cc_run 088D:02D9 +1 [R,W]; cc_run_page 088D:042B [R]; op07_file 088D:071A [W]; cc_menu 088D:0B53 [R] |
| 1292 | 2 | `int` | `g_page_jump` | 0 (BSS) | - | set by JMP (06) | cc_run 088D:02DF [W]; cc_run_page 088D:0424 +1 [R]; op06_jmp 088D:06E4 [W]; cc_menu 088D:0B5A [R]; cc_submenu 088D:0DD1 [R] |
| 1294 | 2 | `int` | `g_cur_page` | 0 (BSS) | **yes** | current page number (resume point) | cc_run 088D:02C5 [W]; cc_run_page 088D:03BA [W]; cc_error 088D:0577 +1 [R]; op_load 088D:16F5 [R] |
| 1296 | 2 | `int` | `g_cur_label` | 0 (BSS) | - | current case label D<n> (-1 outside cases) | cc_run_page 088D:03BD [W]; cc_error 088D:055F +1 [R]; cc_menu 088D:0AB1 [W]; cc_submenu 088D:0CE2 [W] |
| 1298 | 40 | `char far*[10]` | `g_menu_texts` | 0 (BSS) | - | option texts for select_menu (NULL-terminated) | cc_menu 088D:0A5A [&]; cc_submenu 088D:0BF1 [&]; cc_build_options 088D:211D +5 [W] |
| 12C0 | 20 | `int[10]` | `g_menu_ids` | 0 (BSS) | - | option label ids | cc_menu 088D:0A8C +1 [R]; cc_submenu 088D:0CA9 +1 [R]; cc_build_options 088D:2141 +2 [W] |
| 12D4 | 20 | `jmp_buf` | `g_jmp_run` | 0 (BSS) | - | setjmp in cc_run (errors longjmp here) | cc_run 088D:02B6 [&]; cc_error 088D:05C5 [&] |
| 12E8 | 20 | `jmp_buf` | `g_jmp_menu` | 0 (BSS) | - | setjmp in cc_menu (nested-menu exit) | cc_menu 088D:0A2A [&]; cc_submenu 088D:0DE3 [&] |
| 12FC | 2 | `int` | `g_resume_pos` | 0 (BSS) | - | bytecode offset to resume at after LOAD | cc_run 088D:02EC [R]; op_load 088D:1701 [W] |
| 12FE | 128 | `int[8][8]` | `g_submenu_cursor` | 0 (BSS) | - | nested SEN cursor memory | cc_menu 088D:0B80 [W]; cc_submenu 088D:0BBE +11 [R,W] |
| 137E | 40 | `char[40]` | `g_cur_script` | 0 (BSS) | **yes** | current script file name | cc_error 088D:057C +1 [&]; op07_file 088D:0737 [&]; op_save 088D:1364 [&]; op_load 088D:164B +1 [&]; game_main 0ABA:02D4 +1 [&] |
| 13A6 | 2 | `int` | `g_leave_script` | 0 (BSS) | - | 1 = stop the interpreter, back to the title | cc_run 088D:0306 [R]; cc_error 088D:05B6 [W]; op1F_exit 088D:090C [W]; cc_menu 088D:0B61 [R]; cc_system_menu 088D:2279 +1 [W]; game_main 0ABA:03A6 +1 [R,W] |
| 13A8 | 2 | `int` | `g_quit_game` | 0 (BSS) | - | 1 = quit to DOS | cc_error 088D:05BC [W]; cc_system_menu 088D:22B2 [W]; game_main 0ABA:01E0 +2 [R,W] |
| 13AA | 2 | `int` | `g_title_flag_13aa` | 0 (BSS) | - | write-only: cleared together with g_leave_script by game_main (0ABA:03A9); no reader | game_main 0ABA:03A9 [W] |
| 13AC | 10 | `char[10]` | `g_load_name` | 0 (BSS) | - | save name used by op_load when g_force_load == 1 (never written: dead) | op_load 088D:145F [&] |
| 13B6 | 2 | `int` | `g_input_from_mouse` | 0 (BSS) | - | 1 = last input_get_cmd result came from the mouse (select_menu clamps instead of wrapping) | input_get_cmd 0AF8:02E2 +1 [W]; select_menu 0B7D:0569 +1 [R] |
| 13B8 | 4 | `void far*` | `g_old_int06` | 0 (BSS) | - | saved INT 06h (STOP) vector | kbd_hook_copy_stop 0AF8:003D +1 [W]; kbd_unhook_copy_stop 0AF8:0077 +1 [R] |
| 13BC | 4 | `void far*` | `g_old_int05` | 0 (BSS) | - | saved INT 05h (COPY) vector | kbd_hook_copy_stop 0AF8:002E +1 [W]; kbd_unhook_copy_stop 0AF8:0065 +1 [R] |
| 13C0 | 4 | `void far*` | `g_pmd_music_buf` | 0 (BSS) | - | PMD music buffer address (INT 60h AH=06h; buffer - 1) | snd_init 0B2C:0105 +1 [W]; music_load 0B2C:02C3 +1 [R] |
| 13C4 | 4 | `void far*` | `g_pmd_efc_buf` | 0 (BSS) | - | PMD FM-effect buffer (AH=0Bh) | snd_init 0B2C:0126 +3 [R,W] |
| 13C8 | 15 | `char[15]` | `g_music_name` | 0 (BSS) | **yes** (40-byte block) | current song base name (sound.md calls it g_music_name); the save writes 40 bytes from here (13C8..13EF) | snd_init 0B2C:00DF [W]; music_load 0B2C:01D3 +1 [&]; music_get_name 0B2C:02E1 [&]; music_play 0B2C:02F9 [&]; music_stop 0B2C:037E [W]; music_fade 0B2C:0393 [&]; … 1 more functions |
| 13D7 | 2 | `int` | `g_music_started` | 0 (BSS) | **yes** (in block) | 1 = MUSIC_PLAY already issued for the current name | music_load 0B2C:01ED +1 [W]; music_play 0B2C:02F1 +1 [R,W] |
| 13D9 | 1 | `-` | (unreferenced) | 0 (BSS) | **yes** (in block) | no code reference | — |
| 13DA | 1 | `byte` | `g_vsync_spin_count` | 0 (BSS) | **yes** (in block) | only written by the dead `gdc_vsync_spin` 0B7D:0A4B (text module BSS) | gdc_vsync_spin 0B7D:0A4D +3 [R,W] |
| 13DB | 1 | `-` | (unreferenced) | 0 (BSS) | **yes** (in block) | no code reference | — |
| 13DC | 16 | `void far*[4]` | `g_gfxbuf` | 0 (BSS) | **yes** (in block) | off-screen plane buffers B,R,G,I, normalised to seg:0000 | gfx_alloc_buffers 0C2A:0086 +1 [W]; gfx_merge_page1 0C2A:0281 +7 [R]; gfx_copy_row 0C2A:05F2 +7 [R]; seq_slide_up_iplane 0E31:00AA +9 [R]; seq_slide_up 0E31:0145 +7 [R]; logo_show_mark 0E31:0265 +7 [R]; … 4 more functions |
| 13EC | 48 | `byte[16][3]` | `g_gdt_palette` | 0 (BSS) | partly (first 4 B in the music block) | palette (R,G,B 0..15) of the last GDT that carried one; target of FADEIN; written by PALETTE | op36_palette 088D:1BD7 +2 [W]; op21_fadein 088D:1FBA [&]; omake_menu 0ABA:0127 [&]; game_main 0ABA:029C [&]; gdt_load 0C2A:0366 [&]; gdt_decode_buf 0C2A:0474 [&]; … 5 more functions |
| 141C | 2 | `int` | `g_disp_off` | 0 (BSS) | - | display rect: VRAM offset y*80+x | gdt_decode_buf 0C2A:04A3 [W]; gfx_show 0C2A:0666 [R]; fx_blind 0C2A:06B0 [R]; fx_center_open 0C2A:0787 [R]; fx_dissolve 0C2A:096E +2 [R,W] |
| 141E | 2 | `int` | `g_disp_wbytes` | 0 (BSS) | - | display rect width in bytes | gdt_decode_buf 0C2A:048B [W]; gfx_show 0C2A:066A [R]; fx_blind 0C2A:06B4 [R]; fx_center_open 0C2A:0792 [R]; fx_dissolve 0C2A:0968 +2 [R,W] |
| 1420 | 2 | `int` | `g_disp_h` | 0 (BSS) | - | display rect height | gdt_decode_buf 0C2A:0492 [W]; gfx_show 0C2A:0672 +1 [R]; fx_blind 0C2A:06F3 +1 [R]; fx_center_open 0C2A:077E +1 [R]; fx_dissolve 0C2A:0974 +2 [R,W] |
| 1422 | 15 | `char[15]` | `g_gfx_state` | 0 (BSS) | **yes** (40-byte block) | current base GDT name; the save writes 40 bytes from here (1422..1449) | op11_window 088D:194A [W]; game_main 0ABA:0395 [W]; gfx_get_state_ptr 0C2A:000A [&]; gfx_alloc_buffers 0C2A:0090 [W]; gdt_load_buf 0C2A:0132 [&]; gdt_load 0C2A:0378 [&] |
| 1431 | 2 | `int` | `g_p1_off` | 0 (BSS) | **yes** (in block) | rect of the last gdt_load_page1: offset | gdt_decode_vram 0C2A:01F1 [W]; gfx_merge_page1 0C2A:0217 [R] |
| 1433 | 2 | `int` | `g_p1_wbytes` | 0 (BSS) | **yes** (in block) | width in bytes | gdt_decode_vram 0C2A:01D9 [W]; gfx_merge_page1 0C2A:021F +1 [R] |
| 1435 | 2 | `int` | `g_p1_h` | 0 (BSS) | **yes** (in block) | height | gdt_decode_vram 0C2A:01E0 [W]; gfx_merge_page1 0C2A:0209 +1 [R] |
| 1437 | 1 | `-` | (padding) | 0 (BSS) | **yes** (in block) |  | — |
| 1438 | 48 | `byte[16][3]` | `g_pal_shadow` | 0 (BSS) | partly (first 18 B in the gfx block) | shadow of the hardware palette (R,G,B) | pal_set 0DCA:01E2 +2 [W]; pal_get_all 0DCA:02EA [&] |
| 1468 | 2 | `int` | `g_access_page` | 0 (BSS) | - | last value written to port A6h | set_access_page 0DCA:0156 [W]; set_pages_vsync 0DCA:017D [W]; get_pages 0DCA:019C [R] |
| 146A | 2 | `int` | `g_display_page` | 0 (BSS) | - | last value written to port A4h | set_display_page 0DCA:0168 [W]; set_pages_vsync 0DCA:0183 [W]; get_pages 0DCA:01A5 [R] |
| 146C | 512 | `byte[512]` | `g_mouse_isr_stack` | 0 (BSS) | - | private stack of isr_mouse (SS = DGROUP, SP = 166Ch at 0E9B:00C7) | — |
| 166C | 2 | `int` | `g_mouse_ymin` | 0 (BSS) | - | 0 (also the initial SP of the ISR stack) | mouse_init 0E9B:000F [W]; mouse_read_deltas 0E9B:0195 [R]; mouse_set_limits 0E9B:02F1 [W] |
| 166E | 2 | `int` | `g_mouse_ymax` | 0 (BSS) | - | 399 | mouse_init 0E9B:001B [W]; mouse_read_deltas 0E9B:019E [R]; mouse_set_limits 0E9B:02F9 [W] |
| 1670 | 2 | `int` | `g_mouse_xmin` | 0 (BSS) | - | 0 | mouse_init 0E9B:0015 [W]; mouse_read_deltas 0E9B:01AA [R]; mouse_set_limits 0E9B:02ED [W] |
| 1672 | 2 | `int` | `g_mouse_xmax` | 0 (BSS) | - | 639 | mouse_init 0E9B:0021 [W]; mouse_read_deltas 0E9B:01B3 [R]; mouse_set_limits 0E9B:02F5 [W] |
| 1674 | 2 | `-` | (unreferenced) | 0 (BSS) | - | no code reference | — |
| 1676 | 1 | `byte` | `g_mouse_lbtn` | 0 (BSS) | - | left button state from the last interrupt (0E9B:01DA) | mouse_read_deltas 0E9B:01BF +3 [R,W]; mouse_get_buttons 0E9B:0276 [R] |
| 1677 | 1 | `byte` | `g_mouse_rbtn` | 0 (BSS) | - | right button state (0E9B:01EC) | mouse_read_deltas 0E9B:01C5 +3 [R,W]; mouse_get_buttons 0E9B:028A [R] |
| 1678 | 1 | `byte` | `g_mouse_lbtn_prev` | 0 (BSS) | - | previous left state (edge detection, 0E9B:01C2) | mouse_read_deltas 0E9B:01C2 +2 [R,W] |
| 1679 | 1 | `byte` | `g_mouse_rbtn_prev` | 0 (BSS) | - | previous right state | mouse_read_deltas 0E9B:01C8 +2 [R,W] |
| 167A | 2 | `unsigned` | `g_mouse_saved_ss` | 0 (BSS) | - | interrupted SS saved by isr_mouse | isr_mouse 0E9B:00BF +1 [R,W] |
| 167C | 2 | `unsigned` | `g_mouse_saved_sp` | 0 (BSS) | - | interrupted SP | isr_mouse 0E9B:00BB +1 [R,W] |
| 167E | 4 | `void far*` | `g_old_int15` | 0 (BSS) | - | saved INT 15h (mouse) vector | mouse_init 0E9B:0046 +1 [W]; mouse_shutdown 0E9B:006A +1 [R] |
| 1682 | 2 | `int` | `g_mouse_dx` | 0 (BSS) | - | X delta assembled from the two 4-bit reads (0E9B:0124/013A) | mouse_read_deltas 0E9B:0124 +3 [R,W] |
| 1684 | 2 | `int` | `g_mouse_dy` | 0 (BSS) | - | Y delta (0E9B:0154/016A) | mouse_read_deltas 0E9B:0154 +3 [R,W] |
| 1686 | 4 | `void far*` | `g_old_int0a` | 0 (BSS) | - | saved INT 0Ah vector | vsync_irq_install 0ECB:006E +1 [W]; vsync_irq_remove 0ECB:0090 +3 [R] |
| 168A | 4 | `void (far*)()` | `g_vsync_hook` | 0 (BSS) | - | callback called by isr_vsync (blink_tick) | isr_vsync 0ECB:002B +2 [R,call]; vsync_irq_install 0ECB:0055 +1 [W]; vsync_set_hook 0ECB:00CA +1 [W] |
| 168E | 260 | `-` | (unreferenced) | 0 (BSS) | - | no code reference (between the 0ECB and 0ED8 objects) | — |
| 1792 | 2 | `int` | `g_adt_count` | 0 (BSS) | - | ADT frame count N | adt_play 0ED8:0027 [R]; adt_load 0ED8:017A +1 [R]; adt_free 0ED8:0221 +1 [R] |
| 1794 | 240 | `unsigned[120]` | `g_adt_sizes` | 0 (BSS) | - | ADT frame blob sizes | adt_load 0ED8:012A +2 [&,R] |
| 1884 | 2 | `int` | `g_adt_loop` | 0 (BSS) | - | ADT loop flag | adt_play 0ED8:002D [R] |
| 1886 | 240 | `int[120]` | `g_adt_delays` | 0 (BSS) | - | vsyncs before each frame | adt_play 0ED8:001E [R]; adt_load 0ED8:016D [&] |
| 1976 | 480 | `void far*[120]` | `g_adt_frames` | 0 (BSS) | - | malloc'd frame blobs | adt_play 0ED8:004D +1 [R]; adt_load 0ED8:019A +5 [R,W]; adt_free 0ED8:0230 +1 [R] |
| 1B56 | 2 | `int` | `g_blink_sx` | 0 (BSS) | - | blink source x (bytes) | blink_start 0ED8:0375 +1 [R,W] |
| 1B58 | 2 | `int` | `g_blink_sy` | 0 (BSS) | - | blink source y (lines) | blink_start 0ED8:037B +1 [R,W] |
| 1B5A | 2 | `int` | `g_blink_dx` | 0 (BSS) | - | blink destination x (bytes) | blink_start 0ED8:0381 +1 [R,W] |
| 1B5C | 2 | `int` | `g_blink_dy` | 0 (BSS) | - | blink destination y (lines) | blink_start 0ED8:0387 +1 [R,W] |
| 1B5E | 2 | `int` | `g_blink_w` | 0 (BSS) | - | width in bytes | blink_tick 0ED8:02AF +3 [R]; blink_start 0ED8:038D [W] |
| 1B60 | 2 | `int` | `g_blink_h` | 0 (BSS) | - | frame height | blink_tick 0ED8:0291 +2 [R]; blink_start 0ED8:0393 [W] |
| 1B62 | 2 | `int` | `g_blink_src` | 0 (BSS) | - | sy*80+sx | blink_tick 0ED8:029B [R]; blink_start 0ED8:03A2 [W] |
| 1B64 | 2 | `int` | `g_blink_dst` | 0 (BSS) | - | dy*80+dx | blink_tick 0ED8:028D [R]; blink_start 0ED8:03B2 [W] |
| 1B66 | 1536 | `byte[96][16]` | `g_ank_font` | 0 (BSS) | - | ANK glyph cache: 96 glyphs (8x16) for characters 20h..7Fh, read from the kanji ROM as codes 2920h+i (INT 18h AH=14h) by 0F16:0009; glyph for c at 1B66+(c-20h)*16 (0F16:07A9 `add ax,1966h`) | — |

#### BSS, RTL part

| DS: | Size | Type | Name | Initial | Saved | Meaning | Accesses (function first-address [kind]) |
|---|---:|---|---|---|---|---|---|
| 2166 | 128 | `void (far*)()[32]` | `_atexittbl` | 0 (BSS) | - | RTL, see note. atexit table (0000:0281 writes `[bx+2166/2168]`, 0000:066C calls them in reverse) | atexit 0000:02A2 +1 [W]; __exit 0000:0685 [call] |
| 21E6 | 14 | `char[14]` | `__tmpnam_buf` | 0 (BSS) | - | RTL, see note. static result buffer of __mkname/tmpnam (0000:0978 passes 21E6h) | __mkname 0000:0978 [&] |
| 21F4 | 6 | `char[]` | `__sp_ext` | 0 (BSS) | - | RTL, see note. searchpath (0000:0C69) extension buffer | __searchpath_core 0000:0C97 +1 [&] |
| 21FA | 10 | `char[]` | `__sp_name` | 0 (BSS) | - | RTL, see note. searchpath (0000:0C69) name buffer | __searchpath_core 0000:0C9C +3 [&] |
| 2204 | 67 | `char[]` | `__sp_dir` | 0 (BSS) | - | RTL, see note. searchpath (0000:0C69) directory / current PATH element buffer | __searchpath_core 0000:0CA1 +9 [&,R,W] |
| 2247 | 4 | `char[4]` | `__sp_drive` | 0 (BSS) | - | RTL, see note. searchpath drive buffer | __searchpath_core 0000:0CA6 +6 [&,W] |
| 224B | 81 | `char[81]` | `__sp_result` | 0 (BSS) | - | RTL, see note. searchpath resulting full path | __searchpath_core 0000:0C74 [&] |
| 229C | 1 | `char` | `__fputc_ch` | 0 (BSS) | - | RTL, see note. one-byte buffer for unbuffered fputc | fputc 0000:382F +8 [R,W] |

## 3. String table

Every NUL-terminated string in DGROUP, in address order (138 entries; `\e` = ESC, `\n` = LF,
`\r` = CR). Texts are cp932; translations are mine. "via" = referenced through a pointer table.
The 16 script error messages are explained with their raising sites in
[scripts.md §3.1](scripts.md#31-original-keyword-names). Byte-identical strings are **not** merged by
the compiler (e.g. "rb" appears 5×, 「メモリ確保に失敗しました」 4×), except that the `""` passed to
`spawnl` at DS:0777 coincides with the terminating NUL of "XEN_SE.EFC" **[I]**.

| DS: | Bytes | Text | Name | Meaning / translation | Referenced by |
|---|---:|---|---|---|---|
| 0004 | 43 | `Borland C++ - Copyright 1991 Borland Intl.` | __CopyRight | C0 copyright banner (not referenced by code) | — |
| 002F | 15 | `Divide error\r\n` | ZeroDivMSG | "Divide error" — printed by the INT 00h handler 0000:015C, then _exit(3) | — |
| 0076 | 27 | `オプションが間違っています` | s_bad_option | "The option is wrong" (main, bad command line) | main 0450:003B |
| 0092 | 8 | `_?DISK_` | s_label_pattern | volume-label wildcard for findfirst(FA_LABEL) | get_volume_label 0843:040E |
| 009A | 6 | `%c:%s` | s_drive_file_fmt | "X:name" path built while searching drives | find_file_any_drive 0843:020E; xfopen 0843:03D3 |
| 00A0 | 7 | `%c:\%s` | s_drive_root_fmt | "X:\name" (root of drive X) | find_drive_by_label 0843:02CA; get_volume_label 0843:0446 |
| 01F2 | 8 | `_ADISK_` | (g_disk_labels[0]) | volume label of disk A | op_load 088D:144C |
| 01FA | 8 | `_BDISK_` | (g_disk_labels[1]) | volume label of disk B | — |
| 0202 | 8 | `_CDISK_` | (g_disk_labels[2]) | volume label of disk C | — |
| 020A | 8 | `_DDISK_` | (g_disk_labels[3]) | volume label of disk D | — |
| 0212 | 8 | `_EDISK_` | (g_disk_labels[4]) | volume label of disk E | — |
| 0238 | 4 | `.cc` | s_dot_cc | default script extension | cc_load 088D:0033 |
| 023C | 3 | `rb` | s_mode_rb | fopen mode (shared by cc_load, op_load, op_set_progress) | cc_load 088D:0059; op_load 088D:14AC; op_set_progress 088D:1C57 |
| 023F | 40 | `Error 0022: command file is too large.\n` | s_err_cmdfile_large | script file > 64 KB | cc_load 088D:00B5 |
| 0267 | 25 | `メモリ確保に失敗しました` | s_alloc_failed_cc | "Memory allocation failed" | cc_load 088D:0112 |
| 0280 | 36 | `\ncouldn't alocate for source data.\n` | s_alloc_failed_src | (sic "alocate") second line of the allocation error | cc_load 088D:011E |
| 02A4 | 29 | `命令FILEがLOADされてないのだ` | s_cmdfile_not_loaded | "The command FILE has not been LOADed" (cc_run with g_script_buf == NULL) | cc_run 088D:02A4 |
| 02C1 | 7 | `debug1` | s_debug1 | page does not start with 01 | cc_run_page 088D:0374 |
| 02C8 | 7 | `debug2` | s_debug2 | page number not FB | cc_run_page 088D:039D |
| 02CF | 31 | `構文が正しくありませんぜ、旦那` | s_cc_err00 | error 0: "The syntax is wrong, boss" | via `g_cc_errmsg` (DS:00B0) |
| 02EE | 42 | `ページにはちゃんとページ番号をつけて頂戴!` | s_cc_err01 | error 1: "Give every page a proper page number!" | via `g_cc_errmsg` (DS:00B4) |
| 0318 | 40 | `ENDやPENDをきちんと対応させていませんよ` | s_cc_err02 | error 2: "END and PEND are not matched properly" | via `g_cc_errmsg` (DS:00B8) |
| 0340 | 44 | `SEN～SENEND間に構文エラーがありますぜ、旦那` | s_cc_err03 | error 3: "Syntax error between SEN and SENEND, boss" | via `g_cc_errmsg` (DS:00BC) |
| 036C | 44 | `IF構文がちょっとおかしいぞなもし。見直そう!` | s_cc_err04 | error 4: "The IF construct is a bit odd. Check it!" | via `g_cc_errmsg` (DS:00C0) |
| 0398 | 34 | `MUSIC_LOADの後にFILE名を忘れずに!` | s_cc_err05 | error 5: "Don't forget the FILE name after MUSIC_LOAD!" | via `g_cc_errmsg` (DS:00C4) |
| 03BA | 37 | `G_SABUN_LOADの後にFILE名が必要でっせ` | s_cc_err06 | error 6: "G_SABUN_LOAD needs a FILE name" | via `g_cc_errmsg` (DS:00C8) |
| 03DF | 35 | `G_LOADの後にファイル名が必要でっせ` | s_cc_err07 | error 7: "G_LOAD needs a file name" | via `g_cc_errmsg` (DS:00CC) |
| 0402 | 38 | `フラグへの代入文がちょっとおかしいぞ!` | s_cc_err08 | error 8: "The flag assignment is a bit odd!" | via `g_cc_errmsg` (DS:00D0) |
| 0428 | 53 | `JMP命令だけど、飛び先のページが見つからないよ～(T_T)` | s_cc_err09 | error 9: "JMP command, but the target page cannot be found (T_T)" | via `g_cc_errmsg` (DS:00D4) |
| 045D | 33 | `PENDがないよん。ちゃんと付けてね` | s_cc_err10 | error 10: "There is no PEND. Please add it" | via `g_cc_errmsg` (DS:00D8) |
| 047E | 50 | `SEN～SENEND間で指定しているラベルがみつからないの` | s_cc_err11 | error 11: "The label given between SEN and SENEND cannot be found" | via `g_cc_errmsg` (DS:00DC) |
| 04B0 | 36 | `SEN～SENEND間の選択肢が、多すぎるわ` | s_cc_err12 | error 12: "Too many choices between SEN and SENEND" | via `g_cc_errmsg` (DS:00E0) |
| 04D4 | 47 | `ブロック終了前に次のブロックがはじまっちゃたよ` | s_cc_err13 | error 13: "The next block started before the block ended" | via `g_cc_errmsg` (DS:00E4) |
| 0503 | 52 | `ラベルとENDが１対１に対応していないようでっせ、旦那` | s_cc_err14 | error 14: "Labels and END do not correspond one-to-one, boss" | via `g_cc_errmsg` (DS:00E8) |
| 0537 | 42 | `JMP命令でジャンプ先がちょっとおかしいよん` | s_cc_err15 | error 15: "The JMP target is a bit odd" (never raised) | via `g_cc_errmsg` (DS:00EC) |
| 0561 | 27 | `%s page=%d, LABEL=D%d, %s\n` | s_cc_errfmt_label | script error line with label | cc_error 088D:0580 |
| 057C | 16 | `%s page=%d, %s\n` | s_cc_errfmt | script error line without label | cc_error 088D:05A7 |
| 058C | 37 | `%sディスクをドライブ２へ入れて下さい` | s_insert_disk_drv2 | "Please insert disk %s into drive 2" | op_disk_change 088D:0FEC |
| 05B1 | 9 | `_%cDISK_` | s_label_fmt | builds "_BDISK_" etc. | op_disk_change 088D:1003 |
| 05BA | 27 | `ディスクをセットして下さい` | s_set_disk | "Please insert a disk" (drive not ready) | op_disk_change 088D:1024; op_save 088D:12A2 |
| 05D5 | 31 | `ディスクへ書き込みができません` | s_write_protected | "Cannot write to the disk" | op_save 088D:1291 |
| 05F4 | 5 | `.dat` | s_dot_dat | save extension | op_save 088D:12D0 |
| 05F9 | 3 | `wb` | s_mode_wb | fopen mode (op_save, op_set_progress) | op_save 088D:12F3; op_set_progress 088D:1CDE |
| 05FC | 14 | `\e[7;31m%s\e[0m` | s_errbox_fmt | reverse red text (error_box) | error_box 088D:142F |
| 060A | 7 | `%s.dat` | s_save_fmt | save file name | op_load 088D:149B |
| 0611 | 29 | `データがセーブされていません` | s_not_saved | "No data has been saved" | op_load 088D:14C8 |
| 062E | 29 | `ゲームディスクではありません` | s_not_game_disk | "This is not a game disk" | op_load 088D:158B |
| 064B | 19 | `ディスクが違います` | s_wrong_disk | "Wrong disk" | op_load 088D:160E |
| 065E | 8 | `xen.gdt` | s_xen_gdt_w | progress file (op_set_progress) | op_set_progress 088D:1C5B; op_set_progress 088D:1CE2 |
| 0666 | 29 | `ファイルがオープンできません` | s_cannot_open | "Cannot open the file" | op_set_progress 088D:1C71 |
| 0683 | 15 | `　キャンセル　` | s_sys_cancel | system menu: "Cancel" | via `g_sysmenu_items` (DS:021C) |
| 0692 | 15 | `　最初に戻る　` | s_sys_restart | system menu: "Return to the beginning" | via `g_sysmenu_items` (DS:0220) |
| 06A1 | 15 | `　ゲーム終了　` | s_sys_quit | system menu: "Quit the game" | via `g_sysmenu_items` (DS:0224) |
| 06B0 | 27 | `　このままゲームを続ける。` | s_sys_continue | quit menu: "Continue the game." | via `g_quitconfirm_items` (DS:022C) |
| 06CB | 27 | `　本当にゲームを終了する。` | s_sys_reallyquit | quit menu: "Really quit the game." | via `g_quitconfirm_items` (DS:0230) |
| 06EA | 2 | ` ` | (g_name_insert) | initial value of g_name_insert | text_print 0B7D:022D |
| 06FF | 45 | `　　　　　　　　ゲーム終了後にお楽しみ下さい` | s_omake_locked | "Please enjoy this after finishing the game" (omake_locked_notice) | omake_locked_notice 0ABA:0030 |
| 072C | 6 | `menu2` | s_menu2 | omake menu image | omake_menu 0ABA:0108 |
| 0732 | 8 | `xen.gdt` | s_xen_gdt_r | progress file (game_main) | game_main 0ABA:022C |
| 073A | 3 | `rb` | s_mode_rb2 | fopen mode (game_main) | game_main 0ABA:0228 |
| 073D | 6 | `menu1` | s_menu1 | title menu image | game_main 0ABA:0278; game_main 0ABA:034B; game_main 0ABA:0370 |
| 0743 | 4 | `s00` | s_s00 | first prologue script | game_main 0ABA:02D0 |
| 0747 | 5 | `s00b` | s_s00b | prologue after DREAM_01 | game_main 0ABA:02F3 |
| 074C | 5 | `s00c` | s_s00c | prologue after DREAM_02 + a DREAM_03 end | game_main 0ABA:02F9 |
| 0751 | 2 | `l` | s_l | load-menu script L.CC | game_main 0ABA:0304 |
| 0753 | 2 | `k` | s_k | CG gallery script K.CC | game_main 0ABA:0324 |
| 0755 | 2 | `m` | s_m | music-mode script M.CC | game_main 0ABA:033B |
| 075F | 9 | `GAME END` | s_game_end | emergency-exit message (via g_msg_game_end) | via `g_msg_game_end` (DS:0758) |
| 076D | 11 | `XEN_SE.EFC` | s_xen_se_efc | sound-effect file (via g_se_file) | via `g_se_file` (DS:0768) |
| 0777 | 1 | `` | s_empty | "" (spawnl argv[0]); it is the terminating NUL of "XEN_SE.EFC" (string tail shared) **[I]** | snd_init 0B2C:00C1 |
| 0778 | 4 | `MMD` | s_mmd | MIDI driver signature (INT 61h vector+2) | snd_detect_mmd 0B2C:0020 |
| 077C | 4 | `PMD` | s_pmd | PMD signature (INT 60h vector+2) | snd_detect_pmd 0B2C:0052 |
| 0780 | 7 | `PUTCM6` | s_putcm6 | MIDI tone-upload program | snd_init 0B2C:00C5 |
| 0787 | 6 | `LITRA` | s_litra | its argument (tone bank) | snd_init 0B2C:00BD |
| 078D | 3 | `rb` | s_mode_rb3 | fopen mode (snd_init, music_load) | snd_init 0B2C:012B; music_load 0B2C:025D |
| 0790 | 35 | `効果音ファイルをオープンできません` | s_se_open_failed | "Cannot open the sound-effect file" | snd_init 0B2C:0188 |
| 07B3 | 5 | `%s.M` | s_music_fmt | PMD song file | music_load 0B2C:0240 |
| 07B8 | 7 | `%s.MMD` | s_mmd_fmt | MIDI song file | music_load 0B2C:024C |
| 07BF | 25 | `音色データーを転送中です` | s_tone_xfer | "Transferring tone data" | snd_msg_tone_xfer 0B2C:0510 |
| 07E4 | 11 | `Ａディスク` | (g_disk_names[0]) | "disk Ａ" (41-byte slots, also 080D Ｂ, 0836 Ｃ, 085F Ｄ, 0888 Ｅ) | prompt_insert_disk 0B7D:0817 |
| 080D | 11 | `Ｂディスク` | (g_disk_names[1]) | "disk Ｂ" | — |
| 0836 | 11 | `Ｃディスク` | (g_disk_names[2]) | "disk Ｃ" | — |
| 085F | 11 | `Ｄディスク` | (g_disk_names[3]) | "disk Ｄ" | — |
| 0888 | 11 | `Ｅディスク` | (g_disk_names[4]) | "disk Ｅ" | — |
| 08B1 | 14 | `\e[5;37m〈EB9F〉\e[0m` | s_page_cursor | blinking page-overflow cursor: SJIS EB9Fh = user character 7621h (Return-key symbol, DS:09B2) | text_print_big 0B7D:0161; text_print 0B7D:03F9 |
| 08BF | 19 | `数字ではない:\wｴﾗｰ` | s_not_digit | "not a digit: error" (0x04 code followed by a non-digit; printf) | text_print 0B7D:0305 |
| 08D2 | 14 | `\e[5;37m▼\e[0m` | s_end_cursor | blinking ▼ end-of-message cursor | text_print 0B7D:043D |
| 08E0 | 28 | `menu : 選択項目がありません` | s_menu_no_items | "menu: there are no items to choose" | select_menu 0B7D:049F |
| 08FC | 5 | `%-*s` | s_menu_item_fmt | left-justified option text (menu_draw_item) | menu_draw_item 0B7D:0635; menu_draw_item_near 0B7D:06E0 |
| 0901 | 31 | `をドライブ２へセットして下さい` | s_set_in_drive2 | "... please put into drive 2" (prompt_insert_disk) | prompt_insert_disk 0B7D:0823 |
| 0920 | 6 | `\e[>5l` | s_esc_cursor_on | PC-98 console: show text cursor | text_cursor_show 0B7D:0992 |
| 0926 | 6 | `\e[>5h` | s_esc_cursor_off | PC-98 console: hide text cursor | text_cursor_show 0B7D:0998 |
| 092C | 6 | `\e[>1l` | s_esc_fnkey_on | PC-98 console: show function-key row | fnkey_line_show 0B7D:0A14 |
| 0932 | 6 | `\e[>1h` | s_esc_fnkey_off | PC-98 console: hide function-key row | fnkey_line_show 0B7D:0A1A |
| 0938 | 3 | `\e*` | s_esc_cls | PC-98 console: clear screen (text_layer_clear) | text_layer_clear 0B7D:0A28 |
| 093B | 9 | `\e[%d;%dH` | s_esc_locate | cursor position (text_locate) | text_locate 0B7D:0A3E |
| 0976 | 25 | `メモリ確保に失敗しました` | s_alloc_failed_gfx | "Memory allocation failed" (gfx_alloc_buffers, gdt_read_file) | gfx_alloc_buffers 0C2A:0041; gdt_read_file 0C2A:03FB |
| 098F | 11 | `\nda_data..` | s_da_data | second line of the buffer-allocation error | gfx_alloc_buffers 0C2A:004D |
| 099A | 7 | `%s.gdt` | s_gdt_fmt | GDT file name | gdt_read_file 0C2A:0393 |
| 09A1 | 3 | `rb` | s_mode_rb4 | fopen mode (gdt_read_file) | gdt_read_file 0C2A:03A4 |
| 09A4 | 6 | `waku2` | s_waku2 | empty-CG frame image (fx_waku2_close) | fx_waku2_close 0C2A:081A |
| 09D4 | 25 | `メモリ確保に失敗しました` | s_alloc_failed_init | "Memory allocation failed" (check_free_memory, printed first) | check_free_memory 0E1A:0020 |
| 09ED | 42 | `\n約550KBytes程度の空きメモリが必要です。\n` | s_need_550kb | "About 550 KB of free memory is required." | check_free_memory 0E1A:002C |
| 0A17 | 35 | `\nDIP SW 2-8 を OFF にして下さい。\n` | s_dipsw | "Please set DIP switch 2-8 to OFF." (dead check_dipsw_gdc) | check_dipsw_gdc 0E1A:0059 |
| 0A3A | 48 | `GDC クロックが 5MHz での動作は保証されません。\n` | s_gdc_5mhz | "Operation with a 5 MHz GDC clock is not guaranteed." (dead) | check_dipsw_gdc 0E1A:0065 |
| 0A8E | 5 | `rogo` | s_rogo | logo image ROGO.GDT | opening_logo 0E31:03FB |
| 0A93 | 6 | `0100b` | s_0100b | opening image/animation 0100B | opening_title 0E31:04B7; opening_title 0E31:04E2 |
| 0A99 | 8 | `xen_03a` | s_xen_03a | opening music 1 | opening_title 0E31:0500 |
| 0AA1 | 8 | `xen_03b` | s_xen_03b | opening music 2 | opening_title 0E31:0540 |
| 0AA9 | 7 | `0100bb` | s_0100bb | opening image 0100BB | opening_title 0E31:0552 |
| 0AB0 | 7 | `xenon2` | s_xenon2 | title image XENON2.GDT | opening_title 0E31:0558 |
| 0AB7 | 5 | `st01` | s_st01 | staff roll image (via g_staff_names) | via `g_staff_names` (DS:0A6A) |
| 0ABC | 5 | `st02` | s_st02 | staff roll image (via g_staff_names) | via `g_staff_names` (DS:0A6E) |
| 0AC1 | 5 | `st03` | s_st03 | staff roll image (via g_staff_names) | via `g_staff_names` (DS:0A72) |
| 0AC6 | 5 | `st04` | s_st04 | staff roll image (via g_staff_names) | via `g_staff_names` (DS:0A76) |
| 0ACB | 5 | `st05` | s_st05 | staff roll image (via g_staff_names) | via `g_staff_names` (DS:0A7A) |
| 0AD0 | 5 | `st06` | s_st06 | staff roll image (via g_staff_names) | via `g_staff_names` (DS:0A7E) |
| 0AD5 | 5 | `st07` | s_st07 | staff roll image (via g_staff_names) | via `g_staff_names` (DS:0A82) |
| 0ADA | 5 | `st08` | s_st08 | staff roll image (via g_staff_names) | via `g_staff_names` (DS:0A86) |
| 0ADF | 5 | `st09` | s_st09 | staff roll image (via g_staff_names) | via `g_staff_names` (DS:0A8A) |
| 0AE4 | 9 | `xenon_02` | s_xenon_02 | staff-roll music | staff_roll 0E31:05E9 |
| 0AED | 5 | `st00` | s_st00 | first staff-roll image | staff_roll 0E31:0607 |
| 0B38 | 7 | `%s.adt` | s_adt_fmt | ADT file name | adt_load 0ED8:009E |
| 0B3F | 3 | `rb` | s_mode_rb5 | fopen mode (adt_load) | adt_load 0ED8:00AF |
| 0B42 | 22 | `ADT file is not fount` | s_adt_not_found | (sic "fount") ADT open failure | adt_load 0ED8:00CB |
| 0B58 | 25 | `メモリ確保に失敗しました` | s_alloc_failed_adt | "Memory allocation failed" (adt_load) | adt_load 0ED8:01BC |
| 0B71 | 11 | `buffer[%d]` | s_adt_buffer_fmt | printed with the frame index when a frame malloc fails | adt_load 0ED8:01C9 |
| 0B7E | 31 | `Abnormal program termination\r\n` | abortMSG | RTL abort() message (0000:026A) | abort 0000:026B |
| 0EC8 | 4 | `TMP` | s_tmp | RTL: TMP environment variable (tmpnam) | __mkname 0000:098A |
| 0ECC | 5 | `.$$$` | s_tmp_ext | RTL: temporary file extension | __mkname 0000:09AB |
| 0ED2 | 5 | `.COM` | s_com | RTL spawn: extension search | __searchpath_core 0000:0D60 |
| 0ED7 | 5 | `.EXE` | s_exe | RTL spawn: extension search | __searchpath_core 0000:0D8E |
| 0EDC | 5 | `PATH` | s_path | RTL spawn: PATH variable | __searchpath 0000:0E42 |
| 0EE4 | 7 | `(null)` | s_null | RTL printf "%s" of NULL | — |
| 11FE | 50 | `print scanf : floating point formats not linked\r\n` | _fpnotlinked_msg | RTL: floating-point printf/scanf stub message (0000:16C8) | — |
| 125C | 6 | `\e[>5l` | s_esc_cursor_on2 | RTL _setcursortype: show cursor | _setcursortype 0000:2795 |
| 1262 | 6 | `\e[>5h` | s_esc_cursor_off2 | RTL _setcursortype: hide cursor | _setcursortype 0000:278E |
| 1268 | 2 | `\r` | s_cr | RTL fputc text-mode CR | fputc 0000:3971 |

Non-referenced code-side strings: "Divide error\r\n" (DS:002F) is loaded with `mov dx,2Fh` by the
INT 00h handler 0000:015C; the float-stub message (DS:11FE) with `mov dx,11FEh` at 0000:16C8; "(null)"
(DS:0EE4) through the printf tables. The copyright banner DS:0004 is never referenced.

## 4. Pointer and constant tables (decoded) [V]

| DS: | Table | Decoded contents | Used by |
|---|---|---|---|
| 00B0 | `g_cc_errmsg` far `char*[16]` | DS:02CF, 02EE, 0318, 0340, 036C, 0398, 03BA, 03DF, 0402, 0428, 045D, 047E, 04B0, 04D4, 0503, 0537 (all 16 relocated) | `cc_error` 088D:0559 indexes `[bx+00B0]` |
| 01F2 | `g_disk_labels` `char[5][8]` | `_ADISK_`, `_BDISK_`, `_CDISK_`, `_DDISK_`, `_EDISK_` | initialiser image copied by `op_load` 088D:144C (label check of a floppy save) |
| 021C | `g_sysmenu_items` far `char*[4]` | DS:0683 「　キャンセル　」, DS:0692 「　最初に戻る　」, DS:06A1 「　ゲーム終了　」, NULL | initialiser image, `cc_system_menu` 088D:2248 → `select_menu` |
| 022C | `g_quitconfirm_items` far `char*[3]` | DS:06B0 「　このままゲームを続ける。」, DS:06CB 「　本当にゲームを終了する。」, NULL | initialiser image, `cc_system_menu` 088D:228A |
| 06F7 | `g_title_menu_rows` `int[3]` | 176, 224, 272 (y of the 32-line highlight bar, indexed by the menu row) | `title_menu_hilite` 0ABA:0056 |
| 07E4 | `g_disk_names` `char[5][41]` | 「Ａディスク」 … 「Ｅディスク」 at 07E4, 080D, 0836, 085F, 0888 | initialiser image, `prompt_insert_disk` 0B7D:0808 → 「Ｘディスク」+「をドライブ２へセットして下さい」 |
| 0944 | `g_gfxbuf_raw` far `void*[4]` | (runtime) calloc results | 0C2A |
| 0956 | `g_vram_planes` far `void*[4]` | A800:0000, B000:0000, B800:0000, E000:0000 | `gfx_merge_page1` 0C2A:01F9 |
| 0966 | `g_dissolve_order` `byte[16]` | 0, 10, 4, 14, 8, 2, 12, 6, 13, 7, 1, 11, 5, 15, 9, 3 | `fx_dissolve_cell` 0C2A:087B |
| 09AA | `g_plane_segs` `unsigned[4]` | A800h, B000h, B800h, E000h | font renderer 0F16:0369 / 0F16:03A0 |
| 09B2 | `g_gaiji_7621` `byte[34]` | see below | `engine_init` 0E1A:00DD → 0F16:0841 (INT 18h AH=1Ah, DX = 7621h, BX:CX = DS:09B2) |
| 0A6A | `g_staff_names` far `char*[9]` | DS:0AB7 "st01" … DS:0ADF "st09" (stride 5) | initialiser image, `staff_roll` 0E31:05D2 |
| 0B14 | `g_blink_table` `int[14]` | 0, 1, 1, 6, 3, 300, 1, 6, 3, 3, 1, 6, 3, 300 | `blink_tick` 0ED8:0270 (graphics.md §3.5) |
| 0CA2–0CAD | RTL exit hooks | three far pointers, all 0000:066B (`retf`) until stdio replaces them | `exit` path 0000:066C |
| 0CAE | `_streams[20]` | stdin/stdout/stderr/stdaux/stdprn + 15 free FILEs (20 bytes each) | stdio |
| 1276 | `_INIT_` | `{0,2,0EF5}`, `{0,10h,22FF}`, `{0,10h,2464}`, `{0,10h,263D}` (type, priority, near offset in seg 0000) | C0 0000:01E1 |

The user-defined character **7621h** (Shift-JIS **EB9Fh**) defined from DS:09B2 is exactly the glyph
the message printer puts at text position (73,24) when a message overflows four rows
(`s_page_cursor`, DS:08B1, blinking). The 34-byte buffer is a 2-byte header (00 00) followed by
16 rows of 16 bits **[V]**:

```
................      row 0-2 empty
.###########....      7FF0
#...........#...      8008
#........##.#...      8068   (x5)
#....#...##.#...      8468
#...##...##.#...      8C68
#..########.#...      9FE8
#.########..#...      BFC8
#...........#...      8008
.###########....      7FF0
```

i.e. a rounded key cap with a "Return" arrow (⏎, closest Unicode: U+23CE). The end-of-message prompt
(DS:08D2) uses the ordinary ▼ (SJIS 81A5h).

Code-segment tables that are sometimes mistaken for DGROUP data (not part of this map): kinsoku table
0B7D:046C, opcode value/handler tables 088D:094C / 088D:09B6, scan-code switch 0AF8:01D4, the GDT
header copy at 0456:008C and plane segments 0456:0015–001B (CS-relative), and all data of the dead LALF driver
(0C2A:0A1A–19FF, addressed with DS = CS).

## 5. What the save file actually captures [V]

`op_save` writes the flag block and three blocks of fixed size starting at a name buffer; only the
first part of each block is meaningful, the rest is whatever follows in BSS:

| File block | DGROUP bytes | Contents |
|---|---|---|
| flags (256 B) | DS:00F2–01F1 | `g_flags` |
| script name (40 B) | DS:137E–13A5 | `g_cur_script` (40-byte buffer, exact) |
| page (2 B) | DS:1294 | `g_cur_page` |
| music block (40 B) | DS:13C8–13EF | `g_music_name` name (15) · `g_music_started` (2) · 13D9–13DB (incl. the dead `g_vsync_spin_count`) · `g_gfxbuf` far pointers (16, load-address dependent) · first 4 bytes of `g_gdt_palette` |
| graphics block (40 B) | DS:1422–1449 | `g_gfx_state` name (15) · `g_p1_off/wbytes/h` (6) · 1 pad byte · first 18 bytes of `g_pal_shadow` (colours 0–5) |

The loader uses only the two names (as strings); everything after the NUL is ignored. Nothing else in
DGROUP persists across a save/load, and `g_progress` lives in `XEN.GDT` instead.

## 6. Note on the Borland RTL rows

The C0 block (DS:0000–0071) follows Borland C++ 3.x `C0.ASM` for far-data models exactly
(`_Int0Vector`…`_Int6Vector`, `_C0argc/_C0argv/_C0environ`, `_envLng/_envseg/_envSize`, `_psp`,
`_osversion`, `errno`, heap pointers), confirmed by the accesses in 0000:0000–0171. The library block
DS:0B7E–1275 and the RTL BSS (2166–229D) were identified from their access patterns
(`_ctype` `[c+0BA1]`, `_streams` 20-byte FILEs with self tokens, `_nfile` = 20 tested by C0,
`_openfd`, `_dosErrorToSV` used by `__IOerror`, `_atexittbl` used by `atexit`/`exit`, `_mbctype`
for Shift-JIS, the PC-98 conio state built by the init routine 0000:263D). Field-level names inside
`_streams`, the conio struct, the spawn scratch buffers and the zenkaku/hankaku tables are **[I]**;
names follow the RTL analysis (`rtl-globals.tsv`), which is authoritative for them. The four RTL init routines reached only through the `_INIT_` table — 0000:0EF5 `_setupio` (initialises `_openfd[5..]` and FILEs 5..19), 0000:22FF `_setargv`, 0000:2464 `_setenvp` (malloc'd environment → DS:1244) and 0000:263D `_crtinit` (PC-98 conio init) — are not reached by recursive descent from the entry point; they are traced only because they are now seeded by name.

## 7. Corrections to other documents (resolved from the code)

Status: rows 1–10 and 13 have been applied to the documents and name files by the audit pass;
rows 11 and 14 concern annotations generated by `re-tools/agsdis.py` and are documented in
functions.md §3 instead; row 12 needs no change.

| # | Document / file | Statement | Resolution |
|---|---|---|---|
| 1 | engine.md §1.1 | DGROUP "0x232C … initialised data 0x0000–0x128B, BSS 0x128C–0x232B, then stack" | File data ends at 128B, but the `_INIT_` table runs to 128D; **BSS is DS:128E–229D** (C0 0000:00B5–00BE); the stack is a separate segment SS = 11C9 = DS:22A0, size 1010h (to DS:32AF); 232C is only the header's `minalloc` end, superseded by C0 (§1.1) |
| 2 | engine.md §2.4 step 2 | threshold 0x6C278 "(442 984 bytes)" and a single message | 0x6C278 = **443 000** bytes (platform.md is right); `check_free_memory` prints **two** strings: 「メモリ確保に失敗しました」 (DS:09D4) then 「約550KBytes程度の空きメモリが必要です。」 (DS:09ED), 0E1A:001F–0034 |
| 3 | engine.md §3 | "missing file → keep DS:06FD value (0)" | A missing XEN.GDT skips the read (0ABA:023F `je 0272`), so `g_progress` keeps its **previous** value (0 at start-up). DS:06FD is only the initialiser of the local read buffer, used when the file exists but is empty |
| 4 | engine.md §3 | "`g_leave_script = DS:13AA = 0`" | `g_leave_script` is **DS:13A6**; game_main zeroes 13A6 and a separate write-only word DS:13AA together (0ABA:03A6/03A9) |
| 5 | names-core.tsv | `0F9F:021A g_xen_default` "default XEN.GDT value if file missing" | DS:021A is the initialiser of `op_set_progress`'s local byte (088D:1C50); if the file is missing the opcode shows 「ファイルがオープンできません」 and returns (088D:1C70); the value 0 is used only for an empty file |
| 6 | platform.md §3.2 | "(names-core.tsv calls DS:13B6 `g_menu_wrap` … inverted)" | Already fixed in the name files: names.tsv now has `g_input_from_mouse` (names-sound.tsv); names-core.tsv no longer names 13B6. The parenthetical can be dropped |
| 7 | graphics.md §6 | "DS:13EC (named `g_fade_params` in names-core.tsv)" | Outdated: names-core.tsv now uses `g_gdt_palette`, matching graphics.md |
| 8 | sound.md §3, §6 | calls DS:13C8 `g_music_name` | names-core.tsv called it `g_music_state`; renamed to `g_music_name` so that names.tsv, sound.md, engine.md and this file agree |
| 9 | README.md (defects), engine.md §6, scripts.md §5.1 | page cursor glyph EB9Fh is "PC-98-only, no Unicode/cp932 equivalent" | EB9Fh is the user-defined character **7621h that the engine itself defines** at start-up from DS:09B2 (`engine_init` 0E1A:00DD → 0F16:0841, INT 18h AH=1Ah): a Return-key symbol (§4). A port can draw that bitmap or use U+23CE |
| 10 | engine.md §6 | "Shift-JIS lead byte (ctype table DS:0F4D bit 2)" | The table is the RTL's **`_mbctype`** at DS:0F4C (indexed `[c+0F4D]`), distinct from `_ctype` at DS:0BA0; bit 2 = lead byte, bit 3 = trail byte |
| 11 | ags.asm annotations | inside 0C2A:0A1A–19FF, operands such as `[0x21a]` are annotated `g_xen_default`, `[0x21c]` `g_sysmenu_items`, `0x238` `s_dot_cc` etc. | That dead driver sets DS = CS (0C2A:0FAA, 1017, 1111); these are offsets into its own segment, not DGROUP. The annotations are spurious |
| 12 | save-format.md §4 | "the remaining 39 bytes of each block are unrelated state" | Correct; exact contents are in §5 (music block also carries the first 4 bytes of `g_gdt_palette`; graphics block the page-1 rect and palette-shadow colours 0–5) |
| 13 | engine.md §2.4 step 3 vs platform.md §2.1 | engine.md: 0000:2779/0000:2953 "console/ctrl-break setup, inferred" | platform.md is right: `_setcursortype` 0000:2779 emits DS:1262 "\e[>5h" / DS:125C "\e[>5l"; 0000:2953 sends `ESC [ > 1 h` (CS-resident bytes at 0000:2934 + 'h') through INT DCh CL=10h (0000:2928). No ctrl-break handling |
| 14 | ghidra-seed names | `0F9F:0128`, `0F9F:0100`, `0F9F:00F0` appear as "&g_…" annotations on `push 0xf0` / `push 0x128` etc. | Immediate constants (sizes, line counts), not addresses; ignore those annotations (e.g. adt_load 0ED8:0126 `push 0xf0` = 240 bytes) |

## 8. Open questions

* DS:0064 (initialised to 229Eh = `edata@`): exact Borland symbol; no reference found in the traced code.
* DS:168E–1791 (260 bytes), DS:0AF6–0AF9, 0B0A, 0B0E–0B11, 13D9, 13DB, 1674: no reference
  in the recursive-descent listing; may be used by untraced RTL code or be dead statics.
* Ownership of DS:09AA (plane-segment table used by 0F16) — placed between the 0C2A and 0E1A data,
  most likely defined in the 0DCA display library **[I]**.
* `g_name_insert` size (13 bytes assumed from the layout).
