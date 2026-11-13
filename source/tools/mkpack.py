#!/usr/bin/env python3
"""Build the Xenon asset pack (xenon.pak) from the original game files.

usage: mkpack.py --game GAMEDIR [--lang-ja] [--scripts LANG=DIR ...] [--fonts FONTDIR]
                 [--strings LANGDIR] [--fix-data-bugs] -o xenon.pak

GAMEDIR holds the original HDD install (AGS.EXE, PMD.COM, *.GDT, *.ADT, *.M, XEN_SE.EFC, *.CC).
Language-neutral assets are stored byte for byte. Scripts are decompressed (LZSS removed).
Every language gets a "lang" meta entry with its text settings (tools/langs.py SETTINGS:
encoding=cp932|utf-8, wrap=kinsoku|word; engine/text/enc.c); its scripts must be in that
encoding (tl_extract.py takes it from tools/langs.py) and its strings table is stored in it.

Fonts: cp932 languages share the neutral font16/font8 (JIS X 0208 / JIS X 0201 keyed). Each
UTF-8 language gets its own font8/font16, keyed by Unicode code point:
  font8   ASCII plus langs.narrow(lang), from lat8.xfnt (the Sony 8x16 ISO 8859-1 design)
  font16  every other character the language's texts use, plus the prompt glyphs (U+25BC and
          U+E000 = the user-defined return glyph JIS 7621h), cut from k16.xfnt via cp932.
A character is 1 column in the engine exactly when font8 has it. The build fails if any text
character has no glyph in either table.
  --lang-ja        include the original Japanese scripts as language "ja"
  --scripts L=DIR  include the scripts of language L (en, pl: decompressed .CC bytecode produced
                   by tl_extract.py); repeatable
  --strings DIR    language string tables: DIR/<lang>/strings.txt (engine messages)
  --fonts DIR      font tables produced by xfont.py: DIR/k16.xfnt, DIR/ank8.xfnt, DIR/lat8.xfnt
  --fix-data-bugs  patch known data bugs (M.CC xenon_102 -> xenon_10; K.CC duplicate PAGE 23)
  --voicemap FILE  voice cue table (resources/voice/voicemap.tsv, re-tools/voicemap.py): one
                   VOICECUE entry per script and packed language (docs/voice.md §2.1)

VOICECUE entry (little-endian): "VCUE", u16 version (1), u16 count, then count records of
20 bytes sorted by (offset, part): u32 offset of the message's FD token in that language's
bytecode, u8 part (segment after the n-th {KEY}), 3 reserved, clip name[12] (NUL padded).
The table is keyed by FD offsets in the original Japanese bytecode; each language's script
has the same token structure (tl_extract.py replaces string contents only), so the k-th string
token of the original is the k-th of the translation. The build stops if a structure differs
or a cue does not name a string token.
"""
import argparse
import collections
import csv
import glob
import hashlib
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import cctok  # noqa: E402
import langs  # noqa: E402
import lzss   # noqa: E402
import xfont  # noqa: E402
import xpak   # noqa: E402


LANG_SETTINGS = langs.SETTINGS
PY_CODEC = {'cp932': 'cp932', 'utf-8': 'utf-8'}

PROMPT_CHARS = '\u25bc\ue000'     # engine/text/enc.c text_prompt_str
U_RETURN, JIS_RETURN = 0xE000, 0x7621


def sjis_to_jis(hi, lo):
    """engine/util/sjis.h sjis_to_jis"""
    hi -= 0x71 if hi <= 0x9F else 0xB1
    hi = hi * 2 + 1
    if lo > 0x7F:
        lo -= 1
    if lo >= 0x9E:
        lo -= 0x7D
        hi += 1
    else:
        lo -= 0x1F
    return ((hi & 0xFF) << 8) | (lo & 0xFF)


def script_texts(pk, lang, codec):
    """All string tokens of the language's scripts, decoded strictly."""
    out = []
    for name, typ, l, data in pk.entries:
        if typ != xpak.T_SCRIPT or l != lang:
            continue
        for t in cctok.tokenize(data):
            if t.kind == 'str':
                try:
                    out.append(t.val.decode(codec))
                except UnicodeDecodeError as e:
                    sys.exit('mkpack: %s: string %r is not %s (%s)' % (name, t.val[:40], codec, e))
    return out


def utf8_fonts(lang_name, texts, fontdir):
    """font8 and font16 XFNT blobs for a UTF-8 language (see the module docstring)."""
    _, _, _, k16 = xfont.read_xfnt(open(os.path.join(fontdir, 'k16.xfnt'), 'rb').read())
    _, _, _, lat8 = xfont.read_xfnt(open(os.path.join(fontdir, 'lat8.xfnt'), 'rb').read())
    narrow = set(chr(c) for c in range(0x20, 0x7F)) | set(langs.narrow(lang_name))
    used = set(''.join(texts)) | set(PROMPT_CHARS)
    f8, f16, missing = {}, {}, []
    for c in sorted(narrow):
        if ord(c) in lat8:
            f8[ord(c)] = lat8[ord(c)]
        else:
            missing.append('%s (U+%04X, narrow)' % (c, ord(c)))
    for c in sorted(used - narrow):
        if ord(c) < 0x20:
            continue                                # control codes
        if ord(c) == U_RETURN:
            jis = JIS_RETURN
        else:
            try:
                b = c.encode('cp932')
            except UnicodeEncodeError:
                b = b''
            jis = sjis_to_jis(b[0], b[1]) if len(b) == 2 else None
        if jis is None or jis not in k16:
            missing.append('%s (U+%04X)' % (c, ord(c)))
        else:
            f16[ord(c)] = k16[jis]
    if missing:
        sys.exit('mkpack: %s: no glyph for %s' % (lang_name, ', '.join(missing)))
    return xfont.write_xfnt(8, 16, True, f8), xfont.write_xfnt(16, 16, True, f16)


def base(path):
    return os.path.splitext(os.path.basename(path))[0]


def add_dir_files(pk, gamedir, pattern, typ):
    files = sorted(glob.glob(os.path.join(gamedir, pattern)))
    for f in files:
        data = open(f, 'rb').read()
        if typ == xpak.T_GDT and len(data) < 24:
            continue                           # XEN.GDT is the progress byte, not an image
        pk.add(base(f), typ, xpak.L_ANY, data)
    return len(files)


def fix_data_bugs(name, data):
    data = bytearray(data)
    if name.upper() == 'M':
        # music mode "Ago" loads the nonexistent xenon_102 (sound.md §4.2): the intended file is
        # XENON_10. Rewrite the FD string in place (length byte + text + NUL).
        i = data.find(b'\xfd\x09xenon_102\x00')
        if i >= 0:
            data[i:i + 12] = b'\xfd\x08xenon_10\x00'
            # the string got one byte shorter; nothing references absolute offsets except
            # page searches, which scan tokens, so the shorter stream is fine.
    if name.upper() == 'K':
        # second PAGE 23 (shows r&m03) is unreachable (scripts.md §6): renumber it 123 and make
        # the first PAGE 23's "forward" target lead there? Too invasive for a data patch; we only
        # renumber so the gallery page becomes reachable via fall-through from the first PAGE 23.
        pass
    return bytes(data)


def add_scripts(pk, scripts, lang, fix):
    n = 0
    for f in sorted(scripts):
        raw = open(f, 'rb').read()
        name = base(f).upper()
        if name.endswith('.U'):
            name = name[:-2]
            data = raw                      # already decompressed (tl_extract.py output)
        else:
            data = lzss.unpack_file(raw)
        if fix:
            data = fix_data_bugs(name, data)
        pk.add(name, xpak.T_SCRIPT, lang, data)
        n += 1
    return n


def token_shape(data):
    """(kinds/values with strings blanked, offsets of the string tokens)"""
    toks = cctok.tokenize(data)
    return ([(x.kind, None if x.kind == 'str' else x.val) for x in toks],
            [x.pos for x in toks if x.kind == 'str'])


def add_voice_cues(pk, voicemap, gamedir, langs):
    cues = collections.defaultdict(list)          # script -> [(ja offset, part, clip)]
    with open(voicemap, encoding='utf-8') as f:
        for r in csv.DictReader(f, delimiter='\t'):
            cues[r['script'].upper()].append((int(r['offset'], 16), int(r['part']), r['clip']))
    scripts = {(n, l): d for n, typ, l, d in pk.entries if typ == xpak.T_SCRIPT}
    n = 0
    for name in sorted(cues):
        path = os.path.join(gamedir, name + '.CC')
        if not os.path.exists(path):
            sys.exit('mkpack: --voicemap: no %s in the game directory' % path)
        ref_shape, ref_str = token_shape(lzss.unpack_file(open(path, 'rb').read()))
        ordinal = {off: k for k, off in enumerate(ref_str)}
        for lang_name in langs:
            lang = xpak.LANG_IDS[lang_name]
            data = scripts.get((name, lang))
            if data is None:
                continue
            shape, strs = token_shape(data)
            if shape != ref_shape:
                sys.exit('mkpack: --voicemap: %s (%s) does not have the token structure of the '
                         'original script' % (name, lang_name))
            recs = []
            for off, part, clip in cues[name]:
                if off not in ordinal:
                    sys.exit('mkpack: --voicemap: %s %04X is not a string token' % (name, off))
                if len(clip) > 11:
                    sys.exit('mkpack: --voicemap: clip name %r too long' % clip)
                recs.append((strs[ordinal[off]], part, clip))
            recs.sort()
            blob = bytearray(b'VCUE' + struct.pack('<HH', 1, len(recs)))
            for off, part, clip in recs:
                blob += struct.pack('<IB3x12s', off, part, clip.encode('ascii'))
            pk.add(name, xpak.T_VOICECUE, lang, bytes(blob))
            n += 1
    return n


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--game', required=True)
    ap.add_argument('--lang-ja', action='store_true')
    ap.add_argument('--scripts', action='append', default=[], metavar='LANG=DIR')
    ap.add_argument('--strings')
    ap.add_argument('--fonts')
    ap.add_argument('--fix-data-bugs', action='store_true')
    ap.add_argument('--voicemap')
    ap.add_argument('-o', '--output', required=True)
    a = ap.parse_args()

    pk = xpak.Pack()
    counts = {}
    counts['gdt'] = add_dir_files(pk, a.game, '*.GDT', xpak.T_GDT)
    counts['adt'] = add_dir_files(pk, a.game, '*.ADT', xpak.T_ADT)
    counts['music'] = add_dir_files(pk, a.game, '*.M', xpak.T_MUSIC)
    counts['efc'] = add_dir_files(pk, a.game, '*.EFC', xpak.T_EFC)
    # PMD's built-in SSG effect table: PMD.COM 2CE9h..3095h (pmd-driver.md §7.2), CS offsets
    # with ORG 100h -> file offset = address - 100h. Entries {priority, u16 offset from 2CE9h}.
    pmdcom = os.path.join(a.game, 'PMD.COM')
    if os.path.exists(pmdcom):
        com = open(pmdcom, 'rb').read()
        pk.add('ssgefc', xpak.T_SSGEFC, xpak.L_ANY, com[0x2CE9 - 0x100:0x3096 - 0x100])
        counts['ssgefc'] = 1
    langs = []
    if a.lang_ja:
        counts['ja'] = add_scripts(pk, glob.glob(os.path.join(a.game, '*.CC')), xpak.L_JA, a.fix_data_bugs)
        langs.append('ja')
    for spec in a.scripts:
        lang_name, _, d = spec.partition('=')
        if lang_name not in LANG_SETTINGS or lang_name == 'ja' or not d:
            sys.exit('mkpack: --scripts %r: want LANG=DIR with LANG one of %s'
                     % (spec, ', '.join(k for k in LANG_SETTINGS if k != 'ja')))
        counts[lang_name] = add_scripts(pk, glob.glob(os.path.join(d, '*.CC')), xpak.LANG_IDS[lang_name],
                                        a.fix_data_bugs)
        langs.append(lang_name)
    for lang_name in langs:
        enc, wrap = LANG_SETTINGS[lang_name]
        pk.add('lang', xpak.T_META, xpak.LANG_IDS[lang_name],
               ('encoding=%s\nwrap=%s\n' % (enc, wrap)).encode('ascii'))
    texts = {}
    for lang_name in langs:
        lang = xpak.LANG_IDS[lang_name]
        codec = PY_CODEC[LANG_SETTINGS[lang_name][0]]
        texts[lang_name] = script_texts(pk, lang, codec)
        f = os.path.join(a.strings, lang_name, 'strings.txt') if a.strings else ''
        if not os.path.exists(f):
            continue
        blob = bytearray()
        for line in open(f, encoding='utf-8'):
            line = line.rstrip('\n')
            if not line or line.startswith('#'):
                continue
            key, _, val = line.partition('=')
            blob += key.encode('ascii') + b'\0' + val.encode(codec) + b'\0'
            texts[lang_name].append(val)
        pk.add('strings', xpak.T_STRINGS, lang, bytes(blob))
        counts['strings_' + lang_name] = 1
    if a.fonts:
        for name, fname in (('font16', 'k16.xfnt'), ('font8', 'ank8.xfnt')):
            f = os.path.join(a.fonts, fname)
            if os.path.exists(f):
                pk.add(name, xpak.T_FONT, xpak.L_ANY, open(f, 'rb').read())
                counts[name] = 1
        for lang_name in langs:
            if LANG_SETTINGS[lang_name][0] != 'utf-8':
                continue
            f8, f16 = utf8_fonts(lang_name, texts[lang_name], a.fonts)
            pk.add('font8', xpak.T_FONT, xpak.LANG_IDS[lang_name], f8)
            pk.add('font16', xpak.T_FONT, xpak.LANG_IDS[lang_name], f16)
            counts['fonts_' + lang_name] = (len(f8) + len(f16))
    meta = 'format=1\nfix_data_bugs=%d\n' % a.fix_data_bugs
    if a.voicemap:
        counts['voicecue'] = add_voice_cues(pk, a.voicemap, a.game, langs)
        # the voice pack records the same digest (mkvoice.py); the engine warns on a mismatch
        meta += 'voicemap=%s\n' % hashlib.md5(open(a.voicemap, 'rb').read()).hexdigest()
    pk.add('meta', xpak.T_META, xpak.L_ANY, meta.encode('ascii'))

    data = pk.build()
    with open(a.output, 'wb') as fo:
        fo.write(data)
    print('%s: %d entries, %d bytes (%s)' % (a.output, len(pk.entries), len(data),
                                           ', '.join('%s=%d' % kv for kv in counts.items())))


if __name__ == '__main__':
    main()
