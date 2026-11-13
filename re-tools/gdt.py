#!/usr/bin/env python3
"""GDT ("DA1") image decoder for C's Ware AGS engine (Xenon ~Mugen no Shitai~, PC-98, 1994).

Reimplements the hand-written decoder in AGS.EXE segment 0456 (entry 0456:0004,
see docs/re/graphics.md).  Output is bit-exact with the original code: every GDT
and every ADT frame was checked against an x86 emulation of 0456:0004.

Format summary (all little-endian):
  00  4  "DA1\\0"
  04  u32 file size (informational, not used by the engine)
  08  u8  x   (in bytes = 8-pixel units)
  09  u8  width (in bytes)
  0A  u16 y   (lines)
  0C  u16 height (lines; not used by the decoder itself)
  0E  u16 flags: 0x8000 = 24-byte palette follows, 0x4000 = 1-byte column codec
                 (else 2-byte column codec, used by ADT frames), 0x0F00 = 4 planes
  10  24  palette (only if flags & 0x8000), 16 x 12-bit, packed 2 colours / 3 bytes
  ..  4 x u16 compressed plane sizes (B, R, G, I order = VRAM A800/B000/B800/E000)
  ..  plane streams

Each plane stream is a sequence of vertical column programs, one per 8-pixel
column (1-byte codec) or 16-pixel column pair (2-byte codec); a column program
runs top-to-bottom and ends with 0xFF.  See graphics.md for the opcode tables.

Library use:
    img = gdt.decode(open('ROGO.GDT','rb').read())          # onto blank planes
    img.index()        -> numpy (400,640) uint8 colour indices
    img.palette_rgb()  -> list of 16 (r,g,b) 0..255
    gdt.decode(data, planes=img.planes)                      # overlay like the engine

Command line:
    gdt.py [-o OUTDIR] [--index FILE] [--composite-dir DIR] FILE.GDT...
        Writes OUTDIR/<NAME>.png (indexed PNG cropped to the image rectangle;
        pixels the image does not write are transparent) and a TSV index.
    gdt.py --composite OUT.png BASE.GDT [OVERLAY.GDT[:merge8] ...]
        Engine-style composition on a 640x400 canvas: each file is decoded into the
        same plane buffers (like script ops G_LOAD/0x16/0x28); ":merge8" applies the
        file the way ops 0x34+0x35 do (decoded separately, colour 8 = transparent).
        The palette is that of the last file carrying one, as on the real machine.

Requires numpy and Pillow.
"""
import argparse
import os
import struct
import sys

import numpy as np
from PIL import Image

PLANE_SIZE = 0x10000          # one 64 KiB segment per plane, offsets wrap like 16-bit DI
SCREEN_BYTES = 80 * 400       # 640x400, 1 bpp per plane

# 2-byte codec, opcodes D3..F2: copy a word from (di + offset) in the same plane.
_REL2 = [-0x500, -0x3C0, -0x280, -0x140, -0xA0, -0x50,
         -0x282, -0x142, -0xA2, -0x52, -2, 0x4E, 0x9E, 0x13E, 0x27E,
         -0x284, -0x144, -0xA4, -0x54, -4, 0x4C, 0x9C, 0x13C, 0x27C,
         -0x146, -0xA6, -0x56, -6, 0x4A, 0x9A, 0x13A, -8]
# 1-byte codec, opcodes A0..EF (high nibble): copy from (di - offset) in the same plane.
_REL1 = {0xA: 0x500, 0xB: 0x280, 0xC: 0x140, 0xD: 0xA0, 0xE: 2}
_PAIRS = {1: (0, 1), 2: (0, 2), 3: (1, 2)}


def _dup(n):
    """Low nibble replicated into both nibbles (a 4-pixel pattern repeated)."""
    n &= 0xF
    return n | (n << 4)


def _ror8(x, n):
    return ((x >> n) | (x << (8 - n))) & 0xFF


class GdtError(Exception):
    pass


def parse_header(data):
    """Return dict with the header fields and stream offsets of a DA1 blob."""
    if len(data) < 24 or data[:4] != b'DA1\0':
        raise GdtError('not a DA1 image')
    size, = struct.unpack_from('<I', data, 4)
    xb, wb = data[8], data[9]
    y, h, flags = struct.unpack_from('<HHH', data, 10)
    p = 16
    palette = None
    if flags & 0x8000:
        palette = unpack_palette(data[16:40])
        p = 40
    sizes = struct.unpack_from('<4H', data, p)
    p += 8
    offs = []
    for s in sizes:
        offs.append(p)
        p += s
    return dict(size=size, xb=xb, wb=wb, x=xb * 8, w=wb * 8, y=y, h=h, flags=flags,
                palette=palette, plane_sizes=sizes, plane_offsets=offs, end=p)


def unpack_palette(raw):
    """24 bytes -> 16 (R,G,B) 4-bit triples (engine routine 0C2A:0514).

    Per pair of colours (n, n+1) in 3 bytes b0 b1 b2:
      B[n] = b0>>4, R[n] = b0&15, G[n] = b1>>4,
      B[n+1] = b1&15, R[n+1] = b2>>4, G[n+1] = b2&15
    """
    pal = []
    for i in range(8):
        b0, b1, b2 = raw[3 * i:3 * i + 3]
        pal.append((b0 & 15, b1 >> 4, b0 >> 4))
        pal.append((b2 >> 4, b2 & 15, b1 & 15))
    return pal


def pal4_to_rgb(pal):
    return [(r * 17, g * 17, b * 17) for r, g, b in pal]


# ---------------------------------------------------------------------------
# 1-byte (8-pixel) column codec: 0456:38EA, jump table 0456:3902
# ---------------------------------------------------------------------------
def _column1(d, si, planes, pl, di):
    v = planes[pl]

    def put(val):
        nonlocal di
        v[di] = val
        di = (di + 80) & 0xFFFF

    while True:
        b = d[si]
        si += 1
        if b == 0xFF:                                   # end of column
            return si
        if b < 0x40:                                    # 00-1F fill 0x00, 20-3F fill 0xFF
            n = b & 0x1F
            if n == 0:
                n = d[si]
                si += 1
            val = 0x00 if b < 0x20 else 0xFF
            for _ in range(n):
                put(val)
        elif b < 0xA0:                                  # 40/60/80: copy plane 0/1/2
            n = b & 0x1F
            if n == 0:
                n = d[si]
                si += 1
            src = planes[(b - 0x40) >> 5]
            for _ in range(n):
                put(src[di])
        elif b < 0xF0:                                  # A0-EF: copy from earlier in plane
            n = b & 0xF
            if n == 0:
                n = d[si]
                si += 1
            off = _REL1[b >> 4]
            for _ in range(n):
                put(v[(di - off) & 0xFFFF])
        elif b <= 0xF8:                                 # F0 n / F1-F8: literal bytes
            if b == 0xF0:
                n = d[si]
                si += 1
            else:
                n = b - 0xF0
            for _ in range(n):
                put(d[si])
                si += 1
        elif b == 0xF9:                                 # skip n lines
            di = (di + 80 * d[si]) & 0xFFFF
            si += 1
        elif b == 0xFA:                                 # fill n x byte
            n, val = d[si], d[si + 1]
            si += 2
            for _ in range(n):
                put(val)
        elif b == 0xFB:                                 # NOT plane0 (n<80) / plane1
            n = d[si]
            si += 1
            src = planes[0] if n < 0x80 else planes[1]
            for _ in range(n & 0x7F):
                put(src[di] ^ 0xFF)
        elif b == 0xFC:
            n = d[si]
            si += 1
            if n < 0x80:                                # NOT plane2
                for _ in range(n):
                    put(planes[2][di] ^ 0xFF)
            else:                                       # 2-line nibble pattern
                q = d[si]
                si += 1
                a, c = _dup(q), _dup(q >> 4)
                for _ in range(n & 0x7F):
                    put(a)
                    put(c)
        elif b == 0xFD:
            n = d[si]
            si += 1
            if n < 0x80:                                # 2-line byte pattern
                a, c = d[si], d[si + 1]
                si += 2
                pat = (a, c)
            elif n < 0xC0:                              # 4-line: nibbles + rotated
                q = d[si]
                si += 1
                a, c = _dup(q), _dup(q >> 4)
                pat = (a, c, _ror8(a, 2), _ror8(c, 2))
            else:                                       # 4-line nibble pattern
                q, r = d[si], d[si + 1]
                si += 2
                pat = (_dup(q), _dup(q >> 4), _dup(r), _dup(r >> 4))
            for _ in range(n & (0x7F if n < 0x80 else 0x3F)):
                for t in pat:
                    put(t)
        else:                                           # 0xFE
            n = d[si]
            si += 1
            if n < 0x40:                                # 4-line byte pattern
                pat = d[si:si + 4]
                si += 4
                for _ in range(n):
                    for t in pat:
                        put(t)
            else:                                       # AND of two planes
                a, c = _PAIRS[n >> 6]
                for _ in range(n & 0x3F):
                    put(planes[a][di] & planes[c][di])


# ---------------------------------------------------------------------------
# 2-byte (16-pixel) column-pair codec: 0456:0240, jump table 0456:0258
# Tokens are read as words: low byte = parameter a, high byte = opcode.
# ---------------------------------------------------------------------------
def _column2(d, si, planes, pl, di):
    v = planes[pl]

    def put(w):
        nonlocal di
        v[di] = w & 0xFF
        v[(di + 1) & 0xFFFF] = w >> 8
        di = (di + 80) & 0xFFFF

    def word(p, addr):
        q = planes[p]
        return q[addr & 0xFFFF] | (q[(addr + 1) & 0xFFFF] << 8)

    while True:
        a, op = d[si], d[si + 1]
        si += 2
        if op == 0xFF:                                  # end of column pair
            return si
        if op < 0x80:                                   # 2-line nibble pattern, op reps
            w0, w1 = _dup(a) * 0x101, _dup(a >> 4) * 0x101
            for _ in range(op):
                put(w0)
                put(w1)
        elif op < 0xC0:                                 # 4-line nibble+rotated pattern
            lo, hi = _dup(a), _dup(a >> 4)
            pat = (lo * 0x101, hi * 0x101, _ror8(lo, 2) * 0x101, _ror8(hi, 2) * 0x101)
            for _ in range(op & 0x3F):
                for t in pat:
                    put(t)
        elif op < 0xD0:                                 # fill a lines with nibble op&15
            w = _dup(op) * 0x101
            for _ in range(a):
                put(w)
        elif op < 0xD2:                                 # literal words, n = a + 256*(op&1)
            for _ in range(a | ((op & 1) << 8)):
                put(d[si] | (d[si + 1] << 8))
                si += 2
        elif op == 0xD2:                                # skip a lines
            di = (di + 80 * a) & 0xFFFF
        elif op <= 0xF2:                                # copy from relative position
            off = _REL2[op - 0xD3]
            for _ in range(a):
                put(word(pl, di + off))
        elif op <= 0xF5:                                # copy plane 0/1/2
            for _ in range(a):
                put(word(op - 0xF3, di))
        elif op <= 0xF8:                                # NOT plane 0/1/2
            for _ in range(a):
                put(word(op - 0xF6, di) ^ 0xFFFF)
        elif op <= 0xFB:                                # AND planes (0,1) (0,2) (1,2)
            p, q = _PAIRS[op - 0xF8]
            for _ in range(a):
                put(word(p, di) & word(q, di))
        elif op == 0xFC:                                # fill a lines with literal word
            w = d[si] | (d[si + 1] << 8)
            si += 2
            for _ in range(a):
                put(w)
        elif op == 0xFD:                                # 2-line pattern
            if a < 0x80:
                pat = (d[si] * 0x101, d[si + 1] * 0x101)
                si += 2
            else:
                pat = struct.unpack_from('<2H', d, si)
                si += 4
            for _ in range(a & 0x7F):
                for t in pat:
                    put(t)
        else:                                           # 0xFE: 4-line patterns
            if a < 0x40:
                # quirk reproduced from 0456:055E (never used by game data)
                pa, _unused, pc, pd = d[si:si + 4]
                si += 4
                swn = ((pa & 15) << 4) | (pa >> 4)
                pat = (pa * 0x101, pd * 0x101, pd | (swn << 8),
                       (((pd & 15) << 4) | (pc >> 4)) | ((((pc & 15) << 4) | (pd >> 4)) << 8))
            elif a < 0x80:
                p, q = d[si], d[si + 1]
                si += 2
                pat = (_dup(p) * 0x101, _dup(p >> 4) * 0x101, _dup(q) * 0x101, _dup(q >> 4) * 0x101)
            elif a < 0xC0:
                pat = tuple(b * 0x101 for b in d[si:si + 4])
                si += 4
            else:
                pat = struct.unpack_from('<4H', d, si)
                si += 8
            for _ in range(a & 0x3F):
                for t in pat:
                    put(t)


class GdtImage:
    """Result of decoding: header fields + the four 64 KiB plane buffers."""

    def __init__(self, hdr, planes):
        self.__dict__.update(hdr)
        self.planes = planes

    def index(self):
        """(400,640) uint8 array of colour indices (B + 2R + 4G + 8I)."""
        return planes_to_index(self.planes)

    def palette_rgb(self):
        return pal4_to_rgb(self.palette) if self.palette else None


def new_planes(fill=0):
    return [bytearray(bytes([fill]) * PLANE_SIZE) for _ in range(4)]


def decode(data, planes=None, strict=True):
    """Decode a DA1 blob into `planes` (4 bytearrays of 64 KiB; new blank ones if None).

    Reproduces 0456:00D5: the rectangle is processed column by column from x to x+w-1;
    with the 1-byte codec (flags & 0x4000) every column uses it, otherwise column
    pairs use the 2-byte codec and a leading column (odd x) / trailing column
    (odd x+w) use the 1-byte codec.  Before the first pair each plane pointer is
    rounded up to an even offset (0456:3E64; the engine buffers are word aligned).
    """
    hdr = parse_header(data)
    if planes is None:
        planes = new_planes()
    ptr = list(hdr['plane_offsets'])
    pos = hdr['y'] * 80 + hdr['xb']
    wb = hdr['wb']
    flags = hdr['flags']

    def single(c):
        for pl in range(4):
            ptr[pl] = _column1(data, ptr[pl], planes, pl, c & 0xFFFF)

    def pairs(c, npairs):
        if npairs == 0:
            return c
        if flags & 0x4000:
            for _ in range(npairs):
                single(c)
                single(c + 1)
                c += 2
            return c
        for pl in range(4):
            ptr[pl] += ptr[pl] & 1
        for _ in range(npairs):
            for pl in range(4):
                ptr[pl] = _column2(data, ptr[pl], planes, pl, c & 0xFFFF)
            c += 2
        return c

    half = wb >> 1
    if not pos & 1:
        c = pairs(pos, half)
        if wb & 1:
            single(c)
    elif not wb & 1:
        single(pos)
        c = pairs(pos + 1, half - 1)
        single(c)
    else:
        single(pos)
        pairs(pos + 1, half)
    if strict:
        ends = [o + s for o, s in zip(hdr['plane_offsets'], hdr['plane_sizes'])]
        for pl in range(4):
            if not (ends[pl] - 1 <= ptr[pl] <= ends[pl]):
                raise GdtError('plane %d stream ended at %d, expected %d' % (pl, ptr[pl], ends[pl]))
    return GdtImage(hdr, planes)


def planes_to_index(planes):
    bits = [np.unpackbits(np.frombuffer(bytes(p[:SCREEN_BYTES]), np.uint8)).reshape(400, 640)
            for p in planes]
    return (bits[0] | (bits[1] << 1) | (bits[2] << 2) | (bits[3] << 3)).astype(np.uint8)


def index_to_planes(idx):
    planes = new_planes()
    for p in range(4):
        b = np.packbits(((idx >> p) & 1).astype(np.uint8), axis=1).tobytes()
        planes[p][:SCREEN_BYTES] = b
    return planes


def written_mask(data):
    """(400,640) bool: True where the image result does not depend on prior screen
    contents (decoded on all-0 and all-1 backgrounds and compared)."""
    a = decode(data, new_planes(0x00)).index()
    b = decode(data, new_planes(0xFF)).index()
    transparent = (a == 0) & (b == 15)
    mixed = (a != b) & ~transparent
    return ~transparent, int(mixed.sum())


def merge_color8(dst_planes, src_planes, xb, y, wb, h):
    """0C2A:01F9: copy rect from src into dst, treating colour-8 pixels of src
    (only intensity plane set) as transparent."""
    for row in range(y, y + h):
        for c in range(xb, xb + wb):
            i = row * 80 + c
            p0, p1, p2, p3 = (src_planes[k][i] for k in range(4))
            keep = (~(p0 | p1 | p2) & p3) & 0xFF       # 1 = colour 8 = keep dst
            for k in range(4):
                dst_planes[k][i] &= keep
            dst_planes[0][i] |= p0
            dst_planes[1][i] |= p1
            dst_planes[2][i] |= p2
            dst_planes[3][i] |= p3 & ~keep & 0xFF


# ---------------------------------------------------------------------------
# Palette source for images without their own palette (flags 0x4F00).
# These are "sabun" overlays loaded with script op 0x16 on top of a CG; the
# hardware palette is the CG's.  Derived from the scenario scripts.
# ---------------------------------------------------------------------------
BA_B_BASE = {'BI': '0010_2', 'FA': '0010_2', 'MAI': '1060', 'MEG': '1070', 'RA': '0010',
             'RYO': '1040', 'SA': '1040', 'TO': '0070'}
# Images merged with colour-8 keying (script ops 0x34 + 0x35, 0C2A:0142/01F9).
MERGE8 = {'0040B', '0110', '0120', 'BI_BAST', 'FA_BAST', 'KAI_BAST', 'MAI_BAST', 'MEG_BAST',
          'RA&TO03B', 'RA&TO03C', 'RA_BAST', 'RYO_BAST', 'SA_BAST', 'TO_BAST'}


def palette_base(name):
    """Name of the GDT whose palette is in effect when `name` (no palette) is shown."""
    n = name.upper()
    if n.endswith('_BA_B'):
        return BA_B_BASE.get(n[:-5])
    if n[-1] in 'BCDE':
        return n[:-1]
    return None


def kind_of(name, hdr):
    n = name.upper()
    if n in ('WAKU', 'WAKU2'):
        return 'frame'
    if n.startswith('ST0'):
        return 'staff'
    if n in ('MENU1', 'MENU2', 'MUSIC', 'ALL', 'ALL2', 'ROGO', 'XENON2', '0100B', '0100BB'):
        return 'system/opening'
    if n.startswith('BAST_M') or n.endswith('_M') or n == 'SAYA06BM':
        return 'blink-strip(3 frames)'
    if n.endswith('_BA_B'):
        return 'bust-diff'
    if n in MERGE8:
        return 'bust/merge8'
    if not hdr['flags'] & 0x8000:
        return 'diff'
    if hdr['wb'] == 70 and hdr['h'] == 296 and hdr['xb'] == 5 and hdr['y'] == 4:
        return 'cg'
    return 'cg-part'


def save_png(path, idx, rgb, alpha=None):
    """Write an indexed PNG; colour index 16 is used for transparent pixels."""
    pal = [c for col in rgb for c in col]
    if alpha is not None and not alpha.all():
        idx = idx.copy()
        idx[~alpha] = 16
        pal += [0, 0, 0]
        im = Image.fromarray(idx, 'P')
        im.putpalette(pal)
        im.info['transparency'] = 16
        im.save(path, optimize=True, transparency=16)
    else:
        im = Image.fromarray(idx, 'P')
        im.putpalette(pal)
        im.save(path, optimize=True)


def gray_palette():
    return [(i * 17, i * 17, i * 17) for i in range(16)]


def batch(files, outdir, index_path):
    os.makedirs(outdir, exist_ok=True)
    by_name = {os.path.splitext(os.path.basename(f))[0].upper(): f for f in files}
    pal_cache = {}

    def palette_of(name):
        if name in pal_cache:
            return pal_cache[name]
        f = by_name.get(name)
        pal = None
        if f:
            d = open(f, 'rb').read()
            if len(d) >= 24 and d[:4] == b'DA1\0':
                pal = parse_header(d)['palette']
        pal_cache[name] = pal
        return pal

    rows = []
    for f in sorted(files):
        name = os.path.splitext(os.path.basename(f))[0].upper()
        data = open(f, 'rb').read()
        try:
            hdr = parse_header(data)
        except GdtError:
            rows.append([name, str(len(data)), '', '', '', '', '', '', 'not a DA1 image (stub)', ''])
            continue
        img = decode(data)
        idx = img.index()
        alpha, mixed = written_mask(data)
        if hdr['palette']:
            pal, src = hdr['palette'], 'own'
        else:
            base = palette_base(name)
            pal = palette_of(base) if base else None
            src = ('borrowed:' + base) if pal else 'none(gray)'
        rgb = pal4_to_rgb(pal) if pal else gray_palette()
        x, y, w, h = hdr['x'], hdr['y'], hdr['w'], hdr['h']
        crop = idx[y:y + h, x:x + w]
        a = alpha[y:y + h, x:x + w].copy()
        notes = []
        if name in MERGE8:
            a &= crop != 8
            notes.append('colour 8 = transparent (merge8)')
        if not alpha[y:y + h, x:x + w].all():
            notes.append('%d unwritten px transparent' % int((~alpha[y:y + h, x:x + w]).sum()))
        if mixed:
            notes.append('%d px depend on prior screen' % mixed)
        save_png(os.path.join(outdir, name + '.png'), crop, rgb, a)
        codec = '1-byte' if hdr['flags'] & 0x4000 else '2-byte'
        rows.append([name, str(len(data)), '%d,%d' % (x, y), '%dx%d' % (w, h),
                     '0x%04X' % hdr['flags'], codec, kind_of(name, hdr), src,
                     '; '.join(notes), '%d,%d,%d,%d' % (hdr['xb'], hdr['y'], hdr['wb'], hdr['h'])])
    with open(index_path, 'w') as fo:
        fo.write('name\tfile_size\tpos_px(x,y)\tsize_px(wxh)\tflags\tcodec\tkind\tpalette\tnotes\traw(xbytes,y,wbytes,h)\n')
        for r in rows:
            fo.write('\t'.join(r) + '\n')
    return rows


def composite(out, specs):
    """Engine-style composition; see module docstring."""
    planes = new_planes()
    pal = None
    for spec in specs:
        path, _, mode = spec.partition(':')
        data = open(path, 'rb').read()
        hdr = parse_header(data)
        if mode == 'merge8':
            tmp = decode(data, new_planes(0x00))     # page 1 is cleared before use
            merge_color8(planes, tmp.planes, hdr['xb'], hdr['y'], hdr['wb'], hdr['h'])
        else:
            decode(data, planes)
            if hdr['palette']:
                pal = hdr['palette']
    save_png(out, planes_to_index(planes), pal4_to_rgb(pal) if pal else gray_palette())


# Bust room used for composites of the merge8 busts (most frequent in the scripts).
BAST_ROOM = {'BI': '0010_2', 'FA': '0010_2', 'KAI': '0040', 'MAI': '1060', 'MEG': '1070',
             'RA': '0050', 'RYO': '1060', 'SA': '1060', 'TO': '0070'}


# Diffs that the scripts apply on top of earlier diffs of the same CG (op 0x16 chains).
DIFF_CHAIN = {'MAI02C': ['MAI02B'], 'RA02C': ['RA02B'], 'RYO02C': ['RYO02B'],
              'R&M03C': ['R&M03B'], 'R&SC': ['R&SB'], 'TO09C': ['TO09B'], 'TO03C': ['TO03B'],
              'BI06C': ['BI06B'], 'BI06D': ['BI06B', 'BI06C'], 'BI06E': ['BI06B', 'BI06C', 'BI06D'],
              'TO05C': ['TO05B'], 'TO05D': ['TO05B', 'TO05C'], 'TO05E': ['TO05B', 'TO05C', 'TO05D']}


def composite_batch(files, outdir):
    """Write engine-style composites for every overlay image (diffs on their CG,
    busts merged on a room, bust diffs on room+bust) into outdir."""
    os.makedirs(outdir, exist_ok=True)
    by_name = {os.path.splitext(os.path.basename(f))[0].upper(): f for f in files}
    n = 0
    for name in sorted(by_name):
        data = open(by_name[name], 'rb').read()
        if len(data) < 24:
            continue
        hdr = parse_header(data)
        kind = kind_of(name, hdr)
        specs = None
        if name in MERGE8:
            if name.endswith('_BAST'):
                room = BAST_ROOM[name[:-5]]
            elif name.startswith('RA&TO03'):
                room = 'RA&TO03'
            else:
                room = '0040' if name == '0040B' else '0010_2'
            specs = [by_name['WAKU'], by_name[room], by_name[name] + ':merge8']
        elif kind == 'bust-diff':
            pre = name[:-5]
            bast = (pre if pre != 'SA' else 'SA') + '_BAST'
            room = palette_base(name)
            specs = [by_name['WAKU'], by_name[room], by_name[bast] + ':merge8', by_name[name]]
        elif kind == 'diff' or name in ('BI06B', 'BI06C', 'BI06D', 'BI06E', 'FA09B', 'TO03B'):
            base = palette_base(name)
            if base in by_name:
                specs = ([by_name['WAKU'], by_name[base]] +
                         [by_name[p] for p in DIFF_CHAIN.get(name, [])] + [by_name[name]])
        if specs:
            composite(os.path.join(outdir, name + '.png'), specs)
            n += 1
    return n


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('files', nargs='+')
    ap.add_argument('-o', '--outdir', default='.')
    ap.add_argument('--index', help='TSV index path (default OUTDIR/index.tsv)')
    ap.add_argument('--composite', metavar='OUT.png', help='compose files onto one canvas')
    ap.add_argument('--composite-dir', metavar='DIR',
                    help='also write engine-style composites of all overlay images to DIR')
    a = ap.parse_args(argv)
    if a.composite:
        composite(a.composite, a.files)
        return
    rows = batch(a.files, a.outdir, a.index or os.path.join(a.outdir, 'index.tsv'))
    print('decoded %d files into %s' % (len(rows), a.outdir))
    if a.composite_dir:
        print('wrote %d composites into %s' % (composite_batch(a.files, a.composite_dir),
                                              a.composite_dir))


if __name__ == '__main__':
    sys.exit(main())
