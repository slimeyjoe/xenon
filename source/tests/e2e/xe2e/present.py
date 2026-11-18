"""Presentation oracle (SDL3 backend): what the renderer put on the screen versus the engine's
planes. The backend's `readback` command (lrvne/backends/control/control.h) returns one RGB sample per
source pixel, taken at the centre of that pixel's destination block, the destination rectangle it
drew, and two counters it computed over the whole drawable: destination pixels that differ from
their source pixel's sample (nonuniform) and lit pixels outside the rectangle (border_lit).

Rules (plan §4.1, lrvne/backends/sdl3/main.c dest_rect):
  * the rectangle is the one the scale mode prescribes for the drawable size, centred;
  * everything outside it is black;
  * integer and integer-aspect modes (nearest filtering) reproduce every pixel exactly, so the
    samples equal palette[index] (or black with graphics off) and nonuniform is 0;
  * fit and stretch filter linearly, so only pixels whose eight neighbours have the same colour
    are checked (bilinear interpolation of equal colours is that colour); edges are left alone.
"""
import math
import os

import numpy as np

from . import golden

INTEGER, INTEGER_ASPECT, FIT, STRETCH = 0, 1, 2, 3
MODE_NAMES = {'integer': INTEGER, 'aspect': INTEGER_ASPECT, 'fit': FIT, 'stretch': STRETCH}


def expected_dest(W, H, mode):
    """(x, y, w, h) the backend must draw into for a WxH drawable, as integers."""
    if mode == INTEGER:
        f = max(1, min(W // 640, H // 400))
        w, h = 640 * f, 400 * f
    elif mode == INTEGER_ASPECT:
        bx = by = 1
        for fx in range(1, W // 640 + 1):
            fy = int(fx * 1.2 + 0.5)
            if 1 <= fy <= H // 400:
                bx, by = fx, fy
        w, h = 640 * bx, 400 * by
    elif mode == FIT:
        s = min(W / 640.0, H / 480.0)
        w, h = 640.0 * s, 480.0 * s
    else:
        w, h = W, H
    x, y = (W - w) / 2.0, (H - h) / 2.0          # negative when the image is larger than the drawable
    return (int(math.floor(x + 0.5)), int(math.floor(y + 0.5)),
            int(math.floor(w + 0.5)), int(math.floor(h + 0.5)))


def flat_mask(idx):
    """Pixels whose 3x3 neighbourhood is one colour (edges of the screen count as flat)."""
    m = np.ones(idx.shape, bool)
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            if dx == 0 and dy == 0:
                continue
            sh = np.roll(np.roll(idx, dy, 0), dx, 1)
            eq = sh == idx
            # roll wraps around: do not demand anything of the wrapped neighbours
            if dy == -1:
                eq[-1, :] = True
            if dy == 1:
                eq[0, :] = True
            if dx == -1:
                eq[:, -1] = True
            if dx == 1:
                eq[:, 0] = True
            m &= eq
    return m


def check(frame, rb, where, out_dir=None, name=None):
    """Returns a list of problem strings (empty when the rendering matches the frame)."""
    problems = []
    exp = expected_dest(rb.win[0], rb.win[1], rb.mode)
    if tuple(rb.dest) != exp:
        problems.append('dest rect %s, expected %s for %dx%d mode %d' % (tuple(rb.dest), exp, rb.win[0], rb.win[1], rb.mode))
    if rb.border_lit:
        problems.append('%d lit pixels outside the image rectangle' % rb.border_lit)
    want = frame.rgb()
    if rb.linear:
        mask = flat_mask(frame.idx)
        what = 'flat-area'
    else:
        mask = np.ones(frame.idx.shape, bool)
        what = ''
        if rb.nonuniform:
            problems.append('%d destination pixels differ from their source pixel' % rb.nonuniform)
    diff = (rb.rgb != want).any(axis=2) & mask
    if diff.any():
        ys, xs = np.nonzero(diff)
        problems.append('%d %s pixels differ from the planes (x %d-%d, y %d-%d)' % (
            int(diff.sum()), what or 'rendered', xs.min(), xs.max(), ys.min(), ys.max()))
        if out_dir and name:
            os.makedirs(out_dir, exist_ok=True)
            golden.write_rgb(os.path.join(out_dir, name + '.planes.png'), frame.idx, frame.pal, frame.gon)
            from PIL import Image
            Image.fromarray(rb.rgb, 'RGB').save(os.path.join(out_dir, name + '.rendered.png'))
            d = np.full((400, 640, 3), 40, np.uint8)
            d[diff] = (255, 0, 0)
            Image.fromarray(d, 'RGB').save(os.path.join(out_dir, name + '.diff.png'))
    return ['present %s: %s' % (where, p) for p in problems]
