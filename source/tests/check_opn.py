#!/usr/bin/env python3
"""Compare opn_lite with ymfm on the reference register logs.

usage: check_opn.py TEST_OPN_BIN MUSIC_VGM_DIR SFX_VGM_DIR TMPDIR [--verbose] [--only NAME ...]

Every song (first 20 s) and every effect is rendered through both cores from the same VGM
register stream (writes spaced one chip sample apart, as on hardware) and compared per
46 ms frame:

  level   RMS difference over the active frames (dB)
  env     log-RMS envelope difference per frame, 90th percentile of |diff| (dB); 75th for
          the SSG, whose square-wave phase at note start is arbitrary on the chip and differs
          between the cores (one-frame transients)
  lsd     log-spectral distance per frame over 1/3-octave bands up to 0.45 * rate, mean (dB)
  snr     sample-domain SNR at the best lag within +-2 samples

1. Native rate (55 467 Hz, one chip sample per output sample): the port itself.
   FM part alone (SSG writes dropped): tight limits including the SNR. SSG part alone:
   level/envelope/spectrum only - the noise generator is sampled differently by the two
   cores (ymfm samples the LFSR at 166 kHz, opn_lite integrates it), so noise phases differ.
2. Reduced rates (22 050 Hz, the Amiga setting; 11 025 Hz, the fallback): opn_lite computes
   the FM waveform at the output instants, so harmonics above half the rate fold back
   exactly as they do when ymfm's output is decimated by picking the nearest sample. The
   check compares against that decimated reference (correctness of the rate scaling: pitch,
   envelope times, levels).
   The aliasing itself is reported, not judged: "alias" is the level excess against a
   band-limited (polyphase resampled) ymfm reference, i.e. how much louder the folded
   harmonics make the lite output. lite2 (2x FM oversampling with the half-band decimator)
   is listed the same way.
"""
import functools
import glob
import multiprocessing
import os
import subprocess
import sys

import numpy as np
from scipy.signal import firwin, resample_poly

SONG_SECONDS = 20.0
FRAME = 1024
FLOOR_DB = -60.0        # frames quieter than this (in the reference) are ignored
NATIVE = 55466

# limits: (|level| dB, env p90 dB, lsd dB[, snr dB])
LIM_FM_NATIVE = (0.3, 1.0, 1.0, 45.0)
LIM_SSG_NATIVE = (2.5, 4.0, 3.0)     # noise: sampled (ymfm) vs integrated (lite), ~2 dB; env: note-start phase
LIM_REDUCED = (2.0, 6.0, 6.0)        # folded harmonics and noise move with sub-sample timing
REDUCED = (22050, 11025)


def render(binary, core, rate, vgm, out, seconds, mask='all'):
    r = subprocess.run([binary, core, str(rate), vgm, out, str(seconds), mask],
                       capture_output=True, check=True)
    got_rate, n = r.stdout.decode().split()
    return int(got_rate), np.fromfile(out, dtype='<i2').astype(np.float64)


def db(x):
    return 20.0 * np.log10(np.maximum(x, 1e-9))


def frames(x, n):
    k = len(x) // n
    return x[:k * n].reshape(k, n)


def band_edges(rate, n):
    """1/3-octave band edges as rfft bin indices, 100 Hz .. 0.45 * rate"""
    edges = [100.0]
    while edges[-1] * 2 ** (1 / 3) < 0.45 * rate:
        edges.append(edges[-1] * 2 ** (1 / 3))
    bins = sorted(set(int(round(e * n / rate)) for e in edges))
    return [b for b in bins if b < n // 2 + 1]


def compare(a, b, rate, env_pct=90):
    """a = lite, b = reference at the same rate. Returns dict of metrics."""
    n = min(len(a), len(b))
    a, b = a[:n] / 32768.0, b[:n] / 32768.0
    fa, fb = frames(a, FRAME), frames(b, FRAME)
    ra = db(np.sqrt(np.mean(fa ** 2, axis=1)))
    rb = db(np.sqrt(np.mean(fb ** 2, axis=1)))
    act = rb > FLOOR_DB
    res = {'frames': int(act.sum())}
    if act.sum() < 3:
        res.update(level=0.0, env=0.0, lsd=0.0)
        return res
    res['level'] = float(db(np.sqrt(np.mean(fa[act] ** 2))) - db(np.sqrt(np.mean(fb[act] ** 2))))
    res['env'] = float(np.percentile(np.abs(ra[act] - rb[act]), env_pct))
    win = np.hanning(FRAME)
    sa = np.abs(np.fft.rfft(fa[act] * win, axis=1))
    sb = np.abs(np.fft.rfft(fb[act] * win, axis=1))
    edges = band_edges(rate, FRAME)
    ba = np.stack([np.sqrt(np.mean(sa[:, lo:hi] ** 2, axis=1)) for lo, hi in zip(edges[:-1], edges[1:])], 1)
    bb = np.stack([np.sqrt(np.mean(sb[:, lo:hi] ** 2, axis=1)) for lo, hi in zip(edges[:-1], edges[1:])], 1)
    # ignore bands that are empty in the reference (below -70 dB re the frame peak)
    lb = db(bb)
    mask = lb > (lb.max(axis=1, keepdims=True) - 70.0)
    d = np.abs(db(ba) - lb)
    res['lsd'] = float(np.sqrt(np.mean((d * mask) ** 2, axis=1) / np.maximum(mask.sum(axis=1), 1)).mean())
    return res


def snr_best_lag(a, b, maxlag=2):
    n = min(len(a), len(b)) - 2 * maxlag
    best = -1e9
    for lag in range(-maxlag, maxlag + 1):
        x = a[maxlag + lag:maxlag + lag + n]
        y = b[maxlag:maxlag + n]
        e = np.sum((x - y) ** 2)
        s = np.sum(y ** 2)
        v = 10 * np.log10(s / max(e, 1e-9)) if s > 0 else 99.0
        best = max(best, v)
    return best


def has_signal(x):
    return np.sqrt(np.mean(x ** 2)) >= 32768 * 10 ** (FLOOR_DB / 20)


def decimate_nearest(ref, rate):
    idx = np.rint(np.arange(0, len(ref) * rate / NATIVE) * (NATIVE / rate)).astype(np.int64)
    idx = idx[idx < len(ref)]
    return ref[idx]


@functools.lru_cache(maxsize=None)
def band_taps(up, down):
    """the low-pass filter resample_poly designs by default for up/down (scipy 1.16), made once:
    at these ratios it has 0.5 to 1.1 million taps and its Kaiser window took most of the
    check's time when it was designed again on every call. Read-only: resample_poly copies it."""
    m = max(up, down)
    h = firwin(2 * 10 * m + 1, 1.0 / m, window=('kaiser', 5.0))
    h.flags.writeable = False
    return h


def resample_band(ref, rate):
    g = np.gcd(rate, NATIVE)
    # the taps are float64: equal to the default design only for float64 input
    assert ref.dtype == np.float64
    return resample_poly(ref, rate // g, NATIVE // g, window=band_taps(rate // g, NATIVE // g))


def over(m, lim, with_snr=False):
    bad = []
    if abs(m['level']) > lim[0]: bad.append('level')
    if m['env'] > lim[1]: bad.append('env')
    if m['lsd'] > lim[2]: bad.append('lsd')
    if with_snr and m.get('snr') is not None and m['snr'] < lim[3]: bad.append('snr')
    return bad


def fmt(m, snr=False):
    s = 'lvl %+5.2f env %4.2f lsd %4.2f' % (m['level'], m['env'], m['lsd'])
    if snr and m.get('snr') is not None:
        s += ' snr %4.1f' % m['snr']
    return s


def run_case(job):
    """One case: every render and comparison. Its files are <tmp>/<case>-*.raw. Returns the
    status parts, the failed limits, the worst-case notes and the alias levels, for main() to
    take in case order."""
    binary, name, vgm, seconds, base = job
    T = lambda part: base + '-' + part + '.raw'
    parts, bad, notes, aliases = [], [], [], []
    _, ref = render(binary, 'ymfm', NATIVE, vgm, T('ref'), seconds)
    _, ref_fm = render(binary, 'ymfm', NATIVE, vgm, T('ref_fm'), seconds, 'fm')
    _, ref_ssg = render(binary, 'ymfm', NATIVE, vgm, T('ref_ssg'), seconds, 'ssg')

    # 1. native: FM and SSG separately
    _, lite_fm = render(binary, 'lite', NATIVE, vgm, T('lite'), seconds, 'fm')
    if has_signal(ref_fm):
        m = compare(lite_fm, ref_fm, NATIVE)
        m['snr'] = snr_best_lag(lite_fm, ref_fm)
        notes.append(('native fm', m, True))
        parts.append('fm ' + fmt(m, True))
        bad += ['native fm ' + b for b in over(m, LIM_FM_NATIVE, True)]
    _, lite_ssg = render(binary, 'lite', NATIVE, vgm, T('lite'), seconds, 'ssg')
    if has_signal(ref_ssg):
        m = compare(lite_ssg, ref_ssg, NATIVE, env_pct=75)
        notes.append(('native ssg', m, False))
        parts.append('ssg ' + fmt(m))
        bad += ['native ssg ' + b for b in over(m, LIM_SSG_NATIVE)]

    # 2. reduced rates against the nearest-sample decimated reference; alias excess
    #    against the band-limited one
    for rate in REDUCED:
        got, lite = render(binary, 'lite', rate, vgm, T('lite'), seconds)
        m = compare(lite, decimate_nearest(ref, got), got)
        notes.append(('%d' % got, m, False))
        ma = compare(lite, resample_band(ref, got), got)
        aliases.append((got, ma['level']))
        parts.append('%d %s alias %+4.1f' % (got, fmt(m), ma['level']))
        bad += ['%d %s' % (got, b) for b in over(m, LIM_REDUCED)]
    got, lite2 = render(binary, 'lite2', 22050, vgm, T('lite'), seconds)
    ma = compare(lite2, resample_band(ref, got), got)
    aliases.append(('lite2 22050', ma['level']))
    parts.append('lite2 alias %+4.1f' % ma['level'])
    return name, parts, bad, notes, aliases


def main():
    args = sys.argv[1:]
    verbose = '--verbose' in args
    only = []
    if '--only' in args:
        only = args[args.index('--only') + 1:]
        args = args[:args.index('--only')]
    args = [a for a in args if a != '--verbose']
    binary, music_dir, sfx_dir, tmp = args[:4]
    os.makedirs(tmp, exist_ok=True)

    cases = []
    for v in sorted(glob.glob(os.path.join(music_dir, '*.vgm'))):
        cases.append((os.path.splitext(os.path.basename(v))[0], v, SONG_SECONDS))
    for v in sorted(glob.glob(os.path.join(sfx_dir, 'se_*.vgm'))):
        cases.append((os.path.splitext(os.path.basename(v))[0], v, 1e9))
    if only:
        cases = [c for c in cases if c[0] in only]

    fails = 0
    worst = {}
    alias = {}

    def note(key, m, snr=False):
        w = worst.setdefault(key, [0.0, 0.0, 0.0, 999.0])
        w[0] = max(w[0], abs(m['level'])); w[1] = max(w[1], m['env']); w[2] = max(w[2], m['lsd'])
        if snr and m.get('snr') is not None:
            w[3] = min(w[3], m['snr'])

    # the cases run in parallel, each in files of its own; the results are taken in case order,
    # so the output does not depend on which finishes first
    jobs = [(binary, name, vgm, seconds, os.path.join(tmp, name)) for name, vgm, seconds in cases]
    with multiprocessing.Pool(min(len(jobs), os.process_cpu_count() or 1) or 1) as pool:
        for name, parts, bad, notes, aliases in pool.imap(run_case, jobs):
            for key, m, snr in notes:
                note(key, m, snr)
            for key, level in aliases:
                alias.setdefault(key, []).append((level, name))
            status = 'FAIL' if bad else 'ok  '
            if bad:
                fails += 1
            if bad or verbose:
                print('%s %-9s %s%s' % (status, name, ' | '.join(parts), (' <- ' + ', '.join(bad)) if bad else ''))

    for key, w in worst.items():
        print('worst %-10s |level| %.2f dB, env p90 %.2f dB, lsd %.2f dB%s' %
              (key, w[0], w[1], w[2], (', snr %.1f dB' % w[3]) if w[3] < 999 else ''))
    for key, lst in alias.items():
        lv = np.array([x[0] for x in lst])
        top = sorted(lst, reverse=True)[:3]
        print('alias excess %-12s median %+.1f dB, worst %s' %
              (str(key), np.median(lv), ', '.join('%s %+.1f' % (n, l) for l, n in top)))
    print('check_opn: %d cases, %d failures' % (len(cases), fails))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
