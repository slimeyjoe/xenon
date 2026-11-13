#!/usr/bin/env python3
"""savedump.py - decode Xenon (PC-98, AGS engine) save slots SAVEn.DAT and the XEN.GDT progress byte.

Usage:
    python3 -I re-tools/savedump.py SAVE1.DAT [SAVE2.DAT ...]          # human-readable dump
    python3 -I re-tools/savedump.py --json OUTDIR SAVE*.DAT             # one JSON file per save
    python3 -I re-tools/savedump.py --xen XEN.GDT                       # decode the completion bitmask

Layout (written by AGS.EXE 088D:126A, read back by 088D:1441; see docs/re/save-format.md):

    off   size  field
    0x000    1  magic 0x0B (loader rejects anything else)
    0x001   10  volume label of the disk in drive 2 at save time ("_ADISK_".."_EDISK_" + NUL + junk).
                Present ONLY when the engine runs in floppy mode ("AGS F"); absent in HDD mode,
                where the file is 381 bytes and every later offset is 10 lower.
    0x00B  256  flag block = DS:00F2..DS:01F1: 2048 one-bit flags, flag n = bit (n&7) of byte (n>>3)
    0x10B   40  current script file name, e.g. "s0305a.cc" (DS:137E)
    0x133    2  current page number (DS:1294), u16 LE - the load resumes at the START of this page
    0x135    2  bytecode offset of the save opcode (written but ignored by the loader)
    0x137   40  music state block (DS:13C8): name of current .M (byte 0 zeroed = "no music to restore")
    0x15F   40  graphics state block (DS:1422): name of current base GDT (byte 0 zeroed = "nothing to restore")
"""
import sys, json, os

def decode(data):
    if not data or data[0] != 0x0B:
        raise ValueError('not a save file (first byte must be 0x0B)')
    floppy = len(data) == 391
    if len(data) not in (381, 391):
        raise ValueError('unexpected size %d (381 = HDD mode, 391 = floppy mode)' % len(data))
    p = 1
    out = {'mode': 'floppy' if floppy else 'hdd', 'size': len(data)}
    if floppy:
        lab = data[p:p+10]; p += 10
        out['disk_label'] = lab.split(b'\0')[0].decode('ascii', 'replace')
        out['disk_label_raw'] = lab.hex()
    v = data[p:p+256]; p += 256
    out['vars_raw'] = v.hex()
    out['vars_nonzero'] = {('0x%02X' % i): b for i, b in enumerate(v) if b}
    # 2048 one-bit flags: flag n = bit (n & 7) of byte (n >> 3)  (bitmask8 088D:22C6)
    out['flags_set'] = [n for n in range(2048) if v[n >> 3] >> (n & 7) & 1]
    s = data[p:p+40]; p += 40
    out['script'] = s.split(b'\0')[0].decode('cp932', 'replace')
    out['page'] = int.from_bytes(data[p:p+2], 'little'); p += 2
    out['save_opcode_offset'] = int.from_bytes(data[p:p+2], 'little'); p += 2
    m = data[p:p+40]; p += 40
    g = data[p:p+40]; p += 40
    for key, blk in (('music', m), ('graphics', g)):
        name = blk.split(b'\0')[0] if blk[0] else b''
        # byte 0 is zeroed when there is nothing to restore; show the remnant for reference
        remnant = blk[1:].split(b'\0')[0]
        out[key] = {'restore': bool(blk[0]), 'name': name.decode('cp932', 'replace'),
                    'remnant_after_byte0': remnant.decode('cp932', 'replace'), 'raw': blk.hex()}
    assert p == len(data)
    return out

XEN_BITS = {
    0: 'bit0 (opcode 0x37 arg 1)', 1: 'bit1 (arg 2)', 2: 'bit2 (arg 3)', 3: 'bit3 (arg 4)',
    4: 'bit4 (arg 5)', 5: 'bit5 (arg 6)', 6: 'bit6 (arg 7)',
}

def decode_xen(b):
    v = b[0] if b else 0
    prologue = 's00' if not v & 1 else ('s00b' if (not v & 2 or not v & 0x3C) else 's00c')
    return {'value': v, 'bits_set': [XEN_BITS[i] for i in range(7) if v >> i & 1],
            'title_start_script': prologue, 'omake_unlocked': (v & 0x7F) == 0x7F}

def main(argv):
    if len(argv) >= 2 and argv[0] == '--xen':
        print(json.dumps(decode_xen(open(argv[1], 'rb').read()), ensure_ascii=False, indent=1)); return
    outdir = None
    if argv and argv[0] == '--json':
        outdir = argv[1]; argv = argv[2:]; os.makedirs(outdir, exist_ok=True)
    for fn in argv:
        d = decode(open(fn, 'rb').read())
        if outdir:
            base = os.path.splitext(os.path.basename(fn))[0]
            with open(os.path.join(outdir, base + '.json'), 'w', encoding='utf-8') as f:
                json.dump(d, f, ensure_ascii=False, indent=1)
        else:
            print('%s: %s mode, label=%s script=%s page=%d (opcode @%d) music=%r gfx=%r' % (
                fn, d['mode'], d.get('disk_label', '-'), d['script'], d['page'], d['save_opcode_offset'],
                d['music']['name'] or '(none; remnant %s)' % d['music']['remnant_after_byte0'],
                d['graphics']['name'] or '(none; remnant %s)' % d['graphics']['remnant_after_byte0']))
            print('   flags set:', ' '.join('F%d' % n for n in d['flags_set']))

if __name__ == '__main__':
    main(sys.argv[1:])
