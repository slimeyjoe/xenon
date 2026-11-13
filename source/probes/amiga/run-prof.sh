#!/bin/sh
# opn_lite PC-sampling profile on WinUAE's cycle-exact 68040/25 (as run-bench.sh):
#   ./run-prof.sh [SONG [RATE [OS]]]      default XENON_24 11025 1
# Needs opnprof-SONG (make opnprof-SONG); writes bench-SONG/prof-RATE[-os2].txt, then
# python3 symprof.py bench-SONG/opn_lite_prof.lst bench-SONG/prof-RATE.txt reads it.
set -eu
RIG=$(cd "$(dirname "$0")/../../tests/amiga" && pwd)
. "$RIG/run-lib.sh"
SONG=${1:-XENON_24}
RATE=${2:-11025}
OS=${3:-1}
tag=prof-$RATE
[ "$OS" = 1 ] || tag=$tag-os$OS
RUN=${RUN:-prof}
GDB_PORT=${GDB_PORT:-2347}
emu_setup "$RUN" wb
cp "$HERE/opnprof-$SONG" "$W/dh0/C/opnprof"
emu_script <<EOF2
!C:SetPatch QUIET
Version
C:opnprof $RATE $OS DH0:$tag.txt
Type DH0:$tag.txt FIRST 1 LINES 2
EOF2
emu_config native
sed -i -e "s/^cpu_model=.*/cpu_model=68040/" -e "s/^cpu_compatible=.*/cpu_compatible=true/" \
    -e "s/^cpu_speed=.*/cpu_speed=real/" "$W/a.uae"
cat >> "$W/a.uae" <<EOF3
fpu_model=68040
mmu_model=0
cpu_cycle_exact=true
cpu_memory_cycle_exact=true
cpu_frequency=25000000
cpu_data_cache=true
EOF3
EMU_TIMEOUT=${EMU_TIMEOUT:-600}
emu_run
mkdir -p "$HERE/bench-$SONG"
cp "$W/dh0/$tag.txt" "$HERE/bench-$SONG/$tag.txt"
echo "== bench-$SONG/$tag.txt"
