#!/usr/bin/env python3
"""The translations against the original game and against each other. usage: check_text.py GAMEDIR

For every dictionary text/translation/script_<lang>.txt:
  * every key resolves to the place it names in the game's scripts and its CRC matches
    (tools/tlkeys.py): the translation was written for this text;
  * it is line-aligned with script_en.txt: the same key lines at the same line numbers, so every
    "Lnnnn" in the guides, the notes and the lint output means the same entry in every language;
  * every story string comes out of the build with no Japanese left in it (tools/tl_extract.py's
    translate): nothing in the story is untranslated;
  * every voiced part (resources/voice/voicemap.tsv: script, string, part = the text after the
    n-th key wait) has translated text: the voice never plays over Japanese or an empty part.
Exit 1 on any failure; the first ones of each kind are printed."""
import csv
import glob
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, os.path.join(HERE, '..', 'tools'))
import tlkeys       # noqa: E402
import tl_extract   # noqa: E402

JAPANESE = re.compile('[぀-ヿ一-鿿]')      # kana and kanji
BLANK = ' \n　\x05'


def key_lines(path):
    lines = [l.rstrip('\r') for l in open(path, encoding='utf-8').read().split('\n')]
    return len(lines), [(i + 1, l) for i, l in enumerate(lines) if l.startswith(('//', '!//'))]


def main():
    game = tlkeys.Game(sys.argv[1])
    dicts = sorted(glob.glob(os.path.join(ROOT, 'text', 'translation', 'script_*.txt')))
    ref = os.path.join(ROOT, 'text', 'translation', 'script_en.txt')
    ref_n, ref_keys = key_lines(ref)
    cues = list(csv.DictReader(open(os.path.join(ROOT, 'resources', 'voice', 'voicemap.tsv'),
                                    encoding='utf-8'), delimiter='\t'))
    fails = 0

    def fail(kind, items):
        nonlocal fails
        if items:
            fails += len(items)
            print('FAIL %s: %d' % (kind, len(items)))
            for x in items[:10]:
                print('    ' + x)

    for path in dicts:
        name = os.path.basename(path)
        # the dictionary as the build reads it (tlkeys.load_dict), but every bad key reported
        bad, d = [], {}
        lines = open(path, encoding='utf-8').readlines()
        i = 0
        while i < len(lines):
            line = lines[i].rstrip('\n')
            if line.startswith('//') and i + 1 < len(lines):
                try:
                    jp = tlkeys.resolve(game, line)
                    if jp is not None:
                        d[jp] = lines[i + 1].rstrip('\n')
                except tlkeys.KeyError_ as e:
                    bad.append('L%d: %s' % (i + 1, e))
                i += 2
            else:
                i += 1
        fail('%s: keys that do not resolve' % name, bad)
        n, keys = key_lines(path)
        if path != ref:
            first = next(('L%d: %s, but %s L%d: %s' % (a[0], a[1][:44], os.path.basename(ref), b[0], b[1][:44])
                          for a, b in zip(keys, ref_keys) if a != b), None)
            if first or n != ref_n or len(keys) != len(ref_keys):
                fail('%s: not line-aligned with %s' % (name, os.path.basename(ref)),
                     ['%d lines, %d keys (%s: %d, %d); first difference %s'
                      % (n, len(keys), os.path.basename(ref), ref_n, len(ref_keys), first or 'at the end')])
        left = []
        for script, strs in game.strings.items():
            for off, val in strs.items():
                out = tl_extract.translate(val, d)
                if JAPANESE.search(out):
                    left.append('%s %04X: %s' % (script, off, out.replace('\n', '\\n')[:60]))
        fail('%s: story strings with Japanese left' % name, left)
        voice = []
        for c in cues:
            script, off, part = c['script'].upper(), int(c['offset'], 16), int(c['part'])
            val = game.strings[script][off]
            ja = val.split(b'\x0c')[part].decode('cp932')
            tl = tl_extract.translate(val, d).split('\x0c')[part]
            if JAPANESE.search(ja) and (JAPANESE.search(tl) or not tl.strip(BLANK)):
                voice.append('%s %04X part %d (%s): %r' % (script, off, part, c['clip'], tl[:40]))
        fail('%s: voiced parts without a translation' % name, voice)
        print('%s: %d keys, line-aligned, every story string and voiced part translated'
              % (name, len(keys)) if not (bad or left or voice) else '%s: checked' % name)
    print('check_text: %d dictionaries, %d failures' % (len(dicts), fails))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
