#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""pmd_capture.py - run the real PMD.COM 4.7g (pmdemu.py, under unicorn) on a song and write
what it does interrupt by interrupt, as a reference for lrvne's PMD module (its tests,
lrvne/tests/data/pmd/README.md).

Usage (with the Python that has unicorn):
    pmd_capture.py --pmd PMD.COM SONG.M -o OUT.log [--opts "/M10 /V0 /E4 /F /K"]
                   [--ssgtab TABLE] [--irqs N] [--event IRQ:AX ...]

--ssgtab replaces PMD's built-in SSG-effect table (CS:2CE9h) in memory with TABLE before the
song starts, so that a test of the drums of part K needs none of PMD.COM's own data.
--event IRQ:AX calls INT 60h with AX (hex) just before interrupt number IRQ (0 = before the
first), e.g. 1940 (fade volume 40h), 0210 (fade-out, speed 10h), 0307 (SSG effect 7).

The log, one line each:
    # comment
    I r=v r=v ...                   the installer's register writes (hex)
    S AX r=v ...                    an INT 60h call (the song start is S 0000) and its writes
    T CLOCK K s2=SS m=MMMM f=FF s1=SS r=v ...
                                    a timer interrupt at master clock CLOCK (3 993 600 Hz,
                                    counted from the installation), K = A, B or AB (both flags,
                                    B's work first); then PMD's status2, measure counter, fade
                                    volume and status1 after it; then its writes, in order
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import pmdemu   # noqa: E402

SSG_TABLE = 0x2CE9
SSG_TABLE_SIZE = 0x3096 - 0x2CE9


def wlist(ws):
    return ' '.join('%02X=%02X' % (w[3], w[4]) for w in ws)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('song')
    ap.add_argument('-o', '--out', required=True)
    ap.add_argument('--pmd', required=True)
    ap.add_argument('--opts', default='/M10 /V0 /E4 /F /K')
    ap.add_argument('--ssgtab')
    ap.add_argument('--irqs', type=int, default=6000)
    ap.add_argument('--event', action='append', default=[])
    a = ap.parse_args(argv)
    events = {}
    for ev in a.event:
        irq, ax = ev.split(':')
        events.setdefault(int(irq), []).append(int(ax, 16))

    e = pmdemu.PMDEmu(a.pmd, opts=a.opts)
    pps = e.read_cs(0x30AC, 1)[0]
    if pps != 0:
        sys.exit('pmd_capture: PMD thinks PPSDRV is there (30ACh = %02Xh)' % pps)
    out = ['# PMD.COM 4.7g under emulation (26K board), options "%s"' % a.opts,
           '# song %s, %d interrupts%s' % (os.path.basename(a.song), a.irqs,
                                           ', SSG-effect table %s' % os.path.basename(a.ssgtab)
                                           if a.ssgtab else ''),
           'I ' + wlist(e.writes)]
    if a.ssgtab:
        table = open(a.ssgtab, 'rb').read()
        if len(table) > SSG_TABLE_SIZE:
            sys.exit('pmd_capture: the table is larger than PMD\'s (%d bytes)' % SSG_TABLE_SIZE)
        e.uc.mem_write(pmdemu.PSP * 16 + SSG_TABLE, table)
    e.load_music(open(a.song, 'rb').read())

    def call(ax):
        n = len(e.writes)
        e.kind = 'S'
        if ax == 0x0000:
            e.mstart()
        else:
            e.int60(ax)
        out.append(('S %04X ' % ax + wlist(e.writes[n:])).rstrip())

    call(0x0000)
    irq = 0
    while irq < a.irqs:
        for ax in events.pop(irq, []):
            call(ax)
        n = len(e.writes)
        kind = e.step()
        if not kind:
            continue
        s1, s2 = e.read_cs(0x3111, 2)
        fade = e.read_cs(0x3115, 1)[0]
        meas = int.from_bytes(e.read_cs(0x315B, 2), 'little')
        out.append(('T %d %s s2=%02X m=%04X f=%02X s1=%02X %s' % (
            e.clk, kind, s2, meas, fade, s1, wlist(e.writes[n:]))).rstrip())
        irq += 1
    with open(a.out, 'w') as f:
        f.write('\n'.join(out) + '\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
