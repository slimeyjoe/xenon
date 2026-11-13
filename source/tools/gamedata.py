#!/usr/bin/env python3
"""The original game data: import it from your copy of the game, check it, derive resources.

The repository does not carry the game (C's Ware, 1994). `import` takes the files from the
original release, checks every one against resources/inventory.tsv (name and md5) and writes
them to resources/original/ (not committed); `make pack` builds the pack from there.

usage:
  gamedata.py import SRC... [--out DIR] [--offset N]
  gamedata.py check [GAMEDIR]
  gamedata.py derive [PART...] [--python PY]

import SRC: any mix of
  - D88 floppy images (*.d88), Anex86 FDI or raw floppy images (*.fdi, *.hdm, *.xdf, *.dup);
  - hard-disk images: Anex86 HDI (*.hdi), T98-Next NHD (*.nhd); the DOS partitions are found
    through the PC-98 partition table. --offset N reads a FAT volume at byte N of any image;
  - a zip holding such images (only its .d88/.fdi/.hdm/.hdi/.nhd members are read), or one
    member of it as ZIP::path/in/zip.d88;
  - a directory: the game's files anywhere below it, any case (an installed copy, an emulator's
    host drive), and the images and zips in it.
  The FAT12/16 file systems are read directly (no mtools). Only files whose name and md5 are in
  the inventory are taken; each goes to DIR/game or DIR/floppy_only (the inventory's location
  column) with the inventory's date. Repeated imports add to what is there (floppies, then an
  HDD image). Exit status 1 unless every file the pack is built from is there afterwards.
check: the same verification of an existing game directory (default resources/original/game),
  nothing written; `make pack` runs it first.
derive: the resources the RE documents refer to, made from resources/original/ by the tools in
  re-tools/ (none of it is committed): images (gdt.py), anim (adt.py), scripts (ccdis.py), sfx
  (pmdinfo.py: effect blobs), music (pmdinfo.py: compares resources/music/index.tsv), saves
  (savedump.py) and vgm (pmdemu.py: the reference recordings the tests compare with; needs
  unicorn, PY a Python that has it). Default: all of them; Pillow is needed for images/anim.
  The committed tables made partly by tools (music/index.tsv, sfx/xen_se_efc.tsv) are compared
  with the tools' output and reported when stale.

The file groups the report uses:
  pack inputs       *.GDT (but XEN.GDT), *.ADT, *.M, *.EFC, *.CC, AGS.EXE, PMD.COM: required
  other game files  the batch files and disk markers of the release: not used by the pack
  HDD install only  files of the HDD image the inventory was made from that the floppies do not
                    have (AUTOEXEC.BAT, its own CONFIG.SYS and XEN.GDT): not used
  floppy only       the boot files and the saves on disk A (derive saves uses the saves)
"""
import argparse
import collections
import glob
import hashlib
import os
import struct
import subprocess
import sys
import tempfile
import time
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
RES = os.path.join(ROOT, 'resources')
ORIG = os.path.join(RES, 'original')
INVENTORY = os.path.join(RES, 'inventory.tsv')
RETOOLS = os.path.join(ROOT, 're-tools')
sys.path.insert(0, HERE)
import d88   # noqa: E402

IMAGE_EXT = ('.d88', '.d98', '.88d', '.fdi', '.hdm', '.xdf', '.dup', '.hdi', '.nhd')
PACK_EXT = ('.GDT', '.ADT', '.M', '.EFC', '.CC')


def u16(b, o):
    return struct.unpack_from('<H', b, o)[0]


def u32(b, o):
    return struct.unpack_from('<I', b, o)[0]


# ---- inventory ---------------------------------------------------------------------------------

Row = collections.namedtuple('Row', 'name size mtime floppy kind md5 location group')


def load_inventory(path=INVENTORY):
    rows = []
    with open(path, encoding='utf-8') as f:
        head = f.readline().rstrip('\n').split('\t')
        for line in f:
            if line.strip():
                rows.append(dict(zip(head, line.rstrip('\n').split('\t'))))
    floppy_names = {r['name'] for r in rows if r['location'] == 'floppy_only'}
    out = []
    for r in rows:
        name, loc = r['name'], r['location']
        ext = os.path.splitext(name)[1]
        if loc == 'floppy_only':
            group = 'floppy only'
        elif r['floppy'] == '-' or name in floppy_names:
            group = 'HDD install only'       # the floppies have no copy of this content
        elif (ext in PACK_EXT and name != 'XEN.GDT') or name in ('AGS.EXE', 'PMD.COM'):
            group = 'pack inputs'
        else:
            group = 'other game files'
        out.append(Row(name, int(r['size']), r['mtime'], r['floppy'], r['kind'], r['md5'], loc, group))
    return out


GROUPS = ('pack inputs', 'other game files', 'HDD install only', 'floppy only')


# ---- FAT12/16 ----------------------------------------------------------------------------------

def bpb_ok(img, base):
    """The FAT geometry at img[base:] if a plausible BPB is there, else None."""
    if base < 0 or base + 64 > len(img):
        return None
    b = img[base:base + 64]
    bps, spc, res, nfats, nroot, tot16, media, spf = struct.unpack_from('<HBHBHHBH', b, 11)
    tot = tot16 or u32(b, 32)
    if bps not in (256, 512, 1024, 2048) or spc == 0 or spc & (spc - 1) or res < 1:
        return None
    if nfats not in (1, 2) or nroot == 0 or media < 0xF0 or spf == 0 or tot == 0:
        return None
    fat = base + res * bps
    root = fat + nfats * spf * bps
    data = root + (nroot * 32 + bps - 1) // bps * bps
    clusters = (tot * bps - (data - base)) // (spc * bps)
    if clusters <= 0 or data > len(img):
        return None
    return dict(bps=bps, spc=spc, fat=fat, root=root, nroot=nroot, data=data,
                clusters=clusters, fat16=clusters >= 4085, end=base + tot * bps)


class Fat:
    def __init__(self, img, base, label):
        self.img = img
        self.g = bpb_ok(img, base)
        self.label = label
        if self.g is None:
            raise ValueError('%s: no FAT file system at byte %d' % (label, base))

    def next(self, c):
        g, img = self.g, self.img
        if g['fat16']:
            v = u16(img, g['fat'] + 2 * c)
            return None if v >= 0xFFF8 else v
        v = u16(img, g['fat'] + c * 3 // 2)
        v = v >> 4 if c & 1 else v & 0xFFF
        return None if v >= 0xFF8 else v

    def chain(self, c):
        out, seen = [], set()
        while c is not None and 2 <= c < self.g['clusters'] + 2 and c not in seen:
            seen.add(c)
            out.append(c)
            c = self.next(c)
        return out

    def read_chain(self, c, size=None):
        csize = self.g['bps'] * self.g['spc']
        parts = []
        for k in self.chain(c):
            o = self.g['data'] + (k - 2) * csize
            parts.append(self.img[o:o + csize])
        data = b''.join(parts)
        return data if size is None else data[:size]

    def entries(self, raw):
        for i in range(0, len(raw) - 31, 32):
            e = raw[i:i + 32]
            if e[0] == 0:
                break
            if e[0] == 0xE5 or e[11] == 0x0F or e[11] & 0x08:
                continue            # deleted, long-name part, volume label
            base = e[0:8].rstrip(b' ')
            if base[:1] == b'\x05':
                base = b'\xe5' + base[1:]
            ext = e[8:11].rstrip(b' ')
            name = (base + (b'.' + ext if ext else b'')).decode('cp932', 'replace')
            yield name, e[11], u16(e, 26), u32(e, 28)

    def walk(self, raw=None, path='', seen=None):
        """(path, name, data) of every file."""
        if raw is None:
            g = self.g
            raw = self.img[g['root']:g['root'] + g['nroot'] * 32]
            seen = set()
        for name, attr, clus, size in self.entries(raw):
            if attr & 0x10:
                if name in ('.', '..') or clus < 2 or clus in seen:
                    continue            # a damaged directory may point back up the tree
                seen.add(clus)
                yield from self.walk(self.read_chain(clus), path + name + '\\', seen)
            else:
                yield path + name, name, self.read_chain(clus, size) if size else b''


# ---- images ------------------------------------------------------------------------------------

def pc98_partitions(img, base, ss, spt, heads):
    """Byte offsets of the partitions in the PC-98 partition table (physical sector 1)."""
    pt = img[base + ss:base + ss + 512]
    offs = []
    for i in range(0, len(pt) - 31, 32):
        e = pt[i:i + 32]
        if not any(e) or e[0] == 0:
            continue
        lba = u16(e, 10) * heads * spt + e[9] * spt + e[8]
        offs.append(base + lba * ss)
    return offs


def volumes(img, label, offset=None):
    """The FAT volumes of one image: [Fat]."""
    if offset is not None:
        return [Fat(img, offset, '%s@%d' % (label, offset))]
    lower = label.lower()
    # D88: the size field is the file's length; sectors stored with headers
    if len(img) >= 0x2B0 and u32(img, 0x1C) == len(img) and img[0x1B] in (0x00, 0x10, 0x20, 0x30, 0x40):
        _, tracks = d88.d88_parse(img)
        return [Fat(d88.to_raw(tracks), 0, label)]
    if img[:15] == b'T98HDDIMAGE.R0\0':
        base, heads, spt, ss = u32(img, 0x110), u16(img, 0x118), u16(img, 0x11A), u16(img, 0x11C)
        cands = pc98_partitions(img, base, ss, spt, heads)
    elif lower.endswith(('.hdi', '.fdi')) or (len(img) > 4096 and u32(img, 8) + u32(img, 12) == len(img)):
        base, ss, spt, heads = u32(img, 8), u32(img, 16), u32(img, 20), u32(img, 24)
        # an HDD (HDI) has the IPL and the partition table first, a floppy (FDI) the boot sector
        cands = [o for o in pc98_partitions(img, base, ss or 512, spt, heads) if bpb_ok(img, o)] or [base]
    else:
        cands = [0]                         # a raw floppy image (.hdm, .xdf, .dup, ...)
    out = [Fat(img, o, '%s (partition at byte %d)' % (label, o) if o else label)
           for o in cands if bpb_ok(img, o)]
    if not out:
        raise ValueError('%s: no FAT file system found (try --offset)' % label)
    return out


def read_zip(path, member=None):
    with zipfile.ZipFile(path) as z:
        names = [member] if member else [n for n in z.namelist() if n.lower().endswith(IMAGE_EXT)]
        for n in sorted(names):
            yield '%s::%s' % (path, n), z.read(n)


def sources(spec, offset=None):
    """(label, path in source, name, data) of every file in one SRC."""
    if '::' in spec and not os.path.exists(spec):
        zpath, member = spec.split('::', 1)
        for lab, img in read_zip(zpath, member):
            for v in volumes(img, lab, offset):
                for p, n, d in v.walk():
                    yield v.label, p, n, d
        return
    if os.path.isdir(spec):
        for dirpath, dirs, files in os.walk(spec):
            dirs.sort()
            for f in sorted(files):
                full = os.path.join(dirpath, f)
                low = f.lower()
                if low.endswith(IMAGE_EXT) or low.endswith('.zip'):
                    try:
                        yield from sources(full, offset)
                    except (ValueError, zipfile.BadZipFile, struct.error) as e:
                        print('gamedata: skipped %s: %s' % (full, e))
                else:
                    yield spec, os.path.relpath(full, spec), f.upper(), None
        return
    if spec.lower().endswith('.zip'):
        for lab, img in read_zip(spec):
            for v in volumes(img, lab, offset):
                for p, n, d in v.walk():
                    yield v.label, p, n, d
        return
    with open(spec, 'rb') as f:
        img = f.read()
    for v in volumes(img, spec, offset):
        for p, n, d in v.walk():
            yield v.label, p, n, d


# ---- import / check ----------------------------------------------------------------------------

def md5(data):
    return hashlib.md5(data).hexdigest()


def set_mtime(path, mtime):
    t = time.mktime(time.strptime(mtime, '%Y-%m-%d %H:%M'))
    os.utime(path, (t, t))


def report(inv, have, differs, where):
    """have: set of Row; differs: {name: [(md5, origin)]}. Prints; returns True if complete."""
    ok = True
    for grp in GROUPS:
        rows = [r for r in inv if r.group == grp]
        if not rows:
            continue
        got = [r for r in rows if r in have]
        missing = sorted({r.name for r in rows if r not in have} - {r.name for r in got})
        line = '  %-17s %3d/%d' % (grp, len(got), len(rows))
        if missing:
            line += '  missing: ' + ' '.join(missing[:12]) + (' ...' if len(missing) > 12 else '')
        print(line)
        if grp == 'pack inputs' and missing:
            ok = False
    names = {r.name for r in inv if r.group == 'pack inputs'}
    for name in sorted(differs):
        if any(r.name == name for r in have):
            continue
        for h, origin in differs[name][:3]:
            print('  differs: %s md5 %s (%s)%s' % (name, h, origin,
                                                     '' if name in names else ', not used by the pack'))
    print('gamedata: %s: %s' % (where, 'every file the pack is built from is there' if ok else
                                'INCOMPLETE: the pack cannot be built from it'))
    return ok


def cmd_import(a):
    inv = load_inventory(a.inventory)
    by_name = collections.defaultdict(list)
    for r in inv:
        by_name[r.name].append(r)
    found = {}                               # Row -> data
    differs = collections.defaultdict(list)
    for spec in a.src:
        if not os.path.exists(spec) and '::' not in spec:
            sys.exit('gamedata: %s does not exist' % spec)
        counts = collections.Counter()
        try:
            for label, path, name, data in sources(spec, a.offset):
                rows = by_name.get(name)
                if not rows:
                    continue
                if data is None:
                    with open(os.path.join(label, path), 'rb') as f:
                        data = f.read()
                h = md5(data)
                match = [r for r in rows if r.md5 == h]
                if match:
                    for r in match:
                        found.setdefault(r, data)
                    counts[label] += 1
                else:
                    differs[name].append((h, '%s: %s' % (label, path)))
        except (ValueError, zipfile.BadZipFile, struct.error) as e:
            sys.exit('gamedata: %s' % e)
        for label, n in counts.items():
            print('gamedata: %s: %d files of the game' % (label, n))
        if not counts:
            print('gamedata: %s: no files of the game' % spec)
    written = 0
    for r, data in sorted(found.items()):
        d = os.path.join(a.out, r.location)
        os.makedirs(d, exist_ok=True)
        path = os.path.join(d, r.name)
        if not (os.path.exists(path) and md5(open(path, 'rb').read()) == r.md5):
            with open(path, 'wb') as f:
                f.write(data)
            written += 1
        set_mtime(path, r.mtime)
    print('gamedata: %d files written to %s (%d already there)' % (written, a.out, len(found) - written))
    have = set(found)
    for r in inv:                          # what earlier imports left
        if r not in have and verify_file(os.path.join(a.out, r.location, r.name), r):
            have.add(r)
    return 0 if report(inv, have, differs, a.out) else 1


def verify_file(path, r):
    try:
        with open(path, 'rb') as f:
            return md5(f.read()) == r.md5
    except OSError:
        return False


def cmd_check(a):
    inv = [r for r in load_inventory(a.inventory) if r.location == 'game']
    gamedir = a.gamedir or os.path.join(ORIG, 'game')
    if not os.path.isdir(gamedir):
        print('gamedata: no game directory %s: import the game first, e.g.\n'
              '  make data SRC=path/to/the/floppy/images (or an HDD image, or an installed copy)'
              % gamedir)
        return 1
    present = {f.upper(): f for f in os.listdir(gamedir)}
    have, differs = set(), collections.defaultdict(list)
    for r in inv:
        real = present.get(r.name)
        if real is None:
            continue
        if real != r.name:
            differs[r.name].append(('-', 'named %s: the tools need the DOS name %s' % (real, r.name)))
            continue
        h = md5(open(os.path.join(gamedir, real), 'rb').read())
        if h == r.md5:
            have.add(r)
        else:
            differs[r.name].append((h, gamedir))
    if a.quiet and all(r in have for r in inv if r.group == 'pack inputs'):
        return 0
    return 0 if report(inv, have, differs, gamedir) else 1


# ---- derive ------------------------------------------------------------------------------------

def tool(name):
    return os.path.join(RETOOLS, name)


def run(py, args, **kw):
    print('  ' + ' '.join(os.path.relpath(x, ROOT) if os.path.isabs(x) and x.startswith(ROOT) else x
                          for x in [os.path.basename(args[0])] + args[1:6]) + (' ...' if len(args) > 6 else ''))
    return subprocess.run([py, '-I'] + args, check=True, **kw)


def tsv_rows(text):
    lines = [l for l in text.split('\n') if l and not l.startswith('#')]
    head = lines[0].split('\t')
    return head, [dict(zip(head, l.split('\t'))) for l in lines[1:]], [l for l in text.split('\n') if l.startswith('#')]


def stale_check(committed, tool_out, key):
    """Compare the tool's columns of a committed table with the tool's output."""
    ch, crows, ccom = tsv_rows(open(committed, encoding='utf-8').read())
    th, trows, tcom = tsv_rows(tool_out)
    bad = []
    if ccom != tcom:
        bad.append('comment lines')
    cmap = {r[key]: r for r in crows}
    tmap = {r[key]: r for r in trows}
    bad += ['%s missing' % k for k in sorted(set(tmap) - set(cmap))]
    bad += ['%s not made by the tool' % k for k in sorted(set(cmap) - set(tmap))]
    for k in sorted(set(cmap) & set(tmap)):
        bad += ['%s %s' % (k, col) for col in th if cmap[k].get(col) != tmap[k][col]]
    rel = os.path.relpath(committed, ROOT)
    if bad:
        print('  STALE %s: differs from the tool in %s; regenerate those columns, keep the others'
              % (rel, ', '.join(bad[:8]) + (' ...' if len(bad) > 8 else '')))
    else:
        print('  %s: the tool columns are current' % rel)
    return not bad


def have_module(py, mod):
    return subprocess.run([py, '-I', '-c', 'import ' + mod], capture_output=True).returncode == 0


PARTS = ('images', 'anim', 'scripts', 'sfx', 'music', 'saves', 'vgm')


def cmd_derive(a):
    py = a.python
    game = os.path.join(ORIG, 'game')
    floppy = os.path.join(ORIG, 'floppy_only')
    if not os.path.isdir(game):
        sys.exit('gamedata: no %s: import the game first (make data SRC=...)' % game)
    parts = a.parts or list(PARTS)
    for p in parts:
        if p not in PARTS:
            sys.exit('gamedata: derive: unknown part %s (%s)' % (p, ' '.join(PARTS)))
    files = lambda pat: sorted(glob.glob(os.path.join(game, pat)))  # noqa: E731
    ok = True
    for p in parts:
        print('gamedata: derive %s' % p)
        if p in ('images', 'anim') and not have_module(py, 'PIL'):
            print('  skipped: %s has no Pillow (pip install Pillow)' % py)
            continue
        if p == 'images':
            out = os.path.join(RES, 'images')
            run(py, [tool('gdt.py'), '-o', out, '--composite-dir', os.path.join(out, 'composite')]
                + files('*.GDT'), stdout=subprocess.DEVNULL)
        elif p == 'anim':
            run(py, [tool('adt.py'), 'batch', game, '-o', os.path.join(RES, 'anim')], stdout=subprocess.DEVNULL)
        elif p == 'scripts':
            run(py, [tool('ccdis.py'), '--export', os.path.join(RES, 'scripts')] + files('*.CC'),
                stdout=subprocess.DEVNULL)
        elif p == 'sfx':
            r = run(py, [tool('pmdinfo.py'), '--efc', os.path.join(game, 'XEN_SE.EFC'), '--split',
                         os.path.join(RES, 'sfx', 'blobs')], capture_output=True, text=True)
            ok &= stale_check(os.path.join(RES, 'sfx', 'xen_se_efc.tsv'), r.stdout, 'num')
        elif p == 'music':
            r = run(py, [tool('pmdinfo.py'), '--tsv'] + files('*.M'), capture_output=True, text=True)
            ok &= stale_check(os.path.join(RES, 'music', 'index.tsv'), r.stdout, 'file')
        elif p == 'saves':
            saves = sorted(glob.glob(os.path.join(floppy, 'SAVE[1-4].DAT')))
            xen = os.path.join(floppy, 'XEN.GDT')
            if not saves:
                print('  skipped: no saves in %s (they are on floppy disk A)' % floppy)
                continue
            out = os.path.join(RES, 'saves')
            run(py, [tool('savedump.py'), '--json', out] + saves, stdout=subprocess.DEVNULL)
            if os.path.exists(xen):
                r = run(py, [tool('savedump.py'), '--xen', xen], capture_output=True, text=True)
                with open(os.path.join(out, 'XEN.GDT.floppy.json'), 'w', encoding='utf-8') as f:
                    f.write(r.stdout)
        elif p == 'vgm':
            if not have_module(py, 'unicorn'):
                print('  skipped: %s has no unicorn. The reference recordings need it:\n'
                      '    python3 -m venv ~/.venvs/xenon-re && ~/.venvs/xenon-re/bin/pip install unicorn\n'
                      '    make data-derive DATA_PY=~/.venvs/xenon-re/bin/python' % py)
                continue
            common = ['--gamedir', game, '--pmd', os.path.join(game, 'PMD.COM')]
            run(py, [tool('pmdemu.py'), '--all-music', os.path.join(RES, 'music', 'vgm')] + common,
                stdout=subprocess.DEVNULL)
            run(py, [tool('pmdemu.py'), '--all-se', os.path.join(RES, 'sfx', 'vgm')] + common,
                stdout=subprocess.DEVNULL)
    print('gamedata: derive %s' % ('done' if ok else 'done, with the problems above'))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    ap.add_argument('--inventory', default=INVENTORY)
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('import', help='take the game files from the original release')
    p.add_argument('src', nargs='+')
    p.add_argument('--out', default=ORIG)
    p.add_argument('--offset', type=int, default=None, help='a FAT volume at this byte of the image')
    p = sub.add_parser('check', help='verify a game directory against the inventory')
    p.add_argument('gamedir', nargs='?')
    p.add_argument('-q', '--quiet', action='store_true', help='print nothing when complete')
    p = sub.add_parser('derive', help='make the derived resources from resources/original')
    p.add_argument('parts', nargs='*')
    p.add_argument('--python', default=os.environ.get('DATA_PY') or sys.executable)
    a = ap.parse_args()
    return {'import': cmd_import, 'check': cmd_check, 'derive': cmd_derive}[a.cmd](a)


if __name__ == '__main__':
    sys.exit(main())
