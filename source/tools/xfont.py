#!/usr/bin/env python3
"""Convert bitmap fonts (BDF or PCF, optionally gzipped) to the engine's glyph table format.

usage: xfont.py [--no-bold] [--ags AGS.EXE] -o OUT.xfnt FONT [FONT...]

FONT is a BDF or PCF file, or a text-art file (.txt: blocks of "U+XXXX <char>" followed by
cell_h rows of '.'/'#', un-emboldened, already placed in the cell; '#' comment lines).

XFNT format (little-endian):
  "XFNT" u8 version=1, u8 cell_w (8|16), u8 cell_h (16), u8 flags (bit0: emboldened),
  u16 count, u16 reserved, then count x u16 code (sorted), then count glyphs of
  cell_h * cell_w/8 bytes (rows top to bottom, MSB = leftmost pixel).
Codes: 16-wide fonts use the JIS X 0208 code (0x2121..), 8-wide fonts the JIS X 0201 byte.

All input fonts must have the same cell size; later fonts override earlier codes.
--ags adds the engine's user-defined character JIS 7621h (the page-overflow "return" glyph,
docs/re/font.md §4) from AGS.EXE DS:09B2 to a 16-wide table.
The glyphs are emboldened as the engine does (font.md §3): 16-wide `r | r<<1` keeping 1-0-1
gaps clear, 8-wide `r | r<<1`. That bold extends to the left and the PC-98 ROM glyphs leave
column 0 clear for it; X11 fonts are aligned at column 0, so for them (--bold auto/right) the
bold is mirrored (`r | r>>1`), which equals shifting the glyph into the ROM layout first.

Tables used by the pack (the X11 bitmap fonts in fonts/x11, licences in fonts/x11/README.md):
  xfont.py -o ../build/fonts/ank8.xfnt fonts/x11/8x16rk.bdf
  xfont.py --ags ../resources/original/game/AGS.EXE -o ../build/fonts/k16.xfnt \
           fonts/x11/jiskan16.bdf
  xfont.py -o ../build/fonts/lat8.xfnt fonts/x11/8x16.bdf fonts/latin8-pl.txt
The last one is the same Sony design as 8x16rk (ASCII identical except 5Ch, which is the yen sign
in 8x16rk) in ISO 8859-1, so its codes are Unicode code points; mkpack.py takes the narrow
glyphs of UTF-8 languages from it; fonts/latin8-pl.txt adds the Polish letters in that design.
"""
import argparse
import gzip
import struct
import sys


def read_any(path):
    data = open(path, 'rb').read()
    if data[:2] == b'\x1f\x8b':
        data = gzip.decompress(data)
    return data


# ---- XFNT tables -------------------------------------------------------------------------------
def write_xfnt(cell_w, cell_h, bold, table):
    """table: {code: [row ints]} -> XFNT bytes"""
    codes = sorted(table)
    out = bytearray(b'XFNT')
    out += struct.pack('<BBBBHH', 1, cell_w, cell_h, 1 if bold else 0, len(codes), 0)
    for c in codes:
        out += struct.pack('<H', c)
    for c in codes:
        for r in table[c]:
            out += r.to_bytes(cell_w // 8, 'big')
    return bytes(out)


def read_xfnt(data):
    """XFNT bytes -> (cell_w, cell_h, bold, {code: [row ints]})"""
    if data[:4] != b'XFNT':
        raise ValueError('not an XFNT table')
    _, cell_w, cell_h, flags, count, _ = struct.unpack_from('<BBBBHH', data, 4)
    codes = struct.unpack_from('<%dH' % count, data, 12)
    wb = cell_w // 8
    g = 12 + 2 * count
    table = {}
    for i, c in enumerate(codes):
        base = g + i * cell_h * wb
        table[c] = [int.from_bytes(data[base + r * wb:base + (r + 1) * wb], 'big') for r in range(cell_h)]
    return cell_w, cell_h, bool(flags & 1), table


# ---- text art ----------------------------------------------------------------------------
def parse_art(data):
    """{code: [row ints]}, cell width from the row length."""
    glyphs = {}
    code = None
    width = None
    for ln in data.decode('utf-8').splitlines():
        if not ln.strip():
            code = None                        # a blank line ends a glyph block
            continue
        if code is None and ln.startswith('#'):
            continue                           # comment (only outside glyph blocks)
        if ln.startswith('U+'):
            code = int(ln.split()[0][2:], 16)
            glyphs[code] = []
            continue
        if code is None or set(ln) - set('.#'):
            raise ValueError('bad art line: %r' % ln)
        width = len(ln)
        glyphs[code].append(int(ln.replace('.', '0').replace('#', '1'), 2))
    return width, glyphs


# ---- BDF ------------------------------------------------------------------------------------
def parse_bdf(data):
    glyphs = {}
    lines = data.decode('latin-1').splitlines()
    i = 0
    w = h = None
    while i < len(lines):
        ln = lines[i]
        if ln.startswith('FONTBOUNDINGBOX'):
            _, bw, bh, _, _ = ln.split()[:5]
            w, h = int(bw), int(bh)
        if ln.startswith('STARTCHAR'):
            enc = None
            bbx = None
            rows = []
            i += 1
            while not lines[i].startswith('ENDCHAR'):
                t = lines[i]
                if t.startswith('ENCODING'):
                    enc = int(t.split()[1])
                elif t.startswith('BBX'):
                    bbx = [int(v) for v in t.split()[1:5]]
                elif t.startswith('BITMAP'):
                    i += 1
                    while not lines[i].startswith('ENDCHAR'):
                        rows.append(int(lines[i].strip(), 16))
                        i += 1
                    continue
                i += 1
            if enc is not None and enc >= 0 and bbx:
                glyphs[enc] = (bbx, rows)
        i += 1
    return w, h, glyphs


# ---- PCF ------------------------------------------------------------------------------------
PCF_METRICS, PCF_BITMAPS, PCF_ENCODINGS = 4, 8, 0x20
PCF_COMPRESSED_METRICS = 0x100


def parse_pcf(data):
    if data[:4] != b'\x01fcp':
        raise ValueError('not a PCF file')
    ntab, = struct.unpack_from('<I', data, 4)
    tables = {}
    for k in range(ntab):
        typ, fmt, size, off = struct.unpack_from('<IIII', data, 8 + 16 * k)
        tables[typ] = (fmt, size, off)

    def order(fmt):
        return '>' if fmt & 4 else '<'

    # metrics
    fmt, size, off = tables[PCF_METRICS]
    o = order(fmt)
    p = off + 4
    metrics = []
    if fmt & PCF_COMPRESSED_METRICS:
        n, = struct.unpack_from(o + 'H', data, p)
        p += 2
        for k in range(n):
            vals = [v - 0x80 for v in data[p:p + 5]]
            metrics.append(vals)      # lsb, rsb, width, ascent, descent
            p += 5
    else:
        n, = struct.unpack_from(o + 'I', data, p)
        p += 4
        for k in range(n):
            metrics.append(list(struct.unpack_from(o + '5h', data, p)))
            p += 12
    # bitmaps
    fmt, size, off = tables[PCF_BITMAPS]
    o = order(fmt)
    count, = struct.unpack_from(o + 'I', data, off + 4)
    offsets = struct.unpack_from(o + '%dI' % count, data, off + 8)
    sizes = struct.unpack_from(o + '4I', data, off + 8 + 4 * count)
    bitmaps_off = off + 8 + 4 * count + 16
    pad = 1 << (fmt & 3)
    msb_first = bool(fmt & 8)
    unit = 1 << ((fmt >> 4) & 3)
    bitmaps = data[bitmaps_off:bitmaps_off + sizes[fmt & 3]]
    # encodings
    fmt, size, off = tables[PCF_ENCODINGS]
    o = order(fmt)
    min2, max2, min1, max1, default = struct.unpack_from(o + 'HHHHh', data, off + 4)
    p = off + 14
    glyphs = {}
    for b1 in range(min1, max1 + 1):
        for b2 in range(min2, max2 + 1):
            idx, = struct.unpack_from(o + 'H', data, p)
            p += 2
            if idx == 0xFFFF:
                continue
            lsb, rsb, width, ascent, descent = metrics[idx]
            h = ascent + descent
            rowbytes = ((width + 7) // 8 + pad - 1) // pad * pad
            gstart = offsets[idx]
            rows = []
            for r in range(h):
                rb = bytearray(bitmaps[gstart + r * rowbytes:gstart + (r + 1) * rowbytes])
                # bytes within a unit are stored in `o` order; swap to big-endian-per-unit
                if unit > 1 and o == '<':
                    for u in range(0, len(rb), unit):
                        rb[u:u + unit] = rb[u:u + unit][::-1]
                if not msb_first:
                    rb = bytearray(int('{:08b}'.format(b)[::-1], 2) for b in rb)
                v = int.from_bytes(rb[:(width + 7) // 8], 'big')
                rows.append(v)
            code = (b1 << 8) | b2
            glyphs[code] = ([width, h, lsb, -descent], rows)
    return glyphs


# ---- glyph normalisation ----------------------------------------------------------------------
def to_cell(bbx, rows, cell_w, cell_h, ascent):
    """Place a BDF glyph (BBX w h xoff yoff, rows as ints of width w) into a cell."""
    w, h, xoff, yoff = bbx
    out = [0] * cell_h
    top = ascent - (yoff + h)        # first row of the glyph in the cell
    for r, v in enumerate(rows):
        y = top + r
        if 0 <= y < cell_h:
            # BDF rows are padded to a byte boundary on the right
            padded = ((w + 7) // 8) * 8
            row = (v >> (padded - w)) & ((1 << w) - 1)
            row = (row << (cell_w - w - xoff)) if cell_w - w - xoff >= 0 else row >> (xoff + w - cell_w)
            out[y] |= row & ((1 << cell_w) - 1)
    return out


def bold16(r, right=False):
    gap = (r >> 1) & ~r & (r << 1)          # 1 0 1 patterns: the clear pixel stays clear
    if right:
        return ((r | (r >> 1)) & ~gap) & 0xFFFF
    return ((r | (r << 1)) & ~gap) & 0xFFFF


def bold8(r, right=False):
    if right:
        return (r | (r >> 1)) & 0xFF
    return (r | (r << 1)) & 0xFF


def uses_first_column(rows, cell_w):
    first = 1 << (cell_w - 1)
    return any(v & first for v in rows)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('fonts', nargs='+')
    ap.add_argument('-o', '--output', required=True)
    ap.add_argument('--no-bold', action='store_true')
    ap.add_argument('--ags', help='AGS.EXE: add user character 7621h from DS:09B2')
    ap.add_argument('--cell', type=int, help='force cell width (8 or 16)')
    ap.add_argument('--bold', choices=('auto', 'left', 'right'), default='auto',
                    help='bold direction: left = as the engine does it on ROM glyphs, right = '
                         'for fonts aligned at column 0; auto picks right when any glyph uses '
                         'column 0 (default)')
    a = ap.parse_args()

    table = {}
    cell_w = a.cell
    cell_h = 16
    for path in a.fonts:
        data = read_any(path)
        if path.endswith('.txt'):
            w, glyphs = parse_art(data)
            if cell_w is None:
                cell_w = w
            if w != cell_w or any(len(r) != cell_h for r in glyphs.values()):
                sys.exit('%s: glyphs must be %dx%d' % (path, cell_w, cell_h))
            table.update(glyphs)
            continue
        if data[:4] == b'\x01fcp':
            glyphs = parse_pcf(data)
            w = max(g[0][0] for g in glyphs.values())
            ascent = max(g[0][1] + g[0][3] for g in glyphs.values())    # h + yoff(-descent)
        else:
            w, h, glyphs = parse_bdf(data)
            ascent = None
            # FONT_ASCENT from properties
            for ln in data.decode('latin-1').splitlines():
                if ln.startswith('FONT_ASCENT'):
                    ascent = int(ln.split()[1])
                    break
            if ascent is None:
                ascent = 14
        if cell_w is None:
            cell_w = 16 if w > 8 else 8
        for code, (bbx, rows) in glyphs.items():
            table[code] = to_cell(bbx, rows, cell_w, cell_h, ascent)

    # Bold direction. The engine extends every pixel to the LEFT (font.md §3) and the PC-98 ROM
    # glyphs are laid out for that: column 0 is always clear (ANK ink in columns 1..7, kanji in
    # 1..15). X11 bitmap fonts are aligned at column 0, so a left bold would lose the bold copy
    # of every leftmost pixel and the left side of A, M, B... comes out one pixel thin. For such
    # fonts we embolden to the RIGHT instead, which is pixel-identical to moving the glyph into
    # the ROM layout (one column right) and applying the engine's left bold.
    if a.bold == 'auto':
        n_first = sum(1 for rows in table.values() if uses_first_column(rows, cell_w))
        right = n_first > 0
        print('bold: %d of %d glyphs use column 0 -> emboldening to the %s'
              % (n_first, len(table), 'right' if right else 'left'))
    else:
        right = a.bold == 'right'

    if not a.no_bold:
        for code in table:
            table[code] = [bold16(r, right) if cell_w == 16 else bold8(r, right)
                           for r in table[code]]

    if a.ags:
        exe = open(a.ags, 'rb').read()
        off = 0xE00 + 0x0F9F * 16 + 0x9B2 + 2          # DGROUP 0F9F, skip the 2-byte header
        rows = [struct.unpack_from('<H', exe, off + 2 * r)[0] for r in range(16)]
        # the rows are stored as two bytes, left byte first (big-endian pixel order)
        rows = [((v & 0xFF) << 8) | (v >> 8) for v in rows]
        # a genuine ROM-layout glyph: always the engine's own left bold
        table[0x7621] = rows if a.no_bold else [bold16(r) for r in rows]

    out = write_xfnt(cell_w, cell_h, not a.no_bold, table)
    open(a.output, 'wb').write(out)
    print('%s: %d glyphs, cell %dx%d, %d bytes' % (a.output, len(table), cell_w, cell_h, len(out)))


if __name__ == '__main__':
    main()
