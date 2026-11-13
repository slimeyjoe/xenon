#!/usr/bin/env python3
"""Windows 95 release (1997): "ldp4"/"ldp6" voice clip -> WAV decoder (stdlib only).

Usage:
  python3 -I tools/ldp.py FILE.ldp [...]            # writes FILE.ldp.wav next to each input
  python3 -I tools/ldp.py -o OUTDIR FILE.ldp [...]

Format (player XENON.EXE 0x40e040, block decoder 0x40de75) - docs/re/other-versions.md §2.4:
  0x00  "ldp" + '4' or '6'      code width in bits, also selects the delta table
  0x04  u32 BE sample rate      (22050 in every shipped file)
  0x08  u32 BE sample count
  0x0C  blocks: u16 BE byte count, u16 BE sample count, then that many bytes of an MSB-first
        bit stream of codes. Per block the accumulator and the bit buffer start at 0.
        code != 0: acc = (acc + TABLE[code]) & 0xFFFF, output acc as int16
        code == 0: read one more code n, output acc n+1 times
Output: 16-bit mono PCM. The engine shifts samples right by (16 - volume); full volume is a
no-op. In all 4983 shipped clips (all ldp6, 22050 Hz) the block sample counts add up to the
header count and the blocks end exactly at the end of the file.
"""
import os
import struct
import sys
import wave

# Delta tables, XENON.EXE data 0x41B628 (ldp4) and 0x41B648 (ldp6), as int16.
TABLE4 = (0, 8, 26, 86, 283, 927, 3043, 9986, -32768, -9986, -3043, -927, -283, -86, -26, -8)
_POS6 = (0, 1, 2, 3, 4, 5, 6, 7, 10, 15, 20, 29, 40, 56, 78, 109, 153, 214, 299, 419, 586,
         819, 1145, 1601, 2240, 3132, 4380, 6126, 8567, 11980, 16754, 23431)
TABLE6 = _POS6 + (-32768,) + tuple(-v for v in reversed(_POS6[1:]))
TABLES = {4: TABLE4, 6: TABLE6}


def decode(data):
    """Return (rate, samples as a list of int16)."""
    if data[:3] != b'ldp' or data[3] - 48 not in TABLES:
        raise ValueError('not an ldp4/ldp6 file')
    bits = data[3] - 48
    table = [v & 0xFFFF for v in TABLES[bits]]
    mask = (1 << bits) - 1
    rate, count = struct.unpack_from('>II', data, 4)
    pos = 12
    out = []
    while len(out) < count:
        nbytes, nsamp = struct.unpack_from('>HH', data, pos)
        pos += 4
        blk = data[pos:pos + nbytes]
        pos += nbytes
        bp = acc = buf = nb = k = 0

        def code():
            nonlocal bp, buf, nb
            while nb < bits:
                buf = (buf << 8) | (blk[bp] if bp < len(blk) else 0)
                bp += 1
                nb += 8
            nb -= bits
            return (buf >> nb) & mask

        while k < nsamp:
            c = code()
            if c:
                acc = (acc + table[c]) & 0xFFFF
                out.append(acc - 0x10000 if acc & 0x8000 else acc)
                k += 1
            else:
                r = code() + 1
                out.extend([acc - 0x10000 if acc & 0x8000 else acc] * r)
                k += r
    return rate, out[:count]


def write_wav(path, rate, samples):
    w = wave.open(path, 'wb')
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(rate)
    w.writeframes(struct.pack('<%dh' % len(samples), *samples))
    w.close()


def main(a):
    outdir = None
    if a[:1] == ['-o']:
        outdir, a = a[1], a[2:]
        os.makedirs(outdir, exist_ok=True)
    if not a:
        print(__doc__)
        return 1
    for p in a:
        with open(p, 'rb') as f:
            rate, s = decode(f.read())
        out = os.path.join(outdir, os.path.basename(p) + '.wav') if outdir else p + '.wav'
        write_wav(out, rate, s)
        print('%s: %d Hz, %d samples, %.2f s' % (p, rate, len(s), len(s) / rate))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
