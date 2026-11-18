# WarpOS probes (WinUAE, Blizzard PPC)

> These probes were written while the platform layer was this engine's own (before the move to
> lrvne, `source/lrvne`); what they measured is lrvne's Amiga backend now
> (`source/lrvne/backends/amiga`, its README). Paths into the old tree (`engine/snd/opn_lite.c`,
> `config/*.mk`) name files that are in git history only: rebuild such a probe from a checkout
> of that history. The results stay valid as records.

What the PowerPC (WarpOS) build of the Amiga backend relies on, checked in WinUAE's Blizzard
PPC emulation (qemu-uae). Tested 2026-11-11 with WarpUP 5.1 (`powerpc.library` 16.1).
There is no PowerPC hardware to test on: everything here is emulator evidence, see
[Emulator limits](#emulator-limits). The runner (`run-wos.sh`) and its environment are lrvne's
rigs (`../../lrvne/rigs/README.md`); the game's WarpOS notes: `../../tests/warpos/README.md`.

## wosprobe

`wosprobe.c` (PPC) + `wosprobe68k.c` (68k, `vc +aos68k`, linked into the same executable) +
`wosprobe_nat.c` (PPC, OS headers without the alignment pragma). Output is line-buffered,
so a hang still shows how far it got. Results (`out-probe.txt`):

1. **Structure layout.** vbccppc aligns `int` and pointers to 4 bytes; the NDK and WarpOS
   headers don't ask for 68k alignment. Of 65 sizes/offsets of the structures the backend
   uses (`lrvne/backends/amiga/oslayout.h`; 74 entries with the backend's own shared structures),
   **51 differ** from the 68k compiler without
   `#pragma amiga-align` (`ln_Name` at 12 instead of 10, `struct IOStdReq` 52 bytes instead of
   48, ...), **0 differ** with the OS headers wrapped in `wos_osbegin.h`/`wos_osend.h` (now `core/os_begin.h`/`os_end.h`). The
   C library headers must stay outside the wrap (vc.lib is built with natural alignment).
2. **Tasks.** The main program's `FindTask(NULL)` is the shell's 68k process ("Initial CLI",
   4 KB 68k stack); `FindTaskPPC(NULL)` is its PPC mirror "Initial CLI_PPC1" with a **36 KB**
   PPC stack. `powerpc.library` 16.1.
3. **Run68K** works for a 68k function with register arguments and for an OS call by base +
   LVO (exec AvailMem at -216 gives the same as the stub).
4. **Variadic stubs:** `CreateNewProcTags` with a 68k entry point starts the process; it runs
   and signals the PPC parent.
5. **PPC task** (`CreateTaskPPC`, needs `TASKATTR_INHERITR2`: without it r2, the TOC pointer,
   is garbage and the task hangs on its first global): its 68k calls run in a separate 68k
   mirror task; it opened `timer.device` and did a 100 ms `TR_ADDREQUEST`. 200 signal round trips
   main <-> the task's mirror all arrived. A **68k vertical-blank interrupt server** signalling
   the task's mirror woke it 10 times through `Wait()` (the 68k stub) and 10 times through
   `WaitPPC()`: signals are shared between a PPC task and its mirror, as the docs say (V11+).
   After its last 68k call the task leaves the PPC task list at once.
6. **Cost of a 68k call from PPC: ~20 ms in this emulator** (AvailMem, ReadEClock); a signal
   round trip ~40 ms (~80 ms before the emulator's stall fixes). See below.
7. **Custom chip registers** read from PPC (`vhposr`, `intenar`, `dmaconr`) give live values.
8. **Chip RAM** written by PPC and summed by 68k code through Run68K: correct, with and
   without an explicit `SetCache(CACHE_DCACHEFLUSH)` (Run68K does "cache management
   automatically"; the emulator has no caches, so this proves nothing about real boards).
9. **Timing.**
   - The time base (`mftb`) runs at **16.3 MHz** against the E-clock (15.96 MHz before the
     emulator's stall fixes), about qemu-uae's 16.6 MHz (`TBFREQ`, bus/4 of a 66 MHz bus).
     `GetInfo`'s bus clock is WarpOS's own measurement at boot and varies here: 678 MHz in
     every run before the stall fixes (`GetSysTimePPC` 10x slow), 310 MHz in this one
     (`GetSysTimePPC` 216 ms for 1 s). The game's profile clock calibrates the time base
     against the E-clock instead (`lrvne/backends/amiga/timer.c`).
   - `DoIO(TR_ADDREQUEST 1 ms)` from PPC: 60 ms each, including two E-clock reads (20 ms
     each).
   - A 68k VERTB server signalling **the shell's own process** (the main program's 68k
     side, not a mirror WarpOS created) wakes a `WaitTime`/`WaitPPC` of the main PPC task:
     50 of 50 in 50 frames (58 before the fixes). A `Wait()` through the 68k stub: 50 in 51
     frames (64).

## Emulator limits

- **68k calls from PPC take ~one PAL frame (20 ms) each** (`ppc_cpu_idle` and `ppc_model=604e`
  make no difference). WinUAE's board interrupt path (`cpuboard_rethink`) looks immediate.
  Not confirmed: WarpOS's 68k side may be falling back to vertical-blank polling here. On
  real hardware a context switch costs microseconds. The game now makes one 68k call per
  frame plus palette loads and flips when they happen (see [The game](#the-game)); its tests
  in this emulator check function, not speed.
- **`GetSysTimePPC` is unreliable**: WarpOS derives it from the bus clock it measures at
  boot, which comes out differently from run to run here (678 MHz before the stall fixes
  made it run 10x slow; 65-310 MHz after). The time base itself is right (probe "9").
- **Stalls of 259 s, and a hang**: fixed in the emulator, see
  [The stalls](#the-stalls-fixed-in-the-emulator).
- **No caches modelled** (68k or PPC), so cache-coherency bugs between the CPUs can't show
  here. The backend flushes explicitly where PPC-written data is read by DMA or the 68k.
- **No audio DMA** in this WinUAE build (no SDL3); the Paula/AHI paths need an SDL3 + PPC build.

## The game

`printf 'DH0:xenon LANG=en MAXTICKS=40\n' | WOS_FILES="../build/warpos/xenon ../build/xenon.pak" tests/warpos/run-wos.sh game`
(paths relative to `source/`; screenshots: `AUTOPLAY="snap:2000,4000"`, into `snaps/`;
the timed `WOS_SHOTS` of these runs is gone). Results 2026-11-11:

- Starts, passes the layout check, preloads the pack, opens the PAL HiRes Laced screen
  (planar CPU copy) and draws the opening logo correctly; the PPC audio task opens Paula
  (11015 Hz) from its 68k mirror. No audio DMA in this WinUAE build: 2 blocks rendered, 0
  interrupts, and the task still ends cleanly on quit (no `am_task_wait_gone` timeout).
- **Before the call batching:** a median of 380-420 ms per frame (the stall log's first 32
  frames; about 15 68k calls at 20 ms, and the timer wait came back 180-300 ms late: its
  reply had to cross back to the PPC before the next clock read).
- **After** (`m68k/frame68k.c`, now `core/frameos.c`: one 68k call per frame for the timer wait, input and the
  memory sample; the profile clock on the time base; the audio task on `WaitPPC` and a
  PPC semaphore): **64 ms per frame** (502 frames in 32 s once the 259 s stalls are taken
  out), the timer wait about 2 ms late. Same with `AUDIO=none` and `AUDIO=paula`; a later
  run without a stall: 502 frames in 33 s, 66 ms per frame. Its startup calibration of the
  time base said 18.2 MHz, against 15.96 MHz in the probe's 1 s measurement: the 20 ms
  context switches blur a 200 ms calibration here (they take microseconds on a real board).
- **After the emulator's stall fixes**
  ([The stalls](#the-stalls-fixed-in-the-emulator)):
  - `MAXTICKS=5000`: through the opening to the title menu (drawn correctly at 60 s);
    5001 frames in 6014 ticks (56.42 Hz), **21 ms per frame**.
  - `MAXTICKS=20000` (425 s, longer than the old 259 s period): 20001 frames in
    23205 ticks, 20 ms per frame, no stall over 2.4 s.
  - In both, the game falls behind in bursts of about 1 s every 50 frames or so on the
    title screen ("other" time in the stall log; 296 frame-clock stalls over ten ticks,
    3094 ticks lost in the long run). Not investigated yet: the next thing to look at.
  - Exit as before: 0 bytes not freed, stack main 3616 / 36864, audio 1580 / 36864.
- MAXTICKS counts frames, and the quit request is honoured only where the engine polls for
  it: in the opening that was frame 502. A static logo hold looks like a hang in
  screenshots; it isn't.
- Exit: `exit 0 after 502 frames`, engine `0 bytes not freed`, stack high-water main
  3520 / 36864 (the shell's PPC stack), audio 1580 / 36864.
- `Avail`: the first `OpenLibrary("powerpc.library")` boots WarpOS, which keeps 5.9 MB of
  fast RAM. After one game run, compared with WarpOS already booted: chip -41 KB, fast -4 KB.
  Not settled whether that is first-use system caching (a first screen open) or a leak: a
  second game run in the same session would tell, but each run takes 8-10 minutes here.

## Window, scale, aspect (2026-11-12)

`run-wos.sh` takes `WOS_WB=MODEID:DEPTH` (a Workbench in that mode, for `WINDOW`),
`WOS_RTG=1` (a uaegfx Zorro III board next to the Blizzard PPC), and the 68k harness's
snapshot server answers (lrvne's `rigs/snapd.py`, for every run), so the presentation test of the
window, scale and aspect modes (`lrvne/backends/amiga/README.md` "Window, scale, aspect") runs
on the PPC build and is checked against the same reference as the 68k one:

    make TARGET=warpos lrvne-present LRVNE_PRESENT_ARGS="--configs wb-aga256"

(`--configs` names lrvne's configurations: `wb-aga256`, `wb-rtg16`, `uaegfx-x2a`, `native`, ...;
then run with `run-wos.sh` and `amiga.py check`). Results, 25 snapshots each, all PASS: `WINDOW` on an AGA
256-colour Workbench (borderless), `WINDOW ASPECT` on a 16-bit uaegfx Workbench (640x480),
`SCALE=2 ASPECT` on the own screen (a 1280x1024 board mode, 1280x960 shown), and the default
screen. One run at a time: parallel qemu-uae runs starve each other.

## The stalls (fixed in the emulator)

The 259 s stalls were two emulator bugs, both fixed on 2026-11-11 in the emulator trees
(branch `netbsd` of the user's qemu-uae, 4dbeab9a00, and WinUAE, 7ebcec83). The runs here need both fixes. Found with
`stallprobe` (the main PPC task loops on `WaitTime` and reports every wait over 1 s on the
64-bit time base), host gdb on the `winuae` process, WinUAE's gdbserver for the 68k, and
`QEMU_UAE_LOG=guest_errors,trace:ppc_decr_store,trace:ppc_decr_excp` (every DEC write
and decrementer interrupt).

**1. A lost decrementer interrupt (the 259 s stalls).** During a stall in `wosprobe`
section 5:
- both CPUs were idle: the 68k in `STOP`, the PPC in WarpOS's idle loop (MSR `POW` and
  `EE` set, no interrupt pending);
- qemu's decrementer timer was due 163 s later, at 2^32 ticks after a DEC write about
  96 s earlier, near when the stall began.

The 603's decrementer interrupts when its MSB goes from 0 to 1, and qemu models exactly
that (`PPC_DECR_UNDERFLOW_TRIGGERED`, `__cpu_ppc_store_decr` in `hw/ppc/ppc.c`). A
negative value written while DEC is negative already, a deadline that has passed after
the last interrupt was taken, therefore brings the next interrupt a full wrap later:
2^32 / 16.6 MHz = 258.7 s. The write itself wasn't caught in a trace (the traced runs
had none), but the timer's expiry time pins it down. On a board the window between
reading the time base and writing DEC is a few instructions. Here qemu's virtual clock
follows host time and keeps running while the PPC thread is held off (WinUAE's spinlock,
host scheduling).

Fix (qemu-uae): a `PPC_DECR_LATE_WRITE` timer flag, set for the UAE machine in
`uae/qemu-uae-cpu.c`, so a negative write raises the interrupt even when DEC is already
negative. It deliberately deviates from the chip.

**2. Lock starvation (a hang without end).** Seen once, at boot (with the DEC trace on),
and still hung when the host timeout killed the run after 13 minutes. Host backtraces:
- the PPC thread was blocked in `uae_ppc_io_mem_write` to `0xF60038`
  (`CSIII_REG_INT_LVL`, the board's interrupt register) waiting for WinUAE's PPC
  spinlock, holding QEMU's BQL, so QEMU's timers had stopped too;
- the 68k ran WarpOS's 68k side spinning on a mailbox byte for the PPC's reply (a
  `tst.b 6(a1)` + cache flush loop at `0x440c3e52`).

WinUAE's main thread holds that spinlock while it emulates and hands it over only when
it has time to spare: frame sync, or the sleeps in the `STOP` state. The emulation ran
at ~32 frames/s, behind real time, so it never did.

Fix (WinUAE): `uae_ppc_execute_starved()`, called once per line from
`hsync_handler_post`. A PPC that has waited 1 ms for the spinlock gets the same slice as
at frame sync (`uae_ppc_execute_quick`: release, sleep 1 ms, take it back). A first
attempt handed the lock over for one access per line (`uae_ppc_execute_check`). It made
things worse: a host context switch per line kept the 68k behind real time, so the PPC
got one access per line (a custom-chip read took two lines), and `wosprobe` took 572 s.

**After both fixes:**

| | before | after |
|---|---|---|
| `stallprobe 600` (600 s of 2 ms `WaitTime`) | hung at boot (the one run, bug 2) | 0 stalls, longest wait 48 ms |
| `wosprobe`, whole run | ~90 s, or a timeout in section 5 | 49 s, all sections |
| signal ping-pong main <-> PPC task mirror | 83 ms | 42 ms |
| 50 `WaitTime` wakes by a 68k VERTB signal | 58 frames | 50 frames |
| 68k call from PPC (`AvailMem`) | 20 ms | 19 ms (unchanged) |
| the game, opening + title | 64 ms per frame | 21 ms per frame |

`GetInfo`'s bus clock is WarpOS's own measurement at boot, and in this emulator it
depends on timing: 678 MHz in every run before the fixes; 65, 310 and 70 MHz in runs
after. `GetSysTimePPC` runs off it (216 ms per second at 310 MHz). The game's profile
clock measures the time base against the E-clock and doesn't use it.

## Next

Order of work (2026-11-11): the stalls (done, above), then the game in the emulator, then
audio (an SDL3 + PPC WinUAE build; the current one has no audio DMA).

- **The game:** the ~1 s bursts on the title screen ([The game](#the-game), "other" time
  in the stall log) are the first lead.
- **Speed against the 68k build: unknown.** The 68k build keeps full frame rate on the
  real A1200's 68040/25 (opn_lite at 11 kHz; 22 kHz is too much for it, see
  `probes/amiga/README.md`). For the PPC there is no hardware here, and the emulator can't
  answer: qemu-uae runs PPC code as fast as the host translates it (no 603e/604e clock),
  each 68k call costs ~19 ms instead of microseconds, and neither CPU's caches are
  modelled. Its 20 ms per frame says nothing about a real board. What would: the same
  title-screen run with `MAXTICKS` on a Blizzard PPC or CyberStorm PPC, compared with the
  A1200's statistics, ideally also with `RATE=22050`. Without hardware, the game's time
  base profile clock could at most give the PPC's proportions (which subsystems cost
  what), not absolute speed.
