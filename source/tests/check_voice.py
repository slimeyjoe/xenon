#!/usr/bin/env python3
"""Decode voice clips with the engine's decoder (test_voice) and compare them with a reference.
usage: check_voice.py BIN VOICEPAK [N]

ldp packs: every sample must equal the reference decoder tools/ldp.py.
opus packs: the reference is the system libopus (ctypes; skipped if it is not installed). The
engine's libopus is fixed point, the system one usually float, so the lengths must match
exactly and the samples to within 40 dB SNR (they differ in the last bits only).

Checks N clips (default 60: the longest, the shortest, the multi-part ones, a spread of the
rest) with read sizes 1, 7 and 512 samples, so runs, blocks and packets cross read boundaries."""
import ctypes
import ctypes.util
import math
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'tools'))
import xpak   # noqa: E402
import ldp    # noqa: E402


class OpusRef:
    def __init__(self):
        name = ctypes.util.find_library('opus')
        for cand in ([name] if name else []) + ['/opt/local/lib/libopus.dylib', '/usr/local/lib/libopus.dylib',
                                                '/opt/homebrew/lib/libopus.dylib', 'libopus.so.0']:
            try:
                self.lib = ctypes.CDLL(cand)
                break
            except OSError:
                continue
        else:
            raise OSError('no libopus')
        self.lib.opus_decoder_create.restype = ctypes.c_void_p
        self.lib.opus_decoder_create.argtypes = [ctypes.c_int32, ctypes.c_int, ctypes.POINTER(ctypes.c_int)]
        self.lib.opus_decode.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int32,
                                         ctypes.POINTER(ctypes.c_int16), ctypes.c_int, ctypes.c_int]
        self.lib.opus_decoder_destroy.argtypes = [ctypes.c_void_p]

    def decode(self, data):
        rate, count, skip, _ = struct.unpack_from('>IIHH', data, 4)
        err = ctypes.c_int()
        dec = self.lib.opus_decoder_create(rate, 1, ctypes.byref(err))
        buf = (ctypes.c_int16 * 5760)()
        out, pos = [], 16
        while pos + 2 <= len(data):
            n = struct.unpack_from('>H', data, pos)[0]
            got = self.lib.opus_decode(dec, data[pos + 2:pos + 2 + n], n, buf, 5760, 0)
            if got < 0:
                break
            out.extend(buf[:got])
            pos += 2 + n
        self.lib.opus_decoder_destroy(dec)
        return out[skip:skip + count]


def snr(want, got):
    sig = sum(x * x for x in want) or 1
    err = sum((a - b) ** 2 for a, b in zip(want, got))
    return 99.0 if err == 0 else 10 * math.log10(sig / err)


def main():
    binary, pak = sys.argv[1:3]
    count = int(sys.argv[3]) if len(sys.argv) > 3 else 60
    data = open(pak, 'rb').read()
    clips = sorted((e for e in xpak.read(data) if e['type'] == xpak.T_VOICE), key=lambda e: e['size'])
    opus = clips and clips[0]['data'][:4] == b'opus'
    ref = None
    if opus:
        try:
            ref = OpusRef()
        except OSError:
            print('check_voice: skipped (an opus pack, and no system libopus to compare with)')
            return 0
    pick = clips[:5] + clips[-5:] + [e for e in clips if e['name'][-1] in 'ABC'][:10]
    step = max(1, len(clips) // max(1, count - len(pick)))
    pick += clips[::step]
    seen, fails, worst = set(), 0, 99.0
    for e in pick:
        if e['name'] in seen:
            continue
        seen.add(e['name'])
        if opus:
            want = ref.decode(e['data'])
        else:
            _, want = ldp.decode(e['data'])
        wantb = struct.pack('<%dh' % len(want), *want)
        for chunk in (1, 7, 512) if e in clips[:5] else (7, 512):
            got = subprocess.run([binary, pak, e['name'], str(chunk)], capture_output=True).stdout
            if opus and len(got) == len(wantb):
                s = snr(want, struct.unpack('<%dh' % (len(got) // 2), got))
                worst = min(worst, s)
                if s >= 40:
                    continue
                print('FAIL %s (chunk %d): %.1f dB SNR against libopus' % (e['name'], chunk, s))
            elif got != wantb:
                print('FAIL %s (chunk %d): %d bytes, expected %d' % (e['name'], chunk, len(got), len(wantb)))
            else:
                continue
            fails += 1
    print('check_voice: %s, %d clips, %d failures%s' % ('opus' if opus else 'ldp', len(seen), fails,
                                                       ' (worst %.1f dB SNR)' % worst if opus else ''))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
