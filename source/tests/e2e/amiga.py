#!/usr/bin/env python3
"""The Amiga build in WinUAE (README.md "Amiga in WinUAE"), on lrvne's rigs (lrvne/rigs/README.md).

    amiga.py present [--configs native,rtg,promote,uaegfx]
        stage A: the presentation test in each emulator configuration against the null
        backend's reference: lrvne's rigs/present.py winuae
    amiga.py game --scenario NAME [--lang en] [--configs native,...] [--max-snaps N]
        stage B: replays an end-to-end scenario's input on the Amiga build (AUDIO=none, FAST,
        an autoplay spec file) with snapshots at the scenario's snapshot points, against the
        null backend replaying the same spec
    amiga.py check --kind present|game --ref REF.jsonl RUN_DIR... (re-check finished runs)

The configurations are lrvne's (rigs/lrvrig/winuae.py: native, rtg, promote, uaegfx; "scaled"
and "window" name the groups). Each runs in its own work dir with its own gdb port, in
parallel, in warp (--real-time throttles them; the results are the same). A run is bounded by
--timeout, the one backstop, and one it ended is reported as such. One amiga.py at a time (a
lock in the rigs' work directory: a second one waits). Needs build/host/xenon,
build/amiga/xenon, build/xenon.pak, Python 3 with numpy and Pillow, and the machine settings
of lrvne/rigs/rig.conf.example (tests/rig.conf).
"""
import argparse
import fcntl
import glob
import json
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.abspath(os.path.join(HERE, '..', '..'))
RIGS = os.path.join(SRC, 'lrvne', 'rigs')
sys.path.insert(0, HERE)
sys.path.insert(0, RIGS)
from lrvrig import winuae                                   # noqa: E402
from xe2e.engine import Frame, frame_digest, KEYS, KEY_NAMES, HOLD_NAMES   # noqa: E402

GUEST_ARGS = ''                                             # --guest-args


def read_ref(lines):
    """Reference frames by snapshot key from the null backend's snap lines."""
    ref = {}
    for line in lines:
        d = json.loads(line)
        if d.get('ev') != 'snap':
            continue
        f = Frame(d)
        f.hash = d.get('hash')
        ref[d['name']] = f
    return ref


def present_key(name, index):
    return '%02d-%s' % (index, name)


# ---- stage A: lrvne's presentation test -----------------------------------------------------

def cmd_present(args):
    cmd = [sys.executable, '-I', os.path.join(RIGS, 'present.py'), 'winuae', '--host-bin', args.host_bin,
           '--program', args.amiga_bin, '--pak', args.pak, '--out', args.out, '--configs',
           ','.join(args.configs), '--port', str(args.port), '--timeout', str(args.timeout)]
    if args.real_time:
        cmd.append('--real-time')
    if args.guest_args:
        cmd += ['--guest-args', args.guest_args]
    return subprocess.run(cmd).returncode


# ---- stage B ----------------------------------------------------------------------------------

def build_spec(tr, max_snaps, max_scrolled=4):
    """Autoplay spec text, the snapshot ticks with their expected digests, and MAXTICKS for one
    game transcript (xe2e/engine.py Engine.transcript)."""
    presses = {}
    ranges = {'ctrl': [], 'instant': [], 'anykey': []}
    open_since = {}
    snaps = []
    for e in tr['transcript']:
        if e[0] == 'press':
            presses.setdefault(e[1], set()).update(e[2])
        elif e[0] == 'hold':
            _, t, level, on = e
            if on and level not in open_since:
                open_since[level] = t
            elif not on and level in open_since:
                ranges[level].append((open_since.pop(level), t))
        elif e[0] == 'mouse':
            raise ValueError('mouse input at tick %d cannot be replayed by autoplay' % e[1])
        elif e[0] == 'snap':
            snaps.append((e[1], e[2], e[3]))
    # every golden, then evenly spread image checks, then a few shake frames
    chosen = {}
    for t, name, dig in snaps:
        if name not in ('check', 'snap') and len(chosen) < max_snaps:
            chosen.setdefault(t, (name, dig))
    checks = [(t, n, d) for t, n, d in snaps if n in ('check', 'snap') and t not in chosen]
    room = max_snaps - len(chosen) - min(max_scrolled, len(tr.get('scrolled_ticks', [])))
    if room > 0 and checks:
        step = max(1, len(checks) // room)
        for t, n, d in checks[::step][:room]:
            chosen.setdefault(t, (n, d))
    for t in tr.get('scrolled_ticks', [])[:max_scrolled]:
        chosen.setdefault(t, ('shake', None))
    last = max(list(chosen) + [max(presses) if presses else 0])
    max_ticks = last + 2
    for level in open_since:
        ranges[level].append((open_since[level], max_ticks + 1))
    words = []
    # the transcript's commands as lrvne's key names, a chord in the order the engine's
    # keyboard handler gave its commands (CANCEL, the cursor, OK, HELP, f.1-f.5, other)
    if presses:
        words.append('at:' + ','.join('%d=%s' % (t, '+'.join(KEY_NAMES[c] for c in sorted(k, key=KEYS.index)))
                                      for t, k in sorted(presses.items())))
    for level, rs in ranges.items():
        if rs and level in HOLD_NAMES:      # anykey: a level the engine does not read
            words.append('%s:%s' % (HOLD_NAMES[level], ','.join('%d-%d' % r for r in rs)))
    words.append('snap:' + ','.join(str(t) for t in sorted(chosen)))
    text = '\n'.join(words) + '\n'
    return text, chosen, max_ticks


def host_reference(args, game, spec_path, max_ticks, storage):
    work = os.path.join(args.out, 'game', game + '.storage')
    os.makedirs(work, exist_ok=True)
    for fn in os.listdir(work):
        os.unlink(os.path.join(work, fn))
    for fn, hexdata in storage.items():
        with open(os.path.join(work, fn), 'wb') as f:
            f.write(bytes.fromhex(hexdata))
    r = subprocess.run([args.host_bin, '--pak', args.pak, '--lang', args.lang, '--storage', work,
                        '--max-ticks', str(max_ticks), '--autoplay', '@' + spec_path],
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    return read_ref(r.stdout.decode().splitlines())


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
    calib = winuae.load_calib(args.out)
    for cfg in args.configs:
        if cfg not in calib:
            print('note: no calibration for %s (run "present" first); using %s' % (cfg, winuae.DEFAULT_GEOM[cfg]))
    rc = 0
    port = args.port
    for path in paths:
        with open(path) as f:
            tr = json.load(f)
        game = tr['name']
        gdir = os.path.join(args.out, 'game')
        os.makedirs(gdir, exist_ok=True)
        spec_text, chosen, max_ticks = build_spec(tr, args.max_snaps)
        spec_path = os.path.join(gdir, game + '.spec')
        with open(spec_path, 'w') as f:
            f.write(spec_text)
        ref = host_reference(args, game, spec_path, max_ticks, tr['storage'])
        # the replay must reproduce the control-mode run on the host before it means anything
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
        saves = os.path.join(gdir, game + '.saves')
        os.makedirs(saves, exist_ok=True)
        for fn in os.listdir(saves):
            os.unlink(os.path.join(saves, fn))
        for fn, hexdata in tr['storage'].items():
            with open(os.path.join(saves, fn), 'wb') as f:
                f.write(bytes.fromhex(hexdata))
        xargs = 'LANG=%s AUDIO=none FAST AUTOPLAY=@PROGDIR:%s.spec MAXTICKS=%d' % (args.lang, game, max_ticks)
        procs, dirs = [], {}
        for cfg in args.configs:
            run_name = 'game-%s-%s' % (game, cfg)
            dirs[cfg] = winuae.snapdir(run_name)
            procs.append(winuae.start_run(args.amiga_bin, cfg, run_name, port, xargs + GUEST_ARGS,
                                          args.timeout, os.path.join(gdir, run_name + '.log'),
                                          guest_files=[args.pak, spec_path], saves=saves,
                                          saves_dir='saves/xenon', warp=not args.real_time))
            port += 1
        t0 = time.time()
        winuae.wait_runs(procs)
        print('%s: %d snapshots up to tick %d, emulator runs %.0f s' % (game, len(chosen), max_ticks, time.time() - t0))
        results = {}
        for cfg in args.configs:
            results[cfg] = winuae.check_run('game', cfg, dirs[cfg], ref, os.path.join(gdir, game), calib)
        rc |= winuae.report(results)
    return rc


def cmd_check(args):
    with open(args.ref) as f:
        ref = read_ref(f)
    if args.kind == 'present':
        ref = {present_key(fr.name, fr.tick): fr for fr in ref.values()}
    calib = winuae.load_calib(args.out)
    results = {}
    for d in args.runs:
        run = os.path.basename(os.path.dirname(os.path.abspath(d)))
        cfg = next((c for c in winuae.ALL if run.endswith('-' + c)), 'native')
        results[cfg] = winuae.check_run(args.kind, cfg, d, ref, args.out, calib)
    if args.kind == 'present':
        winuae.save_calib(args.out, calib)
    return winuae.report(results)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--host-bin', default=os.path.join(SRC, '..', 'build', 'host', 'xenon'))
    ap.add_argument('--amiga-bin', default=os.path.join(SRC, '..', 'build', 'amiga', 'xenon'))
    ap.add_argument('--pak', default=os.path.join(SRC, '..', 'build', 'xenon.pak'))
    ap.add_argument('--out', default=os.path.join(SRC, '..', 'build', 'amiga', 'e2e'))
    ap.add_argument('--port', type=int, default=2351, help='first gdb port (one per configuration)')
    ap.add_argument('--timeout', type=int, default=600, help='emulator timeout per run, seconds')
    ap.add_argument('--guest-args', default='',
                    help='more program arguments for every run, e.g. "UPLOAD=chunky"')
    ap.add_argument('--real-time', action='store_true',
                    help='throttled emulation (default: warp, the results are the same)')
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('present')
    p.add_argument('--configs', default=','.join(winuae.CONFIGS))
    p = sub.add_parser('game')
    p.add_argument('--scenario', required=True)
    p.add_argument('--lang', default='en')
    p.add_argument('--configs', default='native')
    p.add_argument('--max-snaps', type=int, default=40)
    p.add_argument('--warp', action='store_true', help=argparse.SUPPRESS)   # the default now
    p.add_argument('--reuse-transcripts', action='store_true')
    p = sub.add_parser('check')
    p.add_argument('--kind', choices=('present', 'game'), required=True)
    p.add_argument('--ref', required=True)
    p.add_argument('runs', nargs='+')
    args = ap.parse_args()
    global GUEST_ARGS
    GUEST_ARGS = (' ' + args.guest_args) if args.guest_args else ''
    args.out = os.path.abspath(args.out)
    os.makedirs(args.out, exist_ok=True)
    if hasattr(args, 'configs'):
        try:
            args.configs = winuae.configs(args.configs)
        except ValueError as e:
            ap.error(str(e))
    if args.cmd in ('present', 'game'):
        # the runs' work dirs and ports are fixed: one amiga.py at a time
        os.makedirs(winuae.WORKTOP, exist_ok=True)
        lock = open(os.path.join(winuae.WORKTOP, '.xenon-amiga-e2e.lock'), 'w')
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            print('another amiga.py is running: waiting for it to end', flush=True)
            fcntl.flock(lock, fcntl.LOCK_EX)
        return cmd_present(args) if args.cmd == 'present' else cmd_game(args)
    if args.cmd == 'check':
        return cmd_check(args)
    return 2


if __name__ == '__main__':
    sys.exit(main())
