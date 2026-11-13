#!/usr/bin/env python3
"""tools/gamedata.py import/check on synthetic disk images (no game data needed).

Builds small FAT12 and FAT16 volumes (1024- and 512-byte sectors, files in the root and in a
subdirectory) with made-up files, wraps them the ways a PC-98 game is found (raw floppy, D88,
Anex86 HDI and T98-Next NHD with a PC-98 partition table, a zip, a plain directory), imports
each against a made-up inventory and checks what lands where; then the failure cases (a changed
file, a missing pack input, a lower-case name for check). usage: check_import.py TMPDIR"""
import hashlib
import os
import shutil
import struct
import subprocess
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, '..', 'tools', 'gamedata.py')

# name, location, data; the pack inputs are AGS.EXE, PMD.COM, *.CC, *.GDT, *.M
FILES = [('AGS.EXE', 'game', b'MZ' + bytes(range(256)) * 9),
         ('PMD.COM', 'game', b'\xe9' + b'pmd' * 700),
         ('S00.CC', 'game', b'cc' * 3000),
         ('0010.GDT', 'game', bytes(5000)),
         ('XEN_01.M', 'game', b'm' * 77),
         ('XENON.BAT', 'game', b'echo off\r\n'),
         ('SAVE1.DAT', 'floppy_only', b'\x0b' + bytes(390))]


def fat_volume(files, bps, total, spc=1, subdir=None, nroot=64):
    """A FAT volume (FAT12 or FAT16 by cluster count): files in the root, subdir = (NAME, files)."""
    clusters = total
    fat16 = clusters >= 4085
    spf = -(-((clusters + 2) * (2 if fat16 else 3) // (1 if fat16 else 2)) // bps)
    root_secs = -(-nroot * 32 // bps)
    data_sec = 1 + 2 * spf + root_secs
    img = bytearray(total * bps)
    bpb = struct.pack('<HBHBHHBHHHI', bps, spc, 1, 2, nroot, total if total < 65536 else 0, 0xFE,
                      spf, 8, 2, 0)
    img[0:3] = b'\xeb\x3c\x90'
    img[3:11] = b'TEST    '
    img[11:11 + len(bpb)] = bpb
    if total >= 65536:
        struct.pack_into('<I', img, 32, total)
    fat = {}
    nxt = [2]
    csize = bps * spc

    def alloc(data):
        n = max(1, -(-len(data) // csize))
        first = nxt[0]
        for k in range(n):
            c = first + k
            fat[c] = c + 1 if k < n - 1 else 0xFFFF
            o = (data_sec + (c - 2) * spc) * bps
            img[o:o + csize] = data[k * csize:(k + 1) * csize].ljust(csize, b'\0')
        nxt[0] += n
        return first

    def entry(name, attr, clus, size):
        if name in ('.', '..'):
            field = name.ljust(11).encode()
        else:
            base, _, ext = name.partition('.')
            field = base.ljust(8).encode() + ext.ljust(3).encode()
        return field + bytes([attr]) + bytes(14) + struct.pack('<HI', clus, size)

    root = b''
    for name, data in files:
        root += entry(name, 0x20, alloc(data) if data else 0, len(data))
    if subdir:
        dname, dfiles = subdir
        body = b''.join(entry(n, 0x20, alloc(d), len(d)) for n, d in dfiles)
        c = nxt[0]
        body = entry('.', 0x10, c, 0) + entry('..', 0x10, 0, 0) + body
        assert alloc(body) == c
        root += entry(dname, 0x10, c, 0)
    ro = (1 + 2 * spf) * bps
    img[ro:ro + len(root)] = root
    for f in range(2):
        fo = (1 + f * spf) * bps
        if fat16:
            struct.pack_into('<HH', img, fo, 0xFFFE, 0xFFFF)
            for c, v in fat.items():
                struct.pack_into('<H', img, fo + 2 * c, v)
        else:
            img[fo:fo + 3] = b'\xfe\xff\xff'
            for c, v in fat.items():
                v &= 0xFFF
                o = fo + c * 3 // 2
                w = struct.unpack_from('<H', img, o)[0]
                w = (w & 0x000F) | (v << 4) if c & 1 else (w & 0xF000) | v
                struct.pack_into('<H', img, o, w)
    return bytes(img)


def d88(raw):
    """A 2HD D88 (77 cylinders x 2 heads x 8 sectors of 1024 bytes) around a raw floppy image."""
    out = bytearray(0x2B0)
    out[0:5] = b'TEST\0'
    out[0x1B] = 0x20
    tracks = len(raw) // (8 * 1024)
    for t in range(tracks):
        struct.pack_into('<I', out, 0x20 + 4 * t, len(out))
        for r in range(8):
            out += bytes([t // 2, t % 2, r + 1, 3]) + struct.pack('<HBBB5xH', 8, 0, 0, 0, 1024)
            o = (t * 8 + r) * 1024
            out += raw[o:o + 1024]
    struct.pack_into('<I', out, 0x1C, len(out))
    return bytes(out)


def hdd_data(vol, spt=17, heads=4):
    """Physical disk (512-byte sectors): IPL, PC-98 partition table, the volume at cylinder 1."""
    cyl_bytes = spt * heads * 512
    cyls = 1 + -(-len(vol) // cyl_bytes) + 1
    disk = bytearray(cyls * cyl_bytes)
    disk[0:8] = b'\xeb\x0a\x90\x90IPL1'
    e = bytearray(32)
    e[0], e[1] = 0xA1, 0x81
    struct.pack_into('<BBH', e, 8, 0, 0, 1)            # start sector, head, cylinder
    struct.pack_into('<BBH', e, 12, 0, 0, cyls - 2)
    e[16:32] = b'MS-DOS 6.20     '
    disk[512:544] = e
    disk[cyl_bytes:cyl_bytes + len(vol)] = vol
    return bytes(disk), cyls, heads, spt


def hdi(vol):
    disk, cyls, heads, spt = hdd_data(vol)
    return struct.pack('<8I', 0, 0, 4096, len(disk), 512, spt, heads, cyls).ljust(4096, b'\0') + disk


def nhd(vol):
    disk, cyls, heads, spt = hdd_data(vol)
    h = bytearray(512)
    h[0:15] = b'T98HDDIMAGE.R0\0'
    struct.pack_into('<IIHHH', h, 0x110, 512, cyls, heads, spt, 512)
    return bytes(h) + disk


def run(*args):
    r = subprocess.run([sys.executable, '-I', TOOL] + list(args), capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def main():
    tmp = os.path.abspath(sys.argv[1])
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp)
    inv = os.path.join(tmp, 'inventory.tsv')
    with open(inv, 'w', encoding='utf-8') as f:
        f.write('name\tsize\tmtime\tfloppy\tkind\tmd5\tlocation\n')
        for name, loc, data in FILES:
            f.write('%s\t%d\t1994-12-09 12:00\tA\ttest\t%s\t%s\n' % (name, len(data),
                                                                  hashlib.md5(data).hexdigest(), loc))
    files = [(n, d) for n, _, d in FILES]
    fails = 0

    def expect(label, cond, out=''):
        nonlocal fails
        if not cond:
            fails += 1
            print('FAIL %s\n    %s' % (label, out.replace('\n', '\n    ')))

    def imported(label, path, *extra):
        out_dir = os.path.join(tmp, 'out-' + label)
        rc, out = run('--inventory', inv, 'import', path, *extra, '--out', out_dir)
        expect('%s: exit status %d' % (label, rc), rc == 0, out)
        for name, loc, data in FILES:
            p = os.path.join(out_dir, loc, name)
            ok = os.path.exists(p) and open(p, 'rb').read() == data
            expect('%s: %s/%s' % (label, loc, name), ok, out)
        return out_dir

    # two floppies (the files split between them): D88s, a directory of them, a zip, a raw image
    a = fat_volume(files[:4], 1024, 1232)
    b = fat_volume(files[4:], 1024, 1232)
    pa, pb = os.path.join(tmp, 'disk_a.d88'), os.path.join(tmp, 'disk_b.d88')
    open(pa, 'wb').write(d88(a))
    open(pb, 'wb').write(d88(b))
    imported('d88', pa, pb)
    os.makedirs(os.path.join(tmp, 'disks'))
    shutil.copy(pa, os.path.join(tmp, 'disks', 'A.D88'))
    shutil.copy(pb, os.path.join(tmp, 'disks', 'B.D88'))
    imported('d88-dir', os.path.join(tmp, 'disks'))
    z = os.path.join(tmp, 'disks.zip')
    with zipfile.ZipFile(z, 'w') as zf:
        zf.write(pa, 'd88/disk_a.d88')
        zf.write(pb, 'd88/disk_b.d88')
        zf.writestr('readme.txt', 'not an image')
    imported('zip', z)
    rc, out = run('--inventory', inv, 'import', z + '::d88/disk_a.d88', '--out', os.path.join(tmp, 'out-member'))
    expect('zip member: incomplete -> exit 1', rc == 1 and 'XEN_01.M' in out, out)
    raw = os.path.join(tmp, 'disk_a.hdm')
    open(raw, 'wb').write(a)
    imported('hdm', raw, pb)

    # hard disks: the game in \XENON (an HDINST install), FAT12 with 1024-byte sectors and FAT16
    # with 512-byte ones
    for label, vol in (('fat12', fat_volume([('CONFIG.SYS', b'files=10\r\n')], 1024, 2000,
                                            subdir=('XENON', files))),
                       ('fat16', fat_volume([], 512, 9000, subdir=('XENON', files))),):
        p = os.path.join(tmp, 'hdd-%s.hdi' % label)
        open(p, 'wb').write(hdi(vol))
        imported('hdi-' + label, p)
        p = os.path.join(tmp, 'hdd-%s.nhd' % label)
        open(p, 'wb').write(nhd(vol))
        imported('nhd-' + label, p)
    vol = fat_volume([], 1024, 2000, subdir=('XENON', files))
    p = os.path.join(tmp, 'plain.img')
    open(p, 'wb').write(vol)
    imported('offset', p, '--offset', '0')

    # a directory with lower-case names in a subdirectory, plus a stranger
    d = os.path.join(tmp, 'install', 'Xenon')
    os.makedirs(d)
    for name, _, data in FILES:
        open(os.path.join(d, name.lower()), 'wb').write(data)
    open(os.path.join(d, 'other.txt'), 'w').write('x')
    out_dir = imported('directory', os.path.join(tmp, 'install'))

    # check: complete; then a changed, a missing and a lower-case file
    game = os.path.join(out_dir, 'game')
    rc, out = run('--inventory', inv, 'check', '-q', game)
    expect('check complete', rc == 0 and out == '', out)
    bad = os.path.join(tmp, 'bad')
    shutil.copytree(game, bad)
    with open(os.path.join(bad, 'S00.CC'), 'ab') as f:
        f.write(b'!')
    os.remove(os.path.join(bad, 'XEN_01.M'))
    os.rename(os.path.join(bad, '0010.GDT'), os.path.join(bad, '0010.gdt'))
    rc, out = run('--inventory', inv, 'check', bad)
    expect('check: changed, missing, lower case -> exit 1', rc == 1 and 'S00.CC md5' in out
           and 'XEN_01.M' in out and 'named 0010.gdt' in out, out)
    # an image whose copy of a pack input differs: named, and the import fails
    changed = [(n, d + b'!' if n == 'S00.CC' else d) for n, d in files]
    p = os.path.join(tmp, 'changed.hdm')
    open(p, 'wb').write(fat_volume(changed, 1024, 1232))
    rc, out = run('--inventory', inv, 'import', p, '--out', os.path.join(tmp, 'out-changed'))
    expect('changed file -> exit 1, named', rc == 1 and 'differs: S00.CC' in out, out)
    rc, out = run('--inventory', inv, 'import', os.path.join(tmp, 'inventory.tsv'),
                  '--out', os.path.join(tmp, 'out-junk'))
    expect('not an image -> error', rc != 0 and 'no FAT file system' in out, out)

    print('check_import: %d failures' % fails)
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
