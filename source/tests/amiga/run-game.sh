#!/bin/sh
# Runs the Amiga build of the game under WinUAE (no video output) in real time, with
# scripted input, and takes screenshots through the gdbserver at fixed times.
#
#   run-game.sh [native|rtg|promote] [MHz]
#
#   native   AGA screen, no RTG (68040, cpu_speed=max unless MHz is given -> cycle exact)
#   rtg      Workbench + Picasso96 loaded, game on the native mode (BltBitMap patched)
#   promote  FAKENATIVEMODES: the game's HiRes Laced mode promoted to a uaegfx screen
#
# XENON_ARGS overrides the game's command line (XENON_ARGS2: a second run after the first).
# AHI=1 installs AHI 6 (ahi.device, paula.audio, Paula modes, prefs: unit 0 = 8-bit stereo
# 22050 Hz) from $EMU/aos/ahi/ into the guest; combine with XENON_ARGS=...AUDIO=ahi.
# Audio interrupts exist only in a WinUAE built with SDL3 audio: WINUAE=<that binary>
# (SDL_AUDIO_DRIVER=dummy is set for it, no sound device is needed). RUN=name and
# GDB_PORT=n let several runs go in parallel (default work dir game-<variant>, port 2345). (default: English, autoplay that leaves
# the title menu into a new game, exit after 2800 frames). Output: work dir
# $EMU/aos/work/xenon/game-<variant>/ with out.txt (the game's console log) and
# shots/*.png (t25: title screen, t40 and t55: in the game). Machine settings (EMU, WBDISK,
# WINUAE, KICKSTART): tests/rig.conf, README.md.
#
# Snapshot mode (plan §10, tests/e2e/amiga.py): SNAPD=1 replaces the timed screenshots with
# tests/e2e/amiga_snapd.py, which answers the game's snapshot requests (snap.c) and leaves
# screenshots, bitmaps and manifest.jsonl in <work dir>/snaps/. GUEST_FILES copies more files
# into DH0:Xenon/ (an autoplay spec for AUTOPLAY=@...), GUEST_SAVES=dir the files of dir into
# DH0:Xenon/saves/ (progress and save files a scenario starts with). AMIGA_BIN=path runs that
# executable instead of build/amiga/xenon. WARP=1 runs the emulation
# unthrottled. WB=MODEID:DEPTH starts a Workbench in that mode first (for WINDOW, the game in a
# window on it; run-lib.sh emu_wbmode). PRE_CMD is a startup-sequence line run just before the
# game (probes/amiga/keyprobe in the background: "Run >NIL: DH0:Xenon/keyprobe ...", with
# GUEST_FILES).
set -eu
variant=${1:-native}
mhz=${2:-}
RIG=$(cd "$(dirname "$0")" && pwd)
. "$RIG/run-lib.sh"
SRC=$HERE/../..
# (the AUTOPLAY value must be quoted: ReadArgs treats an unquoted '=' as a delimiter)
XENON_ARGS=${XENON_ARGS:-"LANG=en AUTOPLAY=\"at:650=down,750=ok,1000=ok\" MAXTICKS=2800"}

EMU_TIMEOUT=${EMU_TIMEOUT:-150}
emu_setup "${RUN:-game-$variant}" wb
mkdir -p "$W/dh0/Xenon"
cp "${AMIGA_BIN:-$SRC/../build/amiga/xenon}" "$W/dh0/Xenon/xenon"
cp "$SRC/../build/xenon.pak" "$W/dh0/Xenon/xenon.pak"
chmod 755 "$W/dh0/Xenon/xenon"
for f in ${GUEST_FILES:-}; do
	cp "$f" "$W/dh0/Xenon/"
done
if [ -n "${GUEST_SAVES:-}" ]; then
	mkdir -p "$W/dh0/Xenon/saves"
	cp "$GUEST_SAVES"/* "$W/dh0/Xenon/saves/" 2>/dev/null || true
fi
if [ "${AHI:-0}" = 1 ]; then
	AHIDIR=$EMU/aos/ahi
	mkdir -p "$W/dh0/Devs/AHI" "$W/dh0/Devs/AudioModes" "$W/dh0/Prefs/Env-Archive/Sys"
	cp "$AHIDIR/Devs/ahi.device" "$W/dh0/Devs/"
	cp "$AHIDIR/Devs/AHI/paula.audio" "$W/dh0/Devs/AHI/"
	cp "$AHIDIR/Devs/AudioModes/PAULA" "$W/dh0/Devs/AudioModes/"
	python3 "$HERE/mkahiprefs.py" "$W/dh0/Prefs/Env-Archive/Sys/ahi.prefs"
fi
export SDL_AUDIO_DRIVER=dummy
if [ -n "${WB:-}" ]; then
	emu_wbmode "${WB%%:*}" "${WB#*:}"
fi

case $variant in
native)
	monitors=""
	;;
promote)
	python3 "$HERE/settooltype.py" "$WBDISK/Devs/Monitors/uaegfx.info" \
	    "$W/dh0/Devs/Monitors/uaegfx.info" 'FAKENATIVEMODES=Yes'
	monitors="DEVS:Monitors/uaegfx"
	;;
rtg)
	monitors="DEVS:Monitors/PAL
DEVS:Monitors/uaegfx"
	;;
*)
	echo "unknown variant $variant" >&2
	exit 2
	;;
esac

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
	if [ -n "${WB:-}" ] && [ -z "$monitors" ]; then
		monitors="DEVS:Monitors/PAL"
	fi
	if [ -n "$monitors" ]; then
		echo "$monitors" | sed 's/^/!/'
		echo "!C:IPrefs"
	fi
	if [ -n "${WB:-}" ]; then
		echo "!C:LoadWB"
		echo "!Wait 6"
	fi
	echo "Version"
	echo "Avail"
	# PRE_CMD (see the header)
	[ -n "${PRE_CMD:-}" ] && echo "!$PRE_CMD"
	echo "DH0:Xenon/xenon $XENON_ARGS"
	echo "Avail"
	if [ -n "${XENON_ARGS2:-}" ]; then
		# a second run in the same session shows whether memory is returned
		echo "DH0:Xenon/xenon $XENON_ARGS2"
		echo "Avail"
	fi
} | emu_script

if [ "$variant" = native ]; then
	emu_config native
else
	emu_config rtg
fi
# 68040 in real time (the frame clock and the audio interrupts are what we test)
sed -i -e "s/^cpu_model=.*/cpu_model=68040/" -e "s/^warp=.*/warp=false/" "$W/a.uae"
cat >> "$W/a.uae" <<EOF3
fpu_model=68040
mmu_model=0
sound_output=interrupts
EOF3
if [ -n "$mhz" ]; then
	sed -i -e "s/^cpu_compatible=.*/cpu_compatible=true/" -e "s/^cpu_speed=.*/cpu_speed=real/" "$W/a.uae"
	cat >> "$W/a.uae" <<EOF4
cpu_cycle_exact=true
cpu_memory_cycle_exact=true
cpu_frequency=${mhz}000000
cpu_data_cache=true
EOF4
fi

if [ "${WARP:-0}" = 1 ]; then
	sed -i -e "s/^warp=.*/warp=true/" "$W/a.uae"
fi

if [ "${SNAPD:-0}" = 1 ]; then
	# snapshot requests from the guest (snap.c)
	python3 -I "$SRC/tests/e2e/amiga_snapd.py" --port "$GDB_PORT" --dir "$W/dh0/Xenon" \
	    --out "$W/snaps" > "$W/snapd.log" 2>&1 &
else
	# timed screenshots from the host while the guest runs
	(
		for t in 25 40 55; do
			sleep 15
			python3 "$HERE/gdbshot.py" "$GDB_PORT" "$W/shots/t$t.png" >> "$W/gdbshot.log" 2>&1 || true
		done
	) &
fi
EMU_HELPER_PID=$!
EMU_TIMEOUT=${EMU_TIMEOUT:-150}
emu_run
kill "$EMU_HELPER_PID" 2>/dev/null || true
grep -i -E 'Unix RTG|uaegfx|P96|exception|guru|illegal' "$W/emu.log" | head -20 || true
