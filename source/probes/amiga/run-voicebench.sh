#!/bin/sh
# voicebench (voicebench.c): CPU cost of the voice decoders on an emulated 68040 or 68060,
# WinUAE cycle-exact "approximate" timing at MHz (as run-bench.sh), SetPatch for the caches.
#   ./run-voicebench.sh [MHz [CPU [SECONDS]]]
# BIN = the voicebench binary, PAKS = the packs to decode (mkbenchpak.py), copied to DH0:T/.
# SETPATCH=0: no SetPatch (68060). FAST=1: the probe's default fast CPU instead (a functional check, timings meaningless).
set -eu
RIG=$(cd "$(dirname "$0")/../../lrvne/rigs/winuae" && pwd)
. "$RIG/run-lib.sh"
MHZ=${1:-25}
CPUM=${2:-68040}
SECS=${3:-60}
RUN=${RUN:-voicebench-$CPUM-$MHZ}
emu_setup "$RUN" wb
cp "$BIN" "$W/dh0/C/voicebench"
for p in $PAKS; do cp "$p" "$W/dh0/T/"; done
{
	# SETPATCH=0: the 3.1 SetPatch loads 68040.library, which a 68060 cannot run (a real one
	# has 68060.library and a newer SetPatch); the bench needs neither (no FPU, no 64-bit mul)
	[ "${SETPATCH:-1}" = 0 ] || echo "!C:SetPatch QUIET"
	echo "!Stack 100000"
	echo "Version"
	for p in $PAKS; do echo "C:voicebench DH0:T/$(basename "$p") $SECS DH0:T/$(basename "$p").pcm"; done
} | emu_script
emu_config native
if [ "${FAST:-0}" != 1 ]; then
	sed -i -e "s/^cpu_model=.*/cpu_model=$CPUM/" -e "s/^cpu_compatible=.*/cpu_compatible=true/" \
	    -e "s/^cpu_speed=.*/cpu_speed=real/" "$W/a.uae"
	cat >> "$W/a.uae" <<EOF3
fpu_model=$CPUM
mmu_model=0
cpu_cycle_exact=true
cpu_memory_cycle_exact=true
cpu_frequency=${MHZ}000000
cpu_data_cache=true
EOF3
fi
EMU_TIMEOUT=${EMU_TIMEOUT:-1500}
emu_run || true
cat "$W/dh0/out.txt"; ls -l "$W"/dh0/T/*.pcm 2>/dev/null || true
