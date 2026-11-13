#!/usr/bin/env python3
"""charset_pl.py: the one table of characters allowed in each translation, and their on-screen width.

Every text tool (fitcheck.py, lint_translation.py, set_line.py) measures and validates text through
this module, so a change to the character contract is made here and nowhere else.

  en  Python's 'shift_jis' codec: ASCII and the full-width characters of the game's font (what the
      PC-98 patch could write, and what the port's English fonts can draw). Width = encoded bytes.
  pl  shift_jis plus the 18 Polish letters and the few extra Latin letters below (é/à for "déjà vu").
      Each of these letters is ONE column (half-width), like ASCII: the port draws them so
      (source/fonts/latin8-pl.txt), and its build reads this table (source/tools/langs.py, which
      also checks POLISH_WIDTH), so the translators' checks and the engine agree by construction.
      Characters that shift_jis can encode but that we don't want in the Polish script (typographic
      quotes and dashes render full-width, so they cost 2 bytes and look wrong; '~' is byte 0x7E, which the
      PC-98 font draws as an overline) are in PL_FORBIDDEN.
"""
POLISH = 'ąćęłńóśźżĄĆĘŁŃÓŚŹŻ' + 'éàÉÀ'   # Polish letters + extra Latin letters (déjà vu), drawn by the port
POLISH_WIDTH = 1
PL_FORBIDDEN = set('„”“‘’«»–—…~')
LANGS = ('en', 'pl')

def char_ok(c, lang='en'):
    if lang == 'pl':
        if c in POLISH: return True
        if c in PL_FORBIDDEN: return False
    try:
        c.encode('shift_jis'); return True
    except UnicodeEncodeError:
        return False

def char_width(c, lang='en'):
    if lang == 'pl' and c in POLISH: return POLISH_WIDTH
    return len(c.encode('shift_jis', 'replace'))

def bad_chars(s, lang='en'):
    """Sorted unique characters of s that are not allowed in this language."""
    return sorted(set(c for c in s if not char_ok(c, lang)))

def blen(s, lang='en'):
    """On-screen width of s in bytes (columns of the 60-column text window)."""
    if lang == 'en': return len(s.encode('shift_jis', 'replace'))
    return sum(char_width(c, lang) for c in s)

def parse_lang(argv):
    """Remove '--lang X' / '--lang=X' from argv (in place) and return X (default 'en')."""
    lang = 'en'
    for i, a in enumerate(list(argv)):
        if a.startswith('--lang='):
            lang = a.split('=', 1)[1]; argv.remove(a); break
        if a == '--lang' and i + 1 < len(argv):
            lang = argv[i + 1]; del argv[i:i + 2]; break
    if lang not in LANGS:
        raise SystemExit(f'unknown --lang {lang!r} (choose from {", ".join(LANGS)})')
    return lang

def default_translation(repo, lang):
    import os
    return os.path.join(repo, 'translation', 'script_%s.txt' % lang)
