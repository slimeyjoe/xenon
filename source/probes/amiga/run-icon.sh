#!/bin/sh
# The Workbench icon in WinUAE: iconprobe reads xenon.info (default
# build/amiga/xenon.info, or $ICON) with icon.library 40 as Workbench 3.1
# does, lists its tool types and shows it on a Workbench 3.1 screen of
# DEPTH (default 3 = the 8-colour default palette; 2 = the 4-colour one)
# planes with the OS's default palette (the tree's Palette prefs are left out): shots/icon_1.png holds the icon 1:1, complemented and enlarged in
# a window and, drawn by Workbench itself, as an AppIcon on the backdrop.
# out.txt has the tool types.  See probes/amiga/README.md "The Workbench
# icon".
set -eu
depth=${DEPTH:-3}
RIG=$(cd "$(dirname "$0")/../../lrvne/rigs/winuae" && pwd)
. "$RIG/run-lib.sh"
emu_setup "icon-$depth" wb
cp "$HERE/iconprobe" "$W/dh0/C/"
cp "${ICON:-$HERE/../../../build/amiga/xenon.info}" "$W/dh0/xenon.info"
: > "$W/dh0/xenon"
rm -f "$W/dh0/WBStartup/mousehack" "$W/dh0/WBStartup/timehack"

# ScreenMode prefs: Workbench on 0x8004 (HiRes Laced, 640x400) with $depth planes
# (into the tree's own Sys/ drawer: a second "sys/" would be copied over it)
envsys="$W/dh0/Prefs/Env-Archive/Sys"
[ -d "$envsys" ] || envsys="$W/dh0/Prefs/Env-Archive/sys"
mkdir -p "$envsys"
# the tree's Palette prefs (red/green/blue/orange pens 4-7) would hide the OS defaults
rm -f "$envsys/palette.prefs"
python3 - "$envsys/screenmode.prefs" "$depth" <<'EOF'
import struct, sys
def chunk(cid, body):
    return cid + struct.pack('>I', len(body)) + body + (b'\0' if len(body) & 1 else b'')
body = b'PREF' + chunk(b'PRHD', struct.pack('>BBI', 0, 0, 0)) \
    + chunk(b'SCRM', struct.pack('>4IIHHHH', 0, 0, 0, 0, 0x8004, 640, 400, int(sys.argv[2]), 0))
open(sys.argv[1], 'wb').write(chunk(b'FORM', body))
EOF

{
	cat <<EOF
!C:SetPatch QUIET
!C:MakeDir RAM:T RAM:ENV RAM:ENV/Sys RAM:Clipboards
!C:Copy >NIL: ENVARC: RAM:ENV ALL NOREQ
!Assign ENV: RAM:ENV
!Assign T: RAM:T
!Assign CLIPS: RAM:Clipboards
!Assign LIBS: SYS:Classes ADD
!C:IPrefs
!C:LoadWB
!Wait 10
!cd DH0:
EOF
	echo "C:iconprobe"
} | emu_script
emu_config native
emu_run
