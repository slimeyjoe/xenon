#!/bin/bash
# mos.sh start NAME SECONDS [extra qemu args...]  - boot pegasos2 MorphOS headless in the background
# mos.sh mon NAME "hmp command"                   - send one monitor command
# mos.sh shot NAME file.png                       - screenshot
# mos.sh stop NAME
# (more commands below: shell, run, click, waitpix). MOS_DIR = the MorphOS tree (README.md),
# from tests/rig.conf or the environment. QEMU= the QEMU binary (default qemu-system-ppc),
# VGA= the graphics device arguments (default ati-vga), QDEBUG= QEMU logging options, e.g.
# "-d unimp,guest_errors" (into run/NAME/qemu.log).
set -u
RIG_TESTS=$(cd "$(dirname "$0")/.." && pwd)
. "$RIG_TESTS/rig.sh"
rig_need MOS_DIR "the MorphOS tree: boot/boot.img, run/"
M=$MOS_DIR
cmd=$1; name=$2; shift 2
R=$M/run/$name
case $cmd in
start)
    secs=$1; shift
    mkdir -p "$R"; rm -f "$R"/mon.sock
    # the guard process kills QEMU when the time is up or when the guard itself is killed
    (
        trap 'kill $qp 2>/dev/null; sleep 1; kill -9 $qp 2>/dev/null' EXIT INT TERM
        ${QEMU:-qemu-system-ppc} -M pegasos2 -rtc base=localtime -m 1024 \
            ${VGA:--device ati-vga,guest_hwcursor=true,romfile=""} \
            -kernel $M/boot/boot.img \
            -display none -monitor unix:$R/mon.sock,server,nowait \
            -serial file:$R/serial.log ${QDEBUG:-} "$@" >"$R"/qemu.log 2>&1 &
        qp=$!
        echo $qp >"$R"/qemu.pid
        timeout "$secs" tail --pid=$qp -f /dev/null
    ) >/dev/null 2>&1 &
    echo $! >"$R"/guard.pid
    for i in $(seq 50); do [ -S "$R"/mon.sock ] && break; sleep 0.1; done
    [ -S "$R"/mon.sock ] && echo "started $name (guard $(cat "$R"/guard.pid))" || { echo "no monitor"; cat "$R"/qemu.log; }
    ;;
mon)  printf '%s\n' "$1" | socat -t 2 - unix-connect:$R/mon.sock | tr -d '\r' | grep -v '^(qemu)\|^QEMU ' ;;
shot) printf 'screendump %s -f png\n' "$1" | socat -t 2 - unix-connect:$R/mon.sock >/dev/null; sleep 0.3; ls -la "$1" | awk '{print $5, $9}' ;;
click|point)
    # absolute position on the 1920x1080 Ambient screen: home to the corner, then 2 px per unit
    x=$1; y=$2; btn=${3:-1}
    { for i in $(seq 25); do echo "mouse_move -100 -100"; done
      ux=$((x / 2)); uy=$((y / 2))
      while [ $ux -gt 0 ] || [ $uy -gt 0 ]; do
          sx=$((ux > 10 ? 10 : ux)); sy=$((uy > 10 ? 10 : uy))
          echo "mouse_move $sx $sy"; ux=$((ux - sx)); uy=$((uy - sy))
      done
      if [ $cmd = click ]; then echo "mouse_button $btn"; echo "mouse_button 0"; fi
    } | while read -r l; do printf '%s\n' "$l"; sleep 0.01; done | socat -t 1 - unix-connect:$R/mon.sock >/dev/null
    ;;
type)
    # sendkey for each character of a string (lower case, digits, a few symbols)
    s=$1
    for ((i = 0; i < ${#s}; i++)); do
        c=${s:i:1}
        case $c in
        ' ') k=spc;; '.') k=dot;; '/') k=slash;; ':') k=shift-semicolon;; '-') k=minus;; '=') k=equal;;
        '_') k=shift-minus;; '"') k=shift-apostrophe;; '>') k=shift-dot;; [A-Z]) k=shift-${c,,};; *) k=$c;;
        esac
        echo "sendkey $k"
    done | while read -r l; do printf '%s\n' "$l"; sleep 0.09; done | socat -t 1 - unix-connect:$R/mon.sock >/dev/null
    ;;
waitpix)
    # waitpix NAME TIMEOUT X Y R G B [X Y R G B ...]: until every listed pixel is within 24 of its colour
    t=$1; shift
    for i in $(seq "$t"); do
        printf 'screendump %s -f png\n' "$R/wait.png" | socat -t 2 - unix-connect:$R/mon.sock >/dev/null
        sleep 0.5
        python3 -I -c '
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert("RGB"); a = list(map(int, sys.argv[2:]))
for i in range(0, len(a), 5):
    x, y, r, g, b = a[i:i + 5]
    if x >= im.width or y >= im.height: sys.exit(1)
    c = im.getpixel((x, y))
    if max(abs(c[0] - r), abs(c[1] - g), abs(c[2] - b)) > 24: sys.exit(1)
' "$R/wait.png" "$@" 2>/dev/null && { echo "waitpix ok after ${i}s"; exit 0; }
        sleep 0.5
    done
    echo "waitpix timeout"; exit 1
    ;;
shell)
    # live CD boot with a transfer ISO ($1) and the FAT output disk, up to an Ambient Shell
    # in RAM:Xenon holding the transfer ISO's Xenon drawer. $2 = guard seconds.
    iso=$1; secs=${2:-900}
    "$0" start "$name" "$secs" -cdrom $M/iso/morphos-3.21.iso \
        -drive if=none,id=hd,format=raw,file=$M/hd.img -device ide-hd,drive=hd,bus=ide.0,unit=0 \
        -drive if=none,id=out,format=raw,file=$M/out.img -device ide-hd,drive=out,bus=ide.0,unit=1 \
        -drive if=none,id=x,format=raw,media=cdrom,readonly=on,file=$iso -device ide-cd,drive=x,bus=ide.1,unit=1 \
        -audiodev wav,id=snd0,path=$R/audio.wav -machine audiodev=snd0 || exit 1
    # MorphOS Boot dialog (grey Ambient screen, white OK button), then the blue desktop
    "$0" waitpix "$name" 150 200 200 243 243 243 960 918 250 250 250 || exit 1
    # the first click sometimes only activates the dialog window: click until the desktop shows
    for try in 1 2 3 4; do
        sleep 2; "$0" click "$name" 960 922
        "$0" waitpix "$name" 15 200 200 71 121 197 && break
    done
    [ "$try" = 4 ] && ! "$0" waitpix "$name" 1 200 200 71 121 197 && exit 1
    sleep 5; "$0" click "$name" 1600 950; sleep 2   # activate Ambient
    "$0" mon "$name" "sendkey meta_r-n" >/dev/null; sleep 4   # Ambient: New Shell (RAmiga+N)
    vol=$(isoinfo -d -i "$iso" | sed -n 's/^Volume id: //p')
    "$0" type "$name" "copy $vol:Xenon RAM:Xenon all quiet"; "$0" mon "$name" "sendkey ret" >/dev/null; sleep 6
    "$0" type "$name" "cd RAM:Xenon"; "$0" mon "$name" "sendkey ret" >/dev/null; sleep 1
    echo "shell ready in $name"
    ;;
run)
    # type one shell command line and press Return
    "$0" type "$name" "$1"; "$0" mon "$name" "sendkey ret" >/dev/null
    ;;
stop)
    printf 'quit\n' | socat -t 1 - unix-connect:$R/mon.sock >/dev/null 2>&1
    sleep 1; kill "$(cat "$R"/guard.pid)" 2>/dev/null; sleep 1
    pgrep -f "mon.sock.*$name|$R/mon.sock" >/dev/null && echo "still running!" || echo "stopped $name"
    ;;
esac
