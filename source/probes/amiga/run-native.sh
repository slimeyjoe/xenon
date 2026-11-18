#!/bin/sh
# Config 1: native AGA, no RTG.  sbprobe on 0x8004 (HiRes Laced) and on
# 0x29004 (PAL HiRes Laced), trailerprobe from the CLI (with and without
# the appended trailer; the shell window is screenshotted afterwards so
# that LoadSeg errors, which go to the console, are visible too).
set -eu
# sbprobe and trailerprobe (make) into the guest's C:
EMU_C_FILES="$(cd "$(dirname "$0")" && pwd)/sbprobe $(cd "$(dirname "$0")" && pwd)/trailerprobe"
RIG=$(cd "$(dirname "$0")/../../lrvne/rigs/winuae" && pwd)
. "$RIG/run-lib.sh"
emu_setup native
cp "$HERE/trailerprobe.hunk" "$HERE/trailerprobe.raw" "$W/dh0/C/"
emu_script <<'EOF2'
Version
Avail
C:sbprobe 0x8004 native
C:sbprobe 0x29004 nativepal
C:trailerprobe.hunk
Why
C:trailerprobe.raw
Why
C:trailerprobe
Why
!Echo "" >DH0:shot_cli_1.req
!Wait 15
EOF2
emu_config native
emu_run
