#!/usr/bin/env python3
"""set_line.py: replace the translation of one entry (or many) in a translation file, safely.

Usage: python3 tools/set_line.py [--lang en|pl] [--pad] [--file PATH] <L> "<text>"
       python3 tools/set_line.py [--lang en|pl] [--pad] [--file PATH] --tsv FILE [--dry-run]
  <L>     the line number of the key line (//@..., the "Lnnnn" used in docs and in lint output).
          The translation line right below it is replaced.
  --lang  en (default): translation/script_en.txt, shift_jis only.
          pl: translation/script_pl.txt, shift_jis + Polish letters (tools/charset_pl.py).
  --pad   re-flow the text with padding so no word splits across 60-byte rows (same as fitcheck.py --pad).
          Lines containing a literal \\n and menu options (◆) are never re-flowed.
  --tsv   apply many lines at once: each row is "<L><TAB><text>[<TAB>note]"; '#' rows and blank rows
          are skipped. All rows are validated first; if any row fails, nothing is written.
  --dry-run  with --tsv: validate and report, write nothing.

Guards:
- line L must be a key line pointing at the game (not an inert //- entry), and line L+1 must not be;
- the text must be encodable for the language and fit (240 bytes, or 60 for a ◆ menu option);
- the line ending (CRLF or LF) of each replaced line is preserved; nothing else in the file is touched.
"""
import sys, os
sys.dont_write_bytecode = True  # don't leave tools/__pycache__ behind
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import charset_pl, fitcheck

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, '..', 'source', 'tools'))
import tlkeys   # noqa: E402
_GAME = []


def japanese(keyline):
    """The Japanese a key line points at (None for an inert entry), from the game's data."""
    try:
        if not _GAME: _GAME.append(tlkeys.Game())
        return tlkeys.resolve(_GAME[0], keyline)
    except tlkeys.KeyError_ as e:
        raise SystemExit(f'set_line: {e}')

def prepare(raw, n, text, dopad, lang):
    """Validate one replacement. Returns (new_text, jp, error_or_None)."""
    if not (1 <= n < len(raw)) or not raw[n - 1].startswith(b'//'):
        return text, '', f'line {n} is not a key line. Pass the number of the // line (the one above the translation).'
    if raw[n].startswith(b'//') or raw[n].startswith(b'!//'):
        return text, '', f'line {n + 1} is not a translation line.'
    jp = japanese(raw[n - 1].decode('utf-8').rstrip('\r'))
    if jp is None:
        return text, '', f'L{n} is an inert entry (//-): it has no effect on the game. Edit the live entry for this text.'
    menu = jp.lstrip('　 ').startswith('◆')
    if dopad and not menu and '\\n' not in text: text = fitcheck.pad(text)
    bad = charset_pl.bad_chars(text, lang)
    if bad:
        return text, jp, f'L{n}: not encodable for --lang {lang}: {"".join(bad)!r}'
    limit = 60 if menu else 240
    if charset_pl.blen(text, lang) > limit:
        return text, jp, f'L{n}: {charset_pl.blen(text, lang)} bytes > {limit}. Shorten it.'
    return text, jp, None

def write_line(raw, n, text):
    old = raw[n].decode('utf-8').rstrip('\r')
    cr = b'\r' if raw[n].endswith(b'\r') else b''
    raw[n] = text.encode('utf-8') + cr
    return old, cr

def main(argv):
    argv = list(argv)
    lang = charset_pl.parse_lang(argv)
    fitcheck.LANG = lang
    path = charset_pl.default_translation(REPO, lang)
    dopad = False; tsv = None; dry = False; args = []
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == '--pad': dopad = True
        elif a == '--file': path = argv[i + 1]; i += 1
        elif a == '--tsv': tsv = argv[i + 1]; i += 1
        elif a == '--dry-run': dry = True
        elif a in ('-h', '--help'): print(__doc__); return 0
        else: args.append(a)
        i += 1
    raw = open(path, 'rb').read().split(b'\n')

    if tsv is not None:
        rows = []
        for k, line in enumerate(open(tsv, encoding='utf-8').read().split('\n'), 1):
            if not line.strip() or line.startswith('#'): continue
            parts = line.split('\t')
            try: n = int(parts[0].lstrip('Ll'))
            except ValueError: print(f'ERROR: {tsv}:{k}: first column is not a line number'); return 1
            if len(parts) < 2: print(f'ERROR: {tsv}:{k}: no text column'); return 1
            rows.append((n, parts[1]))
        seen, errors, ready = set(), [], []
        for n, text in rows:
            if n in seen: errors.append(f'L{n}: appears twice in {tsv}'); continue
            seen.add(n)
            text, jp, err = prepare(raw, n, text, dopad, lang)
            if err: errors.append(err)
            else: ready.append((n, text))
        if errors:
            print(f'ERROR: {len(errors)} of {len(rows)} rows failed; nothing written.')
            for e in errors: print('  ' + e)
            return 1
        changed = 0
        for n, text in ready:
            if raw[n].decode('utf-8').rstrip('\r') != text: changed += 1
            write_line(raw, n, text)
        if dry:
            print(f'dry run: {len(ready)} rows valid, {changed} would change. Nothing written.'); return 0
        open(path, 'wb').write(b'\n'.join(raw))
        print(f'{len(ready)} rows applied to {os.path.relpath(path, REPO)} ({changed} changed).')
        return 0

    if len(args) != 2:
        print(__doc__); return 2
    n, text = int(args[0].lstrip('Ll')), args[1]
    text, jp, err = prepare(raw, n, text, dopad, lang)
    if err:
        print('ERROR: ' + err); return 1
    old, cr = write_line(raw, n, text)
    open(path, 'wb').write(b'\n'.join(raw))
    print(f'L{n} {jp}\n  old: {old}\n  new: {text}\n  ({charset_pl.blen(text, lang)} bytes, {"CRLF" if cr else "LF"} kept)')
    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
