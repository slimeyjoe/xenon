#!/usr/bin/env python3
"""Small voice packs for voicebench.c: the same clips from a full ldp6 and a full Opus pack.

usage: mkbenchpak.py TEST_VOICE OUTDIR SECONDS PACK...

Takes clips spread over the whole pack (every k-th, so all speakers are in), until SECONDS of
audio, writes OUTDIR/<basename of PACK> for each PACK, and prints the FNV-1a hash voicebench
must print for SECONDS (decoded on the host by TEST_VOICE, the engine's own decoders)."""
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'tools'))
import xpak   # noqa: E402


def clip_seconds(data):
    rate, count = struct.unpack_from('>II', data, 4)
    return count / rate


def main():
    test_voice, outdir, secs = sys.argv[1], sys.argv[2], float(sys.argv[3])
    packs = sys.argv[4:]
    first = xpak.read(open(packs[0], 'rb').read())
    clips = [e for e in first if e['type'] == xpak.T_VOICE]
    step = max(1, len(clips) // 400)
    names, total = [], 0.0
    for e in clips[::step]:
        names.append(e['name'])
        total += clip_seconds(e['data'])
        if total >= secs * 1.2:
            break
    os.makedirs(outdir, exist_ok=True)
    for path in packs:
        ents = {e['name']: e for e in xpak.read(open(path, 'rb').read()) if e['type'] == xpak.T_VOICE}
        pk = xpak.Pack()
        for n in names:
            pk.entries.append((n, xpak.T_VOICE, xpak.L_ANY, ents[n]['data']))
        out = os.path.join(outdir, os.path.basename(path))
        data = pk.build()
        open(out, 'wb').write(data)
        # what voicebench decodes: clips in pack order until SECONDS of audio
        h, samples, rate = 2166136261, 0, 0
        for e in xpak.read(data):           # table order, as voicebench walks pk.entries
            if e['type'] != xpak.T_VOICE:
                continue
            if rate and samples >= secs * rate:
                break
            rate = struct.unpack_from('>I', e['data'], 4)[0]
            pcm = subprocess.run([test_voice, out, e['name'], '512'], capture_output=True, check=True).stdout
            for (s,) in struct.iter_unpack('<H', pcm):
                h = ((h ^ s) * 16777619) & 0xFFFFFFFF
            samples += len(pcm) // 2
        print('%s: %d clips, %d bytes; voicebench %d -> %d samples, hash %08x'
              % (out, len(names), len(data), secs, samples, h))


if __name__ == '__main__':
    main()
