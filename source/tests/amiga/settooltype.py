#!/usr/bin/env python3
"""Add/replace tool types in a classic Workbench .info file (no GUI needed).

usage: settooltype.py IN.info OUT.info 'KEY=value' ...

A tool type whose KEY already exists (also as "(KEY=...)", i.e. commented
out) is replaced; others are appended.  The DiskObject layout (NDK 3.9
workbench/workbench.h): 78-byte header, optional DrawerData (56),
GadgetRender Image, SelectRender Image, DefaultTool string, ToolTypes
array, ToolWindow string, then whatever follows (kept verbatim).
"""
import struct
import sys


def main():
    src, dst = sys.argv[1], sys.argv[2]
    new = sys.argv[3:]
    d = open(src, "rb").read()
    assert d[:2] == b"\xe3\x10", "not an .info file"
    gadget_render, select_render = struct.unpack(">II", d[22:30])
    default_tool, tooltypes = struct.unpack(">II", d[50:58])
    drawer_data, tool_window = struct.unpack(">II", d[66:74])
    pos = 78
    if drawer_data:
        pos += 56
    for img in (gadget_render, select_render):
        if img:
            w, h, depth = struct.unpack(">hhh", d[pos + 4:pos + 10])
            pos += 20 + ((w + 15) // 16) * 2 * h * depth
    if default_tool:
        n, = struct.unpack(">I", d[pos:pos + 4])
        pos += 4 + n
    old = []
    tt_start = pos
    if tooltypes:
        n, = struct.unpack(">I", d[pos:pos + 4])
        pos += 4
        for _ in range(n // 4 - 1):
            ln, = struct.unpack(">I", d[pos:pos + 4])
            old.append(d[pos + 4:pos + 4 + ln].rstrip(b"\0").decode("latin-1"))
            pos += 4 + ln
    tt_end = pos

    def key(t):
        return t.lstrip("(").split("=", 1)[0].rstrip(")").upper()

    for t in new:
        k = key(t)
        for i, o in enumerate(old):
            if key(o) == k:
                old[i] = t
                break
        else:
            old.append(t)
    enc = b"".join(struct.pack(">I", len(t) + 1) + t.encode("latin-1") + b"\0"
                   for t in old)
    block = struct.pack(">I", (len(old) + 1) * 4) + enc
    out = bytearray(d[:tt_start] + block + d[tt_end:])
    struct.pack_into(">I", out, 54, 1)  # ToolTypes pointer: non-null
    open(dst, "wb").write(out)
    print("%s: tool types now: %s" % (dst, old))


if __name__ == "__main__":
    main()
