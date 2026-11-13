#!/bin/bash
# Checks the AROS release archive (make TARGET=aros dist) in hosted AROS: unpacks it with the
# AROS lha (x86_64, from the 2026.09 contrib archive) into Work:x, compares every file with
# build/aros/dist on the host (SYS: is a host directory), asks Version, and runs the unpacked
# game for 600 frames from its own drawer, with PAK (default build/xenon.pak) copied in: the
# archive has no pack, the game's data. (This lha is the Unix one: it unpacks into the current
# directory.) Settings (tests/rig.conf or the environment): AROS_DIR, or CONTRIB (the contrib
# archive's tree, default $AROS_DIR/contrib/AROS-202609-any-x86_64-contrib) and AROS_RUNS
# (default $AROS_DIR/run); DISTDIR = the staging directory to compare with (default
# build/aros/dist).
#
#   run-dist.sh [ARCHIVE.lha]
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$(cd "$HERE/../.." && pwd)
RIG_TESTS=$(cd "$HERE/.." && pwd)
. "$RIG_TESTS/rig.sh"
[ -n "${CONTRIB:-}" ] && [ -n "${AROS_RUNS:-}" ] || \
    rig_need AROS_DIR "the directory with the unpacked AROS releases"
VERSION=$(sed -n 's/^#define XE_VERSION *"\(.*\)"/\1/p' "$SRC/engine/version.h")
A=${1:-$SRC/../build/aros/xenon-aros-x86_64-$VERSION.lha}
DISTDIR=${DISTDIR:-$SRC/../build/aros/dist}
CONTRIB=${CONTRIB:-$AROS_DIR/contrib/AROS-202609-any-x86_64-contrib}
LHA_AROS=$CONTRIB/Extras/Misc/aminet/C/lha
RUNS=${AROS_RUNS:-$AROS_DIR/run}
PAK=${PAK:-$SRC/../build/xenon.pak}
[ -f "$PAK" ] || { echo "run-dist.sh: no pack $PAK (make data SRC=..., make pack)"; exit 2; }
[ -f "$A" ] || { echo "run-dist.sh: no archive $A"; exit 2; }
[ -f "$LHA_AROS" ] || { echo "run-dist.sh: no AROS lha at $LHA_AROS (unpack the contrib archive)"; exit 2; }

printf '%s\n' \
    "!MakeDir Work:x" "!CD Work:x" \
    "!Work:lha xq Work:$(basename "$A") >>SYS:out.txt" \
    "Version Work:x/Xenon/xenon" \
    "!Copy Work:$(basename "$PAK") Work:x/Xenon/xenon.pak" \
    "!CD Work:x/Xenon" \
    "xenon LANG=en MAXTICKS=600 AUDIO=none" \
    | AROS_FILES="$A $LHA_AROS $PAK" "$HERE/aros.sh" hosted dist 120 >/dev/null
R=$RUNS/dist
cat "$R/out.txt"
rc=0
for f in Xenon.info Xenon/xenon Xenon/xenon.info Xenon/ReadMe.txt; do
    if cmp -s "$R/AROS/Work/x/$f" "$DISTDIR/$f"; then
        echo "ok   $(basename "$A"): $f"
    else
        echo "FAIL $(basename "$A"): $f differs or is missing"
        rc=1
    fi
done
if [ -e "$DISTDIR/Xenon/xenon.pak" ]; then echo "FAIL the archive contains xenon.pak"; rc=1
else echo "ok   no xenon.pak in the archive"; fi
if grep -q "^xenon $VERSION " "$R/out.txt"; then echo "ok   Version reports xenon $VERSION"; else echo "FAIL Version"; rc=1; fi
if grep -q "max ticks reached" "$R/out.txt" && grep -q "0 bytes not freed" "$R/out.txt"; then
    echo "ok   the unpacked game ran to MAXTICKS and freed everything"
else
    echo "FAIL the unpacked game did not run to MAXTICKS cleanly"
    rc=1
fi
exit $rc
