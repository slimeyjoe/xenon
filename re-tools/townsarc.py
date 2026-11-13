#!/usr/bin/env python3
"""FM Towns CD release (1995): C's Ware archive lister / extractor (stdlib only).

Usage:
  python3 -I re-tools/townsarc.py list    ARCHIVE [...]       # CC GD GT M PCM CDFILE.F01..F04
  python3 -I re-tools/townsarc.py extract ARCHIVE OUTDIR      # every member -> OUTDIR/NAME
  python3 -I re-tools/townsarc.py extract --xor ARCHIVE OUTDIR  # also undo the XOR 0x7C of CC members

Format (docs/re/other-versions.md §3.2): u16 LE directory size D, then D/16 entries of
name[12] (NUL padded) + u32 LE member offset. Entry k starts at 2 + 16k; a member ends where
the next one starts (the last one at the end of the file). In the CDFILE.F0x voice archives the
members are padded to 2048-byte CD sectors.
"""
import os
import struct
import sys


def parse(data):
    """Return [(name, offset, size)]."""
    dsz = struct.unpack_from('<H', data, 0)[0]
    ents = []
    for k in range(dsz // 16):
        raw = data[2 + 16 * k:14 + 16 * k]
        name = raw.split(b'\0')[0].decode('ascii', 'replace')
        if name:
            ents.append([name, struct.unpack_from('<I', data, 14 + 16 * k)[0]])
    for i, e in enumerate(ents):
        e.append((ents[i + 1][1] if i + 1 < len(ents) else len(data)) - e[1])
    return [tuple(e) for e in ents]


def main(a):
    xor = '--xor' in a
    a = [x for x in a if x != '--xor']
    if len(a) < 2 or a[0] not in ('list', 'extract') or (a[0] == 'extract' and len(a) < 3):
        print(__doc__)
        return 1
    if a[0] == 'list':
        for p in a[1:]:
            with open(p, 'rb') as f:
                d = f.read()
            ents = parse(d)
            print('== %s: %d members, %d bytes' % (p, len(ents), len(d)))
            for n, o, s in ents:
                print('  %-12s %10d %8d' % (n, o, s))
        return 0
    with open(a[1], 'rb') as f:
        d = f.read()
    os.makedirs(a[2], exist_ok=True)
    for n, o, s in parse(d):
        blob = d[o:o + s]
        if xor:
            blob = bytes(c ^ 0x7C for c in blob)
        with open(os.path.join(a[2], n), 'wb') as f:
            f.write(blob)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
