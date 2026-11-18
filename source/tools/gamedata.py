#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The original game data: import it from your copy of the game, check it, derive resources.

The repository does not carry the game, only an inventory of its files: resources/inventory.tsv
(name, size, mtime, floppy, kind, md5, location). This is lrvne's import framework
(source/lrvne/tools: lrvne.inventory, which finds the files in whatever you have: directories,
zip/7z/lzh archives, floppy and hard-disk images, nested in any order) with what only this
game's tools know: where a file goes, which files the pack needs, and the derived data.

usage (python3 -I tools/gamedata.py ...):
  import [SRC...] [--out DIR]    copy the files of the inventory from your media into
                                 resources/original/<location>/<name> (not committed); exit
                                 status 1 unless every file the pack is built from is there
  check [-q] [--out DIR]         verify that tree; `make pack` runs it first
  scan [SRC...]                  print inventory rows for what SRC holds (maintenance)
  derive [PART...] [--python PY] the resources the RE documents refer to, see below
SRC defaults to the directories named in the LRVNE_MEDIA environment variable. Accepted are
D88 floppy images, Anex86 FDI and raw floppy images, hard-disk images (Anex86 HDI, T98-Next
NHD: the DOS partitions are found through the PC-98 partition table), archives of them, and
directories with the game's files anywhere below them, in any letter case. Repeated imports add
to what is there (floppies, then an HDD image).

The groups of the report:
  pack inputs       *.GDT (but XEN.GDT), *.ADT, *.M, *.EFC, *.CC, AGS.EXE, PMD.COM: required
  other game files  the batch files and disk markers of the release: not used by the pack
  HDD install only  files of the HDD image the inventory was made from that the floppies do not
                    have (AUTOEXEC.BAT, its own CONFIG.SYS and XEN.GDT): not used
  floppy only       the boot files and the saves on disk A (derive saves uses the saves)

derive: the resources the RE documents refer to, made from resources/original/ by the tools in
re-tools/ (none of it is committed): images (gdt.py), anim (adt.py), scripts (ccdis.py), sfx
(pmdinfo.py: effect blobs), music (pmdinfo.py: compares resources/music/index.tsv), saves
(savedump.py) and vgm (pmdemu.py: the reference recordings the tests compare with; needs
unicorn, --python a Python that has it). Default: all of them; Pillow is needed for images and
anim. The committed tables made partly by tools (music/index.tsv, sfx/xen_se_efc.tsv) are
compared with the tools' output and reported when stale.
"""
import glob
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
RES = os.path.join(ROOT, 'resources')
ORIG = os.path.join(RES, 'original')
INVENTORY = os.path.join(RES, 'inventory.tsv')
RETOOLS = os.path.join(ROOT, 're-tools')
sys.path.insert(0, os.path.join(HERE, '..', 'lrvne', 'tools'))
from lrvne import inventory   # noqa: E402

PACK_EXT = ('.GDT', '.ADT', '.M', '.EFC', '.CC')


def make_hooks():
    """Where a row's file goes and how it is counted: the groups above."""
    rows = inventory.Inventory.load(INVENTORY).rows
    floppy_names = {r.name for r in rows if r.get('location') == 'floppy_only'}

    def group(row):
        ext = os.path.splitext(row.name)[1]
        if row.get('location') == 'floppy_only':
            return 'floppy only'
        if row.get('floppy') == '-' or row.name in floppy_names:
            return 'HDD install only'       # the floppies have no copy of this content
        if (ext in PACK_EXT and row.name != 'XEN.GDT') or row.name in ('AGS.EXE', 'PMD.COM'):
            return 'pack inputs'
        return 'other game files'

    return inventory.Hooks(dest_path=lambda row: row.get('location') + '/' + row.name,
                           required=lambda row: group(row) == 'pack inputs',
                           group=group)


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


def add_derive_arguments(p):
    p.add_argument('parts', nargs='*', help='%s (default: all)' % ' '.join(PARTS))
    p.add_argument('--python', default=os.environ.get('DATA_PY') or sys.executable,
                   help='a Python with Pillow and unicorn for the parts that need them')


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
    return inventory.run_cli(None, INVENTORY, ORIG, hooks=make_hooks(), prog='gamedata',
                             description=__doc__.split('\n')[0],
                             commands={'derive': (add_derive_arguments, cmd_derive)})


if __name__ == '__main__':
    sys.exit(main())
