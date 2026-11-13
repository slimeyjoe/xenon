# The AROS rig (x86_64, ABIv11): hosted AROS and QEMU

How the AROS build (`make TARGET=aros`) is run and checked: `aros.sh` runs programs in
hosted AROS or in the native live CD under QEMU/KVM (`make TARGET=aros e2e` drives it through
`tests/e2e/aros.py`), `run-dist.sh` checks the release archive. `make` here builds `dbgtype`,
which `aros.sh qemu` needs. Machine settings: `AROS_DIR` in `tests/rig.conf` (copy
`tests/rig.conf.example`) or the environment. What the AROS build was found to rely on is in
`../../probes/aros/README.md`.

## Environment

All under `$AROS_DIR` (`tests/rig.conf`, or the environment): `hosted/`, `native/` and
`contrib/` hold the unpacked releases named below, `run/` the run directories (`AROS_HOSTED`,
`AROS_ISO`, `AROS_RUNS`, `CONTRIB` override single paths; `RIG_CONF=path` reads another config
file). For building: `x86_64-aros-gcc` on `PATH` and
`AROS_SDK` = the SDK sysroot.

* **Which AROS.** deadwood's ABIv11 line (github.com/deadwood2/AROS, release 2026.09, the
  base of AROS One 1.3) is the x86 AROS people run in 2026. The official SourceForge
  "ABI-v1" nightlies are experimental and binary-incompatible with it; i386 ABIv0 is legacy.
  Only x86_64 ABIv11 is targeted; the code is 64-bit clean, so i386 would be one more config.
* **Compiler.** vbcc has no AROS x86 target, so AROS uses gcc: `x86_64-aros-gcc` 10.5, built
  from the source tree with `AROS/scripts/rebuild.sh` option 1 (`toolchain-core-x86_64`,
  about 20 minutes; Fedora needs `netpbm-progs libXxf86vm-devel sdl12-compat-devel yasm`).
  The sysroot is the 2026.09 SDK from axrt.org (`SDK-202609-any-x86_64.zip`: headers,
  `libamiga`, `libautoinit`, startup objects), so the AROS core itself is not built.
  `-std=gnu99`, not `c99`: the AROS headers use the `asm` keyword. `-Wno-pointer-sign`:
  `STRPTR` is `unsigned char *`. Executables are relocatable ELF objects.
* **Hosted AROS** (`AROS-202609-linux-x86_64-system.tar.gz`) is the test machine: one Linux
  process (needs only libc; X11 at run time), SYS: is a host directory. Without X it starts
  an emergency shell on stdin (no startup sequence; it eats the first character of the
  first line and loops at end of input). With X it boots to Wanderer in a few seconds.
* **Native**: the live CD `AROS-202609-pc-x86_64-boot-iso.zip` in QEMU/KVM (`aros.sh qemu`,
  below). No installation: SYS: is the CD.
* **libvirt** is not used: a plain QEMU command line gives everything a test needs (monitor
  socket for screenshots and quit, serial log, WAV audio) without a daemon or XML.

## aros.sh

    printf 'Work:xenon LANG=en MAXTICKS=3000\n' | \
        AROS_FILES="../build/aros/xenon ../build/xenon.pak" SHOTS="20 40" tests/aros/aros.sh hosted NAME 120

Each run copies the hosted system to `$AROS_RUNS/NAME/AROS` (AROS writes into SYS:), puts the
files in `Work:`, turns stdin into `S:User-Startup` (output appended to `out.txt`), boots under
a headless X server (`xwfb-run`, Xwayland on a headless weston; `VISIBLE=1` uses the desktop
instead) and ends when the commands are done or at the timeout. Screenshots with ImageMagick
`import` (`shots/tNN.png`). Sound: AHI's hosted driver plays through ALSA; the run's HOME is
its directory, whose `.asoundrc` sends ALSA to a private PipeWire null sink that exists only
for the run (real-time pacing, nothing audible), recorded with `pw-record` to `audio.wav`
(`AUDIO_OUT=host` for the speakers). An ALSA `null`/`file` device does not work: it has no
clock, the driver writes as fast as it can (6.7 GB in a minute) and AHI hangs.
No AROS process, X server, compositor or sink is left after a run.

An interactive desktop with the game installed as the archive unpacks (start it from its icon):

    printf '!Copy Work:Xenon SYS:Xenon ALL CLONE QUIET\n!Copy Work:Xenon.info SYS: CLONE QUIET\n' | \
        KEEP=1 VISIBLE=1 AUDIO_OUT=host AROS_FILES="../build/aros/dist/Xenon ../build/aros/dist/Xenon.info" \
        tests/aros/aros.sh hosted desk 3600

(`make TARGET=aros dist` first.) Tried 2026-11-11: the drawer and the XENON icon show in
Wanderer, a double click starts the game with the icon's tool types (LANG=pl changed in the
Information window took effect), sound on the host's speakers. A screenshot of the window from
the host: `DISPLAY=:0 xwininfo -root -tree | grep AROS`, then `import -window <id> shot.png`.

    printf 'Work:xenon LANG=en MAXTICKS=2500\n' | VGA=std \
        AROS_FILES="../build/aros/xenon ../build/xenon.pak" SHOTS="20" tests/aros/aros.sh qemu NAME 90

`qemu` adds the files and the User-Startup to a copy of the ISO with xorriso. Only the BIOS
El Torito image is set up again (`-boot_image grub ... grub2_boot_info=on boot_info_table=on`):
replaying the original's EFI/GPT hybrid fails (its GPT partitions overlap), and keeping the
boot image unpatched hangs at "Booting from DVD/CD". The grub menu loses its 5 s wait, the
kernel gets `debug=serial`. The CD is read-only, so the commands' output goes to RAM: and
leaves through `dbgtype` (this directory: a file to `RawPutChar`, the kernel debug channel =
the serial port = `serial.log`); `SER:` gives nothing while debug owns the port. Then
q35 + KVM, `-vga vmware` (`VGA=std` for VESA), ES1370 into `audio.wav`, screenshots by the
monitor's `screendump`, quit by the monitor; a guard kills QEMU at the timeout.

## The release archive (run-dist.sh, 2026-11-11)

`make TARGET=aros dist` packs `build/aros/dist` (as for OS3: `Xenon.info`, `Xenon/` with the
stripped executable, its icon and the AROS `ReadMe.txt`; the pack was in it until 2026-11-13) into
`build/aros/xenon-aros-x86_64-<version>.lha` and writes the upload description
`xenon-aros-x86_64-<version>.readme` next to it. The core system has no unpacker; the
2026.09 contrib archive has an x86_64 `lha` (the Unix port: it unpacks into the current
directory) and Unarc/xadUnFile. `run-dist.sh` unpacks the archive with that lha in hosted
AROS, compares the files on the host, runs `Version` and the unpacked game (with `build/xenon.pak`
copied in): all ok.

