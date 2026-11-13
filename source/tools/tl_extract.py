#!/usr/bin/env python3
"""Build the translated script set of one language from its translation dictionary.

usage: tl_extract.py --lang en --dict ../text/translation/script_en.txt --game GAMEDIR
                     --klm lang/en/klm.txt -o OUTDIR [--lint LINT.txt] [--no-reflow]
                     [--compare DIR]

The dictionary (text/translation/script_<lang>.txt, text/README.md) is a key line followed by
its translation; a key points at the Japanese in the game's scripts (tools/tlkeys.py), and the
last copy of a key wins. Each story script (GAMEDIR/*.CC
except K/L/M, LZSS removed) gets its string tokens translated with the semantics of the PC-98
translation patch's build (its xenreplacer.py), which the dictionaries were written against:

  * a string is cut into segments at the control codes 04 d, 05 and 0C and at newlines;
    a segment whose Japanese (Python's shift_jis decoding, as the keys are written) is a key
    is replaced by the translation; anything else stays as it is;
  * if a multi-line segment keeps an untranslated line, the whole segment is tried with its
    newlines written as spaces (the dictionary's form of the S0106 diary, L4375, which the
    PC-98 build had to patch in by hand, and of two DOOR LOCK OK cards); newlines at the
    start or end of the segment are kept;
  * keys are raw text: a literal "\\n" in the dictionary is not a newline (those entries never
    match, in the PC-98 build either).

The translations have the PC-98 patch's 60-column row layout baked in: rows padded with
full-width/ASCII spaces, and the inter-word space lost wherever a word ended exactly at
column 60. This tool then

  * restores the lost spaces (a letter directly followed by a letter across a 60-column row
    boundary of the original layout),
  * removes the padding runs (the engine word-wraps translations itself),
  * keeps layout strings verbatim (title cards: Japanese starting with 2+ full-width spaces,
    or translations with explicit newlines),
  * translates K/L/M.CC with the table in klm.txt (exact Japanese -> translation),
  * re-emits the bytecode in the language's encoding (tools/langs.py) with FD length bytes
    (the engine never reads them; strings over 255 bytes get 255),
  * writes a lint report of every decision that deserves a human look.

Columns are counted as the engine does (tools/langs.py width()). --compare DIR reports every
story string whose translation (before the layout steps) differs from DIR/NAME.U.CC, e.g. the
project's PC-98 build output scripts_merge/, to check the dictionary semantics.
"""
import argparse
import glob
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import cctok   # noqa: E402
import langs   # noqa: E402
import lzss    # noqa: E402
import tlkeys  # noqa: E402

ROW = 60
ROWS = 4
FW_SPACE = '　'
CTL_SEG_RE = re.compile(rb'(\x04.|\x05|\x0C)', re.S)


# ---- the dictionary ------------------------------------------------------------------------
def key_of(b):
    """The dictionary form of a byte segment, or None when shift_jis cannot decode it."""
    try:
        return b.decode('shift_jis')
    except UnicodeDecodeError:
        return None


def translate_segment(seg, d):
    k = key_of(seg)
    if k is not None and k in d:
        return d[k]
    if b'\n' not in seg:
        return seg.decode('cp932')
    pieces = seg.split(b'\n')
    if k is not None and any(p and key_of(p) not in d for p in pieces):
        joined = k.replace('\n', ' ')
        if joined in d:
            # newlines at the start or end are layout (blank lines above/below a title card):
            # keep them; the ones inside became spaces of the running text
            lead = len(k) - len(k.lstrip('\n'))
            trail = len(k) - len(k.rstrip('\n'))
            return '\n' * lead + d[joined] + '\n' * trail
    return '\n'.join(translate_segment(p, d) if p else '' for p in pieces)


def translate(ja, d):
    """One string token (cp932 bytes) through the dictionary -> str."""
    out = []
    for part in CTL_SEG_RE.split(ja):
        if not part:
            continue
        out.append(part.decode('cp932') if CTL_SEG_RE.fullmatch(part) else translate_segment(part, d))
    return ''.join(out)


# ---- layout (columns as the engine counts them) --------------------------------------------
def chars(s, lang):
    """Split text into (unit, columns); control codes have 0 columns."""
    out = []
    i = 0
    while i < len(s):
        c = s[i]
        if c == '\x04' and i + 1 < len(s):
            out.append((s[i:i + 2], 0))
            i += 2
        elif c in '\x05\x0c\x0a\x03':
            out.append((c, 0))
            i += 1
        else:
            out.append((c, langs.width(c, lang)))
            i += 1
    return out


def is_word_char(u, cols):
    return cols == 1 and (u.isalnum() or u in '\'"')


def restore_spaces(s, notes, lang):
    """Simulate the original printer's row breaks over the padded text and insert a space
    where a word was split by a 60-column boundary with no space (the patch's lost spaces)."""
    units = chars(s, lang)
    out = []
    col = 0
    for k, (u, w) in enumerate(units):
        if u == '\n':
            out.append(u)
            col = 0
            continue
        out.append(u)
        col += w
        if col >= ROW:
            col = 0
            # next unit starts a new row; if both sides are word characters a space was lost
            if k + 1 < len(units):
                nxt, nw = units[k + 1]
                if is_word_char(u, w) and is_word_char(nxt, nw):
                    out.append(' ')
                    notes.append('space restored: ...%s|%s...' % (''.join(out)[-12:-1], nxt))
    return ''.join(out)


PAD_RE = re.compile('(?:　| ){2,}')
PAD_ANY_RE = re.compile('(?:　| )+')


def unpad(s):
    """Collapse padding runs to one ASCII space, keep the leading run of the string, drop
    whitespace before control codes and at the end."""
    m = PAD_ANY_RE.match(s)
    lead = m.group(0) if m else ''
    body = s[len(lead):]
    body = PAD_RE.sub(' ', body)
    body = body.replace(FW_SPACE, ' ')
    body = re.sub(' +(?=[\x04\x05\x0C\x0A]|$)', '', body)
    body = re.sub('(?<=[\x0A]) +', '', body)
    return lead + body


def wrap_rows(s, lang):
    """Estimate rows produced by the engine's word wrap (greedy, 60 columns)."""
    rows = 1
    col = 0
    parts = s.split('\n')
    for n, part in enumerate(parts):
        if n:
            rows += 1
            col = 0
        first = True
        for w in part.split(' '):
            wcol = sum(c for _, c in chars(w, lang))
            need = wcol if first else wcol + 1
            if col + need > ROW and not first:
                rows += 1
                col = wcol
            else:
                col += need
            while col > ROW:          # a single word longer than a row is broken hard
                rows += 1
                col -= ROW
            first = False
    return rows


def is_layout(ja, tl):
    """Title cards (Japanese starting with 2+ full-width spaces) and translations with explicit
    newlines. A translation that turned the Japanese newlines into running text (the joined
    form, e.g. the S0106 diary) is reflowed like any other."""
    return ja.startswith(FW_SPACE * 2) or '\n' in tl


def process_string(ja, tl, lint, where, reflow, lang):
    if tl == ja or not reflow:
        return tl                      # resource names and untouched strings
    if is_layout(ja, tl):
        out = re.sub('(?:　| )+(?=[\x04\x05\x0C\x0A]|$)', '', tl)
        lint.append('%s: layout string kept verbatim: %r' % (where, tl))
        return out
    notes = []
    out = restore_spaces(tl, notes, lang)
    out = unpad(out)
    for n in notes:
        lint.append('%s: %s' % (where, n))
    rows = wrap_rows(out, lang)
    orig_rows = max(1, -(-sum(c for _, c in chars(tl, lang)) // ROW))
    if rows > ROWS:
        lint.append('%s: %d rows after wrap (>%d): %r' % (where, rows, ROWS, out))
    elif rows != orig_rows:
        lint.append('%s: row count %d -> %d' % (where, orig_rows, rows))
    return out


def rebuild(data, new_strings, encoding):
    """Re-emit the bytecode with the string tokens replaced (same order), encoded."""
    out = bytearray()
    it = iter(new_strings)
    for t in cctok.tokenize(data):
        if t.kind == 'str':
            s = next(it).encode(encoding)
            out += b'\xFD' + bytes([min(len(s), 255)]) + s + b'\x00'
        else:
            out += data[t.pos:t.end]
    return bytes(out)


def load_klm(path):
    table = {}
    for line in open(path, encoding='utf-8'):
        if not line.strip() or line.startswith('#'):
            continue
        ja, tl = line.rstrip('\n').split('\t')
        table[ja.replace('\\n', '\n')] = tl.replace('\\n', '\n')
    return table


def has_kana_kanji(s):
    """Japanese text (anything in cp932 rows 82h-9Fh/E0h-FCh: kana, kanji, full-width letters)."""
    for c in s:
        b = c.encode('cp932', 'replace')
        if len(b) == 2 and (0x82 <= b[0] <= 0x9F or 0xE0 <= b[0] <= 0xFC):
            return True
    return False


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--lang', required=True)
    ap.add_argument('--dict', required=True, help='translation dictionary (key line, translation; tlkeys.py)')
    ap.add_argument('--game', required=True, help='original game dir (*.CC)')
    ap.add_argument('--klm', required=True)
    ap.add_argument('-o', '--outdir', required=True)
    ap.add_argument('--lint')
    ap.add_argument('--no-reflow', action='store_true')
    ap.add_argument('--compare', metavar='DIR', help='report differences from DIR/NAME.U.CC')
    a = ap.parse_args()
    enc = langs.SETTINGS[a.lang][0]
    try:
        d = tlkeys.load_dict(a.dict, tlkeys.Game(a.game))
    except tlkeys.KeyError_ as e:
        sys.exit('tl_extract: %s: %s' % (a.dict, e))
    os.makedirs(a.outdir, exist_ok=True)
    lint = []
    stats = dict(scripts=0, strings=0, changed=0, compared=0, differ=0)

    story = [p for p in sorted(glob.glob(os.path.join(a.game, '*.CC')))
             if os.path.basename(p).upper() not in ('K.CC', 'L.CC', 'M.CC')]
    for path in story:
        name = os.path.basename(path)[:-3].upper()
        ja = lzss.unpack_file(open(path, 'rb').read())
        toks = [t.val for t in cctok.tokenize(ja) if t.kind == 'str']
        ref = None
        if a.compare:
            ref = [t.val for t in cctok.tokenize(open(os.path.join(a.compare, name + '.U.CC'), 'rb').read())
                   if t.kind == 'str']
        new = []
        for i, v in enumerate(toks):
            where = '%s#%d' % (name, i)
            jas = v.decode('cp932')
            tl = translate(v, d)
            if ref is not None:
                stats['compared'] += 1
                if tl != ref[i].decode('cp932'):
                    stats['differ'] += 1
                    lint.append('%s: differs from %s: %r -> %r' % (where, a.compare, ref[i].decode('cp932'), tl))
            s = process_string(jas, tl, lint, where, not a.no_reflow, a.lang)
            stats['strings'] += 1
            stats['changed'] += s != jas
            new.append(s)
        open(os.path.join(a.outdir, name + '.U.CC'), 'wb').write(rebuild(ja, new, enc))
        stats['scripts'] += 1

    klm = load_klm(a.klm)
    ctl_re = re.compile('(?:\x04.|\x05|\x0C)+$', re.S)
    for n in ('K', 'L', 'M'):
        ja = lzss.unpack_file(open(os.path.join(a.game, n + '.CC'), 'rb').read())
        new = []
        for i, t in enumerate(t for t in cctok.tokenize(ja) if t.kind == 'str'):
            s = t.val.decode('cp932')
            m = ctl_re.search(s)
            tail = m.group(0) if m else ''
            key = s[:len(s) - len(tail)]
            if key in klm:
                new.append(klm[key] + tail)
            else:
                new.append(s)
                if len(s) > 1 and has_kana_kanji(s):      # not the one-letter disk labels
                    lint.append('%s#%d: no translation for %r' % (n, i, s))
            stats['strings'] += 1
        open(os.path.join(a.outdir, n + '.U.CC'), 'wb').write(rebuild(ja, new, enc))
        stats['scripts'] += 1

    if a.lint:
        with open(a.lint, 'w', encoding='utf-8') as fo:
            fo.write('\n'.join(lint) + '\n')
    msg = 'tl_extract: %(scripts)d scripts, %(strings)d strings, %(changed)d translated' % stats
    if a.compare:
        msg += ', %(differ)d of %(compared)d differ from the reference' % stats
    print(msg, '; %d lint notes' % len(lint))


if __name__ == '__main__':
    main()
