#!/usr/bin/env python3
"""LZSS codec for Xenon (C's Ware, PC-98, 1994) ".CC" scenario scripts ("LCZ" container).

Container layout (verified against AGS.EXE loader 088D:0000):
    0x00  3   "LCZ" magic                (NOT checked by the engine)
    0x03  17  filler                     (NOT read by the engine; uninitialised memory of the
                                          original packer - differs per file)
    0x14  4   u32 LE decompressed size   (engine uses only the low 16 bits, DS:128E)
    0x18  ..  Okumura LZSS stream        (N=4096, F=18, THRESHOLD=2, r0=N-F=0xFEE)

The engine's decoder (088D:0174..0277) initialises ring positions 0..0xFED with 0x00
(rep stosw of 0x7F7 words), NOT with spaces as in Okumura's LZSS.C. For the shipped files the
two fills give identical output (no match ever references the initial ring), but `fill=0` is
the engine-faithful choice.

The encoder is a straight port of Haruhiko Okumura's LZSS.C (4/6/1989) binary-tree encoder,
which reproduces the shipped compressed streams byte-for-byte (see --check).

Usage:
    python3 -I lzss.py d IN.CC OUT.bin          decompress an LCZ file (header stripped)
    python3 -I lzss.py c IN.bin OUT.CC          compress raw bytecode into an LCZ file
                                                (header filler = 17 zero bytes)
    python3 -I lzss.py --check FILE.CC ...      decompress + recompress, compare the LZSS stream
                                                with the original byte-for-byte
"""
import struct
import sys

N = 4096
F = 18
THRESHOLD = 2
NIL = N


def decompress(src, fill=0x00, out_size=None):
    """Decode a raw Okumura LZSS stream (no header). Mirrors 088D:0181..0274."""
    ring = bytearray([fill]) * N
    r = N - F
    out = bytearray()
    i = 0
    n = len(src)
    flags = 0
    while True:
        flags >>= 1
        if not (flags & 0x100):
            if i >= n:
                break
            flags = src[i] | 0xFF00
            i += 1
        if flags & 1:
            if i >= n:
                break
            c = src[i]; i += 1
            out.append(c)
            ring[r] = c
            r = (r + 1) & (N - 1)
        else:
            if i + 1 >= n:
                break
            lo = src[i]; hi = src[i + 1]; i += 2
            pos = lo | ((hi & 0xF0) << 4)
            ln = (hi & 0x0F) + THRESHOLD
            for k in range(ln + 1):
                c = ring[(pos + k) & (N - 1)]
                out.append(c)
                ring[r] = c
                r = (r + 1) & (N - 1)
    if out_size is not None:
        out = out[:out_size]
    return bytes(out)


class _Enc:
    """Port of Okumura's InitTree/InsertNode/DeleteNode/Encode."""

    def __init__(self, fill):
        self.buf = bytearray([fill]) * (N + F - 1)
        self.lson = [NIL] * (N + 1)
        self.rson = [NIL] * (N + 257)
        self.dad = [NIL] * (N + 1)
        self.mpos = 0
        self.mlen = 0

    def insert(self, r):
        buf, lson, rson, dad = self.buf, self.lson, self.rson, self.dad
        cmp = 1
        p = N + 1 + buf[r]
        rson[r] = lson[r] = NIL
        self.mlen = 0
        while True:
            if cmp >= 0:
                if rson[p] != NIL:
                    p = rson[p]
                else:
                    rson[p] = r; dad[r] = p; return
            else:
                if lson[p] != NIL:
                    p = lson[p]
                else:
                    lson[p] = r; dad[r] = p; return
            i = 1
            while i < F:
                cmp = buf[r + i] - buf[p + i]
                if cmp != 0:
                    break
                i += 1
            if i > self.mlen:
                self.mpos = p
                self.mlen = i
                if i >= F:
                    break
        dad[r] = dad[p]; lson[r] = lson[p]; rson[r] = rson[p]
        dad[lson[p]] = r; dad[rson[p]] = r
        if rson[dad[p]] == p:
            rson[dad[p]] = r
        else:
            lson[dad[p]] = r
        dad[p] = NIL

    def delete(self, p):
        lson, rson, dad = self.lson, self.rson, self.dad
        if dad[p] == NIL:
            return
        if rson[p] == NIL:
            q = lson[p]
        elif lson[p] == NIL:
            q = rson[p]
        else:
            q = lson[p]
            if rson[q] != NIL:
                while rson[q] != NIL:
                    q = rson[q]
                rson[dad[q]] = lson[q]; dad[lson[q]] = dad[q]
                lson[q] = lson[p]; dad[lson[p]] = q
            rson[q] = rson[p]; dad[rson[p]] = q
        dad[q] = dad[p]
        if rson[dad[p]] == p:
            rson[dad[p]] = q
        else:
            lson[dad[p]] = q
        dad[p] = NIL

    def encode(self, data):
        buf = self.buf
        out = bytearray()
        inp = iter(data)
        code = bytearray(17)
        code[0] = 0
        cptr = 1
        mask = 1
        s = 0
        r = N - F
        ln = 0
        while ln < F:
            c = next(inp, None)
            if c is None:
                break
            buf[r + ln] = c
            ln += 1
        if ln == 0:
            return bytes(out)
        for i in range(1, F + 1):
            self.insert(r - i)
        self.insert(r)
        while True:
            if self.mlen > ln:
                self.mlen = ln
            if self.mlen <= THRESHOLD:
                self.mlen = 1
                code[0] |= mask
                code[cptr] = buf[r]; cptr += 1
            else:
                code[cptr] = self.mpos & 0xFF; cptr += 1
                code[cptr] = (((self.mpos >> 4) & 0xF0) | (self.mlen - (THRESHOLD + 1))) & 0xFF
                cptr += 1
            mask = (mask << 1) & 0xFF
            if mask == 0:
                out += code[:cptr]
                code[0] = 0; cptr = 1; mask = 1
            last = self.mlen
            i = 0
            while i < last:
                c = next(inp, None)
                if c is None:
                    break
                self.delete(s)
                buf[s] = c
                if s < F - 1:
                    buf[s + N] = c
                s = (s + 1) & (N - 1); r = (r + 1) & (N - 1)
                self.insert(r)
                i += 1
            while i < last:
                i += 1
                self.delete(s)
                s = (s + 1) & (N - 1); r = (r + 1) & (N - 1)
                ln -= 1
                if ln:
                    self.insert(r)
            if ln <= 0:
                break
        if cptr > 1:
            out += code[:cptr]
        return bytes(out)


def compress(data, fill=0x00):
    return _Enc(fill).encode(data)


def read_lcz(raw):
    """Return (header_bytes, decompressed_size, payload) of an LCZ file."""
    if len(raw) < 0x18:
        raise ValueError('file too short for LCZ header')
    hdr = raw[:0x18]
    size = struct.unpack_from('<I', raw, 0x14)[0]
    return hdr, size, raw[0x18:]


def unpack_file(raw, fill=0x00):
    hdr, size, payload = read_lcz(raw)
    if hdr[:3] != b'LCZ':
        sys.stderr.write('warning: no LCZ magic (engine does not check it)\n')
    out = decompress(payload, fill)
    if len(out) != size:
        sys.stderr.write('warning: decoded %d bytes, header says %d\n' % (len(out), size))
    return out


def pack_file(data, filler=b'\0' * 17, fill=0x00):
    return b'LCZ' + filler[:17].ljust(17, b'\0') + struct.pack('<I', len(data)) + compress(data, fill)


def check(paths):
    ok = True
    for p in paths:
        raw = open(p, 'rb').read()
        hdr, size, payload = read_lcz(raw)
        d0 = decompress(payload, 0x00)
        d1 = decompress(payload, 0x20)
        re0 = compress(d0, 0x00)
        re1 = compress(d1, 0x20)
        st = []
        st.append('size=%s' % ('ok' if len(d0) == size else 'BAD(%d!=%d)' % (len(d0), size)))
        st.append('fill0==fill20' if d0 == d1 else 'FILL-DEPENDENT')
        st.append('recompress(fill0)=%s' % ('identical' if re0 == payload else 'differs'))
        st.append('recompress(fill20)=%s' % ('identical' if re1 == payload else 'differs'))
        good = len(d0) == size and (re0 == payload or re1 == payload)
        ok &= good
        print('%-12s %6d -> %6d  %s' % (p.rsplit('/', 1)[-1], len(raw), len(d0), ' '.join(st)))
    return ok


def main(argv):
    if len(argv) >= 2 and argv[1] == '--check':
        sys.exit(0 if check(argv[2:]) else 1)
    if len(argv) != 4 or argv[1] not in ('c', 'd'):
        print(__doc__)
        sys.exit(2)
    data = open(argv[2], 'rb').read()
    if argv[1] == 'd':
        open(argv[3], 'wb').write(unpack_file(data))
    else:
        open(argv[3], 'wb').write(pack_file(data))


if __name__ == '__main__':
    main(sys.argv)
