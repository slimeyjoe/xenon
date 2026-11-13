"""The languages of the pack and their text settings: one table for mkpack.py and tl_extract.py.

  SETTINGS[lang] = (encoding, wrap)     written to the pack's "lang" entry (engine/text/enc.c)
  narrow(lang)                          the 1-column characters of a UTF-8 language besides
                                        ASCII; every other character is a 16-wide glyph. Must
                                        agree with the table the translators measure line
                                        lengths with (text/tools/charset_pl.py for Polish).
  width(c, lang)                        columns of one character, as the engine counts them
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TEXT_TOOLS = os.path.join(HERE, '..', '..', 'text', 'tools')

SETTINGS = {
    'ja': ('cp932', 'kinsoku'),
    'en': ('utf-8', 'word'),
    'pl': ('utf-8', 'word'),
}

_NARROW_EXTRA = {
    'en': '',
}


def _charset_pl():
    """The translators' character table (text/tools/charset_pl.py) is the source of the Polish
    narrow letters, so their line-length checks and the engine agree by construction."""
    import importlib.util
    spec = importlib.util.spec_from_file_location('charset_pl', os.path.join(TEXT_TOOLS, 'charset_pl.py'))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    if mod.POLISH_WIDTH != 1:
        raise ValueError('charset_pl.POLISH_WIDTH is %r; the engine draws these letters 1 column wide'
                         % mod.POLISH_WIDTH)
    return mod.POLISH


def narrow(lang):
    if lang == 'pl' and 'pl' not in _NARROW_EXTRA:
        _NARROW_EXTRA['pl'] = _charset_pl()
    if lang not in _NARROW_EXTRA:
        raise KeyError('no narrow table for %r' % lang)
    return _NARROW_EXTRA[lang]


def width(c, lang):
    """Columns of one Unicode character: control codes 0, ASCII and narrow() 1, else 2.
    For cp932 languages the cp932 byte length (1 = JIS X 0201)."""
    if ord(c) < 0x20:
        return 0
    if SETTINGS[lang][0] == 'cp932':
        return len(c.encode('cp932', 'replace'))
    return 1 if ord(c) < 0x80 or c in narrow(lang) else 2
