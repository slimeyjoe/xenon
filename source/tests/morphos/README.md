# The MorphOS rig: QEMU pegasos2

`mos.sh` boots MorphOS 3.21 in QEMU's `pegasos2` machine headless and drives it through the
monitor (keys, mouse, screenshots), to run the Amiga build (a 68k AmigaOS binary) under
MorphOS's 68k emulation. What was found that way is in `../../probes/morphos/README.md`.

## Environment

Lives outside the repo in `$MOS_DIR` (`tests/rig.conf`, see `tests/rig.conf.example`, or the
environment):

| path | what |
|------|------|
| `iso/morphos-3.21.iso` | live CD from https://morphos-team.net/downloads, md5 a1c13fde4051ddd2205e1db2cc5a373c |
| `boot/boot.img` | Pegasos II boot file, `xorriso -osirrox on -indev iso/morphos-3.21.iso -extract /boot.img boot/boot.img` |
| `hd.img` | 2 GB sparse raw disk (not installed yet) |
| `out.img` | 64 MB MBR + FAT16 disk: MorphOS mounts it as `Out:`, the host reads it with `mtype -i out.img@@1M ::file` |
| `xfer*.iso` | ISO9660 transfer CD with a `Xenon/` drawer (`xorriso -as mkisofs -V NAME -R -J`) |
| `run/NAME/` | per-run monitor socket, serial log, `qemu.log`, `audio.wav` |

MorphOS runs unregistered: full function, slows down after 30 minutes until a reboot.

`mos.sh` drives it headless (no display, HMP monitor socket, a guard process kills QEMU after
the given seconds or on `stop`): `start`, `shell ISO` (boot, pass the MorphOS Boot dialog,
RAmiga+N for a Shell, copy the CD's `Xenon` drawer to `RAM:Xenon`), `run "cmd"` (type a line
via sendkey), `shot file.png` (screendump), `click X Y`, `waitpix`, `stop`. `QEMU=` picks the
binary, `VGA=` the graphics card, `QDEBUG="-d unimp,guest_errors"` logs emulator gaps to
`run/NAME/qemu.log`. Example:

    tests/morphos/mos.sh shell live $MOS_DIR/xfer2.iso 900
    tests/morphos/mos.sh run live "xenon lang=en audio=none maxticks=2400 >Out:run.txt"
    tests/morphos/mos.sh shot live /tmp/x.png; tests/morphos/mos.sh stop live

Pitfalls found on the way:
- The PS/2 mouse moves 2 px per `mouse_move` unit on ati-vga (1 px on sm501); `click` homes the
  pointer to the top-left corner first. The first click on the boot dialog often only activates
  it; `shell` clicks until the desktop colour shows.
- MorphOS does not notice a CD media change made with the monitor's `change`: boot with the
  transfer ISO attached instead.
- Typing via sendkey drops a key now and then at 50 ms per key; 90 ms works. ReadArgs keywords
  are case-insensitive, so type them in lower case (fewer shifted keys).

