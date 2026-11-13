#!/usr/bin/env python3
"""pmdmodel.py - independent Python re-implementation of the PMD 4.7g (PMD.COM, OPN/26K build)
music sequencer, written from the disassembly as documented in docs/re/pmd-sequencer.md.

It does NOT execute PMD code: it re-implements the per-clock part interpreter (notes, lengths,
q/Q gate, ties, loops, transpose, detune, portamento, FM voice load, FM volume -> TL, FM3
extended/special mode, SSG tone period, both SSG envelope formats, software LFO with all
waveforms, MX/EX/DX "extend" modes, temporary volumes, fade scaling) and produces the OPN register
writes per Timer-B clock.  Its purpose is (a) a reference for a port and (b) a check that the
documented formulas are right: --check runs the real driver in re-tools/pmdemu.py (unicorn) and
compares the register-write streams clock by clock.

Usage:
    python3 re-tools/pmdmodel.py SONG.M [--ticks N]                 print model writes per clock (assumes one
                                Timer-A interrupt per clock for MX/EX parts)
    ~/.venvs/xenon-re/bin/python re-tools/pmdmodel.py --check SONG.M [SONG.M ...] [--ticks N]
                                [--event CLOCK:AX ...]   compare with the emulated driver
                                (needs unicorn); --event 500:0214 = INT 60h AX=0214h (fade,
                                speed 20) after clock 500, 300:1940 = set fade volume 40h
    ~/.venvs/xenon-re/bin/python re-tools/pmdmodel.py --check-se XEN_SE.EFC [N ...] [--ticks N]
                                compare FM effects (FM registers only) per Timer-A clock

Scope: everything the 31 Xenon songs use (and most of the command set).  Not modelled: rhythm
part K (SSG drums), FM/SSG sound-effect interaction (masking by effects), PPSDRV, OPNA extras,
fast-forward keys.  Command bytes the model does not implement raise NotImplementedError.
Addresses in comments are PMD.COM CS offsets.
"""
import struct
import sys

FNUM = [0x26A, 0x28F, 0x2B6, 0x2DF, 0x30B, 0x339, 0x36A, 0x39E, 0x3D5, 0x410, 0x44E, 0x48F]  # 2CA9
SSGP = [0xEE8, 0xE12, 0xD48, 0xC89, 0xBD5, 0xB2B, 0xA8A, 0x9F3, 0x964, 0x8DD, 0x85E, 0x7E6]  # 2CC1
CARRIER = [0x80, 0x80, 0x80, 0x80, 0xA0, 0xE0, 0xE0, 0xF0]                              # 2CD9
NONCAR = [0xEE, 0xEE, 0xEE, 0xEE, 0xCC, 0x88, 0x88, 0x00]                               # 2CE1

# operand byte counts (same as pmdinfo.OPLEN) for skipping
SKIP = {0xFF: 1, 0xFE: 1, 0xFD: 1, 0xFB: 0, 0xFA: 2, 0xF9: 2, 0xF8: 4, 0xF7: 2, 0xF6: 0, 0xF5: 1,
        0xF4: 0, 0xF3: 0, 0xF2: 4, 0xF1: 1, 0xF0: 4, 0xEF: 2, 0xEE: 1, 0xED: 1, 0xEC: 1, 0xEB: 1,
        0xEA: 1, 0xE9: 1, 0xE8: 1, 0xE7: 1, 0xE6: 1, 0xE5: 2, 0xE4: 1, 0xE3: 1, 0xE2: 1, 0xE1: 1,
        0xE0: 1, 0xDF: 1, 0xDE: 1, 0xDD: 1, 0xDC: 1, 0xDB: 1, 0xDA: 3, 0xD9: 1, 0xD8: 1, 0xD7: 1,
        0xD6: 2, 0xD5: 2, 0xD4: 1, 0xD3: 1, 0xD2: 1, 0xD1: 1, 0xD0: 1, 0xCF: 1, 0xCE: 6, 0xCD: 5,
        0xCC: 1, 0xCB: 1, 0xCA: 1, 0xC9: 1, 0xC8: 3, 0xC7: 3, 0xC6: 6, 0xC5: 1, 0xC4: 1, 0xC3: 2,
        0xC2: 1, 0xC1: 0, 0xBF: 4, 0xBE: 1, 0xBD: 2, 0xBC: 1, 0xBB: 1, 0xBA: 1, 0xB9: 1, 0xB8: 2,
        0xB7: 1, 0xB6: 1, 0xB5: 2}


def s8(x):
    x &= 0xFF
    return x - 256 if x & 0x80 else x


def s16(x):
    x &= 0xFFFF
    return x - 65536 if x & 0x8000 else x


class Part:
    def __init__(self, name, kind, ch):
        self.name, self.kind, self.ch = name, kind, ch     # kind 'FM'/'SSG'; ch 1..3
        self.addr = 0; self.loop = 0; self.len = 0; self.gate = 0; self.freq = 0
        self.detune = 0; self.lfo = [0, 0]; self.porta = 0; self.pstep = 0; self.prem = 0
        self.vol = 0; self.trans = 0
        # LFO params per set: [delay_ctr, speed_ctr, step, times_ctr, delay, speed, step0, times]
        self.lp = [[0] * 8, [0] * 8]
        self.flags = 0          # +1C
        self.tmpvol = 0         # +1D
        self.md = [[0, 0, 0], [0, 0, 0]]   # delta(+1E), speed ctr(+1F), speed(+20) / +4C..4E
        self.mdc = [[0xFF, 0xFF], [0xFF, 0xFF]]  # +51/+52 , +53/+54
        self.wave = [0, 0]      # +3A / +4F
        self.ext = 0            # +2E (bit0 DX, bit1 MX, bit2 EX; bits 4-6 for LFO2)
        self.e = [0] * 13       # +21..+2D  (index 0 = +21)
        self.mix = 0            # +30
        self.voice = 0          # +31
        self.lstat = 0          # +32
        self.car = 0            # +33
        self.tl = [0, 0, 0, 0]  # +34..+37 (slot1, slot3, slot2, slot4 = reg order 40,44,48,4C)
        self.slot = 0           # +38
        self.vmask = 0          # +39
        self.mask = 0           # +3B
        self.kst = 0xFF         # +3C
        self.lslot = [0, 0]     # +3D / +50
        self.q = 0; self.Q = 0  # +3E/+3F
        self.note = 0xFF        # +55
        self.kd = 0; self.kdc = 0; self.kdm = 0   # +56/+57/+58
        self.fbalg = 0          # +59

    # envelope field accessors (offsets +21..+2D)
    def E(self, off):
        return self.e[off - 0x21]

    def setE(self, off, v):
        self.e[off - 0x21] = v & 0xFF


class Model:
    def __init__(self, mdata):
        self.m = mdata            # music data bytes = .M file minus byte 0  (CS:[30EC] points here)
        self.out = []             # (reg, val) for the current clock
        self.ks = [0, 0, 0]       # 30A5.. key-on slot bits per FM channel
        self.reg27 = 0x3F         # 30AD
        self.sd = [0, 0, 0, 0]    # 3148/314A/314C/314E slot detunes (slot1..4)
        self.sd_any = 0           # 30B9
        self.fm3need = 0          # 30BA
        self.fb3 = 0              # 30C6
        self.restore = 0          # 30C7
        self.tie = 0              # 30A1
        self.tvc = 0              # 30A2
        self.TB = 200; self.t = 78; self.TBc = 200    # 3113 / 3156 (installer already wrote 26h=C8h)
        self.status1 = 0; self.status2 = 0
        self.fade = 0; self.fadesp = 0   # 3115 / 3114
        self.voldown = [0x10, 0]  # 310B FM / 310C SSG (installer defaults: FM 16, SSG 0)
        self.noise = 0; self.noisec = 0
        self.reg7 = 0xBF
        self.na = 0               # Timer-A clocks since last Timer-B clock (3119 - 30B7)
        self.rnd = 0              # 1F32
        self.parts = {}
        order = [('A', 'FM', 1), ('B', 'FM', 2), ('C', 'FM', 3), ('D', 'FM', 3), ('E', 'FM', 3),
                 ('F', 'FM', 3), ('G', 'SSG', 1), ('H', 'SSG', 2), ('I', 'SSG', 3)]
        hdr = [struct.unpack_from('<H', mdata, 2 * i)[0] for i in range(11)]
        self.voicetab = None
        if hdr[0] != 0x18:
            self.voicetab = struct.unpack_from('<H', mdata, 0x18)[0]
        for i, (n, k, ch) in enumerate(order):
            p = Part(n, k, ch)
            a = hdr[i]
            p.addr = 0 if mdata[a] == 0x80 else a
            p.len = 1
            if k == 'FM':
                p.vol = 0x6C
                if i < 3:
                    p.slot, p.vmask = 0xF0, 0xFF
                else:
                    p.mask |= 0x20
            else:
                p.vol, p.mix = 8, 7
                p.setE(0x21, 3)
            self.parts[n] = p
        if hdr[9] != 0 and mdata[hdr[9]] != 0x80:
            pass  # part J exists in the header but this PMD build never processes it
        self.ssg_effects = []
        self.a3119 = 0            # 3119 Timer-A counter (cleared by mstart)

    # ---------------- OPN output ----------------
    def w(self, r, v):
        self.out.append((r & 0xFF, v & 0xFF))

    def start_writes(self):
        """register writes done by mstart (035A): silence 2248, init 04FA, timers 2214."""
        for o in [0, 1, 2, 4, 5, 6, 8, 9, 0xA, 0xC, 0xD, 0xE]:
            self.w(0x80 + o, 0xFF)
        for c in range(3):
            self.w(0x28, c)
        self.w(0x07, 0xBF)
        self.w(0x29, 0x83)          # reg 6 is only cleared here if an SSG effect is active
        self.tempo_reg()
        self.w(0x25, 0); self.w(0x24, 0); self.w(0x27, 0x3F)

    def tempo_reg(self):          # 2C3B
        if self.TB != self.TBc:
            self.TBc = self.TB
            self.w(0x26, self.TB)

    # ---------------- per clock ----------------
    def clock(self, na):
        """one Timer-B clock (music_tick 05A0); na = Timer-A interrupts since previous clock."""
        self.na = na & 0xFF
        self.out = []
        for _ in range(na):                  # Timer-A interrupts before this clock (2BA7)
            self.a3119 = (self.a3119 + 1) & 0xFF
            if self.a3119 & 7 == 0:
                self.fade_step()
        self.allst = 3
        for n in 'GHI':
            self.ssg_tick(self.parts[n])
        for n in 'ABCDEF':
            self.fm_tick(self.parts[n])
        if self.allst:
            for p in self.parts.values():
                if p.lstat != 3:
                    p.lstat = 0
            if self.allst == 3:
                self.status2 = 0xFF
            else:
                self.status2 = (self.status2 + 1) & 0xFF
                if self.status2 == 0xFF:
                    self.status2 = 1
        self.tempo_reg()
        return self.out

    def set_t(self, t):              # 1487
        self.t = t
        if t < 0x12:
            self.TB = 0
            return
        q, r = divmod(0x112C, t)
        tb = (256 - q) & 0xFF
        if r & 0x80:
            tb = (tb - 1) & 0xFF
        self.TB = tb

    def fade_step(self):             # 21CE (every 8th Timer-A interrupt)
        al = self.fadesp
        if al == 0:
            return
        if al < 0x80:
            v = self.fade + al
            if v > 0xFF:
                self.fade, self.fadesp = 0xFF, 0     # (+ song stop unless /F)
            else:
                self.fade = v
        else:
            v = self.fade + al
            if v <= 0xFF:
                self.fade, self.fadesp = 0, 0
            else:
                self.fade = v & 0xFF

    # ---------------- helpers ----------------
    def rb(self, p):
        return self.m[p]

    def rw(self, p):
        return struct.unpack_from('<H', self.m, p)[0]

    def is_ch3(self, p):
        # FM3 music part (C-F); the effect part 'X' (work 3594h) is excluded everywhere
        return p.kind == 'FM' and p.ch == 3 and p.name != 'X'

    @staticmethod
    def transpose(p, al):           # 16C2
        if al & 0x0F == 0x0F or p.trans == 0:
            return al
        n, o = al & 0x0F, al >> 4
        n += s8(p.trans)
        while n < 0:
            n += 12; o -= 1
        while n >= 12:
            n -= 12; o += 1
        return ((o & 0x0F) << 4) | n

    def gate(self, p, si):          # 076F
        if self.m[si] == 0xC1:
            p.gate = 0
            return si + 1
        if p.Q:
            p.gate = (((p.len * p.Q) >> 8) + p.q) & 0xFF
        else:
            p.gate = p.q
        return si

    # ---------------- FM ----------------
    def fm_note_freq(self, p, al):  # 1717
        if al & 0x0F == 0x0F:
            p.note = 0xFF
            if not (p.flags & 0x11):
                p.freq = 0
            return
        p.note = al
        p.freq = FNUM[al & 0x0F] | (((al >> 4) & 7) << 11)

    @staticmethod
    def normalize(ax, cx):          # 18E7  (cx = block<<11)
        ax = s16(ax)
        while True:
            if ax >= 0 and ax >= 0x26A:
                if ax < 0x4D4:
                    return ax & 0xFFFF, cx
                cx += 0x800
                if cx == 0x4000:
                    cx = 0x3800
                    return (0x7FF if ax >= 0x800 else ax), cx
                ax -= 0x26A
                continue
            cx -= 0x800
            if cx < 0:
                cx = 0
                if ax < 8:
                    ax = 8
                return ax & 0xFFFF, cx
            ax += 0x26A

    def fm_freq(self, p):            # 177E
        ax = p.freq
        if ax == 0 or p.slot == 0:
            return
        cx = ax & 0x3800
        ax = (ax & 0x7FF) + p.porta + p.detune
        ax &= 0xFFFF
        if self.is_ch3(p) and self.reg27 != 0x3F:
            bh = p.lslot[0] if p.lslot[0] & 0x0F else 0xF0
            ch = p.lslot[1] if p.lslot[1] & 0x0F else 0xF0
            bl = p.slot
            for bit, sd, hi, lo in ((0x80, 3, 0xA6, 0xA2), (0x40, 2, 0xAC, 0xA8),
                                    (0x20, 1, 0xAE, 0xAA), (0x10, 0, 0xAD, 0xA9)):
                if not (bl & bit):
                    continue
                a = (ax + self.sd[sd]) & 0xFFFF
                if bh & 0x80 and p.flags & 1:
                    a += p.lfo[0]
                if ch & 0x80 and p.flags & 0x10:
                    a += p.lfo[1]
                bh = ((bh << 1) | (bh >> 7)) & 0xFF        # rotated only for present slots (quirk)
                ch = ((ch << 1) | (ch >> 7)) & 0xFF
                a, c = self.normalize(a, cx)
                v = (a | c) & 0xFFFF
                self.w(hi, v >> 8); self.w(lo, v & 0xFF)
            return
        if p.flags & 1:
            ax += p.lfo[0]
        if p.flags & 0x10:
            ax += p.lfo[1]
        a, c = self.normalize(ax, cx)
        v = (a | c) & 0xFFFF
        self.w(0xA3 + p.ch, v >> 8); self.w(0x9F + p.ch, v & 0xFF)

    def fm_volume(self, p):          # 19DD
        bl = p.slot
        if bl == 0:
            return
        cl = (p.tmpvol - 1) & 0xFF if p.tmpvol else p.vol
        if p.name != 'X' and self.voldown[0]:
            cl = (cl * (256 - self.voldown[0])) >> 8
        if p.name != 'X' and self.fade >= 2:
            cl = (cl * (256 - (self.fade >> 1))) >> 8
        buf = [0x80] * 4                  # index 0 = slot4 ... 3 = slot1 (bit7..bit4)
        cl = (~cl) & 0xFF
        b = bl & p.car
        bh = b
        for i in range(4):
            if b & (0x80 >> i):
                buf[i] = cl
        if cl != 0xFF:
            for k, fl in ((0, 2), (1, 0x20)):
                if p.flags & fl:
                    m = p.lslot[k] & bl
                    bh |= m
                    v = s16(p.lfo[k])
                    al = v & 0xFF
                    for i in range(4):
                        if m & (0x80 >> i):
                            if al & 0x80:
                                r = buf[i] - al
                                buf[i] = 0xFF if r >= 0 else r & 0xFF
                            else:
                                r = buf[i] - al
                                buf[i] = 0 if r < 0 else r
        # slot4 -> 4C(+ch-1) [37]; slot3 -> 44 [35]; slot2 -> 48 [36]; slot1 -> 40 [34]
        for i, (reg, ti) in enumerate(((0x4B, 3), (0x43, 1), (0x47, 2), (0x3F, 0))):
            if bh & (0x80 >> i):
                a = buf[i] + p.tl[ti]
                if a > 0xFF:
                    a = 0xFF
                a -= 0x80
                if a < 0:
                    a = 0
                self.w(reg + p.ch, a)

    def keyon(self, p):              # 1BBE
        if p.note == 0xFF:
            return
        c = p.ch - 1
        al = self.ks[c] | p.slot
        if p.kdc:
            al &= p.kdm
        self.ks[c] = al
        self.w(0x28, c | al)

    def keyoff(self, p, check=True):  # 1C38 / 1C3F
        if check and p.note == 0xFF:
            return
        c = p.ch - 1
        self.ks[c] &= (~p.slot) & 0xFF
        self.w(0x28, c | self.ks[c])

    def voice_rec(self, n):          # 1D40
        if self.voicetab is None:
            raise NotImplementedError('external voice buffer')
        q = self.voicetab
        while self.m[q] != n:
            q += 26
        return q + 1

    def voice_mute(self, p):         # 1D7B: TL=7F, SL/RR=7F on [39] slots, then key-off
        al = p.vmask; reg = 0x3F + p.ch
        for _ in range(4):
            al = ((al << 1) | (al >> 7)) & 0xFF
            if al & 1:
                self.w(reg, 0x7F); self.w(reg + 0x40, 0x7F)
            reg += 4
        self.keyoff(p, check=False)

    def voice_restore(self, p):      # 0EA2
        if p.vmask == 0:
            return
        tl = list(p.tl)
        self.restore = 1
        self.voice_load(p)
        self.restore = 0
        p.tl = tl
        al = (~p.car) & p.slot
        for bit, reg, ti in ((0x80, 0x4B, 3), (0x40, 0x43, 1), (0x20, 0x47, 2), (0x10, 0x3F, 0)):
            if al & bit:
                self.w(reg + p.ch, tl[ti])

    def voice_load(self, p):         # 1C74
        bx = self.voice_rec(p.voice)
        v = self.m[bx:bx + 25]
        if p.vmask:
            self.voice_mute(p)
        else:
            p.tl = list(v[4:8])
            return
        dl = v[24]
        if self.restore:
            dl = p.fbalg
        if self.is_ch3(p):
            if self.restore:
                dl = self.fb3
            elif p.slot & 0x10:
                self.fb3 = dl
            else:
                dl = (self.fb3 & 0x38) | (dl & 7)
                self.fb3 = dl
        self.w(0xAF + p.ch, dl)
        p.fbalg = dl
        alg = dl & 7
        if not (p.lslot[0] & 0x0F):
            p.lslot[0] = CARRIER[alg]
        if not (p.lslot[1] & 0x0F):
            p.lslot[1] = CARRIER[alg]
        p.car = CARRIER[alg]
        ah = NONCAR[alg] & p.vmask
        al = p.vmask
        reg = 0x2F + p.ch
        k = 0
        for _ in range(4):
            al = ((al << 1) | (al >> 7)) & 0xFF
            if al & 1:
                self.w(reg, v[k])
            reg += 4; k += 1
        for _ in range(4):
            ah = ((ah << 1) | (ah >> 7)) & 0xFF
            if ah & 1:
                self.w(reg, v[k])
            reg += 4; k += 1
        for _ in range(16):
            al = ((al << 1) | (al >> 7)) & 0xFF
            if al & 1:
                self.w(reg, v[k])
            reg += 4; k += 1
        p.tl = list(v[4:8])

    def fm3_update(self, p):         # 1095
        bit = {'C': 1, 'D': 2, 'E': 4}.get(p.name, 8)
        s = p.slot & 0xF0
        if s == 0:
            need = False
        elif s != 0xF0:
            need = True
        elif not (p.lslot[0] & 0x0F):
            need = False
        elif p.flags & 1:
            need = True
        elif not (p.lslot[1] & 0x0F):
            need = False
        else:
            need = bool(p.flags & 0x10)
        if need:
            self.fm3need |= bit
            ah = 0x7F
        else:
            self.fm3need &= ~bit
            ah = 0x7F if (self.fm3need or self.sd_any == 1) else 0x3F
        if p.mask & 2:
            return
        if ah == self.reg27:
            return
        self.reg27 = ah
        self.w(0x27, ah & 0xCF)
        if ah == 0x3F or p.name == 'C':
            return
        for n in 'CDE':
            if n == p.name:
                break
            q = self.parts[n]
            if q.mask == 0:
                self.fm_freq(q)

    # ---------------- LFO ----------------
    def lfo_swap(self, p):
        pass   # the model keeps LFO1/LFO2 in separate arrays and indexes them instead

    def lfo_reset(self, p, k):       # 203A
        p.lfo[k] = 0
        L = p.lp[k]
        L[0:4] = L[4:8]
        p.mdc[k][0] = p.mdc[k][1]
        if p.flags & (4 << (4 * k)):
            L[0] = (L[0] + 1) & 0xFF
            L[4] = (L[4] + 1) & 0xFF

    def lfo_md(self, p, k):          # 1ECA
        md = p.md[k]
        md[1] = (md[1] - 1) & 0xFF
        if md[1]:
            return
        md[1] = md[2]
        c = p.mdc[k][0]
        if c == 0:
            return
        if not (c & 0x80):
            p.mdc[k][0] = c - 1
        L = p.lp[k]
        st = L[2]
        d = md[0]
        if st & 0x80:
            al = (((-st) & 0xFF) + d) & 0xFF
            if al & 0x80:
                al = 0 if d & 0x80 else 0x81
                L[2] = al            # stored as-is (already "negative" form)
                return
            L[2] = (-al) & 0xFF
        else:
            al = (st + d) & 0xFF
            if al & 0x80:
                al = 0 if d & 0x80 else 0x7F
            L[2] = al

    def lfo_rand(self, x):           # 1F18
        self.rnd = (self.rnd * 0x103 + 3) & 0x7FFF
        return ((x & 0xFFFF) * self.rnd) // 0x7FFF

    def lfo_step(self, p, k):        # 1DE5
        L = p.lp[k]
        wv = p.wave[k]
        if wv == 2:
            p.lfo[k] = (s8(L[2]) * s8(L[3])) & 0xFFFF
            if L[1] != 1:
                L[1] = (L[1] - 1) & 0xFF
                return
            L[1] = L[5]
            self.lfo_md(p, k)
            L[2] = (-L[2]) & 0xFF
            return
        if L[1] != 1:
            L[1] = (L[1] - 1) & 0xFF
            return
        L[1] = L[5]
        if wv in (0, 4, 5):
            if wv == 5:
                d = s8(L[2]) * abs(s8(L[2]))
            else:
                d = s8(L[2])
            p.lfo[k] = (p.lfo[k] + d) & 0xFFFF
            if p.lfo[k] == 0:
                self.lfo_md(p, k)
            al = L[3]
            if al == 0xFF:
                return
            al = (al - 1) & 0xFF
            if al:
                L[3] = al
                return
            L[3] = L[7] if wv == 4 else (2 * L[7]) & 0xFF
            L[2] = (-L[2]) & 0xFF
            return
        if wv == 6:
            al = L[3]
            if al == 0:
                return
            if al != 0xFF:
                L[3] = al - 1
            p.lfo[k] = (p.lfo[k] + s8(L[2])) & 0xFFFF
            return
        if wv == 1:
            p.lfo[k] = (p.lfo[k] + s8(L[2])) & 0xFFFF
            al = L[3]
            if al != 0xFF:
                al = (al - 1) & 0xFF
                if al == 0:
                    p.lfo[k] = (-p.lfo[k]) & 0xFFFF
                    self.lfo_md(p, k)
                    al = (2 * L[7]) & 0xFF
            L[3] = al
            return
        a = (abs(s8(L[2])) * L[3]) & 0xFFFF          # 3, 7: random
        r = self.lfo_rand(2 * a)
        p.lfo[k] = (r - a) & 0xFFFF
        self.lfo_md(p, k)

    def lfo_tick(self, p, k):        # 1DAE ; returns True if value changed
        L = p.lp[k]
        if L[0]:
            L[0] -= 1
            return False
        old = p.lfo[k]
        if p.ext & (2 << (4 * k)):
            n = self.na
            if n == 0:
                return False
            for _ in range(n):
                self.lfo_step(p, k)
        else:
            self.lfo_step(p, k)
        return p.lfo[k] != old

    def lfo_keyon(self, p):          # 1FF5
        p.kdc = p.kd
        for k in (0, 1):
            sh = 4 * k
            if p.flags & (3 << sh):
                if p.flags & (4 << sh):
                    self.lfo_tick(p, k)
                else:
                    self.lfo_reset(p, k)

    def lfo_only(self, p):           # 1FD5
        for k in (0, 1):
            if p.flags & (3 << (4 * k)):
                self.lfo_tick(p, k)

    # ---------------- SSG ----------------
    def ssg_period(self, p, al):     # 174E
        if al & 0x0F == 0x0F:
            p.note = 0xFF
            if not (p.flags & 0x11):
                p.freq = 0
            return
        p.note = al
        o = (al >> 4) & 0x0F
        v = SSGP[al & 0x0F]
        r = v >> o
        if o and (v >> (o - 1)) & 1:
            r += 1
        p.freq = r

    def ssg_freq(self, p):           # 192A
        ax = p.freq
        if ax == 0:
            return
        ax = (ax + p.porta) & 0xFFFF
        if p.ext & 1:
            def scale(a, d):
                prod = s16(a) * s16(d)
                dx = prod >> 12
                return dx + 1 if dx >= 0 else dx - 1
            if p.detune:
                ax = (ax - scale(ax, p.detune)) & 0xFFFF
            if p.flags & 0x11:
                d = 0
                if p.flags & 1:
                    d = p.lfo[0]
                if p.flags & 0x10:
                    d = (d + p.lfo[1]) & 0xFFFF
                if d:
                    ax = (ax - scale(ax, d)) & 0xFFFF
        else:
            ax = (ax - p.detune) & 0xFFFF
            if p.flags & 1:
                ax = (ax - p.lfo[0]) & 0xFFFF
            if p.flags & 0x10:
                ax = (ax - p.lfo[1]) & 0xFFFF
        if ax >= 0x1000:
            ax = 0 if ax & 0x8000 else 0xFFF
        r = (p.ch - 1) * 2
        self.w(r, ax & 0xFF); self.w(r + 1, ax >> 8)

    def ssg_volume(self, p):         # 1B15
        m = p.E(0x21)
        if m == 3 or (m == 0xFF and p.E(0x22) == 0):
            return
        dl = (p.tmpvol - 1) & 0xFF if p.tmpvol else p.vol
        if self.voldown[1]:
            dl = (dl * (256 - self.voldown[1])) >> 8
        if self.fade:
            dl = (dl * (256 - self.fade)) >> 8
        if dl:
            if m == 0xFF:
                lv = p.E(0x2D)
                if lv == 0:
                    dl = 0
                else:
                    x = (dl * (lv + 1)) & 0xFF
                    dl = (x >> 4) + ((x >> 3) & 1)
            else:
                dl = dl + s8(p.E(0x2D))
                if dl < 0 or (dl & 0x80):
                    dl = 0
                elif dl >= 16:
                    dl = 15
            if dl and p.flags & 0x22:
                a = 0
                if p.flags & 2:
                    a = p.lfo[0]
                if p.flags & 0x20:
                    a = (a + p.lfo[1]) & 0xFFFF
                d = s16(dl + a)
                dl = 0 if d < 0 else (15 if d >= 16 else d)
        self.w(7 + p.ch, dl)

    def ssg_keyon(self, p):          # 1BEA
        if p.note == 0xFF:
            return
        bits = (1 << (p.ch - 1)) | (8 << (p.ch - 1))
        al = self.reg7 | bits
        ah = (~(bits & p.mix)) & 0xFF
        al &= ah
        self.reg7 = al
        self.w(7, al)
        # noise frequency only when no SSG effect is running ([0314] bit7); see doc
        if self.noise != self.noisec and self.ssg_noise_ok:
            self.w(6, self.noise)
            self.noisec = self.noise

    ssg_noise_ok = True

    def ssg_keyoff(self, p):         # 1C5D
        if p.note == 0xFF:
            return
        if p.E(0x21) != 0xFF:
            p.setE(0x21, 2)
        else:
            p.setE(0x22, 4)

    def env_keyon(self, p):          # 1F50..
        if p.E(0x21) != 0xFF:
            p.setE(0x21, 0); p.setE(0x2D, 0)
            al = p.E(0x29)
            p.setE(0x23, al)
            if al == 0:
                p.setE(0x21, 1); p.setE(0x2D, p.E(0x24))
            p.setE(0x25, p.E(0x2B)); p.setE(0x26, p.E(0x2C))
        else:
            p.setE(0x29, p.E(0x23) - 16)
            a = s8(p.E(0x24) - 16); p.setE(0x2A, a * 2 if a < 0 else a)
            a = s8(p.E(0x25) - 16); p.setE(0x2B, a * 2 if a < 0 else a)
            p.setE(0x2C, 2 * p.E(0x26) - 16)
            p.setE(0x2D, p.E(0x28))
            p.setE(0x22, 1)
            self.env_ext_step(p)

    def env_normal_step(self, p):    # 2091
        st = p.E(0x21)
        if st == 0:
            v = (p.E(0x23) - 1) & 0xFF
            p.setE(0x23, v)
            if v == 0:
                p.setE(0x21, 1); p.setE(0x2D, p.E(0x24))
            return
        if st == 2:
            if p.E(0x26) == 0:
                p.setE(0x2D, 0xF1)
                return
            v = (p.E(0x26) - 1) & 0xFF
            p.setE(0x26, v)
            if v:
                return
            p.setE(0x26, p.E(0x2C))
        else:
            if p.E(0x25) == 0:
                return
            v = (p.E(0x25) - 1) & 0xFF
            p.setE(0x25, v)
            if v:
                return
            p.setE(0x25, p.E(0x2B))
        lv = (p.E(0x2D) - 1) & 0xFF
        if not (lv >= 0xF1 or lv < 0x0F):
            lv = 0xF1
        p.setE(0x2D, lv)

    def env_ext_step(self, p):       # 20E9 / 20FE
        ph = p.E(0x22)
        if ph == 0:
            return
        lv = p.E(0x2D)
        if ph == 1:
            al = p.E(0x29)
            if s8(al - 1) < 0:
                if p.E(0x23):
                    p.setE(0x29, al + 1)
                return
            lv = lv + al
            if lv >= 0x0F and True:
                pass
            if (lv & 0xFF) >= 0x0F or lv > 0xFF:
                p.setE(0x2D, 0x0F)
                p.setE(0x22, 2)
                if p.E(0x27) == 0x0F:
                    p.setE(0x22, 3)
                return
            p.setE(0x2D, lv)
            p.setE(0x29, p.E(0x23) - 16)
            return
        if ph == 2:
            al = p.E(0x2A)
            if s8(al - 1) < 0:
                if p.E(0x24):
                    p.setE(0x2A, al + 1)
                return
            r = lv - al
            sl = p.E(0x27)
            if r < 0 or r < sl:
                p.setE(0x2D, sl); p.setE(0x22, 3)
                return
            p.setE(0x2D, r)
            a = s8(p.E(0x24) - 16); p.setE(0x2A, a * 2 if a < 0 else a)
            return
        if ph == 3:
            al = p.E(0x2B)
            if s8(al - 1) < 0:
                if p.E(0x25):
                    p.setE(0x2B, al + 1)
                return
            r = lv - al
            p.setE(0x2D, 0 if r < 0 else r)
            a = s8(p.E(0x25) - 16); p.setE(0x2B, a * 2 if a < 0 else a)
            return
        al = p.E(0x2C)
        if s8(al - 1) < 0:
            if p.E(0x26):
                p.setE(0x2C, al + 1)
            return
        r = lv - al
        p.setE(0x2D, 0 if r < 0 else r)
        p.setE(0x2C, 2 * p.E(0x26) - 16)

    def env_tick1(self, p):          # 207E
        old = p.E(0x2D)
        if p.E(0x21) == 0xFF:
            self.env_ext_step(p)
        else:
            self.env_normal_step(p)
        return p.E(0x2D) != old

    def env_tick(self, p):           # 205E
        if p.ext & 4:
            n = self.na
            ch = False
            for _ in range(n):
                if self.env_tick1(p):
                    ch = True
            return ch
        return self.env_tick1(p)

    # ---------------- commands ----------------
    def cmd(self, p, si, c):
        """execute command c (operands at si). returns (new si, action) where action in
        (None, 'mode') - 'mode' means the handler jumped back into the parse loops (C0, CF)."""
        m = self.m
        fm = p.kind == 'FM'
        if c == 0xFF:
            p.voice = m[si]
            if fm:
                if p.mask == 0:
                    self.voice_load(p)
                else:
                    bx = self.voice_rec(p.voice)
                    p.fbalg = m[bx + 24]
                    p.tl = list(m[bx + 4:bx + 8])
            return si + 1, None
        if c == 0xFE:
            p.q = m[si]; return si + 1, None
        if c == 0xFD:
            p.vol = m[si]; return si + 1, None
        if c == 0xFC:
            al = m[si]; si += 1
            if al < 0xFB:
                self.TB = al
                bl = (256 - al) & 0xFF        # 1468: t = 4396/(256-TB), rounded
                if bl < 0x12:
                    self.t = 0xFF
                else:
                    q, r = divmod(0x112C, bl)
                    self.t = q + (1 if r & 0x80 else 0)
            elif al == 0xFF:            # t (1420): clamp >= 18, then TB = 256 - 4396/t
                self.set_t(max(0x12, m[si])); si += 1
                return si, None
            elif al == 0xFE:
                d = s8(m[si]); si += 1
                v = self.TB + d
                if d >= 0:
                    v = 0xFA if v > 0xFF else v
                else:
                    v = 0 if v < 0 else v
                if v >= 0xFB:
                    v = 0xFA
                self.TB = v
            else:                       # FD/FC/FB: relative t (1452)
                d = m[si]; si += 1
                v = self.t + s8(d)
                v = 0xFF if v > 0xFF else (0x12 if v < 0x12 else v)
                self.set_t(v)
                return si, None
            return si, None
        if c == 0xFB:
            self.tie |= 1; return si, None
        if c == 0xFA:
            p.detune = self.rw(si); return si + 2, None
        if c == 0xD5:
            p.detune = (p.detune + self.rw(si)) & 0xFFFF; return si + 2, None
        if c == 0xF9:
            m[self.rw(si) + 1] = 0; return si + 2, None
        if c == 0xF8:
            cnt = m[si]
            if cnt == 0:
                p.lstat = 1
                return self.rw(si + 2) + 2, None
            m[si + 1] = (m[si + 1] + 1) & 0xFF
            if m[si + 1] == cnt:
                return si + 4, None
            return self.rw(si + 2) + 2, None
        if c == 0xF7:
            bx = self.rw(si)
            if (m[bx] - 1) & 0xFF == m[bx + 1]:
                return bx + 4, None
            return si + 2, None
        if c == 0xF6:
            p.loop = si; return si, None
        if c == 0xF5:
            p.trans = m[si]; return si + 1, None
        if c == 0xE7:
            p.trans = (p.trans + m[si]) & 0xFF; return si + 1, None
        if c in (0xF4, 0xE3, 0xF3, 0xE2):
            if c in (0xF4, 0xF3):
                n = 4 if fm else 1
            else:
                n = m[si]; si += 1
            if c in (0xF4, 0xE3):
                v = (p.vol + n) & 0xFF
                lim = 0x80 if fm else 0x10
                if v >= lim:
                    v = lim - 1
            else:
                v = p.vol - n
                if v < 0:
                    v = 0
            p.vol = v
            return si, None
        if c == 0xF2:
            L = p.lp[0]
            L[0] = L[4] = m[si]; L[1] = L[5] = m[si + 1]; L[2] = L[6] = m[si + 2]
            L[3] = L[7] = m[si + 3]
            self.lfo_reset(p, 0)
            return si + 4, None
        if c == 0xF1:
            al = m[si]
            if al & 0xF8:
                al = 1
            p.flags = (p.flags & 0xF8) | (al & 7)
            self.lfo_reset(p, 0)
            if fm and self.is_ch3(p):
                self.fm3_update(p)
            return si + 1, None
        if c == 0xCB:
            p.wave[0] = m[si]; return si + 1, None
        if c == 0xCA:
            p.ext = (p.ext & 0xFD) | ((m[si] & 1) << 1); return si + 1, None
        if c == 0xBB:
            p.ext = (p.ext & 0xDF) | ((m[si] & 1) << 5); return si + 1, None
        if c == 0xCC:
            if not fm:
                p.ext = (p.ext & 0xFE) | (m[si] & 1)
            return si + 1, None
        if c == 0xC9:
            if not fm:
                p.ext = (p.ext & 0xFB) | ((m[si] & 1) << 2)
            return si + 1, None
        if c == 0xEF:
            self.w(m[si], m[si + 1]); return si + 2, None
        if c == 0xD4:                # SSG effect on/off (0167/0282): SSG writes not modelled
            self.ssg_effects.append(m[si]); return si + 1, None
        if c == 0xD3:
            raise NotImplementedError('FM effect started from music data')
        if c == 0xD2:                # 1258: fade-out from music data
            self.fadesp = m[si]; return si + 1, None
        if c == 0xDC:
            self.status1 = m[si]; return si + 1, None
        if c == 0xDB:
            self.status1 = (self.status1 + m[si]) & 0xFF; return si + 1, None
        if c == 0xDF:
            return si + 1, None
        if c in (0xDE, 0xDD):
            n = m[si]
            if c == 0xDE:
                v = n + p.vol
                if fm:
                    v = 0x7F if (v & 0xFF) >= 0x80 else v & 0xFF
                else:
                    v = 0x0F if (v & 0xFF) >= 0x10 else v & 0xFF
            else:
                v = p.vol - n
                v = 0 if v < 0 else (0xFE if v >= 0xFF else v)
            p.tmpvol = v + 1
            self.tvc = 1
            return si + 1, None
        if c == 0xF0 and not fm:
            p.setE(0x23, m[si]); p.setE(0x29, m[si]); p.setE(0x24, m[si + 1])
            p.setE(0x25, m[si + 2]); p.setE(0x2B, m[si + 2])
            p.setE(0x26, m[si + 3]); p.setE(0x2C, m[si + 3])
            if p.E(0x21) == 0xFF:
                p.setE(0x21, 2); p.setE(0x2D, 0xF1)
            return si + 4, None
        if c == 0xCD and not fm:
            p.setE(0x23, m[si] & 0x1F); p.setE(0x24, m[si + 1] & 0x1F)
            p.setE(0x25, m[si + 2] & 0x1F)
            b4 = m[si + 3]
            p.setE(0x26, b4 & 0x0F); p.setE(0x27, ((b4 >> 4) & 0x0F) ^ 0x0F)
            p.setE(0x28, m[si + 4] & 0x0F)
            if p.E(0x21) != 0xFF:
                p.setE(0x21, 0xFF); p.setE(0x22, 4); p.setE(0x2D, 0)
            return si + 5, None
        if c == 0xEE and not fm:
            self.noise = m[si]; return si + 1, None
        if c == 0xD0 and not fm:     # 1696 noise frequency relative, clamp 0..1F
            v = s8(m[si]) + self.noise
            v &= 0xFF
            if v & 0x80:
                v = 0
            if v >= 0x20:
                v = 0x1F
            self.noise = v
            return si + 1, None
        if c == 0xED and not fm:
            p.mix = m[si]; return si + 1, None
        if c == 0xC4:
            p.Q = m[si]; return si + 1, None
        if c == 0xC1:
            return si, None
        if c == 0xCF and fm:
            al = m[si]; si += 1
            if al & 0x0F:
                p.car = (al & 0x0F) << 4
            else:
                if self.is_ch3(p):
                    b = self.fb3
                else:
                    b = self.m[self.voice_rec(p.voice) + 24]
                p.car = CARRIER[b & 7]
            ah = al & 0xF0
            if p.slot == ah:
                return si, None
            p.slot = ah
            if ah:
                p.mask &= 0xDF
            else:
                p.mask |= 0x20
            if self.is_ch3(p):
                self.fm3_update(p)
                if p.name != 'C':
                    for n in 'CDE':
                        if n == p.name:
                            break
                        q = self.parts[n]
                        if q.mask == 0 and not (q.kst & 1):
                            self.keyon(q)
            vm = 0
            if ah & 0x80: vm |= 0x11
            if ah & 0x40: vm |= 0x44
            if ah & 0x20: vm |= 0x22
            if ah & 0x10: vm |= 0x88
            p.vmask = vm
            return si, 'mode'
        if c in (0xC8, 0xC7) and fm:
            if not self.is_ch3(p):
                return si + 3, None
            bl = m[si]; ax = self.rw(si + 1); si += 3
            for i in range(4):
                if bl & (1 << i):
                    self.sd[i] = ax if c == 0xC8 else (self.sd[i] + ax) & 0xFFFF
            self.sd_any = 1 if any(self.sd) else 0
            self.fm3_update(p)
            return si, None
        if c == 0xC5 and fm:
            al = m[si] & 0x0F
            p.lslot[0] = ((al << 4) | 0x0F) if al else p.car
            if self.is_ch3(p):
                self.fm3_update(p)
            return si + 1, None
        if c == 0xB8 and fm:
            al = m[si]; dl = m[si + 1]; si += 2
            ah = al & 0x0F & (p.slot >> 4)
            for bit, ti, reg in ((1, 0, 0x3F), (2, 2, 0x47), (4, 1, 0x43), (8, 3, 0x4B)):
                if not (ah & bit):
                    continue
                if al & 0x80:
                    v = p.tl[ti] + s8(dl)
                    if v & 0x80 or v < 0:
                        v = 0 if (dl & 0x80) else 0x7F
                else:
                    v = dl & 0x7F
                if p.mask == 0:
                    self.w(reg + p.ch, v)
                p.tl[ti] = v
            return si, None
        if c == 0xB6 and fm:         # 0CF2 feedback
            al = m[si]; si += 1
            ch3 = self.is_ch3(p)
            if al & 0x80:
                if not (al & 0x40):
                    al &= 7
                cur = ((self.fb3 if ch3 else p.fbalg) >> 3) & 7
                v = s8(al) + cur
                fb = 0 if v < 0 else (0x38 if v >= 8 else (v << 3))
            else:
                fb = ((al << 3) | (al >> 5)) & 0xFF
            if ch3:
                if not (p.slot & 0x10):
                    return si, None
                dl = (self.fb3 & 7) | fb
                self.fb3 = dl
            else:
                dl = (p.fbalg & 7) | fb
            self.w(0xAF + p.ch, dl)
            p.fbalg = dl
            return si, None
        if c == 0xB5:                # 0CDA slot key-on delay
            p.kdm = (((m[si] & 0x0F) ^ 0x0F) << 4) & 0xFF
            p.kd = p.kdc = m[si + 1]
            return si + 2, None
        if c in (0xD6, 0xBD):        # 1261 MD (BD = LFO2)
            k = 0 if c == 0xD6 else 1
            p.md[k][1] = p.md[k][2] = m[si]; p.md[k][0] = m[si + 1]
            return si + 2, None
        if c == 0xB7:                # 126D MD count
            al = m[si]
            k = 1 if al & 0x80 else 0
            al &= 0x7F if k else 0xFF
            if al == 0:
                al = 0xFF
            p.mdc[k] = [al, al]
            return si + 1, None
        if c == 0xBF:                # 1588 LFO2 M
            L = p.lp[1]
            L[0] = L[4] = m[si]; L[1] = L[5] = m[si + 1]; L[2] = L[6] = m[si + 2]
            L[3] = L[7] = m[si + 3]
            self.lfo_reset(p, 1)
            return si + 4, None
        if c == 0xBC:
            p.wave[1] = m[si]; return si + 1, None
        if c in (0xC2, 0xB9):        # 163E delay (B9 = LFO2)
            k = 0 if c == 0xC2 else 1
            p.lp[k][0] = p.lp[k][4] = m[si]
            self.lfo_reset(p, k)
            return si + 1, None
        if c == 0xBE:                # 15AD/15C2: LFO2 switch; NB resets LFO1 (no swap)
            p.flags = (p.flags & 0x8F) | ((m[si] & 7) << 4)
            self.lfo_reset(p, 0)
            if fm and self.is_ch3(p):
                self.fm3_update(p)
            return si + 1, None
        if c == 0xBA and fm:         # 0F36 LFO2 slot mask
            al = m[si] & 0x0F
            p.lslot[1] = ((al << 4) | 0x0F) if al else p.car
            if self.is_ch3(p):
                self.fm3_update(p)
            return si + 1, None
        if c == 0xC0:
            al = m[si]; si += 1
            if al >= 2:
                if al < 0xF7:
                    raise NotImplementedError('C0 %02X = part end' % al)
                v = m[si]; si += 1
                idx = {0xFF: 0, 0xFE: 0, 0xFD: 1, 0xFC: 1}.get(al)
                if idx is None:
                    return si, None          # PCM/rhythm volume-down, ADPCM flag: no OPN effect
                if al in (0xFF, 0xFD):
                    self.voldown[idx] = v
                elif v == 0:
                    self.voldown[idx] = [0x10, 0][idx]
                elif v & 0x80:
                    r = self.voldown[idx] + v
                    self.voldown[idx] = r & 0xFF if r > 0xFF else 0
                else:
                    self.voldown[idx] = min(0xFF, self.voldown[idx] + v)
                return si, None
            if al:
                p.mask |= 0x40
                if p.mask == 0x40:
                    if fm:
                        self.voice_mute(p)
                    else:
                        bits = (1 << (p.ch - 1)) | (8 << (p.ch - 1))
                        self.reg7 |= bits
                        self.w(7, self.reg7)
            else:
                p.mask &= 0xBF
                if p.mask == 0 and fm:
                    self.voice_restore(p)
            return si, 'mode'
        if c == 0xDA:
            raise NotImplementedError('DA handled in parse')
        if c in (0xEC, 0xEB, 0xEA, 0xE9, 0xE8, 0xE6, 0xE5, 0xE4, 0xE1, 0xE0, 0xC3, 0xCE,
                 0xD9, 0xD8, 0xD7, 0xD1) or (fm and c in (0xF0, 0xEE, 0xED, 0xCD, 0xCC, 0xC9)):
            return si + SKIP[c], None
        raise NotImplementedError('command %02X in part %s' % (c, p.name))

    # ---------------- part tick ----------------
    def parse(self, p, si, masked):
        """command loop. returns ('note', si, al) or ('end', si) or ('masked_note', si)."""
        m = self.m
        while True:
            al = m[si]; si += 1
            if al == 0x80:
                si -= 1
                p.addr = si
                p.lstat = 3
                p.note = 0xFF
                if p.loop:
                    si = p.loop
                    p.lstat = 1
                    continue
                return ('end', si)
            if al < 0x80:
                return ('note', si, al)
            if al < 0xB5:
                raise NotImplementedError('bad command %02X' % al)
            if al == 0xDA:
                return ('porta', si, al)
            si, act = self.cmd(p, si, al)
            if act == 'mode':
                return ('remode', si)

    def fm_tick(self, p):
        if p.addr == 0:
            return
        if p.mask:
            return self.fm_masked(p)
        p.len = (p.len - 1) & 0xFF
        if not (p.kst & 3) and p.len <= p.gate:
            self.keyoff(p)
            p.kst = 0xFF
        if p.len == 0:
            self.fm_parse_note(p, p.addr)
        else:
            self.fm_sustain(p)
        self.allst &= p.lstat

    def fm_parse_note(self, p, si):
        p.flags &= 0xF7
        while True:
            r = self.parse(p, si, False)
            if r[0] == 'remode':
                si = r[1]
                if p.mask:
                    return self.fm_masked_parse(p, si)
                continue
            break
        if r[0] == 'end':
            self.fm_sustain(p)
            return
        si = r[1]; al = r[2]
        if r[0] == 'porta':
            n1 = self.m[si]; si += 1
            self.fm_prep(p, n1)
            self.fm_note_freq(p, self.transpose(p, n1))
            f1, nt = p.freq, p.note
            n2 = self.m[si]; si += 1
            self.fm_note_freq(p, self.transpose(p, n2))
            f2 = p.freq
            p.note, p.freq = nt, f1
            b1 = (f1 >> 11) & 7; b2 = (f2 >> 11) & 7
            diff = (b2 - b1) * 0x26A + ((f2 & 0x7FF) - (f1 & 0x7FF))
            p.len = self.m[si]; si += 1
            si = self.gate(p, si)
            q = int(diff / p.len)
            p.pstep = q & 0xFFFF; p.prem = (diff - q * p.len) & 0xFFFF
            p.flags |= 8
        else:
            self.fm_prep(p, al)
            self.fm_note_freq(p, self.transpose(p, al))
            p.len = self.m[si]; si += 1
            si = self.gate(p, si)
        self.after_note(p)
        self.fm_volume(p)
        self.fm_freq(p)
        self.keyon(p)
        self.finish_note(p, si)

    def after_note(self, p):         # 06A8 temporary volume bookkeeping
        if p.tmpvol and p.note != 0xFF:
            self.tvc = (self.tvc - 1) & 0xFF
            if self.tvc:
                self.tvc = 0
                p.tmpvol = 0

    def finish_note(self, p, si):
        p.addr = si
        self.tie = 0; self.tvc = 0
        p.kst = 2 if self.m[si] == 0xFB else 0

    def fm_prep(self, p, al):        # 1FBF
        if al & 0x0F == 0x0F or self.tie & 1:
            if al & 0x0F != 0x0F:
                p.porta = 0
            self.lfo_only(p)
            return
        p.porta = 0
        self.lfo_keyon(p)

    def porta_step(self, p):         # 128A
        p.porta = (p.porta + p.pstep) & 0xFFFF
        r = s16(p.prem)
        if r > 0:
            p.prem = r - 1; p.porta = (p.porta + 1) & 0xFFFF
        elif r < 0:
            p.prem = (r + 1) & 0xFFFF; p.porta = (p.porta - 1) & 0xFFFF

    def lfo_changes(self, p):
        cl = p.flags
        f = cl & 8
        if cl & 3 and self.lfo_tick(p, 0):
            f |= cl & 3
        if cl & 0x30 and self.lfo_tick(p, 1):
            f |= p.flags & 0x30
        return f

    def fm_sustain(self, p):         # 06EA
        if p.kdc:
            p.kdc -= 1
            if p.kdc == 0 and not (p.kst & 1):
                self.keyon(p)
        if p.flags:
            f = self.lfo_changes(p)
            if f & 0x19:
                if f & 8:
                    self.porta_step(p)
                self.fm_freq(p)
            if f & 0x22 or self.fadesp:
                self.fm_volume(p)
        elif self.fadesp:
            self.fm_volume(p)

    def fm_masked(self, p):          # 0796
        p.len = (p.len - 1) & 0xFF
        if p.len:
            self.allst &= p.lstat
            return
        self.fm_masked_parse(p, p.addr)
        self.allst &= p.lstat

    def fm_masked_parse(self, p, si):
        while True:
            r = self.parse(p, si, True)
            if r[0] == 'remode':
                si = r[1]
                if p.mask == 0:
                    return self.fm_parse_note(p, si)
                continue
            break
        if r[0] == 'end':
            self.tie = 0; self.tvc = 0
            return
        si = r[1]
        if r[0] == 'porta':
            si += 2
        p.freq = 0; p.note = 0xFF
        p.len = self.m[si]; si += 1
        p.addr = si
        self.tie = 0; self.tvc = 0

    def ssg_tick(self, p):
        if p.addr == 0:
            return
        if p.mask:
            p.len = (p.len - 1) & 0xFF
            if p.len == 0:
                p.flags &= 0xF7
                self.ssg_masked_parse(p, p.addr)
            self.allst &= p.lstat
            return
        p.len = (p.len - 1) & 0xFF
        if not (p.kst & 3) and p.len <= p.gate:
            self.ssg_keyoff(p)
            p.kst = 0xFF
        if p.len == 0:
            p.flags &= 0xF7
            self.ssg_parse_note(p, p.addr)
        else:
            self.ssg_sustain(p)
        self.allst &= p.lstat

    def ssg_parse_note(self, p, si):
            while True:
                r = self.parse(p, si, False)
                if r[0] == 'remode':
                    si = r[1]
                    if p.mask:
                        return self.ssg_masked_parse(p, si)
                    continue
                break
            if r[0] == 'end':
                self.ssg_sustain(p)
                return
            si = r[1]; al = r[2]
            if r[0] == 'porta':
                n1 = self.m[si]; si += 1
                self.ssg_prep(p, n1)
                self.ssg_period(p, self.transpose(p, n1))
                f1, nt = p.freq, p.note
                n2 = self.m[si]; si += 1
                self.ssg_period(p, self.transpose(p, n2))
                diff = p.freq - f1
                p.note, p.freq = nt, f1
                p.len = self.m[si]; si += 1
                si = self.gate(p, si)
                q = int(diff / p.len)
                p.pstep = q & 0xFFFF; p.prem = (diff - q * p.len) & 0xFFFF
                p.flags |= 8
            else:
                self.ssg_prep(p, al)
                self.ssg_period(p, self.transpose(p, al))
                p.len = self.m[si]; si += 1
                si = self.gate(p, si)
            self.after_note(p)
            self.ssg_volume(p)
            self.ssg_freq(p)
            self.ssg_keyon(p)
            self.finish_note(p, si)

    def ssg_masked_parse(self, p, si):  # 0911 (no SSG-effect interaction modelled)
        while True:
            r = self.parse(p, si, True)
            if r[0] == 'remode':
                si = r[1]
                if p.mask == 0:
                    return self.ssg_parse_note(p, si)   # 0826: normal parse loop
                continue
            break
        if r[0] == 'end':
            self.tie = 0; self.tvc = 0
            return
        si = r[1]
        if r[0] == 'porta':
            si += 2                    # masked DA consumes one byte, n2 is read as a note
        p.freq = 0; p.note = 0xFF
        p.len = self.m[si]; si += 1
        p.addr = si
        self.tie = 0; self.tvc = 0

    def ssg_prep(self, p, al):       # 1F34
        if al & 0x0F == 0x0F:
            self.lfo_only(p)
            return
        p.porta = 0
        if self.tie & 1:
            self.lfo_only(p)
            return
        self.env_keyon(p)
        self.lfo_keyon(p)

    def ssg_sustain(self, p):        # 089F
        f = p.flags & 8
        if p.flags:
            f = self.lfo_changes(p)
            if f & 0x19:
                if f & 8:
                    self.porta_step(p)
                self.ssg_freq(p)
        ch = self.env_tick(p)
        if ch or (f & 0x22) or self.fadesp:
            self.ssg_volume(p)


class EffectModel(Model):
    """FM sound effect n of an .EFC image, run like fm_effect_on (29A8h) + efc_part_tick (032Bh):
    one clock per Timer-A interrupt on FM channel 3, voices from the EFC voice table."""
    def __init__(self, efc, n):
        Model.__init__(self, bytearray(efc[2:]) + b'\x80' * 32, )  # dummy song header
        self.m = bytearray(efc)
        self.voicetab = struct.unpack_from('<H', self.m, 0xFE)[0]
        p = Part('X', 'FM', 3)
        p.addr = struct.unpack_from('<H', self.m, 2 * n)[0]
        p.len = 1; p.vol = 0x6C; p.slot = 0xF0; p.vmask = 0xFF
        p.kst = 0; p.note = 0           # work area is zero-filled, not FF
        self.parts = {'X': p}
        self.active = True

    def clock(self, na=0):
        self.out = []
        self.allst = 3
        if self.active:
            p = self.parts['X']
            self.fm_tick(p)
            if self.m[p.addr] == 0x80 and p.len == 0:     # 034B -> fm_effect_off 2A1D
                self.voice_mute(p)
                self.active = False
        return self.out


def model_run(mdata, na_list):
    """run the model for len(na_list) clocks; returns list of write lists (index 0 = mstart)."""
    md = Model(bytearray(mdata))
    md.out = []
    md.start_writes()
    res = [md.out]
    for na in na_list:
        res.append(md.clock(na))
    return res


def main(argv):
    if not argv:
        print(__doc__)
        return 1
    ticks = 2000
    if '--ticks' in argv:
        i = argv.index('--ticks'); ticks = int(argv[i + 1]); del argv[i:i + 2]
    if argv[0] == '--check-se':
        import os
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import pmdemu
        efc = open(argv[1], 'rb').read()
        nums = [int(x) for x in argv[2:]] or list(range(1, 51))
        ok_all = True
        for n in nums:
            ok_all &= check_se(pmdemu, efc, n, ticks)
        return 0 if ok_all else 2
    if argv[0] == '--check':
        import importlib.util, os
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import pmdemu
        ok_all = True
        ev = {}
        while '--event' in argv:
            i = argv.index('--event'); c, ax = argv[i + 1].split(':'); del argv[i:i + 2]
            ev[int(c)] = int(ax, 16)
        for path in argv[1:]:
            ok_all &= check(pmdemu, path, ticks, ev)
        return 0 if ok_all else 2
    data = open(argv[0], 'rb').read()[1:]
    for i, wl in enumerate(model_run(data, [1] * ticks)):
        print(i, ' '.join('%02X=%02X' % rv for rv in wl))
    return 0


def trace_ticks(pmdemu, path, ticks, events=None):
    """run the real driver (pmdemu.PMDEmu) and split its register writes per Timer-B clock.
    returns [(0, mstart writes), (na, writes of B clock 1), ...]; na = Timer-A IRQs since the
    previous B clock (= PMD's 3119h-30B7h).  In an IRQ with both flags PMD runs B first."""
    e = pmdemu.PMDEmu()
    e.load_music(open(path, 'rb').read())
    n0 = len(e.writes)
    e.mstart()
    res = [(0, [(w[3], w[4]) for w in e.writes[n0:]])]
    na = 0
    events = events or {}
    while len(res) <= ticks:
        if len(res) - 1 in events:
            e.int60(events.pop(len(res) - 1))
        n1 = len(e.writes)
        kind = e.step()
        if 'B' in kind:
            res.append((na, [(w[3], w[4]) for w in e.writes[n1:]]))
            na = 0
        if 'A' in kind:
            na += 1
    return res


def check_se(pmdemu, efc, n, ticks):
    """compare effect n: real driver (se_on without a song) vs EffectModel, per Timer-A tick.
    SSG registers (00-0Fh) are ignored: command D4 (n) starts PMD's built-in SSG effects,
    which are not modelled here."""
    e = pmdemu.PMDEmu()
    e.load_efc(efc)
    e.se_on(n)
    md = EffectModel(efc, n)
    bad = 0; nw = 0
    for i in range(ticks):
        n1 = len(e.writes)
        kind = e.step()
        if 'A' not in kind:
            continue
        got = [(w[3], w[4]) for w in e.writes[n1:] if w[3] >= 0x10 and
               not (w[3] == 0x27 and w[4] in (0x3F, 0x7F))]
        exp = [rv for rv in md.clock() if rv[0] >= 0x10]
        nw += len(got)
        if got != exp:
            bad += 1
            if bad <= 2:
                print('  se %d A-tick %d differs:\n    emu  : %s\n    model: %s' % (
                    n, i, ' '.join('%02X=%02X' % rv for rv in got),
                    ' '.join('%02X=%02X' % rv for rv in exp)))
        if not md.active and not e.effect_active():
            break
    print('se %2d: %d FM writes, %d ticks differ' % (n, nw, bad))
    return bad == 0


def check(pmdemu, path, ticks, events=None):
    """compare the model against the emulated driver clock by clock.  events = {clock: AX}
    INT 60h calls made after that many clocks (e.g. 0x0214 = fade speed 20h, 0x1940 =
    set fade volume 40h); the model applies AH=02h/19h the same way."""
    trace = trace_ticks(pmdemu, path, ticks, dict(events or {}))
    data = open(path, 'rb').read()[1:]
    na = [t[0] for t in trace[1:]]
    md = Model(bytearray(data))
    md.start_writes()
    mod = [md.out]
    for i, n in enumerate(na):
        ax = (events or {}).get(i)
        if ax is not None:
            if ax >> 8 == 0x02:
                md.fadesp = ax & 0xFF
            elif ax >> 8 == 0x19:
                md.fade = ax & 0xFF
        mod.append(md.clock(n))
    bad = 0
    nw = 0
    for i, (t, m) in enumerate(zip(trace, mod)):
        e = [rv for rv in t[1] if rv[0] != 0x27 or i == 0 or True]
        e = [rv for rv in e if not (rv[0] == 0x27 and rv[1] in (0x3F, 0x7F))] if i else e
        mm = [rv for rv in m if not (rv[0] == 0x27 and rv[1] in (0x3F, 0x7F))] if i else m
        nw += len(e)
        if e != mm:
            bad += 1
            if bad <= 3:
                print('  %s clock %d differs:\n    emu  : %s\n    model: %s' % (
                    os.path.basename(path) if False else path.split('/')[-1], i,
                    ' '.join('%02X=%02X' % rv for rv in e), ' '.join('%02X=%02X' % rv for rv in mm)))
    print('%s: %d clocks, %d register writes, %d clocks differ' % (path.split('/')[-1], len(trace) - 1,
                                                                   nw, bad))
    return bad == 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
