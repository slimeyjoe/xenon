#!/usr/bin/env python3
"""pmdemu.py - run the real PMD.COM 4.7g (as shipped with Xenon) under the unicorn CPU
emulator with a fake PC-9801-26K (YM2203 / OPN) board, and record every OPN register
write as a VGM file (and optionally as a TSV log).

REQUIRES the `unicorn` Python package (tested with unicorn 2.1.4).  In this project it is
installed in a venv, e.g.
    python3 -m venv ~/.venvs/xenon-re && ~/.venvs/xenon-re/bin/pip install unicorn
    ~/.venvs/xenon-re/bin/python re-tools/pmdemu.py ...

Usage:
    pmdemu.py SONG.M -o out.vgm [--loops N] [--seconds S] [--log out.tsv]
    pmdemu.py --efc XEN_SE.EFC --se N -o se_NN.vgm [--log out.tsv]
    pmdemu.py --all-music OUTDIR [--gamedir DIR]     every *.M in the game dir
    pmdemu.py --all-se OUTDIR [--gamedir DIR]        effects 1..50 of XEN_SE.EFC
Options:
    --pmd FILE        PMD.COM to run (default resources/original/game/PMD.COM)
    --opts STR        PMD command line (default "/M10 /V0 /E4 /F /K", as XENON.BAT)
    --loops N         (with --full) record until PMD's loop counter status2 (CS:3112h)
                      reaches N (default 2)
    --full            write everything recorded (until status2 reaches --loops), VGM loop
                      point = the Timer-B tick in which status2 became 1.  Default
                      ("single"): the emulation runs until status2=2 and the VGM holds
                      intro + pass 1 + pass 2; it loops over pass 2 (status2 1->2), ending
                      just before the tick in which status2 became 2.  Pass 1 cannot be
                      used as the loop body: it follows the intro (or, for songs whose L is
                      at the start, includes the song-start init and the first Timer-B
                      period, which still runs at TB=C8h), so looping it is 2-20 ms short
                      and replays init writes; pass 2 is PMD's steady state.
    --seconds S       hard cap on simulated time (default 400; 30 for songs that have no
                      L and loop only with [ ]0 -- in practice PMD's status2 also counts
                      [ ]0 wraps, so all such Xenon songs reach status2=2 well before)
    --opna            fake a YM2608 instead (86 board: reg FFh reads 01h, ext ports
                      018Ch/018Eh); VGM then uses YM2608 (header 0x48, 7987200 Hz, cmds
                      56/57).  PMD.COM still drives it as OPN + rhythm/ADPCM regs only.
Effects (--se/--all-se): one-shot effects run until PMD clears its effect flag (311Eh)
plus 0.3 s; effects that loop forever ([ ]0 - 4 6 7 21 22 25 30 34 40 44 50, they sound
until se_stop) stop when the effect state after a Timer-A tick (part work 3594h..35EFh
without the note counter +5Ah, SSG-effect vars 0309h..0316h, the EFC buffer which holds
the loop counters) repeats an earlier state; the VGM then loops exactly that cycle.

How it works (see docs/re/pmd-sequencer.md, "VGM harness"):
  * PMD.COM is loaded as a .COM at PSP segment 1000h with an empty environment and the
    command tail above; the REAL installer (0100h -> 370Ah) runs until INT 21h AH=31h
    (TSR).  Minimal DOS: AH=09h (print, captured), 25h/35h, 30h, 49h, 4Ah, 31h, 4Ch.
  * Fake hardware: YM2203 at 0188h (address/status) / 018Ah (data).  Status bit 7 never
    busy; bits 0/1 = timer A/B overflow flags.  SSG reg 0Eh (port A) reads C0h = 26K
    jumper "INT5" -> PMD installs its IRQ handler (CS:2A71h) on vector 14h (IRQ 12) and
    unmasks bit 4 of the slave PIC (port 0Ah).  Port A460h (86-board ID) reads FFh, the
    OPN at 0088h is absent (FFh), so PMD selects "YM2203 at 0188h".  PIC ports 00h/08h/
    02h/0Ah are stubbed (IMR reads FFh, ISR reads 00h), 5Fh/71h/77h ignored.
  * The installer's CPU-speed calibration (4316h) counts PIT interrupts during a busy loop;
    we do not emulate the PIT, so at 43B7h BX is forced to [43D3h] -> wait count
    [3150h] = 1 (only affects busy-wait loops between OPN accesses).
  * Like AGS.EXE: INT 60h AH=06h -> music buffer DS:DX, the whole .M file is copied there
    (file byte 0 at DS:DX); AH=0Bh -> effect buffer, XEN_SE.EFC copied there; AH=00h starts
    the song (AH=0Ch AL=n starts an effect).
  * Time: the OPN timers are simulated from the register values: Timer A period
    72*(1024-NA)/3993600 s (NA = reg24h*4 + reg25h&3), Timer B 1152*(256-NB)/3993600 s
    (NB = reg 26h).  Reg 27h: bits 0/1 run A/B, 2/3 enable flags, 4/5 reset flags.  At each
    overflow the flag is set and, if enabled, the IRQ handler (vector 14h) is called via an
    interrupt frame; it reads the status port and acknowledges via reg 27h.  Writes made
    during a handler are stamped with the overflow time.  The timers free-run from the
    installer on; mstart/effect start re-phase both as if they had just overflowed (on
    real hardware the phase is arbitrary).  Like the chip, a new reg-26h value only takes
    effect at the next reload, so the first two Timer-B periods of a song use TB=C8h
    (the value timer_init 2214h sets) and the song tempo applies from the third.
Output:
  * VGM 1.51, YM2203 clock 3993600 (header 0x44), commands 55 aa dd, waits 61/62/63/7n at
    44100 Hz.  Writes made before mstart (installer) are emitted at t=0 first.
  * --log TSV columns (complete, unpruned): time_s, kind (I=installer, S=INT 60h call such
    as mstart, A=Timer-A IRQ, B=Timer-B IRQ, AB=both flags in one IRQ), tick number (count
    of IRQ handler invocations), reg (hex; 1xx = OPNA port 1), val (hex).
Python API:
    from pmdemu import PMDEmu
    e = PMDEmu()                      # installs PMD
    e.load_music(open('X.M','rb').read()); e.load_efc(open('XEN_SE.EFC','rb').read())
    e.mstart();  e.run(seconds=60, loops=2)      # or e.step() for one IRQ
    e.writes   -> list of (time_s, kind, tickno, reg, val)
    e.bidx[b]  -> index into e.writes where Timer-B tick b+1 starts
    e.s2_btick -> {status2 value: Timer-B tick count when it was reached}
    e.status2(), e.effect_active(), e.reg (OPN register shadow), e.int60(ax, dx, ds)
    e.read_cs(addr, n) -> bytes of PMD's resident segment (e.g. part work at 31A0h)
"""
import argparse
import os
import struct
import sys

try:
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_INSN, UC_HOOK_INTR, UC_HOOK_CODE
    from unicorn.x86_const import (UC_X86_INS_IN, UC_X86_INS_OUT, UC_X86_REG_AX, UC_X86_REG_BX,
                                   UC_X86_REG_DX, UC_X86_REG_SP, UC_X86_REG_CS, UC_X86_REG_DS,
                                   UC_X86_REG_ES, UC_X86_REG_SS, UC_X86_REG_IP,
                                   UC_X86_REG_FLAGS, UC_X86_REG_AH, UC_X86_REG_AL)
except ImportError:
    sys.exit('pmdemu.py needs the unicorn package (pip install unicorn)')

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
GAMEDIR = os.path.join(ROOT, 'resources', 'original', 'game')

CLOCK = 3993600
PSP = 0x1000                # PSP / CS segment of PMD
ENVSEG = 0x0F00
STUBSEG = 0xF000            # IRET stubs + sentinel
SENT_OFF = 0x0100           # sentinel offset in STUBSEG
OPN_ADDR, OPN_DATA = 0x188, 0x18A
IRQ_VEC = 0x14              # 26K INT5 jumper -> IRQ12 -> INT 14h


class EmuError(Exception):
    pass


class PMDEmu:
    def __init__(self, pmd_path=None, opts='/M10 /V0 /E4 /F /K', verbose=False, opna=False):
        self.verbose = verbose
        self.opna = opna
        self.reg2 = [0] * 256       # OPNA port-1 shadow
        self.addr2 = 0
        self.uc = uc = Uc(UC_ARCH_X86, UC_MODE_16)
        uc.mem_map(0, 0x110000)
        # IVT: every vector -> IRET stub at F000:n (n = vector number)
        uc.mem_write(STUBSEG * 16, b'\xCF' * 0x100)
        uc.mem_write(STUBSEG * 16 + SENT_OFF, b'\xF4' * 16)      # sentinel (hlt, never run)
        for v in range(256):
            uc.mem_write(v * 4, struct.pack('<HH', v, STUBSEG))
        # environment: empty (00 00)
        uc.mem_write(ENVSEG * 16, b'\0\0\0\0')
        # MCB before PSP: size big
        uc.mem_write((PSP - 1) * 16, b'M' + struct.pack('<HH', PSP, 0x8000))
        # PSP
        psp = bytearray(0x100)
        psp[0:2] = b'\xCD\x20'
        psp[2:4] = struct.pack('<H', 0x9000)
        psp[0x2C:0x2E] = struct.pack('<H', ENVSEG)
        tail = (' ' + opts).encode('ascii')
        psp[0x80] = len(tail)
        psp[0x81:0x81 + len(tail)] = tail
        psp[0x81 + len(tail)] = 0x0D
        uc.mem_write(PSP * 16, bytes(psp))
        pmd = open(pmd_path or os.path.join(GAMEDIR, 'PMD.COM'), 'rb').read()
        uc.mem_write(PSP * 16 + 0x100, pmd)
        # hardware state
        self.reg = [0] * 256        # OPN register shadow
        self.addr = 0
        self.status = 0             # timer flags
        self.pic = {0x02: 0xFF, 0x0A: 0xFF}
        self.clk = 0                # master clocks (3 993 600 Hz) since installation
        self.kind = 'I'
        self.tickno = 0
        self.writes = []            # (time, kind, tickno, reg, val)
        self.dosout = []
        self.unknown_ports = {}
        self.tsr = False
        self.tA_next = None
        self.tB_next = None
        self.bidx = []
        self.s2_btick = {}
        uc.hook_add(UC_HOOK_INSN, self._in, None, 1, 0, UC_X86_INS_IN)
        uc.hook_add(UC_HOOK_INSN, self._out, None, 1, 0, UC_X86_INS_OUT)
        uc.hook_add(UC_HOOK_INTR, self._intr)
        sent = STUBSEG * 16 + SENT_OFF
        uc.hook_add(UC_HOOK_CODE, self._sentinel, None, sent, sent)
        cal = PSP * 16 + 0x43B7
        uc.hook_add(UC_HOOK_CODE, self._calib, None, cal, cal)
        self._install()

    # ---------------- hooks ----------------
    def _in(self, uc, port, size, ud):
        if port == OPN_ADDR:
            return self.status & 3          # never busy
        if port == OPN_DATA:
            a = self.addr
            if a == 0x0E:
                return 0xC0                 # 26K IRQ jumper: INT5 (IRQ12)
            if a == 0xFF:
                return 0x01 if self.opna else 0x00     # YM2608 ID register
            return self.reg[a]
        if self.opna and port == 0x18C:
            return 0x00                     # OPNA extended status: idle
        if self.opna and port == 0x18E:
            return self.reg2[self.addr2]
        if port in self.pic:
            return self.pic[port]
        if port in (0x00, 0x08):
            return 0
        if port in (0x88, 0x8A, 0x8C, 0x8E, 0x18C, 0x18E, 0x288, 0x28A, 0x28C, 0x28E,
                    0x388, 0x38A, 0x38C, 0x38E, 0xA460, 0xA66E):
            return 0xFF
        self.unknown_ports[('in', port)] = self.unknown_ports.get(('in', port), 0) + 1
        return 0xFF

    def _out(self, uc, port, size, value, ud):
        value &= 0xFF
        if port == OPN_ADDR:
            self.addr = value
        elif port == OPN_DATA:
            a = self.addr
            self.reg[a] = value
            self.writes.append((self.time, self.kind, self.tickno, a, value))
            if a == 0x27:
                self._reg27(value)
            elif a in (0x24, 0x25, 0x26):
                pass                         # period taken at next (re)load/overflow
        elif self.opna and port == 0x18C:
            self.addr2 = value
        elif self.opna and port == 0x18E:
            self.reg2[self.addr2] = value
            self.writes.append((self.time, self.kind, self.tickno, 0x100 | self.addr2, value))
        elif port in self.pic:
            self.pic[port] = value
        elif port in (0x00, 0x08, 0x5F, 0x71, 0x77, 0x6E, 0xA460, 0xA66E,
                      0x88, 0x8A, 0x288, 0x28A, 0x388, 0x38A):     # probes of absent boards
            pass
        else:
            self.unknown_ports[('out', port)] = self.unknown_ports.get(('out', port), 0) + 1

    @property
    def time(self):
        return self.clk / CLOCK

    # Timer periods in integer master clocks. Both are multiples of 2304, so coincident
    # A/B overflows are exact ties (an earlier float version drifted and split them after
    # ~1400 Timer-A periods, ~26-37 s into a song, which shifted MX-mode LFO steps).
    def _periodA(self):
        na = (self.reg[0x24] << 2) | (self.reg[0x25] & 3)
        return 72 * (1024 - na)

    def _periodB(self):
        return 1152 * (256 - self.reg[0x26])

    def _reg27(self, v):
        if v & 0x10:
            self.status &= ~1
        if v & 0x20:
            self.status &= ~2
        if v & 1:
            if self.tA_next is None:
                self.tA_next = self.clk + self._periodA()
        else:
            self.tA_next = None
        if v & 2:
            if self.tB_next is None:
                self.tB_next = self.clk + self._periodB()
        else:
            self.tB_next = None

    def _intr(self, uc, intno, ud):
        if intno == 0x21:
            self._dos()
            return
        # software INT to a vector that is not our stub: emulate the call
        off, seg = struct.unpack('<HH', uc.mem_read(intno * 4, 4))
        if seg == STUBSEG:
            return
        ip = uc.reg_read(UC_X86_REG_IP)
        cs = uc.reg_read(UC_X86_REG_CS)
        fl = uc.reg_read(UC_X86_REG_FLAGS)
        sp = (uc.reg_read(UC_X86_REG_SP) - 6) & 0xFFFF
        ss = uc.reg_read(UC_X86_REG_SS)
        uc.mem_write(ss * 16 + sp, struct.pack('<HHH', ip, cs, fl))
        uc.reg_write(UC_X86_REG_SP, sp)
        uc.reg_write(UC_X86_REG_FLAGS, fl & ~0x0300)
        uc.reg_write(UC_X86_REG_CS, seg)
        uc.reg_write(UC_X86_REG_IP, off)

    def _dos(self):
        uc = self.uc
        ah = uc.reg_read(UC_X86_REG_AH)
        al = uc.reg_read(UC_X86_REG_AL)
        ds = uc.reg_read(UC_X86_REG_DS)
        dx = uc.reg_read(UC_X86_REG_DX)
        if ah == 0x09:
            p = ds * 16 + dx
            s = bytes(uc.mem_read(p, 0x400)).split(b'$')[0]
            self.dosout.append(s.decode('cp932', 'replace'))
        elif ah in (0x02, 0x06):
            self.dosout.append(chr(dx & 0xFF))
        elif ah == 0x25:
            uc.mem_write(al * 4, struct.pack('<HH', dx, ds))
        elif ah == 0x35:
            off, seg = struct.unpack('<HH', uc.mem_read(al * 4, 4))
            uc.reg_write(UC_X86_REG_BX, off)
            uc.reg_write(UC_X86_REG_ES, seg)
        elif ah == 0x30:
            uc.reg_write(UC_X86_REG_AX, 0x0005)
        elif ah in (0x49, 0x4A):
            self._clc()
        elif ah == 0x31:
            self.tsr = True
            self.tsr_paras = dx
            uc.emu_stop()
        elif ah == 0x4C:
            self.exitcode = al
            uc.emu_stop()
        else:
            raise EmuError('unhandled DOS call AH=%02X' % ah)

    def _clc(self):
        self.uc.reg_write(UC_X86_REG_FLAGS, self.uc.reg_read(UC_X86_REG_FLAGS) & ~1)

    def _sentinel(self, uc, addr, size, ud):
        uc.emu_stop()

    def _calib(self, uc, addr, size, ud):
        v = struct.unpack('<H', uc.mem_read(PSP * 16 + 0x43D3, 2))[0]
        uc.reg_write(UC_X86_REG_BX, v)

    # ---------------- execution helpers ----------------
    def _run(self, cs, ip, limit=20000000):
        uc = self.uc
        uc.reg_write(UC_X86_REG_CS, cs)
        uc.reg_write(UC_X86_REG_IP, ip)
        uc.emu_start(cs * 16 + ip, STUBSEG * 16 + SENT_OFF, count=limit)

    def _push_frame(self, flags):
        uc = self.uc
        sp = (uc.reg_read(UC_X86_REG_SP) - 6) & 0xFFFF
        ss = uc.reg_read(UC_X86_REG_SS)
        uc.mem_write(ss * 16 + sp, struct.pack('<HHH', SENT_OFF, STUBSEG, flags))
        uc.reg_write(UC_X86_REG_SP, sp)

    def _caller_stack(self):
        uc = self.uc
        uc.reg_write(UC_X86_REG_SS, 0x0080)
        uc.reg_write(UC_X86_REG_SP, 0x7F00)

    def _install(self):
        uc = self.uc
        for r in (UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS):
            uc.reg_write(r, PSP)
        uc.reg_write(UC_X86_REG_SP, 0xFFFE)
        uc.mem_write(PSP * 16 + 0xFFFE, b'\0\0')
        uc.reg_write(UC_X86_REG_FLAGS, 0x0202)
        self._run(PSP, 0x100)
        if not self.tsr:
            raise EmuError('PMD installer did not go resident: %r' % self.dosout)
        off, seg = struct.unpack('<HH', uc.mem_read(IRQ_VEC * 4, 4))
        self.irq = (seg, off)
        if self.irq[0] != PSP:
            raise EmuError('IRQ vector %02Xh not hooked (%04X:%04X)' % ((IRQ_VEC,) + self.irq))
        self.ninstall = len(self.writes)

    def int60(self, ax, dx=0, ds=None):
        """call PMD INT 60h; returns (ax, dx, ds)"""
        uc = self.uc
        self._caller_stack()
        uc.reg_write(UC_X86_REG_AX, ax)
        uc.reg_write(UC_X86_REG_DX, dx)
        uc.reg_write(UC_X86_REG_DS, ds if ds is not None else 0x0080)
        self._push_frame(0x0202)
        self._run(*self.int60_vec)
        return (uc.reg_read(UC_X86_REG_AX), uc.reg_read(UC_X86_REG_DX),
                uc.reg_read(UC_X86_REG_DS))

    @property
    def int60_vec(self):
        off, seg = struct.unpack('<HH', self.uc.mem_read(0x60 * 4, 4))
        return seg, off

    def irq_call(self):
        uc = self.uc
        self._caller_stack()
        self._push_frame(0x0202)
        uc.reg_write(UC_X86_REG_FLAGS, 0x0002)
        self._run(*self.irq)

    def read_cs(self, addr, n):
        return bytes(self.uc.mem_read(PSP * 16 + addr, n))

    def load_music(self, data):
        ax, dx, ds = self.int60(0x0600)
        self.uc.mem_write(ds * 16 + dx, data)
        self.musbuf = (ds, dx)

    def load_efc(self, data):
        ax, dx, ds = self.int60(0x0B00)
        self.uc.mem_write(ds * 16 + dx, data)
        self.efcbuf = (ds, dx)

    def _rephase(self):
        # the song/effect is assumed to start right after a timer overflow (on real
        # hardware the timers free-run since installation, so the phase is arbitrary)
        if self.tA_next is not None:
            self.tA_next = self.clk + self._periodA()
        if self.tB_next is not None:
            self.tB_next = self.clk + self._periodB()

    def mstart(self):
        self.kind = 'S'
        self.int60(0x0000)
        self._rephase()

    def se_on(self, n):
        self.kind = 'S'
        self.int60(0x0C00 | (n & 0xFF))
        self._rephase()

    def status2(self):
        return self.read_cs(0x3112, 1)[0]

    def effect_active(self):
        return self.read_cs(0x311E, 1)[0]

    def step(self):
        """advance to the next timer overflow and run the IRQ handler; returns kind"""
        cand = [t for t in (self.tA_next, self.tB_next) if t is not None]
        if not cand:
            raise EmuError('no timer running')
        t = min(cand)
        kind = ''
        if self.tA_next == t:
            self.tA_next += self._periodA()
            if self.reg[0x27] & 4:
                self.status |= 1
                kind += 'A'
        if self.tB_next == t:
            self.tB_next += self._periodB()
            if self.reg[0x27] & 8:
                self.status |= 2
                kind += 'B'
        self.clk = t
        if kind and not (self.pic[0x0A] & 0x10):
            self.tickno += 1
            self.kind = kind
            if 'B' in kind:
                self.bidx.append(len(self.writes))
            self.irq_call()
        return kind

    def run(self, seconds=400.0, loops=2, until_effect_end=False, tail=0.5):
        """Run timer interrupts.  Music: until status2 (loop counter) reaches `loops`, or
        becomes FFh (all parts ended; then `tail` more seconds), or `seconds` elapse.
        Effect (until_effect_end): until the effect flag 311Eh clears, plus `tail`.
        Sets self.s2_btick[k] = number of Timer-B ticks done when status2 became k,
        self.end_time."""
        self.bidx = []                 # bidx[b] = index into writes where B tick b starts
        self.s2_btick = {}
        end_at = None
        start = self.time
        last = self.status2()
        while self.time - start < seconds:
            self.step()
            if until_effect_end:
                if end_at is None and not self.effect_active():
                    end_at = self.time + tail
            else:
                s2 = self.status2()
                if s2 != last:
                    self.s2_btick.setdefault(s2, len(self.bidx))
                    last = s2
                if s2 == 0xFF:
                    if end_at is None:
                        end_at = self.time + tail
                elif s2 >= loops:
                    break
            if end_at is not None and self.time >= end_at:
                break
        self.end_time = self.time

    def vgm_cut(self, mode='single'):
        """Choose (writes, end_time, loop_index) for the VGM.
        'full'  : everything recorded; loop from the start of the B tick where status2
                  became 1 (exact: what follows is PMD's real second pass).
        'single': intro + pass 1 + pass 2, looping over pass 2: loop from the start of
                  the B tick where status2 became 1, end at the start of the B tick
                  where it became 2 (that tick's writes are the loop's first ones).
                  Pass 2 is used because PMD is periodic from there on; pass 1 starts
                  from the intro's (or song-start) state.
        Songs where status2 never becomes 1 (they end): no loop."""
        b1 = self.s2_btick.get(1)
        b2 = self.s2_btick.get(2)
        w = self.writes
        if b1 is None:
            return w, self.end_time, None
        i1 = self.bidx[b1 - 1]                     # B tick in which status2 became 1
        if mode == 'full' or b2 is None:
            return w, self.end_time, i1
        i2 = self.bidx[b2 - 1]                     # B tick in which status2 became 2
        return w[:i2], (w[i2][0] if i2 < len(w) else self.end_time), i1


# ---------------- VGM ----------------
def write_vgm(path, writes, end_time, loop_index=None, prune=True, opna=False):
    out = bytearray()
    cur = 0          # samples emitted
    loop_off = None

    def wait_to(t):
        nonlocal cur
        target = int(round(t * 44100))
        n = target - cur
        while n > 0:
            if n == 735:
                out.append(0x62); k = 735
            elif n == 882:
                out.append(0x63); k = 882
            elif n <= 16:
                out.append(0x70 + n - 1); k = n
            else:
                k = min(n, 0xFFFF)
                out.append(0x61); out.extend(struct.pack('<H', k))
            n -= k
            cur += k

    shadow = {}
    skip = set()
    if prune:
        # F-number pairs (A4h+ch then A0h+ch, always consecutive in PMD) that rewrite the
        # current value of that channel are dropped as a pair (the latch is not disturbed)
        cur_f = {}
        for i in range(1, len(writes)):
            r = writes[i][3]
            lo = r & 0xFF
            if (0xA0 <= lo <= 0xA2 or 0xA8 <= lo <= 0xAA) and writes[i - 1][3] == r + 4:
                v = (writes[i - 1][4], writes[i][4])
                if cur_f.get(r) == v and i - 1 != loop_index and i != loop_index:
                    skip.update((i - 1, i))
                cur_f[r] = v
            if loop_index is not None and i == loop_index:
                cur_f = {}
    for i, (t, kind, tick, reg, val) in enumerate(writes):
        if i in skip:
            continue
        if loop_index is not None and i == loop_index:
            wait_to(t)
            loop_off = len(out)
            shadow = {}              # after the loop point everything must be explicit
        if prune:
            if reg == 0x27:
                # timer load/enable/reset bits are irrelevant for playback; keep only
                # changes of the CH3 mode bits 6-7
                if 0x27 in shadow and (shadow[0x27] & 0xC0) == (val & 0xC0):
                    continue
            elif ((reg <= 0x0C or 0x30 <= (reg & 0xFF) <= 0x9F or 0xB0 <= (reg & 0xFF) <= 0xB6)
                  and shadow.get(reg) == val):
                continue              # redundant rewrite of a plain value register
            shadow[reg] = val
        wait_to(t)
        if opna:
            out += bytes((0x57 if reg & 0x100 else 0x56, reg & 0xFF, val))
        else:
            out += bytes((0x55, reg, val))
    if loop_index is not None and loop_off is None:
        loop_off = len(out)
    wait_to(end_time)
    out.append(0x66)
    hdr = bytearray(0x80)
    hdr[0:4] = b'Vgm '
    data_start = 0x80
    total = data_start + len(out)
    struct.pack_into('<I', hdr, 0x04, total - 4)
    struct.pack_into('<I', hdr, 0x08, 0x151)
    struct.pack_into('<I', hdr, 0x18, cur)
    if loop_off is not None:
        lsamp = int(round(writes[loop_index][0] * 44100)) if loop_index < len(writes) else cur
        struct.pack_into('<I', hdr, 0x1C, (data_start + loop_off) - 0x1C)
        struct.pack_into('<I', hdr, 0x20, cur - lsamp)
    struct.pack_into('<I', hdr, 0x34, data_start - 0x34)
    if opna:
        struct.pack_into('<I', hdr, 0x48, 2 * CLOCK)       # YM2608 on the 86 board: 7.9872 MHz
    else:
        struct.pack_into('<I', hdr, 0x44, CLOCK)
    with open(path, 'wb') as f:
        f.write(hdr + out)
    return total


def write_log(path, writes):
    with open(path, 'w') as f:
        f.write('time_s\tkind\ttick\treg\tval\n')
        for t, kind, tick, reg, val in writes:
            f.write('%.6f\t%s\t%d\t%02X\t%02X\n' % (t, kind, tick, reg, val))   # reg 1xx = OPNA port 1


def song_cap(data):
    """for songs that only loop via [ ]0 (no L, no end) return a shorter time cap (s)."""
    try:
        sys.path.insert(0, HERE)
        import pmdinfo
        b = data[1:]
        offs = [struct.unpack_from('<H', b, 2 * i)[0] for i in range(11)]
        hasL, inf = False, False
        for i, o in enumerate(offs[:9]):
            if b[o] == 0x80:
                continue
            w = pmdinfo.walk_part(b, o)
            hasL |= w['L'] is not None
            inf |= w['inf_loop']
        if inf and not hasL:
            return 30.0
    except Exception:
        pass
    return None


def render_song(mpath, out, loops=2, seconds=400.0, log=None, efc=None, pmd=None, opts=None,
                mode='single', opna=False):
    data = open(mpath, 'rb').read()
    e = PMDEmu(pmd, opna=opna, **({'opts': opts} if opts else {}))
    e.load_music(data)
    if efc:
        e.load_efc(open(efc, 'rb').read())
    e.nstart = len(e.writes)
    e.mstart()
    cap = song_cap(data)
    if cap:
        seconds = min(seconds, cap)
    e.run(seconds=seconds, loops=max(loops, 2) if mode == 'single' else loops)
    w, end, li = e.vgm_cut(mode)
    e.vgm_end, e.vgm_loop = end, (w[li][0] if li is not None and li < len(w) else None)
    size = write_vgm(out, w, end, li, opna=opna)
    if log:
        write_log(log, e.writes)
    return e, size


def render_se(efc, n, out, log=None, pmd=None, opts=None, seconds=120.0, opna=False):
    """One effect.  One-shot effects run until PMD clears the effect flag (+0.3 s release).
    Effects that loop forever ([ ]0; they sound until se_stop) are stopped as soon as the
    effect state after a Timer-A tick (effect part work 3594h..35EFh + the whole EFC
    buffer, which holds the loop counters, + SSG-effect state 0309h..0316h; the per-part
    note counter at +5Ah is ignored) repeats an
    earlier one; the VGM then loops over exactly that cycle."""
    data = open(efc, 'rb').read()
    e = PMDEmu(pmd, opna=opna, **({'opts': opts} if opts else {}))
    e.load_efc(data)
    e.se_on(n)
    ds, dx = e.efcbuf
    seen = {}
    e.loop_index = None
    e.bidx = []
    end_at = None
    t0 = e.time
    while e.time - t0 < seconds:
        kind = e.step()
        if 'A' not in kind:
            continue
        if not e.effect_active():
            if end_at is None:
                end_at = e.time + 0.3
            if e.time >= end_at:
                break
            continue
        pw = bytearray(e.read_cs(0x3594, 0x5C))
        pw[0x5A] = 0                 # note counter (inc per note, wraps) - not sound state
        snap = (bytes(pw) + e.read_cs(0x0309, 0x0E) +
                bytes(e.uc.mem_read(ds * 16 + dx, len(data))))
        if snap in seen:
            # the loop starts with the Timer-A tick that followed the matching snapshot,
            # not with whatever IRQ came next (a Timer-B ack in between would make every
            # loop pass up to one Timer-A period too long)
            i0, ta = seen[snap]
            tl = (ta + e._periodA()) / CLOCK
            e.loop_index = next((i for i in range(i0, len(e.writes))
                                 if e.writes[i][0] >= tl - 1e-9), len(e.writes))
            e.clk += e._periodA()
            break
        seen[snap] = (len(e.writes), e.clk)
    e.end_time = e.time
    size = write_vgm(out, e.writes, e.end_time, e.loop_index, opna=opna)
    if log:
        write_log(log, e.writes)
    return e, size


def _songline(fn, e, size):
    return '%-12s %7d bytes  vgm %7.2f s  loop@%s  status2=%02X@B%s  B-ticks=%d' % (
        fn, size, e.vgm_end, ('%.2f' % e.vgm_loop) if e.vgm_loop is not None else '-',
        e.status2(), e.s2_btick, len(e.bidx))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('song', nargs='?')
    ap.add_argument('-o', '--out')
    ap.add_argument('--loops', type=int, default=2)
    ap.add_argument('--full', action='store_true')
    ap.add_argument('--seconds', type=float, default=400.0)
    ap.add_argument('--log')
    ap.add_argument('--efc')
    ap.add_argument('--se', type=int)
    ap.add_argument('--pmd')
    ap.add_argument('--opts')
    ap.add_argument('--opna', action='store_true')
    ap.add_argument('--gamedir', default=GAMEDIR)
    ap.add_argument('--all-music')
    ap.add_argument('--all-se')
    a = ap.parse_args()
    mode = 'full' if a.full else 'single'
    if a.all_music:
        os.makedirs(a.all_music, exist_ok=True)
        for fn in sorted(os.listdir(a.gamedir)):
            if not fn.upper().endswith('.M'):
                continue
            out = os.path.join(a.all_music, os.path.splitext(fn)[0] + '.vgm')
            e, size = render_song(os.path.join(a.gamedir, fn), out, a.loops, a.seconds,
                                  pmd=a.pmd, opts=a.opts, mode=mode, opna=a.opna)
            print(_songline(fn, e, size))
        return
    if a.all_se:
        os.makedirs(a.all_se, exist_ok=True)
        efc = a.efc or os.path.join(a.gamedir, 'XEN_SE.EFC')
        for n in range(1, 51):
            out = os.path.join(a.all_se, 'se_%02d.vgm' % n)
            e, size = render_se(efc, n, out, pmd=a.pmd, opts=a.opts, opna=a.opna)
            print('se %2d %6d bytes %6.2f s loop@%s writes=%d' % (
                n, size, e.end_time, ('%.2f' % e.writes[e.loop_index][0]) if e.loop_index
                is not None else '-', len(e.writes)))
        return
    if a.se is not None:
        efc = a.efc or os.path.join(a.gamedir, 'XEN_SE.EFC')
        e, size = render_se(efc, a.se, a.out or 'se_%02d.vgm' % a.se, a.log, a.pmd, a.opts,
                            opna=a.opna)
        print('%d bytes, %.2f s' % (size, e.end_time))
        return
    if not a.song:
        ap.error('need SONG.M, --se N, --all-music or --all-se')
    e, size = render_song(a.song, a.out or os.path.splitext(os.path.basename(a.song))[0] + '.vgm',
                          a.loops, a.seconds, a.log, a.efc, a.pmd, a.opts, mode, a.opna)
    print(_songline(os.path.basename(a.song), e, size))
    if e.unknown_ports:
        print('unknown ports:', e.unknown_ports)


if __name__ == '__main__':
    main()
