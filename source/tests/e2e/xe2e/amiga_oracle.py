"""Presentation oracle for the Amiga backend in WinUAE (plan §10).

Every snapshot (backends/amiga/snap.c, served by amiga_snapd.py) is checked three ways
against the reference frame the null backend computed for the same snapshot:

  * hash: the guest's frame hash (backends/common/ptest.h) of what it presented must equal
    the reference's. A mismatch means the 68k build computed a different frame (engine or
    test-generator divergence), which is reported separately from presentation errors;
  * display: the WinUAE screenshot, cropped at the calibrated position of the 640x400 image,
    must equal palette[index]*17 everywhere (black with graphics off), and every pixel outside
    the image must be black (native overscan border; board rows below a 400-line screen);
  * bitmap: the displayed screen buffer read from guest memory (planar on native, chunky on a
    graphics card, where engine colour i is pen penbase+i) must hold the frame's colour
    indices, rotated by the scroll line.

The image position is calibrated per configuration from the present test's registration
snapshot (a one-pixel frame of colour 1 on black, colour 0 black): the bounding box of the lit
pixels must be exactly 640x400.

A SCALE/ASPECT screen (the request carries sx and dsth) shows the page sx times as wide and
dsth rows tall through the row map of engine/gfx/scale.h; the expected screenshot and bitmap
are the reference expanded the same way (expand() below, checked against test_scale's map).

A window on a public screen (window=1) is found at the screen's position plus the window's
inside (dx, dy); around it is the Workbench, not checked. Its bitmap holds the window's pens
(pens=), mapped back to colour indices; on a 15/16-bit screen the screenshot shows each
colour rounded to the screen's format, so it may differ by up to 8 per channel. The pointer
stays visible over a window: a box at its position (mouse=) is not compared.
"""
import json
import os

import numpy as np
from PIL import Image

W, H = 640, 400


def load_rgb(path):
    return np.array(Image.open(path).convert('RGB'))


def scale_of(rec):
    """(sx, dst_h) of a snapshot record: (1, 400) on the plain 640x400 screen."""
    if 'sx' in rec:
        return int(rec['sx']), int(rec['dsth'])
    return 1, H


def source_rows(dst_h):
    """For each destination row, the page row shown there (engine/gfx/scale.c xe_rowmap_init:
    page row r covers rows r * dst_h // 400 .. (r + 1) * dst_h // 400 - 1)."""
    start = np.array([r * dst_h // H for r in range(H + 1)])
    return np.searchsorted(start, np.arange(dst_h), side='right') - 1


def expand(a, sx, dst_h):
    """A (400, 640[, 3]) array as the scaled screen shows it."""
    if sx == 1 and dst_h == H:
        return a
    return np.repeat(a, sx, axis=1)[source_rows(dst_h)]


def calibrate(shot, w=W, h=H):
    """(x, y) of the w x h image in a registration screenshot, or raises ValueError."""
    lit = (shot != 0).any(axis=2)
    ys, xs = np.nonzero(lit)
    if not len(xs):
        raise ValueError('registration screenshot is black')
    x0, x1, y0, y1 = xs.min(), xs.max(), ys.min(), ys.max()
    if (x1 - x0 + 1, y1 - y0 + 1) != (w, h):
        raise ValueError('registration image is %dx%d at (%d,%d), expected %dx%d' % (
            x1 - x0 + 1, y1 - y0 + 1, x0, y0, w, h))
    return int(x0), int(y0)


def check(frame, rec, run_dir, geom, out_dir, label):
    """Problems (list of strings) for one snapshot record of a manifest."""
    problems = []
    name = rec.get('name', '?')
    tag = '%s %s' % (label, name)
    if 'error' in rec:
        return ['%s: snapshot failed: %s' % (tag, rec['error'])]
    want_hash = getattr(frame, 'hash', None)
    if rec.get('hash', 'none') == 'none':
        problems.append('%s: the guest did not hash the presented frame' % tag)
    elif want_hash is not None and rec['hash'] != want_hash:
        problems.append('%s: frame hash %s, reference %s (the 68k build computed a different frame)' % (
            tag, rec['hash'], want_hash))
    sx, dst_h = scale_of(rec)
    dst_w = W * sx
    exp = expand(frame.rgb(), sx, dst_h)
    window = rec.get('window') == '1'
    # Picasso96 RGB formats of 15/16-bit screens (libraries/Picasso96.h RGBFB_*)
    tol = 8 if window and rec.get('p96fmt') in ('4', '5', '10', '11', '12', '13') else 0
    diff_disp = None
    if 'png' in rec:
        shot = load_rgb(os.path.join(run_dir, rec['png']))
        x, y = geom
        if window:
            x, y = x + int(rec['dx']), y + int(rec['dy'])
        crop = shot[y:y + dst_h, x:x + dst_w]
        if crop.shape != (dst_h, dst_w, 3):
            problems.append('%s: screenshot %dx%d too small for the image at (%d,%d)' % (
                tag, shot.shape[1], shot.shape[0], x, y))
        else:
            diff_disp = (np.abs(crop.astype(np.int16) - exp.astype(np.int16)) > tol).any(axis=2)
            if window and 'mouse' in rec:
                # the pointer sprite (at most 16 lores = 32 pixels wide, hot spot near its
                # top left) over the picture: not the game's
                mx, my = (int(v) for v in rec['mouse'].split(','))
                px, py = geom[0] + mx - x, geom[1] + my - y
                diff_disp[max(py - 4, 0):max(py + 28, 0), max(px - 8, 0):max(px + 36, 0)] = False
            n = int(diff_disp.sum())
            if n:
                ys, xs = np.nonzero(diff_disp)
                problems.append('%s: %d displayed pixels differ (x %d-%d, y %d-%d)' % (
                    tag, n, xs.min(), xs.max(), ys.min(), ys.max()))
            outside = np.zeros(shot.shape[:2], bool) if window else np.ones(shot.shape[:2], bool)
            outside[y:y + dst_h, x:x + dst_w] = False
            lit = outside & (shot != 0).any(axis=2)
            if lit.any():
                cols = np.unique(shot[lit].reshape(-1, 3), axis=0)[:4].tolist()
                problems.append('%s: %d lit pixels outside the image (border), colours %s' % (
                    tag, int(lit.sum()), cols))
        if problems and out_dir:
            os.makedirs(out_dir, exist_ok=True)
            base = os.path.join(out_dir, '%s-%s' % (label, name))
            Image.fromarray(np.ascontiguousarray(exp), 'RGB').save(base + '.expected.png')
            Image.fromarray(shot, 'RGB').save(base + '.screenshot.png')
            if diff_disp is not None:
                d = np.full((dst_h, dst_w, 3), 40, np.uint8)
                d[diff_disp] = (255, 0, 0)
                Image.fromarray(d, 'RGB').save(base + '.diff.png')
    else:
        problems.append('%s: no screenshot' % tag)
    if 'bitmap' in rec:
        idx = np.load(os.path.join(run_dir, rec['bitmap'])).astype(np.int16)
        if 'pens' in rec:                           # a window: its pens back to colour indices
            lut = np.full(256, -1, np.int16)
            for i, p in enumerate(rec['pens'].split(',')):
                lut[int(p)] = i
            idx = lut[idx & 255]
        elif rec.get('standard') == '0':
            idx -= int(rec.get('penbase', '0'))     # graphics card: engine colours on pens 16..31
        want = expand(frame.idx, sx, dst_h)
        bad = idx != want if idx.shape == want.shape else np.ones(want.shape, bool)
        if bad.any():
            ys, xs = np.nonzero(bad)
            problems.append('%s: %d pixels of the displayed bitmap differ (x %d-%d, y %d-%d)' % (
                tag, int(bad.sum()), xs.min(), xs.max(), ys.min(), ys.max()))
            if out_dir:
                os.makedirs(out_dir, exist_ok=True)
                d = np.full((dst_h, dst_w, 3), 40, np.uint8)
                d[bad] = (255, 160, 0)
                Image.fromarray(d, 'RGB').save(os.path.join(out_dir, '%s-%s.bitmapdiff.png' % (label, name)))
    elif 'bitmap_error' in rec:
        problems.append('%s: bitmap not read: %s' % (tag, rec['bitmap_error']))
    return problems


def load_manifest(run_dir):
    recs = []
    path = os.path.join(run_dir, 'manifest.jsonl')
    if os.path.exists(path):
        with open(path) as f:
            for line in f:
                recs.append(json.loads(line))
    return recs
