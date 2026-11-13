#!/usr/bin/env python3
"""Append a payload + 16-byte "XPKT" trailer to an AmigaDOS hunk executable.

usage: mktrailer.py raw|debug EXE PAYLOAD OUT

Trailer (last 16 bytes of OUT): "XPKT", u32 little-endian payload size,
8 bytes of zero padding.  The payload sits directly before the trailer.

raw    OUT = EXE + PAYLOAD + trailer.  Kickstart 3.1 LoadSeg() reads hunk
       types until EOF and fails with "bad loadfile hunk" on this.
debug  OUT = EXE + HUNK_DEBUG(0x3F1) header covering PAYLOAD + trailer.
       LoadSeg() skips a debug hunk, so the file still loads, and the
       trailer is still the last 16 bytes of the file.  The payload is
       padded with zeros to a longword boundary (the size in the trailer
       is the unpadded size; the padding sits between payload and trailer).
"""
import struct
import sys

mode, exe, payload, out = sys.argv[1:5]
data = open(payload, "rb").read()
trailer = b"XPKT" + struct.pack("<I", len(data)) + bytes(8)
if mode == "raw":
    body = data + trailer
elif mode == "debug":
    pad = (-len(data)) % 4
    block = data + bytes(pad) + trailer
    body = struct.pack(">II", 0x3F1, len(block) // 4) + block
else:
    sys.exit("mode must be raw or debug")
with open(out, "wb") as f:
    f.write(open(exe, "rb").read())
    f.write(body)
