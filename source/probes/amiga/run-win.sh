#!/bin/sh
# winprobe in WinUAE (probes/amiga/README.md "The Workbench window").
#   run-win.sh modes [native|rtg]         list the display modes (native: PAL monitor only;
#                                         rtg: PAL + uaegfx)
#   run-win.sh wb MODEID DEPTH [native|rtg]
#                                         a Workbench 3.1 in MODEID (hex) with DEPTH planes
#                                         (or the depth of a graphics card mode), then
#                                         "winprobe wb" on it; shots/win_1.png
# The work dir is $RIG_WORKTOP/win-<what>/ (lrvne/rigs; RUN= overrides); GDB_PORT for parallel runs.
set -eu
what=${1:?modes|wb}
RIG=$(cd "$(dirname "$0")/../../lrvne/rigs/winuae" && pwd)
. "$RIG/run-lib.sh"
case $what in
modes)
	cfg=${2:-native}
	name=${RUN:-win-modes-$cfg}
	;;
wb)
	mode=${2:?MODEID}
	depth=${3:?DEPTH}
	cfg=${4:-native}
	name=${RUN:-win-wb-$mode-$depth}
	;;
*)
	echo "usage: $0 modes [native|rtg] | wb MODEID DEPTH [native|rtg]" >&2
	exit 2
	;;
esac
emu_setup "$name" wb
cp "$HERE/winprobe" "$W/dh0/C/"
# no host mouse integration: the probe moves the pointer itself
rm -f "$W/dh0/WBStartup/mousehack" "$W/dh0/WBStartup/timehack"

[ "$what" = wb ] && emu_wbmode "$mode" "$depth"

{
	cat <<EOF
!C:SetPatch QUIET
!C:MakeDir RAM:T RAM:ENV RAM:ENV/Sys RAM:Clipboards
!C:Copy >NIL: ENVARC: RAM:ENV ALL NOREQ
!Assign ENV: RAM:ENV
!Assign T: RAM:T
!Assign CLIPS: RAM:Clipboards
!Assign LIBS: SYS:Classes ADD
!DEVS:Monitors/PAL
EOF
	[ "$cfg" = rtg ] && echo "!DEVS:Monitors/uaegfx"
	echo "!C:IPrefs"
	if [ "$what" = modes ]; then
		echo "C:winprobe modes"
	else
		echo "!C:LoadWB"
		echo "!Wait 8"
		echo "!cd DH0:"
		echo "C:winprobe wb"
	fi
} | emu_script
emu_config "$cfg"
emu_run
