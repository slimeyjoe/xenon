"""XPAK asset pack format - writer/reader shared by mkpack.py and the tests.

Layout: see source/engine/pack/pack.h (the C reader is the reference for the engine).
"""
import struct

MAGIC = b'XPAK'
VERSION = 1
HEADER_SIZE = 16
ENTRY_SIZE = 32
NAME_LEN = 20
TRAILER_MAGIC = b'XPKT'

T_GDT, T_ADT, T_MUSIC, T_EFC, T_SSGEFC, T_SCRIPT, T_FONT, T_STRINGS, T_META, T_BLOB, T_VOICECUE, T_VOICE = range(1, 13)
L_ANY, L_JA, L_EN, L_PL = 0, 1, 2, 3
LANG_IDS = {'any': L_ANY, 'ja': L_JA, 'en': L_EN, 'pl': L_PL}
TYPE_NAMES = {T_GDT: 'gdt', T_ADT: 'adt', T_MUSIC: 'music', T_EFC: 'efc', T_SSGEFC: 'ssgefc',
              T_SCRIPT: 'script', T_FONT: 'font', T_STRINGS: 'strings', T_META: 'meta', T_BLOB: 'blob',
              T_VOICECUE: 'voicecue', T_VOICE: 'voice'}


class Pack:
    def __init__(self):
        self.entries = []      # (name, type, lang, data)

    def add(self, name, typ, lang, data):
        name = name.upper() if typ in (T_GDT, T_ADT, T_MUSIC, T_EFC, T_SCRIPT, T_VOICECUE, T_VOICE) else name
        if len(name.encode('ascii')) > NAME_LEN:
            raise ValueError('name too long: %r' % name)
        for n, t, l, _ in self.entries:
            if (n.upper(), t, l) == (name.upper(), typ, lang):
                raise ValueError('duplicate entry %r type %d lang %d' % (name, typ, lang))
        self.entries.append((name, typ, lang, bytes(data)))

    def build(self, dedupe=False, data_order=None):
        """dedupe: entries with identical data share one copy (the reader does not care).
        data_order: entry names in the order their data is to be laid out in the file (the
        entries not named follow in table order); the table itself stays sorted."""
        ents = sorted(self.entries, key=lambda e: (e[2], e[0].upper()))
        off = HEADER_SIZE + ENTRY_SIZE * len(ents)
        rank = {n: i for i, n in enumerate(data_order or [])}
        layout = sorted(range(len(ents)), key=lambda i: (rank.get(ents[i][0], len(rank)), i))
        blobs = bytearray()
        seen = {}
        at_of = [0] * len(ents)
        for i in layout:
            data = ents[i][3]
            if dedupe and data in seen:
                at_of[i] = seen[data]
                continue
            while (off + len(blobs)) % 4:
                blobs.append(0)
            at_of[i] = off + len(blobs)
            blobs += data
            if dedupe:
                seen[data] = at_of[i]
        table = bytearray()
        for (name, typ, lang, data), at in zip(ents, at_of):
            table += struct.pack('<20sBBHII', name.encode('ascii'), typ, lang, 0, at, len(data))
        total = off + len(blobs)
        hdr = MAGIC + struct.pack('<HHI4s', VERSION, len(ents), total, b'\0' * 4)
        return bytes(hdr + table + blobs)


def trailer(pack_size):
    return TRAILER_MAGIC + struct.pack('<II', pack_size, pack_size ^ 0xFFFFFFFF) + TRAILER_MAGIC


def read(data):
    """Parse a pack image; returns list of dicts (name, type, lang, offset, size, data)."""
    if data[:4] != MAGIC:
        raise ValueError('not an XPAK')
    ver, count, total = struct.unpack_from('<HHI', data, 4)
    if ver != VERSION or total > len(data):
        raise ValueError('bad XPAK header')
    out = []
    for i in range(count):
        name, typ, lang, _, off, size = struct.unpack_from('<20sBBHII', data, HEADER_SIZE + ENTRY_SIZE * i)
        name = name.rstrip(b'\0').decode('ascii')
        out.append(dict(name=name, type=typ, lang=lang, offset=off, size=size, data=data[off:off + size]))
    return out
