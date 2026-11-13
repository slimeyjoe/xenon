#!/usr/bin/env python3
"""Write an AHI preferences file (ENVARC:Sys/ahi.prefs) for the emulator guest.

usage: mkahiprefs.py OUT [UNIT0_MODE_HEX [FREQ [CHANNELS]]]

Defaults: unit 0 = 0x00020009 "Paula:8 bit stereo", 22050 Hz, 4 channels; units 1-3
and the music unit get the same mode. Format: IFF FORM PREF with PRHD, AHIG and one
AHIU chunk per unit (struct AHIUnitPrefs / AHIGlobalPrefs in <devices/ahi.h>)."""
import struct
import sys


def chunk(cid, body):
    if len(body) & 1:
        body += b"\0"
    return cid + struct.pack(">I", len(body)) + body


def main():
    out = sys.argv[1]
    mode = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x00020009
    freq = int(sys.argv[3]) if len(sys.argv) > 3 else 22050
    channels = int(sys.argv[4]) if len(sys.argv) > 4 else 4
    body = chunk(b"PRHD", struct.pack(">BBI", 0, 0, 0))
    # AHIGlobalPrefs: DebugLevel, DisableSurround, DisableEcho, FastEcho, MaxCPU, ClipMasterVolume
    body += chunk(b"AHIG", struct.pack(">HhhhIh", 0, 0, 0, 0, 0x10000 * 90 // 100, 0))
    for unit in (0, 1, 2, 3, 255):
        # AHIUnitPrefs: Unit, Pad, Channels, AudioMode, Frequency, MonitorVolume, InputGain,
        # OutputVolume, Input, Output
        body += chunk(b"AHIU", struct.pack(">BBHIIIIIII", unit, 0, channels, mode, freq,
                                           0, 0x10000, 0x10000, 0, 0))
    with open(out, "wb") as f:
        f.write(b"FORM" + struct.pack(">I", 4 + len(body)) + b"PREF" + body)


if __name__ == "__main__":
    main()
