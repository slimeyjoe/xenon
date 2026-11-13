#!/bin/sh
# Runs PowerPC (WarpOS) programs in WinUAE with Blizzard PPC emulation (qemu-uae).
#
#   run-wos.sh NAME [wb] < commands
#
# NAME: work dir $EMU/aos/work/xenon/wos-NAME.  "wb" copies the Workbench tree (needed for
# screens/fonts/AHI); without it the guest is a bare shell.  The commands on stdin become
# the startup-sequence (see emu_script in ../amiga/run-lib.sh: plain lines append their
# output to DH0:out.txt, "!" lines run verbatim).  Files to run go into $W/dh0 before:
# set WOS_FILES="path..." (copied to DH0:). WOS_SHOTS="20 45": host screenshots at those
# seconds (shots/tNN.png).
#
# WOS_WB=MODEID:DEPTH starts a Workbench in that mode before the commands (needs "wb"; for
# the game's WINDOW option), WOS_RTG=1 adds a uaegfx Zorro III board (its modes for
# WOS_WB or MODE=), SNAPD=1 serves the game's snapshot requests in DH0: with
# tests/e2e/amiga_snapd.py (results in $W/snaps/, checked with tests/e2e/amiga.py check).
#
# Settings (tests/rig.conf or the environment, README.md): EMU, WINUAE (default: the PPC build
# $EMU/obj-winuae-ppc/winuae), WARPUP (the WarpUP 5.1 tree), BPPC_ROM (the Blizzard PPC ROM),
# KICKSTART, WBDISK (for "wb"); GDB_PORT (default 2361, so it can run next to the 68k rig),
# EMU_TIMEOUT, PPC_MODEL (603ev or 604e), WOS_CPU (68060 or 68040: the board's 68k; the 060
# ROM is the default). WOS_UAE="key=value ..." replaces or adds WinUAE config lines
# (experiments). RTAREA = where WinUAE put its boot ROM, which uaequit calls (default db0000:
# the address emu.log prints as "RTAREA located at").
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$(cd "$HERE/../.." && pwd)
RIG=$(cd "$HERE/../amiga" && pwd)
RIG_TESTS=$(cd "$HERE/.." && pwd)
. "$RIG_TESTS/rig.sh"
EMU=${EMU:-$HOME/amiga-emu}
WINUAE=${WINUAE:-$EMU/obj-winuae-ppc/winuae}
GDB_PORT=${GDB_PORT:-2361}
WARPUP=${WARPUP:-$HOME/amiga-sdk/WarpUP-5.1}
PPC_MODEL=${PPC_MODEL:-603ev}
BPPC_ROM=${BPPC_ROM:-$EMU/rom/blizzardppc_060_44.71.rom}
. "$RIG/run-lib.sh"

name=$1
emu_setup "wos-$name" "${2:-}"

# WarpOS 5.1 runtime: kernel, warp.library, the Blizzard PPC hardware driver
cp "$WARPUP/Libs/powerpc.library" "$WARPUP/Libs/warp.library" \
    "$WARPUP/hwdrivers/blizzardppc/warpHW.library" "$W/dh0/Libs/"
for f in ${WOS_FILES:-}; do
	cp "$f" "$W/dh0/"
done

# ENV: for WarpOS: the Blizzard PPC flash ROM starts Phase5's ppc.library at boot and it
# can't be switched off, so WarpOS needs its "terminator" (powerpc.guide: always 2);
# HideWarning skips the ppc.library requester that would block the unattended run
mkdir -p "$W/dh0/Env/powerpc"
echo 2 > "$W/dh0/Env/powerpc/Terminator"
echo 1 > "$W/dh0/Env/powerpc/HideWarning"
[ -n "${WOS_WB:-}" ] && emu_wbmode "${WOS_WB%%:*}" "${WOS_WB#*:}"
{
	echo "!Assign ENV: DH0:Env"
	if [ -n "${WOS_WB:-}" ]; then
		echo "!C:MakeDir RAM:T RAM:Clipboards"
		echo "!Assign T: RAM:T"
		echo "!Assign CLIPS: RAM:Clipboards"
		echo "!C:Copy >NIL: ENVARC: ENV: ALL NOREQ"
		echo "!Assign LIBS: SYS:Classes ADD"
		echo "!DEVS:Monitors/PAL"
		[ "${WOS_RTG:-0}" = 1 ] && echo "!DEVS:Monitors/uaegfx"
		echo "!C:IPrefs"
		echo "!C:LoadWB"
		echo "!Wait 6"
	fi
	cat
} | emu_script
# WinUAE moves its boot ROM (the uaelib trap uaequit calls) when the board ROM takes
# 0xf00000; RTAREA is the address the emulator log prints ("RTAREA located at")
sed -i "s|^C:uaequit\$|C:uaequit ${RTAREA:-db0000}|" "$W/dh0/S/startup-sequence" "$W/dh0/S/watchdog"

# Blizzard PPC: A1200, the board's 68060 + a PPC 603e/604e through qemu-uae, 128 MB on the
# board.  Real time (warp off): WarpOS boots with the 68k and the PPC in step.
cat > "$W/a.uae" <<EOF
config_description=xenon WarpOS probe ($name)
use_gui=no
kickstart_rom_file=$KICKSTART
chipset=aga
chipset_compatible=A1200
cpu_model=68060
mmu_model=68060
fpu_model=68060
cpu_compatible=false
cpu_24bit_addressing=false
cpu_speed=max
cachesize=0
cycle_exact=false
immediate_blits=true
warp=false
cpuboard_type=BlizzardPPC
cpuboard_rom_file=$BPPC_ROM
cpuboardmem1_size=128
ppc_model=$PPC_MODEL
ppc_implementation=qemu
chipmem_size=4
fastmem_size=0
nr_floppies=1
floppy0type=0
filesystem2=rw,DH0:dh0:$W/dh0,10
debugging_features=gdbserver
gdb_port=$GDB_PORT
EOF

# extra config lines (experiments): WOS_UAE="key=value key=value"
for kv in ${WOS_UAE:-}; do
	k=${kv%%=*}
	sed -i "/^$k=/d" "$W/a.uae"
	echo "$kv" >> "$W/a.uae"
done

if [ "${WOS_RTG:-0}" = 1 ]; then
	cat >> "$W/a.uae" <<EOF
z3mem_size=16
gfxcard_type=ZorroIII
gfxcard_size=16
EOF
fi

if [ "${SNAPD:-0}" = 1 ]; then
	# snapshot requests from the guest (backends/amiga/core/snap.c), SNAPDIR = DH0:
	python3 -I "$SRC/tests/e2e/amiga_snapd.py" --port "$GDB_PORT" --dir "$W/dh0" \
	    --out "$W/snaps" > "$W/snapd.log" 2>&1 &
	EMU_HELPER_PID=$!
fi

# timed screenshots from the host: WOS_SHOTS="20 45" (seconds after the start) ->
# shots/tNN.png through the gdbserver
if [ -n "${WOS_SHOTS:-}" ]; then
	(
		prev=0
		for t in $WOS_SHOTS; do
			sleep $((t - prev))
			prev=$t
			python3 "$RIG/gdbshot.py" "$GDB_PORT" "$W/shots/t$t.png" >> "$W/gdbshot.log" 2>&1 || true
		done
	) &
	EMU_HELPER_PID=$!
fi

emu_run
