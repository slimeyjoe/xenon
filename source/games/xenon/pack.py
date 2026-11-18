# SPDX-License-Identifier: GPL-3.0-or-later
"""The tool side of Xenon ～夢幻の肢体～ for tools/mkpack.py: what goes into its pack besides
the game's files and the profile.

LANGS        the languages in the order of their ids (a save records the id it was made in)
USER_GLYPH   the program file and offset of the engine's user-defined character JIS 7621h
             (the page-overflow "return" glyph): DGROUP 0F9F:09B2 of this AGS.EXE, after a
             2-byte header, 16 rows of two bytes, left byte first
fix_data_bugs(name, data)   with --fix-data-bugs: the script's bytes with the game's known data
             bugs patched (docs/re/sound.md §4.2, scripts.md §6)
"""

LANGS = ('ja', 'en', 'pl')
USER_GLYPH = ('AGS.EXE', 0xE00 + 0x0F9F * 16 + 0x9B2 + 2)


def fix_data_bugs(name, data):
    data = bytearray(data)
    if name.upper() == 'M':
        # music mode "Ago" loads the nonexistent xenon_102 (sound.md §4.2): the intended file is
        # XENON_10. Rewrite the FD string in place (length byte + text + NUL); nothing
        # references absolute offsets except page searches, which scan tokens, so the shorter
        # stream is fine.
        i = data.find(b'\xfd\x09xenon_102\x00')
        if i >= 0:
            data[i:i + 12] = b'\xfd\x08xenon_10\x00'
    # K.CC's second PAGE 23 (scripts.md §6) is unreachable; renumbering it is too invasive for
    # a data patch, so it stays
    return bytes(data)
