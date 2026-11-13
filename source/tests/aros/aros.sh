#!/bin/bash
# Runs programs in hosted AROS (linux-x86_64, ABIv11) for the Xenon port (README.md).
#
#   aros.sh hosted NAME [SECONDS] < commands
#   aros.sh qemu NAME [SECONDS] < commands
#
# Settings (tests/rig.conf or the environment, README.md): AROS_DIR, the directory with the
# unpacked releases, from which AROS_HOSTED (hosted/AROS-202609-linux-x86_64-system),
# AROS_ISO (native/AROS-202609-pc-x86_64-boot-iso/aros-pc-x86_64.iso) and AROS_RUNS (run/)
# follow unless set themselves.
# Every run gets its own copy of the hosted system ($AROS_HOSTED, ~150 MB) in
# $AROS_RUNS/NAME/AROS: AROS writes into SYS: (ENVARC:, prefs), so runs never share a tree.
# Files listed in AROS_FILES="path..." are copied to SYS:Work/ (Work: is assigned to it).
# The commands on stdin become S:User-Startup, run before Wanderer starts:
#   plain lines   run with their output appended to SYS:out.txt (= $AROS_RUNS/NAME/out.txt)
#   "!" lines     go in verbatim (without the "!")
# After the last command the run ends (a marker file tells the host; KEEP=1: it does not, for
# an interactive session with VISIBLE=1), or after SECONDS
# (default 120). Sound goes to $AROS_RUNS/NAME/audio.wav (AUDIO_OUT=host: the speakers).
# SHOTS="20 45" takes screenshots of the X display at those seconds
# (shots/tNN.png); SHOT_END=1 one more just before the end (shots/end.png).
#
# Display: by default a headless X server (xwfb-run: Xwayland on a headless weston), so
# nothing appears on the desktop; VISIBLE=1 uses the desktop's X ($DISPLAY) instead;
# NODISPLAY=1 runs without X (AROS then starts an emergency shell on the host's stdin and
# the startup sequence does not run: for console tests only, fed from stdin directly).
# The AROS process, the X server and the compositor are all gone when this returns.
#
# qemu: the native pc-x86_64 ISO ($AROS_ISO) in QEMU/KVM, headless. The files and a
# User-Startup are added to a copy of the ISO (xorriso; the live CD is SYS:, read-only), so
# output goes to RAM:out.txt and is written to the kernel debug output at the end
# (dbgtype; the kernel runs with debug=serial, KERNEL_ARGS= to change), which is serial.log in
# the run dir (out.txt is cut from it; SER: does not work while debug owns the port). VGA=vmware (default) or std; sound
# from an ES1370 to audio.wav; SHOTS/SHOT_END as above (the monitor's screendump). QEMU= the
# QEMU binary (default qemu-system-x86_64), QEMU_ARGS= more arguments for it.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
RIG_TESTS=$(cd "$HERE/.." && pwd)
. "$RIG_TESTS/rig.sh"
AROS_DIR=${AROS_DIR:-}
if [ -z "$AROS_DIR" ] && [ -z "${AROS_HOSTED:-}${AROS_ISO:-}" ]; then
    rig_need AROS_DIR "the directory with the unpacked AROS releases"
fi
AROS_HOSTED=${AROS_HOSTED:-$AROS_DIR/hosted/AROS-202609-linux-x86_64-system}
AROS_RUNS=${AROS_RUNS:-${AROS_DIR:+$AROS_DIR/run}}
[ -n "$AROS_RUNS" ] || rig_need AROS_RUNS "where the run directories go"

die() { echo "aros.sh: $*" >&2; exit 2; }

# _inner RUNDIR SECONDS: inside the X session (or without X): boot, wait, shoot, stop
inner() {
    local R=$1 secs=$2 t=0
    cd "$R/AROS" || exit 1
    # its own process group: AROSBootstrap forks, and the whole group goes at the end (a
    # global for the trap: a local would be out of scope when the EXIT trap runs)
    setsid boot/linux/AROSBootstrap >"$R/boot.log" 2>&1 &
    AROS_PID=$!
    trap 'kill -- -$AROS_PID 2>/dev/null; sleep 1; kill -9 -- -$AROS_PID 2>/dev/null' EXIT INT TERM
    local p=$AROS_PID
    while [ $t -lt "$secs" ]; do
        sleep 1
        t=$((t + 1))
        kill -0 $p 2>/dev/null || { echo "aros.sh: AROS ended by itself at ${t}s" >>"$R/run.log"; break; }
        for s in ${SHOTS:-}; do
            [ "$s" = "$t" ] && [ -n "${DISPLAY:-}" ] && import -window root "$R/shots/t$t.png" 2>>"$R/run.log"
        done
        if [ -e "$R/AROS/done.marker" ]; then
            echo "aros.sh: done at ${t}s" >>"$R/run.log"
            break
        fi
    done
    [ $t -ge "$secs" ] && echo "aros.sh: timeout at ${t}s" >>"$R/run.log"
    [ "${SHOT_END:-0}" = 1 ] && [ -n "${DISPLAY:-}" ] && import -window root "$R/shots/end.png" 2>>"$R/run.log"
}

case ${1:-} in
_inner)
    inner "$2" "$3"
    exit 0
    ;;
hosted)
    name=${2:?NAME}
    secs=${3:-120}
    [ -x "$AROS_HOSTED/boot/linux/AROSBootstrap" ] || die "no hosted AROS in $AROS_HOSTED"
    R=$AROS_RUNS/$name
    rm -rf "$R"
    mkdir -p "$R/shots"
    cp -a "$AROS_HOSTED" "$R/AROS" || die "copy failed"
    mkdir -p "$R/AROS/Work"
    for f in ${AROS_FILES:-}; do
        cp -a "$f" "$R/AROS/Work/" || die "cannot copy $f"
    done
    # S:User-Startup (replaces the copy's; the pristine tree is never touched)
    {
        echo 'Assign Work: SYS:Work'
        echo 'CD Work:'
        if [ "${NODISPLAY:-0}" != 1 ]; then
            while IFS= read -r line; do
                case $line in
                '!'*) printf '%s\n' "${line#!}" ;;
                '') ;;
                *) printf 'Echo "%% %s" >>SYS:out.txt\n%s >>SYS:out.txt\n' "${line//\"/*\"}" "$line" ;;
                esac
            done
        fi
        # KEEP=1: no end marker, the session lasts until AROS is closed or the timeout
        [ "${KEEP:-0}" = 1 ] || echo 'Echo done >SYS:done.marker'
    } >"$R/AROS/S/User-Startup"
    ln -sf AROS/out.txt "$R/out.txt"
    # hosted AHI plays through the host's ALSA (alsa.audio). By default into a private
    # PipeWire null sink (real-time pacing, nothing audible) recorded to audio.wav in the run
    # dir: ALSA reads $HOME/.asoundrc and HOME is the run dir. The sink exists only during the
    # run. AUDIO_OUT=host plays on the host's default device instead.
    sink=
    if [ "${AUDIO_OUT:-wav}" = wav ] && command -v pactl >/dev/null; then
        sink=xenon_aros_$name
        mod=$(pactl load-module module-null-sink sink_name="$sink" \
                  sink_properties=device.description="$sink") || die "cannot create a null sink"
        cat >"$R/.asoundrc" <<ASOUND
pcm.!default {
    type pipewire
    playback_node "$sink"
}
ASOUND
        export HOME=$R
        pw-record --target "$sink" -P '{ stream.capture.sink=true }' "$R/audio.wav" \
            >"$R/record.log" 2>&1 &
        rec=$!
        trap 'kill $rec 2>/dev/null; wait $rec 2>/dev/null; pactl unload-module $mod' EXIT
    fi
    if [ "${NODISPLAY:-0}" = 1 ]; then
        ( cd "$R/AROS" && env -u DISPLAY -u WAYLAND_DISPLAY timeout -k 5 "$secs" \
              boot/linux/AROSBootstrap ) 2>&1 | tee "$R/boot.log"
    elif [ "${VISIBLE:-0}" = 1 ]; then
        timeout -k 10 $((secs + 20)) "$0" _inner "$R" "$secs"
    else
        command -v xwfb-run >/dev/null || die "xwfb-run is missing (dnf install xwayland-run)"
        timeout -k 10 $((secs + 30)) xwfb-run -- "$0" _inner "$R" "$secs" \
            >>"$R/run.log" 2>&1
    fi
    cat "$R/run.log" 2>/dev/null
    echo "run dir: $R"
    ;;
qemu)
    name=${2:?NAME}
    secs=${3:-180}
    ISO=${AROS_ISO:-$AROS_DIR/native/AROS-202609-pc-x86_64-boot-iso/aros-pc-x86_64.iso}
    [ -f "$ISO" ] || die "no AROS ISO at $ISO"
    R=$AROS_RUNS/$name
    rm -rf "$R"
    mkdir -p "$R/shots" "$R/stage/Work" "$R/stage/S" "$R/stage/grub"
    for f in ${AROS_FILES:-} "$HERE/dbgtype"; do
        cp -a "$f" "$R/stage/Work/" || die "cannot copy $f"
    done
    {
        echo 'Assign Work: SYS:Work'
        echo 'CD Work:'
        while IFS= read -r line; do
            case $line in
            '!'*) printf '%s\n' "${line#!}" ;;
            '') ;;
            *) printf 'Echo "%% %s" >>RAM:out.txt\n%s >>RAM:out.txt\n' "${line//\"/*\"}" "$line" ;;
            esac
        done
        echo 'Work:dbgtype ECHO XENON-OUT-BEGIN'
        echo 'Work:dbgtype RAM:out.txt'
        echo 'Work:dbgtype ECHO XENON-OUT-END'
    } >"$R/stage/S/User-Startup"
    # the boot menu without its 5 s wait; the copy boots by BIOS El Torito only (the original's
    # EFI/GPT hybrid does not replay: its GPT partitions overlap), with the GRUB image's boot
    # info patched for the new layout
    xorriso -indev "$ISO" -osirrox on -extract /boot/grub/grub.cfg "$R/stage/grub/grub.cfg" \
        >/dev/null 2>&1 || die "cannot read grub.cfg"
    chmod u+w "$R/stage/grub/grub.cfg"
    sed -i -e 's/^set timeout=.*/set timeout=0/' \
        -e "s|^\\( *multiboot2 /boot/pc/bootstrap.gz.*\\)\$|\\1 ${KERNEL_ARGS:-debug=serial}|" \
        "$R/stage/grub/grub.cfg"
    xorriso -indev "$ISO" -outdev "$R/test.iso" -map "$R/stage/Work" /Work \
        -map "$R/stage/S/User-Startup" /S/User-Startup \
        -map "$R/stage/grub/grub.cfg" /boot/grub/grub.cfg \
        -boot_image any discard -boot_image any cat_path=/boot.catalog \
        -boot_image grub bin_path=/boot/grub/i386-pc/eltorito.img \
        -boot_image grub grub2_boot_info=on -boot_image any boot_info_table=on \
        -boot_image any load_size=2048 \
        >"$R/xorriso.log" 2>&1 || die "xorriso failed (xorriso.log)"
    (
        trap 'kill $qp 2>/dev/null; sleep 1; kill -9 $qp 2>/dev/null' EXIT INT TERM
        ${QEMU:-qemu-system-x86_64} -machine q35 -enable-kvm -cpu host -m 2048 -smp 2 \
            -cdrom "$R/test.iso" -boot d -vga "${VGA:-vmware}" -display none -no-reboot \
            -serial file:"$R/serial.log" -monitor unix:"$R/mon.sock",server,nowait \
            -audiodev wav,id=a0,path="$R/audio.wav" -device ES1370,audiodev=a0 \
            ${QEMU_ARGS:-} >"$R/qemu.log" 2>&1 &
        qp=$!
        t=0
        while [ $t -lt "$secs" ] && kill -0 $qp 2>/dev/null; do
            sleep 1
            t=$((t + 1))
            for s in ${SHOTS:-}; do
                [ "$s" = "$t" ] && printf 'screendump %s -f png\n' "$R/shots/t$t.png" | \
                    socat -t 2 - unix-connect:"$R/mon.sock" >/dev/null 2>&1
            done
            if grep -q '^XENON-OUT-END' "$R/serial.log" 2>/dev/null; then
                echo "aros.sh: done at ${t}s" >>"$R/run.log"
                break
            fi
        done
        [ $t -ge "$secs" ] && echo "aros.sh: timeout at ${t}s" >>"$R/run.log"
        [ "${SHOT_END:-0}" = 1 ] && printf 'screendump %s -f png\n' "$R/shots/end.png" | \
            socat -t 2 - unix-connect:"$R/mon.sock" >/dev/null 2>&1
        sleep 0.5
        printf 'quit\n' | socat -t 2 - unix-connect:"$R/mon.sock" >/dev/null 2>&1
        sleep 1
    )
    tr -d '\r' <"$R/serial.log" | sed -n '/^XENON-OUT-BEGIN$/,/^XENON-OUT-END$/p' | sed '1d;$d' >"$R/out.txt"
    rm -f "$R/test.iso"
    cat "$R/run.log" 2>/dev/null
    echo "run dir: $R"
    ;;
*)
    sed -n '2,35p' "$0"
    exit 2
    ;;
esac
