#!/usr/bin/env python3
"""Compare the C GDT/ADT decoder (tests/test_gdt) with the verified reference re-tools/gdt.py.

Cases: every GDT alone on 0x00 and 0xFF planes; every diff chain of gdt.DIFF_CHAIN composed
like the engine does; every ADT frame alone on both fills. Also checks that the bounding box
the C decoder reports covers every byte it changed and lies inside the header rectangle.

usage: check_gdt.py TEST_GDT_BIN GAMEDIR
"""
import glob
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 're-tools'))
import gdt   # noqa: E402
import adt   # noqa: E402

PLANE = 80 * 400


def run_c(binary, fill, specs):
    r = subprocess.run([binary, fill] + specs, capture_output=True, check=True)
    rect = None
    for line in r.stderr.decode().splitlines():
        if line.startswith('rect '):
            rect = tuple(int(v) for v in line.split()[1:])
    return r.stdout, rect


def ref(fill, blobs):
    planes = gdt.new_planes(fill)
    hdr = None
    for b in blobs:
        hdr = gdt.parse_header(b)
        gdt.decode(b, planes)
    return b''.join(bytes(p[:PLANE]) for p in planes), hdr


def check_rect(before, after, rect, hdr):
    """Every changed byte must be inside rect; rect must be inside the header rect."""
    x0, y0, x1, y1 = rect
    if x1 <= x0:
        return None if before == after else 'empty rect but bytes changed'
    if x0 < hdr['xb'] or x1 > hdr['xb'] + hdr['wb']:
        return 'rect columns %d..%d outside header %d..%d' % (x0, x1, hdr['xb'], hdr['xb'] + hdr['wb'])
    for p in range(4):
        a = before[p * PLANE:(p + 1) * PLANE]
        b = after[p * PLANE:(p + 1) * PLANE]
        for i in range(PLANE):
            if a[i] != b[i]:
                y, x = divmod(i, 80)
                if not (x0 <= x < x1 and y0 <= y < y1):
                    return 'changed byte plane %d x=%d y=%d outside rect %s' % (p, x, y, rect)
    return None


def main():
    binary, gamedir = sys.argv[1], sys.argv[2]
    fails = 0
    cases = 0
    gdts = sorted(glob.glob(os.path.join(gamedir, '*.GDT')))
    by_name = {os.path.splitext(os.path.basename(f))[0].upper(): f for f in gdts}

    def case(label, fill, specs, blobs):
        nonlocal fails, cases
        cases += 1
        out, rect = run_c(binary, fill, specs)
        want, hdr = ref(0x00 if fill == '00' else 0xFF, blobs)
        if out != want:
            fails += 1
            for p in range(4):
                for i in range(PLANE):
                    if out[p * PLANE + i] != want[p * PLANE + i]:
                        print('FAIL %s fill %s: plane %d offset %d (x=%d y=%d): got %02x want %02x' %
                              (label, fill, p, i, i % 80, i // 80, out[p * PLANE + i], want[p * PLANE + i]))
                        break
                else:
                    continue
                break
            return
        before, _ = ref(0x00 if fill == '00' else 0xFF, blobs[:-1])
        err = check_rect(before, want, rect, hdr)
        if err:
            fails += 1
            print('FAIL %s fill %s: %s' % (label, fill, err))

    for f in gdts:
        data = open(f, 'rb').read()
        if len(data) < 24:
            continue
        for fill in ('00', 'ff'):
            case(os.path.basename(f), fill, [f], [data])

    for name, chain in sorted(gdt.DIFF_CHAIN.items()):
        base = gdt.palette_base(name)
        names = [base] + chain + [name]
        files = [by_name[n] for n in names]
        case('chain ' + '>'.join(names), '00', files, [open(x, 'rb').read() for x in files])

    for f in sorted(glob.glob(os.path.join(gamedir, '*.ADT'))):
        a = adt.parse_adt(open(f, 'rb').read())
        for i, blob in enumerate(a['frames']):
            for fill in ('00', 'ff'):
                case('%s@%d' % (os.path.basename(f), i), fill, ['%s@%d' % (f, i)], [blob])

    print('check_gdt: %d cases, %d failures' % (cases, fails))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
