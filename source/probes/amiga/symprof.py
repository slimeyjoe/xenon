#!/usr/bin/env python3
"""symprof.py: read an opnprof sample dump against the vasm listing of the same build.

    symprof.py bench-XENON_24/opn_lite_prof.lst bench-XENON_24/prof-11025.txt [-n 40]
        [-a opn_lite_68k.lst]        (the assembly loop's listing, for render_68k)

The dump (opnprof.c) gives the code-hunk offset of every named function ("F <hex> <name>")
and the sample count of every non-empty two-byte bucket ("S <hex> <count>"). Functions that
appear in the listing (opn_lite's, compiled with -Dstatic=) are resolved to instructions:
listing address = listing address of the function + (sample offset - the function's hunk
offset). A sample's PC is the address of the instruction about to execute when the interrupt
hit, so the time is charged to the instruction that ends there (the previous one).

Output: the dump's header lines, a per-function table (samples, share, code bytes), the top N
instructions with their assembly, and the annotated listing of the hottest functions, each
local label (the assembly loop's .chan, .mix, ...) heading its block with the block's share.
External functions of the core (opn_lite_eg_due, opn_lite_ssg_output) are resolved too."""
import re
import sys
from collections import defaultdict

def parse_listing(path):
    insns = []                      # (addr, size_hint, lineno, text) in section 00, by address
    labels = {}                     # name -> address of the next instruction
    pending = []
    with open(path, encoding='latin-1') as f:
        for ln in f:
            ln = ln.rstrip('\n')
            m = re.match(r'^\s*\t\s*(\d+): ([._A-Za-z][\w.]*):?$', ln)
            if m:
                pending.append(m.group(2))
                continue
            m = re.match(r'^00:([0-9A-F]{8}) ([0-9A-F]*)\s*\t\s*(\d+)M?:? \t?(.*)$', ln)   # "12M" = macro line
            if m:
                addr = int(m.group(1), 16)
                for name in pending:
                    labels[name] = addr
                pending = []
                insns.append((addr, len(m.group(2)) // 2, int(m.group(3)), m.group(4).strip()))
    return insns, labels

def main():
    args = sys.argv[1:]
    topn = 40
    if '-n' in args:
        i = args.index('-n')
        topn = int(args[i + 1])
        del args[i:i + 2]
    asm_lst = None
    if '-a' in args:
        i = args.index('-a')
        asm_lst = args[i + 1]
        del args[i:i + 2]
    lst_path, dump_path = args
    insns, labels = parse_listing(lst_path)
    if asm_lst:
        # the second listing's addresses are offset past the first one's so they never collide
        a_insns, a_labels = parse_listing(asm_lst)
        base = (max(a for a, _, _, _ in insns) + 0x10000) & ~0xffff
        insns += [(a + base, sz, ln, txt) for a, sz, ln, txt in a_insns]
        labels.update({n: a + base for n, a in a_labels.items()})
        if '_opn_lite_render_68k' in labels:
            labels['_render_68k'] = labels['_opn_lite_render_68k']
    insns.sort()
    addrs = [a for a, _, _, _ in insns]

    header, funcs, samples = [], [], []
    with open(dump_path, encoding='latin-1') as f:
        for ln in f:
            ln = ln.strip()
            if ln.startswith('F '):
                _, off, name = ln.split(None, 2)
                funcs.append((int(off, 16), name))
            elif ln.startswith('S '):
                _, off, cnt = ln.split()
                samples.append((int(off, 16), int(cnt)))
            elif ln.startswith('O '):
                pass
            elif ln:
                header.append(ln)
    funcs.sort()
    total = sum(c for _, c in samples)
    print('\n'.join(header))
    print(f'{total} samples inside the code hunk\n')

    def func_of(off):
        lo, hi = 0, len(funcs)
        while lo < hi:
            mid = (lo + hi) // 2
            if funcs[mid][0] <= off:
                lo = mid + 1
            else:
                hi = mid
        return lo - 1

    per_func = defaultdict(int)
    per_insn = defaultdict(int)     # (func index, listing address) -> count
    import bisect
    for off, cnt in samples:
        fi = func_of(off)
        if fi < 0:
            per_func['?'] += cnt
            continue
        foff, fname = funcs[fi]
        per_func[fname] += cnt
        lname = '_' + fname
        if lname not in labels and ('_opn_lite_' + fname) in labels:
            lname = '_opn_lite_' + fname       # external functions (eg_due, ssg_output)
        if lname in labels:
            la = labels[lname] + (off - foff)
            i = bisect.bisect_right(addrs, la) - 1
            if i >= 0 and addrs[i] == la and i > 0:
                i -= 1              # charge the instruction that ends at the sampled PC
            if i >= 0:
                per_insn[(fname, addrs[i])] += cnt

    print(f'{"function":24s} {"samples":>8s} {"share":>7s} {"bytes":>6s}')
    sizes = {}
    end = max([o for o, _ in samples] + [funcs[-1][0]]) + 2   # the last function runs to the last sample
    for (o, n), (o2, _) in zip(funcs, funcs[1:] + [(end, '')]):
        sizes[n] = o2 - o
    for name, cnt in sorted(per_func.items(), key=lambda kv: -kv[1]):
        if cnt == 0:
            continue
        print(f'{name:24s} {cnt:8d} {100.0 * cnt / total:6.1f}% {sizes.get(name, 0):6d}')

    print(f'\ntop {topn} instructions (count, share, function+offset, assembly)')
    by_addr = {a: (sz, ln, txt) for a, sz, ln, txt in insns}
    for (fname, la), cnt in sorted(per_insn.items(), key=lambda kv: -kv[1])[:topn]:
        sz, ln, txt = by_addr[la]
        base = labels.get('_' + fname, labels.get('_opn_lite_' + fname, 0))
        print(f'{cnt:7d} {100.0 * cnt / total:5.1f}%  {fname}+{la - base:04x}  {txt}')

    # per-function listing with counts, for the hottest functions: every instruction with its
    # count so loops and their bodies can be read in order
    def label_of(n):
        return '_' + n if ('_' + n) in labels else '_opn_lite_' + n
    hot = [n for n, c in sorted(per_func.items(), key=lambda kv: -kv[1]) if label_of(n) in labels][:6]
    by_label = defaultdict(list)
    for n, a in labels.items():
        if not re.fullmatch(r'l\d+', n):   # vbcc's branch targets are not blocks worth a header
            by_label[a].append(n)
    for fname in hot:
        la0 = labels[label_of(fname)]
        la1 = la0 + sizes.get(fname, 0)
        print(f'\n== {fname}: {per_func[fname]} samples ({100.0 * per_func[fname] / total:.1f}%)')
        # local labels (the assembly loop's .chan, .alg4, ...) head their block with its share
        i = bisect.bisect_left(addrs, la0)
        blocks, cur = defaultdict(int), fname
        for a, _, _, _ in insns[i:]:
            if a >= la1:
                break
            if a in by_label and a != la0:
                cur = '/'.join(sorted(by_label[a]))
            blocks[cur] += per_insn.get((fname, a), 0)
        while i < len(insns) and insns[i][0] < la1:
            a, sz, ln, txt = insns[i]
            if a in by_label and a != la0:
                name = '/'.join(sorted(by_label[a]))
                print(f'-- {name}: {blocks[name]} ({100.0 * blocks[name] / total:.1f}%)')
            cnt = per_insn.get((fname, a), 0)
            mark = f'{cnt:6d}' if cnt else '      '
            print(f'{mark} {a - la0:04x}  {txt}')
            i += 1

if __name__ == '__main__':
    main()
