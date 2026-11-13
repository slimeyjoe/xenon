"""pmdcore_emu.py - minimal PC-98 + OPN(A) environment that runs PMD.COM 4.7g's installer,
INT 60h API and FM timer IRQ handler under unicorn (used to verify docs/re/pmd-driver.md).

Usage: <python with unicorn> -I pmdcore_emu.py PMD.COM [install|api|song|efc|fade|keys|ff]
  install  run the installer with Xenon's options (+ error cases), print messages, resident size,
           buffers, vectors, PIC set-up and every OPN register write
  api      call the simple INT 60h getters and print AX/DX/DS
  song     load XENON_01.M, AH=00h, fire 6 Timer-B IRQs, print register writes
  efc      load XEN_SE.EFC, AH=0Ch AL=1 then Timer-A IRQs until the effect ends; SSG effect 0
  fade     AH=02h AL=20, count Timer-A IRQs until fade volume = FFh
  keys     CTRL+ESC stop and CTRL+GRPH fast-forward via the BIOS key map at 0:052A/0:0538
  ff       AH=1Ch DX=3 (skip to measure 3)
  ssgtable dump the 40 built-in SSG effects (table 2CE9h, 11-byte frames) - no emulation
The OPN model only latches registers, answers the busy/status bits, reg FFh (ID), reg 0Eh (IRQ
jumper, INT5) and clears timer flags on reg 27h writes; there is no sound synthesis.
The wait-loop calibration at 4316h (needs PIT IRQs) is short-circuited (30 ns/loop).
"""
import os, sys, struct
from unicorn import *
from unicorn.x86_const import *

PSP = 0x1000
ENV = 0x0F00
STUB = 0x0800          # segment with little call stubs


class OPN:
    def __init__(self, base=0x188, opna=True, jumper=0xC0):
        self.base = base; self.opna = opna; self.jumper = jumper
        self.addr = 0; self.eaddr = 0
        self.regs = [0] * 256; self.eregs = [0] * 256
        self.flags = 0
        self.log = []          # (port-pair, reg, val)
        self.regs[0x0e] = jumper

    def ports(self):
        b = self.base
        return {b: 'a', b + 2: 'd', b + 4: 'ea', b + 6: 'ed'}

    def inp(self, kind):
        if kind == 'a':
            return self.flags & 3
        if kind == 'd':
            if self.addr == 0xff:
                return 1 if self.opna else 0
            if self.addr == 0x0e:
                return self.jumper | 0x3f
            return self.regs[self.addr]
        if kind == 'ea':
            return 0 if self.opna else 0xff
        if kind == 'ed':
            return 0 if self.opna else 0xff

    def out(self, kind, v):
        if kind == 'a':
            self.addr = v
        elif kind == 'd':
            self.regs[self.addr] = v
            self.log.append(('M', self.addr, v))
            if self.addr == 0x27:
                if v & 0x10: self.flags &= ~1
                if v & 0x20: self.flags &= ~2
        elif kind == 'ea':
            self.eaddr = v
        elif kind == 'ed':
            self.eregs[self.eaddr] = v
            self.log.append(('E', self.eaddr, v))


class Machine:
    def __init__(self, com, args=b'', opn=None, pps=False):
        self.mu = mu = Uc(UC_ARCH_X86, UC_MODE_16)
        mu.mem_map(0, 0x100000)
        self.opn = opn or OPN()
        self.out_text = []
        self.pic = {0x02: 0xff, 0x0a: 0xff}
        self.picw = []
        self.done = None
        # PSP
        psp = bytearray(256)
        psp[0x2c:0x2e] = struct.pack('<H', ENV)
        psp[0x80] = len(args)
        psp[0x81:0x81 + len(args) + 1] = args + b'\r'
        mu.mem_write(PSP * 16, bytes(psp))
        mu.mem_write(PSP * 16 + 0x100, com)
        # MCB before PSP: size word at +3
        mu.mem_write((PSP - 1) * 16, b'M' + struct.pack('<HH', PSP, 0x8000))
        mu.mem_write(ENV * 16, b'PATH=A:\\\x00\x00')
        if pps:   # fake PPSDRV header: INT 64h -> 0700:0000 with 'PMP' at +2
            mu.mem_write(0x7000, b'\xcf\x90PMP')
            mu.mem_write(0x64 * 4, struct.pack('<HH', 0, 0x0700))
        mu.hook_add(UC_HOOK_INSN, self.hin, None, 1, 0, UC_X86_INS_IN)
        mu.hook_add(UC_HOOK_INSN, self.hout, None, 1, 0, UC_X86_INS_OUT)
        mu.hook_add(UC_HOOK_INTR, self.hint)
        mu.hook_add(UC_HOOK_CODE, self.hcode, None, PSP * 16 + 0x4316, PSP * 16 + 0x4316)
        self.calib = (30, None)  # ns per loop

    # ---- I/O
    def hin(self, mu, port, size, ud):
        p = self.opn.ports()
        if port in p:
            return self.opn.inp(p[port])
        if port in self.pic:
            return self.pic[port]
        return 0xff

    def hout(self, mu, port, size, val, ud):
        p = self.opn.ports()
        if port in p:
            self.opn.out(p[port], val & 0xff)
        elif port in self.pic:
            self.pic[port] = val & 0xff
            self.picw.append((port, val & 0xff))
        elif port in (0x00, 0x08):
            self.picw.append((port, val & 0xff))

    # ---- skip timing calibration (needs PIT IRQ)
    def hcode(self, mu, addr, size, ud):
        es = mu.reg_read(UC_X86_REG_ES)
        thr = 0x850 if mu.mem_read(es * 16 + 0x30c8, 1)[0] == 1 else 0x10a1
        ns = self.calib[0]
        cx = max(1, -(-thr // ns))
        mu.reg_write(UC_X86_REG_AX, ns)
        mu.reg_write(UC_X86_REG_CX, cx)
        sp = mu.reg_read(UC_X86_REG_SP); ss = mu.reg_read(UC_X86_REG_SS)
        ret = struct.unpack('<H', mu.mem_read(ss * 16 + sp, 2))[0]
        mu.reg_write(UC_X86_REG_SP, sp + 2)
        mu.reg_write(UC_X86_REG_IP, ret)

    # ---- interrupts
    def push(self, v):
        sp = (self.mu.reg_read(UC_X86_REG_SP) - 2) & 0xffff
        self.mu.reg_write(UC_X86_REG_SP, sp)
        self.mu.mem_write(self.mu.reg_read(UC_X86_REG_SS) * 16 + sp, struct.pack('<H', v))

    def hint(self, mu, intno, ud):
        ah = mu.reg_read(UC_X86_REG_AH)
        if intno == 0x21:
            ds = mu.reg_read(UC_X86_REG_DS); dx = mu.reg_read(UC_X86_REG_DX)
            if ah == 0x09:
                a = ds * 16 + dx; s = bytearray()
                while True:
                    c = mu.mem_read(a, 1)[0]
                    if c == 0x24: break
                    s.append(c); a += 1
                self.out_text.append(s.decode('cp932', 'replace'))
            elif ah == 0x02:
                self.out_text.append(chr(mu.reg_read(UC_X86_REG_DL)))
            elif ah == 0x49:
                fl = mu.reg_read(UC_X86_REG_EFLAGS); mu.reg_write(UC_X86_REG_EFLAGS, fl & ~1)
            elif ah in (0x31, 0x4c):
                self.done = (ah, mu.reg_read(UC_X86_REG_AL), dx)
                mu.emu_stop()
            else:
                raise Exception('int21 %02x' % ah)
            return
        # dispatch through IVT
        off, seg = struct.unpack('<HH', mu.mem_read(intno * 4, 4))
        if seg == 0 and off == 0:
            return
        ip = mu.reg_read(UC_X86_REG_IP)
        self.push(mu.reg_read(UC_X86_REG_EFLAGS) & 0xffff)
        self.push(mu.reg_read(UC_X86_REG_CS))
        self.push(ip)
        mu.reg_write(UC_X86_REG_EFLAGS, mu.reg_read(UC_X86_REG_EFLAGS) & ~0x200)
        mu.reg_write(UC_X86_REG_CS, seg)
        mu.reg_write(UC_X86_REG_IP, off)

    # ---- run helpers
    def install(self):
        mu = self.mu
        for r in (UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS):
            mu.reg_write(r, PSP)
        mu.reg_write(UC_X86_REG_SP, 0xfffe)
        mu.mem_write(PSP * 16 + 0xfffe, b'\0\0')
        mu.emu_start(PSP * 16 + 0x100, 0, count=50_000_000)
        return self.done

    def call_far(self, seg, off, regs=None, iret=True, count=20_000_000):
        """Call seg:off as an interrupt handler (iret) from a stub that ends in hlt."""
        mu = self.mu
        stub = STUB * 16
        mu.mem_write(stub, b'\xf4')     # hlt = return target
        for r, v in (regs or {}).items():
            mu.reg_write(r, v)
        mu.reg_write(UC_X86_REG_SS, STUB); mu.reg_write(UC_X86_REG_SP, 0xff00)
        self.push(0x0202)  # flags IF=1
        self.push(STUB); self.push(0)
        mu.reg_write(UC_X86_REG_CS, seg)
        mu.emu_start(seg * 16 + off, stub, count=count)

    def int60(self, ah, al=0, dx=0, ds=None):
        mu = self.mu
        off, seg = struct.unpack('<HH', mu.mem_read(0x180, 4))
        regs = {UC_X86_REG_AX: (ah << 8) | al, UC_X86_REG_DX: dx, UC_X86_REG_DS: ds if ds is not None else STUB}
        self.call_far(seg, off, regs)
        return dict(ax=mu.reg_read(UC_X86_REG_AX), dx=mu.reg_read(UC_X86_REG_DX), ds=mu.reg_read(UC_X86_REG_DS))

    def irq(self, flags):
        mu = self.mu
        vecaddr = struct.unpack('<H', mu.mem_read(PSP * 16 + 0x113, 2))[0]
        off, seg = struct.unpack('<HH', mu.mem_read(vecaddr, 4))
        self.opn.flags |= flags
        self.call_far(seg, off, {})

    def rb(self, a, n=1):
        return bytes(self.mu.mem_read(PSP * 16 + a, n))

    def rw(self, a):
        return struct.unpack('<H', self.rb(a, 2))[0]


def fmt(log):
    return ' '.join('%s%02X=%02X' % (p if p == 'E' else '', r, v) for p, r, v in log)


def main():
    com = open(sys.argv[1], 'rb').read()
    sc = sys.argv[2] if len(sys.argv) > 2 else 'install'
    if sc == 'ssgtable':
        B = 0x2ce9
        rb = lambda a: com[a - 0x100]
        for i in range(40):
            off = rb(B + 3 * i + 1) | rb(B + 3 * i + 2) << 8
            a = B + off; fr = []
            while rb(a) != 0xff:
                f = com[a - 0x100:a - 0x100 + 11]; a += 11
                d = f[9] - 256 if f[9] > 127 else f[9]
                fr.append('dur=%d %s%s tone=%03X noise=%02X vol=%02X env=%04X shape=%X dtone=%+d nsweep=%02X' % (
                    f[0], 'T' if not f[4] & 1 else '-', 'N' if not f[4] & 8 else '-', f[1] | f[2] << 8, f[3],
                    f[5], f[6] | f[7] << 8, f[8], d, f[10]))
            print('%2d pri=%d @%04X ticks=%d' % (i, rb(B + 3 * i), B + off, sum(int(x.split()[0][4:]) for x in fr)))
            for x in fr: print('     ' + x)
        return
    if sc == 'install':
        for args, opna, base in ((b' /M10 /V0 /E4 /F /K', True, 0x188), (b' /K /M10 /V0 /F', False, 0x188),
                                 (b'', True, 0x188), (b' /M7', True, 0x188), (b' /M20 /V20 /E1', True, 0x188),
                                 (b' /H', True, 0x188)):
            m = Machine(com, args, OPN(base=base, opna=opna))
            d = m.install()
            print('==== args=%r opna=%s -> %s' % (args, opna, d))
            print(''.join(m.out_text))
            if d and d[0] == 0x31:
                print('resident paragraphs DX=%04X (%d bytes)' % (d[2], d[2] * 16))
                print('bufs: mus=%04X voice=%04X efc=%04X ports=%04X/%04X wait=%d ns/loop=%d' % (
                    m.rw(0x30ec), m.rw(0x30ee), m.rw(0x30f0), m.rw(0x30f2), m.rw(0x30f4), m.rw(0x3150), m.rw(0x3152)))
                print('INT60=%s FMvec@%04X=%s irq=%d imr/pic writes=%s' % (
                    m.rb(0x180 - PSP * 16, 0) or struct.unpack('<HH', m.mu.mem_read(0x180, 4)),
                    m.rw(0x113), struct.unpack('<HH', m.mu.mem_read(m.rw(0x113), 4)), m.rb(0x115)[0], m.picw))
                print('PIC vars: cmd=%04X eoi=%02X imr=%04X mask=%02X' % (m.rw(0x30bb), m.rb(0x30bd)[0], m.rw(0x30be), m.rb(0x30c0)[0]))
                print('OPN writes:', fmt(m.opn.log))
                print('PSP cmdline:', m.rb(0x80 - 0, 16))
    elif sc == 'api':
        m = Machine(com, b' /M10 /V0 /E4 /F /K')
        m.install()
        m.opn.log.clear()
        for ah in (0x09, 0x0A, 0x05, 0x06, 0x07, 0x0B, 0x0E, 0x10, 0x11, 0x12, 0x15, 0x17, 0x1F, 0x20, 0x21, 0x22, 0x08):
            r = m.int60(ah, 0x55, 0x1234)
            print('AH=%02X -> AX=%04X DX=%04X DS=%04X  err3143=%02X' % (ah, r['ax'], r['dx'], r['ds'], m.rb(0x3143)[0]))
        r = m.int60(0x23); print('AH=23 -> AX=%04X err3143=%02X' % (r['ax'], m.rb(0x3143)[0]))
    elif sc in ('song', 'efc', 'fade', 'keys', 'ff'):
        m = Machine(com, b' /M10 /V0 /E4 /F /K' if sc != 'keys' else b' /M10 /V0 /E4')
        m.install()
        G = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'resources', 'original', 'game', '')
        song = open(G + 'XENON_01.M', 'rb').read()
        m.mu.mem_write(PSP * 16 + m.rw(0x30ec) - 1, song)
        efc = open(G + 'XEN_SE.EFC', 'rb').read()
        m.mu.mem_write(PSP * 16 + m.rw(0x30f0), efc)
        m.opn.log.clear()
        r = m.int60(0x00)
        print('mstart AX=%04X; writes: %s' % (r['ax'], fmt(m.opn.log)))
        print('TB=%02X tempo=%d playing=%d' % (m.rb(0x3113)[0], m.rb(0x315f)[0], m.rb(0x313c)[0]))
        if sc == 'song':
            for i in range(6):
                m.opn.log.clear(); m.irq(2)
                print('TimerB tick %d: %s' % (i, fmt(m.opn.log)[:300]))
            print('TB=%02X tempo=%d measure=%d clk=%d' % (m.rb(0x3113)[0], m.rb(0x315f)[0], m.rw(0x315b), m.rb(0x3118)[0]))
        if sc == 'efc':
            m.opn.log.clear()
            r = m.int60(0x0C, 1)
            print('efc on AX=%04X writes: %s  311D=%02X 311E=%02X 30AD=%02X 30AE=%02X' % (
                r['ax'], fmt(m.opn.log), m.rb(0x311d)[0], m.rb(0x311e)[0], m.rb(0x30ad)[0], m.rb(0x30ae)[0]))
            print('FM3 part flags:', [m.rb(a + 0x3b)[0] for a in (0x3258, 0x32b4, 0x3310, 0x336c)])
            n = 0
            while m.rb(0x311e)[0] and n < 100:
                m.opn.log.clear(); m.irq(1); n += 1
                if n <= 3 or not m.rb(0x311e)[0]:
                    print('TimerA tick %d: %s' % (n, fmt(m.opn.log)))
            print('effect ended after %d Timer-A ticks; FM3 flags:' % n, [m.rb(a + 0x3b)[0] for a in (0x3258, 0x32b4, 0x3310, 0x336c)])
            # SSG effect 0 (bass drum) via AH=03h
            m.opn.log.clear(); r = m.int60(0x03, 0)
            print('ssg eff 0 on: %s 0313=%02X 0314=%02X' % (fmt(m.opn.log), m.rb(0x313)[0], m.rb(0x314)[0]))
            n = 0
            while m.rb(0x313)[0] and n < 200:
                m.opn.log.clear(); m.irq(1); n += 1
                if n <= 3 or not m.rb(0x313)[0]:
                    print('  TA %d: %s' % (n, fmt(m.opn.log)))
            print('ssg effect ended after %d ticks' % n)
        if sc == 'fade':
            m.int60(0x02, 20)
            n = 0
            while m.rb(0x3115)[0] != 0xff and n < 1000:
                m.irq(1); n += 1
            print('fade speed 20: vol FF after %d Timer-A ticks; speed=%02X 30B8=%02X playing=%d status2=%02X' % (
                n, m.rb(0x3114)[0], m.rb(0x30b8)[0], m.rb(0x313c)[0], m.rb(0x3112)[0]))
            m.irq(2)
            print('after next TimerB: playing=%d status2=%02X (/F given -> keeps playing)' % (m.rb(0x313c)[0], m.rb(0x3112)[0]))
        if sc == 'keys':
            m.mu.mem_write(0x538, b'\x10'); m.mu.mem_write(0x52a, b'\x01')   # CTRL + ESC
            m.irq(1)
            print('CTRL+ESC: 30B8=%02X' % m.rb(0x30b8)[0]); m.irq(2)
            print('after TimerB: playing=%d status2=%02X' % (m.rb(0x313c)[0], m.rb(0x3112)[0]))
            m.mu.mem_write(0x52a, b'\x00')
            m.int60(0x00)
            m.mu.mem_write(0x538, b'\x18')   # CTRL+GRPH
            m.opn.log.clear(); m.irq(2)
            print('CTRL+GRPH TimerB writes 26h:', [x for x in m.opn.log if x[1] == 0x26])
            m.mu.mem_write(0x538, b'\x00'); m.opn.log.clear(); m.irq(2)
            print('released:', [x for x in m.opn.log if x[1] == 0x26])
        if sc == 'ff':
            r = m.int60(0x1C, 0, 3)
            print('FF to measure 3: AX=%04X measure=%d clk=%d writes=%d' % (r['ax'], m.rw(0x315b), m.rb(0x3118)[0], len(m.opn.log)))
            print('PIC writes', m.picw[-4:])


if __name__ == '__main__':
    main()
