#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the Xenon asset pack (xenon.pak) from the original game files.

usage: mkpack.py --game GAMEDIR --profile PROFILE --game-def PACK.PY [--lang-ja]
                 [--scripts LANG=DIR ...] [--fonts FONTDIR] [--strings LANGDIR] [--fix-data-bugs]
                 -o xenon.pak

GAMEDIR holds the original HDD install (AGS.EXE, PMD.COM, *.GDT, *.ADT, *.M, XEN_SE.EFC, *.CC).
The pack is lrvne's (include/lrvne/pack.h, written by lrvne.xpak): the engine's entry types
are TYPES below, the game profile is the entry "profile" (docs/profile.md).
Language-neutral assets are stored byte for byte. Scripts are decompressed (LZSS removed).
Every language gets a "lang" meta entry with its text settings (tools/langs.py SETTINGS:
encoding=cp932|utf-8, wrap=kinsoku|word; engine/text/enc.c); its scripts must be in that
encoding (tl_extract.py takes it from tools/langs.py) and its strings table is stored in it.

Fonts: cp932 languages share the neutral font16/font8 (JIS X 0208 / JIS X 0201 keyed); font16
gets the engine's user-defined character JIS 7621h (the page-overflow "return" glyph), which
AGS.EXE defines at DS:09B2 and the font file does not have. Each
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
  --fonts DIR      font tables made by lrvne's xfont: DIR/k16.xfnt, DIR/ank8.xfnt, DIR/lat8.xfnt
  --profile FILE   the game's profile (games/<game>/profile.txt, docs/profile.md): the entry
                   "profile"
  --game-def FILE  the game's pack.py: its languages, where its user-defined glyph is, its
                   data fixes
  --fix-data-bugs  patch the game's known data bugs (pack.py fix_data_bugs)
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
import importlib.util
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', 'lrvne', 'tools'))
import cctok  # noqa: E402
import langs  # noqa: E402
import lzss   # noqa: E402
from lrvne import pmd, profile, xfont, xpak   # noqa: E402

# the engine's entry types (engine/xe.h enum xe_pack_type)
T_GDT, T_ADT, T_MUSIC, T_EFC, T_SSGEFC, T_SCRIPT = range(xpak.T_ENGINE, xpak.T_ENGINE + 6)
TYPES = xpak.TypeRegistry({T_GDT: 'gdt', T_ADT: 'adt', T_MUSIC: 'music', T_EFC: 'efc',
                           T_SSGEFC: 'ssgefc', T_SCRIPT: 'script'})

LANG_SETTINGS = langs.SETTINGS
PY_CODEC = {'cp932': 'cp932', 'utf-8': 'utf-8'}

PROMPT_CHARS = '\u25bc\ue000'     # engine/text/enc.c text_prompt_str
U_RETURN, JIS_RETURN = 0xE000, 0x7621


def sjis_to_jis(hi, lo):
    """lrvne/sjis.h lrv_sjis_to_jis"""
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


def script_texts(scripts, lang, codec):
    """All string tokens of the language's scripts, decoded strictly."""
    out = []
    for (name, l), data in sorted(scripts.items()):
        if l != lang:
            continue
        for t in cctok.tokenize(data):
            if t.kind == 'str':
                try:
                    out.append(t.val.decode(codec))
                except UnicodeDecodeError as e:
                    sys.exit('mkpack: %s: string %r is not %s (%s)' % (name, t.val[:40], codec, e))
    return out


def utf8_fonts(lang_name, texts, fontdir, k16_data):
    """font8 and font16 XFNT blobs for a UTF-8 language (see the module docstring); k16_data
    is the cp932 languages' font16."""
    _, _, _, k16 = xfont.read_xfnt(k16_data)
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
        if typ == T_GDT and len(data) < 24:
            continue                           # XEN.GDT is the progress byte, not an image
        pk.add(base(f), typ, data)
    return len(files)


def user_glyph(exe_path, off):
    """JIS 7621h as the program file defines it at `off`: 16 rows of two bytes, left byte
    first, emboldened the engine's way (to the left, as the PC-98 does a ROM glyph)."""
    exe = open(exe_path, 'rb').read()
    rows = [struct.unpack_from('<H', exe, off + 2 * r)[0] for r in range(16)]
    return [xfont.bold16(((v & 0xFF) << 8) | (v >> 8)) for v in rows]


def font16_with_user_glyph(path, gamedir, game):
    cell_w, cell_h, bold, table = xfont.read_xfnt(open(path, 'rb').read())
    exe, off = game.USER_GLYPH
    table[JIS_RETURN] = user_glyph(os.path.join(gamedir, exe), off)
    return xfont.write_xfnt(cell_w, cell_h, bold, table)


def load_game_def(path):
    """The game's pack definition (games/<game>/pack.py): LANGS, USER_GLYPH, fix_data_bugs."""
    spec = importlib.util.spec_from_file_location('game_pack', path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def add_scripts(pk, packed, scripts, lang, fix):
    """fix: the game's fix_data_bugs, or None"""
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
            data = fix(name, data)
        pk.add(name, T_SCRIPT, data, lang=lang)
        packed[(name, lang)] = data
        n += 1
    return n


def token_shape(data):
    """(kinds/values with strings blanked, offsets of the string tokens)"""
    toks = cctok.tokenize(data)
    return ([(x.kind, None if x.kind == 'str' else x.val) for x in toks],
            [x.pos for x in toks if x.kind == 'str'])


def add_voice_cues(pk, scripts, voicemap, gamedir, langs):
    cues = collections.defaultdict(list)          # script -> [(ja offset, part, clip)]
    with open(voicemap, encoding='utf-8') as f:
        for r in csv.DictReader(f, delimiter='\t'):
            cues[r['script'].upper()].append((int(r['offset'], 16), int(r['part']), r['clip']))
    n = 0
    for name in sorted(cues):
        path = os.path.join(gamedir, name + '.CC')
        if not os.path.exists(path):
            sys.exit('mkpack: --voicemap: no %s in the game directory' % path)
        ref_shape, ref_str = token_shape(lzss.unpack_file(open(path, 'rb').read()))
        ordinal = {off: k for k, off in enumerate(ref_str)}
        for lang_name in langs:
            data = scripts.get((name, lang_name))
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
            pk.add(name, xpak.T_VOICECUE, bytes(blob), lang=lang_name, upper=True)
            n += 1
    return n


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--game', required=True)
    ap.add_argument('--profile', required=True, help='the game\'s profile (games/<game>/profile.txt)')
    ap.add_argument('--game-def', required=True, help='the game\'s pack.py (games/<game>/pack.py)')
    ap.add_argument('--lang-ja', action='store_true')
    ap.add_argument('--scripts', action='append', default=[], metavar='LANG=DIR')
    ap.add_argument('--strings')
    ap.add_argument('--fonts')
    ap.add_argument('--fix-data-bugs', action='store_true')
    ap.add_argument('--voicemap')
    ap.add_argument('-o', '--output', required=True)
    a = ap.parse_args()

    game = load_game_def(a.game_def)
    fix = game.fix_data_bugs if a.fix_data_bugs else None
    pk = xpak.PackWriter(TYPES, langs=game.LANGS)
    scripts = {}                                # (name, lang) -> bytecode
    counts = {}
    counts['gdt'] = add_dir_files(pk, a.game, '*.GDT', T_GDT)
    counts['adt'] = add_dir_files(pk, a.game, '*.ADT', T_ADT)
    counts['music'] = add_dir_files(pk, a.game, '*.M', T_MUSIC)
    counts['efc'] = add_dir_files(pk, a.game, '*.EFC', T_EFC)
    # PMD's built-in SSG effect table (pmd-driver.md §7.2), for lrvne's PMD module
    pmdcom = os.path.join(a.game, 'PMD.COM')
    if os.path.exists(pmdcom):
        with open(pmdcom, 'rb') as f:
            pk.add('ssgefc', T_SSGEFC, pmd.ssg_effect_table(f.read()))
        counts['ssgefc'] = 1
    langs = []
    if a.lang_ja:
        counts['ja'] = add_scripts(pk, scripts, glob.glob(os.path.join(a.game, '*.CC')), 'ja', fix)
        langs.append('ja')
    for spec in a.scripts:
        lang_name, _, d = spec.partition('=')
        if lang_name not in LANG_SETTINGS or lang_name == 'ja' or not d:
            sys.exit('mkpack: --scripts %r: want LANG=DIR with LANG one of %s'
                     % (spec, ', '.join(k for k in LANG_SETTINGS if k != 'ja')))
        counts[lang_name] = add_scripts(pk, scripts, glob.glob(os.path.join(d, '*.CC')),
                                        lang_name, fix)
        langs.append(lang_name)
    for lang_name in langs:
        enc, wrap = LANG_SETTINGS[lang_name]
        pk.add('lang', xpak.T_META, ('encoding=%s\nwrap=%s\n' % (enc, wrap)).encode('ascii'),
               lang=lang_name)
    texts = {}
    for lang_name in langs:
        codec = PY_CODEC[LANG_SETTINGS[lang_name][0]]
        texts[lang_name] = script_texts(scripts, lang_name, codec)
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
        pk.add('strings', xpak.T_STRINGS, bytes(blob), lang=lang_name)
        counts['strings_' + lang_name] = 1
    if a.fonts:
        k16 = font16_with_user_glyph(os.path.join(a.fonts, 'k16.xfnt'), a.game, game)
        pk.add('font16', xpak.T_FONT, k16)
        counts['font16'] = 1
        f = os.path.join(a.fonts, 'ank8.xfnt')
        if os.path.exists(f):
            pk.add('font8', xpak.T_FONT, open(f, 'rb').read())
            counts['font8'] = 1
        for lang_name in langs:
            if LANG_SETTINGS[lang_name][0] != 'utf-8':
                continue
            f8, f16 = utf8_fonts(lang_name, texts[lang_name], a.fonts, k16)
            pk.add('font8', xpak.T_FONT, f8, lang=lang_name)
            pk.add('font16', xpak.T_FONT, f16, lang=lang_name)
            counts['fonts_' + lang_name] = (len(f8) + len(f16))
    meta = 'format=1\nfix_data_bugs=%d\n' % a.fix_data_bugs
    if a.voicemap:
        counts['voicecue'] = add_voice_cues(pk, scripts, a.voicemap, a.game, langs)
        # the voice pack records the same digest (mkvoice.py); the engine warns on a mismatch
        meta += 'voicemap=%s\n' % hashlib.md5(open(a.voicemap, 'rb').read()).hexdigest()
    pk.add('meta', xpak.T_META, meta.encode('ascii'))
    text = open(a.profile, encoding='utf-8').read()
    profile.loads(text)                         # a well-formed key=value text
    pk.add('profile', xpak.T_META, text.encode('utf-8'))

    size = pk.write(a.output)
    print('%s: %d entries, %d bytes (%s)' % (a.output, len(pk.names()), size,
                                           ', '.join('%s=%d' % kv for kv in counts.items())))


if __name__ == '__main__':
    main()
