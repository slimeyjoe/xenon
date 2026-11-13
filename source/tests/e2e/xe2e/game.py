"""The game driver scenarios use: one engine run plus the oracles, with helpers that press keys
only when the engine is blocked waiting for them.

A Game plays until the engine blocks on a key wait ("blocked" stop). The context of the block
is derived from the events: a script menu (self.menu), the title menu (self.title_rows), a
message prompt or a box. Scenarios then advance(), choose(), choose_title() or press().

Every event goes through the script oracle; every blocked state is checked against the image
oracle (one snapshot per wait, cheap); the timeline rules run at the end over all events.
"""
import json
import os
import shutil
import struct
import time

from . import golden, present, textenc
from .engine import Engine, WAIT_KEY, WAIT_OK, ProtocolError
from .images import ImageOracle
from .script import ScriptOracle
from .timeline import Timeline


class ScenarioFailed(Exception):
    pass


class Rotation:
    """Default menu policy: at each menu try every option once, best flag score first
    (script.Script.option_scores); afterwards always the best one, so quiz loops end."""

    def __init__(self):
        self.seen = {}

    def __call__(self, game, items):
        key = (game.script_name, game.menu_pos, tuple(items))
        n = self.seen.get(key, 0)
        self.seen[key] = n + 1
        scores = game.script.scores
        if scores and len(scores) == len(items):
            order = sorted(range(len(items)), key=lambda k: (-scores[k], k))
        else:
            order = list(range(len(items)))
        return order[n] if n < len(items) else order[0]


class Config:
    def __init__(self, binary, pak, lang, out_dir, golden_dir, update=False, keep_logs=True,
                 backend='null', window=None, scale=None, manifest=None):
        self.binary = binary
        self.pak = pak
        self.lang = lang
        self.out_dir = out_dir
        self.golden_dir = golden_dir    # local copies of the goldens (not committed)
        self.manifest = manifest or os.path.join(os.path.dirname(golden_dir), 'golden.tsv')
        self.update = update
        self.keep_logs = keep_logs
        self.backend = backend          # 'null' or 'sdl3' (adds the presentation oracle)
        self.window = window            # sdl3: (W, H) drawable, None = the backend's default
        self.scale = scale              # sdl3: integer | aspect | fit | stretch
        self.transcript_dir = None      # write <dir>/<lang>/<game>.json (tests/e2e/amiga.py)


class Game:
    def __init__(self, cfg, name, progress=None, saves=None, image_checks=True, window=None, scale=None):
        self.cfg = cfg
        self.name = name
        self.problems = []
        self.window = window or cfg.window
        self.scale = scale or cfg.scale
        self.readbacks = 0
        self.events = []
        self.storage = os.path.join(cfg.out_dir, 'storage', name)
        shutil.rmtree(self.storage, ignore_errors=True)
        os.makedirs(self.storage)
        if progress is not None:
            open(os.path.join(self.storage, 'progress.dat'), 'wb').write(bytes([progress]))
        for slot, path in (saves or {}).items():
            shutil.copy(path, os.path.join(self.storage, slot + '.dat'))
        log_path = os.path.join(cfg.out_dir, 'logs', name + '.jsonl')
        os.makedirs(os.path.dirname(log_path), exist_ok=True)
        self.log = open(log_path, 'wb') if cfg.keep_logs else None
        self.script = ScriptOracle(cfg.pak, cfg.lang, self.storage)
        self.images = ImageOracle(cfg.pak)
        self.image_checks = image_checks
        self.ctrl_intervals = []
        self.ctrl_since = None
        self.started = time.time()
        # blocked-state context
        self.menu = None            # (items, cursor) of the script menu shown
        self.menu_cursor = 0
        self.menu_pos = None
        self.menu_system = False
        self.title_rows = None
        self.ok_wait = False
        self.box = None
        self.stop = None
        self.snapshots = 0
        self.scrolled_redraws = 0
        self.scrolled_frames = 0
        self.scrolled_ticks = []
        self._last_scroll = 0
        self.eng = Engine(cfg.binary, cfg.pak, cfg.lang, self.storage, log=self.log,
                          backend=cfg.backend, window=self.window, scale=self.scale)
        self._process(self.eng.pre_events)
        self.eng.record(1)

    # ---- event processing ----
    def _process(self, events):
        for ev in events:
            self.events.append(ev)
            if isinstance(ev, dict):
                continue
            k = ev.ev
            if k == 'frame':
                # the frame changed while the screen stayed scrolled at the same line (G_SCROLL
                # shake): the case backends must upload rotated (plan §10)
                sc = ev.get('scroll', 0)
                if sc:
                    self.scrolled_frames += 1
                    if len(self.scrolled_ticks) < 64:
                        self.scrolled_ticks.append(ev.t)
                if sc and sc == self._last_scroll:
                    self.scrolled_redraws += 1
                self._last_scroll = sc
            if k in ('stop', 'exit', 'ok', 'error', 'frame'):
                continue
            self.script.on_event(ev)
            self.images.on_event(ev)
            if k == 'menu':
                self.menu = (ev.items, ev.a[0])
                self.menu_cursor = ev.a[0]
                self.menu_pos = ev.pos
                self.menu_system = ev.a[1] != 0
            elif k == 'menu_sel':
                self.menu = None
            elif k == 'title_menu':
                self.title_rows = ev.a[0]
                self.title_cursor = 0
            elif k == 'title_sel':
                self.title_rows = None
            elif k == 'wait' and ev.a[0] == WAIT_OK:
                self.ok_wait = ev.a[1] == 0
            elif k == 'box':
                self.box = ev.text
            elif k == 'key':
                # the cursor moves the driver asked for
                if self.menu and ev.a[0] == 1:
                    self.menu_cursor = (self.menu_cursor - 1) % len(self.menu[0])
                elif self.menu and ev.a[0] == 2:
                    self.menu_cursor = (self.menu_cursor + 1) % len(self.menu[0])
                if self.title_rows and ev.a[0] == 1 and self.title_cursor > 0:
                    self.title_cursor -= 1
                elif self.title_rows and ev.a[0] == 2 and self.title_cursor < self.title_rows - 1:
                    self.title_cursor += 1
                if self.box is not None and ev.a[0] in (0, 5):
                    self.box = None

    # ---- running ----
    @property
    def tick(self):
        return self.eng.tick

    @property
    def script_name(self):
        return self.script.cur.name if self.script.cur else None

    @property
    def page(self):
        return self.script.cur_page

    @property
    def exited(self):
        return self.eng.exited is not None

    def run(self, budget=None):
        """Resume until the engine blocks (or a break / budget stop). Returns the stop event."""
        events, stop = self.eng.run(budget)
        self._process(events)
        self.stop = stop
        if stop.ev == 'stop' and stop.why == 'blocked' and self.image_checks:
            self.check_image('blocked t=%d %s@%s' % (stop.t, self.script_name, self.page))
        return stop

    def until_blocked(self, max_runs=1000):
        for _ in range(max_runs):
            stop = self.run()
            if stop.ev == 'exit' or stop.why == 'blocked':
                return stop
        raise ScenarioFailed('engine never blocked')

    def press(self, *keys):
        self.eng.press(*keys)
        if 'quit' in keys or 'emergency' in keys:
            # the engine leaves at once: a fade, effect or wait in progress is cut short, so
            # the timing rules are off from here on (like the CTRL intervals)
            self.ctrl_intervals.append((self.tick, 1 << 62))

    def hold_ctrl(self, on):
        self.eng.hold('ctrl', on)
        if on and self.ctrl_since is None:
            self.ctrl_since = self.tick
        elif not on and self.ctrl_since is not None:
            self.ctrl_intervals.append((self.ctrl_since, self.tick + 1))
            self.ctrl_since = None

    def instant_text(self, on):
        self.eng.hold('instant', on)

    def advance(self, n=1):
        """Press OK at the current block, n times (each press runs to the next block)."""
        stop = self.stop
        for _ in range(n):
            if self.exited:
                return stop
            self._require_blocked()
            self.press('ok')
            stop = self.until_blocked()
        return stop

    def _require_blocked(self):
        if self.stop is None or self.stop.ev != 'stop' or self.stop.why != 'blocked':
            stop = self.until_blocked()
            if stop.ev == 'exit':
                raise ScenarioFailed('engine exited: %s' % stop.raw.get('msg'))

    def choose(self, which):
        """Select an option of the script menu currently shown: an index, or a substring of
        the option text (decoded in the language's encoding)."""
        self._require_blocked()
        if not self.menu:
            raise ScenarioFailed('choose(%r): no menu is shown (t=%d %s@%s)' % (which, self.tick, self.script_name, self.page))
        items, _ = self.menu
        if isinstance(which, int):
            index = which
        else:
            texts = [textenc.decode(i) for i in items]
            hits = [k for k, t in enumerate(texts) if which in t]
            if len(hits) != 1:
                raise ScenarioFailed('choose(%r): %d matches in %r' % (which, len(hits), texts))
            index = hits[0]
        if not 0 <= index < len(items):
            raise ScenarioFailed('choose(%d): menu has %d options' % (index, len(items)))
        guard = 0
        while self.menu_cursor != index:
            self.press('down' if (index - self.menu_cursor) % len(items) <= len(items) // 2 else 'up')
            self.until_blocked()
            guard += 1
            if guard > 20:
                raise ScenarioFailed('cursor did not reach option %d' % index)
        self.press('ok')
        return self.until_blocked()

    def choose_title(self, index):
        self._require_blocked()
        if not self.title_rows:
            raise ScenarioFailed('choose_title: the title menu is not shown')
        while self.title_cursor != index:
            self.press('down' if index > self.title_cursor else 'up')
            self.until_blocked()
        self.press('ok')
        return self.until_blocked()

    def cancel(self):
        self._require_blocked()
        self.press('cancel')
        return self.until_blocked()

    def play_until(self, pred, policy=None, max_steps=20000):
        """Advance through messages and menus until pred(game) is true or the engine exits.
        policy(game, items) -> index picks menu options (default: rotate per menu)."""
        rotation = Rotation()
        for _ in range(max_steps):
            if pred(self):
                return True
            if self.exited:
                return pred(self)
            self._require_blocked()
            if pred(self):
                return True
            if self.menu:
                items, _ = self.menu
                idx = (policy or rotation)(self, items)
                self.choose(idx)
            elif self.title_rows:
                self.choose_title(0)
            else:
                self.advance()
        raise ScenarioFailed('play_until: no progress after %d steps' % max_steps)

    # ---- checks ----
    def snapshot(self, name='snap'):
        self.snapshots += 1
        frame = self.eng.snap(name)
        if self.cfg.backend == 'sdl3':
            self.readbacks += 1
            rb = self.eng.readback(name)
            self.problems += present.check(frame, rb, '%s (t=%d %s@%s)' % (name, self.tick, self.script_name, self.page),
                                           os.path.join(self.cfg.out_dir, 'diff', self.cfg.lang),
                                           '%s-%s-%d' % (self.name, name, self.tick))
        return frame

    def check_image(self, where):
        frame = self.snapshot('check')
        self.images.check(frame, where)
        return frame

    def golden(self, name):
        """Compare the displayed frame with its reference in golden.tsv (record it with update)."""
        frame = self.snapshot(name)
        path = os.path.join(self.cfg.golden_dir, self.cfg.lang, name + '.png')
        problem = golden.compare(frame, path, os.path.join(self.cfg.out_dir, 'diff', self.cfg.lang),
                                 name, self.cfg.update, self.cfg.manifest, self.cfg.lang)
        if problem:
            self.problems.append(problem)
        self.images.check(frame, 'golden ' + name)
        return frame

    def expect(self, cond, msg):
        if not cond:
            self.problems.append('expect: %s (t=%d %s@%s)' % (msg, self.tick, self.script_name, self.page))
        return cond

    def fail(self, msg):
        self.problems.append(msg)

    def last(self, kind, **match):
        """The most recent event of `kind` whose fields match (name=..., a0=...)."""
        for ev in reversed(self.events):
            if isinstance(ev, dict) or ev.ev != kind:
                continue
            ok = True
            for k, v in match.items():
                if k.startswith('a') and k[1:].isdigit():
                    ok &= ev.a[int(k[1:])] == v
                else:
                    ok &= getattr(ev, k) == v
            if ok:
                return ev
        return None

    def events_of(self, kind):
        return [e for e in self.events if not isinstance(e, dict) and e.ev == kind]

    # ---- finish ----
    def finish(self):
        if self.ctrl_since is not None:
            self.hold_ctrl(False)
        tl = Timeline(self.ctrl_intervals)
        tl.check(self.events)
        problems = list(self.problems) + self.script.problems + self.images.problems + tl.problems
        self.timeline_counts = tl.counts
        try:
            self.eng.close()
        except ProtocolError:
            pass
        if self.cfg.transcript_dir:
            d = os.path.join(self.cfg.transcript_dir, self.cfg.lang)
            os.makedirs(d, exist_ok=True)
            with open(os.path.join(d, self.name + '.json'), 'w') as f:
                json.dump({'name': self.name, 'lang': self.cfg.lang, 'ticks': self.tick,
                       'scrolled_frames': self.scrolled_frames, 'scrolled_redraws': self.scrolled_redraws,
                       'scrolled_ticks': self.scrolled_ticks,
                           'storage': self.eng.initial_storage, 'transcript': self.eng.transcript}, f)
        if self.log:
            self.log.close()
        self.elapsed = time.time() - self.started
        return problems

    def summary(self):
        tl = getattr(self, 'timeline_counts', {})
        rb = ', %d readbacks' % self.readbacks if self.readbacks else ''
        return ('ticks %d, %d events, %d snapshots, %d image checks%s, timeline %s, %.1f s' % (
            self.tick, len(self.events), self.snapshots, self.images.checks, rb,
            ' '.join('%s=%d' % kv for kv in sorted(tl.items())), self.elapsed))


def read_save(path):
    buf = open(path, 'rb').read()
    return dict(flags=buf[8:264], script=buf[264:304].split(b'\0')[0].decode(),
                page=struct.unpack_from('<H', buf, 304)[0],
                music=buf[306:322].split(b'\0')[0].decode(), image=buf[323:363].split(b'\0')[0].decode())
