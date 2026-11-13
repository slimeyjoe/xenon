ＸＥＮＯＮ ～夢幻の肢体～

# The translations

The English and Polish translations of *Xenon ～夢幻の肢体～* (C's Ware, 1994), their guides and
the translators' tools. This directory is the master copy: edit the translations here. The
port builds them into the game itself (`make pack` in `../source/`, `../source/lang/README.md`).

The original Japanese script is not here. It is C's Ware's and stays in the game's files, which
you import from your own copy (`make data`, `../docs/game-data.md`). Each entry of a translation
points at its piece of the original by place (script, string, segment) with a checksum of the
text, and `make text-view` writes the dictionaries back out with the Japanese on every key line
into `view/`, for reading and searching (not committed).

| path | what |
|---|---|
| `translation/script_en.txt` | English. Each entry is a key line that points at a piece of the original script (`//@S0301A 0F3C 0 9f3c21aa`; never edited by hand), followed by its translation; `#` lines are notes. Format and traps: `docs/TRANSLATION_GUIDE_EN.md` §2-3 |
| `translation/script_pl.txt` | Polish, line-aligned with the English file: the same keys on the same lines, so every line number (Lnnnn) in the docs holds for both |
| `translation/notes_pl.tsv` | the Polish translator's notes, `line<TAB>note` |
| `view/` | made by `make text-view` from your game, not committed: `script_<lang>.txt` with the Japanese on the key lines (the same line numbers), and `lines.tsv`, every entry with its place in the game, its voice clips, the Japanese and all translations |
| `docs/TRANSLATION_GUIDE_EN.md` | read this before changing a line: the engine's limits, fidelity rules, voices, the locked terms |
| `docs/TRANSLATION_GUIDE_PL.md` | the rules for Polish, on top of the English guide |
| `docs/STORY_BIBLE.md` | the plot of every route, the characters, the mood (spoilers) |
| `tools/` | `lint_translation.py`, `fitcheck.py`, `set_line.py`; `charset_pl.py`, the table of allowed characters and their widths, which the port's build reads too |

## Changing a line

From this directory (`--lang pl` for Polish; the guides' §1 has the details). The tools read
the Japanese from the game's files too, so import the game first (`make data`):

    (cd ../source && make text-view)                         # view/: the Japanese written back
    grep -n '例の原因不明の昏睡' view/script_en.txt            # find the entry: its line number
    python3 tools/lint_translation.py > /tmp/lint.before     # the known issues, to compare with
    python3 tools/lint_translation.py --line 4381            # live, duplicate or inert; which scripts use it
    python3 tools/fitcheck.py --pad "[Kohji]: ..."           # does it fit the window
    python3 tools/set_line.py --pad 4381 "[Kohji]: ..."      # replace the line byte for byte
    python3 tools/lint_translation.py > /tmp/lint.after; diff /tmp/lint.before /tmp/lint.after

Then build the game and look at it:

    cd ../source
    make pack                      # ../build/xenon.pak; needs the original game (make data, ../docs/game-data.md)
    less ../build/en/lint.txt      # what the build decided about each string (../source/lang/README.md)
    ../build/sdl3/xenon --pak ../build/xenon.pak --lang en    # or: make TARGET=host e2e
    make TARGET=host test          # tests/check_text.py: every key still points at its text,
                                   # the languages are line-aligned, every story string and
                                   # every voiced line is translated

`lint_translation.py --line` names the game scripts that use a line when the original scripts
are there (`make data` derives them into `../resources/scripts/`). A new entry (a piece of the
script no entry covers yet) gets its key line from `../source/tools/tlkeys.py locate TEXT`.

## History

The English translation was begun in 2013 by HempTL, siriusxiv, esperknight, MulletDonkey and
OmBadai, and completed by OldCorpo in 2026 in the Xenon PC-98 translation project
(https://github.com/OldCorpo/Xenon-PC98-Tools), whose tools patched it into the PC-98 game's
disk image. In November 2026 it was audited and corrected in a fork of that project
(`docs/TRANSLATION_GUIDE_EN.md` §11), and the Polish translation was begun there.

The files came from that fork.
Since 2026-11-13 this directory is the master copy and is no longer synchronised with that
repository; the PC-98 patch workflow (merging the text into the game's .CC files, rebuilding a
disk image) stayed there. The port takes the text from these files directly, and translates
the menus the patch could not reach (the load menu, the gallery and music mode, the system
menu: `../source/lang/`).

## Credits

- OldCorpo - Translation completion, Tools
- dvo - Proofreading and translation improvements
- Laukku - Some proofreading
- Asterix - Some proofreading
- Xanderzone - Testing, Support
- Fuzion - Testing, Support

~ based on the original work of ~
- HempTL
- siriusxiv
- esperknight
- MulletDonkey
- OmBadai

The Polish translation was made for this port, from the Japanese original.
