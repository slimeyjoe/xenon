"""Oracle C: timing rules over the event stream (docs/re/graphics.md §7, platform.md §4).

Works on the list of events of a run (trace events plus the per-tick `frame` events the
backend emits while recording). Each rule states a frame count the original engine produces:

  wait      WAIT n and the timed message waits last exactly n ticks
  fade      a palette fade with delay d takes d ticks per step; each gun moves one step per
            change and the changes are d ticks apart
  effect    blind = 2 * h/8 ticks, centre-open = h/8, waku2 close = 37, dissolve = 16
  typing    one tick per full-width glyph while the typing wait is on, none when instant
  adt       frame i is drawn delay[i] ticks after the previous one, on alternating pages
  blink     strip copies follow blink_timing.tsv (frame sequence and tick gaps)
"""
import os

from . import textenc
from .engine import WAIT_TICKS, LEAVE_TIMEOUT

HERE = os.path.dirname(os.path.abspath(__file__))
RESOURCES = os.path.join(HERE, '..', '..', '..', '..', 'resources')


BLINK_PATH = os.path.join(RESOURCES, 'anim', 'blink', 'blink_timing.tsv')


def _blink_table():
    rows = []
    path = BLINK_PATH
    try:
        for line in open(path):
            if line.startswith('copy') or not line.strip():
                continue
            c, f, v = line.split()
            rows.append((int(f), int(v)))
    except OSError:
        pass
    return rows


BLINK = _blink_table()
fullwidth_glyphs = textenc.fullwidth_glyphs


class Timeline:
    def __init__(self, ctrl_intervals=()):
        self.problems = []
        self.ctrl = list(ctrl_intervals)    # (from_tick, to_tick) where CTRL was held or a
                                            # quit was pending (Game.press)
        self.counts = {}

    def _skip(self, t0, t1):
        return any(a <= t1 and t0 <= b for a, b in self.ctrl)

    def problem(self, ev, rule, msg):
        self.problems.append('%s t=%d %s: %s' % (rule, ev.t, ev.s, msg))

    def count(self, rule):
        self.counts[rule] = self.counts.get(rule, 0) + 1

    def check(self, events):
        trace = [e for e in events if not isinstance(e, dict) and e.ev not in ('stop', 'ok', 'exit')]
        frames = [e for e in trace if e.ev == 'frame']
        self._waits(trace)
        self._fades(trace, frames)
        self._effects(trace)
        self._typing(trace)
        self._adt(trace)
        self._blink(trace)
        return self.problems

    # ---- rules ----
    def _waits(self, trace):
        open_ = None
        for e in trace:
            if e.ev != 'wait' or e.a[0] != WAIT_TICKS:
                continue
            if e.a[1] == 0:
                open_ = e
            elif open_ is not None:
                self.count('wait')
                if e.a[2] == LEAVE_TIMEOUT and not self._skip(open_.t, e.t):
                    if e.t - open_.t != open_.a[2]:
                        self.problem(e, 'wait', 'WAIT %d lasted %d ticks' % (open_.a[2], e.t - open_.t))
                open_ = None

    def _fades(self, trace, frames):
        start = None
        for e in trace:
            if e.ev != 'fade':
                continue
            if e.a[1] == 0:
                start = e
                continue
            if start is None:
                continue
            self.count('fade')
            delay, steps = e.a[0], e.a[2]
            waits = steps + 1 if steps < 16 else 16
            if not self._skip(start.t, e.t):
                if e.t - start.t != delay * waits:
                    self.problem(e, 'fade', 'fade delay %d, %d steps: took %d ticks, expected %d' %
                                 (delay, steps, e.t - start.t, delay * waits))
                # palette changes inside the fade: one step per gun, `delay` ticks apart
                # frame t shows the state presented at engine tick t-1; the last step shares
                # its tick with whatever follows the fade, so stop one frame early
                pals = [(f.t, bytes.fromhex(f.raw['pal'])) for f in frames if start.t < f.t <= e.t]
                changes = []
                prev_p = None
                for t, p in pals:
                    if prev_p is not None and p != prev_p:
                        if max(abs(a - b) for a, b in zip(p, prev_p)) > 1:
                            self.problem(e, 'fade', 'palette jumped by more than one step at tick %d' % t)
                        changes.append(t)
                    prev_p = p
                for a, b in zip(changes, changes[1:]):
                    if delay > 0 and b - a != delay:
                        self.problem(e, 'fade', 'palette changed %d ticks after the previous change, delay %d' %
                                     (b - a, delay))
            start = None

    def _effects(self, trace):
        start = None
        names = {0: 'blind', 1: 'centre-open', 2: 'waku2 close', 3: 'dissolve'}
        for e in trace:
            if e.ev != 'effect':
                continue
            if e.a[1] == 0:
                start = e
                continue
            if start is None:
                continue
            self.count('effect')
            kind, h = start.a[0], start.a[2]
            want = {0: 2 * (h // 8), 1: h // 8, 2: 37, 3: 16}[kind]
            if not self._skip(start.t, e.t) and e.t - start.t != want:
                self.problem(e, 'effect', '%s over %d lines took %d ticks, expected %d' %
                             (names[kind], h, e.t - start.t, want))
            start = None

    def _typing(self, trace):
        start = None
        interrupted = False
        for e in trace:
            if e.ev == 'msg':
                start = e
                interrupted = False
            elif start is not None and e.ev in ('prompt', 'wait', 'key', 'menu'):
                interrupted = True                      # a key wait inside the message
            elif e.ev == 'msg_end' and start is not None:
                self.count('typing')
                if not interrupted and not self._skip(start.t, e.t):
                    want = 0 if start.a[0] else fullwidth_glyphs(start.text)
                    if e.t - start.t != want:
                        self.problem(e, 'typing', 'message %r typed in %d ticks, expected %d' %
                                     (start.text_str()[:30], e.t - start.t, want))
                start = None

    def _adt(self, trace):
        prev = None
        for e in trace:
            if e.ev != 'adt':
                continue
            if e.a[0] == -2:
                prev = e
                page = None
            elif e.a[0] >= 0 and prev is not None:
                self.count('adt')
                if not self._skip(prev.t, e.t) and e.t - prev.t != e.a[2]:
                    self.problem(e, 'adt', 'frame %d drawn %d ticks after the previous, delay %d' %
                                 (e.a[0], e.t - prev.t, e.a[2]))
                if page is not None and e.a[1] == page:
                    self.problem(e, 'adt', 'frame %d drawn on the same page as the previous' % e.a[0])
                page = e.a[1]
                prev = e
            elif e.a[0] == -3:
                prev = None

    def _blink(self, trace):
        copies = []
        for e in trace:
            if e.ev != 'blink':
                continue
            if e.a[0] == -1:
                copies = [None]
            elif e.a[0] == -2:
                self._blink_run(copies[1:])
                copies = []
            elif e.a[0] >= 0 and copies:
                copies.append(e)
        if len(copies) > 1:
            self._blink_run(copies[1:])

    def _blink_run(self, copies):
        if not copies:
            return
        if not BLINK:
            self.problem(copies[0], 'blink', 'no %s to check the blinks against (make data derives '
                         'it from the game, re-tools/adt.py)' % os.path.relpath(BLINK_PATH))
            return
        for i, e in enumerate(copies):
            self.count('blink')
            if i >= len(BLINK):
                break
            frame, gap = BLINK[i]
            if e.a[0] != frame:
                self.problem(e, 'blink', 'copy %d used strip frame %d, expected %d' % (i, e.a[0], frame))
                break
            if i + 1 < len(copies) and not self._skip(e.t, copies[i + 1].t):
                d = copies[i + 1].t - e.t
                if d != gap:
                    self.problem(e, 'blink', 'copy %d -> %d gap %d ticks, expected %d' % (i, i + 1, d, gap))
                    break
