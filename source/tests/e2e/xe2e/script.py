"""Oracle A: the engine's event stream against the script bytecode it runs.

Every traced event carries the offset of the statement being executed. The oracle parses the
same bytecode the engine loaded (from the pack) with re-tools/ccdis.py and requires, at that
offset, a statement of the right kind with the same content: message text, menu options (the
ones visible under the current flags), opcode and operands, flag assignment, page number. It
also tracks the flags (and re-reads them from the save file after LOAD) and checks that every
asset a script names exists in the pack.
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', '..', 'tools'))          # source/tools (mkpack)
sys.path.insert(0, os.path.join(HERE, '..', '..', '..', 'lrvne', 'tools'))  # lrvne's tools
sys.path.insert(0, os.path.join(HERE, '..', '..', '..', '..', 're-tools'))  # re-tools (ccdis)
import ccdis   # noqa: E402
import mkpack  # noqa: E402
from lrvne import xpak   # noqa: E402
from . import textenc   # noqa: E402

OP_MSG = 0xFD
OP_ASSET = {0x0C: mkpack.T_GDT, 0x16: mkpack.T_GDT, 0x28: mkpack.T_GDT, 0x33: mkpack.T_GDT,
            0x34: mkpack.T_GDT, 0x0E: mkpack.T_MUSIC, 0x2A: mkpack.T_ADT, 0x07: mkpack.T_SCRIPT}
OP_NAMES = {op: v[0] for op, v in ccdis.OPS.items()}

# Data bugs of the original game files (docs/re/sound.md §4.2, scripts.md §6): the engine
# reproduces them faithfully unless the pack was built with --fix-data-bugs, so they are
# reported as notes, not failures.
KNOWN_DATA_BUGS = {
    ('M', 0x0E, 'XENON_102'),      # music mode "Ago" loads the nonexistent xenon_102
}


def _walk(nodes, out):
    for n in nodes:
        out[n['pos']] = n
        t = n['t']
        if t == 'page' or t == 'if':
            _walk(n['body'], out)
        elif t == 'menu':
            for c in n['cases']:
                _walk(c['body'], out)


def _sets_in(nodes, out, cond_true=None):
    """Flag assignments reachable in `nodes`; with cond_true(cond) only the IF bodies that
    hold under the current flags are entered (nested menus are not)."""
    for n in nodes:
        if n['t'] == 'set':
            out.append((n['flag'], 1 if n['val'] else 0))
        elif n['t'] == 'if':
            if cond_true is None or cond_true(n['cond']):
                _sets_in(n['body'], out, cond_true)
        elif n['t'] == 'menu' and cond_true is None:
            for c in n['cases']:
                _sets_in(c['body'], out)


class Script:
    def __init__(self, name, data):
        self.name = name
        self.data = data
        self.ast, self.parser = ccdis.parse(data)
        self.by_pos = {}
        _walk(self.ast, self.by_pos)
        self.pages = {n['num']: n for n in self.ast if n['t'] == 'page'}
        self.messages = sum(1 for n in self.by_pos.values() if n['t'] == 'msg')
        # (flag, value) pairs some IF or option condition in this script asks for
        self.wanted = set()
        for n in self.by_pos.values():
            if n['t'] == 'if':
                self.wanted.update((f, 1 if v else 0) for f, v in n['cond'])
            elif n['t'] == 'menu':
                for o in n['opts']:
                    if o['cond']:
                        self.wanted.update((f, 1 if v else 0) for f, v in o['cond'])

    def option_scores(self, menu_node, opts, cond_true):
        """For each option (dict with 'id'): how many flag assignments its case body would
        make under the current flags that produce a value some condition of this script asks
        for. A policy that prefers high scores passes the quiz loops (S0102) without knowing
        them."""
        cases = {c['id']: c['body'] for c in menu_node['cases']}
        scores = []
        for o in opts:
            sets = []
            _sets_in(cases.get(o['id'], []), sets, cond_true)
            scores.append(sum(1 for s in sets if s in self.wanted))
        return scores


class ScriptOracle:
    def __init__(self, pak_path, lang, storage_dir):
        self.lang = lang
        self.storage = storage_dir
        entries = xpak.read_pack(open(pak_path, 'rb').read()).entries
        self.scripts = {}
        for e in entries:
            if e.type == mkpack.T_SCRIPT and e.lang_tag in (lang, ''):
                if e.name.upper() not in self.scripts or e.lang_tag == lang:
                    self.scripts[e.name.upper()] = e.data
        self.assets = {}
        for e in entries:
            self.assets.setdefault(e.type, set()).add(e.name.upper())
        self.cache = {}
        self.reset()

    def reset(self):
        self.flags = bytearray(256)
        self.cur = None
        self.cur_page = None
        self.visible = None             # options of the menu currently shown
        self.menu_node = None
        self.scores = None
        self.problems = []
        self.notes = []
        self.visited = set()            # (script, page)
        self.shown = set()              # (script, pos) of messages shown
        self.engine_messages = []

    # ---- helpers ----
    def script(self, name):
        name = name.upper()
        if name not in self.cache:
            if name not in self.scripts:
                return None
            self.cache[name] = Script(name, self.scripts[name])
        return self.cache[name]

    def flag(self, f):
        return (self.flags[f >> 3] >> (f & 7)) & 1

    def set_flag(self, f, v):
        if v:
            self.flags[f >> 3] |= 1 << (f & 7)
        else:
            self.flags[f >> 3] &= ~(1 << (f & 7)) & 0xFF

    def cond_true(self, cond):
        return all(self.flag(f) == v for f, v in cond)

    def problem(self, ev, msg):
        self.problems.append('%s@%s pos=%s t=%d: %s' % (ev.s, self.cur_page, ev.pos, ev.t, msg))

    def node(self, ev, kinds):
        if self.cur is None:
            self.problem(ev, 'event outside a script')
            return None
        n = self.cur.by_pos.get(ev.pos)
        if n is None:
            self.problem(ev, 'no statement at this offset (%s expected)' % '/'.join(kinds))
            return None
        if n['t'] not in kinds:
            self.problem(ev, 'statement is %s, event is %s' % (n['t'], ev.ev))
            return None
        return n

    # ---- events ----
    def on_event(self, ev):
        h = getattr(self, 'ev_' + ev.ev, None)
        if h:
            h(ev)

    def ev_script(self, ev):
        self.cur = self.script(ev.name)
        self.cur_page = None
        self.visible = None
        if self.cur is None:
            self.problem(ev, 'script %s is not in the pack' % ev.name)

    def ev_page(self, ev):
        n = self.node(ev, ('page',))
        self.cur_page = ev.a[0]
        if n and n['num'] != ev.a[0]:
            self.problem(ev, 'page %d entered, bytecode says PAGE %d' % (ev.a[0], n['num']))
        if self.cur:
            self.visited.add((self.cur.name, ev.a[0]))

    def ev_flag(self, ev):
        n = self.node(ev, ('set',))
        if n and (n['flag'], n['val'] != 0) != (ev.a[0], ev.a[1] != 0):
            self.problem(ev, 'flag F%d=%d set, bytecode says F%d=%d' % (ev.a[0], ev.a[1], n['flag'], n['val']))
        self.set_flag(ev.a[0], ev.a[1])

    def ev_msg(self, ev):
        if ev.pos is None:
            self.engine_messages.append(ev.text)
            return
        n = self.node(ev, ('msg',))
        if n is None:
            return
        if n['text'] != ev.text:
            self.problem(ev, 'message text differs: engine %r, bytecode %r' %
                         (ev.text_str()[:60], textenc.decode(n['text'])[:60]))
        self.shown.add((self.cur.name, ev.pos))

    def ev_menu(self, ev):
        if ev.a[1]:                       # engine menu (system menu, quit confirmation)
            self.visible = None
            return
        n = self.cur.by_pos.get(ev.pos - 1) if self.cur and ev.pos else None
        if n is None or n['t'] != 'menu':
            n = self.node(ev, ('menu',))
        if n is None:
            self.visible = None
            return
        vis = [o for o in n['opts'] if o['cond'] is None or self.cond_true(o['cond'])]
        self.visible = vis
        self.menu_node = n
        self.scores = self.cur.option_scores(n, vis, self.cond_true)
        want = [o['text'] for o in vis]
        if want != ev.items:
            self.problem(ev, 'menu options differ: engine %r, bytecode %r' %
                         ([textenc.decode(i) for i in ev.items],
                          [textenc.decode(w) for w in want]))
        if not 0 <= ev.a[0] < max(1, len(vis)):
            self.problem(ev, 'menu cursor %d out of range' % ev.a[0])

    def ev_label(self, ev):
        sel = ev.a[1]
        if self.visible is None:
            return
        if not 0 <= sel < len(self.visible):
            self.problem(ev, 'selection %d outside the visible options' % sel)
            return
        if self.visible[sel]['id'] != ev.a[0]:
            self.problem(ev, 'selected option %d leads to D%d, bytecode says D%d' %
                         (sel, ev.a[0], self.visible[sel]['id']))

    def ev_op(self, ev):
        op = ev.a[0]
        n = self.node(ev, ('op', 'unk'))
        if n is None:
            return
        if n['t'] == 'unk':
            self.problem(ev, 'engine executed op %02X that ccdis treats as unknown' % op)
            return
        if n['op'] != op:
            self.problem(ev, 'engine op %02X, bytecode %s' % (op, OP_NAMES.get(n['op'], '%02X' % n['op'])))
            return
        ints = ev.ints or []
        strs = []
        want_ints = []
        for a in n['args']:
            if isinstance(a, bytes):
                strs.append(a)
            else:
                want_ints.append(a)
        for i, w in enumerate(want_ints):
            if w is None:
                continue
            if i >= len(ints) or ints[i] != w:
                self.problem(ev, '%s operand %d: engine %r, bytecode %r' % (OP_NAMES[op], i, ints, want_ints))
                break
        if strs:
            got = (ev.name or '').encode('latin-1')
            if got != strs[0]:
                self.problem(ev, '%s string: engine %r, bytecode %r' % (OP_NAMES[op], got, strs[0]))
            if op in OP_ASSET:
                nm = strs[0].decode('latin-1').upper()
                if nm not in self.assets.get(OP_ASSET[op], ()):
                    if (self.cur.name, op, nm) in KNOWN_DATA_BUGS:
                        self.notes.append('%s: known data bug, %s names the missing %s' % (self.cur.name, OP_NAMES[op], nm))
                    else:
                        self.problem(ev, '%s names %s which is not in the pack' % (OP_NAMES[op], nm))

    def ev_gdt(self, ev):
        if not ev.a[1]:
            self.problem(ev, 'image %s could not be loaded' % ev.name)

    def ev_load(self, ev):
        if not ev.a[0]:
            return
        path = os.path.join(self.storage, ev.name)
        try:
            buf = open(path, 'rb').read()
        except OSError:
            self.problem(ev, 'LOAD %s reported success but %s is missing' % (ev.name, path))
            return
        if buf[:4] != b'XSAV':
            self.problem(ev, 'save %s is not an XSAV record' % ev.name)
            return
        self.flags = bytearray(buf[8:8 + 256])
        script = buf[264:304].split(b'\0')[0].decode('ascii', 'replace')
        page = struct.unpack_from('<H', buf, 304)[0]
        self.cur = self.script(script)
        self.cur_page = page
        self.visible = None

    def ev_error(self, ev):
        self.problem(ev, 'script error %d (page %d label %d)' % (ev.a[0], ev.a[1], ev.a[2]))

    def ev_box(self, ev):
        if ev.a[0]:
            self.problem(ev, 'error box: %s' % ev.text_str())

    # ---- report ----
    def coverage(self):
        pages = {}
        for name in self.scripts:
            s = self.script(name)
            pages[name] = (len([p for (n, p) in self.visited if n == name]), len(s.pages))
        return pages
