#!/bin/sh
# The release archive on the Amiga: unpacks each ARCHIVE (default
# build/amiga/xenon-amiga-<version>.lha, the version from engine/version.h) in WinUAE with the Workbench tree's LhA
# (2.12, 68040 build, so the emulated CPU is a 68040), lists the result,
# starts the game from the first one (AUDIO=none FAST MAXTICKS=200: the
# title must come up and the log end with "max ticks reached"), then
# compares every unpacked file byte for byte with build/amiga/dist/ on the
# host.  The archive has no pack (the game's data): the game runs with PAK,
# default build/xenon.pak, copied into the unpacked drawer, as a player
# would put it there.  Exit status 0 when all of that holds.  out.txt holds LhA's and the
# game's output.  DISTDIR = the staging directory to compare with (default build/amiga/dist).
# See README.md "The release archive".
#
#   run-dist.sh [ARCHIVE.lha ...]
set -eu
RIG=$(cd "$(dirname "$0")" && pwd)
. "$RIG/run-lib.sh"
SRC=$HERE/../..
DISTDIR=${DISTDIR:-$SRC/../build/amiga/dist}
VERSION=$(sed -n 's/^#define XE_VERSION *"\(.*\)"/\1/p' "$SRC/engine/version.h")
PAK=${PAK:-$SRC/../build/xenon.pak}
[ -f "$PAK" ] || { echo "run-dist.sh: no pack $PAK (make data SRC=..., make pack)"; exit 2; }
[ $# -gt 0 ] || set -- "$SRC/../build/amiga/xenon-amiga-$VERSION.lha"

rig_need WBDISK "a Workbench 3.1 tree with LhA 2.x in C:"
emu_setup dist
cp "$WBDISK/C/lha" "$W/dh0/C/lha"
chmod 755 "$W/dh0/C/lha"
# LhA opens mathieeedoubbas.library (a missing one is a requester = a hang here)
cp "$WBDISK"/Libs/mathieee*.library "$W/dh0/Libs/"
cp "$PAK" "$W/dh0/xenon.pak"
n=0
for a; do
	n=$((n + 1))
	cp "$a" "$W/dh0/a$n.lha"
done

{
	# LhA looks for ENV: variables; without the assign that is a "please insert
	# volume ENV" requester (seen with a gdbshot.py screenshot), a hang here
	echo "!C:MakeDir RAM:ENV"
	echo "!Assign ENV: RAM:ENV"
	echo "Version C:lha"
	i=0
	for a; do
		i=$((i + 1))
		echo "!MakeDir DH0:x$i"
		echo "!cd DH0:x$i"
		echo "C:lha x DH0:a$i.lha"
		echo "List DH0:x$i ALL"
	done
	echo "!Copy DH0:xenon.pak DH0:x1/Xenon/xenon.pak"
	echo "!cd DH0:x1/Xenon"
	echo "Version xenon"
	echo "xenon AUDIO=none FAST MAXTICKS=200 LANG=en"
} | emu_script
emu_config native
sed -i -e "s/^cpu_model=.*/cpu_model=68040/" "$W/a.uae"
emu_run

# host side: the unpacked trees against the staging directory
rc=0
i=0
for a; do
	i=$((i + 1))
	for f in Xenon.info Xenon/xenon Xenon/xenon.info Xenon/ReadMe.txt; do
		if cmp -s "$W/dh0/x$i/$f" "$DISTDIR/$f"; then
			echo "ok   $(basename "$a"): $f"
		else
			echo "FAIL $(basename "$a"): $f differs or is missing"
			rc=1
		fi
	done
done
if [ -e "$DISTDIR/Xenon/xenon.pak" ]; then
	echo "FAIL the archive contains xenon.pak"
	rc=1
else
	echo "ok   no xenon.pak in the archive"
fi
if grep -q "^xenon $VERSION " "$W/out.txt"; then
	echo "ok   Version reports xenon $VERSION"
else
	echo "FAIL Version did not report xenon $VERSION (see $W/out.txt)"
	rc=1
fi
if grep -q "max ticks reached" "$W/out.txt"; then
	echo "ok   the unpacked game ran to MAXTICKS"
else
	echo "FAIL the unpacked game did not reach MAXTICKS (see $W/out.txt)"
	rc=1
fi
exit $rc
