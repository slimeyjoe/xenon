"""The text encoding of the language under test, as the engine sees it (engine/text/enc.c).

Trace events carry the engine's text bytes: cp932 for Japanese, UTF-8 for the translations.
configure() reads the language's "lang" entry from the pack (encoding=...) and, for UTF-8, the
code points of its font8 table: those are the 1-column characters, every other one is a
16-wide glyph. Packs without the entry are cp932, as in the engine.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', '..', 'tools'))          # source/tools (xpak, xfont)
import xfont   # noqa: E402
import xpak    # noqa: E402

LANG_IDS = {'ja': xpak.L_JA, 'en': xpak.L_EN, 'pl': xpak.L_PL}

_enc = 'cp932'
_narrow = frozenset()


def configure(pak_path, lang):
    global _enc, _narrow
    lid = LANG_IDS[lang]
    entries = xpak.read(open(pak_path, 'rb').read())

    def find(name, typ):
        for want in (lid, xpak.L_ANY):
            for e in entries:
                if e['name'] == name and e['type'] == typ and e['lang'] == want:
                    return e
        return None

    _enc, _narrow = 'cp932', frozenset()
    meta = find('lang', xpak.T_META)
    if meta and b'encoding=utf-8' in meta['data'].split(b'\n'):
        _enc = 'utf-8'
        _, _, _, table = xfont.read_xfnt(find('font8', xpak.T_FONT)['data'])
        _narrow = frozenset(table)


def encoding():
    return _enc


def decode(b):
    return b.decode(_enc, 'replace')


def _sjis_lead(b):
    return 0x81 <= b <= 0x9F or 0xE0 <= b <= 0xFC


def fullwidth_glyphs(text):
    """16-wide glyphs of a message, each costs one tick in text_print (kanji_draw / big).
    Skips the two-byte {W<d>} (04 d) and '@' codes like the printer."""
    n = 0
    i = 0
    while i < len(text):
        c = text[i]
        if c == 0x04 or c == 0x40:
            i += 2
        elif _enc == 'cp932':
            if _sjis_lead(c) and i + 1 < len(text):
                n += 1
                i += 2
            else:
                i += 1
        elif c < 0x80:
            i += 1
        else:
            k = 1
            while i + k < len(text) and k < 4 and (text[i + k] & 0xC0) == 0x80:
                k += 1
            cp = text[i:i + k].decode('utf-8', 'replace')
            if len(cp) != 1 or ord(cp) not in _narrow:
                n += 1
            i += k
    return n
