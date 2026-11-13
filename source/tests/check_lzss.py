#!/usr/bin/env python3
"""Compare the C LZSS decoder with tools/lzss.py on every .CC file: check_lzss.py BIN GAMEDIR"""
import glob
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'tools'))
import lzss   # noqa: E402


def main():
    binary, gamedir = sys.argv[1], sys.argv[2]
    fails = 0
    files = sorted(glob.glob(os.path.join(gamedir, '*.CC')))
    for f in files:
        want = lzss.unpack_file(open(f, 'rb').read())
        r = subprocess.run([binary, f], capture_output=True)
        if r.returncode != 0 or r.stdout != want:
            fails += 1
            print('FAIL %s: %s' % (f, r.stderr.decode().strip() or 'output differs'))
    print('check_lzss: %d files, %d failures' % (len(files), fails))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
