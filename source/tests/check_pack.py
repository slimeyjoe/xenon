#!/usr/bin/env python3
"""Build a pack from GAMEDIR, read it back through the C reader (bare and bundled behind a
trailer) and compare every entry with its source file. usage: check_pack.py BIN GAMEDIR TMPDIR"""
import glob
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'tools'))
import xpak   # noqa: E402
import lzss   # noqa: E402
import cctok  # noqa: E402

VOICEMAP = os.path.join(HERE, '..', '..', 'resources', 'voice', 'voicemap.tsv')


def check_voice_cues(entries):
    """Every Japanese cue lands on a string token whose text has the crc voicemap.tsv names
    (CRC-32 of the token's Shift-JIS bytes, re-tools/voicemap.py)."""
    import csv
    import struct
    import zlib
    want = {}
    with open(VOICEMAP, encoding='utf-8') as f:
        for r in csv.DictReader(f, delimiter='\t'):
            want[(r['script'].upper(), int(r['offset'], 16), int(r['part']), r['clip'])] = r['crc']
    scripts = {e['name']: e['data'] for e in entries if e['type'] == xpak.T_SCRIPT and e['lang'] == xpak.L_JA}
    got, fails = 0, 0
    for e in entries:
        if e['type'] != xpak.T_VOICECUE:
            continue
        d = e['data']
        if d[:4] != b'VCUE' or struct.unpack_from('<H', d, 4)[0] != 1:
            print('FAIL voicecue %s: bad header' % e['name'])
            fails += 1
            continue
        strs = {t.pos: t.val for t in cctok.tokenize(scripts[e['name']]) if t.kind == 'str'}
        for i in range(struct.unpack_from('<H', d, 6)[0]):
            off, part, clip = struct.unpack_from('<IB3x12s', d, 8 + 20 * i)
            key = (e['name'], off, part, clip.rstrip(b'\0').decode())
            crc = want.get(key)
            if off not in strs or crc is None or '%08x' % zlib.crc32(strs[off]) != crc:
                print('FAIL voicecue %s %04X %s' % (key[0], key[1], key[3]))
                fails += 1
            got += 1
    if got != len(want):
        print('FAIL voicecue: %d cues packed, voicemap has %d' % (got, len(want)))
        fails += 1
    return fails


def main():
    binary, gamedir, tmp = sys.argv[1:4]
    os.makedirs(tmp, exist_ok=True)
    pak = os.path.join(tmp, 'test.pak')
    subprocess.run([sys.executable, '-I', os.path.join(HERE, '..', 'tools', 'mkpack.py'),
                    '--game', gamedir, '--lang-ja', '--voicemap', VOICEMAP, '-o', pak],
                   check=True, stdout=subprocess.DEVNULL)
    data = open(pak, 'rb').read()
    # bundled variant: 1000 bytes of fake executable, the pack, the trailer
    bundled = os.path.join(tmp, 'test.exe')
    with open(bundled, 'wb') as fo:
        fo.write(b'\x00\x00\x03\xf3' + b'E' * 996 + data + xpak.trailer(len(data)))

    fails = 0
    entries = xpak.read(data)
    for path in (pak, bundled):
        r = subprocess.run([binary, path, 'list'], capture_output=True, check=True)
        lines = r.stdout.decode().splitlines()
        if len(lines) != len(entries):
            print('FAIL %s: list has %d entries, expected %d' % (path, len(lines), len(entries)))
            fails += 1
            continue
        for e in entries:
            r = subprocess.run([binary, path, 'get', str(e['lang']), str(e['type']), e['name'].lower()],
                               capture_output=True)
            if r.returncode != 0 or r.stdout != e['data']:
                print('FAIL %s: entry %s differs' % (path, e['name']))
                fails += 1
    # sources
    for f in glob.glob(os.path.join(gamedir, '*.GDT')):
        d = open(f, 'rb').read()
        if len(d) < 24:
            continue
        name = os.path.splitext(os.path.basename(f))[0].upper()
        want = [e for e in entries if e['name'] == name and e['type'] == xpak.T_GDT]
        if not want or want[0]['data'] != d:
            print('FAIL source mismatch %s' % name)
            fails += 1
    for f in glob.glob(os.path.join(gamedir, '*.CC')):
        name = os.path.splitext(os.path.basename(f))[0].upper()
        want = [e for e in entries if e['name'] == name and e['type'] == xpak.T_SCRIPT and e['lang'] == xpak.L_JA]
        if not want or want[0]['data'] != lzss.unpack_file(open(f, 'rb').read()):
            print('FAIL script mismatch %s' % name)
            fails += 1
    # language fallback: lang 2 (en, absent) must fall back to neutral entries
    r = subprocess.run([binary, pak, 'get', '2', str(xpak.T_GDT), 'waku'], capture_output=True)
    if r.returncode != 0:
        print('FAIL language fallback')
        fails += 1
    fails += check_voice_cues(entries)
    print('check_pack: %d entries x 2 sources, %d failures' % (len(entries), fails))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
