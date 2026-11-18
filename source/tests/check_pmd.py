#!/usr/bin/env python3
"""Compare lrvne's PMD module (as this engine sets it up) with the real-driver VGM recordings.

usage: check_pmd.py PMD_TRACE_BIN GAMEDIR MUSIC_VGM_DIR SFX_VGM_DIR

For every song the recorded part of the VGM (intro + pass 1 + pass 2, before any loop
expansion) is the real PMD.COM's register stream; the C sequencer must produce the same
effective chip-state changes (vgmverify.canon) in the same order, each within TOL samples.
Effects 1-50 likewise (their VGMs include the built-in SSG effects).
"""
import glob
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 're-tools'))
import vgmverify   # noqa: E402

TOL = 9
EDGE = vgmverify.EDGE


def run_trace(binary, args):
    r = subprocess.run([binary] + args, capture_output=True, check=True)
    out = []
    for line in r.stdout.decode().splitlines():
        s, reg, val = line.split()
        out.append((int(s), int(reg, 16), int(val, 16)))
    return out


def compare(label, ref, got, horizon):
    a = [e for e in vgmverify.canon(ref) if e[0] < horizon - EDGE]
    b = [e for e in vgmverify.canon(got) if e[0] < horizon - EDGE]
    ca, cb = [x[1:] for x in a], [x[1:] for x in b]
    if ca != cb:
        k = next((i for i in range(min(len(ca), len(cb))) if ca[i] != cb[i]), min(len(ca), len(cb)))
        ctx_a = ' '.join('%02X=%X' % e for e in ca[max(0, k - 4):k + 6])
        ctx_b = ' '.join('%02X=%X' % e for e in cb[max(0, k - 4):k + 6])
        t = (a[k][0] if k < len(a) else b[k][0] if k < len(b) else 0) / 44100.0
        print('FAIL %s: %d ref / %d got events, first difference #%d at %.3f s\n  ref: %s\n  got: %s'
              % (label, len(a), len(b), k, t, ctx_a, ctx_b))
        return False
    dt = max((x[0] - y[0] for x, y in zip(a, b)), key=abs, default=0)
    if abs(dt) > TOL:
        print('FAIL %s: identical %d events but timing off by %d samples' % (label, len(a), dt))
        return False
    print('ok   %-10s %6d events, max dt %+d' % (label, len(a), dt))
    return True


def main():
    binary, gamedir, music_dir, sfx_dir = sys.argv[1:5]
    efc = os.path.join(gamedir, 'XEN_SE.EFC')
    com = os.path.join(gamedir, 'PMD.COM')
    fails = 0
    cases = 0
    for m in sorted(glob.glob(os.path.join(gamedir, '*.M'))):
        name = os.path.splitext(os.path.basename(m))[0]
        vgm = os.path.join(music_dir, name + '.vgm')
        if not os.path.exists(vgm):
            continue
        ev, total, lev, lsamp = vgmverify.read_vgm(vgm)
        got = run_trace(binary, ['song', m, str(total + 1000), efc, com])
        cases += 1
        if not compare(name, ev, got, total + EDGE):
            fails += 1
    for n in range(1, 51):
        vgm = os.path.join(sfx_dir, 'se_%02d.vgm' % n)
        if not os.path.exists(vgm):
            continue
        ev, total, lev, lsamp = vgmverify.read_vgm(vgm)
        got = run_trace(binary, ['se', efc, com, str(n), str(total + 1000)])
        cases += 1
        if not compare('se_%02d' % n, ev, got, total):
            fails += 1
    print('check_pmd: %d cases, %d failures' % (cases, fails))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
