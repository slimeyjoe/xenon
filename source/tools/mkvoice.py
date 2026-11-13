#!/usr/bin/env python3
"""Build the voice pack (xenon_voice.pak) from the Windows release (docs/voice.md §2.2).

usage: mkvoice.py --dat XENON.DAT [--voicemap FILE] [--codec ldp6|opus] [--bitrate KBPS]
                  [--jobs N] -o xenon_voice.pak

Every clip named in the cue table (resources/voice/voicemap.tsv) is taken from the Windows
release's XENON.DAT (tools/arc3.py; duplicate copies resolved by per-byte majority) and stored
as one XPAK_T_VOICE entry named after the clip (upper case, e.g. RYO0212). Identical clips
are stored once (the Windows release has a renamed copy for each repetition of a line).
The clips' data is laid out in the order the cue table first uses them (roughly the order of
play), so reads move forward through the file in short steps (FFS seeks are slow).

A "voice" META entry describes the pack:
  format=1
  codec=ldp6          the payload of every VOICE entry:
                        ldp6  the original data (decoder engine/voice/vdec_ldp.c, format
                              docs/re/other-versions.md §2.4), 22050 Hz, ~261 MB;
                        opus  re-encoded with ffmpeg's libopus (--bitrate, default 24 kbit/s,
                              ~60 MB; decoder engine/voice/vdec_opus.c, layout below)
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
import concurrent.futures
import csv
import hashlib
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import arc3   # noqa: E402
import ldp    # noqa: E402
import xpak   # noqa: E402

OPUS_RATE = 24000                   # 22050 is not an Opus rate; 24 kHz keeps the whole band


def ogg_packets(data):
    """The packets of a single-stream Ogg file and the last page's granule position."""
    out, cur, pos, granule = [], b'', 0, 0
    while pos < len(data):
        if data[pos:pos + 4] != b'OggS':
            raise ValueError('bad Ogg page at %d' % pos)
        granule = struct.unpack_from('<q', data, pos + 6)[0]
        nseg = data[pos + 26]
        lacing = data[pos + 27:pos + 27 + nseg]
        pos += 27 + nseg
        for n in lacing:
            cur += data[pos:pos + n]
            pos += n
            if n < 255:
                out.append(cur)
                cur = b''
    return out, granule


def encode_opus(job):
    """ldp6 clip -> the opus clip layout above (runs in a worker process)."""
    data, kbps = job
    rate, samples = ldp.decode(data)
    pcm = struct.pack('<%dh' % len(samples), *samples)
    ogg = subprocess.run(
        ['ffmpeg', '-v', 'error', '-f', 's16le', '-ar', str(rate), '-ac', '1', '-i', '-',
         '-ar', str(OPUS_RATE), '-c:a', 'libopus', '-b:a', '%dk' % kbps, '-application', 'audio',
         '-f', 'ogg', '-'], input=pcm, capture_output=True, check=True).stdout
    pkts, granule = ogg_packets(ogg)
    if not pkts[0].startswith(b'OpusHead') or not pkts[1].startswith(b'OpusTags'):
        raise ValueError('not an Ogg Opus stream')
    pre48 = struct.unpack_from('<H', pkts[0], 10)[0]          # in 48 kHz units, as in Ogg Opus
    scale = 48000 // OPUS_RATE
    count = (granule - pre48) // scale
    expect = len(samples) * OPUS_RATE // rate
    if abs(count - expect) > 1:
        raise ValueError('Opus length %d, expected %d' % (count, expect))
    out = [b'opus', struct.pack('>IIHH', OPUS_RATE, count, pre48 // scale, 0)]
    for p in pkts[2:]:
        if len(p) > 1275:
            raise ValueError('Opus packet of %d bytes' % len(p))
        out += [struct.pack('>H', len(p)), p]
    return b''.join(out)

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
    # (backends/amiga/README.md "Pack on FFS")
    play_order = list(dict.fromkeys(r['clip'].upper() for r in rows))
    arc = arc3.Arc3(a.dat)
    pk = xpak.Pack()
    flipped = 0
    for clip in clips:
        key = b'ldp' + clip.lower().encode('ascii')
        if key not in arc.entries:
            sys.exit('mkvoice: %s is not in %s' % (clip, a.dat))
        data, disagree, _ = arc.read(key)
        flipped += disagree > 0
        if data[:4] != b'ldp6' or struct.unpack_from('>I', data, 4)[0] != 22050:
            sys.exit('mkvoice: %s is not a 22050 Hz ldp6 clip' % clip)
        pk.entries.append((clip.upper(), xpak.T_VOICE, xpak.L_ANY, data))   # names are unique
    rate, extra = 22050, ''
    if a.codec == 'opus':
        # identical clips (renamed copies) are encoded once; the pack stores them once too
        uniq = sorted({e[3] for e in pk.entries}, key=len, reverse=True)
        with concurrent.futures.ProcessPoolExecutor(a.jobs) as ex:
            enc = dict(zip(uniq, ex.map(encode_opus, [(d, a.bitrate) for d in uniq], chunksize=8)))
        pk.entries = [(n, t, l, enc[d]) for n, t, l, d in pk.entries]
        rate, extra = OPUS_RATE, 'bitrate=%d\n' % a.bitrate
    meta = 'format=1\ncodec=%s\nrate=%d\ncues=%s\nclips=%d\n%s' % (
        a.codec, rate, hashlib.md5(table).hexdigest(), len(clips), extra)
    pk.add('voice', xpak.T_META, xpak.L_ANY, meta.encode('ascii'))
    data = pk.build(dedupe=True, data_order=play_order)
    with open(a.output, 'wb') as fo:
        fo.write(data)
    print('%s: %d clips, %d bytes, codec %s (%d clips repaired from duplicate copies)'
          % (a.output, len(clips), len(data), a.codec, flipped))


if __name__ == '__main__':
    main()
