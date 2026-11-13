#!/bin/sh
# Run the conformance tests. $1 = directory with the built test binaries.
# GAMEDIR = directory with the original game files (default: ../resources/original/game, which
# `make data SRC=...` fills from your copy of the game; tools/gamedata.py). The OPN and PMD
# comparisons also need the reference recordings `make data` derives with unicorn
# (VGM = ../resources, holding music/vgm and sfx/vgm); without them they are skipped.
set -u
BIN=${1:-../build/host/tests}
HERE=$(cd "$(dirname "$0")" && pwd)
GAMEDIR=${GAMEDIR:-$HERE/../../resources/original/game}
VGM=${VGM:-$HERE/../../resources}
PY=${PYTHON:-python3}
rc=0
if [ -d "$VGM/music/vgm" ] && [ -d "$VGM/sfx/vgm" ]; then HAVE_VGM=1; else HAVE_VGM=; fi

run() {
    echo "== $*"
    "$@" || rc=1
}

# the importer on synthetic disk images (needs no game data)
run $PY -I "$HERE/check_import.py" "$BIN/tmp/import"
if ! $PY -I "$HERE/../tools/gamedata.py" check -q "$GAMEDIR"; then
    echo "TESTS FAILED (no complete game in $GAMEDIR: make data SRC=...)"
    exit 1
fi

# the translations against the game, each other and the voice cues
run $PY -I "$HERE/check_text.py" "$GAMEDIR"
run $PY -I "$HERE/check_lzss.py" "$BIN/test_lzss" "$GAMEDIR"
run $PY -I "$HERE/check_gdt.py" "$BIN/test_gdt" "$GAMEDIR"
run $PY -I "$HERE/check_pack.py" "$BIN/test_pack" "$GAMEDIR" "$BIN/tmp"
run $PY -I "$HERE/check_cc.py" "$BIN/test_cc" "$GAMEDIR" "$BIN/tmp"
# SKIP_OPN=1 leaves out the opn_lite vs ymfm comparison (builds without C++, slow emulated runs)
if [ -z "$HAVE_VGM" ]; then
echo "== check_opn: skipped (no reference recordings in $VGM: make data, with unicorn)"
elif [ -z "${SKIP_OPN:-}" ]; then
run $PY -I "$HERE/check_opn.py" "$BIN/test_opn" "$VGM/music/vgm" "$VGM/sfx/vgm" "$BIN/tmp"
fi
# the voice decoder against tools/ldp.py, when a voice pack has been built (make voice-pack)
VOICE_PAK=${VOICE_PAK:-$HERE/../../build/xenon_voice.pak}
if [ -f "$VOICE_PAK" ]; then
run $PY -I "$HERE/check_voice.py" "$BIN/test_voice" "$VOICE_PAK"
else
echo "== check_voice: skipped (no $VOICE_PAK; make voice-pack WINDAT=...)"
fi
run "$BIN/test_text"
run "$BIN/test_p2c"
run "$BIN/test_scale"
if [ -n "$HAVE_VGM" ]; then
run "$BIN/test_opn_split" "$VGM/music/vgm/XENON_24.vgm" 22050
run $PY -I "$HERE/check_pmd.py" "$BIN/pmd_trace" "$GAMEDIR" "$VGM/music/vgm" "$VGM/sfx/vgm"
else
echo "== test_opn_split, check_pmd: skipped (no reference recordings in $VGM: make data, with unicorn)"
fi

if [ $rc -eq 0 ]; then echo "ALL TESTS PASSED"; else echo "TESTS FAILED"; fi
exit $rc
