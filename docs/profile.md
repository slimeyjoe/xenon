# The game profile

The engine is C's Ware's AGS.EXE, re-implemented for any game made with it; it knows no game. A
game is two things:

1. **Its profile**, the entry `profile` of its pack: `key=value` lines (UTF-8; blank lines and
   lines starting with `#` ignored), parsed by lrvne (`lrvne/profile.h`) against the engine's
   table in `source/engine/game/profile.c`. A key the table does not have, a value of the wrong
   type or out of range, a missing required key: the engine refuses the pack with a message
   naming the line. `make pack` writes it from `source/games/<game>/profile.txt`.
2. **Its native modules** (`source/games/<game>/`): routines of the game's AGS.EXE that are no
   interpreted script, compiled into the program and enabled by the profile's `modules`
   (`source/engine/mod/mod.h`).

Xenon's: [source/games/xenon/profile.txt](../source/games/xenon/profile.txt).

## Keys

| key | type | required | what |
|---|---|---|---|
| `id` | name | yes | the game's short name: its saves go to the backend's store of this id (`<user data>/<program>/<id>`, on the Amiga `PROGDIR:saves/<id>`) |
| `title` | text | no (the id) | the window's and the screen's title |
| `lang` | tag | yes | the language played without `--lang` (one of the pack's, its `langs` entry) |
| `modules` | names | no | the native modules the game enables, by name (below) |
| `title_image` | picture | yes | the title menu's picture (`G_LOAD`ed and shown, then faded in) |
| `title_rows` | 3 integers | yes | the top line of each of the title menu's three bars: start, load, extras (0ABA:0056, DS:06F7) |
| `title_bar` | 3 integers | yes | a bar of the title and extras menus: left byte column, width in bytes, height in lines (inverted to highlight) |
| `start` | script | yes | the script a new game starts with when no rule of `start_if_clear` applies |
| `start_if_clear` | rules | no | `MASK:SCRIPT,...`: the first rule none of whose progress bits (the endings seen, `COMPLETE n` sets bit n-1) is set chooses the script instead |
| `load_script` | script | yes | the title menu's load |
| `omake_image` | picture | yes | the extras menu's picture, a difference picture over the title's |
| `omake_scripts` | 1-3 scripts | yes | the extras menu's rows, a script each (`k,m`: the CG gallery, the music mode) |
| `omake_unlock` | integer | no (0) | the progress bits that open the extras; before, a row shows the `omake_locked` message |
| `se_bank` | name | no (none) | the FM sound effect bank (`.EFC`) PMD loads |
| `music_buffer` | integer | yes | bytes of PMD's music buffer (its `/M` option: 10240 for `/M10`); a longer song is cut |
| `music_keep_after_fade` | yes/no | no (no) | PMD's `/F`: a song plays on, silent, after a fade out |
| `voice_music_volume` | 1..100 | no (100) | the music's volume while the voice pack plays (the FM music masks speech); `--volume` overrides it |

Names are the base names of the game's files (no extension: the entry type is the
extension), at most 15 bytes; integers are decimal or `0x` hexadecimal.

## Native modules

A module takes one of the engine's hooks; a hook without one is logged and does nothing, so a
pack that enables no module plays (no logo, an `OPENING` and `STAFF_ROLL` that do nothing, a
`WINDOW 1` that leaves the picture as it is). `tests/check_core.py` runs the engine that way.

| hook | where | Xenon's module |
|---|---|---|
| logo | before the title screen (then 120 frames' wait) | `xenon_logo`: the C's Ware logo (0E31:03FA) |
| `OPENING n` (29h) | a script | `xenon_opening`: the dissolve, the logo zoom animation, the title cards (0E31:04AE) |
| `STAFF_ROLL` (47h) | a script | `xenon_staff_roll`: the credits pictures timed to the song's cue (0E31:05D2) |
| `WINDOW 1` (11h) | a script | `xenon_frame_close`: the CG frame closes over the picture (0C2A:07F0) |

A game's modules are registered by the program's `main` (`source/games/main.c`).

## What is the engine's and what the game's

Decided item by item from the research notes (`docs/re/`): what every AGS.EXE does is the
engine's; what this AGS.EXE was built with for this game is the game's.

| item | where it is |
|---|---|
| the interpreter, its opcodes and errors, the picture and animation formats, the effects of `G_EFFECT`, palette fades, the eye-blink animator, the message printer, the choice menu, the system menu, saves, the text layer | engine |
| the message window's geometry: 4 rows of 60 columns at 18 lines, the initial origin (byte column 10, line 313) and colours, the prompt's cell | engine: constants of the printer's code and DGROUP initial values that the scripts change with `TEXT_POS`/`TEXT_COLOR` (engine.md §6) |
| the title menu's flow (logo, title, start / load / extras, the fade to black, the 120-frame wait after the logo) | engine (game_main 0ABA) |
| the title menu's picture, bars and their positions; the extras menu's picture, scripts and unlock mask; the start-script rules; the load script | profile |
| the logo, opening and staff-roll sequences with their pictures, animation, songs and sound effect | modules (`source/games/xenon/sequences.c`) |
| the frame-close effect, its picture and rectangle | module (`source/games/xenon/frame.c`) |
| PMD 4.7g and its built-in SSG effects | engine (the driver; the table is taken from `PMD.COM` by the pack builder) |
| the FM effect bank's name, the music buffer, `/F` | profile |
| the voice cue mechanics (when a clip plays, waits, is cut) | engine; the voice's default music volume: profile |
| the game's id (save directory), window title, default language | profile |
| the program's name (where a backend finds the pack, `<program>.pak`), the release archive's drawer, ReadMe and icon picture | the game's `game.mk` |
| the languages and their ids, the user-defined glyph JIS 7621h taken from the game's AGS.EXE, the data fixes of `--fix-data-bugs` | the game's `pack.py` (tool side) |
