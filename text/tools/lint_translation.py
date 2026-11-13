#!/usr/bin/env python3
"""lint_translation.py: catch the known pipeline traps in a translation file.

Usage:
  python3 tools/lint_translation.py [--lang en|pl] [--all] [--file PATH]   scan the whole file
  python3 tools/lint_translation.py [--lang en|pl] --line L [--file PATH]  explain one entry: is it live, where is it used
  --lang en (default) checks translation/script_en.txt; --lang pl checks translation/script_pl.txt
  (character table: tools/charset_pl.py; rules: docs/TRANSLATION_GUIDE_PL.md).

Line numbers ("L1234") are always the line of the key line (//@...), the same convention as the
docs and tools/set_line.py. The English is on line L+1. The keys point into the original game's
scripts (../source/tools/tlkeys.py), so the tool needs the game's data (make data,
docs/game-data.md); text/view/ shows the dictionaries with the Japanese written back.

Whole-file checks:
  ERROR  English not encodable with shift_jis (e.g. em dash, en dash, accented letters)
  ERROR  menu option (◆) longer than 60 bytes: the menu clears only 60 columns on close, leaving residue
  WARN   English longer than 240 bytes (4 rows): the engine pages it behind a key press
  WARN   speaker tag does not match the Japanese tag (宏治 = Kouji, コウジ = Kohji, ...)
  WARN   Japanese line repeated with different English: only the LAST copy is used in-game
  INFO   inert entry (//-): its key matched nothing in the game, so the line has no effect (the
         PC-98 projects' dead keys, e.g. with ～ U+FF5E that shift_jis can't encode; listed with --all)
Extra checks with --lang pl (the English file is the reference for "untranslated"):
  ERROR  a glossary term that is banned (katalizator, kobieta-medium, deja vu without accents; guide §0/§6)
  UNTRANSLATED  still identical to the English line (counted; listed with --all). Untranslated lines
         are skipped by every other check.
  WARN   Japanese honorific (-san/-kun/-sama/-sensei/-chan) or Japanese laugh (Fufu, Kukuku, Hmph...) left in
  WARN   gendered 1st-person form in a gender-neutral zone (prologue dream L90-L345, DREAM_04 opening
         L17541-L17549; guide §3)
  PROGRESS  translated / kept / untranslated counts
Exit code 1 if there is any ERROR. Some issues are known and pre-existing (see
docs/TRANSLATION_GUIDE_EN.md §10). Compare against a saved baseline rather than expecting a clean run.
"""
import sys, os, re, glob, collections
sys.dont_write_bytecode = True  # don't leave tools/__pycache__ behind
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import charset_pl
LANG = 'en'

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, '..', 'source', 'tools'))
import tlkeys   # noqa: E402
_GAME = []
# the original game's scripts, decompressed: `make data` derives them (docs/game-data.md)
SCRIPTS = os.path.join(REPO, '..', 'resources', 'scripts', 'decompressed')
TAG = {'宏治': 'Kouji', 'コウジ': 'Kohji', 'トレイシー': 'Tracy', '涼子': 'Ryouko', 'ランファ': 'Lanhua',
       'ファル': 'Far', '沙夜香': 'Sayaka', '舞': 'Mai', '恵': 'Megumi', 'ビアンカ': 'Bianca'}

def enc_ok(s):
    try: s.encode('shift_jis'); return True
    except UnicodeEncodeError: return False

def tl_ok(s): return enc_ok(s) if LANG == 'en' else not charset_pl.bad_chars(s, LANG)

def blen(s): return charset_pl.blen(s.replace('\\n', '\n'), LANG)

def twin_of(jp): return jp.replace('～', '〜').replace('－', '−')

def load(path):
    """[(line of the key, Japanese or None for an inert entry, translation)]."""
    try:
        if not _GAME: _GAME.append(tlkeys.Game())
        return [(ln, jp, tl) for ln, jp, tl, _ in tlkeys.entries(path, _GAME[0])]
    except tlkeys.KeyError_ as e:
        raise SystemExit(f'lint_translation: {os.path.relpath(path)}: {e}')

def scripts_using(jp):
    """Game scripts (the original Japanese, decompressed: SCRIPTS/S*.bin) whose bytes contain this
    Japanese text; None when the scripts are not there."""
    try: b = jp.replace('\\n', '\n').encode('shift_jis')
    except UnicodeEncodeError: b = twin_of(jp).replace('\\n', '\n').encode('shift_jis', 'replace')
    # match whole strings only: FD <len> <text> 00 (the length byte excludes the NUL)
    whole = b'\xfd' + bytes([len(b) & 0xff]) + b + b'\x00'
    files = sorted(glob.glob(os.path.join(SCRIPTS, 'S*.bin')))
    if not files:
        return None
    hits = []
    for f in files:
        if whole in open(f, 'rb').read(): hits.append(os.path.basename(f).split('.')[0])
    return hits

def explain(pairs, n):
    by = {ln: (jp, en) for ln, jp, en in pairs}
    if n not in by:
        print(f'L{n} is not a //Japanese line (the English of entry L{n - 1}?)'); return 1
    jp, en = by[n]
    if jp is None:
        print(f'L{n}  INERT: its key matched nothing in the game, so this entry has no effect.\n'
              f'      EN (L{n + 1}): {en}')
        return 0
    copies = [ln for ln, j, _ in pairs if j == jp]
    print(f'L{n}  JP: {jp}\n      EN (L{n + 1}): {en}')
    if not enc_ok(jp):
        tw = [ln for ln, j, _ in pairs if j == twin_of(jp)]
        print('DEAD KEY: this entry never matches the game.', f'Edit the live twin instead: L{tw[-1]}.' if tw else
              'No twin with the same text. The game text is probably matched by repair entries for pieces of it (L19503+); search there for a fragment.')
    elif len(copies) > 1:
        print(f'DUPLICATE: the same Japanese is at {", ".join("L%d" % c for c in copies)}. '
              f'The game uses the English of the LAST copy, L{copies[-1]}, for every occurrence.'
              + ('' if copies[-1] == n else f' Edit L{copies[-1]}, not L{n}.'))
    else:
        print('LIVE: unique entry; editing it changes the game.')
    used = scripts_using(jp)
    if used is None:
        print('Used in game scripts: (unknown: the original scripts are not in %s; make data, docs/game-data.md)'
              % os.path.normpath(SCRIPTS))
    else:
        print('Used in game scripts:', ', '.join(used) if used else
              '(not found as one string: probably a split string or a line-break string matched in pieces)')
    return 0

def scan(pairs, show_all):
    out = collections.defaultdict(list)
    last, by_jp = {}, collections.defaultdict(list)
    for ln, jp, en in pairs:
        last[jp] = ln; by_jp[jp].append((ln, en))
    same_as_en = set()
    if LANG == 'pl':   # untranslated lines are only counted (scan_pl), not checked line by line
        en_by = {ln: en for ln, _, en in load(os.path.join(REPO, 'translation', 'script_en.txt'))}
        same_as_en = {ln for ln, _, t in pairs if en_by.get(ln) == t}
    for ln, jp, en in pairs:
        if jp is None:
            out['INFO'].append(f'L{ln}: inert entry (no effect)')
        elif not enc_ok(jp):
            t = last.get(twin_of(jp))
            out['INFO'].append(f'L{ln}: dead key' + (f'; live twin L{t}' if t else '; no twin (see --line)'))
        if ln in same_as_en: continue
        if not tl_ok(en):
            bad = ''.join(charset_pl.bad_chars(en, LANG))
            what = 'English has non-shift_jis characters' if LANG == 'en' else f'translation has characters not allowed for --lang {LANG}'
            out['ERROR'].append(f'L{ln}: {what} {bad!r}' + ('' if jp is not None and enc_ok(jp) else ' (dead entry)'))
        if jp is None:
            continue
        if jp.lstrip('　 ').startswith('◆') and blen(en) > 60:
            out['ERROR'].append(f'L{ln}: menu option is {blen(en)} bytes (max 60)')
        elif blen(en) > 240:
            out['WARN'].append(f'L{ln}: {blen(en)} bytes > 240, pages over')
        m = re.match(r'［([^］]*)］', jp); m2 = re.match(r'\[([^\]]*)\]', en)
        tag = TAG if LANG == 'en' else {**TAG, **TAG_PL}
        if m and m.group(1) in tag and (not m2 or m2.group(1) != tag[m.group(1)]):
            out['WARN'].append(f'L{ln}: tag {m2.group(1) if m2 else "(none)"} but JP ［{m.group(1)}］ = {tag[m.group(1)]}')
    for jp, v in by_jp.items():
        if jp is not None and len(set(e for _, e in v)) > 1 and enc_ok(jp):
            out['WARN'].append(f'L{v[-1][0]}: Japanese repeated at ' + ', '.join(f'L{x}' for x, _ in v) +
                               f' with different {"English" if LANG == "en" else "translations"}; the game uses L{v[-1][0]} for all'
                               + ('' if LANG == 'en' or v[-1][0] not in same_as_en else ' (and L%d is still untranslated)' % v[-1][0]))
    if LANG == 'pl':
        scan_pl(pairs, out); print('PROGRESS: ' + out['PROGRESS'][0])
    for k in ('ERROR', 'WARN', 'INFO', 'UNTRANSLATED'):
        if k == 'UNTRANSLATED' and LANG == 'en': continue
        print(f'{k}: {len(out[k])}')
        for x in (out[k] if (k in ('ERROR', 'WARN') or show_all) else []): print('  ' + x)
    return 1 if out['ERROR'] else 0

TAG_PL = {'私': 'Ja', 'オレ': 'Ja', '男': 'Mężczyzna', '男の声': 'Męski głos', '女': 'Kobieta',
          '女の声': 'Kobiecy głos', '目の前の女性': 'Kobieta przede mną', '目の前の女': 'Kobieta przede mną',
          '右の女': 'Kobieta po prawej', '左の女': 'Kobieta po lewej', '女性一同': 'Wszystkie kobiety'}
BANNED_PL = [(r'katalizator', '触媒 = "medium" (guide §0)'), (r'kobiet\w*-medium', '触媒の女 = just "medium" (guide §0)'),
             (r'\bdeja vu\b', '既視感覚 = "déjà vu" with accents (guide §6)')]
JP_MARKERS = re.compile(r'(?<=[a-z])-(?:san|kun|sama|sensei|chan|senpai)\b|\b(?:N?[Ff]ufu(?:fu)?|Ufufu|Kukuku|Hmph|Ehehe|Heh)\b')
GENDERED_1P = re.compile(r'\b\w+(?:łem|łam|łabym|łbym)\b|\b(?:sam|sama|sami|nagi|naga)\b', re.I)
NOT_GENDERED = {'prześcieradłem', 'krzesłem', 'światłem', 'hasłem', 'mydłem', 'szkłem', 'piekłem', 'czołem', 'kołem', 'stołem', 'tłem', 'źródłem', 'masłem', 'wiosłem', 'skrzydłem'}  # nouns, instrumental
NEUTRAL_ZONES = [(90, 345), (17541, 17549)]
NAMES_RE = re.compile(r'\b(?:Kouji|Kohji|Ryouko|Sayaka|Mai|Megumi|Tracy|Lanhua|Far|Farneria|Bianca|Hm+|Huh|Mm+|Eh|Ah|Oh|Uh|Ha)\b')

def scan_pl(pairs, out):
    """Polish-only checks. 'Untranslated' is measured against the English file, line by line."""
    en_path = os.path.join(REPO, 'translation', 'script_en.txt')
    en_by = {ln: en for ln, _, en in load(en_path)}
    done = kept = todo = 0
    for ln, jp, pl in pairs:
        en = en_by.get(ln)
        if en is None:
            out['ERROR'].append(f'L{ln}: no such //Japanese line in the English file: the files are no longer line-aligned')
            continue
        if pl != en: done += 1
        elif not re.search(r'[A-Za-z]{2,}', NAMES_RE.sub('', re.sub(r'^\[[^\]]*\]:\s*', '', en))) or en == jp \
                or jp is None:
            kept += 1      # codes, ellipses, SFX-only lines, bare names ("[Kohji]: Tracy."), inert entries
        else:
            todo += 1
            out['UNTRANSLATED'].append(f'L{ln}: {en[:70]}')
        if pl != en:
            for pat, why in BANNED_PL:
                if re.search(pat, pl, re.I): out['ERROR'].append(f'L{ln}: banned term /{pat}/: {why}')
            mk = JP_MARKERS.findall(pl)
            if mk: out['WARN'].append(f'L{ln}: Japanese honorific/laugh left in: {", ".join(sorted(set(mk)))}')
            if any(a <= ln <= b for a, b in NEUTRAL_ZONES):
                g = [w.group(0) for w in GENDERED_1P.finditer(pl) if w.group(0).lower() not in NOT_GENDERED]
                if g: out['WARN'].append(f'L{ln}: gender-neutral zone, gendered form(s): {", ".join(g)} (guide §3)')
    out['PROGRESS'] = [f'{done} translated, {kept} kept as-is (codes/punctuation), {todo} untranslated']

def main(argv):
    global LANG
    argv = list(argv)
    LANG = charset_pl.parse_lang(argv)
    path = charset_pl.default_translation(REPO, LANG); line = None; show_all = False
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == '--file': path = argv[i + 1]; i += 1
        elif a == '--line': line = int(argv[i + 1].lstrip('Ll')); i += 1
        elif a == '--all': show_all = True
        elif a in ('-h', '--help'): print(__doc__); return 0
        else: path = a
        i += 1
    pairs = load(path)
    return explain(pairs, line) if line else scan(pairs, show_all)

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
