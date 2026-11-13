# The WarpOS rig: WinUAE with Blizzard PPC emulation

`run-wos.sh` runs PowerPC (WarpOS) programs, the game's WarpOS build among them, in WinUAE's
Blizzard PPC emulation (qemu-uae), on the 68k rig's plumbing (`../amiga/run-lib.sh`). What the
WarpOS build was found to rely on, the emulator's limits and the game's runs are in
`../../probes/warpos/README.md`.

## Environment

| what | where |
|------|-------|
| compiler | `vc +warpos` (vbccppc + vscppc, vasmppc_std, vlink amigaehf) on `PATH` |
| WarpOS headers + runtime | `$WARPUP` (default `~/amiga-sdk/WarpUP-5.1`, from WarpUP_V51Upd.lha) |
| emulator | `$WINUAE` (default `$EMU/obj-winuae-ppc/winuae`: WinUAE with `WINUAE_UNIX_WITH_PPC_QEMU`, no SDL3: no audio DMA) + `qemu-uae.so` |
| board ROM | `$BPPC_ROM` (default `$EMU/rom/blizzardppc_060_44.71.rom`: 68060 + PPC 603e, 128 MB on the board) |

The settings come from `tests/rig.conf` (copy `tests/rig.conf.example`) or the environment;
`EMU`, `KICKSTART` and `WBDISK` are the 68k rig's (`../amiga/README.md`), whose `uaequit`
(`make -C ../amiga`) the runs use too.

`run-wos.sh NAME [wb] < commands` builds a work dir `$EMU/aos/work/xenon/wos-NAME/`
with `../amiga/run-lib.sh` (same startup-sequence conventions, guest watchdog, traps) and
adds:
- `powerpc.library`, `warp.library` and the Blizzard PPC `warpHW.library` in `Libs:`;
- `ENV:powerpc/Terminator` = 2 and `HideWarning` = 1. The Blizzard PPC flash ROM starts
  Phase5's `ppc.library` at boot and it can't be switched off, so WarpOS needs its
  "terminator" (WarpOS.guide). Without it the first `OpenLibrary("powerpc.library")` hangs;
- `uaequit db0000`: with the board's ROM at 0xf00000, WinUAE moves its boot ROM (the uaelib
  trap) to 0xdb0000 ("RTAREA located at" in `emu.log`; `RTAREA=` overrides). `uaequit` takes
  the base as an argument now (default 0xf00000 as before);
- `WOS_FILES="files..."`: copied to `DH0:`; `WOS_UAE="key=value ..."`: config overrides.

Example, in `probes/warpos/`: `make && printf 'DH0:wosprobe\n' | WOS_FILES=$PWD/wosprobe ../../tests/warpos/run-wos.sh probe`
(about 50 s, output in `probes/warpos/out-probe.txt`; it needs the emulator fixes in
`probes/warpos/README.md` "The stalls", without them give it `EMU_TIMEOUT=700`).
`printf 'DH0:stallprobe 600\n' | EMU_TIMEOUT=800 WOS_FILES=$PWD/stallprobe ../../tests/warpos/run-wos.sh stall`
checks for stalls (`stallprobe [SECONDS] [WAITUS]`, about 11 minutes).

Pitfalls:
- `run-lib.sh`'s `emu_run` returns the emulator's exit code through `wait`: a caller under
  `set -e` dies on a timeout before printing anything.
- Several qemu-uae instances at once starve each other on this 4-core host (all three
  parallel runs timed out): run them one at a time.
- `cpu_speed=real` hangs the guest before the first command completes; keep `max`.
- `pgrep -f "winuae -f"` matches the shell running it; use `pgrep -x winuae`.

