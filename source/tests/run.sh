#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Run the conformance tests. $1 = directory with the built test binaries; in the environment
# GAME (the game below source/games), EXE (the program), PAK and VOICE_PAK (its packs, when
# built): make test sets them.
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
export GAME=${GAME:?set GAME, the game below source/games (make test does)}
rc=0
if [ -d "$VGM/music/vgm" ] && [ -d "$VGM/sfx/vgm" ]; then HAVE_VGM=1; else HAVE_VGM=; fi

run() {
    echo "== $*"
    "$@" || rc=1
}

if ! $PY -I "$HERE/../tools/gamedata.py" check -q; then
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
if [ -f "${VOICE_PAK:-}" ]; then
run $PY -I "$HERE/check_voice.py" "$BIN/test_voice" "$VOICE_PAK"
else
echo "== check_voice: skipped (no ${VOICE_PAK:-voice pack}; make voice-pack WINDAT=...)"
fi
run "$BIN/test_text"
# the engine without the game's modules, and the profile's checks (needs the built pack)
if [ -f "${PAK:-}" ] && [ -x "${EXE:-}" ]; then
run $PY -I "$HERE/check_core.py" "$EXE" "$PAK" "$BIN/tmp/core"
else
echo "== check_core: skipped (no ${PAK:-pack}; make pack)"
fi
# the engine's own shutdown: the tick limit ends the run through it (as on the machines) and the
# headless backend fails a run that leaves memory behind (lrvne/docs/porting-an-engine.md);
# the schedule goes through the game's modules, with the voice when its pack is there
if [ -f "${PAK:-}" ] && [ -x "${EXE:-}" ]; then
echo "== $(basename "$EXE") to --max-ticks: the engine frees everything it allocated"
mkdir -p "$BIN/tmp/teardown"
t=0
# shellcheck disable=SC2046
"$EXE" --pak "$PAK" $( [ -f "${VOICE_PAK:-}" ] && echo --voice-pak "$VOICE_PAK") --no-audio \
    --storage "$BIN/tmp/teardown" --autoplay ok:45 --max-ticks 6000 >/dev/null \
    2>"$BIN/tmp/teardown/log" || t=$?
if [ $t -eq 3 ]; then
echo "teardown: ended at the tick limit, nothing left"
else
echo "teardown: exit $t, not 3"; cat "$BIN/tmp/teardown/log"; rc=1
fi
fi
if [ -n "$HAVE_VGM" ]; then
run "$BIN/test_opn_split" "$VGM/music/vgm/XENON_24.vgm" 22050
run $PY -I "$HERE/check_pmd.py" "$BIN/pmd_trace" "$GAMEDIR" "$VGM/music/vgm" "$VGM/sfx/vgm"
else
echo "== test_opn_split, check_pmd: skipped (no reference recordings in $VGM: make data, with unicorn)"
fi

if [ $rc -eq 0 ]; then echo "ALL TESTS PASSED"; else echo "TESTS FAILED"; fi
exit $rc
