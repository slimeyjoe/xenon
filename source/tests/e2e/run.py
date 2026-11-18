#!/usr/bin/env python3
"""Run the end-to-end scenarios against the null backend, or against the SDL3 backend rendering
offscreen (--backend sdl3: every snapshot is also read back from the renderer and compared).

usage: run.py [--bin build/host/xenon] [--pak build/xenon.pak] [--lang en|ja|all]
              [--backend null|sdl3] [--window WxH] [--scale integer|aspect|fit|stretch]
              [--out build/host/e2e] [--update] [--list] [-k SUBSTRING] [--seed N] [--jobs N]
              [NAME...]

Scenarios live in scenarios/*.py and register themselves with @scenario(name). The goldens'
reference is tests/e2e/golden.tsv (a hash per golden); the pictures are local copies in
tests/e2e/golden/<lang>/<name>.png, saved when a frame matches. --update rewrites both from the
current run (review the diff images in --out first). Exit code 1 when any scenario failed.

The scenarios run in parallel, --jobs at a time (default: one per CPU), each language and each
part of a scenario (parts=N: independent games, ctx.part) as its own unit in its own process;
the results are printed in scenario order once a scenario's parts are done, so the output does
not depend on which unit finishes first. --jobs 1 runs everything in this process.
"""
import argparse
import concurrent.futures
import glob
import importlib.util
import os
import sys
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from xe2e.game import Config, Game, ScenarioFailed   # noqa: E402
from xe2e import golden, textenc   # noqa: E402

REGISTRY = []


def scenario(name, langs=('en', 'ja'), tags=(), backends=('null', 'sdl3'), parts=1):
    """parts: the scenario is called once per part (ctx.part 0..parts-1), each a unit that runs
    in parallel with the others; the parts' results are reported together as the scenario."""
    def deco(fn):
        REGISTRY.append((name, fn, tuple(langs), tuple(tags) + tuple('only:' + b for b in backends if len(backends) == 1), tuple(backends), parts))
        return fn
    return deco


def load_scenarios():
    import builtins
    builtins.scenario = scenario            # the scenario modules use the decorator
    for path in sorted(glob.glob(os.path.join(HERE, 'scenarios', '*.py'))):
        spec = importlib.util.spec_from_file_location('scenarios.' + os.path.basename(path)[:-3], path)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    src = os.path.join(HERE, '..', '..')
    ap.add_argument('--bin', default=os.path.join(src, '..', 'build', 'host', 'xenon'))
    ap.add_argument('--pak', default=os.path.join(src, '..', 'build', 'xenon.pak'))
    ap.add_argument('--lang', default='en')
    ap.add_argument('--backend', default='null', choices=('null', 'sdl3'))
    ap.add_argument('--window', default=None, help='sdl3: drawable size WxH (default 1280x800)')
    ap.add_argument('--scale', default=None, choices=('integer', 'aspect', 'fit', 'stretch'))
    ap.add_argument('--out', default=os.path.join(src, '..', 'build', 'host', 'e2e'))
    ap.add_argument('--golden', default=os.path.join(HERE, 'golden'), help='local copies of the goldens')
    ap.add_argument('--manifest', default=os.path.join(HERE, 'golden.tsv'), help='the goldens\' hashes')
    ap.add_argument('--update', action='store_true', help='rewrite the goldens')
    ap.add_argument('--list', action='store_true')
    ap.add_argument('-k', default=None, help='only scenarios whose name contains this')
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--no-logs', action='store_true')
    ap.add_argument('--transcripts', default=None,
                    help='write each game\'s input transcript to DIR/<lang>/<game>.json (amiga.py)')
    ap.add_argument('--jobs', '-j', type=int, default=os.process_cpu_count() or 1,
                    help='units run at once (default: one per CPU)')
    ap.add_argument('names', nargs='*')
    args = ap.parse_args()

    load_scenarios()
    langs = ['en', 'ja'] if args.lang == 'all' else [args.lang]
    selected = [(i, n, ls, tags, parts) for i, (n, f, ls, tags, bs, parts) in enumerate(REGISTRY)
                if (not args.names or n in args.names) and (not args.k or args.k in n) and args.backend in bs]
    if args.list:
        for i, n, ls, tags, parts in selected:
            print('%-24s langs=%s %s' % (n, ','.join(ls), ' '.join(tags)))
        return 0

    # (lang, scenario, [units]) in report order; the units are started longest first by the
    # tags (a slow scenario, then the routes) so the long ones do not end up last
    report = [(lang, i, n, [(lang, i, k) for k in range(parts)])
              for lang in langs for i, n, ls, tags, parts in selected if lang in ls]
    weight = {i: 0 if 'slow' in tags else 1 if 'route' in tags else 2 for i, n, ls, tags, parts in selected}
    units = sorted((u for r in report for u in r[3]), key=lambda u: weight[u[1]])

    failed = 0
    if args.jobs > 1 and len(units) > 1:
        pool = concurrent.futures.ProcessPoolExecutor(min(args.jobs, len(units)), initializer=_init,
                                                      initargs=(args,))
        futures = {u: pool.submit(run_unit, u) for u in units}
        result = lambda u: futures[u].result()
    else:
        pool = None
        _init(args)
        result = run_unit
    try:
        for lang, i, name, parts in report:
            problems, summaries = [], []
            for u in parts:
                p, sm, changes = result(u)
                problems += p
                summaries += sm
                if pool:                            # --update: the goldens a worker recorded
                    for path, lng, nm, h in changes:
                        golden.manifest(path).set(lng, nm, h)
            summary = '; '.join(summaries)
            if problems:
                failed += 1
                print('FAIL %s [%s] (%s)' % (name, lang, summary))
                for p in problems[:40]:
                    print('    ' + p.replace('\n', '\n    '))
                if len(problems) > 40:
                    print('    ... %d more' % (len(problems) - 40))
            else:
                print('PASS %s [%s] (%s)' % (name, lang, summary))
            sys.stdout.flush()
    finally:
        if pool:
            pool.shutdown(cancel_futures=True)
    golden.save_manifests()
    print('%d/%d scenarios passed' % (len(report) - failed, len(report)))
    return 1 if failed else 0


_opts = None
_configured = None


def _init(args):
    """In each process that runs units: the scenarios and the options."""
    global _opts
    _opts = args
    if not REGISTRY:
        load_scenarios()


def run_unit(unit):
    """One part of one scenario in one language. Returns its problems, the summaries of its
    games and the goldens it recorded (--update)."""
    global _configured
    lang, i, part = unit
    args = _opts
    name, fn = REGISTRY[i][:2]
    cfg = Config(os.path.abspath(args.bin), os.path.abspath(args.pak), lang, os.path.abspath(args.out),
                 os.path.abspath(args.golden), update=args.update, keep_logs=not args.no_logs,
                 backend=args.backend, window=tuple(int(v) for v in args.window.split('x')) if args.window else None,
                 scale=args.scale, manifest=os.path.abspath(args.manifest))
    cfg.seed = args.seed
    cfg.transcript_dir = os.path.abspath(args.transcripts) if args.transcripts else None
    if _configured != lang:
        textenc.configure(os.path.abspath(args.pak), lang)
        _configured = lang
    games = []
    ctx = Context(cfg, name, games, part)
    problems = []
    try:
        fn(ctx)
    except ScenarioFailed as e:
        problems.append('scenario stopped: %s' % e)
    except Exception:
        problems.append('scenario crashed:\n' + traceback.format_exc())
    for g in games:
        problems += g.finish()
    return problems, [g.summary() for g in games], golden.take_changes()


class Context:
    """What a scenario function receives."""

    def __init__(self, cfg, name, games, part=0):
        self.cfg = cfg
        self.name = name
        self.part = part                    # which part of a scenario with parts=N
        self.lang = cfg.lang
        self.seed = cfg.seed
        self._games = games

    def game(self, suffix=None, **kw):
        g = Game(self.cfg, self.name + ('-' + suffix if suffix else ''), **kw)
        self._games.append(g)
        return g


if __name__ == '__main__':
    sys.exit(main())
