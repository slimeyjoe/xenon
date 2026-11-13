#!/usr/bin/env python3
"""Amiga presentation conformance in WinUAE (plan §10; README.md "Amiga in WinUAE").

    amiga.py present [--configs native,rtg,promote]
        stage A: the synthetic presentation test (backends/common/ptest.h) on the Amiga
        build in each emulator configuration, against the null backend's reference
    amiga.py game --scenario NAME [--lang en] [--configs native,...] [--max-snaps N]
        stage B: replays an end-to-end scenario's input on the Amiga build (AUDIO=none, FAST,
        an autoplay spec file) with snapshots at the scenario's snapshot points, against the
        null backend replaying the same spec
    amiga.py check --kind present|game --ref REF.jsonl RUN_DIR... (re-check finished runs)

Configurations: native (AGA HiRes Laced, no Picasso96), rtg (Picasso96 loaded, the game on
the native mode), promote (FAKENATIVEMODES: the HiRes Laced mode promoted to a uaegfx screen),
uaegfx (Picasso96 loaded, MODE= the uaegfx 640x480x8 board mode). Not in the default set, the
SCALE/ASPECT screens (backends/amiga/README.md "Window, scale, aspect"): native-aspect (ASPECT
on PAL HiRes Laced: 640x480), promote-aspect (the promoted mode, 640x480), uaegfx-x2 (SCALE=2:
1280x800 on a board mode), uaegfx-x2a (SCALE=2 ASPECT: 1280x960); --configs scaled runs those.
And the Workbench window (WINDOW; --configs window): wb-rtg8 (800x600 8-bit Workbench), wb-rtg16
(1024x768 16-bit, ASPECT: 640x480), wb-rtg32x2 (1280x1024 32-bit, SCALE=2: a borderless
1280x800 window), wb-aga256 (PAL HiRes Laced, 256 colours: a borderless window), wb-aga8 (8
colours: no free pens, the game must open its own screen). A window's image position is the
screen's (the configuration's default) plus the window's inside as the request gives it; what
lies around it is the Workbench, not checked.
Each runs tests/amiga/run-game.sh in its own work dir with its own gdb port, in parallel.
Needs build/host/xenon, build/amiga/xenon, build/xenon.pak, Python 3 with numpy and Pillow,
and the WinUAE rig of tests/amiga/README.md (its settings: tests/rig.conf).
"""
import argparse
import glob
import json
import os
import signal
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..'))
import rig                                                  # noqa: E402
from xe2e import amiga_oracle                               # noqa: E402
from xe2e.engine import Frame, frame_digest                 # noqa: E402

CONFIGS = ('native', 'rtg', 'promote', 'uaegfx')
SCALED = ('native-aspect', 'promote-aspect', 'uaegfx-x2', 'uaegfx-x2a')
WINDOWED = ('wb-rtg8', 'wb-rtg16', 'wb-rtg32x2', 'wb-aga256', 'wb-aga8')
# run-game.sh variant and extra program arguments per configuration
VARIANT = {'native': ('native', ''), 'rtg': ('rtg', ''), 'promote': ('promote', ''),
           'uaegfx': ('rtg', ' MODE=0x50031000'),
           'native-aspect': ('native', ' ASPECT'), 'promote-aspect': ('promote', ' ASPECT'),
           'uaegfx-x2': ('rtg', ' SCALE=2'), 'uaegfx-x2a': ('rtg', ' SCALE=2 ASPECT'),
           'wb-rtg8': ('rtg', ' WINDOW'), 'wb-rtg16': ('rtg', ' WINDOW ASPECT'),
           'wb-rtg32x2': ('rtg', ' WINDOW SCALE=2'), 'wb-aga256': ('native', ' WINDOW'),
           'wb-aga8': ('native', ' WINDOW')}
# the Workbench's mode and depth (run-game.sh WB=) of the window configurations
WB_MODE = {'wb-rtg8': '0x50041000:8', 'wb-rtg16': '0x50051100:16', 'wb-rtg32x2': '0x50071303:24',
           'wb-aga256': '0x29004:8', 'wb-aga8': '0x29004:3'}
WORKTOP = os.path.join(rig.get('EMU', os.path.expanduser('~/amiga-emu')), 'aos', 'work', 'xenon')
GUEST_ARGS = ''                                             # --guest-args
DEFAULT_GEOM = {'native': (76, 39), 'rtg': (76, 39), 'promote': (0, 0), 'uaegfx': (0, 0),
                'native-aspect': (76, 39), 'promote-aspect': (0, 0), 'uaegfx-x2': (0, 112),
                'uaegfx-x2a': (0, 32),
                'wb-rtg8': (0, 0), 'wb-rtg16': (0, 0), 'wb-rtg32x2': (0, 0), 'wb-aga256': (76, 39),
                'wb-aga8': (76, 39)}


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


# ---- emulator runs ----------------------------------------------------------------------------

def start_run(cfg, run_name, port, xenon_args, timeout, log_path, guest_files=(), amiga_bin=None, warp=False,
              saves=None):
    env = dict(os.environ)
    variant, extra = VARIANT[cfg]
    env.update(RUN=run_name, GDB_PORT=str(port), SNAPD='1',
               XENON_ARGS=xenon_args + extra + GUEST_ARGS, EMU_TIMEOUT=str(timeout))
    if cfg in WB_MODE:
        env['WB'] = WB_MODE[cfg]
    if guest_files:
        env['GUEST_FILES'] = ' '.join(guest_files)
    if amiga_bin:
        env['AMIGA_BIN'] = amiga_bin
    if warp:
        env['WARP'] = '1'
    if saves:
        env['GUEST_SAVES'] = saves
    log = open(log_path, 'w')
    p = subprocess.Popen([os.path.join(SRC, 'tests', 'amiga', 'run-game.sh'), variant], env=env,
                         stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
    return p


def wait_runs(procs):
    try:
        for p in procs:
            p.wait()
    except BaseException:
        # run-game.sh kills its emulator on TERM (run-lib.sh traps)
        for p in procs:
            if p.poll() is None:
                os.killpg(p.pid, signal.SIGTERM)
        for p in procs:
            try:
                p.wait(timeout=20)
            except subprocess.TimeoutExpired:
                os.killpg(p.pid, signal.SIGKILL)
        raise


# ---- checking ----------------------------------------------------------------------------------

def check_run(kind, cfg, run_dir, ref, out, calib):
    recs = amiga_oracle.load_manifest(run_dir)
    problems = []
    geom = calib.get(cfg)
    reg = [r for r in recs if r.get('name', '').startswith('00-')]
    if any(r.get('window') == '1' for r in recs):
        # a window: the screen's position (no registration image on a Workbench); the
        # oracle adds the window's inside from each request
        geom = DEFAULT_GEOM[cfg]
    elif kind == 'present':
        if reg and 'png' in reg[0]:
            try:
                sx, dst_h = amiga_oracle.scale_of(reg[0])
                geom = amiga_oracle.calibrate(amiga_oracle.load_rgb(os.path.join(run_dir, reg[0]['png'])),
                                              640 * sx, dst_h)
                calib[cfg] = geom
            except ValueError as e:
                problems.append('%s: calibration: %s' % (cfg, e))
    if geom is None:
        geom = DEFAULT_GEOM[cfg]
        problems.append('%s: no calibration (run "present" first), using %s' % (cfg, geom))
    seen = set()
    for rec in recs:
        name = rec.get('name')
        if name is None:
            problems += amiga_oracle.check(None, rec, run_dir, geom, None, cfg)
            continue
        frame = ref.get(name)
        if frame is None:
            problems.append('%s %s: no reference snapshot' % (cfg, name))
            continue
        seen.add(name)
        problems += amiga_oracle.check(frame, rec, run_dir, geom, os.path.join(out, 'diff', cfg), cfg)
    missing = sorted(set(ref) - seen)
    if missing:
        problems.append('%s: %d snapshots missing (%s%s)' % (cfg, len(missing), ', '.join(missing[:5]),
                                                             ' ...' if len(missing) > 5 else ''))
    return len(seen), geom, problems


def report(results):
    failed = 0
    for cfg, (n, geom, problems) in results.items():
        if problems:
            failed += 1
            print('FAIL %s (%d snapshots, image at %s)' % (cfg, n, geom))
            for p in problems[:60]:
                print('    ' + p)
            if len(problems) > 60:
                print('    ... %d more' % (len(problems) - 60))
        else:
            print('PASS %s (%d snapshots, image at %s)' % (cfg, n, geom))
    return 1 if failed else 0


def load_calib(out):
    path = os.path.join(out, 'calibration.json')
    if os.path.exists(path):
        with open(path) as f:
            return {k: tuple(v) for k, v in json.load(f).items()}
    return {}


def save_calib(out, calib):
    with open(os.path.join(out, 'calibration.json'), 'w') as f:
        json.dump(calib, f, indent=1)


# ---- stage A ----------------------------------------------------------------------------------

def cmd_present(args):
    os.makedirs(args.out, exist_ok=True)
    ref_path = os.path.join(args.out, 'present-ref.jsonl')
    with open(ref_path, 'w') as f:
        subprocess.run([args.host_bin, '--present-test'], stdout=f, check=True)
    with open(ref_path) as f:
        ref = read_ref(f)
    ref = {present_key(fr.name, fr.tick): fr for fr in ref.values()}
    procs, dirs = [], {}
    for i, cfg in enumerate(args.configs):
        run_name = 'present-' + cfg
        dirs[cfg] = os.path.join(WORKTOP, run_name, 'snaps')
        procs.append(start_run(cfg, run_name, args.port + i, 'PRESENTTEST', args.timeout,
                               os.path.join(args.out, run_name + '.log'), amiga_bin=args.amiga_bin))
    t0 = time.time()
    wait_runs(procs)
    print('emulator runs: %.0f s' % (time.time() - t0))
    calib = load_calib(args.out)
    results = {cfg: check_run('present', cfg, dirs[cfg], ref, args.out, calib) for cfg in args.configs}
    save_calib(args.out, calib)
    return report(results)


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
    if presses:
        words.append('at:' + ','.join('%d=%s' % (t, '+'.join(sorted(k))) for t, k in sorted(presses.items())))
    for level, rs in ranges.items():
        if rs:
            words.append('%s:%s' % (level, ','.join('%d-%d' % r for r in rs)))
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
    calib = load_calib(args.out)
    for cfg in args.configs:
        if cfg not in calib:
            print('note: no calibration for %s (run "present" first); using %s' % (cfg, DEFAULT_GEOM[cfg]))
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
            dirs[cfg] = os.path.join(WORKTOP, run_name, 'snaps')
            procs.append(start_run(cfg, run_name, port, xargs, args.timeout,
                                   os.path.join(gdir, run_name + '.log'), guest_files=[spec_path],
                                   amiga_bin=args.amiga_bin, warp=args.warp, saves=saves))
            port += 1
        t0 = time.time()
        wait_runs(procs)
        print('%s: %d snapshots up to tick %d, emulator runs %.0f s' % (game, len(chosen), max_ticks, time.time() - t0))
        results = {}
        for cfg in args.configs:
            results[cfg] = check_run('game', cfg, dirs[cfg], ref, os.path.join(gdir, game), calib)
        rc |= report(results)
    return rc


def cmd_check(args):
    with open(args.ref) as f:
        ref = read_ref(f)
    if args.kind == 'present':
        ref = {present_key(fr.name, fr.tick): fr for fr in ref.values()}
    calib = load_calib(args.out)
    results = {}
    for d in args.runs:
        run = os.path.basename(os.path.dirname(os.path.abspath(d)))
        cfg = next((c for c in CONFIGS + SCALED + WINDOWED if run.endswith('-' + c)), 'native')
        results[cfg] = check_run(args.kind, cfg, d, ref, args.out, calib)
    if args.kind == 'present':
        save_calib(args.out, calib)
    return report(results)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--host-bin', default=os.path.join(SRC, '..', 'build', 'host', 'xenon'))
    ap.add_argument('--amiga-bin', default=None, help='default: build/amiga/xenon')
    ap.add_argument('--pak', default=os.path.join(SRC, '..', 'build', 'xenon.pak'))
    ap.add_argument('--out', default=os.path.join(SRC, '..', 'build', 'amiga', 'e2e'))
    ap.add_argument('--port', type=int, default=2351, help='first gdb port (one per configuration)')
    ap.add_argument('--timeout', type=int, default=600, help='emulator timeout per run, seconds')
    ap.add_argument('--guest-args', default='',
                    help='more program arguments for every run, e.g. "UPLOAD=chunky"')
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('present')
    p.add_argument('--configs', default=','.join(CONFIGS))
    p = sub.add_parser('game')
    p.add_argument('--scenario', required=True)
    p.add_argument('--lang', default='en')
    p.add_argument('--configs', default='native')
    p.add_argument('--max-snaps', type=int, default=40)
    p.add_argument('--warp', action='store_true', help='unthrottled emulation (WARP=1)')
    p.add_argument('--reuse-transcripts', action='store_true')
    p = sub.add_parser('check')
    p.add_argument('--kind', choices=('present', 'game'), required=True)
    p.add_argument('--ref', required=True)
    p.add_argument('runs', nargs='+')
    args = ap.parse_args()
    global GUEST_ARGS
    GUEST_ARGS = (' ' + args.guest_args) if args.guest_args else ''
    args.out = os.path.abspath(args.out)
    if hasattr(args, 'configs'):
        names = []
        for c in args.configs.split(','):
            names += (list(SCALED) if c == 'scaled' else list(WINDOWED) if c == 'window' else
                      [c] if c else [])
        args.configs = names
        for c in args.configs:
            if c not in CONFIGS + SCALED + WINDOWED:
                ap.error('unknown configuration %s' % c)
    if args.cmd == 'present':
        return cmd_present(args)
    if args.cmd == 'game':
        return cmd_game(args)
    if args.cmd == 'check':
        return cmd_check(args)
    return 2


if __name__ == '__main__':
    sys.exit(main())
