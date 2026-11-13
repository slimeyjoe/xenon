#!/usr/bin/env python3
"""Snapshot server for WinUAE runs of the Amiga build (plan §10; started by
tests/amiga/run-game.sh with SNAPD=1). It watches the guest directory for snap_<name>.req
files (backends/amiga/snap.c), and for each one attaches to the WinUAE gdbserver, takes a
screenshot, reads the displayed screen bitmap from guest memory, detaches and acknowledges.

Output in --out: <name>.png (screenshot), <name>.bitmap.npy (colour indices read from guest
memory, absent when the bitmap cannot be located) and manifest.jsonl (one JSON object per
snapshot: the request's fields plus file names and errors).

    amiga_snapd.py --port 2345 --dir WORK/dh0/Xenon --out WORK/snaps
"""
import argparse
import glob
import json
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import numpy as np                                          # noqa: E402
from xe2e import uae                                        # noqa: E402


def handle(req_path, args):
    with open(req_path) as f:
        line = f.read().strip()
    d = uae.parse_request(line)
    name = d.get('name', os.path.basename(req_path)[5:-4])
    rec = dict(d)
    png = os.path.join(args.out, name + '.png')
    g = uae.Gdb(args.port)
    try:
        g.monitor('screenshot ' + png)
        rec['png'] = os.path.basename(png)
        rows = int(d.get('rows', '400'))
        rows = min(rows, 400)
        # SCALE/ASPECT (snap.c): the page is dsth rows of 640 * sx pixels at dx,dy
        width, dx, dy = 640, 0, 0
        if 'sx' in d:
            width, rows = 640 * int(d['sx']), int(d['dsth'])
            dx, dy = int(d['dx']), int(d['dy'])
        try:
            if d.get('standard') == '1':
                bpr = int(d['bpr'])
                planes = [int(x, 16) + dy * bpr + dx // 8 for x in d['planes'].split(',')]
                lo = min(planes)
                hi = max(planes) + bpr * (rows - 1) + width // 8
                mem = g.read(lo, hi - lo)
                idx = uae.planar_indices(mem, lo, planes, bpr, rows, width)
            elif 'p96mem' in d and d.get('p96fmt', '1') != '1':
                idx = None                          # a true colour screen: no pens to read
                rec['bitmap_skipped'] = 'p96fmt %s' % d['p96fmt']
            elif 'p96mem' in d:
                bpr = int(d['p96bpr'])
                mem = g.read(int(d['p96mem'], 16) + dy * bpr + dx, bpr * (rows - 1) + width)
                idx = uae.chunky_indices(mem, bpr, rows, width)
            else:
                idx = None
                rec['bitmap_error'] = 'no planar bitmap and no Picasso96API'
            if idx is not None:
                npy = os.path.join(args.out, name + '.bitmap.npy')
                np.save(npy, idx)
                rec['bitmap'] = os.path.basename(npy)
        except (uae.GdbError, ValueError, KeyError) as e:
            rec['bitmap_error'] = str(e)
    finally:
        g.detach()
    rec['host_time'] = time.time()
    with open(os.path.join(args.out, 'manifest.jsonl'), 'a') as f:
        f.write(json.dumps(rec) + '\n')
    os.unlink(req_path)
    ack = req_path[:-4] + '.ack'
    open(ack, 'w').close()
    print('snapd: %s' % name, flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', type=int, required=True)
    ap.add_argument('--dir', required=True, help='host directory the guest writes requests into')
    ap.add_argument('--out', required=True)
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    while True:
        for req in sorted(glob.glob(os.path.join(args.dir, 'snap_*.req'))):
            time.sleep(0.2)
            try:
                handle(req, args)
            except Exception as e:                          # keep serving; the guest times out
                print('snapd: %s: %s' % (req, e), file=sys.stderr, flush=True)
                with open(os.path.join(args.out, 'manifest.jsonl'), 'a') as f:
                    f.write(json.dumps({'req': os.path.basename(req), 'error': str(e)}) + '\n')
                try:
                    os.unlink(req)
                    open(req[:-4] + '.ack', 'w').close()
                except OSError:
                    pass
        time.sleep(0.1)


if __name__ == '__main__':
    main()
