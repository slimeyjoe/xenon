#!/usr/bin/env python3
"""Check/pad a translated line for Xenon's text window.

Usage:
  fitcheck.py [--lang en|pl] [--menu] [--pad] "LINE"      (or read lines from stdin with -)
Rules modelled (see docs/TRANSLATION_GUIDE_EN.md, "Engine limits"):
  * --lang en (default): text is encoded with Python's 'shift_jis' codec (the English character set, tools/charset_pl.py);
    anything not encodable is an ERROR
  * --lang pl: shift_jis plus the Polish letters, each 1 byte wide; typographic quotes/dashes are
    ERRORs too (the table is tools/charset_pl.py; see docs/TRANSLATION_GUIDE_PL.md §2)
  * the window hard-wraps every 60 bytes, mid-word; padding spaces are used so words don't split
  * dialogue/narration: max 4 rows = 240 bytes; menu options (--menu): 1 row = 60 bytes
--pad re-flows the text with ASCII spaces so no word is split across rows (keeping any leading
indentation), then checks it.
"""
import sys, os
sys.dont_write_bytecode = True  # don't leave tools/__pycache__ behind
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import charset_pl
ROW=60
LANG='en'   # set by --lang, or by importers (set_line.py) before calling pad()/blen()
def enc(s):
    bad=[c for c in s if not _ok(c)]
    return bad
def _ok(c): return charset_pl.char_ok(c, LANG)
def blen(s): return charset_pl.blen(s, LANG)
def rows(s):
    if LANG!='en':
        out=[];cur='';w=0
        for c in s:
            cw=charset_pl.char_width(c, LANG)
            if w+cw>ROW: out.append(cur); cur='';w=0
            cur+=c;w+=cw
        out.append(cur); return out
    out=[];cur=b''
    for c in s:
        b=c.encode('shift_jis','replace')
        if len(cur)+len(b)>ROW: out.append(cur); cur=b''
        cur+=b
    out.append(cur); return [r.decode('shift_jis','replace') for r in out]
def pad(s):
    """Re-flow s with ASCII padding so no word splits across 60-byte rows.
    Leading indentation (ASCII or ideographic spaces) is kept as-is."""
    import re
    m=re.match(r'[\u3000 ]*',s); lead=m.group(0); body=s[len(lead):]
    t=re.sub(r'[\u3000 ]{2,}|\u3000',' ',body.strip())
    words=t.split(' '); lines=[]; cur=lead
    for w in words:
        cand=(cur+' '+w) if cur.strip() else cur+w
        # pl: fill rows to 59 so a padding space always separates words at a row break
        # (a row filled to exactly 60 drops the space; the text then reads "word.Next" off-screen)
        if blen(cand)<=ROW-(LANG!='en'): cur=cand
        else:
            lines.append(cur); cur=w
    lines.append(cur)
    res=''
    for i,l in enumerate(lines):
        if i<len(lines)-1: res+=l+' '*(ROW-blen(l))
        else: res+=l
    return res
def check(s,menu):
    lim=ROW if menu else 4*ROW
    bad=enc(s); n=blen(s); rs=rows(s)
    print(f"bytes={n} rows={len(rs)} limit={lim} {'OK' if n<=lim and not bad else 'FAIL'}")
    if bad: print("  ERROR non-shift_jis chars:" if LANG=='en' else f"  ERROR chars not allowed for --lang {LANG}:",' '.join(f'{c!r}(U+{ord(c):04X})' for c in sorted(set(bad))))
    if n>lim: print(f"  ERROR too long by {n-lim} bytes")
    for i,r in enumerate(rs):
        print(f"  row{i+1} |{r}|")
    for i in range(len(rs)-1):
        a,b=rs[i],rs[i+1]
        if a and b and a[-1].isalnum() and b[0].isalnum(): print(f"  WARN word split between row{i+1} and row{i+2}: ...{a[-8:]}|{b[:8]}...")
def main():
    global LANG
    a=sys.argv[1:]
    LANG=charset_pl.parse_lang(a)
    if not a or '-h' in a or '--help' in a:
        print(__doc__); sys.exit(0 if a else 2)
    menu='--menu' in a; dopad='--pad' in a
    a=[x for x in a if not x.startswith('--')]
    lines=sys.stdin.read().splitlines() if a==['-'] else a
    for s in lines:
        if dopad:
            p=pad(s); print("PADDED:"+repr(p)); print("PADDED_RAW:"+p); check(p,menu)
        else: check(s,menu)
if __name__ == '__main__':
    main()
