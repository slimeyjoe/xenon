"""Client side of the control protocol (backends/control/control.c), spoken by the null
backend and by the SDL3 backend rendering offscreen."""
import base64
import hashlib
import json
import os
import subprocess

import numpy as np

from . import textenc

KEYS = ('ok', 'cancel', 'up', 'down', 'left', 'right', 'help', 'f1', 'f2', 'f3', 'f4', 'f5',
        'other', 'quit', 'emergency', 'mouse_l', 'mouse_r')

# XE_TR_WAIT a0
WAIT_TICKS, WAIT_KEY, WAIT_OK, WAIT_MUSIC = 0, 1, 2, 3
# leave reasons (a2 of the leave event)
LEAVE_KEY, LEAVE_CTRL, LEAVE_TIMEOUT, LEAVE_QUIT = 0, 1, 2, 3


class ProtocolError(Exception):
    pass


class Event:
    """One JSON line from the backend. text/items are decoded to bytes (the language's
    encoding, xe2e.textenc)."""
    __slots__ = ('ev', 't', 'pos', 's', 'name', 'text', 'items', 'a', 'ints', 'raw')

    def __init__(self, d):
        self.raw = d
        self.ev = d['ev']
        self.t = d.get('t', 0)
        self.pos = d.get('pos')
        self.s = d.get('s', '')
        self.name = d.get('name')
        self.text = bytes.fromhex(d['text']) if 'text' in d else None
        self.items = [bytes.fromhex(x) for x in d['items']] if 'items' in d else None
        self.a = d.get('a', [0, 0, 0, 0])
        self.ints = d.get('ints')

    def __getitem__(self, k):
        return self.raw[k]

    def get(self, k, default=None):
        return self.raw.get(k, default)

    @property
    def why(self):
        return self.raw.get('why')

    def text_str(self):
        return textenc.decode(self.text) if self.text is not None else ''

    def __repr__(self):
        extra = ''
        if self.name:
            extra += ' %s' % self.name
        if self.text is not None:
            extra += ' %r' % self.text_str()[:40]
        if self.items:
            extra += ' %r' % [textenc.decode(i) for i in self.items]
        if self.ev in ('stop', 'exit'):
            extra += ' %s' % (self.raw.get('why') or self.raw.get('msg'))
        return '<%s t=%s pos=%s %s%s>' % (self.ev, self.t, self.pos, self.a, extra)


class Frame:
    """A displayed frame: idx (400, 640) uint8 colour indices, pal 48 bytes RGB4, gon, scroll."""

    def __init__(self, d):
        self.tick = d['t']
        self.pal = bytes.fromhex(d['pal'])
        self.gon = d['gon']
        self.scroll = d.get('scroll', 0)
        self.name = d.get('name')
        packed = np.frombuffer(base64.b64decode(d['data']), np.uint8)
        idx = np.empty(packed.size * 2, np.uint8)
        idx[0::2] = packed >> 4
        idx[1::2] = packed & 15
        self.idx = idx.reshape(400, 640)

    def rgb(self):
        pal = np.array([v * 17 for v in self.pal], np.uint8).reshape(16, 3)
        if not self.gon:
            return np.zeros((400, 640, 3), np.uint8)
        return pal[self.idx]


def frame_digest(f):
    """Digest of what a frame shows (indices in display order, palette, graphics on)."""
    return hashlib.sha1(f.idx.tobytes() + f.pal + bytes([1 if f.gon else 0])).hexdigest()[:16]


class Readback:
    """What the backend rendered for the displayed frame (SDL3 `readback`): rgb (400, 640, 3)
    uint8 samples, win (W, H), dest (x, y, w, h), mode, linear, nonuniform, border_lit."""

    def __init__(self, d):
        self.tick = d['t']
        self.name = d.get('name')
        self.win = tuple(d['win'])
        self.dest = tuple(d['dest'])
        self.mode = d['mode']
        self.linear = bool(d['linear'])
        self.nonuniform = d['nonuniform']
        self.border_lit = d['border_lit']
        self.rgb = np.frombuffer(base64.b64decode(d['data']), np.uint8).reshape(400, 640, 3)


class Engine:
    """A running engine in control mode."""

    def __init__(self, binary, pak, lang, storage, max_ticks=5000000, log=None, backend='null',
                 window=None, scale=None):
        os.makedirs(storage, exist_ok=True)
        # what the driver did, for replaying the run elsewhere (tests/e2e/amiga.py): inputs at
        # the tick of the stop they were given in, snapshots with a digest of the frame, and
        # the storage files the engine started with
        self.transcript = []
        self.initial_storage = {}
        for fn in sorted(os.listdir(storage)):
            path = os.path.join(storage, fn)
            if os.path.isfile(path) and fn.endswith('.dat'):
                with open(path, 'rb') as f:
                    self.initial_storage[fn] = f.read().hex()
        self.backend = backend
        self.cmd = [binary, '--pak', pak, '--lang', lang, '--storage', storage,
                    '--max-ticks', str(max_ticks), '--control']
        if backend == 'sdl3':
            if window:
                self.cmd += ['--window', '%dx%d' % tuple(window)]
            if scale:
                self.cmd += ['--scale', scale]
        elif window or scale:
            raise ValueError('window/scale need the sdl3 backend')
        self.proc = subprocess.Popen(self.cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=open(os.path.join(storage, 'stderr.txt'), 'wb'))
        self.log = log                      # file object for the raw event log, or None
        self.exited = None                  # exit event once seen
        self.tick = 0
        # the backend emits the logo setup events, then stops with why=start
        self.pre_events, self.last_stop = self._read_until_stop()

    # ---- raw protocol ----
    def _send(self, line):
        if self.exited:
            raise ProtocolError('engine exited: %s' % self.exited)
        self.proc.stdin.write((line + '\n').encode())
        self.proc.stdin.flush()

    def _read(self):
        line = self.proc.stdout.readline()
        if not line:
            rc = self.proc.wait()
            raise ProtocolError('engine closed the pipe (exit code %s)' % rc)
        d = json.loads(line)
        if self.log and d['ev'] not in ('snap', 'frame', 'present'):
            self.log.write(line)
        ev = Event(d) if d['ev'] not in ('snap', 'frame', 'present') or 'data' not in d else d
        if isinstance(ev, Event):
            if ev.ev == 'exit':
                self.exited = ev
            if ev.ev not in ('ok', 'error'):
                self.tick = max(self.tick, ev.t)
        return ev

    def _expect_ok(self):
        ev = self._read()
        if isinstance(ev, dict) or ev.ev != 'ok':
            raise ProtocolError('expected ok, got %r' % (ev,))

    def _read_until_stop(self):
        events = []
        while True:
            ev = self._read()
            if isinstance(ev, dict):
                events.append(ev)                   # a frame with data (record 2)
                continue
            events.append(ev)
            if ev.ev in ('stop', 'exit'):
                return events, ev

    # ---- commands ----
    def run(self, budget=None):
        """Resume the engine. Returns (events, stop_event); stop.why is blocked / break / ticks,
        or the event is an exit."""
        if self.exited:
            return [], self.exited
        self._send('run' if budget is None else 'run %d' % budget)
        events, stop = self._read_until_stop()
        self.last_stop = stop
        return events, stop

    def press(self, *keys):
        for k in keys:
            if k not in KEYS:
                raise ValueError('unknown key %r' % k)
        self._send('press ' + ','.join(keys))
        self._expect_ok()
        self.transcript.append(['press', self.last_stop.t, list(keys)])

    def hold(self, level, on):
        self._send('hold %s %d' % (level, 1 if on else 0))
        self._expect_ok()
        self.transcript.append(['hold', self.last_stop.t, level, 1 if on else 0])

    def mouse(self, dx, dy):
        self._send('mouse %d %d' % (dx, dy))
        self._expect_ok()
        self.transcript.append(['mouse', self.last_stop.t, dx, dy])

    def breaks(self, *kinds, clear=False):
        if clear:
            self._send('unbreak all')
            self._expect_ok()
        for k in kinds:
            self._send('break %s' % k)
            self._expect_ok()

    def record(self, level):
        self._send('record %d' % level)
        self._expect_ok()

    def snap(self, name='snap'):
        self._send('snap %s' % name.replace(' ', '_'))
        d = self._read()
        if not isinstance(d, dict) or d.get('ev') != 'snap':
            raise ProtocolError('expected snap, got %r' % (d,))
        self._expect_ok()
        f = Frame(d)
        self.transcript.append(['snap', self.last_stop.t, name, frame_digest(f)])
        return f

    def readback(self, name='readback'):
        """The rendered frame (backends with a display; the null backend answers with an error)."""
        self._send('readback %s' % name.replace(' ', '_'))
        d = self._read()
        if not isinstance(d, dict) or d.get('ev') != 'present':
            raise ProtocolError('expected present, got %r' % (d,))
        self._expect_ok()
        return Readback(d)

    def close(self):
        if self.proc.poll() is None:
            try:
                if not self.exited:
                    self._send('quit')
                    self.proc.stdin.close()
                self.proc.wait(timeout=5)
            except (OSError, subprocess.TimeoutExpired, ProtocolError):
                self.proc.kill()
                self.proc.wait()
        if self.log:
            self.log.flush()
