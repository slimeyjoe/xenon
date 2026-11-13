#!/usr/bin/env python3
"""Take a WinUAE screenshot through its gdbserver (monitor screenshot).

usage: gdbshot.py PORT OUTPUT.png [more monitor commands...]

Attaching stops the CPU at a debugger boundary; we run the monitor
command(s), then detach ("D"), which resumes emulation.
"""
import socket
import sys
import time


def frame(payload):
    cs = sum(payload.encode()) & 0xFF
    return b"$" + payload.encode() + b"#" + ("%02x" % cs).encode()


def read_packet(s, timeout=20.0):
    s.settimeout(timeout)
    buf = b""
    while True:
        c = s.recv(1)
        if not c:
            raise EOFError("gdbserver closed")
        if c == b"+" and not buf:
            continue  # ack
        buf += c
        if buf.startswith(b"$") and len(buf) >= 4 and buf[-3] == ord("#"):
            s.sendall(b"+")
            return buf[1:-3].decode(errors="replace")


def main():
    port = int(sys.argv[1])
    out = sys.argv[2]
    cmds = ["screenshot " + out] + sys.argv[3:]
    last = None
    for attempt in range(50):
        try:
            s = socket.create_connection(("127.0.0.1", port), timeout=5)
            break
        except OSError as e:
            last = e
            time.sleep(0.2)
    else:
        print("gdbshot: connect failed: %s" % last, file=sys.stderr)
        return 1
    # Attach stops the CPU; give it a moment to reach the boundary.
    time.sleep(0.3)
    rc = 0
    for c in cmds:
        s.sendall(frame("qRcmd," + c.encode().hex()))
        reply = read_packet(s)
        # Replies may be O-packets (hex text) followed by OK.
        while reply.startswith("O") and reply != "OK":
            print(bytes.fromhex(reply[1:]).decode(errors="replace"), end="")
            reply = read_packet(s)
        print("gdbshot: %s -> %s" % (c, reply))
        if reply != "OK":
            rc = 1
    s.sendall(frame("D"))
    try:
        read_packet(s, 5)
    except Exception:
        pass
    s.close()
    return rc


if __name__ == "__main__":
    sys.exit(main())
