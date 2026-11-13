#!/usr/bin/env python3
"""ADT animation player/exporter and eye-blink ("_M" strip) exporter for the AGS engine
(Xenon ~Mugen no Shitai~, PC-98).  Uses re-tools/gdt.py for the image codec.

ADT file layout (loader 0ED8:0078, little-endian):
  000  3    "ADT" (read, never checked)
  003  u16  frame count N (<= 120)
  005  120 x u16  byte size of each frame blob
  0F5  u16  loop flag (0 = play once; nonzero = loop forever from frame loop_start-1)
  0F7  u16  loop_start
  0F9  120 x u16  delay[i]: VSYNCs to wait before frame i is shown
  1E9  frame blobs, back to back; each is a complete DA1 image (no palette,
       flags 0x0F00 = 2-byte column codec) drawn straight into VRAM.

Playback (0ED8:0001): page = 1; for i in 0..N-1: wait delay[i] vsyncs, select
access page `page` (port A6h), decode frame i into it, display that page (port A4h),
page ^= 1.  Frames therefore alternate between the two VRAM pages, and each frame is
drawn over whatever that page held two frames earlier; the script puts the base CG on
both pages first.  Timing unit = one VSYNC of the 640x400 mode (~56.4 Hz).

Eye-blink strips (*_M.GDT, BAST_M0x): the script loads the strip into the off-screen
plane buffers (op 0x28) and starts the VSYNC-driven animator 0ED8:036F(sx,sy,dx,dy,w,h)
(op 0x2E, w in bytes).  Each tick of 0ED8:0270 copies frame k = rows sy+k*h.. of the
buffer to (dx,dy) on screen following a fixed timing table (DS:0B14).

Usage:
    adt.py play  NAME.ADT BASE.GDT [BASE2.GDT ...] -o OUTDIR   # one ADT
    adt.py blink STRIP.GDT sx sy dx dy w h BASE.GDT [...] -o OUT.gif
    adt.py batch GAMEDIR -o OUTDIR      # all ADTs + all CG blink strips used by scripts
Outputs: animated GIF (CG area 560x296 at 40,4 unless --full), per-frame PNGs for ADTs
(screen-composited, cropped to the union of the frame rectangles) and a frames.tsv
with timings.  Requires numpy and Pillow.
"""
import argparse
import os
import struct
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gdt  # noqa: E402

VSYNC_HZ = 56.42          # PC-98 400-line mode: 24.83 kHz / 440 lines
CG_BOX = (40, 4, 600, 300)
# DS:0B14: ticks to wait before each copy of the blink animator (state index 0..13,
# after 13 it wraps to 2).
BLINK_TABLE = [0, 1, 1, 6, 3, 300, 1, 6, 3, 3, 1, 6, 3, 300]


def parse_adt(data):
    count, = struct.unpack_from('<H', data, 3)
    sizes = struct.unpack_from('<120H', data, 5)
    loop, loop_start = struct.unpack_from('<HH', data, 0xF5)
    delays = struct.unpack_from('<120H', data, 0xF9)
    frames, o = [], 0x1E9
    for i in range(count):
        frames.append(data[o:o + sizes[i]])
        o += sizes[i]
    return dict(magic=data[:3], count=count, sizes=sizes[:count], loop=loop,
                loop_start=loop_start, delays=delays[:count + 1], frames=frames, end=o)


def base_planes(paths):
    """Decode a chain of GDTs into one buffer like G_LOAD/op 0x16; returns planes, palette."""
    planes, pal = gdt.new_planes(), None
    for p in paths:
        img = gdt.decode(open(p, 'rb').read(), planes)
        if img.palette:
            pal = img.palette
    return planes, pal


def play_adt(adt, planes, pal):
    """Simulate 0ED8:0001.  Returns list of (index_image, ticks_shown, frame_no)."""
    pages = [[bytearray(p) for p in planes], [bytearray(p) for p in planes]]
    shown = []
    page = 1
    for i, blob in enumerate(adt['frames']):
        gdt.decode(blob, pages[page])
        shown.append([gdt.planes_to_index(pages[page]), 0, i])
        page ^= 1
    for k in range(len(shown)):
        shown[k][1] = adt['delays'][k + 1] if k + 1 < len(adt['delays']) else 0
    return shown


def save_gif(path, frames, rgb, box):
    pal = [c for col in rgb for c in col] + [0] * (768 - 48)
    ims = []
    durs = []
    for idx, ticks in frames:
        crop = idx[box[1]:box[3], box[0]:box[2]]
        im = Image.fromarray(crop, 'P')
        im.putpalette(pal)
        ims.append(im)
        durs.append(max(20, int(round(ticks * 1000 / VSYNC_HZ))))
    ims[0].save(path, save_all=True, append_images=ims[1:], duration=durs, loop=0,
                optimize=False, disposal=1)


def export_adt(adt_path, bases, outdir, full=False):
    name = os.path.splitext(os.path.basename(adt_path))[0].upper()
    adt = parse_adt(open(adt_path, 'rb').read())
    planes, pal = base_planes(bases)
    rgb = gdt.pal4_to_rgb(pal)
    seq = play_adt(adt, planes, pal)
    d = os.path.join(outdir, name)
    os.makedirs(d, exist_ok=True)
    # union rect of all frames
    x0, y0, x1, y1 = 640, 400, 0, 0
    rows = []
    for i, blob in enumerate(adt['frames']):
        h = gdt.parse_header(blob)
        if h['w'] == 640 and h['h'] == 400:
            kind = 'pause (no-op frame)'
        else:
            kind = 'image'
            x0, y0 = min(x0, h['x']), min(y0, h['y'])
            x1, y1 = max(x1, h['x'] + h['w']), max(y1, h['y'] + h['h'])
        rows.append((i, h['x'], h['y'], h['w'], h['h'], '0x%04X' % h['flags'], len(blob),
                     adt['delays'][i], 1 - (i & 1), kind))
    base_idx = gdt.planes_to_index(planes)
    gdt.save_png(os.path.join(d, 'base.png'), base_idx[y0:y1, x0:x1], rgb)
    for idx, ticks, i in seq:
        gdt.save_png(os.path.join(d, '%02d.png' % i), idx[y0:y1, x0:x1], rgb)
    with open(os.path.join(d, 'frames.tsv'), 'w') as fo:
        fo.write('# %s: %d frames, loop=%d loop_start=%d, crop of PNGs = (%d,%d)-(%d,%d); '
                 'bases: %s\n' % (name, adt['count'], adt['loop'], adt['loop_start'],
                                  x0, y0, x1, y1, ' '.join(os.path.basename(b) for b in bases)))
        fo.write('frame\tx\ty\tw\th\tflags\tbytes\tdelay_before_vsync\tvram_page\tkind\n')
        for r in rows:
            fo.write('\t'.join(str(v) for v in r) + '\n')
    gif_frames = [(base_idx, max(adt['delays'][0], 30))] + \
                 [(idx, ticks if k + 1 < len(seq) else 90) for k, (idx, ticks, _) in enumerate(seq)]
    save_gif(os.path.join(outdir, name + '.gif'), gif_frames, rgb,
             (0, 0, 640, 400) if full or name == '0100B' else CG_BOX)
    return name, adt


def blink_sequence(nticks_limit=2000):
    """Simulate 0ED8:0270 from the 0ED8:03CB reset state.

    Returns [(frame_k, ticks_until_next_copy)] for one start-up pass plus one full period.
    """
    b30, b32, b34, b36 = 0, 0, 0, 0
    copies = []
    t = 0
    while t < nticks_limit and len(copies) < 2 + 12 * 2:
        t += 1
        if BLINK_TABLE[b32] > b34:
            b34 += 1
            continue
        copies.append((b30, t))
        b34 = 0
        b32 += 1
        if b32 > 13:
            b32 = 2
        b30 += 1 if b36 else -1
        if b30 > 2:
            b36, b30 = 0, 1
        elif b30 < 0:
            b36, b30 = 1, 1
    out = []
    for i, (k, tt) in enumerate(copies[:-1]):
        out.append((k, copies[i + 1][1] - tt))
    return out


def export_blink(strip, args, bases, out, full=False):
    sx, sy, dx, dy, w, h = args
    screen, pal = base_planes(bases)
    buf = [bytearray(p) for p in screen]
    gdt.decode(open(strip, 'rb').read(), buf)        # op 0x28: strip into the buffer only
    rgb = gdt.pal4_to_rgb(pal)
    frames = []
    for k, ticks in blink_sequence()[:14]:
        for r in range(h):
            s = (sy + k * h + r) * 80 + sx
            t = (dy + r) * 80 + dx
            for p in range(4):
                screen[p][t:t + w] = buf[p][s:s + w]
        frames.append((gdt.planes_to_index(screen), ticks))
    save_gif(out, frames, rgb, (0, 0, 640, 400) if full else CG_BOX)


# Blink strips with the script call arguments (op 0x2E) and the CG state they run on,
# extracted from the scenario scripts (src == dst cases; BAST_M01/M02 omitted because
# they depend on the bust placement).
BLINKS = {
    '1010B_M': ((43, 40, 43, 40, 10, 32), ['1010']),
    'BI03_M': ((33, 0, 33, 96, 24, 120), ['BI03']),
    'BI04_M': ((56, 48, 56, 48, 14, 112), ['BI04']),
    'BI06E_M': ((39, 72, 39, 72, 24, 56), ['BI06', 'BI06B', 'BI06C', 'BI06D', 'BI06E']),
    'FA02_M': ((50, 16, 50, 16, 14, 96), ['FA02']),
    'FA03_M': ((28, 24, 28, 24, 18, 64), ['FA03']),
    'MAI01_M': ((18, 48, 18, 48, 15, 80), ['MAI01']),
    'MAI04_M': ((32, 64, 32, 64, 16, 40), ['MAI04']),
    'MEG01_M': ((27, 56, 27, 56, 23, 72), ['MEG01']),
    'MEG06_M': ((33, 24, 33, 24, 18, 64), ['MEG06']),
    'RA01_M': ((15, 24, 15, 24, 19, 112), ['RA01']),
    'RA01B_M': ((15, 24, 15, 24, 19, 112), ['RA01', 'RA01B']),
    'RA02_M': ((8, 32, 8, 32, 11, 80), ['RA02']),
    'RA02B_M': ((8, 40, 8, 40, 9, 104), ['RA02', 'RA02B']),
    'RA02C_M': ((11, 48, 11, 48, 8, 104), ['RA02', 'RA02B', 'RA02C']),
    'RA06B_M': ((12, 24, 12, 24, 9, 72), ['RA06', 'RA06B']),
    'RA06C_M': ((12, 24, 12, 24, 9, 72), ['RA06', 'RA06C']),
    'RYO01_M': ((41, 64, 41, 64, 17, 104), ['RYO01']),
    'RYO01B_M': ((12, 40, 12, 40, 22, 88), ['RYO01B']),
    'RYO04_M': ((21, 56, 21, 56, 10, 104), ['RYO04']),
    'SAYA01_M': ((44, 32, 44, 32, 14, 88), ['SAYA01']),
    'SAYA03_M': ((50, 40, 50, 40, 13, 72), ['SAYA03']),
    'SAYA06BM': ((52, 32, 52, 32, 10, 40), ['SAYA06', 'SAYA06B']),
    'TO01_M': ((61, 8, 61, 8, 8, 72), ['TO01']),
    'TO02_M': ((15, 8, 15, 8, 14, 88), ['TO02']),
    'TO03_M': ((41, 8, 41, 8, 17, 88), ['TO03']),
    'TO05_M': ((58, 16, 58, 16, 9, 88), ['TO05']),
    'TO09_M': ((42, 24, 42, 24, 16, 64), ['TO09']),
    'TO09B_M': ((42, 24, 42, 24, 16, 64), ['TO09', 'TO09B']),
    'TO09C_M': ((42, 24, 42, 24, 16, 56), ['TO09', 'TO09B', 'TO09C']),
}
ADT_BASES = {'0100B': ['0100B'], 'FA03': ['WAKU', 'FA03'], 'FA08': ['WAKU', 'FA08'],
             'FA09': ['WAKU', 'FA09'], 'MEG05': ['WAKU', 'MEG05'], 'RA03': ['WAKU', 'RA03']}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('play')
    p.add_argument('adt')
    p.add_argument('bases', nargs='+')
    p.add_argument('-o', '--outdir', default='.')
    p.add_argument('--full', action='store_true')
    b = sub.add_parser('blink')
    b.add_argument('strip')
    b.add_argument('args', nargs=6, type=int)
    b.add_argument('bases', nargs='+')
    b.add_argument('-o', '--out', required=True)
    b.add_argument('--full', action='store_true')
    t = sub.add_parser('batch')
    t.add_argument('gamedir')
    t.add_argument('-o', '--outdir', default='.')
    a = ap.parse_args(argv)
    if a.cmd == 'play':
        export_adt(a.adt, a.bases, a.outdir, a.full)
    elif a.cmd == 'blink':
        export_blink(a.strip, a.args, a.bases, a.out, a.full)
    else:
        g = lambda n, ext='.GDT': os.path.join(a.gamedir, n + ext)  # noqa: E731
        os.makedirs(a.outdir, exist_ok=True)
        for name, bases in ADT_BASES.items():
            export_adt(g(name, '.ADT'), [g(x) for x in bases], a.outdir)
            print('ADT', name)
        bd = os.path.join(a.outdir, 'blink')
        os.makedirs(bd, exist_ok=True)
        for name, (args, bases) in BLINKS.items():
            export_blink(g(name), args, [g('WAKU')] + [g(x) for x in bases],
                         os.path.join(bd, name + '.gif'))
        print('blink strips', len(BLINKS))
        with open(os.path.join(bd, 'blink_timing.tsv'), 'w') as fo:
            fo.write('copy\tstrip_frame\tvsyncs_until_next_copy\n')
            for i, (k, ticks) in enumerate(blink_sequence()):
                fo.write('%d\t%d\t%d\n' % (i, k, ticks))


if __name__ == '__main__':
    sys.exit(main())
