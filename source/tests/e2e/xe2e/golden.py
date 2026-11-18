"""Golden snapshots.

The reference is golden.tsv (committed): one sha256 per golden over what the frame shows (the
colour indices, the 16-colour palette, graphics on, scroll). The pictures are not committed,
they are the game's art: golden/<lang>/<name>.png is a local copy (4-bit palette PNG, the
palette, graphics-on and scroll in text chunks; 5-25 KB each) that a run saves when the frame
matches the reference and that a later failure is drawn against. --update rewrites both."""
import hashlib
import os
import struct

import numpy as np
from PIL import Image, PngImagePlugin

HEADER = ('# e2e goldens: sha256 of what each golden frame shows (tests/e2e/xe2e/golden.py '
          'frame_hash).\n# Rewritten by run.py --update; the pictures stay local '
          '(golden/<lang>/<name>.png).\nlang\tname\tsha256\n')


def frame_hash(idx, pal, gon, scroll):
    return hashlib.sha256(np.ascontiguousarray(idx, np.uint8).tobytes() + bytes(pal)
                          + struct.pack('<ii', int(gon), int(scroll))).hexdigest()


class Manifest:
    """golden.tsv: (lang, name) -> sha256."""

    def __init__(self, path):
        self.path = path
        self.rows = {}
        self.dirty = False
        if os.path.exists(path):
            for line in open(path, encoding='utf-8'):
                f = line.rstrip('\n').split('\t')
                if line.startswith('#') or f[0] == 'lang' or len(f) != 3:
                    continue
                self.rows[(f[0], f[1])] = f[2]

    def get(self, lang, name):
        return self.rows.get((lang, name))

    def set(self, lang, name, h):
        _changes.append((self.path, lang, name, h))
        if self.rows.get((lang, name)) != h:
            self.rows[(lang, name)] = h
            self.dirty = True

    def save(self):
        if not self.dirty:
            return
        with open(self.path, 'w', encoding='utf-8') as f:
            f.write(HEADER)
            for (lang, name), h in sorted(self.rows.items()):
                f.write('%s\t%s\t%s\n' % (lang, name, h))
        self.dirty = False


_manifests = {}
_changes = []       # the goldens recorded in this process (run.py takes them from each unit)


def manifest(path):
    if path not in _manifests:
        _manifests[path] = Manifest(path)
    return _manifests[path]


def take_changes():
    """The (manifest path, lang, name, sha256) recorded since the last call."""
    out = list(_changes)
    del _changes[:]
    return out


def save_manifests():
    for m in _manifests.values():
        m.save()


def save(path, frame):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    img = Image.fromarray(frame.idx, 'P')
    pal = []
    for v in frame.pal:
        pal.append(v * 17)
    img.putpalette(pal + [0] * (768 - len(pal)))
    info = PngImagePlugin.PngInfo()
    info.add_text('xe_pal', frame.pal.hex())
    info.add_text('xe_gon', str(frame.gon))
    info.add_text('xe_scroll', str(frame.scroll))
    tmp = '%s.%d.tmp' % (path, os.getpid())
    img.save(tmp, format='PNG', bits=4, pnginfo=info, optimize=True)
    os.replace(tmp, path)               # a reader never sees half a picture


def load(path):
    img = Image.open(path)
    idx = np.array(img.convert('P') if img.mode != 'P' else img, np.uint8)
    pal = bytes.fromhex(img.text.get('xe_pal', '00' * 48))
    gon = int(img.text.get('xe_gon', '1'))
    scroll = int(img.text.get('xe_scroll', '0'))
    return idx, pal, gon, scroll


def write_rgb(path, idx, pal, gon=1):
    rgb = np.array([v * 17 for v in pal], np.uint8).reshape(16, 3)[idx] if gon else np.zeros(idx.shape + (3,), np.uint8)
    Image.fromarray(rgb, 'RGB').save(path)


def compare(frame, golden_path, out_dir, name, update=False, manifest_path=None, lang=None):
    """Returns None when the frame matches the reference hash (or was recorded with update),
    else a problem string; writes <name>.actual.png (and .golden.png / .diff.png when the local
    copy of the golden is there and current) to out_dir."""
    m = manifest(manifest_path)
    h = frame_hash(frame.idx, frame.pal, frame.gon, frame.scroll)
    if update:
        save(golden_path, frame)
        m.set(lang, name, h)
        return None
    ref = m.get(lang, name)
    if ref is None:
        return 'golden %s [%s] is not in %s (record it with --update)' % (name, lang, manifest_path)
    local = load(golden_path) if os.path.exists(golden_path) else None
    if local is not None and frame_hash(*local) != ref:
        local = None                        # out of date: golden.tsv changed since it was saved
        try:
            os.remove(golden_path)
        except OSError:
            pass
    if h == ref:
        if local is None:
            save(golden_path, frame)        # the local copy later failures are drawn against
        return None
    os.makedirs(out_dir, exist_ok=True)
    write_rgb(os.path.join(out_dir, name + '.actual.png'), frame.idx, frame.pal, frame.gon)
    if local is None:
        return ('golden %s: differs from the reference in golden.tsv; no local copy to compare '
                'with (see %s/%s.actual.png; run the scenario once on an unchanged tree to make '
                'golden/%s/%s.png)' % (name, out_dir, name, lang, name))
    idx, pal, gon, scroll = local
    reasons = []
    diff = idx != frame.idx
    if diff.any():
        ys, xs = np.nonzero(diff)
        reasons.append('%d pixels differ (x %d-%d, y %d-%d)' % (int(diff.sum()), xs.min(), xs.max(), ys.min(), ys.max()))
    if pal != frame.pal:
        reasons.append('palette differs')
    if gon != frame.gon:
        reasons.append('graphics %s, golden %s' % ('on' if frame.gon else 'off', 'on' if gon else 'off'))
    if scroll != frame.scroll:
        reasons.append('scroll %d, golden %d' % (frame.scroll, scroll))
    write_rgb(os.path.join(out_dir, name + '.golden.png'), idx, pal, gon)
    d = np.zeros((400, 640, 3), np.uint8)
    d[..., 0] = np.where(diff, 255, 40)
    d[..., 1] = np.where(diff, 0, 40)
    d[..., 2] = np.where(diff, 0, 40)
    Image.fromarray(d, 'RGB').save(os.path.join(out_dir, name + '.diff.png'))
    return 'golden %s: %s (see %s/%s.*.png)' % (name, '; '.join(reasons), out_dir, name)
