# Languages

The pack carries every language; the player picks one at start (`--lang ja|en|pl` on the host
backends, `LANG=ja|en|pl` on the Amiga, default `en`: the profile's `lang`). Saves and the
`progress` record are shared by all languages.

| tag | pack id | encoding | wrap | story scripts from | engine strings | K/L/M.CC |
|-----|---------|----------|------|--------------------|----------------|----------|
| `ja` | 1 | cp932 | kinsoku | the original game, unchanged | `ja/strings.txt` | original |
| `en` | 2 | UTF-8 | word | `text/translation/script_en.txt` | `en/strings.txt` | `en/klm.txt` |
| `pl` | 3 | UTF-8 | word | `text/translation/script_pl.txt` | `pl/strings.txt` | `pl/klm.txt` |

The engine knows no language: the pack names them (its `langs` entry, written by lrvne's pack
writer; the ids are given in the order of `LANG_ORDER` in `tools/mkpack.py`, and a save records
the id it was made in).

Encoding and wrap are defined once in `tools/langs.py` and written into each language's `lang`
meta entry of the pack, which `engine/text/enc.c` reads (`text_next()` is the only place that
decodes text).

## Where the translation text comes from

`../text/` (repository root) holds the translations, the master copy (`text/README.md`): the
dictionaries `translation/script_<lang>.txt`, the translators' guides and story bible, and their
tools. `text/tools/charset_pl.py` is the Polish character table that `tools/langs.py` reads, so
the translators' line-length checks and the engine agree by construction. The text came from the
Xenon PC-98 translation project; since 2026-11-13 it is edited here only.

The dictionaries do not contain the original script: each entry's key points at its piece of the
Japanese in the game's scripts (script, string offset, segment, CRC-32; `tools/tlkeys.py`).
`tl_extract.py` resolves the keys from GAMEDIR and applies the dictionary by text as before (the
last copy of a key wins, a key translates its text wherever it occurs). `make text-view` writes
the dictionaries back out with the Japanese into `../text/view/` for the translators.
`tests/check_text.py` (in `make test`) checks every dictionary against the game: every key still
names the text it was written for, all languages are line-aligned with `script_en.txt`, no story
string keeps Japanese, and every voiced part (`resources/voice/voicemap.tsv`, keyed the same way)
has translated text.

After a change to the text (the guides' §1 has the editing workflow):

    cd source && make pack                               # rebuild build/xenon.pak
    SKIP_OPN=1 make TARGET=host test
    make TARGET=host e2e E2E_ARGS="--lang en"; make TARGET=host e2e E2E_ARGS="--lang ja"

Then read `build/<lang>/lint.txt` (below). JA output must not change, nor the other language's.

## Building the scripts (`make pack`)

`tools/tl_extract.py` builds each translated language from its dictionary (semantics of the
project's PC-98 build, see its docstring) into `build/<lang>/scripts/*.U.CC`, and
`tools/mkpack.py` packs them with the fonts and the strings. Variables: `GAMEDIR` (original
game, filled by `make data`, `../docs/game-data.md`; `make pack` checks it first), `X11FONTS` (default `lrvne/fonts/x11`, the BDFs lrvne carries), `TL_DICT_EN`, `TL_DICT_PL`,
`TL_COMPARE=DIR` (diff the English against the project's PC-98 build output).

`build/<lang>/lint.txt` lists every decision worth a human look:

- `space restored` - a word ended exactly at column 60 of the PC-98 patch layout and the space
  was lost; the tool put it back.
- `row count A -> B` - the engine's word wrap gives a different number of rows than the
  dictionary's padded layout (normal: padding is removed).
- `layout string kept verbatim` - title cards and other strings whose layout is deliberate
  (Japanese starts with 2+ full-width spaces, or the translation has explicit newlines).

Lines a translation has not reached yet are whatever the dictionary holds there (the Polish
file started as a copy of the English one).

**Rule for chained cards.** The prologue cards (S00/S00B/S00C) are separate messages that keep
the previous text on screen (code 05), each followed by a spacer message `\n　`. A line that
wraps to a second row gets its first two columns blanked by that spacer (seen as
"eczywistością" for "rzeczywistością"). These lines must fit one 60-column row; the PC-98
engine behaves the same way.

## Fonts

- `ja`: font8 = X11 `8x16rk` (JIS X 0201), font16 = X11 `jiskan16` keyed by JIS code
  (`build/fonts/ank8.xfnt`, `k16.xfnt`).
- UTF-8 languages get their own Unicode-keyed pair, made by `mkpack.py`: font8 = ASCII plus the
  language's narrow letters from `build/fonts/lat8.xfnt` (X11 `8x16`, the same Sony design in
  ISO 8859-1, plus `../fonts/latin8-pl.txt`: the Polish letters drawn for this port), font16 =
  the full-width characters the language's texts use, cut from `k16.xfnt`, plus the prompts
  (U+25BC ▼ and U+E000 for the JIS user glyph 7621h). A character is 1 column if it is ASCII
  or in font8, else 2. `mkpack.py` fails on any character without a glyph.
- Polish text must not use `~` and typographic quotes/dashes (`PL_FORBIDDEN` in
  `text/tools/charset_pl.py`).

## Engine strings

`<lang>/strings.txt` (`key=value`, UTF-8; `ja` is converted to cp932) holds the engine's own
messages (system menu, load/save, error boxes); `<lang>/klm.txt` translates K.CC (CG gallery),
L.CC (load) and M.CC (music mode), which the PC-98 translation patch never built (exact Japanese
`TAB` translation). The Polish files are drafts for the Polish editor, as is
`../fonts/latin8-pl.txt`.

## Testing

`tests/test_text.c` covers the column rules (the decoders are lrvne's, tested there). The e2e scenarios run in `en`
and `ja` only; Polish is checked by play-review. The routes to each dream are in
`../../docs/re/script-catalog.md` §2-3.

## Adding a language

1. `tools/langs.py`: `SETTINGS` (encoding, wrap) and its narrow letters.
2. `LANG_ORDER` in `tools/mkpack.py` (append: the ids of the others must not change); the
   player ReadMe.
3. Missing narrow glyphs: draw them in a glyph art file (as lrvne's `fonts/latin8-pl.txt`) and
   add it to the `lat8` rule of the Makefile.
4. `lang/<id>/strings.txt` and `klm.txt`; the dictionary `text/translation/script_<id>.txt`
   (a copy of `script_en.txt` to start from, as Polish did: the same keys on the same lines, the
   English as placeholder text, which `check_text.py` keeps aligned), its rules in
   `text/tools/charset_pl.py` and a guide in `text/docs/`, a `TL_DICT_*` variable and the
   `make pack` rule.
