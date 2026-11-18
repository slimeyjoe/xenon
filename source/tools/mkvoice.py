#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the voice pack (xenon_voice.pak) from the Windows release (docs/voice.md §2.2).

usage: mkvoice.py --dat XENON.DAT [--voicemap FILE] [--codec ldp6|opus] [--bitrate KBPS]
                  [--jobs N] -o xenon_voice.pak

Every clip named in the cue table (resources/voice/voicemap.tsv) is taken from the Windows
release's XENON.DAT (tools/arc3.py; duplicate copies resolved by per-byte majority) and stored
as one VOICE entry of an lrvne voice pack (lrvne.voicepack) named after the clip (upper case, e.g. RYO0212). Identical clips
are stored once (the Windows release has a renamed copy for each repetition of a line).
The clips' data is laid out in the order the cue table first uses them (roughly the order of
play), so reads move forward through the file in short steps (FFS seeks are slow).

A "voice" META entry describes the pack:
  format=1
  codec=ldp6          the payload of every VOICE entry:
                        ldp6  the original data (lrvne's ldp decoder, lrvne/vdec.h; format
                              docs/re/other-versions.md §2.4), 22050 Hz, ~261 MB;
                        opus  re-encoded with ffmpeg's libopus (--bitrate, default 24 kbit/s,
                              ~48 MB; lrvne's Opus decoder, layout below)
  rate=22050          the clips' sample rate (opus: 24000)
  cues=<md5 of the cue table>   the main pack's cue tables must come from the same table
  clips=<count>
  bitrate=<kbit/s>    opus only

An opus clip (docs/voice.md §3), all numbers big-endian:
  0x00  "opus"
  0x04  u32 sample rate the clip decodes at (24000), mono
  0x08  u32 sample count, after the pre-skip
  0x0C  u16 pre-skip: decoded samples to drop at the start (encoder delay)
  0x0E  u16 0
  0x10  packets: u16 byte count, then the Opus packet (RFC 6716), until the end of the entry
"""
import argparse
import csv
import hashlib
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', 'lrvne', 'tools'))
import arc3   # noqa: E402
import ldp    # noqa: E402
from lrvne import voicepack   # noqa: E402


def ldp_pcm16(data):
    """A decoder for lrvne.voicepack: ldp clip -> (rate, 16-bit LE samples)."""
    rate, samples = ldp.decode(data)
    return rate, struct.pack('<%dh' % len(samples), *samples)


ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--dat', required=True)
    ap.add_argument('--voicemap', default=os.path.join(ROOT, 'resources', 'voice', 'voicemap.tsv'))
    ap.add_argument('--codec', default='ldp6', choices=['ldp6', 'opus'])
    ap.add_argument('--bitrate', type=int, default=24, help='opus: kbit/s (default 24)')
    ap.add_argument('--jobs', type=int, default=os.cpu_count() or 1, help='opus: encoder processes')
    ap.add_argument('-o', '--output', required=True)
    a = ap.parse_args()

    table = open(a.voicemap, 'rb').read()
    rows = list(csv.DictReader(table.decode('utf-8').splitlines(), delimiter='\t'))
    clips = sorted({r['clip'] for r in rows})
    # the clips' data in the order the cue table first uses them (scripts in order, messages in
    # order: roughly the order of play), so a player's reads mostly move forward through the
    # file in short steps; on FFS a long or backward seek walks the file's block chain
    # (lrvne/backends/amiga/README.md "Preload")
    play_order = list(dict.fromkeys(r['clip'].upper() for r in rows))
    arc = arc3.Arc3(a.dat)
    data = {}
    flipped = 0
    for clip in clips:
        key = b'ldp' + clip.lower().encode('ascii')
        if key not in arc.entries:
            sys.exit('mkvoice: %s is not in %s' % (clip, a.dat))
        d, disagree, _ = arc.read(key)
        flipped += disagree > 0
        if d[:4] != b'ldp6' or struct.unpack_from('>I', d, 4)[0] != 22050:
            sys.exit('mkvoice: %s is not a 22050 Hz ldp6 clip' % clip)
        data[clip.upper()] = d
    order = play_order + sorted(set(data) - set(play_order))
    try:
        pack = voicepack.build_voice_pack(
            [(n, data[n], 'ldp6', 22050) for n in order],
            codec='opus' if a.codec == 'opus' else None, cues=hashlib.md5(table).hexdigest(),
            bitrate=a.bitrate, jobs=a.jobs, decoders={'ldp6': ldp_pcm16})
    except voicepack.VoiceError as e:
        sys.exit('mkvoice: %s' % e)
    with open(a.output, 'wb') as fo:
        fo.write(pack)
    print('%s: %d clips, %d bytes, codec %s (%d clips repaired from duplicate copies)'
          % (a.output, len(clips), len(pack), a.codec, flipped))


if __name__ == '__main__':
    main()
