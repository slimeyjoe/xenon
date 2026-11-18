#!/bin/sh
# RTG configs: Workbench 3.1 + Picasso96 tree as DH0:, uaegfx Zorro III
# board.  One emulator run per variant:
#   run-rtg.sh native    config 2: P96 loaded, sbprobe on 0x8004 and 0x29004
#   run-rtg.sh uaegfx    config 3: sbprobe on the uaegfx 640x480x8 mode
#   run-rtg.sh promote   config 2 + FAKENATIVEMODES=Yes tool type on the
#                        uaegfx monitor icon, no PAL monitor: sbprobe 0x8004
#   run-rtg.sh wb        LoadWB with trailerprobe in SYS:WBStartup (a
#                        Workbench start), plus the CLI run
# P96_SHOWMODES=Essential instead of All for Picasso96's ShowModes setting.
set -eu
variant=${1:-native}
# sbprobe and trailerprobe (make) into the guest's C:
EMU_C_FILES="$(cd "$(dirname "$0")" && pwd)/sbprobe $(cd "$(dirname "$0")" && pwd)/trailerprobe"
RIG=$(cd "$(dirname "$0")/../../lrvne/rigs/winuae" && pwd)
. "$RIG/run-lib.sh"
emu_setup "rtg-$variant" wb

# ENVARC:Picasso96/ShowModes: "Essential" in the tree; the WinUAE P96 smoke
# scripts set "All".  Override with P96_SHOWMODES=Essential to compare.
printf '%s' "${P96_SHOWMODES:-All}" > "$W/dh0/Prefs/Env-Archive/Picasso96/ShowModes"

# The P96 driver icon: FAKENATIVEMODES promotes LowRes/HiRes/SuperHiRes
# (and their laced variants) to a 640x480 board mode (Picasso96.readme).
case $variant in
promote)
	python3 "$RIG/settooltype.py" "$WBDISK/Devs/Monitors/uaegfx.info" \
	    "$W/dh0/Devs/Monitors/uaegfx.info" 'FAKENATIVEMODES=Yes'
	monitors="DEVS:Monitors/uaegfx"
	;;
*)
	monitors="DEVS:Monitors/PAL
DEVS:Monitors/uaegfx"
	;;
esac

# Workbench start: the program plus a tool icon in SYS:WBStartup.
if [ "$variant" = wb ]; then
	cp "$HERE/trailerprobe" "$W/dh0/WBStartup/trailerprobe"
	python3 "$RIG/settooltype.py" "$WBDISK/Utilities/Clock.info" \
	    "$W/dh0/WBStartup/trailerprobe.info" 'DONOTWAIT'
	rm -f "$W/dh0/WBStartup/mousehack" "$W/dh0/WBStartup/timehack"
fi

{
	cat <<EOF
!C:SetPatch QUIET
!C:MakeDir RAM:T RAM:ENV RAM:ENV/Sys RAM:Clipboards
!C:Copy >NIL: ENVARC: RAM:ENV ALL NOREQ
!Assign ENV: RAM:ENV
!Assign T: RAM:T
!Assign CLIPS: RAM:Clipboards
!Assign LIBS: SYS:Classes ADD
EOF
	echo "$monitors" | sed 's/^/!/'
	echo "!C:IPrefs"
	echo "Version"
	echo "Avail"
	echo "List DEVS:Monitors LIBS:Picasso96"
	case $variant in
	native)
		echo "C:sbprobe 0x8004 rtgnative"
		echo "C:sbprobe 0x29004 rtgpal"
		;;
	uaegfx)
		echo "C:sbprobe uaegfx rtg"
		;;
	promote)
		echo "C:sbprobe 0x8004 promote"
		;;
	wb)
		echo "C:trailerprobe"
		echo "!C:LoadWB"
		echo "!Wait 20"
		echo '!Echo "" >DH0:shot_wb_1.req'
		echo "!Wait 15"
		;;
	esac
} | emu_script
emu_config rtg
emu_run
grep -i -E 'Unix RTG|uaegfx|P96' "$W/emu.log" | head -20
