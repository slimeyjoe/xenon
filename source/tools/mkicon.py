#!/usr/bin/env python3
"""Make the Workbench icon of the Amiga build: xenon.info, the XENON lettering of the
opening (resources/anim/0100B/28.png, the last frame of the logo zoom) on a transparent
background - the red glyphs cut out of the frame by colour, scaled to 64 px wide, with a
one-pixel black drop shadow; the ship and the sky behind them are left out. The frame is the
game's art and not committed: `make data` derives it from 0100B.ADT (tools/gamedata.py), and
the Makefile makes the icons from it at build time (the same bytes from any import).

usage: mkicon.py [-o xenon.info] [--flavour os3|aros] [--width 64] [--squash] [--stack N] [--no-shadow]
                 [--tooltype KEY=VALUE ...] [--drawer] [--preview DIR] [--check FILE.info]
                 [FRAME.png]

--drawer makes the drawer icon of the game's directory instead (same picture, no tool
types, a DrawerData window like the system drawers'); it is installed as <directory>.info.

The file carries two renderings of the same glyphs:
  - a classic 3-plane Image for icon.library 37-40 (OS 2.x/3.1), complemented when selected:
    the lettering in pen 7, the shadow in pen 1 (black), everything else pen 0, which is
    the Workbench backdrop. Pen 7 is orange on a stock 8-colour Workbench 3.1 (pens 4-7 =
    red, green, blue, orange, measured with probes/amiga/run-icon.sh), salmon under a
    MagicWB palette, and a 4-colour Workbench, which drops the third plane, shows it blue.
  - an OS 3.5 "FORM ICON" (FACE + two IMAG chunks, run-length coded like icon.library 44
    does it), frameless, colour 0 transparent, the lettering in four reds shaded top to
    bottom, a brighter copy for the selected state. icon.library 44 and later (OS 3.5/3.9,
    3.2, 4.x, AROS, MorphOS) shows this one and 3.1 ignores it, so one file serves every
    Workbench.
Tool types are the game's options (backends/amiga/README.md) at their defaults; the ones
in parentheses are switched off, the way the Information window does it.
--squash halves the height for a Workbench on a non-interlaced HiRes screen (tall pixels);
--preview writes enlarged PNGs of every rendering; --check decodes an existing .info file
(ours or anybody's) and prints what it holds, which is how the writer was validated against
icons made by other tools.
"""
import argparse
import os
import struct

from PIL import Image

FRAME = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'resources', 'anim',
                     '0100B', '28.png')

# the options the Amiga backend reads from the icon (parse_tooltypes in backends/amiga/core/main.c)
# at their defaults, per flavour (backends/amiga/README.md "Layout"; --flavour)
DEFAULT_TOOLTYPES = [
    'AUDIO=paula',
    'RATE=11025',
    'AUDIOCPU=75',
    'OPN=lite',
    'PRELOAD=auto',
    'MODE=0x8004',
    '(LANG=en)',
    '(PAK=PROGDIR:xenon.pak)',
    '(AUDIOBUF=1024)',
    '(AHIUNIT=0)',
    '(PAULA14)',
    '(VOICEPAK=PROGDIR:xenon_voice.pak)',
    '(NOVOICE)',
    '(VOICEVOL=100)',
    '(MUSICVOL=50)',
    '(VOICESTRICT)',
    '(VOICEMIX)',
    '(QUIET)',
    '(WINDOW)',
    '(SCALE=1)',
    '(ASPECT)',
]
# AROS: AHI, 44.1 kHz, the best 640x400 mode (no MODE), the chunky upload; 256 KB of stack, like
# the shell's (hosted AROS's X11 driver crashed with 64 KB; the program also swaps itself onto
# 256 KB, backends/amiga/aros/stack_aros.c)
AROS_TOOLTYPES = [
    'AUDIO=ahi',
    'RATE=44100',
    'AUDIOCPU=75',
    'OPN=lite',
    'PRELOAD=auto',
    '(MODE=0x00000000)',
    '(UPLOAD=chunky)',
    '(LANG=en)',
    '(PAK=PROGDIR:xenon.pak)',
    '(AUDIOBUF=4096)',
    '(AHIUNIT=0)',
    '(VOICEPAK=PROGDIR:xenon_voice.pak)',
    '(NOVOICE)',
    '(VOICEVOL=100)',
    '(MUSICVOL=50)',
    '(VOICESTRICT)',
    '(VOICEMIX)',
    '(QUIET)',
    '(WINDOW)',
    '(SCALE=1)',
    '(ASPECT)',
]
FLAVOURS = {'os3': (DEFAULT_TOOLTYPES, 8192), 'aros': (AROS_TOOLTYPES, 262144)}

# the default Workbench 3.1 palette (IPrefs without Palette prefs, 8 pens) for the previews;
# pens 0-3 are the 4-colour set
WB_PENS = [(0xaa, 0xaa, 0xaa), (0x00, 0x00, 0x00), (0xff, 0xff, 0xff), (0x66, 0x88, 0xbb),
           (0xee, 0x44, 0x44), (0x55, 0xdd, 0x55), (0x00, 0x44, 0xdd), (0xee, 0x99, 0x00)]
# the same pens under MagicWB (dark grey, light grey, brown, salmon), the other common case
MWB_PENS = WB_PENS[:4] + [(0x7b, 0x7b, 0x7b), (0xaf, 0xaf, 0xaf), (0xaa, 0x90, 0x7c), (0xff, 0xa9, 0x97)]

WBDRAWER = 2
WBTOOL = 3
NO_ICON_POSITION = -0x80000000
GFLG_GADGIMAGE = 0x0004
GACT_RELVERIFY = 0x0001
GTYP_BOOLGADGET = 0x0001
WB_DISKREVISION = 1
IMAGE35F_HASTRANSPARENTCOLOR = 1
IMAGE35F_HASPALETTE = 2
ICON35F_FRAMELESS = 1


# ---- the picture ------------------------------------------------------------------------------

TRANSPARENT, LETTER, SHADOW = 0, 1, 2

# the lettering of the OS 3.5 image, top row to bottom row (the frame's reds, a little
# lighter at the top), plus its shadow; index 0 of the palette is the transparent colour
LETTER_SHADES = [(0xff, 0x55, 0x33), (0xee, 0x33, 0x22), (0xdd, 0x2a, 0x1c), (0xbb, 0x22, 0x18)]
SHADOW_RGB = (0x00, 0x00, 0x00)


def is_letter(rgb):
    """The XENON lettering of the frame is the only strongly red thing in it (the planet's
    glow is orange: green at half the red or more)."""
    r, g, b = rgb
    return r >= 0x70 and g < 0.45 * r and b < 0.6 * r


def letter_mask(path, width, squash):
    """The lettering cut out of the frame and scaled: a list of rows of 0..255 coverage."""
    im = Image.open(path).convert('RGB')
    w, h = im.size
    height = max(1, round(h * width / w))
    if squash:
        height = max(1, height // 2)
    raw = im.tobytes()
    mask = Image.frombytes('L', (w, h), bytes(255 if is_letter(raw[i:i + 3]) else 0
                                              for i in range(0, len(raw), 3)))
    # trim to the lettering, then scale by area averaging
    box = mask.getbbox()
    mask = mask.crop(box)
    cw, ch = mask.size
    height = max(1, round(ch * width / cw)) // (2 if squash else 1)
    mask = mask.resize((width, height), Image.BOX)
    px = mask.load()
    return [[px[x, y] for x in range(width)] for y in range(height)]


def glyph_rows(coverage, shadow=True):
    """Rows of TRANSPARENT / LETTER / SHADOW: a pixel is lettering when the glyphs cover
    at least half of it; a one-pixel drop shadow to the lower right keeps the letters
    readable on any background (it never fills a gap between letters wider than 1 px)."""
    h, w = len(coverage), len(coverage[0])
    rows = [[LETTER if coverage[y][x] >= 128 else TRANSPARENT for x in range(w)] for y in range(h)]
    if shadow:
        rows.append([TRANSPARENT] * w)
        for y in range(h):
            for x in range(w):
                if rows[y][x] == LETTER and rows[y + 1][x] == TRANSPARENT:
                    rows[y + 1][x] = SHADOW
                if rows[y][x] == LETTER and x + 1 < w and rows[y][x + 1] == TRANSPARENT:
                    rows[y][x + 1] = SHADOW
    return rows


def classic_indices(rows):
    """Workbench pens: the lettering pen 7 (orange on a stock 8-colour 3.1, salmon under
    MagicWB, blue on 4 colours), the shadow pen 1 (black), the rest pen 0 = the backdrop."""
    return [[{TRANSPARENT: 0, LETTER: 7, SHADOW: 1}[v] for v in r] for r in rows]


def color_image(rows):
    """(indices, palette) of the OS 3.5 image: index 0 transparent, the lettering shaded
    by row, the shadow last."""
    h = len(rows)
    palette = [(0, 0, 0)] + LETTER_SHADES + [SHADOW_RGB]
    out = []
    for y, r in enumerate(rows):
        shade = 1 + min(len(LETTER_SHADES) - 1, y * len(LETTER_SHADES) // max(1, h - 1))
        out.append([shade if v == LETTER else len(palette) - 1 if v == SHADOW else 0 for v in r])
    return out, palette


def planar(rows, w, h, depth):
    bpr = ((w + 15) // 16) * 2
    out = bytearray()
    for p in range(depth):
        for y in range(h):
            row = bytearray(bpr)
            for x in range(w):
                if (rows[y][x] >> p) & 1:
                    row[x >> 3] |= 0x80 >> (x & 7)
            out += row
    return bytes(out)


def lighter(palette, amount=0.35):
    return [palette[0]] + [tuple(int(c + (255 - c) * amount) for c in rgb) for rgb in palette[1:]]


# ---- OS 3.5 run-length coding (Encode35/Decode35 of icon.library, after Dirk Stoecker) -------

def encode35(src, depth):
    """Returns (format, data): format 1 = run-length coded bit stream of `depth`-bit
    entries, 0 = stored (when that is shorter). A literal port of Encode35."""
    size = len(src)
    buf = bytearray()
    numcopy, numequal, bitbuf, numbits, k, i = 0, 1, 0, 0, 0, 1
    while numequal or numcopy:
        if i < size and numequal and src[i - 1] == src[i]:
            numequal += 1
            i += 1
        elif i < size and numequal * depth <= 16:
            numcopy += numequal
            numequal = 1
            i += 1
        else:
            if i == size and numcopy + numequal <= 128 and (numequal - 1) * depth <= 8:
                numcopy += numequal
                numequal = 0
            if numcopy:
                j = min(numcopy, 128)
                bitbuf = (bitbuf << 8) | (j - 1)
                numcopy -= j
            else:
                j = min(numequal, 128)
                bitbuf = (bitbuf << 8) | ((256 - (j - 1)) & 0xff)
                numequal -= j
                k += j - 1
                j = 1
            buf.append((bitbuf >> numbits) & 0xff)
            while j:
                j -= 1
                numbits += depth
                bitbuf = (bitbuf << depth) | src[k]
                k += 1
                if numbits >= 8:
                    numbits -= 8
                    buf.append((bitbuf >> numbits) & 0xff)
            if i < size and not numcopy and not numequal:
                numequal = 1
                i += 1
    if numbits:
        buf.append((bitbuf << (8 - numbits)) & 0xff)
    if len(buf) > size:
        return 0, bytes(src)
    return 1, bytes(buf)


def decode35(buf, bits, entries):
    out = []
    cop = loop = numbits = bitbuf = pos = 0
    numbytes = len(buf)
    mask = (1 << bits) - 1
    while (numbytes or (cop and numbits >= bits)) and len(out) < entries:
        if not cop:
            if numbits < 8:
                bitbuf = (bitbuf << 8) | buf[pos]
                pos += 1
                numbytes -= 1
                numbits += 8
            byte = (bitbuf >> (numbits - 8)) & 0xff
            numbits -= 8
            if byte <= 127:
                cop = byte + 1
            elif byte > 128:
                loop = 256 - byte + 1
            else:
                continue
        if cop:
            loop += 1
        if numbits < bits:
            bitbuf = (bitbuf << 8) | buf[pos]
            pos += 1
            numbytes -= 1
            numbits += 8
        byte = (bitbuf >> (numbits - bits)) & mask
        while loop and len(out) < entries:
            out.append(byte)
            loop -= 1
        if cop:
            cop -= 1
        numbits -= bits
    if len(out) != entries:
        raise ValueError('run-length data ends after %d of %d entries' % (len(out), entries))
    return out


# ---- the file -------------------------------------------------------------------------------

def chunk(cid, data):
    return cid + struct.pack('>I', len(data)) + data + (b'\0' if len(data) & 1 else b'')


def imag_chunk(rows, palette, depth, transparent=None):
    flat = bytes(v for row in rows for v in row)
    ifmt, idata = encode35(flat, depth)
    pfmt, pdata = encode35(bytes(c for rgb in palette for c in rgb), 8)
    assert decode35(idata, depth, len(flat)) == list(flat) if ifmt else True
    flags = IMAGE35F_HASPALETTE | (IMAGE35F_HASTRANSPARENTCOLOR if transparent is not None else 0)
    hdr = struct.pack('>BBBBBBHH', transparent or 0, len(palette) - 1, flags, ifmt, pfmt, depth,
                      len(idata) - 1, len(pdata) - 1)
    return chunk(b'IMAG', hdr + idata + pdata)


def drawer_data():
    """struct DrawerData of a drawer icon, the way Workbench 3.1's own drawers have it: a
    window of 300x120 at 100,50 on the Workbench screen (NewWindow flags as in the system
    icons), the drawer's own icon coordinates at the origin. The six bytes of flags and
    view modes that OS 2.0+ keeps after the tool types (default view) are added by build()."""
    newwindow = struct.pack('>hhhhBBIIIIIIIhhHHH', 100, 50, 300, 120, 255, 255, 0, 0x200127f,
                            0, 0, 0, 0, 0, 96, 64, 0xffff, 0xffff, 1)
    return newwindow + struct.pack('>ii', 0, 0)


def build(glyphs, tooltypes, stack, drawer=False):
    h, w = len(glyphs), len(glyphs[0])
    # classic part: DiskObject, [DrawerData], one 3-plane Image (complement highlight), the
    # tool types, [DrawerData2]
    rows = classic_indices(glyphs)
    depth = 3
    image = struct.pack('>hhhhhIBBI', 0, 0, w, h, depth, 1, (1 << depth) - 1, 0, 0) \
        + planar(rows, w, h, depth)
    gadget = struct.pack('>IhhhhHHHIIIiIhI', 0, 0, 0, w, h, GFLG_GADGIMAGE, GACT_RELVERIFY,
                         GTYP_BOOLGADGET, 1, 0, 0, 0, 0, 0, WB_DISKREVISION)
    head = struct.pack('>HH', 0xe310, 1) + gadget \
        + struct.pack('>BBIIiiIIi', WBDRAWER if drawer else WBTOOL, 0, 0, 1, NO_ICON_POSITION,
                      NO_ICON_POSITION, 1 if drawer else 0, 0, 0 if drawer else stack)
    tt = struct.pack('>I', (len(tooltypes) + 1) * 4)
    for t in tooltypes:
        s = t.encode('latin-1') + b'\0'
        tt += struct.pack('>I', len(s)) + s
    classic = head + (drawer_data() if drawer else b'') + image + tt \
        + (struct.pack('>IH', 0, 0) if drawer else b'')
    # OS 3.5 part: FORM ICON { FACE (frameless), IMAG normal, IMAG selected }, colour 0 transparent
    crows, cpal = color_image(glyphs)
    cdepth = max(1, (len(cpal) - 1).bit_length())
    face = chunk(b'FACE', struct.pack('>BBBBH', w - 1, h - 1, ICON35F_FRAMELESS, 0, len(cpal) * 3 - 1))
    form = face + imag_chunk(crows, cpal, cdepth, 0) + imag_chunk(crows, lighter(cpal), cdepth, 0)
    iff = b'FORM' + struct.pack('>I', 4 + len(form)) + b'ICON' + form
    return classic + iff, rows, (crows, cpal)


# ---- reading back (--check) -----------------------------------------------------------------

def parse(d):
    """Decodes a .info file: dict with the DiskObject fields, the classic images as
    (w, h, depth, rows), the tool types and the OS 3.5 images as (w, h, rows, palette)."""
    if d[:2] != b'\xe3\x10':
        raise ValueError('not an icon file')
    g = struct.unpack('>IhhhhHHHIIIiIhI', d[4:48])
    typ, _, deftool, tts, cx, cy, drawer, toolwin, stack = struct.unpack('>BBIIiiIIi', d[48:78])
    r = {'width': g[3], 'height': g[4], 'flags': g[5], 'type': typ, 'stack': stack,
         'position': (cx, cy), 'images': [], 'tooltypes': [], 'os35': [], 'default_tool': None}
    pos = 78
    if drawer:
        pos += 56
    for ptr in (g[8], g[9]):
        if not ptr:
            continue
        _, _, w, h, depth, _, _, _, _ = struct.unpack('>hhhhhIBBI', d[pos:pos + 20])
        pos += 20
        bpr = ((w + 15) // 16) * 2
        rows = [[0] * w for _ in range(h)]
        for p in range(depth):
            for y in range(h):
                line = d[pos:pos + bpr]
                pos += bpr
                for x in range(w):
                    if line[x >> 3] & (0x80 >> (x & 7)):
                        rows[y][x] |= 1 << p
        r['images'].append((w, h, depth, rows))
    if deftool:
        n, = struct.unpack('>I', d[pos:pos + 4])
        r['default_tool'] = d[pos + 4:pos + 4 + n].rstrip(b'\0').decode('latin-1')
        pos += 4 + n
    if tts:
        n, = struct.unpack('>I', d[pos:pos + 4])
        pos += 4
        for _ in range(n // 4 - 1):
            ln, = struct.unpack('>I', d[pos:pos + 4])
            r['tooltypes'].append(d[pos + 4:pos + 4 + ln].rstrip(b'\0').decode('latin-1'))
            pos += 4 + ln
    if toolwin:
        n, = struct.unpack('>I', d[pos:pos + 4])
        pos += 4 + n
    if drawer and g[14] == WB_DISKREVISION:
        r['drawer_flags'] = struct.unpack('>IH', d[pos:pos + 6])      # dd_Flags, dd_ViewModes
        pos += 6
    r['classic_size'] = pos
    if d[pos:pos + 4] == b'FORM' and d[pos + 8:pos + 12] == b'ICON':
        end = pos + 8 + struct.unpack('>I', d[pos + 4:pos + 8])[0]
        p = pos + 12
        face = None
        while p + 8 <= end:
            cid = d[p:p + 4]
            ln, = struct.unpack('>I', d[p + 4:p + 8])
            body = d[p + 8:p + 8 + ln]
            if cid == b'FACE':
                face = struct.unpack('>BBBBH', body[:6])
            elif cid == b'IMAG' and face:
                w, h = face[0] + 1, face[1] + 1
                transp, ncol, flags, ifmt, pfmt, depth, isz, psz = struct.unpack('>BBBBBBHH', body[:10])
                idata = body[10:10 + isz + 1]
                pdata = body[10 + isz + 1:10 + isz + 1 + psz + 1]
                flat = list(idata[:w * h]) if ifmt == 0 else decode35(idata, depth, w * h)
                pal = None
                if flags & IMAGE35F_HASPALETTE:
                    pb = list(pdata[:3 * (ncol + 1)]) if pfmt == 0 else decode35(pdata, 8, 3 * (ncol + 1))
                    pal = [tuple(pb[3 * i:3 * i + 3]) for i in range(ncol + 1)]
                rows = [flat[y * w:(y + 1) * w] for y in range(h)]
                r['os35'].append((w, h, rows, pal, {'transparent': transp if flags & 1 else None,
                                                    'format': (ifmt, pfmt), 'depth': depth}))
            p += 8 + ln + (ln & 1)
    return r


def to_png(rows, palette, path, zoom=4):
    h, w = len(rows), len(rows[0])
    im = Image.new('RGB', (w, h))
    px = im.load()
    for y in range(h):
        for x in range(w):
            px[x, y] = palette[rows[y][x]]
    im.resize((w * zoom, h * zoom), Image.NEAREST).save(path)


def check(path, preview):
    d = open(path, 'rb').read()
    r = parse(d)
    print('%s: %d bytes, type %d, %dx%d, stack %d, position %s, classic part %d bytes%s' %
          (path, len(d), r['type'], r['width'], r['height'], r['stack'], r['position'], r['classic_size'],
           ', drawer flags/view %s' % (r['drawer_flags'],) if 'drawer_flags' in r else ''))
    for i, (w, h, depth, rows) in enumerate(r['images']):
        print('  classic image %d: %dx%d, %d planes' % (i, w, h, depth))
        if preview:
            to_png(rows, (WB_PENS * 32)[:1 << depth], '%s/check-classic%d.png' % (preview, i))
    for i, (w, h, rows, pal, info) in enumerate(r['os35']):
        print('  OS 3.5 image %d: %dx%d, %s colours, depth %d, formats %s, transparent %s' %
              (i, w, h, len(pal) if pal else 'no palette', info['depth'], info['format'], info['transparent']))
        if preview and pal:
            to_png(rows, pal, '%s/check-os35-%d.png' % (preview, i))
    print('  default tool:', r['default_tool'])
    print('  tool types:', r['tooltypes'])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('frame', nargs='?', default=FRAME)
    ap.add_argument('-o', '--output', default='xenon.info')
    ap.add_argument('--width', type=int, default=64)
    ap.add_argument('--squash', action='store_true', help='half height (HiRes non-interlaced Workbench)')
    ap.add_argument('--flavour', choices=sorted(FLAVOURS), default='os3',
                    help='default tool types and stack: os3 (also WarpOS) or aros')
    ap.add_argument('--stack', type=int, default=None,
                    help='do_StackSize (default: the flavour\'s; the 68k game uses ~2.4 KB)')
    ap.add_argument('--no-shadow', action='store_true', help='no drop shadow under the letters')
    ap.add_argument('--drawer', action='store_true',
                    help='a drawer icon (for the game directory, saved as <dir>.info) instead of '
                         'the tool icon: same picture, no tool types')
    ap.add_argument('--tooltype', action='append', default=[], metavar='KEY=VALUE',
                    help='replace a default tool type (same KEY, also in parentheses) or add one')
    ap.add_argument('--preview', metavar='DIR', help='write enlarged PNGs of the renderings there')
    ap.add_argument('--check', metavar='FILE', help='decode an .info file and describe it (no output)')
    a = ap.parse_args()

    if a.check:
        check(a.check, a.preview)
        return

    def key(t):
        return t.lstrip('(').split('=', 1)[0].rstrip(')').upper()
    flav_tooltypes, flav_stack = FLAVOURS[a.flavour]
    if a.stack is None:
        a.stack = flav_stack
    tooltypes = [] if a.drawer else list(flav_tooltypes)
    for t in a.tooltype:
        for i, o in enumerate(tooltypes):
            if key(o) == key(t):
                tooltypes[i] = t
                break
        else:
            tooltypes.append(t)

    glyphs = glyph_rows(letter_mask(a.frame, a.width, a.squash), not a.no_shadow)
    data, rows, (crows, cpal) = build(glyphs, tooltypes, a.stack, a.drawer)
    with open(a.output, 'wb') as f:
        f.write(data)
    back = parse(data)       # the file must read back as written
    assert back['tooltypes'] == tooltypes and back['images'][0][3] == rows
    assert back['os35'][0][2] == crows and back['os35'][0][3] == cpal
    assert back['type'] == (WBDRAWER if a.drawer else WBTOOL)
    print('%s: %d bytes, %s icon %dx%d, %d colours in the OS 3.5 image, %d tool types' %
          (a.output, len(data), 'drawer' if a.drawer else 'tool', len(glyphs[0]), len(glyphs),
           len(cpal), len(tooltypes)))
    if a.preview:
        to_png(rows, WB_PENS, a.preview + '/classic8.png')
        to_png(rows, MWB_PENS, a.preview + '/classic8-mwb.png')
        to_png([[p & 3 for p in r] for r in rows], WB_PENS[:4], a.preview + '/classic4.png')
        grey = [(0xaa, 0xaa, 0xaa)] + cpal[1:]     # the transparent colour as a grey backdrop
        to_png(crows, grey, a.preview + '/os35.png')
        to_png(crows, lighter(grey), a.preview + '/os35-selected.png')


if __name__ == '__main__':
    main()
