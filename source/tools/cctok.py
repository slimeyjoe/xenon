"""The token stream of AGS script bytecode (.CC after LZSS), as the engine walks it.

The part of the script format the build needs (tools/mkpack.py, tools/tl_extract.py): a message
or option text is FD <length byte> <cp932 bytes> 00, an operand is FB/FE/FF + int16 LE (number,
flag, label id), everything else is a one-byte opcode. The full disassembler and structured
decompiler built on it is re-tools/ccdis.py (docs/re/scripts.md, opcodes.md)."""
import struct


class ParseError(Exception):
    pass


class Tok:
    __slots__ = ('pos', 'kind', 'val', 'end', 'lenbyte')

    def __init__(self, pos, kind, val, end, lenbyte=None):
        self.pos, self.kind, self.val, self.end, self.lenbyte = pos, kind, val, end, lenbyte

    def is_op(self, b):
        return self.kind == 'op' and self.val == b


def tokenize(d):
    """Token stream exactly as cc_skip_token (088D:0514) walks it."""
    out = []
    i = 0
    n = len(d)
    while i < n:
        b = d[i]
        if b == 0xFD:
            j = d.find(b'\0', i + 1)
            if j < 0:
                raise ParseError('unterminated string at %04X' % i)
            text = d[i + 2:j] if j >= i + 2 else b''
            out.append(Tok(i, 'str', text, j + 1, d[i + 1]))
            i = j + 1
        elif b in (0xFB, 0xFE, 0xFF):
            if i + 3 > n:
                raise ParseError('truncated operand at %04X' % i)
            v = struct.unpack_from('<h', d, i + 1)[0]
            out.append(Tok(i, {0xFB: 'int', 0xFE: 'var', 0xFF: 'id'}[b], v, i + 3))
            i += 3
        else:
            out.append(Tok(i, 'op', b, i + 1))
            i += 1
    return out
