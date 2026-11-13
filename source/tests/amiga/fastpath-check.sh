#!/bin/sh
# Guards the fullscreen fast path while the window / scale / aspect modes are added
# (backends/amiga/README.md "Window, scale, aspect"): the objects that draw the default
# custom screen, and the whole engine, must stay byte for byte what they were.
#
#   tests/amiga/fastpath-check.sh record      build, write ../build/<target>/fastpath.sha
#   tests/amiga/fastpath-check.sh check       build, compare with that file
#   tests/amiga/fastpath-check.sh ref REV     build git revision REV (its source/ unpacked
#                                              into ../build/fastpath-ref/REV/) and compare with this tree
#
# TARGET=amiga (default), warpos or aros. Run from source/ with the target's compiler on PATH.
# vbcc and gcc write no paths or dates into these objects, so equal sources give equal bytes
# (checked by building twice when this script was written).
set -eu
TARGET=${TARGET:-amiga}
cd "$(dirname "$0")/../.."
B=../build/$TARGET

# the protected objects, relative to the build directory
protected() {
    (cd "$1" && {
        find engine backends/common -name '*.o' 2>/dev/null
        for f in backends/amiga/chipset/upload_planar.o backends/amiga/core/upload_rtg.o \
                 backends/amiga/core/upload_chunky.o backends/amiga/core/frameos.o; do
            [ -f "$f" ] && echo "$f"
        done
    } | LC_ALL=C sort)
}

sums() {
    (cd "$1" && protected . | xargs sha256sum)
}

build() {
    make -C "$1" TARGET="$TARGET" >/dev/null
}

case "${1:-check}" in
record)
    build .
    sums "$B" > "$B/fastpath.sha"
    echo "fastpath: recorded $(wc -l < "$B/fastpath.sha") objects in $B/fastpath.sha"
    ;;
check)
    [ -f "$B/fastpath.sha" ] || { echo "fastpath: no $B/fastpath.sha (run record first)"; exit 2; }
    build .
    if (cd "$B" && sha256sum --quiet -c fastpath.sha); then
        echo "fastpath: OK, $(wc -l < "$B/fastpath.sha") objects unchanged"
    else
        echo "fastpath: CHANGED (see above)"; exit 1
    fi
    ;;
ref)
    REV=${2:?revision}
    # the revision's source/ in a directory of its own, so that its top-level build/ is too
    W=../build/fastpath-ref/$(git rev-parse --short "$REV")
    if [ ! -d "$W/source" ]; then
        mkdir -p "$W/source"
        git archive "$REV" . | tar -x -f - -C "$W/source"
    fi
    build "$W/source"
    build .
    RB=$W/build/$TARGET                             # where it builds: the top-level build/,
    [ -d "$RB" ] || RB=$W/source/build/$TARGET      # or source/build/ before 2026-11-13
    sums "$RB" > "$B/fastpath.ref"
    if (cd "$B" && sha256sum --quiet -c fastpath.ref); then
        echo "fastpath: OK, $(wc -l < "$B/fastpath.ref") objects equal to $REV"
    else
        echo "fastpath: CHANGED against $REV (see above)"; exit 1
    fi
    ;;
*)
    echo "usage: $0 record|check|ref REV" >&2; exit 2
    ;;
esac
