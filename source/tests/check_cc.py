#!/usr/bin/env python3
"""Script VM checks: check_cc.py BIN GAMEDIR TMPDIR

1. Static check of all decompressed scripts with the C checker; results must match
   re-tools/ccdis.py (pages, strings, unknown ops, coverage) and be clean.
2. Trace run of every script with the tracing host: no script error may occur and every
   message/op must be visited in token order (sanity of the interpreter's navigation).
"""
import glob
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'tools'))      # lzss
sys.path.insert(0, os.path.join(HERE, '..', '..', 're-tools'))   # ccdis
import lzss    # noqa: E402
import ccdis   # noqa: E402


def main():
    binary, gamedir, tmp = sys.argv[1:4]
    os.makedirs(tmp, exist_ok=True)
    fails = 0
    files = sorted(glob.glob(os.path.join(gamedir, '*.CC')))
    bins = []
    for f in files:
        data = lzss.unpack_file(open(f, 'rb').read())
        out = os.path.join(tmp, os.path.basename(f) + '.bin')
        open(out, 'wb').write(data)
        bins.append((f, out, data))

    r = subprocess.run([binary, 'check'] + [b[1] for b in bins], capture_output=True)
    lines = dict(line.split(' ', 1) for line in r.stdout.decode().splitlines())
    for f, out, data in bins:
        name = os.path.basename(out)
        got = dict(kv.split('=') for kv in lines[name].split())
        ast, p = ccdis.parse(data)
        missing, double = ccdis.coverage(data, ast)
        strings = []

        def count(n, ctx):
            if n['t'] == 'msg':
                strings.append(n)
            elif n['t'] == 'op':
                strings.extend(a for a in n['args'] if isinstance(a, bytes))
            elif n['t'] == 'menu':
                strings.extend(n['opts'])
        ccdis.walk(ast, count)
        want = dict(pages=sum(1 for n in ast if n['t'] == 'page'), unknown=len(p.unknown),
                    uncovered=len(missing), overlap=len(double), strings=len(strings), parse_error=0)
        bad = [k for k in want if int(got[k]) != want[k]]
        if bad or r.returncode != 0 and (int(got['parse_error']) or int(got['uncovered'])):
            fails += 1
            print('FAIL check %s: got %s want %s' % (name, {k: got[k] for k in want}, want))

    for f, out, data in bins:
        t = subprocess.run([binary, 'trace', out], capture_output=True)
        text = t.stdout.decode('cp932', errors='replace')
        if 'ERROR' in text or t.returncode != 0:
            fails += 1
            print('FAIL trace %s:\n%s' % (os.path.basename(f), text[-600:]))
    print('check_cc: %d scripts, %d failures' % (len(files), fails))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
