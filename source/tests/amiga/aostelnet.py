#!/usr/bin/env python3
# aostelnet.py HOST PORT [CMD...]: run AmigaOS shell commands over telnet.
# Every option is refused; each CMD is sent and its output printed up to
# the next prompt (PROMPT, a regex, default "\d+\.[^>\n]*> $").  Without
# CMDs it prints what arrives in WAIT seconds (the greeting).
# Taken from the author's Plan 9 workspace (source/tools/amiga/aostelnet.py).
import os, re, socket, sys, time

IAC, DONT, DO, WONT, WILL, SB, SE = 255, 254, 253, 252, 251, 250, 240
host, port, cmds = sys.argv[1], int(sys.argv[2]), sys.argv[3:]
prompt = re.compile(os.environ.get('PROMPT', r'\d+\.[^>\n]*> $').encode())
tmo = float(os.environ.get('TMO', '120'))
s = socket.create_connection((host, port), timeout=10)


def recv(secs):
    out = b''
    end = time.time() + secs
    s.settimeout(0.3)
    while time.time() < end:
        try:
            d = s.recv(4096)
        except socket.timeout:
            if prompt.search(out):
                return out, True
            continue
        if not d:
            return out, False
        i = 0
        while i < len(d):
            c = d[i]
            if c == IAC and i + 1 < len(d):
                op = d[i + 1]
                if op in (DO, DONT, WILL, WONT) and i + 2 < len(d):
                    s.sendall(bytes([IAC, WONT if op in (DO, DONT) else DONT,
                        d[i + 2]]))
                    i += 3
                    continue
                if op == SB:
                    j = d.find(bytes([IAC, SE]), i)
                    i = len(d) if j < 0 else j + 2
                    continue
                i += 2
                continue
            out += bytes([c])
            i += 1
    return out, prompt.search(out) is not None


def show(b):
    sys.stdout.write(b.decode('latin-1').replace('\r', ''))
    sys.stdout.flush()


out, ok = recv(float(os.environ.get('WAIT', '5')))
show(out)
for c in cmds:
    s.sendall(c.encode('latin-1') + b'\r\n')
    out, ok = recv(tmo)
    show(out)
    if not ok:
        sys.exit('no prompt after: ' + c)
