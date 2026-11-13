#!/usr/bin/env python3
"""FM Towns CD image (BIN/CUE, 2352-byte sectors): data track -> ISO, audio tracks -> WAV.

Usage:
  python3 -I re-tools/cdbin.py info  IMAGE.cue
  python3 -I re-tools/cdbin.py iso   IMAGE.cue OUT.iso          # track 1 MODE1/2352 user data
  python3 -I re-tools/cdbin.py audio IMAGE.cue OUTDIR           # trackNN.wav, 44.1 kHz stereo

The cue of the Xenon dump ("Xenon (19xx)(Cs Ware)(Jp)") is wrong for track 3: it puts INDEX 01
at 26:57:31 (file sector 121306), but track 2 fades out into a digital-silence gap at sectors
121156..121162 and the music of track 3 starts at 121163 - with the cue position the first
1.95 s of track 3 would be appended to track 2. 'audio' therefore moves each audio track start back
to the beginning of the nearest run of digital silence (within 4 s) before the cue position.
The ISO 9660 file system can then be read with 7z (7z x OUT.iso). docs/re/other-versions.md §3.1.
"""
import os
import re
import sys
import wave

SECTOR = 2352
SYNC = b'\x00' + b'\xff' * 10 + b'\x00'


def parse_cue(path):
    text = open(path, encoding='latin-1').read()
    binname = re.search(r'FILE\s+"([^"]+)"', text).group(1)
    binpath = os.path.join(os.path.dirname(path), binname)
    if not os.path.exists(binpath):          # case differences (.BIN vs .bin)
        low = {f.lower(): f for f in os.listdir(os.path.dirname(path) or '.')}
        binpath = os.path.join(os.path.dirname(path), low[binname.lower()])
    tracks = []
    for m in re.finditer(r'TRACK\s+(\d+)\s+(\S+)(.*?)INDEX\s+01\s+(\d+):(\d+):(\d+)', text, re.S):
        mm, ss, ff = int(m.group(4)), int(m.group(5)), int(m.group(6))
        tracks.append(dict(no=int(m.group(1)), mode=m.group(2), start=(mm * 60 + ss) * 75 + ff))
    return binpath, tracks


def silent_run_start(f, sector, back=300):
    """First sector of the last all-zero run before `sector` (within `back` sectors), or None."""
    best = None
    s = sector - 1
    while s >= sector - back:
        f.seek(s * SECTOR)
        if f.read(SECTOR).strip(b'\0'):
            if best is not None:
                return best
        else:
            best = s
        s -= 1
    return best


def layout(cue):
    binpath, tracks = parse_cue(cue)
    total = os.path.getsize(binpath) // SECTOR
    with open(binpath, 'rb') as f:
        for i, t in enumerate(tracks):
            if t['mode'] == 'AUDIO' and i > 0 and tracks[i - 1]['mode'] == 'AUDIO':
                s = silent_run_start(f, t['start'])
                if s is not None:
                    t['cue_start'], t['start'] = t['start'], s
        for i, t in enumerate(tracks):
            t['end'] = tracks[i + 1]['start'] if i + 1 < len(tracks) else total
        if tracks and tracks[0]['mode'].startswith('MODE1'):
            # the data track ends at its last sector with a sync pattern
            end = tracks[0]['end']
            while end > 0:
                f.seek((end - 1) * SECTOR)
                if f.read(12) == SYNC:
                    break
                end -= 1
            tracks[0]['end'] = end
    return binpath, tracks


def main(a):
    if len(a) < 2 or a[0] not in ('info', 'iso', 'audio'):
        print(__doc__)
        return 1
    binpath, tracks = layout(a[1])
    if a[0] == 'info':
        for t in tracks:
            n = t['end'] - t['start']
            extra = ' (cue said %d)' % t['cue_start'] if 'cue_start' in t else ''
            print('track %02d %-11s sectors %6d..%6d%s  %d:%05.2f' % (
                t['no'], t['mode'], t['start'], t['end'], extra, n // 4500, n % 4500 / 75))
        return 0
    with open(binpath, 'rb') as f:
        if a[0] == 'iso':
            t = tracks[0]
            with open(a[2], 'wb') as out:
                f.seek(t['start'] * SECTOR)
                for _ in range(t['end'] - t['start']):
                    out.write(f.read(SECTOR)[16:16 + 2048])
            return 0
        os.makedirs(a[2], exist_ok=True)
        for t in tracks:
            if t['mode'] != 'AUDIO':
                continue
            f.seek(t['start'] * SECTOR)
            w = wave.open(os.path.join(a[2], 'track%02d.wav' % t['no']), 'wb')
            w.setnchannels(2)
            w.setsampwidth(2)
            w.setframerate(44100)
            w.writeframes(f.read((t['end'] - t['start']) * SECTOR))
            w.close()
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
