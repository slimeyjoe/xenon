#!/usr/bin/env python3
"""ccdis.py - disassembler / structured decompiler for Xenon (AGS.EXE, C's Ware 1994) .CC scripts.

Reads an original compressed .CC file ("LCZ" container, see lzss.py) or an already decompressed
bytecode file (*.bin / *.U.CC, detected by the missing "LCZ" magic), and emits a structured
listing: pages, IF blocks, SEN (menu) blocks with their D-labels (cases), every statement with
its file offset (offset into the *decompressed* bytecode), Japanese text decoded from cp932.

The grammar implemented here follows the AGS.EXE interpreter in segment 088D exactly
(see docs/re/scripts.md and docs/re/opcodes.md):

    file    := junk* page*                       (088D:02CB: execution starts at the first 01)
    page    := 01 FB<n> stmt* 02                 (PAGE n ... PEND)
    stmt    := FD str 00                         (message)
             | FE<f> 12 FB<v>                    (flag assignment)
             | 13 cond 14 0A stmt* 0B            (IF cond THEN ... END)
             | 1B 04 option* 05 case*            (SEN ... SENEND  + labelled cases)
             | op operands                       (see OPS)
    cond    := FE<f> 12 FB<v> (15 FE<f> 12 FB<v>)*
    option  := [13 cond 14] FD text 00 FF<id>
    case    := FF<id> stmt* 08                   (label D<id> ... END)

Usage:
    python3 -I ccdis.py FILE.CC                       listing to stdout
    python3 -I ccdis.py --raw FILE.CC                 also show raw token bytes per statement
    python3 -I ccdis.py --check FILE.CC ...           parse check: unknown ops, coverage, structure
    python3 -I ccdis.py --export OUTDIR FILE.CC ...   write OUTDIR/{decompressed,listing,strings}/NAME.*
                                                      plus OUTDIR/assets.tsv, flags.tsv, summary.tsv,
                                                      graph.tsv
"""
import os
import sys
import collections

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
# the LZSS codec and the tokenizer are the build's (source/tools)
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'source', 'tools'))
import lzss  # noqa: E402
from cctok import ParseError, Tok, tokenize  # noqa: E402,F401

# ---------------------------------------------------------------------------------------------
# Opcode table.  operand spec: i = FB int16 (mandatory), o = optional FB int16 (default 1),
# s = FD string.  handler = dispatch target in 088D (table 088D:094C/09B6), callee = engine fn.
# ---------------------------------------------------------------------------------------------
OPS = {
    # byte: (mnemonic, operands, handler, callee, short description)
    0x03: ('CLEAR_LOWER', '', '088D:078A', '0B7D:0852', 'clear graphics rows y>=0x128 (message area)'),
    0x06: ('JMP', 'i', '088D:06E4', '088D:0ECD', 'jump to PAGE n of this file'),
    0x07: ('FILE', 's', '088D:071A', '-', 'chain to script file <name>.cc'),
    0x0C: ('G_LOAD', 's', '088D:07B5', '0C2A:0315', 'load <name>.gdt to off-screen buffer + set palette'),
    0x0D: ('G_DISP', '', '088D:07C7', '0C2A:0649', 'copy off-screen image rect to VRAM'),
    0x0E: ('MUSIC_LOAD', 's', '088D:0857', '0B2C:01C4', 'load <name>.M into PMD'),
    0x0F: ('MUSIC_PLAY', '', '088D:0860', '0B2C:02E5', 'PMD AH=00 music start'),
    0x10: ('MUSIC_STOP', '', '088D:0869', '0B2C:033C', 'PMD AH=01 music stop'),
    0x11: ('WINDOW', 'i', '088D:07A3', '0C2A:07F0/0DCA:04E3', '1: load waku2 + shutter-close CG area, 0: clear graphics page'),
    0x16: ('G_SABUN_LOAD', 's', '088D:07BE', '0C2A:04AB', 'load difference image <name>.gdt + palette'),
    0x19: ('SAVE', 's', '088D:0778', '088D:126A', 'write save file <name>.dat'),
    0x1C: ('LOAD', 's', '088D:076F', '088D:1441', 'read save file <name>.dat and resume'),
    0x1D: ('DISK', 's', '088D:0792', '088D:0F9C', 'floppy mode: ask for disk <letter>'),
    0x1F: ('EXIT', '', '088D:090C', '-', 'stop interpreter (DS:13A6=1) -> back to title'),
    0x20: ('WAIT', 'i', '088D:08AE', '0DCA:0012', 'wait n vsyncs'),
    0x21: ('FADEIN', 'o', '088D:08B6', '0DCA:02F7', 'fade palette to loaded palette, speed n'),
    0x22: ('FADEOUT', 'o', '088D:08BE', '0DCA:0445', 'fade palette to black, speed n'),
    0x23: ('G_CLEAR', '', '088D:08C6', '0C2A:05B2', 'clear off-screen plane buffers'),
    0x24: ('G_EFFECT', 'i', '088D:079A', '0C2A:075E/0C2A:0690', 'show off-screen image with wipe (1 centre-open, 0 blinds)'),
    0x26: ('GRAPH_OFF', '', '088D:07D0', '0DCA:00D9', 'INT 18h AH=41h graphics display off'),
    0x27: ('GRAPH_ON', '', '088D:07D9', '0DCA:00BF', 'INT 18h AH=40h graphics display on'),
    0x28: ('G_LOAD_OVL', 's', '088D:07AC', '0C2A:00DB(name,1)', 'decode <name>.gdt, no palette/rect update'),
    0x29: ('OPENING', 'o', '088D:0914', '0E31:04AE', 'canned sequence: 0100b.adt + music xen_03a'),
    0x2A: ('ADT_LOAD', 's', '088D:07FD', '0ED8:0078', 'load animation <name>.adt'),
    0x2B: ('ADT_FREE', '', '088D:07F4', '0ED8:0217', 'free animation'),
    0x2D: ('ADT_PLAY', '', '088D:0806', '0ED8:0001', 'play loaded animation'),
    0x2E: ('ANIM_START', 'iiiiii', '088D:080F', '0ED8:036F', 'blink_start(sx,sy,dx,dy,w,h): vsync eye-blink strip animation'),
    0x2F: ('ANIM_STOP', '', '088D:0818', '0ED8:03CB', 'stop vsync animation'),
    0x30: ('MUSIC_FADE', 'i', '088D:0896', '0B2C:0386', 'PMD AH=02 fade out, speed n'),
    0x31: ('MUSIC_FADEWAIT', '', '088D:089F', '0B2C:0427', 'fade music (speed 25), wait, stop'),
    0x32: ('G_SCROLL', 'i', '088D:08CE', '0B7D:0A68', 'graphics GDC (A0h/A2h) SCROLL: display starts at line n (screen shake)'),
    0x33: ('G_LOAD_BUF', 's', '088D:0821', '0C2A:00DB(name,0)', 'same as 28 (callee ignores the int)'),
    0x34: ('G_LOAD_PARTS', 's', '088D:082A', '0C2A:0142', 'decode <name>.gdt straight into VRAM page 1'),
    0x35: ('G_PARTS_COPY', '', '088D:0833', '0C2A:01F9', 'merge page-1 rect into buffers (colour 8 transparent)'),
    0x36: ('PALETTE', 'iiii', '088D:083C', '0DCA:0200', 'set palette entry idx to (r,g,b)'),
    0x37: ('COMPLETE', 'i', '088D:0904', '088D:1C19', 'XEN.GDT |= 1<<(n-1) (route completion)'),
    0x38: ('TEXTPOS', 'iiii', '088D:08D6', '-', 'text window x,y and menu x,y (DS:07D8/07DA/07E0/07E2)'),
    0x39: ('TEXTCOLOR', 'ii', '088D:084E', '-', 'text colours DS:07DC, DS:07DE'),
    0x3A: ('MUSIC_VOL', 'i', '088D:088D', '0B2C:0482', 'PMD AH=19h AL=n fade volume (0 full .. FFh silent)'),
    0x40: ('MSG_CLEAR', '', '088D:0845', '0B7D:0783', 'clear message window'),
    0x42: ('FADEWHITE', 'o', '088D:08A6', '0DCA:0494', 'fade palette to white, delay n per step'),
    0x43: ('VRAM_WPAGE', 'i', '088D:07EB', '0DCA:0150', 'out 0A6h: VRAM drawing page'),
    0x44: ('VRAM_DPAGE', 'i', '088D:07E2', '0DCA:0162', 'out 0A4h: VRAM display page'),
    0x45: ('SE_PLAY', 'i', '088D:0872', '0B2C:043C', 'PMD AH=0Ch FM sound effect n'),
    0x46: ('SE_STOP', '', '088D:087B', '0B2C:0461', 'PMD AH=0Dh sound effect off'),
    0x47: ('STAFFROLL', '', '088D:08FD', '0E31:05D2', 'canned ending/staff-roll sequence'),
    0x48: ('MUSIC_WAIT', 'i', '088D:0884', '0B2C:04D8', 'wait while PMD AH=0Ah status1 == n'),
}
# structural tokens (no dispatch entry or a terminator entry)
STRUCT = {0x01: 'PAGE', 0x02: 'PEND', 0x04: 'SEN', 0x05: 'SENEND', 0x08: 'END', 0x0A: 'THEN',
          0x0B: 'ENDIF', 0x12: '=', 0x13: 'IF(', 0x14: ')', 0x15: '&&', 0x1B: 'SEN-MARK'}

ASSET_KIND = {0x07: 'script', 0x0C: 'gdt', 0x16: 'gdt', 0x28: 'gdt', 0x33: 'gdt', 0x34: 'gdt',
              0x0E: 'music', 0x2A: 'adt', 0x19: 'save', 0x1C: 'save', 0x1D: 'disk'}


# ---------------------------------------------------------------------------------------------
def load(path):
    raw = open(path, 'rb').read()
    if raw[:3] == b'LCZ':
        return lzss.unpack_file(raw), True
    return raw, False


# ---------------------------------------------------------------------------------------------
def sjis_text(b, escapes=True):
    """Decode cp932 message text, rendering engine control codes (0B7D:019B) as {..} escapes."""
    out = []
    i = 0
    n = len(b)
    while i < n:
        c = b[i]
        if (0x81 <= c <= 0x9F) or (0xE0 <= c <= 0xFC):
            pair = b[i:i + 2]
            try:
                out.append(pair.decode('cp932'))
            except UnicodeDecodeError:
                out.append('{SJ:%s}' % pair.hex().upper())
            i += 2
            continue
        if escapes:
            if c == 0x0A:
                out.append('\\n')
            elif c == 0x04 and i + 1 < n:
                out.append('{W%s}' % chr(b[i + 1]))
                i += 1
            elif c == 0x05:
                out.append('{KEEP}')
            elif c == 0x0C:
                out.append('{KEY}')
            elif c == 0x03:
                out.append('{BIG}')
            elif c < 0x20 or c >= 0x7F:
                out.append('{%02X}' % c)
            elif c == 0x22 or c == 0x5C:
                out.append('\\' + chr(c))
            else:
                out.append(chr(c))
        else:
            out.append(chr(c) if 0x20 <= c < 0x7F else '{%02X}' % c)
        i += 1
    return ''.join(out)


# ---------------------------------------------------------------------------------------------
# AST nodes are dicts: {'t': type, 'pos': start, 'end': end, ...}

class Parser:
    def __init__(self, data):
        self.d = data
        self.t = tokenize(data)
        self.i = 0
        self.warn = []
        self.unknown = []

    def peek(self, k=0):
        j = self.i + k
        return self.t[j] if j < len(self.t) else None

    def take(self):
        x = self.t[self.i]
        self.i += 1
        return x

    def expect(self, kind, val=None, what=''):
        x = self.peek()
        if x is None or x.kind != kind or (val is not None and x.val != val):
            raise ParseError('expected %s at %04X, got %s' % (
                what or kind, x.pos if x else len(self.d), self.desc(x)))
        return self.take()

    @staticmethod
    def desc(x):
        if x is None:
            return 'EOF'
        if x.kind == 'op':
            return 'op %02X' % x.val
        return '%s %r' % (x.kind, x.val)

    # --- grammar ---------------------------------------------------------------------------
    def parse_file(self):
        nodes = []
        # junk before the first PAGE is never executed (088D:02CB finds the first 01)
        while self.peek() is not None and not self.peek().is_op(0x01):
            x = self.take()
            nodes.append({'t': 'junk', 'pos': x.pos, 'end': x.end})
            self.warn.append('%04X: token before first PAGE' % x.pos)
        while self.peek() is not None:
            nodes.append(self.page())
        return nodes

    def page(self):
        p = self.expect('op', 0x01, 'PAGE (01)')
        n = self.expect('int', None, 'page number FB')
        body = self.block({0x02})
        e = self.expect('op', 0x02, 'PEND (02)')
        return {'t': 'page', 'pos': p.pos, 'end': e.end, 'num': n.val, 'body': body}

    def cond(self):
        """after 13: (FE f 12 FB v) joined by 15, until 14 (088D:1104)."""
        terms = []
        while True:
            x = self.peek()
            if x is None:
                raise ParseError('EOF in condition')
            if x.is_op(0x14):
                self.take()
                return terms
            if x.is_op(0x15):
                self.take()
                continue
            v = self.expect('var', None, 'FE flag in condition')
            self.expect('op', 0x12, '12 in condition')
            c = self.expect('int', None, 'FB value in condition')
            terms.append((v.val, c.val))

    def block(self, stops):
        body = []
        while True:
            x = self.peek()
            if x is None:
                raise ParseError('EOF inside block (wanted %s)' % '/'.join('%02X' % s for s in stops))
            if x.kind == 'op' and x.val in stops:
                return body
            body.append(self.stmt())

    def stmt(self):
        x = self.peek()
        pos = x.pos
        if x.kind == 'str':
            self.take()
            return {'t': 'msg', 'pos': pos, 'end': x.end, 'text': x.val, 'lenbyte': x.lenbyte}
        if x.kind == 'var':
            self.take()
            self.expect('op', 0x12, '12 in assignment')
            v = self.expect('int', None, 'FB in assignment')
            return {'t': 'set', 'pos': pos, 'end': v.end, 'flag': x.val, 'val': v.val}
        if x.kind != 'op':
            raise ParseError('unexpected %s at %04X' % (self.desc(x), pos))
        op = x.val
        if op == 0x1B:
            return self.menu()
        if op == 0x13:
            self.take()
            c = self.cond()
            self.expect('op', 0x0A, 'THEN (0A)')
            body = self.block({0x0B})
            e = self.expect('op', 0x0B, 'END (0B)')
            return {'t': 'if', 'pos': pos, 'end': e.end, 'cond': c, 'body': body}
        if op in OPS:
            self.take()
            mn, spec, _, _, _ = OPS[op]
            args = []
            end = x.end
            for s in spec:
                y = self.peek()
                if s in 'io':
                    if y is not None and y.kind == 'int':
                        self.take()
                        args.append(y.val)
                        end = y.end
                    elif s == 'o':
                        args.append(None)  # engine default 1
                    else:
                        raise ParseError('%s at %04X: missing int operand' % (mn, pos))
                elif s == 's':
                    if y is None or y.kind != 'str':
                        raise ParseError('%s at %04X: missing string operand' % (mn, pos))
                    self.take()
                    args.append(y.val)
                    end = y.end
            return {'t': 'op', 'pos': pos, 'end': end, 'op': op, 'args': args}
        # not in dispatch table: the engine silently skips one statement (088D:091C)
        self.take()
        self.unknown.append((pos, op))
        return {'t': 'unk', 'pos': pos, 'end': x.end, 'op': op}

    def menu(self):
        m = self.expect('op', 0x1B, 'SEN marker 1B')
        self.expect('op', 0x04, 'SEN (04)')
        opts = []
        while True:
            x = self.peek()
            if x is None:
                raise ParseError('EOF in SEN')
            if x.is_op(0x05):
                self.take()
                break
            c = None
            opos = x.pos
            if x.is_op(0x13):
                self.take()
                c = self.cond()
            s = self.expect('str', None, 'option text')
            i = self.expect('id', None, 'option label FF')
            opts.append({'pos': opos, 'text': s.val, 'id': i.val, 'cond': c, 'end': i.end})
        if len(opts) > 9:
            self.warn.append('%04X: SEN with %d options (engine limit 9, error 12)' % (m.pos, len(opts)))
        cases = []
        while self.peek() is not None and self.peek().kind == 'id':
            lab = self.take()
            body = self.block({0x08})
            e = self.expect('op', 0x08, 'END (08)')
            cases.append({'pos': lab.pos, 'end': e.end, 'id': lab.val, 'body': body})
        end = cases[-1]['end'] if cases else self.t[self.i - 1].end
        return {'t': 'menu', 'pos': m.pos, 'end': end, 'opts': opts, 'cases': cases}


def parse(data):
    p = Parser(data)
    ast = p.parse_file()
    return ast, p


# ---------------------------------------------------------------------------------------------
def fmt_cond(c):
    return ' && '.join('F%d == %d' % (f, v) for f, v in c)


def fmt_arg(a):
    if a is None:
        return '(1)'
    if isinstance(a, bytes):
        return '"%s"' % sjis_text(a)
    return str(a)


def walk(nodes, fn, ctx=()):
    for n in nodes:
        fn(n, ctx)
        t = n['t']
        if t == 'page':
            walk(n['body'], fn, ctx + (('page', n['num']),))
        elif t == 'if':
            walk(n['body'], fn, ctx + (('if', fmt_cond(n['cond'])),))
        elif t == 'menu':
            for c in n['cases']:
                fn({'t': 'case', 'pos': c['pos'], 'end': c['end'], 'id': c['id'], 'menu': n}, ctx)
                walk(c['body'], fn, ctx + (('case', c['id']),))


class Lister:
    def __init__(self, data, raw=False):
        self.d = data
        self.raw = raw
        self.lines = []

    def rawb(self, a, b):
        if not self.raw:
            return ''
        h = self.d[a:b].hex(' ').upper()
        return '  ; ' + (h if len(h) <= 60 else h[:57] + '...')

    def emit(self, pos, depth, text, a=None, b=None):
        self.lines.append('%04X  %s%s%s' % (pos, '    ' * depth, text,
                                             self.rawb(a, b) if a is not None else ''))

    def nodes(self, nodes, depth):
        for n in nodes:
            self.node(n, depth)

    def node(self, n, depth):
        t = n['t']
        if t == 'junk':
            self.emit(n['pos'], depth, '; (not executed) byte %02X' % self.d[n['pos']])
        elif t == 'page':
            self.lines.append('')
            self.emit(n['pos'], depth, 'PAGE %d' % n['num'], n['pos'], n['pos'] + 4)
            self.nodes(n['body'], depth + 1)
            self.emit(n['end'] - 1, depth, 'PEND')
        elif t == 'msg':
            self.emit(n['pos'], depth, 'MSG "%s"' % sjis_text(n['text']), n['pos'], n['end'])
        elif t == 'set':
            self.emit(n['pos'], depth, 'F%d = %d' % (n['flag'], n['val']), n['pos'], n['end'])
        elif t == 'if':
            self.emit(n['pos'], depth, 'IF %s THEN' % fmt_cond(n['cond']))
            self.nodes(n['body'], depth + 1)
            self.emit(n['end'] - 1, depth, 'END')
        elif t == 'op':
            mn = OPS[n['op']][0]
            args = ', '.join(fmt_arg(a) for a in n['args'])
            s = mn + (' ' + args if args else '')
            self.emit(n['pos'], depth, s, n['pos'], n['end'])
        elif t == 'unk':
            self.emit(n['pos'], depth, '?? op %02X (skipped by engine)' % n['op'])
        elif t == 'menu':
            self.emit(n['pos'], depth, 'SEN')
            for o in n['opts']:
                c = ('IF %s: ' % fmt_cond(o['cond'])) if o['cond'] else ''
                self.emit(o['pos'], depth + 1, '%s"%s" -> D%d' % (c, sjis_text(o['text']), o['id']))
            self.emit(n['opts'][-1]['end'] if n['opts'] else n['pos'] + 2, depth, 'SENEND')
            for c in n['cases']:
                self.emit(c['pos'], depth, 'D%d:' % c['id'])
                self.nodes(c['body'], depth + 1)
                self.emit(c['end'] - 1, depth + 1, 'END')


def listing(name, data, compressed_info, ast, p, raw=False):
    L = Lister(data, raw)
    hdr = ['; %s - Xenon AGS script, %d bytes of bytecode' % (name, len(data))]
    if compressed_info:
        hdr.append('; %s' % compressed_info)
    hdr.append('; offsets are into the decompressed bytecode; flags F<n> are bit n of DS:00F2')
    hdr.append('; message escapes: \\n newline, {W<d>} timed wait d*60 vsyncs instead of the ▼ key wait,')
    hdr.append(';   {KEEP} do not clear the window after this message, {KEY} wait for key (no ▼)')
    L.lines.extend(hdr)
    L.nodes(ast, 0)
    for w in p.warn:
        L.lines.append('; WARNING ' + w)
    return '\n'.join(L.lines) + '\n'


# ---------------------------------------------------------------------------------------------
def coverage(data, ast):
    cov = bytearray(len(data))

    def mark(a, b):
        for k in range(a, b):
            cov[k] += 1
    # mark every token span reachable from the AST: simply mark node spans at the leaf level
    def leaf(n, ctx):
        t = n['t']
        if t in ('msg', 'set', 'op', 'unk', 'junk'):
            mark(n['pos'], n['end'])
        elif t == 'page':
            mark(n['pos'], n['pos'] + 4)       # 01 FB lo hi
            mark(n['end'] - 1, n['end'])       # 02
        elif t == 'if':
            # 13 cond 14 0A ... 0B
            body = n['body']
            hdr_end = body[0]['pos'] if body else n['end'] - 1
            mark(n['pos'], hdr_end)
            mark(n['end'] - 1, n['end'])
        elif t == 'menu':
            mark(n['pos'], n['pos'] + 2)       # 1B 04
            for o in n['opts']:
                mark(o['pos'], o['end'])
            se = n['opts'][-1]['end'] if n['opts'] else n['pos'] + 2
            mark(se, se + 1)                   # 05
        elif t == 'case':
            mark(n['pos'], n['pos'] + 3)       # FF lo hi
            mark(n['end'] - 1, n['end'])       # 08
    walk(ast, leaf)
    missing = [k for k, c in enumerate(cov) if c == 0]
    double = [k for k, c in enumerate(cov) if c > 1]
    return missing, double


def semantic_checks(ast):
    """Checks mirroring engine behaviour that a well-formed script must satisfy."""
    out = []
    pages = {}
    for n in ast:
        if n['t'] == 'page':
            if n['num'] in pages:
                out.append('%04X: duplicate PAGE %d (first one wins in 088D:0ECD)' % (n['pos'], n['num']))
            pages.setdefault(n['num'], n['pos'])

    def ends_with_transfer(body):
        if not body:
            return False
        last = body[-1]
        if last['t'] == 'if' and last['cond'] == [(0, 0)]:
            # "IF F0 == 0" idiom: F0 is never set, so the block is unconditional
            return ends_with_transfer(last['body'])
        return last['t'] == 'op' and last['op'] in (0x06, 0x07, 0x1F, 0x1C)

    def chk(n, ctx):
        if n['t'] == 'op' and n['op'] == 0x06 and n['args'][0] not in pages:
            out.append('%04X: JMP %d to missing page (engine error 9, fatal)' % (n['pos'], n['args'][0]))
        if n['t'] == 'if' and not ends_with_transfer(n['body']):
            out.append('%04X: IF body does not end with JMP/FILE/EXIT (engine returns early)' % n['pos'])
        if n['t'] == 'menu':
            ids = [o['id'] for o in n['opts']]
            cids = [c['id'] for c in n['cases']]
            for i in ids:
                if i not in cids:
                    out.append('%04X: option D%d has no case (engine error 11)' % (n['pos'], i))
    walk(ast, chk)

    # the last page must not fall through (088D:035A would run past the buffer -> debug1, fatal)
    plist = [n for n in ast if n['t'] == 'page']
    if plist:
        last = plist[-1]['body']
        if not (ends_with_transfer(last) or (last and last[-1]['t'] == 'menu')):
            out.append('PAGE %d (last page) falls off the end of the file' % plist[-1]['num'])

    # label uniqueness per page (case search 088D:0F2E scans linearly from SENEND to PEND)
    for pg in ast:
        if pg['t'] != 'page':
            continue
        seen = collections.Counter()

        def lab(n, ctx):
            if n['t'] == 'case':
                seen[n['id']] += 1
        walk(pg['body'], lab)
        for k, v in seen.items():
            if v > 1:
                out.append('PAGE %d: label D%d defined %d times' % (pg['num'], k, v))
    return out


# ---------------------------------------------------------------------------------------------
def collect(name, data, ast):
    """strings, assets, flag use, jumps, menus for export."""
    strings, assets, flags, jumps, menus = [], [], collections.defaultdict(lambda: [0, 0]), [], []

    def f(n, ctx):
        page = next((v for k, v in reversed(ctx) if k == 'page'), None)
        if n['t'] == 'page':
            page = n['num']
        t = n['t']
        if t == 'msg':
            strings.append((n['pos'], page, 'msg', sjis_text(n['text'])))
        elif t == 'set':
            flags[n['flag']][1] += 1
        elif t == 'if':
            for fl, v in n['cond']:
                flags[fl][0] += 1
        elif t == 'menu':
            menus.append((n['pos'], page, len(n['opts']), sum(1 for o in n['opts'] if o['cond']),
                          len([c for k, c in ctx if k == 'case'])))
            for o in n['opts']:
                strings.append((o['pos'], page, 'option', sjis_text(o['text'])))
                if o['cond']:
                    for fl, v in o['cond']:
                        flags[fl][0] += 1
        elif t == 'op':
            op = n['op']
            for a in n['args']:
                if isinstance(a, bytes):
                    nm = sjis_text(a)
                    strings.append((n['pos'], page, 'arg:' + OPS[op][0], nm))
                    if op in ASSET_KIND:
                        assets.append((name, n['pos'], OPS[op][0], ASSET_KIND[op], nm))
            if op == 0x06:
                jumps.append((n['pos'], page, 'page', n['args'][0], ctx))
            elif op == 0x07:
                jumps.append((n['pos'], page, 'file', sjis_text(n['args'][0]).lower(), ctx))
            elif op == 0x1F:
                jumps.append((n['pos'], page, 'exit', None, ctx))
            elif op == 0x47:
                jumps.append((n['pos'], page, 'staffroll', None, ctx))
            elif op == 0x29:
                assets.append((name, n['pos'], 'OPENING', 'adt', '0100b (built in)'))
            elif op == 0x37:
                jumps.append((n['pos'], page, 'complete', n['args'][0], ctx))
    walk(ast, f)
    return strings, assets, flags, jumps, menus


def ctx_str(ctx):
    parts = []
    for k, v in ctx:
        if k == 'page':
            parts.append('P%d' % v)
        elif k == 'case':
            parts.append('D%d' % v)
        elif k == 'if':
            parts.append('IF[%s]' % v)
    return '/'.join(parts)


# ---------------------------------------------------------------------------------------------
def do_check(paths):
    allok = True
    for path in paths:
        name = os.path.basename(path).split('.')[0].upper()
        try:
            data, comp = load(path)
            ast, p = parse(data)
        except ParseError as e:
            print('%-8s PARSE ERROR %s' % (name, e))
            allok = False
            continue
        missing, double = coverage(data, ast)
        sem = semantic_checks(ast)
        lens = []

        def lb(n, ctx):
            if n['t'] == 'msg' and n['lenbyte'] != len(n['text']):
                lens.append(n['pos'])
        walk(ast, lb)
        rt = ''
        if comp:
            raw = open(path, 'rb').read()
            rt = 'lzss-roundtrip=%s' % ('ok' if lzss.compress(data) == raw[0x18:] else 'DIFF')
        ok = not p.unknown and not missing and not double and not p.warn
        allok &= ok
        print('%-8s %6d bytes  pages=%-3d unknown_ops=%d uncovered=%d overlap=%d warn=%d lenbyte_mismatch=%d %s'
              % (name, len(data), sum(1 for n in ast if n['t'] == 'page'), len(p.unknown),
                 len(missing), len(double), len(p.warn), len(lens), rt))
        for w in p.warn + sem:
            print('         note: ' + w)
    return allok


def do_export(outdir, paths):
    for sub in ('decompressed', 'listing', 'strings'):
        os.makedirs(os.path.join(outdir, sub), exist_ok=True)
    all_assets = []
    flagrows = []
    summary = []
    graph = []
    for path in sorted(paths):
        name = os.path.basename(path).split('.')[0].upper()
        data, comp = load(path)
        ast, p = parse(data)
        info = None
        if comp:
            raw = open(path, 'rb').read()
            info = 'source %s: %d bytes LCZ, header filler %s' % (
                os.path.basename(path), len(raw), raw[3:0x14].hex())
        open(os.path.join(outdir, 'decompressed', name + '.bin'), 'wb').write(data)
        open(os.path.join(outdir, 'listing', name + '.txt'), 'w', encoding='utf-8').write(
            listing(name, data, info, ast, p))
        strings, assets, flags, jumps, menus = collect(name, data, ast)
        with open(os.path.join(outdir, 'strings', name + '.tsv'), 'w', encoding='utf-8') as fh:
            fh.write('offset\tpage\tkind\ttext\n')
            for pos, page, kind, text in strings:
                fh.write('%04X\t%s\t%s\t%s\n' % (pos, page, kind, text))
        all_assets += assets
        for fl in sorted(flags):
            flagrows.append((fl, name, flags[fl][0], flags[fl][1]))
        pages = [n['num'] for n in ast if n['t'] == 'page']
        files = sorted(set(j[3] for j in jumps if j[2] == 'file'))
        summary.append((name, len(data), len(pages), sum(1 for s in strings if s[2] == 'msg'),
                        len(menus), sum(m[2] for m in menus),
                        ','.join(files), sum(1 for j in jumps if j[2] == 'exit'),
                        ','.join(str(j[3]) for j in jumps if j[2] == 'complete'),
                        sum(1 for j in jumps if j[2] == 'staffroll'),
                        len([f for f in flags if flags[f][1]]), len([f for f in flags if flags[f][0]])))
        for pos, page, kind, tgt, ctx in jumps:
            if kind in ('file', 'exit', 'staffroll', 'complete'):
                graph.append((name, '%04X' % pos, ctx_str(ctx), kind, '' if tgt is None else str(tgt)))
    with open(os.path.join(outdir, 'assets.tsv'), 'w', encoding='utf-8') as fh:
        fh.write('script\toffset\topcode\tkind\tname\n')
        for r in all_assets:
            fh.write('%s\t%04X\t%s\t%s\t%s\n' % r)
    with open(os.path.join(outdir, 'flags.tsv'), 'w', encoding='utf-8') as fh:
        fh.write('flag\tscript\treads\twrites\n')
        for r in sorted(flagrows):
            fh.write('F%d\t%s\t%d\t%d\n' % r)
    with open(os.path.join(outdir, 'summary.tsv'), 'w', encoding='utf-8') as fh:
        fh.write('script\tbytes\tpages\tmessages\tmenus\toptions\tfile_jumps\texits\tcomplete\t'
                 'staffroll\tflags_written\tflags_tested\n')
        for r in summary:
            fh.write('\t'.join(str(x) for x in r) + '\n')
    with open(os.path.join(outdir, 'graph.tsv'), 'w', encoding='utf-8') as fh:
        fh.write('script\toffset\tcontext\tkind\ttarget\n')
        for r in graph:
            fh.write('\t'.join(r) + '\n')


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        sys.exit(2)
    if argv[1] == '--check':
        sys.exit(0 if do_check(argv[2:]) else 1)
    if argv[1] == '--export':
        do_export(argv[2], argv[3:])
        return
    raw = False
    args = argv[1:]
    if args[0] == '--raw':
        raw = True
        args = args[1:]
    for path in args:
        name = os.path.basename(path).split('.')[0].upper()
        data, comp = load(path)
        ast, p = parse(data)
        sys.stdout.write(listing(name, data, None, ast, p, raw))


if __name__ == '__main__':
    main(sys.argv)
