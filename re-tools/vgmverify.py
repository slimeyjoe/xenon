#!/usr/bin/env python3
"""vgmverify.py - check the VGM reference recordings against the real PMD.COM.

For every song / effect the VGM is expanded by its loop (header loop offset + loop
samples) to several passes and compared with a fresh, uncut recording of the real driver
under pmdemu (needs unicorn).  Both streams are reduced to *effective* chip-state changes
with one continuous register shadow, so the VGM's pruning of redundant writes and its
explicit rewrites after the loop point do not count:
  * plain value registers: an event when the value changes
  * F-number: A4h-A6h/ACh-AEh only latch; an event when A0h-A2h/A8h-AAh commits a new
    (high, low) pair
  * 28h key on/off: an event when the slot mask of that channel changes
  * 0Dh (SSG envelope shape) always (a write restarts the envelope)
  * 27h: only bits 6-7 (CH3 mode); timer registers 24h-26h are ignored
The event lists must match in order; times are compared separately.

Result per file:
  OK      identical events, timing within TOL samples (rounding of the loop length to
          whole samples; default 9 = 0.2 ms)
  VARIES  events differ, but the real driver does not repeat itself either (consecutive
          passes of the real recording differ: Timer-A driven LFO / software envelopes,
          whose phase against the Timer-B tempo clock shifts every pass, or a random LFO)
          - no single-pass loop can be exact.  Reported: events of the same register value
          that are only shifted (by up to one Timer-A period) and events without a
          counterpart within that window
  FAIL    anything else (exit status 1)

Usage:
    vgmverify.py [--music DIR] [--sfx DIR] [--passes N] [--tol SMP] [XENON_01.M ... se:4 ...]
    (default: all songs of the game directory and effects 1-50)
"""
import argparse, bisect, collections, os, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
GAMEDIR = os.path.join(ROOT, 'resources', 'original', 'game')
SR = 44100
EDGE = SR // 10          # events in the last 0.1 s before the horizon are not compared
WIN = 900                # matching window for differing streams: ~1 Timer-A period (18.46 ms)


def S(t):
    return int(round(t * SR))


def canon(writes):
    """(sample, reg, val) in order -> effective state-change events (sample, key, val)."""
    sh, latch, fq, key, out = {}, {}, {}, {}, []
    for s, r, v in writes:
        if 0x24 <= r <= 0x26:
            continue
        if r == 0x27:
            v &= 0xC0
            if sh.get(r, 0) != v:
                sh[r] = v; out.append((s, r, v))
        elif r == 0x28:
            ch = v & 7
            if key.get(ch, 0) != (v & 0xF0):
                key[ch] = v & 0xF0; out.append((s, r, v))
        elif r == 0x0D:
            out.append((s, r, v))
        elif 0xA4 <= r <= 0xA6 or 0xAC <= r <= 0xAE:
            latch[r - 4] = v
        elif 0xA0 <= r <= 0xA2 or 0xA8 <= r <= 0xAA:
            f = (latch.get(r, 0), v)
            if fq.get(r) != f:
                fq[r] = f; out.append((s, r, f[0] << 8 | v))
        elif sh.get(r) != v:
            sh[r] = v; out.append((s, r, v))
    return out


def group(r):
    if r == 0x28: return 'keyon'
    if r == 0x27: return 'ch3mode'
    if r <= 0x05: return 'ssg-tone'
    if r == 0x06: return 'noise'
    if r == 0x07: return 'mixer'
    if r <= 0x0A: return 'ssg-vol'
    if r <= 0x0D: return 'ssg-env'
    if 0xA0 <= r <= 0xAA: return 'fm-freq'
    return 'fm-op'


def read_vgm(path):
    """-> (writes [(sample, reg, val)], total samples, index of first loop write or None,
    loop samples)"""
    d = open(path, 'rb').read()
    if d[:4] != b'Vgm ':
        raise ValueError('not a VGM')
    total, lrel, lsamp = struct.unpack_from('<III', d, 0x18)
    loff = 0x1C + lrel if lrel else None
    i, t, ev, lev = 0x34 + struct.unpack_from('<I', d, 0x34)[0], 0, [], None
    while True:
        if i == loff:
            lev = len(ev)
        c = d[i]
        if c == 0x55: ev.append((t, d[i + 1], d[i + 2])); i += 3
        elif c == 0x61: t += struct.unpack_from('<H', d, i + 1)[0]; i += 3
        elif c == 0x62: t += 735; i += 1
        elif c == 0x63: t += 882; i += 1
        elif 0x70 <= c <= 0x7F: t += c - 0x6F; i += 1
        elif c == 0x66: break
        else: raise ValueError('unexpected VGM command %02X at %X' % (c, i))
    if t != total:
        raise ValueError('header total %d != stream %d samples' % (total, t))
    if loff is not None and lev is None:
        lev = len(ev)
    return ev, total, lev, lsamp


def expand(ev, total, lev, lsamp, horizon):
    out = list(ev)
    if lev is None or lsamp == 0:
        return out
    body, k = ev[lev:], 1
    while total + (k - 1) * lsamp < horizon:
        out += [(s + k * lsamp, r, v) for s, r, v in body]
        k += 1
    return out


def compare(vgm, real, horizon, tol):
    a = [e for e in canon(vgm) if e[0] < horizon - EDGE]
    b = [e for e in canon(real) if e[0] < horizon - EDGE]
    ca, cb = [x[1:] for x in a], [x[1:] for x in b]
    if ca == cb:
        dt = max((x[0] - y[0] for x, y in zip(a, b)), key=abs, default=0)
        return abs(dt) <= tol, 'identical %d events over %.1f s, max dt %+d smp (%.2f ms)' % (
            len(a), horizon / SR, dt, dt * 1000 / SR)
    # Match every event to the same (reg, value) event of the other stream within one
    # Timer-A period (sequence alignment is useless here: the streams are so repetitive
    # that it misaligns and reports identical stretches as replaced).
    first = next((k for k in range(min(len(ca), len(cb))) if ca[k] != cb[k]), min(len(ca), len(cb)))
    pool = collections.defaultdict(list)
    for s, r, v in b:
        pool[(r, v)].append(s)
    shifted, missing = collections.Counter(), collections.Counter()
    for s, r, v in a:
        L = pool[(r, v)]
        j = bisect.bisect_left(L, s - WIN)
        if j < len(L) and L[j] <= s + WIN:
            if abs(L[j] - s) > tol:
                shifted[group(r)] += 1
            L.pop(j)
        else:
            missing[group(r)] += 1
    extra = collections.Counter()
    for (r, v), L in pool.items():
        extra[group(r)] += len(L)
    fmt = lambda c: ', '.join('%s %d' % kv for kv in c.most_common(4)) or '-'
    t_first = (a[first][0] if first < len(a) else b[first][0]) / SR
    return False, ('%d vgm / %d real events, first difference at %.2f s; shifted by <= 1 Timer-A '
                   'period: %d (%s); unmatched: %d vgm-only / %d real-only (%s)' % (
                       len(a), len(b), t_first, sum(shifted.values()), fmt(shifted),
                       sum(missing.values()), sum(extra.values()), fmt(missing + extra)))


def real_periodic(e, real_c):
    """do consecutive passes (status2 k -> k+1) of the real recording have the same events?"""
    b = e.s2_btick
    ks = sorted(k for k in b if k != 0xFF)
    tb = [S(e.writes[e.bidx[b[k] - 1]][0]) for k in ks]
    segs = [[x[1:] for x in real_c if t0 <= x[0] < t1] for t0, t1 in zip(tb, tb[1:])]
    return all(p == q for p, q in zip(segs, segs[1:])) if len(segs) > 1 else True


def check_song(fn, a):
    import pmdemu
    name = os.path.splitext(fn)[0]
    ev, total, lev, lsamp = read_vgm(os.path.join(a.music, name + '.vgm'))
    e = pmdemu.PMDEmu()
    e.load_music(open(os.path.join(a.gamedir, fn), 'rb').read())
    e.nstart = len(e.writes)
    e.mstart()
    horizon = total + (a.passes * lsamp if lev is not None else 0)
    e.run(seconds=horizon / SR + 2.0, loops=a.passes + 3)
    real = [(S(t), r, v) for t, _, _, r, v in e.writes]
    horizon = min(horizon, S(e.time))
    ok, msg = compare(expand(ev, total, lev, lsamp, horizon), real, horizon, a.tol)
    if ok:
        return 'OK', msg
    if lev is not None and not real_periodic(e, canon(real)):
        return 'VARIES', msg + ' (real passes differ too)'
    return 'FAIL', msg


def check_se(n, a):
    import pmdemu
    ev, total, lev, lsamp = read_vgm(os.path.join(a.sfx, 'se_%02d.vgm' % n))
    e = pmdemu.PMDEmu()
    e.load_efc(open(os.path.join(a.gamedir, 'XEN_SE.EFC'), 'rb').read())
    e.se_on(n)
    horizon = total + (a.passes * lsamp if lev is not None else 0)
    while S(e.time) < horizon:
        e.step()
    real = [(S(t), r, v) for t, _, _, r, v in e.writes]
    ok, msg = compare(expand(ev, total, lev, lsamp, horizon), real, horizon, a.tol)
    return ('OK' if ok else 'FAIL'), msg


def job(arg):
    j, a = arg
    try:
        st, msg = check_se(int(j[3:]), a) if j.startswith('se:') else check_song(j, a)
    except Exception as ex:
        st, msg = 'FAIL', 'exception %r' % ex
    name = ('se_%02d' % int(j[3:])) if j.startswith('se:') else os.path.splitext(j)[0]
    return st, '%-10s %-6s %s' % (name, st, msg)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('items', nargs='*')
    ap.add_argument('--music', default=os.path.join(ROOT, 'resources', 'music', 'vgm'))
    ap.add_argument('--sfx', default=os.path.join(ROOT, 'resources', 'sfx', 'vgm'))
    ap.add_argument('--gamedir', default=GAMEDIR)
    ap.add_argument('--passes', type=int, default=3, help='loop passes to compare beyond the file')
    ap.add_argument('--tol', type=int, default=9, help='timing tolerance in samples')
    ap.add_argument('-j', '--jobs', type=int, default=max(1, (os.cpu_count() or 2) - 1))
    a = ap.parse_args()
    items = a.items or (sorted(f for f in os.listdir(a.gamedir) if f.upper().endswith('.M')) +
                        ['se:%d' % n for n in range(1, 51)])
    count = collections.Counter()
    from multiprocessing import Pool
    with Pool(a.jobs) as p:
        for st, line in p.imap(job, [(i, a) for i in items]):
            count[st] += 1
            print(line, flush=True)
    print('summary: ' + ', '.join('%s %d' % kv for kv in sorted(count.items())))
    return 1 if count['FAIL'] else 0


if __name__ == '__main__':
    sys.exit(main())
