#!/usr/bin/env python3
"""D88 floppy image and Anex86 HDI inspection / conversion (stdlib only).

Usage:
  python3 -I tools/d88.py info  IMAGE.d88            # header, track table, sector stats, FAT BPB
  python3 -I tools/d88.py raw   IMAGE.d88 OUT.img    # sector dump in C/H/R order (for mtools)
  python3 -I tools/d88.py hdi   IMAGE.hdi            # HDI header, PC-98 partition table, BPB, mtools offset

IMAGE.d88 may also be given as  ARCHIVE.zip::member/path.d88  to read straight from a zip
without extracting it. Images are only read, never modified.

The raw output can be read with mtools, e.g.
  MTOOLS_SKIP_CHECK=1 mdir -i OUT.img -a ::/
  MTOOLS_SKIP_CHECK=1 mcopy -n -m -i OUT.img '::/*' DESTDIR/
For an HDI the 'hdi' command prints the byte offset of the DOS partition to use as
  mcopy -i IMAGE.hdi@@OFFSET ...
See docs/re/media.md.
"""
import struct
import sys
import zipfile
from collections import Counter


def read_image(spec):
    if '::' in spec:
        zpath, member = spec.split('::', 1)
        with zipfile.ZipFile(zpath) as z:
            return z.read(member)
    with open(spec, 'rb') as f:
        return f.read()


def d88_parse(d):
    hdr = {
        'name': d[0:17].split(b'\0')[0].decode('cp932', 'replace'),
        'write_protect': d[0x1A],
        'media': d[0x1B],
        'disk_size': struct.unpack_from('<I', d, 0x1C)[0],
    }
    offs = struct.unpack_from('<164I', d, 0x20)
    tracks = []
    for ti, o in enumerate(offs):
        if not o:
            continue
        p = o
        nsec = struct.unpack_from('<H', d, p + 4)[0]
        secs = []
        for _ in range(nsec):
            c, h, r, n = d[p:p + 4]
            ns, dens, dele, st = struct.unpack_from('<HBBB', d, p + 4)
            size = struct.unpack_from('<H', d, p + 14)[0]
            secs.append(dict(c=c, h=h, r=r, n=n, nsec=ns, density=dens, deleted=dele,
                             status=st, size=size, data=d[p + 16:p + 16 + size]))
            p += 16 + size
        tracks.append((ti, o, secs))
    return hdr, tracks


def bpb(b):
    f = struct.unpack_from('<HBHBHHBHHH', b, 11)
    keys = ('bytes_per_sector', 'sectors_per_cluster', 'reserved', 'fats', 'root_entries',
            'total_sectors', 'media', 'sectors_per_fat', 'sectors_per_track', 'heads')
    r = dict(zip(keys, f))
    r['oem'] = b[3:11].decode('ascii', 'replace')
    if b[0x26] == 0x29:
        r['serial'] = '%08X' % struct.unpack_from('<I', b, 0x27)[0]
        r['bpb_label'] = b[0x2B:0x36].decode('ascii', 'replace')
        r['fs_type'] = b[0x36:0x3E].decode('ascii', 'replace')
    return r


def cmd_info(spec):
    d = read_image(spec)
    hdr, tracks = d88_parse(d)
    print('header:', hdr, 'file length', len(d), 'tracks used', len(tracks),
          'first track at 0x%X' % tracks[0][1])
    st = Counter()
    for ti, o, secs in tracks:
        for s in secs:
            st[(s['n'], s['nsec'], s['density'], s['deleted'], s['status'], s['size'])] += 1
            if (s['c'], s['h']) != (ti // 2, ti % 2):
                st['C/H differs from track index'] += 1
        if [s['r'] for s in secs] != sorted(s['r'] for s in secs):
            st['interleaved track'] += 1
    print('sector headers (N, sectors/track, density, deleted, status, size): count')
    for k, v in st.items():
        print('  ', k, v)
    raw = to_raw(tracks)
    print('raw size', len(raw))
    print('BPB:', bpb(raw[:512]))


def to_raw(tracks):
    out = bytearray()
    for ti, o, secs in tracks:
        for s in sorted(secs, key=lambda s: s['r']):
            out += s['data']
    return bytes(out)


def cmd_raw(spec, outp):
    _, tracks = d88_parse(read_image(spec))
    with open(outp, 'wb') as f:
        f.write(to_raw(tracks))


def cmd_hdi(path):
    with open(path, 'rb') as f:
        h = f.read(4096)
        names = ('reserved', 'hdd_type', 'header_size', 'data_size', 'sector_size',
                 'sectors', 'heads', 'cylinders')
        hd = dict(zip(names, struct.unpack_from('<8I', h, 0)))
        print('HDI header:', hd)
        base = hd['header_size']
        ss = hd['sector_size']
        f.seek(base + ss)  # partition table = physical sector 1
        pt = f.read(512)
        spc = hd['sectors'] * hd['heads']
        for i in range(16):
            e = pt[i * 32:i * 32 + 32]
            if not any(e):
                continue
            sc = struct.unpack_from('<H', e, 10)[0]
            ec = struct.unpack_from('<H', e, 14)[0]
            print('partition %d: mid %02X sid %02X ipl CHS %d/%d/%d start CHS %d/%d/%d '
                  'end CHS %d/%d/%d name %r' % (
                      i, e[0], e[1], struct.unpack_from('<H', e, 6)[0], e[5], e[4],
                      sc, e[9], e[8], ec, e[13], e[12],
                      e[16:32].decode('cp932', 'replace')))
            lba = sc * spc + e[9] * hd['sectors'] + e[8]
            off = base + lba * ss
            f.seek(off)
            print('  start LBA', lba, 'mtools offset', off, 'BPB:', bpb(f.read(512)))


def main(a):
    if len(a) >= 2 and a[0] == 'info':
        cmd_info(a[1])
    elif len(a) >= 3 and a[0] == 'raw':
        cmd_raw(a[1], a[2])
    elif len(a) >= 2 and a[0] == 'hdi':
        cmd_hdi(a[1])
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
