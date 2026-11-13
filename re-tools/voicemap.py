#!/usr/bin/env python3
"""Voice cue table: which Windows-release voice clip belongs to which PC-98 message.

Usage:
  python3 -I re-tools/voicemap.py --dat XENON.DAT [--scripts DIR] [--overrides FILE]
                               [-o resources/voice/voicemap.tsv] [--report FILE]
  python3 -I re-tools/voicemap.py --show [SCRIPT...] [--scripts DIR] [-o FILE]

  --dat        the Windows release's XENON.DAT (arc3, source/tools/arc3.py)
  --scripts    decompressed PC-98 bytecode, default resources/scripts/decompressed
  --overrides  hand corrections, default resources/voice/overrides.tsv (if present)
  -o           the cue table (default resources/voice/voicemap.tsv)
  --report     write the review report there instead of stderr (resources/voice/report.txt is
               ignored by git: the report quotes the lines)
  --show       print the cues of the table (-o) with the text of their lines, read from the
               scripts (all, or the SCRIPTs named), and check every crc

The Windows scripts (.sco, docs/re/other-versions.md §2.3) carry the PC-98 text and name the
clip of every voiced line. A text record is a run of 16-bit words: cp932 characters and ASCII
controls written as <char, 00> ('s' = key wait, the PC-98 {KEY} 0x0C; 'k' keep window; 'w' timed
wait; 'n' newline), ended by 0000, then zero or more clip names (NUL-terminated, padded to even
length). A record with N key-wait segments has N clips (suffixes A, B, C) or one.

Per script, the PC-98 strings (messages and menu options, in bytecode order, ccdis) are aligned
with the Windows text records on normalised text (controls, spaces and punctuation removed):
exact runs first (difflib), then a similarity alignment inside the gaps (ratio >= 0.6).
A matched message receives the clips of its record, clip k on the segment after the k-th
{KEY} (part k). Overrides are applied last.

Output (TSV, sorted by script and offset; offsets are those of the FD string token in the
Japanese bytecode and carry over to every translation, whose token structure is identical):
  script  offset  page  part  clip  match  crc
match: exact, fuzzy0.xx (similarity), override. crc: CRC-32 of the line (the string token's
Shift-JIS bytes, hex), which lets a tool confirm that the offset still names the same line
without the table carrying the game's text (tests/check_pack.py; --show prints the text). Clips on a message without visible text are
kept there on purpose: the S00 prologue and the dream openings draw each line with the graphics
off ({W0}{KEEP} after GRAPH_OFF), fade it in, and only then print the empty "\n　{KEEP}" message
that carries the clip, so the voice starts when the line has become visible. The report lists clips that found no message,
fuzzy matches below 0.8, part/clip count disagreements, and speaker-tag disagreements.
"""
import argparse
import collections
import csv
import difflib
import os
import re
import sys
import unicodedata
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', 'source', 'tools'))   # arc3, the build's
import arc3   # noqa: E402
import ccdis  # noqa: E402

ROOT = os.path.normpath(os.path.join(HERE, '..'))
CLIP = re.compile(rb'[A-Z]{3}[0-9]{4}[A-Z0-9]?\x00')

# speaker tag -> clip prefix (named characters only; generic tags such as 女 are not checked)
SPEAKER = {'涼子': 'RYO', '宏治': 'KOH', 'ランファ': 'RAN', '舞': 'MAI', '恵': 'MEG', '沙夜香': 'SAY',
           'ファル': 'FAL', 'トレイシー': 'TRA', 'ビアンカ': 'BIA', 'コウジ': 'KOU'}


def is_lead(c):
    return 0x81 <= c <= 0x9F or 0xE0 <= c <= 0xEF


def norm(s):
    s = unicodedata.normalize('NFKC', s)
    s = re.sub(r'\{[^}]*\}|\\n', '', s)
    return re.sub(r'[\s・‥…、。,.!?！？ー\-~〜「」『』（）()]', '', s)


# ---- Windows ------------------------------------------------------------------------------

def win_records(sco):
    """[(text, parts, [clips])] in file order; text without controls."""
    b = bytes(x ^ 0xFF for x in sco)
    out = []
    i, n = 0, len(b) & ~1
    while i < n:
        if not is_lead(b[i]):
            i += 2
            continue
        j = i
        chars = []
        parts = 1
        while j < n and b[j:j + 2] != b'\0\0':
            w = b[j:j + 2]
            if is_lead(w[0]):
                try:
                    chars.append(w.decode('cp932'))
                except UnicodeDecodeError:
                    pass
            elif w[0] == ord('s'):
                parts += 1
            j += 2
        j += 2
        clips = []
        while True:
            m = CLIP.match(b, j)
            if not m:
                break
            clips.append(m.group()[:-1].decode())
            j = m.end() + (m.end() & 1)
        out.append((''.join(chars), parts, clips))
        i = j
    return out


# ---- PC-98 --------------------------------------------------------------------------------

def line_crc(raw):
    """The crc column: CRC-32 of a string token's text bytes (after FD and its length byte)."""
    return '%08x' % zlib.crc32(raw)


def pc_strings(data):
    """[(offset, page, kind, text, parts, crc)] for messages and menu options."""
    ast = ccdis.parse(data)[0]
    strings = ccdis.collect('', data, ast)[0]
    out = []
    for pos, page, kind, text in strings:
        if kind not in ('msg', 'option'):
            continue
        end = data.index(b'\0', pos + 2)
        out.append((pos, page, kind, text, data[pos + 2:end].count(b'\x0c') + 1,
                    line_crc(data[pos + 2:end])))
    return out


# ---- alignment ----------------------------------------------------------------------------

def align(pa, wa):
    """pa, wa: normalised strings. Returns {i: (j, score)} monotonic."""
    pairs = {}
    sm = difflib.SequenceMatcher(None, pa, wa, autojunk=False)
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == 'equal':
            for k in range(i2 - i1):
                pairs[i1 + k] = (j1 + k, 1.0)
        elif tag == 'replace':
            # small Needleman-Wunsch on similarity inside the gap
            ni, nj = i2 - i1, j2 - j1
            sim = [[difflib.SequenceMatcher(None, pa[i1 + a], wa[j1 + b]).ratio()
                    for b in range(nj)] for a in range(ni)]
            best = [[0.0] * (nj + 1) for _ in range(ni + 1)]
            for a in range(ni - 1, -1, -1):
                for b in range(nj - 1, -1, -1):
                    take = sim[a][b] + best[a + 1][b + 1] if sim[a][b] >= 0.6 else -1
                    best[a][b] = max(take, best[a + 1][b], best[a][b + 1])
            a = b = 0
            while a < ni and b < nj:
                if sim[a][b] >= 0.6 and best[a][b] == sim[a][b] + best[a + 1][b + 1]:
                    pairs[i1 + a] = (j1 + b, sim[a][b])
                    a += 1
                    b += 1
                elif best[a][b] == best[a + 1][b]:
                    a += 1
                else:
                    b += 1
    return pairs


def build(dat, scripts, overrides):
    arc = arc3.Arc3(dat)
    rows = []
    report = collections.defaultdict(list)
    names = sorted(f[:-4] for f in os.listdir(scripts) if f.endswith('.bin'))
    for name in names:
        key = ('sco' + name.lower()).encode()
        if key not in arc.entries:
            report['no Windows script'].append(name)
            continue
        data = open(os.path.join(scripts, name + '.bin'), 'rb').read()
        ps = pc_strings(data)
        ws = win_records(arc.read(key)[0])
        pairs = align([norm(s[3]) for s in ps], [norm(r[0]) for r in ws])
        used = set()
        for i, (off, page, kind, text, nparts, crc) in enumerate(ps):
            if i not in pairs:
                continue
            j, score = pairs[i]
            used.add(j)
            wtext, wparts, clips = ws[j]
            if not clips:
                continue
            if kind != 'msg':
                report['clip on a menu option (dropped)'].append('%s %04X %s' % (name, off, clips))
                continue
            match = 'exact' if score == 1.0 else 'fuzzy%.2f' % score
            if score < 0.8:
                report['fuzzy match below 0.8'].append('%s %04X %s %s | %s' % (
                    name, off, ','.join(clips), text[:40], wtext[:40]))
            if len(clips) > 1 and (len(clips) != nparts or wparts != nparts):
                report['clip/part count differs (assigned in order)'].append(
                    '%s %04X pc98 parts %d, windows parts %d, clips %s' % (name, off, nparts, wparts, clips))
            for k, c in enumerate(clips):
                rows.append([name, '%04X' % off, str(page), str(min(k, nparts - 1)), c, match, crc, text])
        for j, (wtext, wparts, clips) in enumerate(ws):
            if clips and j not in used:
                report['clip with no PC-98 message'].append('%s %s %s' % (name, ','.join(clips), wtext[:50]))

    if overrides and os.path.exists(overrides):
        with open(overrides, encoding='utf-8') as f:
            for r in csv.DictReader((l for l in f if not l.startswith('#')), delimiter='\t'):
                rows = [x for x in rows if not (x[0] == r['script'] and x[1] == r['offset'].upper()
                                                and x[3] == r['part'])]
                if r['clip'] != '-':
                    data = open(os.path.join(scripts, r['script'] + '.bin'), 'rb').read()
                    st = {('%04X' % s[0]): s for s in pc_strings(data)}
                    s = st.get(r['offset'].upper())
                    if not s:
                        report['override for an unknown message'].append(str(r))
                        continue
                    rows.append([r['script'], '%04X' % s[0], str(s[1]), r['part'], r['clip'],
                                 'override', s[5], s[3]])

    for r in rows:
        m = re.match(r'［(.+?)］', r[7])
        want = SPEAKER.get(m.group(1)) if m else None
        if want and want != r[4][:3] and r[5] != 'override':
            report['speaker tag differs from clip'].append('%s %s %s %s' % (r[0], r[1], r[4], r[7][:40]))

    rows.sort(key=lambda r: (r[0], int(r[1], 16), int(r[3])))
    return rows, report


def show(table, scripts, only):
    """Print the cues with their lines' text; returns the number of crc mismatches."""
    bad = 0
    cache = {}
    with open(table, encoding='utf-8') as f:
        for r in csv.DictReader(f, delimiter='\t'):
            if only and r['script'].upper() not in only:
                continue
            if r['script'] not in cache:
                data = open(os.path.join(scripts, r['script'] + '.bin'), 'rb').read()
                cache[r['script']] = {'%04X' % s[0]: s for s in pc_strings(data)}
            s = cache[r['script']].get(r['offset'].upper())
            text = s[3] if s else '(no message at this offset)'
            flag = ''
            if not s or s[5] != r['crc']:
                flag = '  [CRC MISMATCH]'
                bad += 1
            print('%s\t%s\t%s\t%s\t%s%s' % (r['script'], r['offset'], r['part'], r['clip'], text, flag))
    return bad


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--dat')
    ap.add_argument('--show', nargs='*', metavar='SCRIPT')
    ap.add_argument('--scripts', default=os.path.join(ROOT, 'resources/scripts/decompressed'))
    ap.add_argument('--overrides', default=os.path.join(ROOT, 'resources/voice/overrides.tsv'))
    ap.add_argument('-o', '--output', default=os.path.join(ROOT, 'resources/voice/voicemap.tsv'))
    ap.add_argument('--report')
    a = ap.parse_args(argv)
    if a.show is not None:
        bad = show(a.output, a.scripts, {x.upper() for x in a.show})
        if bad:
            print('%d cues whose crc does not match the line at their offset' % bad, file=sys.stderr)
        return 1 if bad else 0
    if not a.dat:
        ap.error('--dat XENON.DAT is required (or --show)')
    rows, report = build(a.dat, a.scripts, a.overrides)
    os.makedirs(os.path.dirname(os.path.abspath(a.output)), exist_ok=True)
    with open(a.output, 'w', encoding='utf-8', newline='') as f:
        f.write('script\toffset\tpage\tpart\tclip\tmatch\tcrc\n')
        for r in rows:
            f.write('\t'.join(r[:7]) + '\n')
    rep = open(a.report, 'w', encoding='utf-8') if a.report else sys.stderr
    stats = collections.Counter(r[5][:5] for r in rows)
    print('%d cues on %d messages (%s), %d distinct clips' % (
        len(rows), len({(r[0], r[1]) for r in rows}),
        ', '.join('%s %d' % kv for kv in sorted(stats.items())), len({r[4] for r in rows})), file=rep)
    for k, v in report.items():
        print('\n## %s: %d' % (k, len(v)), file=rep)
        for x in v:
            print('  ' + x, file=rep)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
