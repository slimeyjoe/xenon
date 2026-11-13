#!/usr/bin/env python3
"""The keys of the translation dictionaries: pointers into the original game's scripts.

A dictionary (text/translation/script_<lang>.txt) is a sequence of entries, a key line and the
translation on the next line (text/docs/TRANSLATION_GUIDE_EN.md §2). A key names a piece of the
original Japanese script by where it is, not by its text: the text is the game's and stays in
the game's files (make data, docs/game-data.md).

  //@S0301A 0F3C 0 9f3c21aa   script, offset of the string token in the decompressed bytecode
                              (hex), segment, CRC-32 of the key text (Shift-JIS)
  segment  n     the n-th text segment of the string: the string cut at the control codes
                 04 x, 05 and 0C, which is what the build translates one by one (tl_extract.py)
           n.k   the k-th line of that segment (a segment with line breaks, matched by line)
           n.j   the whole segment with its line breaks written as spaces
  //-            an inert entry: its key matched nothing in the game (kept so that line numbers
                 stay the same; it has no effect)

The build resolves every key to the Japanese it names and then applies the dictionary by text
as it always has: the last copy of a key wins, and a key translates every place where its text
occurs, not only the one it points to. The CRC makes sure that the place still holds the text
the translation was written for (another version of the game, or changed files, fail).

usage:
  tlkeys.py check FILE...                 every key resolves and its CRC matches
  tlkeys.py view [-o DIR] [LANG...]        the dictionaries with the Japanese written back
                                          (text/view/, not committed), and lines.tsv: each
                                          entry's place, voice clips, Japanese, translations
  tlkeys.py locate TEXT                    the key for a piece of Japanese text
  tlkeys.py convert IN OUT                 a dictionary keyed by Japanese text (the PC-98
                                          projects' format) -> keys as pointers
GAMEDIR (or --game) is the original game's directory, default resources/original/game.
"""
import argparse
import csv
import glob
import os
import re
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)
import cctok   # noqa: E402
import lzss    # noqa: E402

GAMEDIR = os.path.join(ROOT, 'resources', 'original', 'game')
TEXT = os.path.join(ROOT, 'text')
CTL_SEG_RE = re.compile(rb'(\x04.|\x05|\x0C)', re.S)     # the same cut as tl_extract.py
NOT_STORY = ('K', 'L', 'M')                             # gallery, load, music: lang/<lang>/klm.txt
KEY_RE = re.compile(r'^//@(\S+) ([0-9A-F]{4,}) (\d+)(?:\.(\d+|j))? ([0-9a-f]{8})$')
INERT = '//-'


class KeyError_(Exception):
    pass


def crc(text):
    return '%08x' % zlib.crc32(text.encode('shift_jis'))


def segments(val):
    """The text segments of a string token's bytes, in order (control codes left out)."""
    return [p for p in CTL_SEG_RE.split(val) if p and not CTL_SEG_RE.fullmatch(p)]


def decode(b):
    """A byte segment as the dictionary writes it (Python's shift_jis), or None."""
    try:
        return b.decode('shift_jis')
    except UnicodeDecodeError:
        return None


class Game:
    """The story scripts of the original game: name -> {string offset: bytes}, in game order."""

    def __init__(self, gamedir=None):
        self.gamedir = gamedir or os.environ.get('GAMEDIR') or GAMEDIR
        files = sorted(glob.glob(os.path.join(self.gamedir, '*.CC')))
        if not files:
            raise KeyError_('no game scripts in %s: import the game first (make data SRC=..., '
                            'docs/game-data.md)' % self.gamedir)
        self.strings = {}
        for f in files:
            name = os.path.basename(f)[:-3].upper()
            if name in NOT_STORY:
                continue
            data = lzss.unpack_file(open(f, 'rb').read())
            self.strings[name] = {t.pos: t.val for t in cctok.tokenize(data) if t.kind == 'str'}
        self._where = None

    def text(self, script, off, seg, form):
        """The key text at a place, or raise."""
        try:
            val = self.strings[script][off]
        except KeyError:
            raise KeyError_('%s %04X: no string there' % (script, off))
        segs = segments(val)
        if seg >= len(segs):
            raise KeyError_('%s %04X: no segment %d' % (script, off, seg))
        s = segs[seg]
        if form is None:
            t = decode(s)
        elif form == 'j':
            t = decode(s)
            t = t.replace('\n', ' ') if t is not None else None
        else:
            pieces = s.split(b'\n')
            if int(form) >= len(pieces):
                raise KeyError_('%s %04X %d: no line %s' % (script, off, seg, form))
            t = decode(pieces[int(form)])
        if t is None:
            raise KeyError_('%s %04X %d: not Shift-JIS text' % (script, off, seg))
        return t

    def places(self):
        """text -> the first place (script, off, seg, form) where it is a key, in game order:
        whole segments, then their lines, then the joined form."""
        if self._where is None:
            w = {}
            for name in self.strings:
                for off, val in self.strings[name].items():
                    for i, s in enumerate(segments(val)):
                        t = decode(s)
                        if t is not None:
                            w.setdefault(t, (name, off, i, None))
                        if b'\n' in s:
                            for k, p in enumerate(s.split(b'\n')):
                                tp = decode(p)
                                if p and tp is not None:
                                    w.setdefault(tp, (name, off, i, str(k)))
                            if t is not None:
                                w.setdefault(t.replace('\n', ' '), (name, off, i, 'j'))
            self._where = w
        return self._where

    def locate(self, text):
        """The key line for a text, or None if the text is nowhere in the story."""
        p = self.places().get(text)
        return format_key(*p, crc(text)) if p else None


def format_key(script, off, seg, form, c):
    return '//@%s %04X %d%s %s' % (script, off, seg, '' if form is None else '.' + form, c)


def parse_key(line):
    """A key line -> (script, off, seg, form, crc); INERT -> None; anything else raises."""
    if line == INERT:
        return None
    m = KEY_RE.match(line)
    if not m:
        raise KeyError_('not a key line: %r (a key by Japanese text? tlkeys.py convert)' % line[:60])
    return m.group(1), int(m.group(2), 16), int(m.group(3)), m.group(4), m.group(5)


def resolve(game, line):
    """A key line -> the Japanese text it names (None for an inert entry); checks the CRC."""
    k = parse_key(line)
    if k is None:
        return None
    script, off, seg, form, c = k
    t = game.text(script, off, seg, form)
    if crc(t) != c:
        raise KeyError_('%s %04X %d%s: the game has other text there than the translation was '
                        'written for (CRC %s, expected %s)'
                        % (script, off, seg, '' if form is None else '.' + form, crc(t), c))
    return t


def entries(path, game):
    """[(line number of the key, Japanese or None, translation, raw key line)] in file order."""
    lines = [l.rstrip('\r') for l in open(path, encoding='utf-8').read().split('\n')]
    out = []
    for i, l in enumerate(lines):
        if l.startswith('//') and i + 1 < len(lines):
            out.append((i + 1, resolve(game, l), lines[i + 1], l))
    return out


def load_dict(path, game):
    """The dictionary as the build uses it: Japanese -> translation, later copies win
    (text mode, universal newlines; a key line takes the next line, whatever it is)."""
    lines = open(path, encoding='utf-8').readlines()
    d = {}
    i = 0
    while i < len(lines):
        line = lines[i].rstrip('\n')
        if line.startswith('//'):
            if i + 1 < len(lines):
                jp = resolve(game, line)
                if jp is not None:
                    d[jp] = lines[i + 1].rstrip('\n')
                i += 2
            else:
                i += 1
        else:
            i += 1
    return d


# ---- commands --------------------------------------------------------------------------------

def cmd_check(a, game):
    bad = 0
    for path in a.files:
        n = inert = 0
        lines = open(path, encoding='utf-8').read().split('\n')
        for i, l in enumerate(lines):
            l = l.rstrip('\r')
            if not l.startswith('//'):
                continue
            try:
                if resolve(game, l) is None:
                    inert += 1
                n += 1
            except KeyError_ as e:
                print('%s:%d: %s' % (os.path.relpath(path), i + 1, e))
                bad += 1
        print('%s: %d keys (%d inert), %d bad' % (os.path.relpath(path), n, inert, bad))
    return 1 if bad else 0


def voice_cues(path=None):
    """(script, string offset) -> [(clip, match)] from resources/voice/voicemap.tsv."""
    path = path or os.path.join(ROOT, 'resources', 'voice', 'voicemap.tsv')
    cues = {}
    if os.path.exists(path):
        with open(path, encoding='utf-8') as f:
            for r in csv.DictReader(f, delimiter='\t'):
                cues.setdefault((r['script'].upper(), int(r['offset'], 16)), []).append(
                    (r['clip'], r['match']))
    return cues


def cmd_view(a, game):
    langs = a.langs or sorted(re.match(r'script_(\w+)\.txt$', os.path.basename(p)).group(1)
                              for p in glob.glob(os.path.join(TEXT, 'translation', 'script_*.txt')))
    os.makedirs(a.out, exist_ok=True)
    cues = voice_cues()
    table = {}
    for lang in langs:
        src = os.path.join(TEXT, 'translation', 'script_%s.txt' % lang)
        raw = open(src, encoding='utf-8', newline='').read()
        out = []
        for line in raw.split('\n'):
            body, cr = (line[:-1], '\r') if line.endswith('\r') else (line, '')
            if body.startswith('//') and body != INERT:
                body = '//' + resolve(game, body)
            out.append(body + cr)
        dst = os.path.join(a.out, 'script_%s.txt' % lang)
        with open(dst, 'w', encoding='utf-8', newline='') as f:
            f.write('\n'.join(out))
        for ln, jp, tl, key in entries(src, game):
            row = table.setdefault(ln, {'key': key, 'jp': jp})
            row[lang] = tl
        print('%s: the dictionary with the Japanese written back' % os.path.relpath(dst))
    dst = os.path.join(a.out, 'lines.tsv')
    with open(dst, 'w', encoding='utf-8') as f:
        f.write('line\tscript\toffset\tsegment\tvoice\tjapanese\t%s\n' % '\t'.join(langs))
        for ln in sorted(table):
            r = table[ln]
            k = parse_key(r['key'])
            if k:
                script, off, seg, form, _ = k
                v = ' '.join('%s%s' % (clip, '' if m in ('exact', 'override') else '(%s)' % m)
                             for clip, m in cues.get((script, off), []))
                place = [script, '%04X' % off, '%d%s' % (seg, '' if form is None else '.' + form)]
            else:
                v, place = '', ['-', '-', '-']
            f.write('\t'.join(['%d' % ln] + place + [v, r['jp'] or '(inert)'] +
                              [r.get(lang, '').replace('\t', ' ') for lang in langs]) + '\n')
    print('%s: every entry, its place, its voice clips (fuzzy = the voice says something a '
          'little different), the Japanese and the translations' % os.path.relpath(dst))
    return 0


def cmd_locate(a, game):
    k = game.locate(a.text)
    print(k or 'not in the story scripts')
    return 0 if k else 1


def cmd_convert(a, game):
    """Rewrite text keys as pointers, line for line (nothing else changes). A key whose text
    is nowhere in the story becomes inert: it cannot have matched anything."""
    raw = open(a.input, encoding='utf-8', newline='').read()
    out, n, inert = [], 0, []
    for i, line in enumerate(raw.split('\n')):
        body, cr = (line[:-1], '\r') if line.endswith('\r') else (line, '')
        if body.startswith('//') and not body.startswith('//@') and body != INERT:
            key = body[2:]
            new = game.locate(key)
            if new is None:
                new = INERT
                inert.append(i + 1)
            elif resolve(game, new) != key:
                raise KeyError_('line %d: the pointer does not give the key back' % (i + 1))
            body = new
            n += 1
        elif body.startswith('!//'):
            body = '!' + INERT               # a disabled entry: never read by the build
            inert.append(i + 1)
        out.append(body + cr)
    with open(a.output, 'w', encoding='utf-8', newline='') as f:
        f.write('\n'.join(out))
    print('%s: %d keys, %d of them inert (%s)' % (a.output, n, len(inert),
                                                  ' '.join('L%d' % x for x in inert[:60])))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    ap.add_argument('--game', help='the original game directory (default $GAMEDIR or %s)'
                    % os.path.relpath(GAMEDIR))
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('check')
    p.add_argument('files', nargs='+')
    p = sub.add_parser('view')
    p.add_argument('-o', '--out', default=os.path.join(TEXT, 'view'))
    p.add_argument('langs', nargs='*')
    p = sub.add_parser('locate')
    p.add_argument('text')
    p = sub.add_parser('convert')
    p.add_argument('input')
    p.add_argument('output')
    a = ap.parse_args()
    try:
        game = Game(a.game)
        return {'check': cmd_check, 'view': cmd_view, 'locate': cmd_locate,
                'convert': cmd_convert}[a.cmd](a, game)
    except KeyError_ as e:
        sys.exit('tlkeys: %s' % e)


if __name__ == '__main__':
    sys.exit(main())
