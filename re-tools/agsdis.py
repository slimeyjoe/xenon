#!/usr/bin/env python3
"""agsdis.py - recursive-descent disassembler for Xenon's AGS.EXE (Borland C++ 1991, 16-bit MZ).

Also accepts a .COM file (e.g. PMD.COM): loaded at 0000:0100, entry 0000:0100, no DGROUP.

Usage: agsdis.py AGS.EXE names.tsv out_listing.asm [out_xrefs.tsv]

- Load image = file[hdr*16:], segment values are relative paragraphs (load base 0).
- Seeds: entry point, every far call (9A) target, every relocated far pointer in code,
  extra seeds/names from names.tsv ("SSSS:OOOO<TAB>name<TAB>comment").
- Handles Borland switch forms:
    cmp bx,N / ja / shl bx,1 / jmp cs:[bx+T]           (dense table)
    mov cx,N / mov bx,T / cs:mov ax,[bx] / cmp ax,dx / jz / add bx,2 / loop / ... jmp cs:[bx+2N]  (value table)
- Annotates DGROUP (seg 0F9F) string references and global variables.
"""
import sys, struct, re
from capstone import Cs, CS_ARCH_X86, CS_MODE_16
from capstone.x86 import X86_OP_IMM, X86_OP_MEM

DGROUP = 0x0F9F

def load(path):
    d = open(path, 'rb').read()
    if d[:2] != b'MZ':                      # .COM: single segment 0000, image at offset 0x100
        return b'\0' * 0x100 + d, set(), (0, 0x100), (0, 0xFFFE)
    (sig, lastp, np_, nrel, hdrp, minal, maxal, ss, sp, csum, ip, cs, relo, ovl) = struct.unpack_from('<14H', d, 0)
    img = d[hdrp*16:]
    rel = [struct.unpack_from('<HH', d, relo+4*i) for i in range(nrel)]
    relset = set(s*16+o for o, s in rel)
    return img, relset, (cs, ip), (ss, sp)

def find_strings(img):
    base = DGROUP*16
    seg = img[base:base+0x10000]
    out = {}
    if DGROUP:
        pat = rb'[\x09\x0a\x0d\x1b\x20-\x7e\x81-\x9f\xa1-\xdf\xe0-\xfc][\x09\x0a\x0d\x1b\x20-\xfc]{2,}\x00'
    else:   # .COM programs use DOS '$'-terminated strings
        pat = rb'[\x09\x0a\x0d\x1b\x20-\x7e\x81-\x9f\xa1-\xdf\xe0-\xfc][\x09\x0a\x0d\x1b\x20-\x23\x25-\xfc]{3,}[\x00$]'
    for m in re.finditer(pat, seg):
        s = m.group()[:-1]
        try: t = s.decode('cp932')
        except UnicodeDecodeError: continue
        if sum(ch.isalnum() or ord(ch) > 0x3000 for ch in t) >= 2:
            out[m.start()] = t
    return out

class Dis:
    def __init__(self, img, relset, names):
        self.img, self.relset, self.names = img, relset, names
        self.md = Cs(CS_ARCH_X86, CS_MODE_16); self.md.detail = True
        self.insn = {}      # linear -> (seg, off, insn)
        self.data = {}      # linear -> ('dw', n, comment)
        self.funcs = {}     # linear -> (seg, off)
        self.ends = {}      # end linear -> start linear
        self.cases = {}
        self.indirect = []  # (seg, insn) of every indirect jmp, re-examined after tracing
        self.xref = {}      # target linear -> set of from linear
        self.strings = find_strings(img)

    def lin(self, s, o): return s*16+o

    def add_func(self, s, o):
        L = self.lin(s, o)
        if L >= len(self.img): return
        if L not in self.funcs:
            self.funcs[L] = (s, o); self.work.append((s, o))

    def run(self, seeds):
        self.work = []
        for s, o in seeds: self.add_func(s, o)
        while True:
            while self.work:
                s, o = self.work.pop()
                self.trace(s, o)
            # re-examine indirect jumps now that more preceding code is known
            before = len(self.insn)
            for seg, i in list(self.indirect):
                todo = []
                self.switch(seg, i, [], todo)
                for t in todo:
                    if self.lin(seg, t) not in self.insn: self.trace_from(seg, t)
            if len(self.insn) == before and not self.work: break

    def trace_from(self, seg, off):
        self.trace(seg, off)

    def trace(self, seg, off):
        todo = [off]
        while todo:
            o = todo.pop()
            hist = []
            while True:
                L = self.lin(seg, o)
                if L in self.insn or L in self.data or L >= len(self.img) or o > 0xffff: break
                code = self.img[L:L+16]
                try: i = next(self.md.disasm(code, o))
                except StopIteration:
                    self.data[L] = ('db', 1, 'undecodable'); break
                self.insn[L] = (seg, o, i)
                self.ends[L + i.size] = L
                hist.append(i)
                m, ops = i.mnemonic, i.op_str
                nxt = o + i.size
                if i.bytes[0] == 0x9a and i.size == 5:
                    to, ts = struct.unpack_from('<HH', i.bytes, 1)
                    self.add_func(ts, to); self.xr(self.lin(ts, to), L)
                elif m == 'call' and i.operands and i.operands[0].type == X86_OP_IMM:
                    t = i.operands[0].imm & 0xffff
                    self.add_func(seg, t); self.xr(self.lin(seg, t), L)
                elif m == 'call' and re.match(r'(ax|bx|cx|dx|si|di|bp)$', ops):
                    tt = []
                    self.switch(seg, i, hist, tt)
                    for t in tt: self.add_func(seg, t)
                elif m.startswith('j') or m in ('loop', 'loope', 'loopne', 'jcxz'):
                    if i.operands and i.operands[0].type == X86_OP_IMM:
                        t = i.operands[0].imm & 0xffff
                        todo.append(t); self.xr(self.lin(seg, t), L)
                        if m == 'jmp': break
                    elif m == 'jmp':
                        self.indirect.append((seg, i))
                        self.switch(seg, i, hist, todo); break
                    elif m == 'ljmp':
                        if i.bytes[0] == 0xea:
                            to, ts = struct.unpack_from('<HH', i.bytes, 1); self.add_func(ts, to)
                        break
                if m in ('ret', 'retf', 'iret', 'hlt'): break
                if m == 'int' and ops == '0x20': break
                o = nxt

    def xr(self, t, f): self.xref.setdefault(t, set()).add(f)

    def open_table(self, seg, T, at):
        # table of near targets running until the lowest target (table usually sits just before code)
        L0 = self.lin(seg, T); tg = []; lo = 0x10000; k = 0
        while k < 512:
            if T + 2*k >= lo: break
            v = struct.unpack_from('<H', self.img, L0+2*k)[0]
            if v < 0x10 or self.lin(seg, v) >= len(self.img): break
            tg.append(v); lo = min(lo, v); k += 1
        return tg

    def back(self, L, n=16):
        out = []
        while len(out) < n and L in self.ends:
            L = self.ends[L]; out.append(self.insn[L][2])
        return out[::-1]

    def switch(self, seg, i, hist, todo):
        Li = self.lin(seg, i.address)
        prev = self.back(Li)
        mm = re.match(r'word ptr cs:\[bx \+ (0x[0-9a-f]+)\]', i.op_str)
        if not mm and re.match(r'(ax|bx|cx|dx|si|di|bp)$', i.op_str) and prev:
            p = prev[-1]
            m3 = re.match(r'(\w+), word ptr (?:cs:)?\[(\w+) \+ (0x[0-9a-f]+)\]', p.op_str)
            if p.mnemonic == 'mov' and m3 and m3.group(1) == i.op_str:
                T = int(m3.group(3), 16)
                tg = self.open_table(seg, T, i.address)
                if tg:
                    self.data[self.lin(seg, T)] = ('dw', len(tg), 'jump table (register indirect)')
                    for t in tg: todo.append(t); self.xr(self.lin(seg, t), Li)
            return
        if not mm: return
        T = int(mm.group(1), 16)
        # value-table switch: mov cx,N ; mov bx,V ; ... jmp cs:[bx+2N]
        cx = bx = None
        for h in prev:
            m2 = re.match(r'(cx|bx), (0x[0-9a-f]+|\d+)$', h.op_str)
            if h.mnemonic == 'mov' and m2:
                if m2.group(1) == 'cx': cx = int(m2.group(2), 0)
                else: bx = int(m2.group(2), 0)
        if cx and bx is not None and 2*cx == T:
            V = self.lin(seg, bx)
            vals = [struct.unpack_from('<H', self.img, V+2*k)[0] for k in range(cx)]
            tg = [struct.unpack_from('<H', self.img, V+2*cx+2*k)[0] for k in range(cx)]
            self.data[V] = ('dw', cx, 'switch case values')
            self.data[V+2*cx] = ('dw', cx, 'switch case targets: ' + ', '.join('%X->%04X' % (v, t) for v, t in zip(vals, tg)))
            self.cases[V+2*cx] = list(zip(vals, tg))
            for t in tg: todo.append(t); self.xr(self.lin(seg, t), Li)
            return
        n = None
        for h in reversed(prev[-8:]):
            m2 = re.match(r'bx, (0x[0-9a-f]+|\d+)$', h.op_str)
            if h.mnemonic == 'cmp' and m2: n = int(m2.group(1), 0) + 1; break
        bias = 0
        for h in reversed(prev[-8:]):
            m2 = re.match(r'(ax|bx), (0x[0-9a-f]+|\d+)$', h.op_str)
            if h.mnemonic == 'sub' and m2: bias = int(m2.group(2), 0); break
        if n is None or n > 512: return
        L = self.lin(seg, T)
        tg = [struct.unpack_from('<H', self.img, L+2*k)[0] for k in range(n)]
        self.data[L] = ('dw', n, 'switch targets (case %#x..%#x): ' % (bias, bias+n-1) + ', '.join('%X->%04X' % (bias+k, t) for k, t in enumerate(tg)))
        for t in tg:
            todo.append(t); self.xr(self.lin(seg, t), Li)

    def name(self, s, o):
        k = '%04X:%04X' % (s, o)
        if k in self.names: return self.names[k][0]
        return 'sub_%04X_%04X' % (s, o)

    def gname(self, off):
        k = '%04X:%04X' % (DGROUP, off)
        if k in self.names: return self.names[k][0]
        return None

    def annotate(self, seg, i):
        notes = []
        if i.bytes[0] == 0x9a and i.size == 5:
            to, ts = struct.unpack_from('<HH', i.bytes, 1)
            notes.append(self.name(ts, to))
        elif i.mnemonic == 'call' and i.operands and i.operands[0].type == X86_OP_IMM:
            notes.append(self.name(seg, i.operands[0].imm & 0xffff))
        for op in i.operands:
            if op.type == X86_OP_IMM and i.mnemonic in ('push', 'mov') and op.size == 2:
                v = op.imm & 0xffff
                if v in self.strings and v >= 0x40:
                    notes.append('"%s"' % self.strings[v].replace('\n', '\\n').replace('\x1b', '\\e')[:60])
                elif self.gname(v):
                    notes.append('&' + self.gname(v))
            if op.type == X86_OP_MEM and op.mem.base == 0 and op.mem.index == 0 and i.prefix[1] in (0, 0x3e):
                v = op.mem.disp & 0xffff
                g = self.gname(v)
                if g: notes.append(g)
                elif v in self.strings: notes.append('"%s"' % self.strings[v][:40])
        return '; ' + ' | '.join(notes) if notes else ''

    def dump(self, out):
        items = sorted(set(self.insn) | set(self.data))
        f = open(out, 'w', encoding='utf-8')
        f.write('; AGS.EXE disassembly (generated by re-tools/agsdis.py). Addresses are SEG:OFF relative to load base 0;\n')
        f.write('; file offset = 0xE00 + SEG*16 + OFF. Ghidra (base 0x1000) address = (SEG+0x1000):OFF. DGROUP = 0F9F.\n\n')
        prev_end = None
        for L in items:
            if L in self.funcs:
                s, o = self.funcs[L]
                k = '%04X:%04X' % (s, o)
                cm = self.names.get(k, (None, ''))[1]
                xs = self.xref.get(L, ())
                f.write('\n;%s\n%s:  ; %04X:%04X  callers=%d %s\n' % ('-'*70, self.name(s, o), s, o, len(xs), ('; ' + cm) if cm else ''))
            if L in self.data:
                kind, n, cm = self.data[L]
                seg = self.seg_of(L)
                if kind == 'dw':
                    vals = [struct.unpack_from('<H', self.img, L+2*k)[0] for k in range(n)]
                    f.write('%04X:%04X  dw %s  ; %s\n' % (seg, L-seg*16, ', '.join('%04X' % v for v in vals), cm))
                else:
                    f.write('%04X:%04X  db %02X  ; %s\n' % (seg, L-seg*16, self.img[L], cm))
                continue
            seg, o, i = self.insn[L]
            if L in self.xref and L not in self.funcs:
                f.write('  loc_%04X_%04X:\n' % (seg, o))
            f.write('%04X:%04X  %-20s %-6s %-34s %s\n' % (seg, o, i.bytes.hex(), i.mnemonic, i.op_str, self.annotate(seg, i)))
        f.close()

    def seg_of(self, L):
        best = 0
        for s, o in self.funcs.values():
            if s*16 <= L and s > best: best = s
        return best

def read_names(p):
    names = {}
    try:
        for line in open(p, encoding='utf-8'):
            if not line.strip() or line.startswith('#'): continue
            parts = line.rstrip('\n').split('\t')
            names[parts[0].upper()] = (parts[1], parts[2] if len(parts) > 2 else '')
    except FileNotFoundError: pass
    return names

if __name__ == '__main__':
    img, relset, (cs, ip), _ = load(sys.argv[1])
    if not relset and cs == 0 and ip == 0x100:
        DGROUP = 0
    names = read_names(sys.argv[2])
    d = Dis(img, relset, names)
    seeds = [(cs, ip)]
    for k in names:
        s, o = (int(x, 16) for x in k.split(':'))
        if not DGROUP or s != DGROUP: seeds.append((s, o))
    # far pointers in relocated words: dword ofs:seg where seg is a code segment
    for L in relset:
        segv = struct.unpack_from('<H', img, L)[0]
        if segv < DGROUP and L >= 2 and L - 2 not in relset:
            pass
    strong = [(cs, ip)] + [x for x in seeds[1:] if names['%04X:%04X' % x][1] != 'ghidra-seed']
    weak = [x for x in seeds[1:] if names['%04X:%04X' % x][1] == 'ghidra-seed']
    d.run(strong)
    for _ in range(3):
        covered = set()
        for L, (sg, o, i) in d.insn.items(): covered.update(range(L, L + i.size))
        for L, (kind, n, cm) in d.data.items(): covered.update(range(L, L + (2*n if kind == 'dw' else n)))
        new = [x for x in weak if d.lin(*x) not in covered and d.lin(*x) not in d.funcs]
        if not new: break
        d.run(new[:1]) if False else d.run(new)
    # second pass: any 9A far-call targets found in unexplored areas are already followed
    d.dump(sys.argv[3])
    if len(sys.argv) > 4:
        with open(sys.argv[4], 'w') as f:
            for L, (s, o) in sorted(d.funcs.items()):
                f.write('%04X:%04X\t%s\t%d\n' % (s, o, d.name(s, o), len(d.xref.get(L, ()))))
    print('functions', len(d.funcs), 'insns', len(d.insn), 'strings', len(d.strings))
