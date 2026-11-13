#!/usr/bin/env python3
"""Windows 95 release (1997): XENON.DAT "arc3" archive lister / extractor (stdlib only).

Usage:
  python3 -I tools/arc3.py list    XENON.DAT [EXT ...]       # name, copies, size, method, date
  python3 -I tools/arc3.py extract XENON.DAT OUTDIR [EXT ...] # every file -> OUTDIR/name.ext
  python3 -I tools/arc3.py check   XENON.DAT                  # names whose stored copies differ

EXT filters by extension (bmp ldp mid sco vid). Format (reader XENON.EXE 0x40a320 open,
0x40a050 name lookup, 0x40a750 file open) - docs/re/other-versions.md §2.1:

  header (0x2E bytes, big-endian u32s): "arc3", version (<= 1), block size (0x800),
    data base block (0x10), ?, ?, directory block (1), directory size, ...
  directory: a byte stream of entries; key = extension + base name ("bmp0010", "ldpryo0041").
    hi = b >> 4, lo = b & 15
    hi < 15        name = name[:hi] + next lo bytes; then u24 block
    F0             another copy of the current name at u24 block (the CD had duplicates
                   to shorten seeks; the engine picks the copy nearest the current position)
    FF             increment the last character of the name; then u24 block
    F1..FE         group header: name = next lo bytes; u24 block; u24 offset of the next group
  file at (data base + block) * block size: 32-byte header (BE u32): blocks, size, stored
    size, time, time (Unix), method (0 stored, 1 and 2 compressed - all shipped files are 0),
    then the data.

Copies of one name are not always identical: in 21 of the 336 names stored more than once some
copies differ from the others by 1-4 isolated single-bit flips (mostly bit 7), errors made when
the archive was written. 'extract' therefore takes a per-byte majority vote over all copies (the
first copy is wrong for 7 of those names); names stored once may carry flips that cannot be seen.
"""
import os
import struct
import sys
import time
from collections import Counter


def be(b, o, n=4):
    return int.from_bytes(b[o:o + n], 'big')


class Arc3:
    def __init__(self, path):
        self.f = open(path, 'rb')
        h = self.f.read(0x2E)
        if h[:4] != b'arc3':
            raise ValueError('%s: not an arc3 file' % path)
        if be(h, 4) > 1:
            raise ValueError('%s: arc3 version %d not supported' % (path, be(h, 4)))
        self.block = be(h, 8)
        self.base = be(h, 12)
        self.f.seek(be(h, 0x18) * self.block)
        self.dir = self.f.read(be(h, 0x1C))
        self.entries = {}          # key (bytes) -> [block, ...] in directory order
        self.groups = []
        self._parse()

    def _parse(self):
        d = self.dir
        pos = 0
        name = b''
        while pos < len(d):
            b = d[pos]
            pos += 1
            hi, lo = b >> 4, b & 15
            if hi == 15 and lo == 0:
                self.entries[name].append(be(d, pos, 3))
                pos += 3
                continue
            if hi == 15 and lo == 15:
                name = name[:-1] + bytes([name[-1] + 1])
            elif hi == 15:
                name = d[pos:pos + lo]
                pos += lo
                self.groups.append((name, be(d, pos + 3, 3)))
                self.entries.setdefault(name, []).append(be(d, pos, 3))
                pos += 6
                continue
            else:
                name = name[:hi] + d[pos:pos + lo]
                pos += lo
            self.entries.setdefault(name, []).append(be(d, pos, 3))
            pos += 3

    def header(self, block):
        self.f.seek((self.base + block) * self.block)
        h = self.f.read(32)
        return dict(blocks=be(h, 0), size=be(h, 4), stored=be(h, 8), time=be(h, 12),
                    time2=be(h, 16), method=be(h, 20))

    def read_copy(self, blk):
        h = self.header(blk)
        if h['method'] != 0:
            raise ValueError('block %d: compression method %d not implemented' % (blk, h['method']))
        return self.f.read(h['size'])

    def copies(self, key):
        return [self.read_copy(b) for b in self.entries[key]]

    def read(self, key):
        """Contents of `key`: the per-byte majority over all stored copies.
        Returns (data, number of bytes where the copies disagreed, number of tied votes)."""
        ds = self.copies(key)
        if len(set(ds)) == 1:
            return ds[0], 0, 0
        out = bytearray()
        disagree = ties = 0
        for col in zip(*ds):
            c = Counter(col).most_common(2)
            if len(c) > 1:
                disagree += 1
                ties += c[0][1] == c[1][1]
            out.append(c[0][0])
        return bytes(out), disagree, ties


EXTS = (b'bmp', b'ldp', b'mid', b'sco', b'vid')


def split(key):
    for e in EXTS:
        if key.startswith(e):
            return key[len(e):].decode('ascii'), e.decode()
    return key.decode('ascii', 'replace'), ''


def main(a):
    if len(a) < 2 or a[0] not in ('list', 'extract', 'check'):
        print(__doc__)
        return 1
    arc = Arc3(a[1])
    if a[0] == 'check':
        for key, blocks in arc.entries.items():
            if len(blocks) > 1:
                _, dis, ties = arc.read(key)
                if dis:
                    print('%-16s %2d copies, %d bytes disagree, %d tied' % (key.decode(), len(blocks), dis, ties))
        return 0
    filt = a[3:] if a[0] == 'extract' else a[2:]
    if a[0] == 'extract':
        os.makedirs(a[2], exist_ok=True)
    n = 0
    for key, blocks in arc.entries.items():
        base, ext = split(key)
        if filt and ext not in filt:
            continue
        if a[0] == 'list':
            h = arc.header(blocks[0])
            print('%-16s %-3s %2d %10d m%d %s' % (base, ext, len(blocks), h['size'], h['method'],
                                                time.strftime('%Y-%m-%d', time.gmtime(h['time']))))
        else:
            data, dis, ties = arc.read(key)
            if dis:
                print('%s.%s: copies disagree in %d bytes (%d tied, kept the first copy\'s byte)'
                      % (base, ext, dis, ties), file=sys.stderr)
            with open(os.path.join(a[2], '%s.%s' % (base, ext)), 'wb') as f:
                f.write(data)
        n += 1
    print('%d files' % n, file=sys.stderr)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
