# AGS.EXE function index

Every function of `AGS.EXE` with its address, name, module, size, number of direct callers, a
one-line purpose and the document that describes it. Generated from the shared listing
(`docs/re/listing/ags.asm`, `funcs.tsv`, `names.tsv`; the first two are made locally, README.md
"Regenerating") after the audit pass; **all 472 functions are
named** (the listing grew from 444 to 472 functions because the audit named 28 entry points that
recursive descent had not reached — dead library and engine code, see §3).

Conventions: `SEG:OFF` in the shared-listing convention (file offset = `0xE00 + SEG*16 + OFF`);
"size" = bytes from the entry to the end of the last instruction of the listing block (shared tails
and jump tables placed after a function are not counted); "callers" = direct `call`/`lcall` sites
found by the disassembler (0 for functions reached only through vectors, function pointers or the
startup table, and for dead code). Purposes are the `names.tsv` comments, shortened; **[V]/[I]**
markers are omitted here — see the comments in `names.tsv` and the subsystem documents.

## 1. Functions per segment

| Seg | Module | Functions | Document |
|---|---|---:|---|
| 0000 | C0 startup + Borland C++ 3.0 RTL (Japanese PC-98 edition) | 194 | this file §2 |
| 0450 | `main` | 1 | engine.md §2.3 |
| 0456 | GDT decoder (hand-written asm) | 15 | graphics.md §2 |
| 0843 | drive/disk/file layer | 10 | engine.md §5 |
| 088D | script loader, interpreter, opcode handlers, save/load | 73 | scripts.md, opcodes.md, save-format.md |
| 0ABA | title screen, omake, run loop | 7 | engine.md §3 |
| 0AF8 | keyboard and input abstraction | 13 | platform.md §3 |
| 0B2C | PMD music / sound-effect interface | 17 | sound.md §1–3 |
| 0B7D | message printer, menus, text layer, GDC scroll | 21 | engine.md §6–7 |
| 0C2A | GDT loading, buffers, effects (0000–0A19); dead "MUSIC LALF" FM driver (0A1A–19FF) | 36 | graphics.md §3; sound.md §1.2 |
| 0DCA | display / palette / VSYNC library | 26 | platform.md §6, graphics.md §3 |
| 0E1A | init, shutdown, startup checks | 4 | engine.md §2 |
| 0E31 | logo, opening, staff roll | 9 | engine.md §4 |
| 0E9B | bus-mouse driver | 12 | platform.md §3.4 |
| 0ECB | VSYNC interrupt | 5 | platform.md §4.2 |
| 0ED8 | ADT player, eye-blink animator | 7 | graphics.md §3.5–3.6 |
| 0F16 | kanji-ROM / ANK glyph drawing | 22 | font.md |
| **total** | | **472** | |

## 2. Segment 0000: Borland C++ runtime

The whole segment is library code; **no engine code lives in it** (**verified** by reading every
function). The library is Borland C++ 3.0 (1991; TLINK 5.0 signature in the MZ header, engine.md §1)
in the large model, in its **Japanese PC-98 edition**: conio works through `ESC[>` sequences and
`INT DCh`, `delay` uses PC-98 timing sources, `sound` drives the PC-98 8253/beeper, and the Shift-JIS
helpers `chkctype`, `nthctype`, `hantozen` and `zentohan` are present; `_setargv`, `fnsplit` and the
`searchpath` family are Shift-JIS aware. Names follow the Borland sources; helpers whose source name
is not certain are marked [I] in `names.tsv` (`__searchpath_*`, `__fopen_mode`, `__fgetn`,
`__fbufcnt`, `__flushterm`, `__brk_setblock`, `ReallocMove`).

### 2.1 Module layout

| Range | Borland module | Contents |
|---|---|---|
| 0000–0269 | c0.asm | `_c0_startup` (entry), `__cleanup`, `__checknull`, `__terminate`, `ZeroDivision` (INT 0 handler), SaveVectors, `__restorezero`, Initialize/Cleanup (run the `#pragma startup` table DS:1276–128D); 0266 = CS copy of DGROUP |
| 026A–02FB | abort, atexit, chdrive | `abort`, `atexit`, `_chdrive`, `_getdrive` |
| 02FC–0560 | spawn helpers | `__DOScmd` (command tail), `__DOSenv` (environment block) |
| 0561–066A | DOS wrappers | `_dos_getfileattr/_dos_setfileattr`, `findfirst/findnext`, `_dos_getdrive/_dos_setdrive`, `getvect/setvect`, `_ErrorMessage` |
| 066B–0719 | exit.c, h_scopy | exit hooks, `__exit`, `exit`, `_exit`, `_cexit`, `_c_exit`; `SCOPY@` |
| 071A–0842 | getdisk, helpers | `getdisk/setdisk`, `_dos_getvect/_dos_setvect` (byte-identical second copy), `LXLSH@`, `PADD@/PSUB@/PSBP@`, `N_SPUSH@/F_SPUSH@` |
| 0843–0A86 | ioerror, longtoa, … | `__IOerror/__DOSerror`, `isatty`, `__longtoa/__utoa`, `lseek`, `__mkname/__tmpnam`, `N_LXMUL@`, `N_PCMP@`, `_read` |
| 0A87–0EF4 | searchpath (J) | `__searchpath*`, `_searchenv`, `_searchstr`, `segread` |
| 0EF5–0FF2 | setupio, toupper, unlink | `_setupio` (startup prio 2), `toupper`, `unlink` |
| 0FF3–14CD | vprinter.asm | `__vprinter` (printf engine) + fragments; jump table 149E–14CD |
| 14CE–16E6 | stpcpy, jctype (J), calloc | `stpcpy`, `chkctype`, `nthctype`, `hantozen`, `zentohan` body (15D5), `calloc`, `coreleft` thunk, "floating point formats not linked" stubs |
| 16E7–1A88 | exec.asm | `__exec` P_OVERLAY loader stub (16E7–1846, copied to the top of memory; INT 21h AX=4B03h at 17E6; its zero-filled buffers are the 930 "zero bytes" in this segment), `_exec` (184E); 1A7D–1A88 far-heap variables in CS |
| 1A89–1ED7 | farheap.asm | free-list management, `farfree`, `CreateHeap`, `ExtendHeap`, `malloc` (1CF0), `farmalloc` (1CFA), `realloc`, `farrealloc` |
| 1ED8–2080 | brk, farcore, getch | `__brk`, `__sbrk`, `farcoreleft` (2018), `getch` |
| 2081–2263 | int86, ioctl, kbhit, loadprog | `int86`, `int86x`, `intr`, `ioctl`, `kbhit` (dead), `_LoadProg` |
| 22F9–263C | realcvt, setargv (J), setblock, setenvp, setjmp, spawn | `_setargv` (startup), `setblock`, `_setenvp` (startup), `setjmp/longjmp`, `_spawn` (P_WAIT), `spawnl` |
| 263D–29CB | PC-98 conio | `_crtinit` (startup), cursor helpers, `_setcursortype`, `delay` (GDC-status or ARTIC-counter loops), function-key row on/off, `sound/nosound` |
| 29CC–3605 | file I/O, stdio | `_chmod`, `close`, `eof`, `fclose`, `fflush`, `filelength`, `flushall`, `fnsplit`, `fopen`, `fprintf`, `fread`, `fseek/ftell`, `fwrite`, `_fgetc/fgetc`, `getchar`, `getenv`, `memcpy/memset/movmem/memmove` |
| 3606–3E19 | open, printf, fputc, puts, read, setvbuf | `open/_open`, `printf`, `_fputc/fputc/putchar`, `__fputn`, `puts` (3BD4), `read`, `setvbuf` |
| 3E1A–414B | fnsplit.cas (J) | `_fnsplit` (+ switch tables 3EEE, 4106), second `fnsplit` wrapper (4128, dead) |
| 414C–4504 | sprintf, string, write, exit hooks | `sprintf/vsprintf`, `strcat`, `strcpy`, `stricmp` (4237), `strlen`, `strncmp`, `strncpy`, `write/_write`, `_xfclose/_xfflush`, `zentohan` far entry (4502) |

### 2.2 Facts about the library that matter to the engine (**verified**)

* `0000:4237` is **`stricmp`** (folds `a`–`z`), not `strcmp`: the disk-label checks (`op_disk_change`,
  `op_load`), the script-name check in `op_load` and the same-song test in `music_load` are
  case-insensitive.
* `0000:1CF0` is **`malloc(size_t)`** (= `farmalloc(0:size)`); `farmalloc(unsigned long)` is 0000:1CFA.
  Every block is returned as `seg:0004` (4-byte header at `seg:0000`), so all loaded files start at
  an even address.
* `0000:214B` is **`intr(int, struct REGPACK far *)`**, `0000:20B2` is `int86x`, `0000:2081` `int86`.
* `0000:3BD4` is **`puts`**; `0000:06FE` is `SCOPY@` (struct copy) and `0000:0822` `F_SPUSH@` (push a
  struct by value — used to pass the 48-byte palette to `pal_fade_to`).
* `0000:16C2` (`coreleft`, called by `check_free_memory`) is a thunk to `farcoreleft` 0000:2018.
* `0000:4502` is **`zentohan`** (full-width → single-byte), used by `op_disk_change` for `Ｂ` → `B`.
* The text printer's lead-byte test "`DS:0F4D` bit 2" is the Japanese RTL's `_mbctype` table
  (DS:0F4C/0F4D); Borland's ordinary `_ctype` is at DS:0BA0.
* Startup functions run through the `#pragma startup` table at DS:1276–128D (4 records):
  `_setupio` 0EF5, `_setargv` 22FF, `_setenvp` 2464, `_crtinit` 263D. The spawners `_exec` 184E and
  `_spawn` 252A are reached through near function pointers from `spawnl`.
* Library globals (streams, heap, errno, `_mbctype`, …) are listed in [globals.md](globals.md).

### 2.3 Data inside the code segment (not functions)

| Address | Content |
|---|---|
| 0000:0266 | CS-resident copy of DGROUP (written by C0 at 0000:0003); 0268–0269 unused |
| 0000:149E–14CD | `__vprinter` jump table, 24 near pointers |
| 0000:16E7–1846 | `__exec` overlay-loader stub with its zero-filled buffers (only used by `spawnl(P_OVERLAY)`; the engine uses only `P_WAIT`) |
| 0000:1A7D–1A88 | far-heap variables `__first`, `__last`, `__rover`, saved DS, realloc size |
| 0000:22FD | saved DS for `_setargv` |
| 0000:2522–2529 | unreachable tail of `longjmp`; CS words used by `_spawn` |
| 0000:2934–2938 | `"ESC[>1"` used by `__fnkey_esc` |
| 0000:3EEE–3EFD, 4106–4127 | `fnsplit` switch tables |
| 0000:1E89 | not a function: a Ghidra seed in the middle of an instruction of `farrealloc` (left as a weak seed) |

## 3. Code without callers

Besides interrupt handlers (`isr_null` 0AF8:000A, `isr_mouse` 0E9B:009C, `isr_vsync` 0ECB:0001), the
VSYNC callback `blink_tick` 0ED8:0270 and RTL entries reached through tables, the following engine
functions are **never called** (dead code, **verified** by the disassembler's cross-reference table;
indirect use through a function pointer was not excluded exhaustively):

| Address | Name | What it would do |
|---|---|---|
| 0456:003E | `gdt_set_planes_vram` | point the GDT decoder at VRAM A800/B000/B800/E000 |
| 0456:3EBD | `norm_ds_si` | normalise DS:SI |
| 0843:026E | `find_drive_by_label` | find the drive holding a given volume label |
| 0AF8:02B9 | `kbd_shift_held` | BIOS shift state bit 0 |
| 0B2C:019B | `snd_clear_efc_hook` | PMD AH=14h with a NULL hook |
| 0B7D:06D2 | `menu_draw_item_near` | near copy of `menu_draw_item` |
| 0B7D:08A5 | `pal_set_all_black` | `pal_set_all(0…)` |
| 0B7D:08EA | `gdt_unpack_palette_2` | copy of `gdt_unpack_palette` |
| 0B7D:0988 / 0A0A | `text_cursor_show` / `fnkey_line_show` | `ESC[>5l/h`, `ESC[>1l/h` |
| 0B7D:0A4B | `gdc_vsync_spin` | spin on graphic-GDC VSYNC counting in DS:13DA |
| 0C2A:0A1A–19FF | `lalf_*` | the whole embedded "MUSIC LALF" YM2203 driver (far entries `lalf_start` 0FA3, `lalf_set_volume` 10BD, `lalf_set_param` 10FC, `lalf_tick` 1108, `lalf_set_channel_flag` 1151) |
| 0DCA:008B/00A5/00F3/0174/0196/023A/0288 | `text_on`, `text_off`, `gdc_mode_4a_16`, `set_pages_vsync`, `get_pages`, `pal_get_rgb`, `pal_set_all_but8` | display-library entries never used |
| 0E1A:0041 | `check_dipsw_gdc` | DIP-switch / GDC-clock warning (engine.md §2.4) |
| 0E9B:0273 / 02C8 | `mouse_get_buttons` / `mouse_set_limits` | mouse API entries never used |
| 0ECB:00B5 | `vsync_get_ticks` | read the 32-bit VSYNC counter (so `g_vsync_ticks` is write-only) |

Plus 37 seg-0000 library functions with no direct caller (dead library entries such as `kbhit`,
`getchar`, `farrealloc`, `ftell`, or functions reached through tables/pointers).

Note for readers of the listing: inside the dead LALF driver (0C2A:0A1A–19FF) DS = CS, so the
listing's DGROUP annotations on its memory operands (e.g. `; g_xen_default` on `[0x21a]`) are
spurious. Likewise a few `push imm` annotations elsewhere are coincidental (e.g. `push 0xb0 ;
&g_cc_errmsg` at 0E31:045D is the height 176, `push 0xf0 ; &g_menu_cursor` at 0E31:0662 is 240 frames).

## 4. Name consistency

* Every address in `funcs.tsv` has exactly one name in the merged `names.tsv` (the canonical name file;
  it was originally merged from per-area working files `names-core/gfx/script/sound/audit.tsv`, which
  agreed wherever they overlapped: 49 addresses were named in two files, always identically).
* The audit corrected these earlier names: `int86x_like` → `intr`, `perror_like` → `puts`,
  `N_SCOPY_far` → `SCOPY@`, `N_SCOPY@` → `F_SPUSH@`, `farmalloc` (0000:1CF0) → `malloc`, `strcmp` →
  `stricmp`, `farcoreleft` (0000:16C2) → `coreleft`, `pc98_esc_seq` → `__fnkey_esc`;  Stale mentions in the documents (`g_menu_wrap`, `g_fade_params`,
  `farmalloc`, `perror_like`, `strcmp`) were updated.
* Every `` `name` SEG:OFF `` pair in the documents was checked against `names.tsv`; the remaining
  mismatches are call sites inside a function (e.g. "`text_print` 0B7D:01FF"), which are intended.

## 5. Full table

| Address | Name | Seg / module | Size | Callers | Purpose | Doc |
|---|---|---|---:|---:|---|---|
| 0000:0000 | `_c0_startup` | 0000 RTL | 319 | 0 | C0 entry: DGROUP@@, INT21/30 version, PSP/env/heaptop, SaveVectors, env scan, _stklen min 200h, AH=4Ah shrink, zero BSS 128E-229D, _nfile>20 -> … | functions.md §2 |
| 0000:013F | `__cleanup` | 0000 RTL | 19 | 1 | run #pragma exit table (ExitStart..ExitEnd = 128E..128E, empty) via Cleanup c0.asm | functions.md §2 |
| 0000:0152 | `__checknull` | 0000 RTL | 1 | 1 | empty (retf) in far-data models c0.asm | functions.md §2 |
| 0000:0153 | `__terminate` | 0000 RTL | 9 | 1 | INT 21h AH=4Ch AL=status c0.asm | functions.md §2 |
| 0000:015C | `ZeroDivision` | 0000 RTL | 21 | 0 | INT 0 handler (installed by SaveVectors, cs:015C): _ErrorMessage("Divide error\r\n" DS:002F), _exit(3) not in listing as function (inside __terminate) | functions.md §2 |
| 0000:0171 | `SaveVectors` | 0000 RTL | 67 | 1 | near: save INT 0/4/5/6 into DS:003E/0042/0046/004A, INT 0 -> ZeroDivision c0.asm | functions.md §2 |
| 0000:01B4 | `__restorezero` | 0000 RTL | 45 | 1 | restore INT 0/4/5/6 from DS:003E..004D c0.asm | functions.md §2 |
| 0000:01E1 | `Initialize` | 0000 RTL | 68 | 3 | near: run _INIT_ records (es:si..di, 6 bytes {type 0=near/1=far, prio, off, seg}) in ascending priority, mark FFh c0.asm | functions.md §2 |
| 0000:0225 | `Cleanup` | 0000 RTL | 65 | 3 | near: run _EXIT_ records in descending priority c0.asm | functions.md §2 |
| 0000:026A | `abort` | 0000 RTL | 23 | 5 | _ErrorMessage("Abnormal program termination\r\n" DS:0B7E), _exit(3); also c0's out-of-memory exit | functions.md §2 |
| 0000:0281 | `atexit` | 0000 RTL | 45 | 0 | register far fn in DS:2166[32], count DS:0B9E; returns 1 when full no callers | functions.md §2 |
| 0000:02AE | `_chdrive` | 0000 RTL | 53 | 0 | _dos_setdrive(d) then _dos_getdrive; 0 if it took effect else -1 no callers | functions.md §2 |
| 0000:02E3 | `_getdrive` | 0000 RTL | 25 | 1 | returns current drive 1-based via _dos_getdrive | functions.md §2 |
| 0000:02FC | `__DOScmd` | 0000 RTL | 259 | 1 | near pascal (char far **argv): build DOS command tail (len byte, ' '-separated, CR) in farmalloc'd buffer, max 7Fh spawn module | functions.md §2 |
| 0000:03FF | `__DOSenv` | 0000 RTL | 354 | 1 | near pascal (envp, pathname, void far **envsave): build paragraph-aligned env block + word 1 + program path, <2000h bytes spawn module | functions.md §2 |
| 0000:0561 | `_dos_getfileattr` | 0000 RTL | 32 | 1 | INT 21h AX=4300h, *attrib=CX; error -> __DOSerror | functions.md §2 |
| 0000:0581 | `_dos_setfileattr` | 0000 RTL | 29 | 0 | INT 21h AX=4301h no callers | functions.md §2 |
| 0000:059E | `findfirst` | 0000 RTL | 51 | 4 | INT 21h 2Fh/1Ah/4Eh/1Ah (DTA swap) | functions.md §2 |
| 0000:05D1 | `findnext` | 0000 RTL | 45 | 0 | INT 21h 4Fh with DTA swap no callers | functions.md §2 |
| 0000:05FE | `_dos_getdrive` | 0000 RTL | 18 | 4 | INT 21h AH=19h, *drive = AL+1 | functions.md §2 |
| 0000:0610 | `_dos_setdrive` | 0000 RTL | 22 | 2 | INT 21h AH=0Eh DL=drive-1, *ndrives = AL | functions.md §2 |
| 0000:0626 | `getvect` | 0000 RTL | 15 | 2 | INT 21h AH=35h (identical copy at 0753; which object is which is arbitrary) | functions.md §2 |
| 0000:0635 | `setvect` | 0000 RTL | 17 | 4 | INT 21h AH=25h (identical copy at 0762) | functions.md §2 |
| 0000:0646 | `_ErrorMessage` | 0000 RTL | 37 | 2 | _write(2, msg, strlen(msg)) used by abort/ZeroDivision | functions.md §2 |
| 0000:066B | `_dummy_exithook` | 0000 RTL | 1 | 0 | retf; initial value of _exitbuf/_exitfopen/_exitopen (DS:0CA2/0CA6/0CAA = 0000:066B) | functions.md §2 |
| 0000:066C | `__exit` | 0000 RTL | 87 | 4 | near (errcode,[bp+6]=dontexit,[bp+8]=quick) ret 6: !quick -> atexit fns (reverse), __cleanup, (*_exitbuf)(); always __restorezero, __checknull; … | functions.md §2 |
| 0000:06C3 | `exit` | 0000 RTL | 15 | 5 | __exit(status, 0, 0) | functions.md §2 |
| 0000:06D2 | `_exit` | 0000 RTL | 18 | 2 | __exit(status, 0, quick=1) | functions.md §2 |
| 0000:06E4 | `_cexit` | 0000 RTL | 14 | 1 | __exit(0, dontexit=1, 0) no callers | functions.md §2 |
| 0000:06F2 | `_c_exit` | 0000 RTL | 12 | 0 | __exit(0, 1, 1) not in listing | functions.md §2 |
| 0000:06FE | `SCOPY@` | 0000 RTL | 28 | 11 | far struct copy: cx bytes from [bp+6] src to [bp+0Ah] dest, retf 8 (Borland large-model F_SCOPY@); was N_SCOPY_far | functions.md §2 |
| 0000:071A | `getdisk` | 0000 RTL | 26 | 1 | _dos_getdrive - 1 (0-based) | functions.md §2 |
| 0000:0734 | `setdisk` | 0000 RTL | 31 | 1 | _dos_setdrive(d+1), returns number of drives | functions.md §2 |
| 0000:0753 | `_dos_getvect` | 0000 RTL | 15 | 4 | INT 21h AH=35h (second copy of getvect) | functions.md §2 |
| 0000:0762 | `_dos_setvect` | 0000 RTL | 17 | 4 | INT 21h AH=25h (second copy of setvect) | functions.md §2 |
| 0000:0773 | `N_LXLSH@` | 0000 RTL | 33 | 1 | near-call entry (pop bx/push cs/push bx) of LXLSH@ (0776): long dx:ax <<= cl | functions.md §2 |
| 0000:0794 | `N_PADD@` | 0000 RTL | 47 | 3 | near entry of PADD@ (0797): huge ptr dx:ax + long cx:bx, normalised (seg += carry*1000h, off &= 0Fh) | functions.md §2 |
| 0000:07C3 | `N_PSUB@` | 0000 RTL | 49 | 0 | near entry of PSUB@ (07C6): huge ptr dx:ax - long cx:bx (negates and shares PADD@ code) not in listing | functions.md §2 |
| 0000:07F4 | `N_PSBP@` | 0000 RTL | 40 | 1 | near entry of PSBP@ (07F7): huge ptr difference dx:ax - cx:bx as long | functions.md §2 |
| 0000:081C | `N_SPUSH@` | 0000 RTL | 6 | 0 | near entry of SPUSH@: push cx-byte struct from dx:ax onto the stack not in listing | functions.md §2 |
| 0000:0822 | `F_SPUSH@` | 0000 RTL | 33 | 15 | far: copy cx bytes from dx:ax into a stack hole below the return address (pass struct by value, e.g. pal_fade_to's 48-byte palette); was N_SCOPY@ | functions.md §2 |
| 0000:0843 | `__IOerror` | 0000 RTL | 57 | 21 | pascal(doserr): map DOS error (or -errno) via _dosErrorToSV DS:0E6E, set _doserrno DS:0E6C and errno DS:0062, return -1 | functions.md §2 |
| 0000:087C | `__DOSerror` | 0000 RTL | 18 | 4 | pascal(doserr): __IOerror(doserr), return doserr | functions.md §2 |
| 0000:088E | `isatty` | 0000 RTL | 17 | 3 | IOCTL AX=4400h, DX & 80h | functions.md §2 |
| 0000:089F | `__longtoa` | 0000 RTL | 130 | 2 | pascal (long val, char far *buf, int radix 2..36, char maybeSigned, char hexStyle): ascii conversion, returns buf | functions.md §2 |
| 0000:0921 | `__utoa` | 0000 RTL | 32 | 1 | pascal (unsigned, buf): __longtoa(v, buf, 10, 0, 'a') | functions.md §2 |
| 0000:0941 | `lseek` | 0000 RTL | 41 | 9 | (fd, long ofs, whence): clear O_EOF (200h) in _openfd, INT 21h AH=42h, error -> -1L | functions.md §2 |
| 0000:096A | `__mkname` | 0000 RTL | 93 | 2 | pascal (num, pfx, buf): buf (default DS:21E6) = pfx (default "TMP") + __utoa(num) + ".$$$" tmpnam module | functions.md §2 |
| 0000:09C7 | `__tmpnam` | 0000 RTL | 89 | 0 | pascal (buf, unsigned far *num): next unused temp name (loop while _dos_getfileattr succeeds) no callers | functions.md §2 |
| 0000:0A20 | `N_LXMUL@` | 0000 RTL | 23 | 3 | near long multiply dx:ax * cx:bx -> dx:ax (plain near ret; used by fread/fwrite/calloc) | functions.md §2 |
| 0000:0A37 | `N_PCMP@` | 0000 RTL | 33 | 5 | near huge-pointer compare dx:ax vs cx:bx (normalise to seg:0..F, compare), flags out | functions.md §2 |
| 0000:0A58 | `_read` | 0000 RTL | 47 | 3 | raw read: O_WRONLY(2) in _openfd -> error 5, INT 21h AH=3Fh | functions.md §2 |
| 0000:0A87 | `__searchpath_copy` | 0000 RTL | 126 | 3 | near pascal (src, dst): copy string, upper-casing single-byte chars via _ctype (bit 8 -> toupper), SJIS pairs copied untouched; returns end … | functions.md §2 |
| 0000:0B05 | `__getcurdir_raw` | 0000 RTL | 27 | 1 | far pascal (buf, drive): INT 21h AH=47h, returns 0 or DOS error searchpath helper | functions.md §2 |
| 0000:0B20 | `__searchpath_try` | 0000 RTL | 329 | 3 | near pascal: build "D:\curdir\dir\name.ext" (using _getdrive/__getcurdir_raw/_mbsbtype) and findfirst it (attr 27h or 37h); 0 = found searchpath … | functions.md §2 |
| 0000:0C69 | `__searchpath_core` | 0000 RTL | 460 | 3 | near (char far *pathOrVar, int mode, char far *file): fnsplit file; mode&1: getenv(var) as PATH list, mode&4: use string as path list, mode&2: … | functions.md §2 |
| 0000:0E35 | `__searchpath` | 0000 RTL | 24 | 1 | near pascal (mode, file): __searchpath_core("PATH", mode, file) — used by _LoadProg | functions.md §2 |
| 0000:0E4D | `_searchenv` | 0000 RTL | 68 | 0 | (file, varname, buf): __searchpath_core(varname, 1, file) -> strcpy or "" no callers | functions.md §2 |
| 0000:0E91 | `_searchstr` | 0000 RTL | 68 | 0 | (file, ipath, buf): __searchpath_core(ipath, 4, file) no callers | functions.md §2 |
| 0000:0ED5 | `segread` | 0000 RTL | 32 | 1 | store ES, caller CS, SS, DS into struct SREGS | functions.md §2 |
| 0000:0EF5 | `_setupio` | 0000 RTL | 186 | 0 | #pragma startup (prio 2): init _openfd/_streams for handles 5.._nfile-1; stdin/stdout: clear _F_TERM if !isatty, setvbuf(stdin/stdout, NULL, … | functions.md §2 |
| 0000:0FAF | `toupper` | 0000 RTL | 44 | 1 | _ctype (DS:0BA1) bit 8 (_IS_LOW) -> c-20h; EOF passes | functions.md §2 |
| 0000:0FDB | `unlink` | 0000 RTL | 24 | 1 | INT 21h AH=41h (remove) | functions.md §2 |
| 0000:0FF3 | `__vprinter_hex4` | 0000 RTL | 7 | 2 | near: dx -> 4 hex digits at es:di (for %p) vprinter fragment | functions.md §2 |
| 0000:0FFA | `__vprinter_hex2` | 0000 RTL | 9 | 1 | near: al -> 2 hex digits vprinter fragment | functions.md §2 |
| 0000:1003 | `__vprinter_hex1` | 0000 RTL | 8 | 1 | near: nibble -> hex digit (add 90h/daa/adc 40h/daa) vprinter fragment | functions.md §2 |
| 0000:100B | `__vprinter` | 0000 RTL | 26 | 4 | printf engine (fmt, args, putter fn near ptr, dest); 80-byte stack buffer; jump table 149E (24 entries), class table DS:0EEB | functions.md §2 |
| 0000:1025 | `__vprinter_strlen` | 0000 RTL | 13 | 3 | near: strlen of es:di into cx vprinter fragment | functions.md §2 |
| 0000:1032 | `__vprinter_putc` | 0000 RTL | 9 | 7 | near: store al in buffer, flush when 80 bytes vprinter fragment | functions.md §2 |
| 0000:103B | `__vprinter_flush` | 0000 RTL | 1123 | 3 | near: call putter([bp+0Eh]) with buffered bytes, count total, set error flag vprinter fragment | functions.md §2 |
| 0000:14CE | `stpcpy` | 0000 RTL | 52 | 4 | (dst, src): memcpy(strlen+1), return dst+len (_fstpcpy) | functions.md §2 |
| 0000:1502 | `chkctype` | 0000 RTL | 96 | 1 | (unsigned char c, int prevtype): Japanese RTL: prev==1(lead) -> 2 if trail-valid (_mbctype bit 8) else -1; else 1 if lead (bit 4), 0 if ANK/kana, … | functions.md §2 |
| 0000:1562 | `nthctype` | 0000 RTL | 56 | 3 | (char far *s, int n): type (0 ANK, 1 kanji lead, 2 trail, -1) of byte n by chkctype scan (= _mbsbtype) | functions.md §2 |
| 0000:159A | `hantozen` | 0000 RTL | 59 | 0 | ASCII 20h-7Eh -> SJIS via DS:104E, half-width kana A1h-DFh -> SJIS via DS:110C, else unchanged no callers | functions.md §2 |
| 0000:15D5 | `zentohan_body` | 0000 RTL | 144 | 1 | SJIS -> single byte: 8340-8396 katakana via DS:118C, hiragana 829F-82F1 mapped to katakana first, 8140.. via reverse DS:104E search, punctuation … | functions.md §2 |
| 0000:1665 | `calloc` | 0000 RTL | 93 | 1 | (n, size): N_LXMUL@, >FFFFh -> NULL, malloc, setmem 0 | functions.md §2 |
| 0000:16C2 | `coreleft` | 0000 RTL | 6 | 1 | far thunk to farcoreleft 0000:2018 (large-model coreleft returns unsigned long); was named farcoreleft which of the two carries which name | functions.md §2 |
| 0000:16C8 | `_fpnotlinked_printf` | 0000 RTL | 5 | 0 | "printf : floating point formats not linked" to stderr, then abort; default _RealCvtVector DS:126E | functions.md §2 |
| 0000:16CD | `_fpnotlinked_scanf` | 0000 RTL | 26 | 0 | same message with "scanf" (dx=1203h), shares code at 16D0; default for DS:1270/1272/1274 not in listing | functions.md §2 |
| 0000:184E | `_exec` | 0000 RTL | 559 | 0 | near spawner for spawnl(P_OVERLAY) (ptr 184Eh): open file, build PSP cmd tail/FCBs, size memory, copy __exec_stub, load .COM/.EXE overlay not in … | functions.md §2 |
| 0000:1A89 | `FreeLastBlock` | 0000 RTL | 99 | 1 | far heap (farheap.asm): free the last block, shrink heap via __brk | functions.md §2 |
| 0000:1AEC | `FreeInnerBlock` | 0000 RTL | 113 | 1 | far heap: mark free, merge with previous/next free block, else InsertFreeBlock | functions.md §2 |
| 0000:1B5D | `PullFreeBlock` | 0000 RTL | 41 | 2 | far heap: unlink block DS from the circular free list (fields 4=prev,6=next) | functions.md §2 |
| 0000:1B86 | `InsertFreeBlock` | 0000 RTL | 55 | 1 | far heap: insert DS into free list at __rover | functions.md §2 |
| 0000:1BBD | `farfree` | 0000 RTL | 41 | 16 | free/farfree (large model same entry): ignores offset, segment = block | functions.md §2 |
| 0000:1BE6 | `CreateHeap` | 0000 RTL | 100 | 1 | far heap: first allocation: __sbrk align to paragraph, __sbrk(size), __first=__last | functions.md §2 |
| 0000:1C4A | `ExtendHeap` | 0000 RTL | 131 | 1 | far heap: grow by __sbrk, new __last | functions.md §2 |
| 0000:1CCD | `AllocatePartialBlock` | 0000 RTL | 35 | 1 | far heap: split requested size off the end of a free block | functions.md §2 |
| 0000:1CF0 | `malloc` | 0000 RTL | 10 | 9 | malloc(size_t) = farmalloc(0:size) entry (xor dx,dx; jmp 1D03); was named farmalloc — all engine callers push one word | functions.md §2 |
| 0000:1CFA | `farmalloc` | 0000 RTL | 125 | 2 | farmalloc(unsigned long): round up (+13h, /16), search free list from __rover, else Create/ExtendHeap | functions.md §2 |
| 0000:1D77 | `ReallocMove` | 0000 RTL | 124 | 1 | far heap realloc: farmalloc new size (CS:1A85/1A87), copy paragraphs, farfree old (Borland "GrowBlock") | functions.md §2 |
| 0000:1DF3 | `ShrinkBlock` | 0000 RTL | 100 | 1 | far heap realloc: split tail off and free it (or __brk if last) | functions.md §2 |
| 0000:1E57 | `realloc` | 0000 RTL | 7 | 0 | realloc(far ptr, size_t): dx=0 then common farrealloc code no callers | functions.md §2 |
| 0000:1E5E | `farrealloc` | 0000 RTL | 122 | 0 | farrealloc(far ptr, unsigned long) entry not in listing | functions.md §2 |
| 0000:1ED8 | `__brk_setblock` | 0000 RTL | 118 | 2 | near pascal (far addr): resize program block (setblock(_psp, n*40h paras), cache failures in DS:1230), update _heaptop / _brklvl brk module helper | functions.md §2 |
| 0000:1F4E | `__brk` | 0000 RTL | 63 | 2 | near (void far *addr): heapbase <= addr <= heaptop, then __brk_setblock; 0 / -1 | functions.md §2 |
| 0000:1F8D | `__sbrk` | 0000 RTL | 139 | 7 | near (long incr): new brklvl = _brklvl+incr (< 1 MB), range check, returns old brklvl or -1L | functions.md §2 |
| 0000:2018 | `farcoreleft` | 0000 RTL | 80 | 1 | _heaptop - _brklvl - 16, rounded down to paragraph (unsigned long) | functions.md §2 |
| 0000:2068 | `getch` | 0000 RTL | 25 | 1 | ungetch buffer DS:1232/1233, else INT 21h AH=07h | functions.md §2 |
| 0000:2081 | `int86` | 0000 RTL | 49 | 5 | segread + int86x | functions.md §2 |
| 0000:20B2 | `int86x` | 0000 RTL | 153 | 1 | builds "push bp; int n; pop bp; retf" on the stack (INT 25h/26h: pops flags), REGS/SREGS in/out, cflag, __IOerror on CF | functions.md §2 |
| 0000:214B | `intr` | 0000 RTL | 70 | 22 | intr(int, struct REGPACK far*): builds "mov bp,[bp-22h]; int n; retf 2" on stack, all regs incl. BP/DS/ES/flags (r_ax+0 .. r_flags+12h); was … | functions.md §2 |
| 0000:2191 | `__intr_iret` | 0000 RTL | 94 | 1 | single IRET used by intr to reload flags (push cs/call) fragment | functions.md §2 |
| 0000:21EF | `ioctl` | 0000 RTL | 41 | 2 | (fd, func, void far *argdx, argcx): INT 21h AH=44h; func 0 returns DX | functions.md §2 |
| 0000:2218 | `kbhit` | 0000 RTL | 18 | 0 | ungetch pending -> 1, else INT 21h AH=0Bh not in listing (dead) | functions.md §2 |
| 0000:222A | `_LoadProg` | 0000 RTL | 207 | 1 | near (spawner, path, argv, envp, usepath): __searchpath(mode\|2), __DOScmd, __DOSenv (default environ DS:1244), (*_exitbuf)(), spawner(path, cmd, … | functions.md §2 |
| 0000:22F9 | `__realcvt` | 0000 RTL | 4 | 1 | jmp [DS:126E] (_RealCvtVector) for %e/%f/%g in __vprinter | functions.md §2 |
| 0000:22FF | `_setargv` | 0000 RTL | 152 | 0 | #pragma startup (prio 10h): parse PSP:80h command tail (quotes, SJIS-aware backslash), argv[0] from env (DOS 3+), build far argv on stack; _argc … | functions.md §2 |
| 0000:2397 | `__setargv_nextch` | 0000 RTL | 177 | 2 | near helper of _setargv 0000:22FF: store char, count args, read next command-tail char (quote / SJIS-lead / backslash-quote handling); the code … | functions.md §2 |
| 0000:2448 | `setblock` | 0000 RTL | 28 | 1 | INT 21h AH=4Ah (seg, paras): -1 ok, else max available | functions.md §2 |
| 0000:2464 | `_setenvp` | 0000 RTL | 88 | 0 | #pragma startup (prio 10h): build environ[] (far ptrs) from env segment with malloc(_envSize); environ DS:1244, __C0environ DS:0054 not in listing | functions.md §2 |
| 0000:24BC | `setjmp` | 0000 RTL | 54 | 2 | store SP,SS,FLAGS,CS,IP,BP,DI,ES,SI,DS into jmp_buf | functions.md §2 |
| 0000:24F2 | `longjmp` | 0000 RTL | 48 | 2 | restore jmp_buf and IRET to saved CS:IP with AX=val (0->1); 2522-2529: unreachable tail + CS words used by _spawn (2526 SP, 2528 SS) | functions.md §2 |
| 0000:252A | `_spawn` | 0000 RTL | 194 | 0 | near spawner for spawnl(P_WAIT) (ptr 252Ah): parse FCBs (AX=2901h), copy cmd tail to PSP:80h, INT 21h AX=4B00h, AH=4Dh exit code not in listing | functions.md §2 |
| 0000:25EC | `spawnl` | 0000 RTL | 81 | 1 | mode 0 P_WAIT -> _LoadProg(_spawn), 2 P_OVERLAY -> _LoadProg(_exec), else errno=19 | functions.md §2 |
| 0000:263D | `_crtinit` | 0000 RTL | 134 | 0 | #pragma startup (prio 10h), PC-98 conio init: _video DS:1248-1258 from BIOS 0000:0712/0714/071D/0501/0458/0459/0632 (rows, attr, text VRAM … | functions.md §2 |
| 0000:26C3 | `__update_rows` | 0000 RTL | 13 | 1 | PC-98 conio: _video.screenheight (DS:1251) = BIOS 0000:0712 + 1 | functions.md §2 |
| 0000:26D0 | `__update_rows_clip` | 0000 RTL | 23 | 0 | as 26C3 but returns min(rows, winbottom DS:124B) not in listing (dead) | functions.md §2 |
| 0000:26E7 | `__dc_puts` | 0000 RTL | 26 | 1 | near pascal (char far *s): each char via INT DCh CL=10h AH=00h (DOS console direct output) PC-98 conio | functions.md §2 |
| 0000:2701 | `__gdc_csrform` | 0000 RTL | 38 | 1 | near pascal (shape): text GDC CSRFORM cmd 4Bh (port 62h) + 3 params (port 60h) after VSYNC; BIOS 053Bh blink bit PC-98 conio | functions.md §2 |
| 0000:2727 | `__io_wait` | 0000 RTL | 52 | 6 | out 5Fh,al twice (PC-98 I/O recovery) | functions.md §2 |
| 0000:275B | `__bios_cursor` | 0000 RTL | 30 | 1 | near pascal (shape): INT 18h AX=1E80h (cursor form) then AH=11h (show) or 12h (hide) PC-98 conio | functions.md §2 |
| 0000:2779 | `_setcursortype` | 0000 RTL | 161 | 2 | PC-98 conio: ESC[>5h / ESC[>5l via __dc_puts, then cursor shape by __bios_cursor (newer BIOS, DS:1257) or __gdc_csrform; rows >25/20 select shape | platform.md §1–2 |
| 0000:281A | `__delay_gdc` | 0000 RTL | 117 | 1 | near: delay loop counting text GDC status (port 60h bit 6) toggles; calibration by BIOS 0501h bit3, 054Ch bit5, 0459h bit3, 053Ch bit7 PC-98 delay | functions.md §2 |
| 0000:288F | `__delay_artic` | 0000 RTL | 112 | 1 | near: delay by the ARTIC timestamp counter (ports 5Ch/5Eh, 307.2 kHz) PC-98 delay (used when 0458h bit7 or 045Bh bit2) | functions.md §2 |
| 0000:28FF | `delay` | 0000 RTL | 41 | 1 | PC-98 delay(ms): selects __delay_artic or __delay_gdc | platform.md §1–2 |
| 0000:2928 | `__fnkey_esc` | 0000 RTL | 36 | 2 | near pascal (c): print CS-resident "ESC[>1" (2934) + c via INT DCh CL=10h; was pc98_esc_seq | functions.md §2 |
| 0000:294C | `fnkey_line_on` | 0000 RTL | 7 | 1 | ESC[>1l: show function-key row | platform.md §1–2 |
| 0000:2953 | `fnkey_line_off` | 0000 RTL | 7 | 1 | ESC[>1h: hide function-key row | platform.md §1–2 |
| 0000:295A | `fnkey_line_shown` | 0000 RTL | 14 | 0 | returns BIOS 0000:0711 != 0 (function-key row displayed) not in listing (dead) | functions.md §2 |
| 0000:2968 | `sound` | 0000 RTL | 95 | 1 | PC-98 beeper: 8253 ch1 (3FDFh mode 76h... value 7Eh, 3FDBh count from 2.4576/1.9968 MHz by 0501h bit7), port 37h<-06h on | platform.md §1–2 |
| 0000:29C7 | `nosound` | 0000 RTL | 5 | 1 | port 37h <- 07h | platform.md §1–2 |
| 0000:29CC | `_chmod` | 0000 RTL | 29 | 2 | (path, func, attr): INT 21h AH=43h, returns CX (used by open) | functions.md §2 |
| 0000:29E9 | `close` | 0000 RTL | 40 | 1 | range check _nfile, clear _openfd, _close | functions.md §2 |
| 0000:2A11 | `_close` | 0000 RTL | 30 | 2 | INT 21h AH=3Eh, clear _openfd | functions.md §2 |
| 0000:2A2F | `eof` | 0000 RTL | 119 | 1 | O_EOF flag or IOCTL device / position==length | functions.md §2 |
| 0000:2AA6 | `fclose` | 0000 RTL | 184 | 14 |  | functions.md §2 |
| 0000:2B5E | `fflush` | 0000 RTL | 205 | 10 | NULL -> flushall | functions.md §2 |
| 0000:2C2B | `filelength` | 0000 RTL | 65 | 4 | INT 21h 4201h/4202h/4200h | functions.md §2 |
| 0000:2C6C | `flushall` | 0000 RTL | 66 | 1 | fflush every open stream of _streams[_nfile], return count | functions.md §2 |
| 0000:2CAE | `fnsplit` | 0000 RTL | 43 | 1 | far wrapper calling _fnsplit 3EFE with the same 5 far pointers; identical dead copy at 4128 | functions.md §2 |
| 0000:2CD9 | `__fopen_mode` | 0000 RTL | 188 | 1 | near pascal (type, int far *oflag, int far *pmode): parse "r/w/a[+][t/b]", default _fmode DS:0E68; sets _exitfopen DS:0CA6 = 0000:448D (_xfclose) … | functions.md §2 |
| 0000:2D95 | `__openfp` | 0000 RTL | 209 | 1 | near pascal (shflag, type, filename, fp): mode parse, open(), isatty -> _F_TERM, setvbuf(512) | functions.md §2 |
| 0000:2E66 | `__getfp` | 0000 RTL | 74 | 1 | near: first _streams entry with fd < 0, or NULL | functions.md §2 |
| 0000:2EB0 | `fopen` | 0000 RTL | 53 | 5 | __getfp + __openfp | functions.md §2 |
| 0000:2EE5 | `fprintf` | 0000 RTL | 28 | 5 | __vprinter with __fputn | functions.md §2 |
| 0000:2F01 | `__fgetn` | 0000 RTL | 244 | 2 | near pascal (fp, n, buf): bulk read via _read when unbuffered/large, else per-char _fgetc; sets EOF/err flags fread helper | functions.md §2 |
| 0000:2FF5 | `fread` | 0000 RTL | 142 | 22 | (buf, size, n, fp): single __fgetn if size*n < 64K else per item | functions.md §2 |
| 0000:3083 | `__fbufcnt` | 0000 RTL | 130 | 3 | near pascal (fp): characters held in the buffer, counting '\n' twice in text mode (for ftell/fseek) | functions.md §2 |
| 0000:3105 | `fseek` | 0000 RTL | 128 | 1 | fflush, adjust SEEK_CUR by buffered count, reset buffer, lseek | functions.md §2 |
| 0000:3185 | `ftell` | 0000 RTL | 224 | 0 | lseek(fd,0,1) +/- buffered bytes (append mode: via SEEK_END) no callers | functions.md §2 |
| 0000:3265 | `fwrite` | 0000 RTL | 139 | 8 | __fputn per item or whole | functions.md §2 |
| 0000:32F0 | `__flushterm` | 0000 RTL | 62 | 2 | near: fflush all streams with _F_TERM\|_F_LBUF (300h) before a terminal read | functions.md §2 |
| 0000:332E | `_ffill` | 0000 RTL | 120 | 1 | near pascal (fp): refill buffer via read(); 0 ok, -1 EOF/error | functions.md §2 |
| 0000:33A6 | `_fgetc` | 0000 RTL | 21 | 1 | getc() slow path: level++ then fgetc | functions.md §2 |
| 0000:33BB | `__fgetc_near` | 0000 RTL | 3 | 0 | near-call entry thunk (pop ax/push cs/push ax) into fgetc not in listing | functions.md §2 |
| 0000:33BE | `fgetc` | 0000 RTL | 242 | 3 |  | functions.md §2 |
| 0000:34B0 | `getchar` | 0000 RTL | 12 | 0 | fgetc(stdin DS:0CAE) not in listing (dead) | functions.md §2 |
| 0000:34BC | `getenv` | 0000 RTL | 112 | 1 | scan environ (DS:1244) for "name=" | functions.md §2 |
| 0000:352C | `memcpy` | 0000 RTL | 36 | 3 | far, word moves | functions.md §2 |
| 0000:3550 | `setmem` | 0000 RTL | 36 | 2 | (dest, len, value) word stores | functions.md §2 |
| 0000:3574 | `memset` | 0000 RTL | 31 | 17 | (dest, c, n) -> setmem(dest, n, c), returns dest | functions.md §2 |
| 0000:3593 | `movmem` | 0000 RTL | 82 | 3 | (src, dest, len) overlap-safe (std when src < dest) | functions.md §2 |
| 0000:35E5 | `memmove` | 0000 RTL | 33 | 0 | (dest, src, n) -> movmem no callers | functions.md §2 |
| 0000:3606 | `__dos_creat` | 0000 RTL | 27 | 2 | near pascal (attrib, path): INT 21h AH=3Ch open helper | functions.md §2 |
| 0000:3621 | `__dos_trunc` | 0000 RTL | 18 | 1 | near pascal (fd): INT 21h AH=40h CX=0 (truncate at position) open helper | functions.md §2 |
| 0000:3633 | `open` | 0000 RTL | 367 | 1 | (path, oflag, pmode): O_CREAT/O_EXCL/O_TRUNC handling via _chmod/__dos_creat/_open/ioctl, sets _openfd | functions.md §2 |
| 0000:37A2 | `_open` | 0000 RTL | 79 | 1 | INT 21h AH=3Dh with access/share bits, _openfd[fd] | functions.md §2 |
| 0000:37F1 | `printf` | 0000 RTL | 27 | 21 | __vprinter(stdout DS:0CC2, __fputn) | functions.md §2 |
| 0000:380C | `_fputc` | 0000 RTL | 28 | 1 | putc() slow path: level-- then fputc | functions.md §2 |
| 0000:3828 | `fputc` | 0000 RTL | 405 | 5 |  static char DS:229C | functions.md §2 |
| 0000:39BD | `putchar` | 0000 RTL | 20 | 0 | fputc(c, stdout) no callers | functions.md §2 |
| 0000:39D1 | `__fputn` | 0000 RTL | 515 | 3 | near pascal (fp, n, buf): buffered/unbuffered write with '\n'->CRLF in text mode | functions.md §2 |
| 0000:3BD4 | `puts` | 0000 RTL | 88 | 7 | write string + '\n' to stdout (DS:0CC2); returns 0Ah or -1; was perror_like | functions.md §2 |
| 0000:3C2C | `read` | 0000 RTL | 209 | 2 | text-mode read: _read, strip CR, stop at ^Z (sets O_EOF) | functions.md §2 |
| 0000:3CFD | `setvbuf` | 0000 RTL | 285 | 6 |  sets _exitbuf DS:0CA2 = 0000:44CA (_xfflush) | functions.md §2 |
| 0000:3E1A | `CopyIt` | 0000 RTL | 89 | 4 | near pascal (dst, src, maxlen): fnsplit component copy (strncpy + NUL or strcpy) fnsplit module | functions.md §2 |
| 0000:3E73 | `DotFound` | 0000 RTL | 123 | 1 | near pascal: fnsplit "." / ".." test (switch table 3EEE: 0,'/',':','\\') fnsplit module | functions.md §2 |
| 0000:3EFE | `_fnsplit` | 0000 RTL | 520 | 3 | fnsplit body (skip blanks, 80-char copy, backward scan with switch table 4106: 0,'*','.','/',':','?','\\'), SJIS-aware via nthctype; returns … | functions.md §2 |
| 0000:4128 | `fnsplit_2` | 0000 RTL | 36 | 0 | second identical far wrapper around _fnsplit not in listing (dead duplicate) | functions.md §2 |
| 0000:414C | `__sputn` | 0000 RTL | 64 | 0 | near pascal sprintf putter: memcpy into *(char far **)dest, advance, NUL-terminate | functions.md §2 |
| 0000:418C | `sprintf` | 0000 RTL | 34 | 12 |  | functions.md §2 |
| 0000:41AE | `vsprintf` | 0000 RTL | 33 | 0 |  no callers | functions.md §2 |
| 0000:41CF | `strcat` | 0000 RTL | 63 | 4 |  | functions.md §2 |
| 0000:420E | `strcpy` | 0000 RTL | 41 | 16 |  | functions.md §2 |
| 0000:4237 | `stricmp` | 0000 RTL | 65 | 6 | case-insensitive compare (folds 'a'..'z'); was named strcmp. Callers: op_disk_change, op_load (x4), music_load -> those comparisons ignore case | functions.md §2 |
| 0000:4278 | `strlen` | 0000 RTL | 31 | 16 |  | functions.md §2 |
| 0000:4297 | `strncmp` | 0000 RTL | 56 | 2 | (s1, s2, n): compares min(n, strlen(s2)+1) bytes | functions.md §2 |
| 0000:42CF | `strncpy` | 0000 RTL | 49 | 2 | copy up to n, pad with NUL | functions.md §2 |
| 0000:4300 | `write` | 0000 RTL | 337 | 2 | text-mode write: '\n' -> CRLF through 80h-byte stack buffer, else _write; append -> lseek end | functions.md §2 |
| 0000:4451 | `_write` | 0000 RTL | 60 | 8 | raw INT 21h AH=40h; O_RDONLY(1) -> error 5; sets O_CHANGED (1000h) | functions.md §2 |
| 0000:448D | `_xfclose` | 0000 RTL | 61 | 0 | exit hook (_exitfopen): fclose every open stream | functions.md §2 |
| 0000:44CA | `_xfflush` | 0000 RTL | 56 | 0 | exit hook (_exitbuf): fflush the first 4 streams | functions.md §2 |
| 0000:4502 | `zentohan` | 0000 RTL | 3 | 1 | far entry (near jmp) to zentohan_body 0000:15D5: full-width SJIS -> single byte; engine op_disk_change uses it for 'Ｂ'->'B' Japanese RTL … | functions.md §2 |
| 0450:0005 | `main` | 0450 main | 94 | 1 | main(argc, argv): no arg -> HDD mode (DS:0072=1); "F"/"f" -> floppy mode (DS:0074=1) | engine.md §2.3 |
| 0456:0004 | `gdt_decode` | 0456 gfx codec | 17 | 3 | far cdecl (far *blob): decode DA1 image into the 4 plane segments set by gdt_set_planes | graphics.md §2.3–2.5 |
| 0456:001D | `gdt_set_planes` | 0456 gfx codec | 33 | 3 | far cdecl (seg0,seg1,seg2,seg3): destination segments for planes B,R,G,I (stored at CS:0015..001B) | graphics.md §2.3–2.5 |
| 0456:003E | `gdt_set_planes_vram` | 0456 gfx codec | 29 | 0 | (no caller; not in listing) set decoder destination CS:0015..001B to VRAM A800/B000/B800/E000, retf | graphics.md §2.3–2.5 |
| 0456:00D5 | `gdt_decode_impl` | 0456 gfx codec | 135 | 1 | near, DS:SI=blob: copy header to CS:008C, skip palette, set plane ptrs, run columns | graphics.md §2.3–2.5 |
| 0456:015C | `gdt_decode_pairs` | 0456 gfx codec | 62 | 4 | near: decode CS:00D4 column pairs (2-byte codec, or 2x1-byte if flags&0x4000) | graphics.md §2.3–2.5 |
| 0456:019A | `gdt_read_plane_sizes` | 0456 gfx codec | 21 | 1 | near: 4 x u16 plane sizes -> CS:00B6..00BC | graphics.md §2.3–2.5 |
| 0456:01AF | `gdt_plane_ptrs` | 0456 gfx codec | 65 | 1 | near: compute normalized far ptrs of the 4 plane streams -> CS:00C2..00D0 | graphics.md §2.3–2.5 |
| 0456:01F0 | `norm_si_dx` | 0456 gfx codec | 11 | 3 | near: normalize DX:SI (SI&=15) | graphics.md §2.3–2.5 |
| 0456:01FB | `gdt_col2_planes` | 0456 gfx codec | 69 | 1 | near: one 16-px column pair for planes 0..3 (2-byte codec) | graphics.md §2.3–2.5 |
| 0456:0240 | `gdt_col2` | 0456 gfx codec | 2323 | 4 | near: 2-byte codec column-pair interpreter; jump table 0456:0258 (256 words), unrolled blocks 0456:0B53..38A2 | graphics.md §2.3–2.5 |
| 0456:38A5 | `gdt_col1_planes` | 0456 gfx codec | 69 | 6 | near: one 8-px column for planes 0..3 (1-byte codec) | graphics.md §2.3–2.5 |
| 0456:38EA | `gdt_col1` | 0456 gfx codec | 1402 | 4 | near: 1-byte codec column interpreter; jump table 0456:3902 (256 words) | graphics.md §2.3–2.5 |
| 0456:3E64 | `gdt_align_plane_ptrs` | 0456 gfx codec | 69 | 1 | near: round the 4 stream ptrs up to even address before 2-byte codec | graphics.md §2.3–2.5 |
| 0456:3EA9 | `align_even_dsax` | 0456 gfx codec | 20 | 4 | near helper for gdt_align_plane_ptrs | graphics.md §2.3–2.5 |
| 0456:3EBD | `norm_ds_si` | 0456 gfx codec | 19 | 0 | (no caller; not in listing) near: normalise DS:SI so SI<16 | graphics.md §2.3–2.5 |
| 0843:0000 | `drive_to_daua` | 0843 disk | 273 | 6 | DOS drive letter -> PC-98 DA/UA (INT DCh CL=12h/13h; MEGDOS fallback A..D -> 90h..93h) | engine.md §5 |
| 0843:0119 | `count_dos_drives` | 0843 disk | 95 | 2 | INT 21h AH=52h LoL / DOS<3 fallback | engine.md §5 |
| 0843:0178 | `disk_sense` | 0843 disk | 58 | 6 | INT 1Bh AH=04h sense on DA/UA (90h..93h only); returns status & F0h (60h = not ready, 10h = write protected) | engine.md §5 |
| 0843:01B2 | `find_file_any_drive` | 0843 disk | 168 | 1 | scan drives A.. for path with findfirst; returns drive index or -1 | engine.md §5 |
| 0843:026E | `find_drive_by_label` | 0843 disk | 168 | 0 | (far char *pattern) (no caller; not in listing) for each DOS drive: class via drive_to_daua (switch 0843:0316), FDD must sense ready, … | engine.md §5 |
| 0843:032A | `cur_drive_ready` | 0843 disk | 51 | 1 | current drive DA/UA ready check | engine.md §5 |
| 0843:035D | `fopen_if_ready` | 0843 disk | 39 | 1 | fopen only if current drive ready | engine.md §5 |
| 0843:0384 | `xfopen` | 0843 disk | 128 | 7 | fopen(name) in cwd, else on the first drive that has the file ("%c:%s") | engine.md §5 |
| 0843:0404 | `get_volume_label` | 0843 disk | 140 | 4 | findfirst("X:\_?DISK_", FA_LABEL) on drive n -> label string; waits while drive not ready | engine.md §5 |
| 0843:0490 | `peek_byte` | 0843 disk | 16 | 1 | far peek (seg, off) | engine.md §5 |
| 088D:0000 | `cc_load` | 088D script | 666 | 2 | load "<name>[.cc]" (fnsplit/strcat ".cc"), LCZ header ignored, u16 size @0x14, Okumura LZSS (ring zero-filled) into farmalloc'd DS:00A8 | scripts.md §1–4 |
| 088D:029A | `cc_run` | 088D script | 116 | 1 | interpreter main loop: setjmp(DS:12D4); start at first 01 (or DS:12FC after LOAD); run pages until DS:1290 (FILE) or DS:13A6 (EXIT) | scripts.md §1–4 |
| 088D:030E | `cc_free` | 088D script | 26 | 2 | farfree script buffer DS:00A8 | scripts.md §1–4 |
| 088D:0328 | `cc_find_byte` | 088D script | 50 | 11 | cc_find_byte(pos, byte): token-wise scan for a token whose first byte == byte; -1 if none | scripts.md §1–4 |
| 088D:035A | `cc_run_page` | 088D script | 333 | 1 | execute one page at pos: expects 01 FB n ("debug1"/"debug2"), sets DS:1294; runs body to PEND or to SEN marker 1B then menu | scripts.md §1–4 |
| 088D:04A7 | `cc_skip_stmt` | 088D script | 81 | 52 | skip one statement: 13 -> to after 14; 06/07/0C/0E -> 2 tokens; else 1 token | scripts.md §1–4 |
| 088D:0514 | `cc_skip_token` | 088D script | 59 | 53 | skip one token: FB/FE/FF=3 bytes, FD=scan to NUL (from len byte), other=1 | scripts.md §1–4 |
| 088D:0559 | `cc_error` | 088D script | 121 | 57 | fprintf(stderr,"%s page=%d, LABEL=D%d, %s"); DS:13A6=DS:13A8=1; longjmp(DS:12D4) | scripts.md §1–4 |
| 088D:05D2 | `cc_get_u16` | 088D script | 42 | 29 | read little-endian word at script offset | scripts.md §1–4 |
| 088D:05FC | `cc_exec` | 088D script | 64 | 8 | cc_exec(pos, terminator): dispatch statements (table 088D:094C/09B6, 53 entries) until byte==terminator; returns pos after terminator (or new pos … | scripts.md §1–4 |
| 088D:063C | `op13_if` | 088D script | 168 | 1 | opcode 13 IF handler (inside cc_exec) | opcodes.md |
| 088D:06E4 | `op06_jmp` | 088D script | 54 | 1 | opcode 06 JMP handler: DS:1292=1, pos=cc_find_page(n) | opcodes.md |
| 088D:071A | `op07_file` | 088D script | 43 | 1 | opcode 07 FILE handler: DS:1290=1, strcpy(DS:137E, name) | opcodes.md |
| 088D:0745 | `opFD_msg` | 088D script | 409 | 1 | opcode FD message handler: text_print(str, !kana_lock) | opcodes.md |
| 088D:08DE | `opFE_set` | 088D script | 46 | 1 | opcode FE flag assignment handler | opcodes.md |
| 088D:090C | `op1F_exit` | 088D script | 64 | 1 | opcode 1F EXIT: DS:13A6=1 | opcodes.md |
| 088D:0A20 | `cc_menu` | 088D script | 377 | 1 | top-level SEN: build options, select_menu, run case, loop until JMP/FILE/EXIT/LOAD; setjmp(DS:12E8) | scripts.md §1–4 |
| 088D:0B99 | `cc_submenu` | 088D script | 610 | 3 | nested SEN inside a case (recursive, cursor memory DS:12FE[8][8]); ends with longjmp(DS:12E8) | scripts.md §1–4 |
| 088D:0DFB | `cc_skip_block` | 088D script | 186 | 3 | skip a false IF body up to matching 0B (nested IF aware; error 13 on 0A/FF) | scripts.md §1–4 |
| 088D:0ECD | `cc_find_page` | 088D script | 97 | 2 | find "01 FB n" token-wise from 0; error 9 if absent | scripts.md §1–4 |
| 088D:0F2E | `cc_find_label` | 088D script | 110 | 2 | find case "FF id" between SENEND(05) and PEND(02); -1 if absent | scripts.md §1–4 |
| 088D:0F9C | `op_disk_change` | 088D script | 241 | 1 | opcode 0x1D: floppy mode only - wait until drive B: holds volume "_<L>DISK_" | engine.md §5.3 |
| 088D:108D | `cc_assign` | 088D script | 119 | 1 | FE f 12 FB v: v!=0 -> flag_set(f) else flag_clear(f) | scripts.md §1–4 |
| 088D:1104 | `cc_eval_cond` | 088D script | 191 | 2 | 13 (FE f 12 FB v)(15 ...)* 14: AND of (flag_test(f)==v) | scripts.md §1–4 |
| 088D:11C3 | `flag_set` | 088D script | 46 | 1 | DS:00F2[f/8] \|= 1<<(f&7); error 8 if f>=0x800 | scripts.md §1–4 |
| 088D:11F1 | `flag_clear` | 088D script | 48 | 1 | DS:00F2[f/8] &= ~(1<<(f&7)) | scripts.md §1–4 |
| 088D:1221 | `flag_test` | 088D script | 55 | 1 | returns 0/1 | scripts.md §1–4 |
| 088D:1258 | `flags_clear_all` | 088D script | 18 | 1 | memset(DS:00F2,0,0x100) (new game, called from 0ABA:039F) | scripts.md §1–4 |
| 088D:126A | `op_save` | 088D script | 373 | 1 | opcode 0x19: write "<name>.dat" save slot | save-format.md |
| 088D:13DF | `error_box` | 088D script | 98 | 5 | beep 500Hz/500ms, centered red text on text layer, wait key | engine.md §5.4 |
| 088D:1441 | `op_load` | 088D script | 719 | 1 | opcode 0x1C: read "<name>.dat"; restore vars, script, page, gfx, music | save-format.md |
| 088D:1710 | `op0C_g_load` | 088D script | 69 | 1 | G_LOAD name -> 0C2A:0315 | opcodes.md |
| 088D:1755 | `op28_g_load_ovl` | 088D script | 71 | 1 | name -> 0C2A:00DB(name,1) | opcodes.md |
| 088D:179C | `op16_g_sabun_load` | 088D script | 69 | 1 | G_SABUN_LOAD name -> 0C2A:04AB | opcodes.md |
| 088D:17E1 | `op0D_g_disp` | 088D script | 18 | 1 | -> 0C2A:0649 | opcodes.md |
| 088D:17F3 | `op26_graph_off` | 088D script | 18 | 1 | -> 0DCA:00D9 (INT 18h AH=41h) | opcodes.md |
| 088D:1805 | `op27_graph_on` | 088D script | 18 | 1 | -> 0DCA:00BF (INT 18h AH=40h) | opcodes.md |
| 088D:1817 | `op43_vram_wpage` | 088D script | 78 | 1 | FB n -> 0DCA:0150(n!=0) out A6h | opcodes.md |
| 088D:1865 | `op44_vram_dpage` | 088D script | 78 | 1 | FB n -> 0DCA:0162(n!=0) out A4h | opcodes.md |
| 088D:18B3 | `op24_g_effect` | 088D script | 78 | 1 | FB n -> n ? 0C2A:075E : 0C2A:0690 | opcodes.md |
| 088D:1901 | `op11_window` | 088D script | 83 | 1 | FB n -> n ? 0C2A:07F0 (waku2 frame) : 0DCA:04E3 (GRCG clear); DS:1422[0]=0 | opcodes.md |
| 088D:1954 | `op32_g_scroll` | 088D script | 69 | 1 | FB n -> 0B7D:0A68(n): graphics GDC SCROLL start line n (shake effect) | opcodes.md |
| 088D:1999 | `op2A_adt_load` | 088D script | 67 | 1 | FD name -> 0ED8:0078 | opcodes.md |
| 088D:19DC | `op2B_adt_free` | 088D script | 18 | 1 | -> 0ED8:0217 | opcodes.md |
| 088D:19EE | `op2D_adt_play` | 088D script | 18 | 1 | -> 0ED8:0001 | opcodes.md |
| 088D:1A00 | `op2E_anim_start` | 088D script | 107 | 1 | 6x FB -> 0ED8:036F(a,b,c,d,e,f) | opcodes.md |
| 088D:1A6B | `op2F_anim_stop` | 088D script | 18 | 1 | -> 0ED8:03CB | opcodes.md |
| 088D:1A7D | `op38_textpos` | 088D script | 105 | 1 | 4x FB -> DS:07D8,07DA,07E0,07E2 | opcodes.md |
| 088D:1AE6 | `op33_g_load_buf` | 088D script | 69 | 1 | FD name -> 0C2A:00DB(name,0) (same as op 28) | opcodes.md |
| 088D:1B2B | `op34_g_load_parts` | 088D script | 67 | 1 | FD name -> 0C2A:0142 | opcodes.md |
| 088D:1B6E | `op35_g_parts_copy` | 088D script | 18 | 1 | -> 0C2A:01F9 | opcodes.md |
| 088D:1B80 | `op36_palette` | 088D script | 153 | 1 | 4x FB idx,r,g,b -> DS:13EC[idx*3]=rgb; 0DCA:0200(idx,rgb) | opcodes.md |
| 088D:1C19 | `op_set_progress` | 088D script | 267 | 1 | opcode 0x37 <int n>: XEN.GDT \|= 1<<(n-1) | save-format.md |
| 088D:1D32 | `op40_msg_clear` | 088D script | 18 | 1 | -> 0B7D:0783 | opcodes.md |
| 088D:1D44 | `op39_textcolor` | 088D script | 93 | 1 | 2x FB -> DS:07DC, DS:07DE | opcodes.md |
| 088D:1DA1 | `op0E_music_load` | 088D script | 69 | 1 | MUSIC_LOAD name -> 0B2C:01C4 | opcodes.md |
| 088D:1DE6 | `op0F_music_play` | 088D script | 18 | 1 | -> 0B2C:02E5 | opcodes.md |
| 088D:1DF8 | `op10_music_stop` | 088D script | 18 | 1 | -> 0B2C:033C | opcodes.md |
| 088D:1E0A | `op45_se_play` | 088D script | 69 | 1 | FB n -> 0B2C:043C(n) | opcodes.md |
| 088D:1E4F | `op46_se_stop` | 088D script | 18 | 1 | -> 0B2C:0461 | opcodes.md |
| 088D:1E61 | `op48_music_wait` | 088D script | 69 | 1 | FB n -> 0B2C:04D8(n) | opcodes.md |
| 088D:1EA6 | `op3A_music_vol` | 088D script | 69 | 1 | FB n -> 0B2C:0482(n) | opcodes.md |
| 088D:1EEB | `op30_music_fade` | 088D script | 69 | 1 | FB n -> 0B2C:0386(n) | opcodes.md |
| 088D:1F30 | `op23_g_clear` | 088D script | 18 | 1 | -> 0C2A:05B2 | opcodes.md |
| 088D:1F42 | `op20_wait` | 088D script | 69 | 1 | FB n -> 0DCA:0012(n) | opcodes.md |
| 088D:1F87 | `op21_fadein` | 088D script | 78 | 1 | [FB n=1] -> 0DCA:02F7(DS:13EC palette by value, n) | opcodes.md |
| 088D:1FD5 | `op22_fadeout` | 088D script | 63 | 1 | [FB n=1] -> 0DCA:0445(n) | opcodes.md |
| 088D:2014 | `op42_fadewhite` | 088D script | 63 | 1 | [FB n=1] -> 0DCA:0494(n) | opcodes.md |
| 088D:2053 | `op29_opening` | 088D script | 63 | 1 | [FB n=1] -> 0E31:04AE(n) | opcodes.md |
| 088D:2092 | `cc_build_options` | 088D script | 427 | 2 | parse SEN 04..05 into DS:1298 (far char*[10]) / DS:12C0 (ids[10]); returns last index; error 12 if >9 | scripts.md §1–4 |
| 088D:223D | `cc_system_menu` | 088D script | 131 | 2 | cancel menu: Cancel / Return to title (DS:13A6=1) / Quit (confirm; DS:13A8=DS:13A6=1) | engine.md §8 |
| 088D:22C0 | `op03_clear_lower` | 088D script | 6 | 1 | -> 0B7D:0852 | opcodes.md |
| 088D:22C6 | `bitmask8` | 088D script | 15 | 3 | 1<<(n&7) | scripts.md §1–4 |
| 0ABA:0005 | `omake_locked_notice` | 0ABA game | 81 | 2 | shows "please enjoy after finishing the game" | engine.md §3, §7 |
| 0ABA:0056 | `title_menu_hilite` | 0ABA game | 103 | 8 | XOR-invert title/omake menu bar #n (4 planes, 14 words x 32 lines) | engine.md §3, §7 |
| 0ABA:00BD | `omake_cg_gallery` | 0ABA game | 28 | 1 | if (DS:06E8&0x7F)==0x7F run "k" else notice | engine.md §3, §7 |
| 0ABA:00D9 | `omake_music_mode` | 0ABA game | 28 | 1 | if (DS:06E8&0x7F)==0x7F run "m" else notice | engine.md §3, §7 |
| 0ABA:00F5 | `omake_menu` | 0ABA game | 143 | 1 | shows menu2.gdt, 2 items, returns 0/1/-1 | engine.md §3, §7 |
| 0ABA:0190 | `title_menu_select` | 0ABA game | 76 | 1 | 3 items (start / load / omake) | engine.md §3, §7 |
| 0ABA:01DC | `game_main` | 0ABA game | 526 | 1 | title screen loop + script run loop | engine.md §3, §7 |
| 0AF8:000A | `isr_null` | 0AF8 input | 24 | 0 | INT 05h/06h replacement (COPY / STOP keys): push all, iret | platform.md §3 |
| 0AF8:0022 | `kbd_hook_copy_stop` | 0AF8 input | 63 | 1 | save INT 05h/06h vectors (DS:13BC/13B8), point both to 0AF8:000A | platform.md §3 |
| 0AF8:0061 | `kbd_unhook_copy_stop` | 0AF8 input | 37 | 1 | restore INT 05h/06h | platform.md §3 |
| 0AF8:0086 | `bios500_set_bit5` | 0AF8 input | 28 | 1 | save BIOS 0000:0500, set bit 5 (restored by 0AF8:00A2) | platform.md §3 |
| 0AF8:00A2 | `bios500_restore` | 0AF8 input | 27 | 1 | restore 0000:0500 if saved | platform.md §3 |
| 0AF8:00BD | `kbd_getch_nowait` | 0AF8 input | 75 | 4 | DOS 06h DL=FFh; ctrl/cursor keys get AH = INT18h-02h shift bits & 19h; BS key -> 0Fh | platform.md §3 |
| 0AF8:0108 | `kbd_kana_locked` | 0AF8 input | 43 | 1 | INT 18h AH=02h bit2 (KANA lock) -> 1 | platform.md §3 |
| 0AF8:0133 | `kbd_flush` | 0AF8 input | 9 | 3 | loop kbd_getch_nowait until 0 | platform.md §3 |
| 0AF8:013C | `kbd_read_cmd` | 0AF8 input | 152 | 1 | BIOS key buffer -> command code (0 cancel,1 up,2 down,3 left,4 right,5 ok,6 other,7 none,8 HELP,9-13 f1-f5 (also SHIFT/CTRL+f),14 BS) | platform.md §3 |
| 0AF8:0254 | `wait_ok_key` | 0AF8 input | 87 | 8 | flush, wait until ok/cancel (kbd/mouse); CTRL held = skip; XFER+ROLLUP+ROLLDOWN = emergency quit | platform.md §3 |
| 0AF8:02AB | `kbd_ctrl_held` | 0AF8 input | 14 | 1 | 0000:053A & 10h (CTRL) | platform.md §3 |
| 0AF8:02B9 | `kbd_shift_held` | 0AF8 input | 14 | 0 | (no caller; not in listing) BIOS 0000:053A bit 0 (SHIFT) | platform.md §3 |
| 0AF8:02C7 | `input_get_cmd` | 0AF8 input | 132 | 4 | blocking: keyboard command, else mouse (delta from (100,100) >20px -> dir, L=5, R=0) | platform.md §3 |
| 0B2C:000B | `snd_detect_mmd` | 0B2C sound | 50 | 1 | getvect(61h)+2 == "MMD" -> 1 (MIDI driver resident) | sound.md §1, §3 |
| 0B2C:003D | `snd_detect_pmd` | 0B2C sound | 50 | 2 | getvect(60h)+2 == "PMD" -> 1 (near) | sound.md §1, §3 |
| 0B2C:006F | `snd_init` | 0B2C sound | 300 | 1 | snd_init(mode): 1=PMD(INT60h) 2=MMD(INT61h)+PUTCM6 LITRA; gets music buf (AH=06) + FM-effect buf (AH=0Bh), loads XEN_SE.EFC; only called with 1 | sound.md §1, §3 |
| 0B2C:019B | `snd_clear_efc_hook` | 0B2C sound | 41 | 0 | UNUSED: PMD AH=14h DS:DX=0:0 (clear effect-interrupt hook) | sound.md §1, §3 |
| 0B2C:01C4 | `music_load` | 0B2C sound | 283 | 5 | music_load(far name): same name -> no-op (sets g_music_started=1); else stop, fopen "%s.M"/"%s.MMD", fread whole file to PMD music buffer (AH=06 … | sound.md §1, §3 |
| 0B2C:02DF | `music_get_name` | 0B2C sound | 6 | 1 | returns far ptr DS:13C8 (current music name) | sound.md §1, §3 |
| 0B2C:02E5 | `music_play` | 0B2C sound | 87 | 5 | if loaded && !g_music_started: g_music_started=1; INT AH=00h (mstart) | sound.md §1, §3 |
| 0B2C:033C | `music_stop` | 0B2C sound | 74 | 6 | INT AH=01h (mstop) repeated while returned AH==FFh (driver busy); clears name | sound.md §1, §3 |
| 0B2C:0386 | `music_fade` | 0B2C sound | 78 | 4 | music_fade(speed): INT AH=02h AL=speed (fade out) | sound.md §1, §3 |
| 0B2C:03D4 | `music_get_fadevol` | 0B2C sound | 83 | 1 | INT AH=08h -> AL (fade-out volume, FFh = fully faded); -1 if no driver/no music | sound.md §1, §3 |
| 0B2C:0427 | `music_fade_stop_wait` | 0B2C sound | 21 | 3 | fade(25), busy-wait until fadevol==FFh, then music_stop | sound.md §1, §3 |
| 0B2C:043C | `se_play` | 0B2C sound | 37 | 2 | se_play(n): PMD INT60h AH=0Ch AL=n (FM sound effect from XEN_SE.EFC) | sound.md §1, §3 |
| 0B2C:0461 | `se_stop` | 0B2C sound | 33 | 3 | PMD INT60h AH=0Dh (FM effect off) | sound.md §1, §3 |
| 0B2C:0482 | `music_set_fadevol` | 0B2C sound | 43 | 1 | music_set_fadevol(v): PMD AH=19h AL=v (0=full volume .. FFh=silent) | sound.md §1, §3 |
| 0B2C:04AD | `pmd_board_present` | 0B2C sound | 43 | 1 | PMD AH=09h; returns 0 if AL==FFh (no FM board) (near) | sound.md §1, §3 |
| 0B2C:04D8 | `music_wait_status` | 0B2C sound | 55 | 2 | music_wait_status(n): if board present, loop while PMD AH=0Ah status1 (set by MML ~ / ~+) == n | sound.md §1, §3 |
| 0B2C:050F | `snd_msg_tone_xfer` | 0B2C sound | 13 | 1 | printf "音色データーを転送中です" (near) | sound.md §1, §3 |
| 0B7D:000C | `text_print_big` | 0B7D text/menu | 399 | 1 | draw text inside 0x03 ... 0x03 escape (double size); also called for '@' insert | engine.md §6 |
| 0B7D:019B | `text_print` | 0B7D text/menu | 721 | 2 | main message printer (window at DS:07D8/07DA, 4x60) | engine.md §6 |
| 0B7D:048C | `select_menu` | 0B7D text/menu | 340 | 4 | generic choice menu (far char* array, NULL-terminated, initial index) -> index / -1 cancel / -2 | engine.md §7 |
| 0B7D:05E0 | `menu_draw_all` | 0B7D text/menu | 71 | 1 | draw all options and highlight initial | engine.md §7 |
| 0B7D:0627 | `menu_draw_item` | 0B7D text/menu | 68 | 1 | sprintf("%-*s") + draw at row y = DS:07E2 + i*18 | engine.md §7 |
| 0B7D:066B | `menu_hilite` | 0B7D text/menu | 103 | 3 | XOR-invert menu row (4 planes, 16 lines) | engine.md §7 |
| 0B7D:06D2 | `menu_draw_item_near` | 0B7D text/menu | 68 | 0 | (no caller; not in listing) near copy of menu_draw_item | engine.md §7 |
| 0B7D:0716 | `menu_clear` | 0B7D text/menu | 109 | 2 | clear option area in all 4 planes | engine.md §7 |
| 0B7D:0783 | `text_window_clear` | 0B7D text/menu | 92 | 4 | clear message window rows (4 planes, 62 bytes x 72 lines) | engine.md §6 |
| 0B7D:07DF | `msg_box` | 0B7D text/menu | 41 | 3 | print message on the text layer at `ESC[22;24H` (1-based: cell 23,21), wait key, clear | engine.md §6 |
| 0B7D:0808 | `prompt_insert_disk` | 0B7D text/menu | 74 | 1 | "<X>ディスクをドライブ２へセットして下さい" | engine.md §6 |
| 0B7D:0852 | `clear_lower_screen` | 0B7D text/menu | 83 | 1 | clear 80-byte rows y=0x128.. in 4 planes | engine.md §6 |
| 0B7D:08A5 | `pal_set_all_black` | 0B7D text/menu | 69 | 0 | (no caller; not in listing) pal_set_all(48 zero bytes) | engine.md §6 |
| 0B7D:08EA | `gdt_unpack_palette_2` | 0B7D text/menu | 158 | 0 | (far *gdt, far *dst48) no caller; duplicate of gdt_unpack_palette 0C2A:0514 | engine.md §6 |
| 0B7D:0988 | `text_cursor_show` | 0B7D text/menu | 29 | 0 | (int on) no caller; on==1 -> "\e[>5l" (cursor on) else "\e[>5h" | platform.md §2 |
| 0B7D:09A5 | `text_gdc_csrform` | 0B7D text/menu | 101 | 2 | (int ignored) text GDC CSRFORM (cmd 4Bh): cursor not displayed, 16-line rows, blink rate 5 | platform.md §2 |
| 0B7D:0A0A | `fnkey_line_show` | 0B7D text/menu | 29 | 0 | (int on) no caller; on==1 -> "\e[>1l" (function-key row on) else "\e[>1h" | platform.md §2 |
| 0B7D:0A27 | `text_layer_clear` | 0B7D text/menu | 13 | 16 | printf("\e*") - clear text VRAM | engine.md §6 |
| 0B7D:0A34 | `text_locate` | 0B7D text/menu | 23 | 5 | printf("\e[%d;%dH", row, col) (args: col, row) | engine.md §6 |
| 0B7D:0A4B | `gdc_vsync_spin` | 0B7D text/menu | 29 | 0 | (no caller; not in listing) near: loop DS:13DA += 4 until graphic GDC status (port A0h) bit 2 = VSYNC | engine.md §6 |
| 0B7D:0A68 | `gdc_scroll` | 0B7D text/menu | 112 | 1 | (int line) G_SCROLL op 32h: graphic GDC SCROLL (70h): SAD1=line*40 words, LEN1=400-line, SAD2=0, LEN2=line | opcodes.md (G_SCROLL) |
| 0C2A:0008 | `gfx_get_state_ptr` | 0C2A gfx | 6 | 1 | returns DX:AX = DS:1422 (g_gfx_state, saved in save games) | graphics.md §3 |
| 0C2A:000E | `gfx_alloc_buffers` | 0C2A gfx | 141 | 1 | calloc 4 x 0x7D10 off-screen plane buffers ("da_data"), normalize -> DS:13DC | graphics.md §3 |
| 0C2A:009B | `gfx_free_buffers` | 0C2A gfx | 64 | 2 | free the 4 plane buffers | graphics.md §3 |
| 0C2A:00DB | `gdt_load_buf` | 0C2A gfx | 103 | 3 | (far char *name, int unused): load name.gdt, decode into plane buffers; no palette, no display-rect update; strcpy(g_gfx_state,name) | graphics.md §3 |
| 0C2A:0142 | `gdt_load_page1` | 0C2A gfx | 87 | 1 | (far char *name): access page 1, decode name.gdt straight into VRAM page 1, remember rect at DS:1431, access page 0 | graphics.md §3 |
| 0C2A:0199 | `gdt_decode_vram` | 0C2A gfx | 96 | 1 | (far *blob, int keep_rect): decode blob into VRAM (A800/B000/B800/E000); if keep_rect==0 store rect -> DS:1431..1435 | graphics.md §3 |
| 0C2A:01F9 | `gfx_merge_page1` | 0C2A gfx | 284 | 1 | merge rect DS:1431 from VRAM page 1 into plane buffers; colour 8 = transparent | graphics.md §3 |
| 0C2A:0315 | `gdt_load` | 0C2A gfx | 115 | 10 | (far char *name): G_LOAD: decode into buffers, palette -> DS:13EC, display rect -> DS:141C, set hardware palette; strcpy(g_gfx_state,name) | graphics.md §3 |
| 0C2A:0388 | `gdt_read_file` | 0C2A gfx | 175 | 4 | (far char *name): sprintf "%s.gdt", open (0843:0384), malloc(filelength), fread whole file; returns far ptr or NULL | graphics.md §3 |
| 0C2A:0437 | `gdt_decode_buf` | 0C2A gfx | 116 | 3 | (far *blob, int keep): decode into plane buffers; if keep==0 also unpack palette -> DS:13EC and set display rect DS:141C..1420 | graphics.md §3 |
| 0C2A:04AB | `gdt_load_diff` | 0C2A gfx | 105 | 2 | (far char *name): decode into buffers (rect kept); if flags byte==0xCF unpack+set palette | graphics.md §3 |
| 0C2A:0514 | `gdt_unpack_palette` | 0C2A gfx | 158 | 2 | near (far *blob, far *out48): 24-byte packed palette -> 16 x (R,G,B) | graphics.md §3 |
| 0C2A:05B2 | `gfx_clear_buffers` | 0C2A gfx | 52 | 2 | memset the 4 plane buffers to 0 | graphics.md §3 |
| 0C2A:05E6 | `gfx_copy_row` | 0C2A gfx | 99 | 9 | near (int off, int wbytes): copy one row of all 4 planes buffer->VRAM (current access page) | graphics.md §3 |
| 0C2A:0649 | `gfx_show` | 0C2A gfx | 71 | 10 | G_DISP: copy display rect (DS:141C/141E/1420) buffers -> VRAM | graphics.md §3 |
| 0C2A:0690 | `fx_blind` | 0C2A gfx | 175 | 1 | interlaced wipe: rows 0,1,4,5 of each 8 top->bottom, then 7,6,3,2 bottom->top, 1 vsync per 8-row band | graphics.md §3 |
| 0C2A:073F | `fmemcpy_n` | 0C2A gfx | 31 | 4 | near (dstoff,dstseg,srcoff,srcseg,len) | graphics.md §3 |
| 0C2A:075E | `fx_center_open` | 0C2A gfx | 146 | 1 | vertical wipe from centre outwards, 8 rows per vsync | graphics.md §3 |
| 0C2A:07F0 | `fx_waku2_close` | 0C2A gfx | 139 | 1 | load waku2 into buffers and shutter-wipe the CG area (40,4,560x296) from top+bottom to centre, 8 rows/vsync | graphics.md §3 |
| 0C2A:087B | `fx_dissolve_cell` | 0C2A gfx | 231 | 1 | near: copy one 4x1 nibble of an 8x8 cell (order table DS:0966) buffer->VRAM | graphics.md §3 |
| 0C2A:0962 | `fx_dissolve` | 0C2A gfx | 184 | 1 | full-screen 16-step ordered-dither dissolve buffer->VRAM, 1 vsync per step (opening only) | graphics.md §3 |
| 0C2A:0F04 | `lalf_opn_init_regs` | 0C2A gfx | 71 | 1 | dead LALF driver: OPN regs 80h..8Fh=0Fh, 10 fixed reg/value pairs (CS:052B), operator regs from a table | sound.md §1.2 |
| 0C2A:0F77 | `lalf_reset_work` | 0C2A gfx | 44 | 1 | dead LALF driver: copy channel-work template (CS:0004 -> CS:02BE, 7 x 3Eh bytes) | sound.md §1.2 |
| 0C2A:0FA3 | `lalf_start` | 0C2A gfx | 136 | 0 | dead LALF driver far entry (far *song): set up 7 channels from the song header | sound.md §1.2 |
| 0C2A:10BD | `lalf_set_volume` | 0C2A gfx | 18 | 0 | dead LALF driver far entry (int v): CS:022C = v (added to TL), CS:0246 = 1 | sound.md §1.2 |
| 0C2A:10FC | `lalf_set_param` | 0C2A gfx | 12 | 0 | dead LALF driver far entry (int v): CS:022E = v (meaning not traced) | sound.md §1.2 |
| 0C2A:1108 | `lalf_tick` | 0C2A gfx | 73 | 0 | dead LALF driver far entry: per-tick update of the 7 channels (calls lalf_channel_step) | sound.md §1.2 |
| 0C2A:1151 | `lalf_set_channel_flag` | 0C2A gfx | 31 | 0 | dead LALF driver far entry (int ch, int on): channel work +3Ch = on!=0 | sound.md §1.2 |
| 0C2A:128A | `lalf_channel_step` | 0C2A gfx | 91 | 7 | dead LALF driver: one sequencer step for the channel at DI | sound.md §1.2 |
| 0C2A:12F5 | `lalf_update_volume` | 0C2A gfx | 170 | 1 | dead LALF driver: FM total-level regs 40h+ch by algorithm (table CS:08C5) | sound.md §1.2 |
| 0C2A:139F | `lalf_ssg_envelope` | 0C2A gfx | 138 | 1 | dead LALF driver: software envelope (attack/decay/sustain/release states 1..4) | sound.md §1.2 |
| 0C2A:1429 | `lalf_update_freq` | 0C2A gfx | 101 | 1 | dead LALF driver: FM A4h/A0h F-number + key-on (28h) / SSG tone regs | sound.md §1.2 |
| 0C2A:14BE | `lalf_read_commands` | 0C2A gfx | 246 | 2 | dead LALF driver: bytecode reader: >=C0h -> command table CS:026A, else octave/note -> F-number (CS:0A6E FM, CS:0A86 SSG) | sound.md §1.2 |
| 0C2A:15B4 | `lalf_lfo_step` | 0C2A gfx | 62 | 1 | dead LALF driver: pitch LFO / vibrato step | sound.md §1.2 |
| 0C2A:15FA | `lalf_key_off` | 0C2A gfx | 27 | 2 | dead LALF driver: FM key-off (reg 28h) / SSG release | sound.md §1.2 |
| 0C2A:19A1 | `lalf_opn_write` | 0C2A gfx | 99 | 25 | dead LALF driver: write OPN register DH=reg, DL=value via ports 188h/18Ah (busy-wait on status bit 7) | sound.md §1.2 |
| 0DCA:0004 | `ctrl_held` | 0DCA display lib | 14 | 1 | 0000:053A & 10h | platform.md §6, graphics.md §3 |
| 0DCA:0012 | `wait_vsync` | 0DCA display lib | 47 | 27 | wait_vsync(n): n VSYNC edges via port 60h bit5 (text GDC); returns early while CTRL held | platform.md §6, graphics.md §3 |
| 0DCA:0041 | `video_init` | 0DCA display lib | 43 | 1 | 16-colour mode, INT18h 42h(CH=C0h) 4Ah(CH=06h), port 6Ah <- 83h (GDC CLOCK-1), 41h (plasma/LCD alignment), pages 0/0 | platform.md §6.1, graphics.md §1 |
| 0DCA:006C | `gdc_wait_vsync_idle` | 0DCA display lib | 31 | 2 | port A0h: wait VSYNC (bit2) then drawing idle (bit3=0) (near) | platform.md §6, graphics.md §3 |
| 0DCA:008B | `text_on` | 0DCA display lib | 26 | 0 | UNUSED: INT 18h AH=0Ch | platform.md §6, graphics.md §3 |
| 0DCA:00A5 | `text_off` | 0DCA display lib | 26 | 0 | UNUSED: INT 18h AH=0Dh | platform.md §6, graphics.md §3 |
| 0DCA:00BF | `graph_on` | 0DCA display lib | 26 | 11 | INT 18h AH=40h (graphics display start) | platform.md §6, graphics.md §3 |
| 0DCA:00D9 | `graph_off` | 0DCA display lib | 26 | 9 | INT 18h AH=41h (graphics display stop) | platform.md §6, graphics.md §3 |
| 0DCA:00F3 | `gdc_mode_4a_16` | 0DCA display lib | 31 | 0 | UNUSED: INT 18h AH=4Ah CH=16h (GDC flashless drawing) | platform.md §6, graphics.md §3 |
| 0DCA:0112 | `gdc_mode_4a_06` | 0DCA display lib | 31 | 1 | INT 18h AH=4Ah CH=06h (GDC flash drawing; no effect, engine never draws with the GDC) | platform.md §6, graphics.md §3 |
| 0DCA:0131 | `graph_mode_640x400` | 0DCA display lib | 31 | 1 | INT 18h AH=42h CH=C0h (640x400 colour) | platform.md §6, graphics.md §3 |
| 0DCA:0150 | `set_access_page` | 0DCA display lib | 18 | 18 | out A6h (draw page), DS:1468 | platform.md §6, graphics.md §3 |
| 0DCA:0162 | `set_display_page` | 0DCA display lib | 18 | 6 | out A4h (display page), DS:146A | platform.md §6, graphics.md §3 |
| 0DCA:0174 | `set_pages_vsync` | 0DCA display lib | 34 | 0 | UNUSED: GDC wait then set access+display page | platform.md §6, graphics.md §3 |
| 0DCA:0196 | `get_pages` | 0DCA display lib | 23 | 0 | UNUSED: *a=DS:1468, *d=DS:146A | platform.md §6, graphics.md §3 |
| 0DCA:01AD | `set_modeff2` | 0DCA display lib | 12 | 1 | out 6Ah, v | platform.md §6, graphics.md §3 |
| 0DCA:01B9 | `pal_set` | 0DCA display lib | 71 | 6 | pal_set(i, r, g, b): ports A8h/ACh(r)/AAh(g)/AEh(b) + shadow DS:1438[i*3] | platform.md §6, graphics.md §3 |
| 0DCA:0200 | `pal_set_rgb` | 0DCA display lib | 58 | 4 | pal_set_rgb(i, struct{r,g,b} by value) | platform.md §6, graphics.md §3 |
| 0DCA:023A | `pal_get_rgb` | 0DCA display lib | 30 | 0 | UNUSED: copy shadow entry i (3 bytes) to far ptr | platform.md §6, graphics.md §3 |
| 0DCA:0258 | `pal_set_all` | 0DCA display lib | 48 | 4 | pal_set_all(far rgb[16]) | platform.md §6, graphics.md §3 |
| 0DCA:0288 | `pal_set_all_but8` | 0DCA display lib | 88 | 0 | UNUSED: like pal_set_all but skips colour 8 | platform.md §6, graphics.md §3 |
| 0DCA:02E0 | `pal_get_all` | 0DCA display lib | 23 | 1 | copy 48-byte shadow palette to far ptr | platform.md §6, graphics.md §3 |
| 0DCA:02F7 | `pal_fade_to` | 0DCA display lib | 334 | 11 | pal_fade_to(rgb[16] by value, delay): step every component by 1 toward target, wait_vsync(delay) per step | platform.md §6, graphics.md §3 |
| 0DCA:0445 | `pal_fade_black` | 0DCA display lib | 79 | 17 | pal_fade_to(all 0, delay) | platform.md §6, graphics.md §3 |
| 0DCA:0494 | `pal_fade_white` | 0DCA display lib | 79 | 3 | pal_fade_to(all 15, delay) | platform.md §6, graphics.md §3 |
| 0DCA:04E3 | `gvram_clear` | 0DCA display lib | 30 | 17 | GRCG TDW (7Ch=80h, tiles 0) fill A800:0000 32000 bytes -> clears 4 planes of access page | platform.md §6, graphics.md §3 |
| 0E1A:0001 | `check_free_memory` | 0E1A init | 64 | 1 | farcoreleft() < 0x6C278 -> "550KB needed" + exit(1) | engine.md §2 |
| 0E1A:0041 | `check_dipsw_gdc` | 0E1A init | 62 | 0 | UNUSED (no callers): DIP SW 2-8 / GDC 5MHz warning | engine.md §2 |
| 0E1A:007F | `engine_init` | 0E1A init | 157 | 1 | init sequence called from main | engine.md §2 |
| 0E1A:011C | `engine_shutdown` | 0E1A init | 98 | 2 | restore hardware / drivers, exit path | engine.md §2 |
| 0E31:000E | `seq_fmemcpy` | 0E31 sequences | 31 | 17 | near (far dst, far src, n): rep movsw/movsb copy | engine.md §4 |
| 0E31:002D | `seq_slide_up_iplane` | 0E31 sequences | 227 | 1 | near (off, w, h, step, full): rows 0..k of buffer plane 3 (I) to VRAM rows h-1-k..h-1, k += step per vsync; full==1: then copy all 4 planes of the … | engine.md §4 |
| 0E31:0110 | `seq_slide_up` | 0E31 sequences | 197 | 1 | near (off, w, h, step): like seq_slide_up_iplane but all 4 planes, k < h | engine.md §4 |
| 0E31:01D5 | `logo_show_mark` | 0E31 sequences | 267 | 1 | near: palette = ROGO palette with entries 5-7,9,10 black; black-out; copy rect off 17h, 17 bytes x 249 lines; pal_fade_to(.,6) | engine.md §4 |
| 0E31:02E0 | `logo_flash_white` | 0E31 sequences | 175 | 1 | near: pal_fade_to(ROGO palette with 5-7,9,10 = white, 1), then pal_fade_to(ROGO palette, 1) | engine.md §4 |
| 0E31:038F | `logo_show_top` | 0E31 sequences | 107 | 1 | near: copy rect off 28h, 14 bytes x 75 lines (all planes) | engine.md §4 |
| 0E31:03FA | `opening_logo` | 0E31 sequences | 180 | 1 | "rogo" logo sequence (custom palette/plane reveals), inferred | engine.md §4 |
| 0E31:04AE | `opening_title` | 0E31 sequences | 292 | 1 | 0100b dissolve + 0100B.ADT + xenon2/0100bb title (script op 0x29), inferred | engine.md §4 |
| 0E31:05D2 | `staff_roll` | 0E31 sequences | 221 | 1 | st00..st09 staff roll (script op 0x47), inferred | engine.md §4 |
| 0E9B:000F | `mouse_init` | 0E9B mouse | 78 | 1 | limits Y 0..399 X 0..639, 8255 mode 93h, BFDBh=1, hook INT 15h -> 0E9B:009C, enable | platform.md §3.4 |
| 0E9B:005D | `mouse_shutdown` | 0E9B mouse | 31 | 1 | disable, BFDBh=0, restore INT 15h | platform.md §3.4 |
| 0E9B:007C | `mouse_irq_enable` | 0E9B mouse | 16 | 2 | 8255 PC4=0 (7FDFh<-08h), unmask slave PIC IR5 (port 0Ah bit5) (near) | platform.md §3.4 |
| 0E9B:008C | `mouse_irq_disable` | 0E9B mouse | 16 | 2 | writes 09h to port 7FD7h (sic, probably typo for 7FDFh), mask slave IR5 (near) | platform.md §3.4 |
| 0E9B:009C | `isr_mouse` | 0E9B mouse | 114 | 0 | INT 15h: read dx/dy nibbles (7FDDh/7FD9h), clamp pos, button edges -> DS:0AFA/0AFB | platform.md §3.4 |
| 0E9B:010E | `mouse_read_deltas` | 0E9B mouse | 316 | 1 | near (from isr_mouse): read X/Y nibbles via 7FDDh/7FD9h into DS:1682/1684, latch, add+clamp DS:0AF2/0AF4, button edges -> DS:0AFA/0AFB | platform.md §3.4 |
| 0E9B:024B | `mouse_get_pos` | 0E9B mouse | 23 | 1 | (far int *x, far int *y) <- DS:0AF2/0AF4 | platform.md §3.4 |
| 0E9B:0262 | `mouse_set_pos` | 0E9B mouse | 17 | 1 | (x, y) | platform.md §3.4 |
| 0E9B:0273 | `mouse_get_buttons` | 0E9B mouse | 45 | 0 | UNUSED: current L/R state | platform.md §3.4 |
| 0E9B:02A0 | `mouse_left_clicked` | 0E9B mouse | 20 | 1 | consume DS:0AFA | platform.md §3.4 |
| 0E9B:02B4 | `mouse_right_clicked` | 0E9B mouse | 20 | 1 | consume DS:0AFB | platform.md §3.4 |
| 0E9B:02C8 | `mouse_set_limits` | 0E9B mouse | 57 | 0 | (x0, y0, x1, y1) no caller; not in listing: sorted into DS:1670/166C/1672/166E | platform.md §3.4 |
| 0ECB:0001 | `isr_vsync` | 0ECB vsync | 78 | 0 | INT 0Ah (CRTV): EOI, ++DS:0AFE (dword), call hook DS:168A, out 64h re-arm | platform.md §4.2 |
| 0ECB:004F | `vsync_irq_install` | 0ECB vsync | 65 | 1 | mask IR2, save INT 0Ah, set 0ECB:0001, unmask, out 64h | platform.md §4.2 |
| 0ECB:0090 | `vsync_irq_remove` | 0ECB vsync | 37 | 1 | mask IR2, restore INT 0Ah (IR2 left masked) | platform.md §4.2 |
| 0ECB:00B5 | `vsync_get_ticks` | 0ECB vsync | 8 | 0 | no caller; not in listing: returns DX:AX = DS:0B00:DS:0AFE | platform.md §4.2 |
| 0ECB:00BD | `vsync_set_hook` | 0ECB vsync | 20 | 2 | vsync_set_hook(far fn) (NULL = none) | platform.md §4.2 |
| 0ED8:0001 | `adt_play` | 0ED8 anim | 119 | 2 | play loaded ADT, alternating VRAM pages 1,0,1..; blocking | graphics.md §3.5–3.6 |
| 0ED8:0078 | `adt_load` | 0ED8 anim | 415 | 2 | (far char *name): read name.adt: header + up to 120 frame blobs (malloc each) | graphics.md §3.5–3.6 |
| 0ED8:0217 | `adt_free` | 0ED8 anim | 58 | 3 | free frame blobs | graphics.md §3.5–3.6 |
| 0ED8:0251 | `fmemcpy_n2` | 0ED8 anim | 31 | 4 | near (dstoff,dstseg,srcoff,srcseg,len) | graphics.md §3.5–3.6 |
| 0ED8:0270 | `blink_tick` | 0ED8 anim | 255 | 0 | vsync callback: copy blink-strip frame from buffers to VRAM per DS:0B14 timing | graphics.md §3.5–3.6 |
| 0ED8:036F | `blink_start` | 0ED8 anim | 92 | 1 | (sx,sy,dx,dy,wbytes,h): set params, install vsync, callback=blink_tick | graphics.md §3.5–3.6 |
| 0ED8:03CB | `blink_stop` | 0ED8 anim | 30 | 3 | reset state, callback=0, uninstall vsync | graphics.md §3.5–3.6 |
| 0F16:0009 | `font_ank_cache_init` | 0F16 font | 110 | 2 | once (DS:0B7C): port 68h<-0Bh, 96x INT 18h AH=14h DX=2920h+i -> 16-byte 8x16 glyphs at DS:1B66+i*16 (chars 20h..7Fh), 68h<-0Ah | font.md |
| 0F16:0077 | `sjis_to_jis` | 0F16 font | 89 | 1 | near (far *hi, far *lo): Shift-JIS -> JIS in place | font.md |
| 0F16:00D0 | `kanji_rom_read` | 0F16 font | 160 | 2 | (sjis_hi, sjis_lo, far *dst32): 68h<-0Bh, A3h<-jis_hi-20h, A1h<-jis_lo; BIOS 054Dh bit6 ? 32 bytes from A400:0000 : 16x (A5h<-line\|20h, in A9h; … | font.md |
| 0F16:0170 | `glyph16_bold` | 0F16 font | 65 | 2 | (far *g16): per row: g \|= g<<1 except where the source has a 1-pixel gap (101 pattern) | font.md |
| 0F16:01B1 | `glyph16_double_height` | 0F16 font | 79 | 1 | near (far *dst64, far *src32): duplicate every row (16x16 -> 16x32) | font.md |
| 0F16:0200 | `glyph16_to_32` | 0F16 font | 206 | 4 | near (far *dst128, far *src32): 16x16 -> 32x32 (double height, each bit doubled horizontally) | font.md |
| 0F16:02CE | `glyph16_or` | 0F16 font | 32 | 4 | near (far *dst, far *src): 16 words dst \|= src | font.md |
| 0F16:02EE | `glyph16_andnot` | 0F16 font | 34 | 4 | near (far *dst, far *src): 16 words dst &= ~src | font.md |
| 0F16:0310 | `vram_put16` | 0F16 font | 37 | 1 | near (far *src, seg, off): 16 rows x 2 bytes to seg:off, stride 80 | font.md |
| 0F16:0335 | `vram_put32` | 0F16 font | 52 | 1 | near (far *src, seg, off): 32 rows x 4 bytes to seg:off, stride 80 | font.md |
| 0F16:0369 | `vram_put_glyph16` | 0F16 font | 55 | 1 | near (far *planes[4][32], off, wait): wait_vsync(wait), vram_put16 per plane (segments DS:09AA) | font.md |
| 0F16:03A0 | `vram_put_glyph32` | 0F16 font | 55 | 1 | near (far *planes[4][128], off, wait): wait_vsync(wait), vram_put32 per plane | font.md |
| 0F16:03D7 | `kanji_draw` | 0F16 font | 326 | 3 | (x_bytes, y, fg, bg, sjis_hi, sjis_lo, wait): opaque 16x16 cell: bg colour, bold ROM glyph in fg; wait_vsync(wait) first | font.md |
| 0F16:051D | `kanji_draw_big` | 0F16 font | 141 | 2 | (x_bytes, y, fg(unused), sjis_hi, sjis_lo, wait): 32x32 double-size bold glyph at (x, y-16), always colour 15 on 0 | font.md |
| 0F16:05AA | `glyph8_bold` | 0F16 font | 31 | 1 | (far *g8x16): row \|= row<<1 | font.md |
| 0F16:05C9 | `vram_andnot8` | 0F16 font | 37 | 8 | (seg, off, far *src16): 16 rows: VRAM &= ~src, stride 80 | font.md |
| 0F16:05EE | `vram_or8` | 0F16 font | 35 | 8 | (seg, off, far *src16): 16 rows: VRAM \|= src, stride 80 | font.md |
| 0F16:0611 | `vram_put_glyph8` | 0F16 font | 171 | 1 | (off, colour, far *g16): per plane: colour bit ? OR : AND NOT | font.md |
| 0F16:06BC | `ank_draw` | 0F16 font | 281 | 2 | (x_bytes, y, fg, bg, ch): opaque 8x16 cell; glyph from DS:1966+ch*16 (cache valid for 20h..7Fh only), bold; no wait | font.md |
| 0F16:07D5 | `text_draw_string` | 0F16 font | 108 | 2 | (x_bytes, y, fg, bg, far char *s, wait): SJIS via kanji_draw, other bytes via ank_draw (menus) | font.md |
| 0F16:0841 | `gaiji_define` | 0F16 font | 48 | 1 | (seg, off, code): INT 18h AH=1Ah DX=code BX:CX=seg:off (user-defined character) | font.md |
| 0F16:0871 | `fill32` | 0F16 font | 24 | 4 | near (far *dst, int value): memset(dst, value, 32) | font.md |
