#!/bin/sh
# opn_lite CPU budget on an emulated 68040: runs opnbench under WinUAE with the
# 68040 CPU model, cycle-exact "approximate" timing at the given frequency
# (default 25 MHz) and SetPatch (68040.library from the Workbench tree, so the
# caches are on as on a real machine).  Usage: ./run-bench.sh [MHz [sum]]; "sum" runs
# "opnbench sum" (the per-configuration hash, to compare with ./opnsum-<song>).
set -eu
RIG=$(cd "$(dirname "$0")/../../lrvne/rigs/winuae" && pwd)
. "$RIG/run-lib.sh"
MHZ=${1:-25}
MODE=${2:-}
RUN=${RUN:-bench}
emu_setup "$RUN" wb
cp "$HERE/opnbench" "$W/dh0/C/"
emu_script <<EOF2
!C:SetPatch QUIET
Version
C:opnbench $MODE
EOF2
emu_config native
# 68040 at $MHZ MHz instead of the probe default (68020, fastest)
sed -i -e "s/^cpu_model=.*/cpu_model=68040/" -e "s/^cpu_compatible=.*/cpu_compatible=true/" \
    -e "s/^cpu_speed=.*/cpu_speed=real/" "$W/a.uae"
cat >> "$W/a.uae" <<EOF3
fpu_model=68040
mmu_model=0
cpu_cycle_exact=true
cpu_memory_cycle_exact=true
cpu_frequency=${MHZ}000000
cpu_data_cache=true
EOF3
EMU_TIMEOUT=${EMU_TIMEOUT:-900}
emu_run
