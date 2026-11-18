#!/bin/bash
# A real A1200 (ACA1240 68040/25, AmigaOS 3.1 with AmiTCP) on the network: telnetd on 2323 at
# $A1200, files fetched by the Amiga's wget from this machine's web server ($A1200_WWW, written
# with sudo, seen by the Amiga as $A1200_URL), the game in $DEST (Stuff:xenon). A1200,
# A1200_WWW and A1200_URL come from tests/rig.conf or the environment (README.md "The real
# A1200"). The board may be shared with other projects: it must be idle in AmigaOS.
#
#   a1200.sh install [FILE...]	put build/amiga/xenon, xenon.info, drawer.info (-> $DEST.info) and
#				build/xenon.pak (or FILEs) into $DEST
#   a1200.sh run [ARGS...]		run the game there over telnet, print its log (Ctrl-C does not
#				reach it: give MAXTICKS= or quit on the Amiga)
#   a1200.sh play-test [load|new]	a timed autoplay run with Avail FLUSH before and after:
#				load = title -> DATA LOAD menu, 2800 ticks (light title music);
#				new = NEW GAME, OK every 60 ticks, 5000 ticks (S00, the heavy
#				xenon_01 song: the case that overloaded the 68040)
#   a1200.sh sh CMD...		AmigaOS shell commands
#   a1200.sh wait [SECS]		wait until telnet answers
# TMO = seconds a command may take over telnet (default 150; run 600, play-test 400).
#
# If the Amiga's wget hangs, check that this machine's firewall lets the A1200 reach port 80.
set -u
here=$(cd "$(dirname "$0")" && pwd)
src=$(cd "$here/../.." && pwd)
RIG_TESTS=$(cd "$here/.." && pwd)
export RIG_CONF=${RIG_CONF:-$RIG_TESTS/rig.conf}
. "$RIG_TESTS/../lrvne/rigs/rig.sh"
DEST=${DEST:-Stuff:xenon}

tel() { WAIT=2 TMO=${TMO:-150} python3 -I "$here/aostelnet.py" "$A1200" 2323 "$@" | tr -d '\r'; }
up() { timeout 3 bash -c "</dev/tcp/$A1200/2323" 2>/dev/null; }
need_up() { up || { echo "a1200.sh: no telnet at $A1200 (AmigaOS not up, or busy?)" >&2; exit 1; }; }
usage() { sed -n '2,20s/^# \{0,1\}//p' "$0" >&2; exit 2; }

cmd=${1:-}; [ $# -gt 0 ] && shift
case $cmd in
install|run|play-test|sh|wait) rig_need A1200 "the A1200's network address" ;;
esac
case $cmd in
install)
	rig_need A1200_WWW "a directory this machine's web server serves"
	rig_need A1200_URL "the URL under which the A1200 sees A1200_WWW"
	need_up
	[ $# -gt 0 ] || set -- "$src/../build/amiga/xenon" "$src/../build/amiga/xenon.info" \
	    "$src/../build/amiga/drawer.info" "$src/../build/xenon.pak"
	sudo install -d -m 755 "$A1200_WWW" || exit 1
	c=("Makedir >NIL: $DEST" "cd $DEST")
	for f; do
		b=$(basename "$f")
		sudo install -m 644 "$f" "$A1200_WWW/$b" || exit 1
		command -v restorecon >/dev/null && sudo restorecon "$A1200_WWW/$b"
		# drawer.info is the icon of $DEST itself: it goes next to the drawer, as <drawer>.info
		case $b in drawer.info) t="$DEST.info";; *) t=$b;; esac
		c+=("Delete >NIL: $t" "GG:bin/wget -q -O $t ${A1200_URL%/}/$b")
		case $b in *.pak|*.info) ;; *) c+=("Protect $b +e");; esac
	done
	tel "${c[@]}" "List" >/dev/null
	tel "List $DEST" | grep -vE '^New Shell|^[0-9]+\.'
	for f; do printf '%-10s %8d bytes here\n' "$(basename "$f")" "$(stat -c %s "$f")"; done;;
run)
	need_up
	TMO=${TMO:-600} tel "cd $DEST" "xenon $*" | grep -vE '^New Shell|^[0-9]+\.[^>]*> ?$';;
play-test)
	need_up
	case ${1:-load} in
	load)	run='xenon LANG=en AUTOPLAY="at:650=down,750=ok,1000=ok" MAXTICKS=2800';;
	new)	run='xenon LANG=en AUTOPLAY="ok:60" MAXTICKS=5000';;
	*)	usage;;
	esac
	shift || true
	TMO=${TMO:-400} tel "cd $DEST" "Avail FLUSH" "$run $*" "Avail FLUSH" | grep -vE '^New Shell';;
sh)	need_up; tel "$@";;
wait)
	t0=$(date +%s)
	until up; do
		[ $(( $(date +%s) - t0 )) -ge "${1:-180}" ] && { echo "a1200.sh: no telnet" >&2; exit 1; }
		sleep 3
	done;;
*)	usage;;
esac
