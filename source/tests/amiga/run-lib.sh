# Shared WinUAE plumbing of the Amiga rig (tests/amiga, tests/warpos) and of the probe run
# scripts (probes/amiga). Sourced, not run, after the caller set RIG to this directory
# (tests/amiga: uaequit, gdbshot.py); HERE is the caller's own directory.
#
# Settings (tests/rig.conf or the environment, README.md): EMU (~/amiga-emu), WINUAE (the
# null-video Linux WinUAE build), WBDISK (read-only Workbench 3.1 + Picasso96 tree), KICKSTART,
# GDB_PORT. EMU_C_FILES="path..." are copied into the guest's C: as well (a probe's programs).
# A run lives in $EMU/aos/work/xenon/<name>/: dh0/ (the guest's DH0:),
# a.uae, emu.log, shots/*.png, out.txt (copied from dh0/out.txt).

RIG_TESTS=$(cd "$RIG/.." && pwd)
. "$RIG_TESTS/rig.sh"
EMU=${EMU:-$HOME/amiga-emu}
WINUAE=${WINUAE:-$EMU/obj-winuae/winuae}
WBDISK=${WBDISK:-}
KICKSTART=${KICKSTART:-$EMU/rom/kick40068.A1200}
GDB_PORT=${GDB_PORT:-2345}
EMU_TIMEOUT=${EMU_TIMEOUT:-300}
HERE=$(cd "$(dirname "$0")" && pwd)
WORKTOP=$EMU/aos/work/xenon

# emu_setup NAME [wb]: fresh work dir; "wb" copies the Workbench/P96 tree.
emu_setup()
{
	W=$WORKTOP/$1
	rm -rf "$W"
	mkdir -p "$W/dh0/C" "$W/dh0/S" "$W/dh0/T" "$W/dh0/Libs" "$W/shots"
	if [ "${2:-}" = wb ]; then
		rig_need WBDISK "a Workbench 3.1 + Picasso96 tree"
		# Selective copy: leave out dev kits, docs and games.
		for d in C Libs Devs S L Prefs Classes Locale Fonts System \
		    Utilities Tools WBStartup Rexxc Expansion Storage; do
			[ -d "$WBDISK/$d" ] && cp -R "$WBDISK/$d" "$W/dh0/"
		done
		cp "$WBDISK"/*.info "$W/dh0/" 2>/dev/null || true
		chmod -R u+w "$W/dh0"
	fi
	cp "$EMU"/aos/C/* "$W/dh0/C/"
	# Run and Execute for the guest watchdog (from the Workbench tree)
	for c in Run Execute; do
		[ -n "$WBDISK" ] || break
		[ -e "$W/dh0/C/$c" ] || cp "$WBDISK/C/$c" "$W/dh0/C/" 2>/dev/null || true
	done
	[ -x "$RIG/uaequit" ] || { echo "run-lib.sh: build $RIG/uaequit first (make -C $RIG)" >&2; exit 2; }
	cp "$RIG/uaequit" ${EMU_C_FILES:-} "$W/dh0/C/"
	chmod 755 "$W"/dh0/C/*
}

# emu_wbmode MODEID DEPTH: the Workbench of the run opens in MODEID (hex) with DEPTH planes
# (the mode's default size): screenmode.prefs into the tree's own Sys/ drawer (a second "sys/"
# would be copied over it); host mouse integration removed (probes inject their own input).
emu_wbmode()
{
	envsys="$W/dh0/Prefs/Env-Archive/Sys"
	[ -d "$envsys" ] || envsys="$W/dh0/Prefs/Env-Archive/sys"
	mkdir -p "$envsys"
	rm -f "$W/dh0/WBStartup/mousehack" "$W/dh0/WBStartup/timehack"
	python3 - "$envsys/screenmode.prefs" "$1" "$2" <<'EOF'
import struct, sys
def chunk(cid, body):
    return cid + struct.pack('>I', len(body)) + body + (b'\0' if len(body) & 1 else b'')
mode, depth = int(sys.argv[2], 16), int(sys.argv[3])
# width/height ~0 = the mode's default ("Default" in ScreenMode prefs)
body = b'PREF' + chunk(b'PRHD', struct.pack('>BBI', 0, 0, 0)) \
    + chunk(b'SCRM', struct.pack('>4IIHHHH', 0, 0, 0, 0, mode, 0xFFFF, 0xFFFF, depth, 1))
open(sys.argv[1], 'wb').write(chunk(b'FORM', body))
EOF
}

# emu_cleanup: kills whatever emu_run started (trap handler).
emu_cleanup()
{
	if [ -n "${EMU_PID:-}" ] && kill -0 "$EMU_PID" 2>/dev/null; then
		echo "== killing emulator $EMU_PID" >&2
		kill "$EMU_PID" 2>/dev/null
		sleep 1
		kill -9 "$EMU_PID" 2>/dev/null
	fi
	pkill -9 -f "winuae -f $W/a.uae" 2>/dev/null || true
	if [ -n "${EMU_HELPER_PID:-}" ]; then
		kill "$EMU_HELPER_PID" 2>/dev/null || true
	fi
}

# emu_script < lines: writes S/startup-sequence.  Lines starting with "!"
# run verbatim; others run with stdout appended to DH0:out.txt.  A guest
# watchdog (S:watchdog, started in the background) quits the emulator after
# EMU_WATCHDOG seconds (default EMU_TIMEOUT - 20) even if a program hangs.
emu_script()
{
	wd=${EMU_WATCHDOG:-$((${EMU_TIMEOUT:-300} - 20))}
	printf 'Wait %s\nEcho "watchdog: quitting the emulator" >>DH0:out.txt\nC:uaequit\n' "$wd" \
	    > "$W/dh0/S/watchdog"
	{
		echo "FailAt 21"
		if [ -e "$W/dh0/C/Run" ] && [ -e "$W/dh0/C/Execute" ]; then
			echo "Run >NIL: Execute S:watchdog"
		fi
		while IFS= read -r line; do
			case "$line" in
			"!"*)
				echo "${line#!}"
				;;
			*)
				# AmigaDOS escapes a quote inside a quoted string as *"
				esc=$(printf '%s' "$line" | sed 's/"/*"/g')
				printf 'Echo "%% %s" >>DH0:out.txt\n' "$esc"
				echo "$line >>DH0:out.txt"
				;;
			esac
		done
		echo "C:uaequit"
	} > "$W/dh0/S/startup-sequence"
}

# emu_config native|rtg: writes a.uae.
emu_config()
{
	{
		cat <<EOF
config_description=xenon probe ($1)
use_gui=no
kickstart_rom_file=$KICKSTART
chipset=aga
chipset_compatible=A1200
cpu_model=68020
cpu_compatible=false
cpu_24bit_addressing=false
cpu_speed=max
cachesize=0
cycle_exact=false
immediate_blits=true
warp=true
chipmem_size=4
fastmem_size=8
nr_floppies=1
floppy0type=0
filesystem2=rw,DH0:dh0:$W/dh0,10
debugging_features=gdbserver
gdb_port=$GDB_PORT
EOF
		if [ "$1" = rtg ]; then
			cat <<EOF
z3mem_size=16
gfxcard_type=ZorroIII
gfxcard_size=16
EOF
		fi
	} > "$W/a.uae"
}

# emu_run: runs the emulator (bounded), serving screenshot requests
# (dh0/shot_*.req -> shots/*.png + .ack) until it exits.
emu_run()
{
	: > "$W/dh0/out.txt"
	trap 'emu_cleanup' EXIT INT TERM HUP
	timeout -k 10 "$EMU_TIMEOUT" "$WINUAE" -f "$W/a.uae" > "$W/emu.log" 2>&1 &
	pid=$!
	EMU_PID=$pid
	while kill -0 "$pid" 2>/dev/null; do
		for req in "$W"/dh0/shot_*.req; do
			[ -e "$req" ] || continue
			name=$(basename "$req" .req)
			name=${name#shot_}
			sleep 0.5	# let the guest settle after creating the request
			python3 "$RIG/gdbshot.py" "$GDB_PORT" "$W/shots/$name.png" \
			    >> "$W/gdbshot.log" 2>&1
			rm -f "$req"
			: > "$W/dh0/shot_$name.ack"
		done
		sleep 0.2
	done
	wait "$pid"
	rc=$?
	EMU_PID=
	cp "$W/dh0/out.txt" "$W/out.txt"
	echo "== $W: emulator exit $rc (124 = timeout)"
	cat "$W/out.txt"
	ls "$W/shots" 2>/dev/null
	return 0
}
