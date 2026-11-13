#!/usr/bin/env python3
"""pmdinfo.py - dump the structure of PMD (Professional Music Driver, M.Kajihara) compiled
music files (.M, as produced by MC.EXE 4.x) and FM sound-effect files (.EFC).

Usage:
    python3 -I re-tools/pmdinfo.py FILE.M [FILE.M ...]        human-readable dump
    python3 -I re-tools/pmdinfo.py --tsv FILE.M [...]          one TSV row per file (inventory)
    python3 -I re-tools/pmdinfo.py --efc XEN_SE.EFC [--split OUTDIR]
                                                            EFC effect table (TSV) and optional
                                                            split of every effect into its own blob

The format knowledge is taken from the PMD.COM 4.7g binary shipped with Xenon (see
docs/re/sound.md):  .M = 1 header byte + "data" area; all offsets inside the data area are
relative to data start (= file offset 1).  Data area starts with 11 part offsets
(FM A-C, FM3-extended D-F, SSG G-I, ADPCM J, rhythm K), the R-pattern table offset (+0x16)
and, unless the first part offset is 0x18, the voice-table offset (+0x18).  The word at voice-4 is the memo-table
offset, the bytes at voice-2/-1 are the MC version (e.g. 0x47) and 0xFE.

Command operand lengths come from the PMD 4.7g command dispatcher (PMD.COM 0A77h, tables at
0A8Eh FM / 0B24h SSG / 0BBAh rhythm; re-checked handler by handler for
docs/re/pmd-sequencer.md: all 75 lengths agree, C0 xx with xx in 02h..F6h is an invalid
command = part end).  The parser is validated by checking that every part ends (0x80)
exactly where the next part begins.
"""
import os
import struct
import sys

PART_NAMES = ['A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K']
PART_KIND = ['FM'] * 3 + ['FM3x'] * 3 + ['SSG'] * 3 + ['ADPCM', 'RHY']   # D-F: FM3 extended slots

# operand byte counts for commands 0xFF..0xB5 (PMD 4.7g).  None = special-cased.
OPLEN = {
    0xFF: 1, 0xFE: 1, 0xFD: 1, 0xFC: None, 0xFB: 0, 0xFA: 2, 0xF9: 2, 0xF8: 4, 0xF7: 2,
    0xF6: 0, 0xF5: 1, 0xF4: 0, 0xF3: 0, 0xF2: 4, 0xF1: 1, 0xF0: 4, 0xEF: 2, 0xEE: 1,
    0xED: 1, 0xEC: 1, 0xEB: 1, 0xEA: 1, 0xE9: 1, 0xE8: 1, 0xE7: 1, 0xE6: 1, 0xE5: 2,
    0xE4: 1, 0xE3: 1, 0xE2: 1, 0xE1: 1, 0xE0: 1, 0xDF: 1, 0xDE: 1, 0xDD: 1, 0xDC: 1,
    0xDB: 1, 0xDA: 3, 0xD9: 1, 0xD8: 1, 0xD7: 1, 0xD6: 2, 0xD5: 2, 0xD4: 1, 0xD3: 1,
    0xD2: 1, 0xD1: 1, 0xD0: 1, 0xCF: 1, 0xCE: 6, 0xCD: 5, 0xCC: 1, 0xCB: 1, 0xCA: 1,
    0xC9: 1, 0xC8: 3, 0xC7: 3, 0xC6: 6, 0xC5: 1, 0xC4: 1, 0xC3: 2, 0xC2: 1, 0xC1: 0,
    0xC0: None, 0xBF: 4, 0xBE: 1, 0xBD: 2, 0xBC: 1, 0xBB: 1, 0xBA: 1, 0xB9: 1, 0xB8: 2,
    0xB7: 1, 0xB6: 1, 0xB5: 2,
}
# short names for the command bytes (semantics from the PMD.COM 4.7g handlers, see
# docs/re/pmd-sequencer.md section 4; the MML spellings are the conventional PMD ones and are
# partly inferred).  'skip' = accepted but ignored by this (OPN) build of PMD.
CMDNAME = {0xFF: '@', 0xFE: 'q', 0xFD: 'V', 0xFC: 't/T', 0xFB: '&', 0xFA: 'D', 0xF9: '[',
           0xF8: ']', 0xF7: ':', 0xF6: 'L', 0xF5: '_', 0xF4: ')', 0xF3: '(', 0xF2: 'M',
           0xF1: '*', 0xF0: 'E', 0xEF: 'y', 0xEE: 'w', 0xED: 'P', 0xEC: 'p(skip)',
           0xEB: 'skip', 0xEA: 'skip', 0xE9: 'skip', 0xE8: 'skip', 0xE7: '__', 0xE6: 'skip',
           0xE5: 'skip', 0xE4: 'skip', 0xE3: ')n', 0xE2: '(n', 0xE1: 'skip', 0xE0: 'skip',
           0xDF: 'Z', 0xDE: ')^', 0xDD: '(^', 0xDC: '~', 0xDB: '~+', 0xDA: '{}', 0xD9: 'skip',
           0xD8: 'skip', 0xD7: 'skip', 0xD6: 'MD', 0xD5: 'DD', 0xD4: 'n', 0xD3: 'N', 0xD2: 'F',
           0xD1: 'skip', 0xD0: 'w+-', 0xCF: 's', 0xCE: 'skip', 0xCD: 'E6', 0xCC: 'DX',
           0xCB: 'MW', 0xCA: 'MX', 0xC9: 'EX', 0xC8: 'sd', 0xC7: 'sdd', 0xC6: 'FM3ex',
           0xC5: 'MM', 0xC4: 'Q', 0xC3: 'skip', 0xC2: 'MA', 0xC1: '&&', 0xC0: 'm/DF',
           0xBF: 'M_B', 0xBE: '*_B', 0xBD: 'MD_B', 0xBC: 'MW_B', 0xBB: 'MX_B', 0xBA: 'MM_B',
           0xB9: 'MA_B', 0xB8: 'O', 0xB7: 'MDC', 0xB6: 'FB', 0xB5: 'sk'}

OPN_CLOCK = 3993600.0          # PC-9801-26K / 86 OPN(A) master clock (Hz)


def u16(b, o):
    return struct.unpack_from('<H', b, o)[0]


def cmd_len(b, p):
    """total length (opcode + operands) of command at b[p] (b[p] >= 0xB5)."""
    c = b[p]
    if c == 0xFC:
        return 3 if b[p + 1] >= 0xFB else 2
    if c == 0xC0:
        return 2 if b[p + 1] < 2 else 3
    n = OPLEN.get(c)
    if n is None:
        raise ValueError('unknown command %02X at %04X' % (c, p))
    return 1 + n


def walk_part(b, start, rhythm=False):
    """Linear scan of one part. Returns dict with end offset and found features."""
    p = start
    info = {'L': None, 'cmds': set(), 'tempo': [], 'voices': [], 'status': [], 'fmefc': [],
            'patterns': [], 'notes': 0, 'inf_loop': False}
    while True:
        c = b[p]
        if c == 0x80:
            info['end'] = p
            return info
        if c < 0x80:
            if rhythm:
                info['patterns'].append(c)
                p += 1
            else:
                info['notes'] += 1
                p += 2
            continue
        if c < 0xB5:
            # PMD treats 0x81..0xB4 as part end (writes 0x80 back): stop
            info['end'] = p
            info['bad'] = c
            return info
        info['cmds'].add(c)
        if c == 0xF6:
            info['L'] = p
        elif c == 0xFC:
            info['tempo'].append(('T', b[p + 1]) if b[p + 1] < 0xFB else
                                 ({0xFF: 't', 0xFE: 'T+-', 0xFD: 't+-'}.get(b[p + 1], 'T?'), b[p + 2]))
        elif c == 0xFF:
            info['voices'].append(b[p + 1])
        elif c == 0xDC:
            info['status'].append('=%d' % b[p + 1])
        elif c == 0xDB:
            info['status'].append('+%d' % b[p + 1])
        elif c == 0xD3:
            info['fmefc'].append(b[p + 1])
        elif c == 0xF8 and b[p + 1] == 0:
            info['inf_loop'] = True
        p += cmd_len(b, p)


def timing(b, start, rhythm=False, rtab=None, maxsteps=200000, marks=None):
    """Interpret a part (expanding [ ] loops) and return (ticks_to_end, ticks_loop, loops).
    If marks is a list, (tick, '~'/'~+', value) is appended for every status command met."""
    p = start
    ticks = 0
    counters = {}
    loop_at = None
    ticks_at_L = None
    steps = 0
    while steps < maxsteps:
        steps += 1
        c = b[p]
        if c == 0x80:
            if ticks_at_L is not None:
                return ticks, ticks - ticks_at_L, 'L'
            return ticks, 0, None
        if c < 0x80:
            if rhythm:
                if rtab is not None:
                    ticks += rpattern_ticks(b, rtab, c)
                p += 1
            else:
                ticks += b[p + 1]
                p += 2
            continue
        if c < 0xB5:
            return ticks, 0, None
        if c == 0xF6:
            ticks_at_L = ticks
        elif c == 0xF9:
            counters[u16(b, p + 1) + 1] = 0
        elif c == 0xF8:
            cnt = b[p + 1]
            back = u16(b, p + 3) + 2
            if cnt == 0:
                # infinite loop: body length = ticks since loop start (approx: report)
                if loop_at is None:
                    loop_at = (back, ticks)
                    p = back
                    continue
                return ticks, ticks - loop_at[1], '[ ]0'
            k = p + 2
            counters[k] = counters.get(k, 0) + 1
            if counters[k] < cnt:
                p = back
                continue
            counters[k] = 0
        elif c == 0xF7:
            f8 = u16(b, p + 1)          # -> count byte of matching F8
            cnt = b[f8]
            if counters.get(f8 + 1, 0) == cnt - 1:
                counters[f8 + 1] = 0
                p = f8 + 4
                continue
        elif c == 0xDA:
            ticks += b[p + 3]
        elif c in (0xDB, 0xDC) and marks is not None and len(marks) < 16:
            marks.append((ticks, '~' if c == 0xDC else '~+', b[p + 1]))
        p += cmd_len(b, p)
    return ticks, 0, '?'


def rpattern_ticks(b, rtab, n):
    p = u16(b, rtab + 2 * n)
    t = 0
    for _ in range(4096):
        c = b[p]
        if c == 0xFF:
            return t
        if c < 0x80:
            t += b[p + 1]
            p += 2
        elif c < 0xC0:
            t += b[p + 2]
            p += 3
        else:
            p += cmd_len(b, p)
    return t


def cstr(b, o):
    e = b.index(0, o)
    return b[o:e].decode('cp932', 'replace')


def parse_m(path):
    raw = open(path, 'rb').read()
    b = raw[1:]
    r = {'file': os.path.basename(path), 'size': len(raw), 'type': raw[0]}
    offs = [u16(b, 2 * i) for i in range(11)]
    r['offs'] = offs
    r['rtab'] = u16(b, 0x16)
    # PMD 0394h: part-A offset 18h means "no voice table" (voices from the /V buffer)
    r['voice'] = u16(b, 0x18) if offs[0] != 0x18 else None
    parts = []
    for i, o in enumerate(offs):
        rhy = (i == 10)
        w = walk_part(b, o, rhy)
        used = b[o] != 0x80 and (w['notes'] > 0 or len(w['patterns']) > 0)
        mk = []
        t = timing(b, o, rhy, r['rtab'] if rhy else None, marks=mk) if used else (0, 0, None)
        parts.append({'marks': mk, 'name': PART_NAMES[i], 'kind': PART_KIND[i], 'off': o, 'used': used,
                      'len': w['end'] + 1 - o, 'walk': w, 'ticks': t})
    r['parts'] = parts
    # contiguity check
    seq = sorted(range(11), key=lambda i: offs[i])
    ok = True
    for a, c in zip(seq, seq[1:]):
        if parts[a]['walk']['end'] + 1 != offs[c]:
            ok = False
    r['contiguous'] = ok
    # R patterns: count = (first pattern offset - rtab)/2
    rt = r['rtab']
    if offs[0] != 0x18:
        r['npat'] = (u16(b, 0x18) - 4 - rt) // 2
    else:
        r['npat'] = None
    # voices / memo
    r['voices'] = []
    r['memo'] = []
    if r['voice'] is not None:
        vp = r['voice']
        r['mc_ver'] = b[vp - 2]
        r['mc_mark'] = b[vp - 1]
        mt = u16(b, vp - 4)
        p = vp
        while p + 26 <= len(b) and not (b[p] == 0 and b[p + 1] == 0xFF) and p < mt:
            r['voices'].append(b[p])
            p += 26
        r['voice_end'] = p
        q = mt
        while q + 2 <= len(b):
            so = u16(b, q)
            if so == 0:
                break
            r['memo'].append(cstr(b, so))
            q += 2
    return r


def tick_seconds(tb):
    return 1152.0 * (256 - tb) / OPN_CLOCK


def song_summary(r):
    tempo = None
    for pt in r['parts']:
        if pt['walk']['tempo']:
            tempo = pt['walk']['tempo'][0]
            break
    longest = max(r['parts'][:10], key=lambda pt: pt['ticks'][0])
    loopkind = sorted(set(pt['ticks'][2] for pt in r['parts'] if pt['used'] and pt['ticks'][2]))
    return tempo, longest, loopkind


def memo_fields(r):
    m = r['memo']
    # MC >= 0x42: [0]=PPZ file, [1]=PCM file, [2]=title, [3]=composer, [4]=arranger, [5..]=memo
    if r.get('mc_ver', 0) >= 0x42:
        m = m[1:]
    get = lambda i: m[i] if i < len(m) else ''
    return get(0), get(1), get(2), get(3), m[4:]


def print_m(r):
    print('== %s  size=%d  type byte=%02X  MC ver=%s  contiguous-parts=%s' % (
        r['file'], r['size'], r['type'],
        ('%02X/%02X' % (r['mc_ver'], r['mc_mark'])) if 'mc_ver' in r else '-', r['contiguous']))
    for pt in r['parts']:
        w = pt['walk']
        t = pt['ticks']
        extra = []
        if w['tempo']:
            extra.append('tempo=%s' % w['tempo'])
        if w['voices']:
            extra.append('@=%s' % sorted(set(w['voices'])))
        if w['status']:
            extra.append('~=%s' % w['status'])
        if w['fmefc']:
            extra.append('N=%s' % w['fmefc'])
        if w['patterns']:
            extra.append('patterns=%d' % len(w['patterns']))
        print('  %s %-5s off=%04X len=%5d used=%d L=%s ticks=%d loop=%d (%s) %s' % (
            pt['name'], pt['kind'], pt['off'], pt['len'], pt['used'],
            ('%04X' % w['L']) if w['L'] is not None else '-', t[0], t[1], t[2], ' '.join(extra)))
    print('  R table @%04X (%s patterns)  voices @%s: %s' % (
        r['rtab'], r.get('npat'), ('%04X' % r['voice']) if r['voice'] is not None else '-',
        ' '.join('%d' % v for v in r['voices'])))
    for i, m in enumerate(r['memo']):
        print('  memo[%d] = %r' % (i, m))


TSV_HDR = ['file', 'size', 'mc_ver', 'parts_used', 'fm', 'ssg', 'adpcm', 'rhythm', 'tempo',
           'ticks_total', 'ticks_loop', 'loop', 'approx_sec_intro', 'approx_sec_loop',
           'n_voices', 'status_marks', 'title', 'composer', 'arranger', 'memo']


def tsv_row(r):
    tempo, longest, loopkind = song_summary(r)
    used = ''.join(pt['name'] for pt in r['parts'] if pt['used'])
    fm = ''.join(pt['name'] for pt in r['parts'][:6] if pt['used'])
    ssg = ''.join(pt['name'] for pt in r['parts'][6:9] if pt['used'])
    adpcm = 'J' if r['parts'][9]['used'] else ''
    rhy = 'K' if r['parts'][10]['used'] else ''
    tt, tl, _ = longest['ticks']
    sec_i = sec_l = ''
    if tempo and tempo[0] == 'T':
        s = tick_seconds(tempo[1])
        sec_i = '%.1f' % ((tt - tl) * s)
        sec_l = '%.1f' % (tl * s)
    status = ['%s%d@tick%d' % (m[1], m[2], m[0]) for pt in r['parts'] for m in pt['marks']]
    ppc, title, comp, arr, memo = memo_fields(r)
    return [r['file'], str(r['size']), '%02X' % r.get('mc_ver', 0), used, fm, ssg, adpcm, rhy,
            ('%s%d' % tempo) if tempo else '', str(tt), str(tl), '/'.join(loopkind) or 'none',
            sec_i, sec_l, str(len(r['voices'])), ','.join(map(str, status)),
            title, comp, arr, ' | '.join(memo)]


def parse_efc(path):
    b = open(path, 'rb').read()
    tab = [u16(b, 2 * i) for i in range(127)]
    vptr = u16(b, 0xFE)
    effects = []
    for i, o in enumerate(tab):
        w = walk_part(b, o)
        end = w['end'] + 1
        t = timing(b, o)
        effects.append({'n': i, 'off': o, 'len': end - o, 'empty': b[o] == 0x80, 'walk': w,
                        'ticks': t, 'data': b[o:end]})
    voices = []
    p = vptr
    while p + 26 <= len(b):
        voices.append((b[p], b[p:p + 26]))
        p += 26
    return b, tab, vptr, effects, voices, p


def main(argv):
    if not argv:
        print(__doc__)
        return 1
    if argv[0] == '--efc':
        path = argv[1]
        outdir = argv[3] if len(argv) > 3 and argv[2] == '--split' else None
        b, tab, vptr, effects, voices, vend = parse_efc(path)
        print('#%s size=%d voice_table=%04X voices=%d (%s) tail=%s' % (
            os.path.basename(path), len(b), vptr, len(voices),
            ' '.join(str(v[0]) for v in voices), b[vend:].hex()))
        print('\t'.join(['num', 'offset', 'length', 'empty', 'ticks', 'tempo', 'voices',
                         'volume', 'commands']))
        for e in effects:
            w = e['walk']
            vol = [e['data'][k + 1] for k in range(len(e['data']) - 1) if e['data'][k] == 0xFD][:1]
            print('\t'.join(map(str, [e['n'], '%04X' % e['off'], e['len'], int(e['empty']),
                                      e['ticks'][0], ','.join('%s%d' % t for t in w['tempo']),
                                      ','.join(map(str, sorted(set(w['voices'])))),
                                      ','.join(map(str, vol)),
                                      ' '.join(CMDNAME.get(c, '%02X' % c) for c in sorted(w['cmds'], reverse=True))])))
            if outdir and not e['empty']:
                os.makedirs(outdir, exist_ok=True)
                with open(os.path.join(outdir, 'se_%02d.bin' % e['n']), 'wb') as f:
                    f.write(e['data'])
        if outdir:
            with open(os.path.join(outdir, 'voices.bin'), 'wb') as f:
                f.write(b[vptr:])
        return 0
    tsv = False
    if argv[0] == '--tsv':
        tsv = True
        argv = argv[1:]
        print('\t'.join(TSV_HDR))
    for path in argv:
        r = parse_m(path)
        if tsv:
            print('\t'.join(tsv_row(r)))
        else:
            print_m(r)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
