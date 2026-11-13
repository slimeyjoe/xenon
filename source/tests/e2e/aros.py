#!/usr/bin/env python3
"""AROS presentation conformance in hosted AROS (tests/aros/aros.sh; README.md "AROS").

    aros.py present [--guest-args "WINDOW SCALE=2 ..."] [--name NAME]
        the synthetic presentation test (backends/common/ptest.h) on the AROS build, against
        the null backend's reference; --guest-args adds program options (the window and
        scale modes of backends/amiga/README.md "Window, scale, aspect": the image read back
        is then the window's inside or the scaled page, checked through amiga_oracle.expand)
    aros.py game --scenario NAME [--lang en] [--max-snaps N]
        replays an end-to-end scenario's input on the AROS build (AUDIO=none, FAST, an autoplay
        spec file) with snapshots at the scenario's snapshot points, against the null backend
        replaying the same spec

On AROS the guest reads the shown page back itself (backends/amiga/aros/snap_aros.c: the
true colour bitmap through ReadPixelArray, a 640x400 PPM per snapshot in SNAPDIR, which is a
host directory in hosted AROS), so every snapshot is checked two ways (xe2e/amiga_oracle.py):
the guest's frame hash against the reference's, and the image read back against the reference
frame's colours. Needs build/host/xenon, build/aros/xenon, build/xenon.pak, the hosted AROS of
tests/aros/README.md (AROS_DIR in tests/rig.conf), Python 3 with numpy and Pillow. The stage-B helpers (spec, reference)
are amiga.py's.
"""
import argparse
import glob
import json
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)
import amiga                                                # noqa: E402
from xe2e import amiga_oracle                               # noqa: E402
from xe2e.engine import frame_digest                        # noqa: E402

import rig                                                  # noqa: E402

ARGS_SH = os.path.join(SRC, 'tests', 'aros', 'aros.sh')
RUNS = rig.get('AROS_RUNS') or (os.path.join(rig.get('AROS_DIR'), 'run') if rig.get('AROS_DIR') else None)


def run_hosted(name, files, commands, timeout, log_path):
    """Runs commands in a fresh hosted AROS (aros.sh hosted NAME); returns the run directory."""
    if not RUNS:
        sys.exit('aros.py: set AROS_DIR (or AROS_RUNS) in tests/rig.conf or the environment '
                 '(tests/aros/README.md)')
    env = dict(os.environ, AROS_FILES=' '.join(files))
    with open(log_path, 'w') as log:
        subprocess.run([ARGS_SH, 'hosted', name, str(timeout)], input='\n'.join(commands) + '\n',
                       text=True, env=env, stdout=log, stderr=subprocess.STDOUT, check=False)
    return os.path.join(RUNS, name)


def manifest(snapdir):
    """Snapshot records from the guest's requests (snap.c: key=value words, image=)."""
    recs = []
    for req in sorted(glob.glob(os.path.join(snapdir, 'snap_*.req'))):
        with open(req) as f:
            words = dict(w.split('=', 1) for w in f.read().split() if '=' in w)
        rec = {'name': words.get('name'), 'hash': words.get('hash', 'none')}
        if 'sx' in words:                           # SCALE/ASPECT/WINDOW: the image is that large
            rec['sx'], rec['dsth'] = words['sx'], words['dsth']
        if 'image' in words:
            rec['png'] = words['image']             # a PPM; amiga_oracle reads it with Pillow
        else:
            rec['error'] = 'no image read back'
        recs.append(rec)
    return recs


def check(recs, snapdir, ref, out, label):
    problems, seen = [], set()
    for rec in recs:
        frame = ref.get(rec['name'])
        if frame is None:
            problems.append('%s %s: no reference snapshot' % (label, rec['name']))
            continue
        seen.add(rec['name'])
        # the image read back is exactly the 640x400 page: no border, at (0, 0)
        problems += amiga_oracle.check(frame, rec, snapdir, (0, 0), os.path.join(out, 'diff', label), label)
    missing = sorted(set(ref) - seen)
    if missing:
        problems.append('%s: %d snapshots missing (%s%s)' % (label, len(missing), ', '.join(missing[:5]),
                                                             ' ...' if len(missing) > 5 else ''))
    return len(seen), (0, 0), problems


def cmd_present(args):
    os.makedirs(args.out, exist_ok=True)
    ref_path = os.path.join(args.out, 'present-ref.jsonl')
    with open(ref_path, 'w') as f:
        subprocess.run([args.host_bin, '--present-test'], stdout=f, check=True)
    with open(ref_path) as f:
        ref = amiga.read_ref(f)
    ref = {amiga.present_key(fr.name, fr.tick): fr for fr in ref.values()}
    t0 = time.time()
    name = args.name or 'e2e-present'
    run = run_hosted(name, [args.aros_bin, args.pak],
                     ['!MakeDir Work:snaps',
                      'Work:xenon PRESENTTEST SNAPDIR=Work:snaps/ ' + args.guest_args],
                     args.timeout, os.path.join(args.out, name + '.log'))
    print('hosted AROS run: %.0f s' % (time.time() - t0))
    snapdir = os.path.join(run, 'AROS', 'Work', 'snaps')
    label = 'hosted' + ('-' + args.guest_args.replace(' ', '-') if args.guest_args else '')
    return amiga.report({label: check(manifest(snapdir), snapdir, ref, args.out, label)})


def cmd_game(args):
    tdir = os.path.join(args.out, 'transcripts')
    if not args.reuse_transcripts:
        r = subprocess.run([sys.executable, '-I', os.path.join(HERE, 'run.py'), '--bin', args.host_bin,
                            '--pak', args.pak, '--lang', args.lang, '--out', os.path.join(args.out, 'host-e2e'),
                            '--transcripts', tdir, '--no-logs', args.scenario])
        if r.returncode != 0:
            print('the scenario fails on the host; fix that first')
            return 1
    paths = [p for p in sorted(glob.glob(os.path.join(tdir, args.lang, '*.json')))
             if os.path.basename(p)[:-5] == args.scenario or os.path.basename(p).startswith(args.scenario + '-')]
    if not paths:
        print('no transcript for %s' % args.scenario)
        return 1
    rc = 0
    for path in paths:
        with open(path) as f:
            tr = json.load(f)
        game = tr['name']
        gdir = os.path.join(args.out, 'game')
        os.makedirs(gdir, exist_ok=True)
        spec_text, chosen, max_ticks = amiga.build_spec(tr, args.max_snaps)
        spec_path = os.path.join(gdir, game + '.spec')
        with open(spec_path, 'w') as f:
            f.write(spec_text)
        ref = amiga.host_reference(args, game, spec_path, max_ticks, tr['storage'])
        bad = []
        for t, (name, dig) in sorted(chosen.items()):
            fr = ref.get('t%d' % t)
            if fr is None:
                bad.append('t%d (%s): no reference snapshot' % (t, name))
            elif dig is not None and frame_digest(fr) != dig:
                bad.append('t%d (%s): host replay differs from the control-mode run' % (t, name))
        if bad:
            rc = 1
            print('FAIL %s: the autoplay replay diverges on the host' % game)
            for b in bad[:20]:
                print('    ' + b)
            continue
        # the saves the scenario starts from, as the game's PROGDIR:saves/
        stage = os.path.join(gdir, game + '.aros')
        saves = os.path.join(stage, 'saves')
        os.makedirs(saves, exist_ok=True)
        for fn in os.listdir(saves):
            os.unlink(os.path.join(saves, fn))
        for fn, hexdata in tr['storage'].items():
            with open(os.path.join(saves, fn), 'wb') as f:
                f.write(bytes.fromhex(hexdata))
        cmd = ('Work:xenon LANG=%s AUDIO=none FAST AUTOPLAY=@Work:%s.spec MAXTICKS=%d SNAPDIR=Work:snaps/'
               % (args.lang, game, max_ticks))
        t0 = time.time()
        run = run_hosted('e2e-game-' + game, [args.aros_bin, args.pak, spec_path, saves],
                         ['!MakeDir Work:snaps', cmd], args.timeout, os.path.join(gdir, game + '-aros.log'))
        print('%s: %d snapshots up to tick %d, hosted AROS run %.0f s' % (game, len(chosen), max_ticks,
                                                                           time.time() - t0))
        snapdir = os.path.join(run, 'AROS', 'Work', 'snaps')
        rc |= amiga.report({'hosted': check(manifest(snapdir), snapdir, ref, os.path.join(gdir, game), 'hosted')})
    return rc


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--host-bin', default=os.path.join(SRC, '..', 'build', 'host', 'xenon'))
    ap.add_argument('--aros-bin', default=os.path.join(SRC, '..', 'build', 'aros', 'xenon'))
    ap.add_argument('--pak', default=os.path.join(SRC, '..', 'build', 'xenon.pak'))
    ap.add_argument('--out', default=os.path.join(SRC, '..', 'build', 'aros', 'e2e'))
    ap.add_argument('--timeout', type=int, default=600, help='hosted AROS timeout per run, seconds')
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('present')
    p.add_argument('--guest-args', default='', help='more program options, e.g. "WINDOW"')
    p.add_argument('--name', help='run name (default e2e-present)')
    p = sub.add_parser('game')
    p.add_argument('--scenario', required=True)
    p.add_argument('--lang', default='en')
    p.add_argument('--max-snaps', type=int, default=40)
    p.add_argument('--reuse-transcripts', action='store_true')
    args = ap.parse_args()
    args.out = os.path.abspath(args.out)
    return cmd_present(args) if args.cmd == 'present' else cmd_game(args)


if __name__ == '__main__':
    sys.exit(main())
