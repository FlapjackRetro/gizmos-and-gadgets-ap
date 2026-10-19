"""Minimal real-mode 8086 interpreter, just enough to run Gizmos & Gadgets' own routines (e.g. the resource
decompressor) on extracted data. Far calls to selected addresses can be intercepted by Python stubs.

Not a general emulator: no interrupts, no I/O ports, no 286+ instructions beyond the few Borland emits
(push imm, imul imm, shifts by imm, enter/leave).
"""
import struct

MASK = {8: 0xFF, 16: 0xFFFF}
SIGN = {8: 0x80, 16: 0x8000}


class Halt(Exception):
    pass


class CPU:
    REG16 = ['ax', 'cx', 'dx', 'bx', 'sp', 'bp', 'si', 'di']
    SREG = ['es', 'cs', 'ss', 'ds']

    def __init__(self):
        self.mem = bytearray(0x110000)
        self.r = dict.fromkeys(self.REG16, 0)
        self.s = dict.fromkeys(self.SREG, 0)
        self.ip = 0
        self.flags = dict(c=0, z=0, s=0, o=0, a=0, p=0, d=0)
        self.stubs = {}          # (cs, ip) -> python callable(cpu); must perform the return itself
        self.steps = 0

    # ---------- memory ----------
    def lin(self, seg, off):
        return ((seg << 4) + (off & 0xFFFF)) & 0xFFFFF

    def rb(self, seg, off):
        return self.mem[self.lin(seg, off)]

    def rw(self, seg, off):
        return self.rb(seg, off) | (self.rb(seg, off + 1) << 8)

    def wb(self, seg, off, v):
        self.mem[self.lin(seg, off)] = v & 0xFF

    def ww(self, seg, off, v):
        self.wb(seg, off, v)
        self.wb(seg, off + 1, v >> 8)

    # ---------- registers ----------
    def g8(self, i):
        v = self.r[self.REG16[i & 3]]
        return (v >> 8) & 0xFF if i & 4 else v & 0xFF

    def s8(self, i, v):
        n = self.REG16[i & 3]
        if i & 4:
            self.r[n] = (self.r[n] & 0x00FF) | ((v & 0xFF) << 8)
        else:
            self.r[n] = (self.r[n] & 0xFF00) | (v & 0xFF)

    def g16(self, i):
        return self.r[self.REG16[i]]

    def s16(self, i, v):
        self.r[self.REG16[i]] = v & 0xFFFF

    def greg(self, i, w):
        return self.g16(i) if w else self.g8(i)

    def sreg(self, i, v, w):
        self.s16(i, v) if w else self.s8(i, v)

    # ---------- stack ----------
    def push(self, v):
        self.r['sp'] = (self.r['sp'] - 2) & 0xFFFF
        self.ww(self.s['ss'], self.r['sp'], v)

    def pop(self):
        v = self.rw(self.s['ss'], self.r['sp'])
        self.r['sp'] = (self.r['sp'] + 2) & 0xFFFF
        return v

    # ---------- fetch ----------
    def fb(self):
        v = self.rb(self.s['cs'], self.ip)
        self.ip = (self.ip + 1) & 0xFFFF
        return v

    def fw(self):
        v = self.rw(self.s['cs'], self.ip)
        self.ip = (self.ip + 2) & 0xFFFF
        return v

    def fsb(self):
        v = self.fb()
        return v - 0x100 if v & 0x80 else v

    # ---------- modrm ----------
    def modrm(self, seg_override):
        m = self.fb()
        mod, reg, rm = m >> 6, (m >> 3) & 7, m & 7
        if mod == 3:
            return reg, ('reg', rm)
        r = self.r
        bases = [r['bx'] + r['si'], r['bx'] + r['di'], r['bp'] + r['si'], r['bp'] + r['di'],
                 r['si'], r['di'], r['bp'], r['bx']]
        if mod == 0 and rm == 6:
            off = self.fw()
            dseg = 'ds'
        else:
            off = bases[rm]
            dseg = 'ss' if rm in (2, 3, 6) else 'ds'
            if mod == 1:
                off += self.fsb()
            elif mod == 2:
                off += self.fw()
        seg = self.s[seg_override] if seg_override else self.s[dseg]
        return reg, ('mem', seg, off & 0xFFFF)

    def get(self, ea, w):
        if ea[0] == 'reg':
            return self.greg(ea[1], w)
        return self.rw(ea[1], ea[2]) if w else self.rb(ea[1], ea[2])

    def put(self, ea, v, w):
        if ea[0] == 'reg':
            self.sreg(ea[1], v, w)
        elif w:
            self.ww(ea[1], ea[2], v)
        else:
            self.wb(ea[1], ea[2], v)

    # ---------- flags / alu ----------
    def szp(self, v, w):
        v &= MASK[16 if w else 8]
        self.flags['z'] = int(v == 0)
        self.flags['s'] = int(bool(v & SIGN[16 if w else 8]))
        self.flags['p'] = int(bin(v & 0xFF).count('1') % 2 == 0)
        return v

    def alu(self, op, a, b, w):
        bits = 16 if w else 8
        M, S = MASK[bits], SIGN[bits]
        f = self.flags
        if op == 0 or op == 2:                    # add, adc
            c = f['c'] if op == 2 else 0
            res = a + b + c
            f['c'] = int(res > M)
            f['o'] = int(bool((~(a ^ b) & (a ^ res)) & S))
            f['a'] = int(((a ^ b ^ res) & 0x10) != 0)
            return self.szp(res, w)
        if op == 5 or op == 3 or op == 7:          # sub, sbb, cmp
            c = f['c'] if op == 3 else 0
            res = a - b - c
            f['c'] = int(res < 0)
            f['o'] = int(bool(((a ^ b) & (a ^ res)) & S))
            f['a'] = int(((a ^ b ^ res) & 0x10) != 0)
            return self.szp(res, w)
        if op == 1:
            res = a | b
        elif op == 4:
            res = a & b
        elif op == 6:
            res = a ^ b
        f['c'] = f['o'] = 0
        return self.szp(res, w)

    def shift(self, op, v, n, w):
        bits = 16 if w else 8
        M, S = MASK[bits], SIGN[bits]
        n &= 0x1F
        if n == 0:
            return v
        f = self.flags
        for _ in range(n):
            if op == 0:                 # rol
                c = (v & S) != 0
                v = ((v << 1) | c) & M
                f['c'] = int(c)
            elif op == 1:               # ror
                c = v & 1
                v = (v >> 1) | (c * S)
                f['c'] = c
            elif op == 2:               # rcl
                c = (v & S) != 0
                v = ((v << 1) | f['c']) & M
                f['c'] = int(c)
            elif op == 3:               # rcr
                c = v & 1
                v = (v >> 1) | (f['c'] * S)
                f['c'] = c
            elif op in (4, 6):          # shl
                f['c'] = int((v & S) != 0)
                v = (v << 1) & M
            elif op == 5:               # shr
                f['c'] = v & 1
                v >>= 1
            elif op == 7:               # sar
                f['c'] = v & 1
                v = (v >> 1) | (v & S)
        if op >= 4:
            self.szp(v, w)
        f['o'] = int(bool((v ^ (v << 1)) & S)) if op in (4, 6) else f['o']
        return v

    def cond(self, c):
        f = self.flags
        t = [f['o'], f['c'], f['z'], f['c'] or f['z'], f['s'], f['p'],
             f['s'] != f['o'], f['z'] or (f['s'] != f['o'])][c >> 1]
        return bool(t) ^ bool(c & 1)

    # ---------- execution ----------
    def run(self, until, max_steps=50_000_000):
        while True:
            key = (self.s['cs'], self.ip)
            if key == until:
                return
            if key in self.stubs:
                self.stubs[key](self)
                continue
            self.step()
            self.steps += 1
            if self.steps > max_steps:
                raise Halt('step limit')

    def far_ret(self, extra=0):
        self.ip = self.pop()
        self.s['cs'] = self.pop()
        self.r['sp'] = (self.r['sp'] + extra) & 0xFFFF

    def step(self):
        seg_ov = None
        rep = None
        start = (self.s['cs'], self.ip)
        while True:
            op = self.fb()
            if op in (0x26, 0x2E, 0x36, 0x3E):
                seg_ov = {0x26: 'es', 0x2E: 'cs', 0x36: 'ss', 0x3E: 'ds'}[op]
            elif op in (0xF2, 0xF3):
                rep = op
            else:
                break
        r, s, f = self.r, self.s, self.flags

        if op < 0x40 and (op & 7) < 6:              # alu r/m, r / r, r/m / acc, imm
            alu_op, kind = op >> 3, op & 7
            w = kind & 1
            if kind < 4:
                reg, ea = self.modrm(seg_ov)
                if kind & 2:
                    a, b = self.greg(reg, w), self.get(ea, w)
                    res = self.alu(alu_op, a, b, w)
                    if alu_op != 7:
                        self.sreg(reg, res, w)
                else:
                    a, b = self.get(ea, w), self.greg(reg, w)
                    res = self.alu(alu_op, a, b, w)
                    if alu_op != 7:
                        self.put(ea, res, w)
            else:
                b = self.fw() if w else self.fb()
                a = r['ax'] if w else r['ax'] & 0xFF
                res = self.alu(alu_op, a, b, w)
                if alu_op != 7:
                    self.sreg(0, res, w)
            return
        if op in (0x06, 0x0E, 0x16, 0x1E):
            self.push(s[self.SREG[(op >> 3) & 3]]); return
        if op in (0x07, 0x17, 0x1F):
            s[self.SREG[(op >> 3) & 3]] = self.pop(); return
        if 0x40 <= op <= 0x4F:                       # inc/dec r16
            i = op & 7
            c = f['c']
            v = self.alu(0 if op < 0x48 else 5, self.g16(i), 1, 1)
            f['c'] = c
            self.s16(i, v); return
        if 0x50 <= op <= 0x57:
            self.push(self.g16(op & 7)); return
        if 0x58 <= op <= 0x5F:
            self.s16(op & 7, self.pop()); return
        if op == 0x68:
            self.push(self.fw()); return
        if op == 0x6A:
            self.push(self.fsb() & 0xFFFF); return
        if op in (0x69, 0x6B):                        # imul r, r/m, imm
            reg, ea = self.modrm(seg_ov)
            a = self.get(ea, 1)
            a = a - 0x10000 if a & 0x8000 else a
            b = self.fw() if op == 0x69 else self.fsb()
            if op == 0x69 and b & 0x8000:
                b -= 0x10000
            res = a * b
            f['c'] = f['o'] = int(not -0x8000 <= res <= 0x7FFF)
            self.s16(reg, res & 0xFFFF); return
        if 0x70 <= op <= 0x7F:
            d = self.fsb()
            if self.cond(op & 0xF):
                self.ip = (self.ip + d) & 0xFFFF
            return
        if op in (0x80, 0x81, 0x83):
            reg, ea = self.modrm(seg_ov)
            w = op != 0x80
            b = self.fsb() & 0xFFFF if op == 0x83 else (self.fw() if op == 0x81 else self.fb())
            res = self.alu(reg, self.get(ea, w), b, w)
            if reg != 7:
                self.put(ea, res, w)
            return
        if op in (0x84, 0x85):
            reg, ea = self.modrm(seg_ov)
            w = op & 1
            self.alu(4, self.get(ea, w), self.greg(reg, w), w); return
        if op in (0x86, 0x87):
            reg, ea = self.modrm(seg_ov)
            w = op & 1
            a, b = self.get(ea, w), self.greg(reg, w)
            self.put(ea, b, w); self.sreg(reg, a, w); return
        if 0x88 <= op <= 0x8B:
            reg, ea = self.modrm(seg_ov)
            w = op & 1
            if op & 2:
                self.sreg(reg, self.get(ea, w), w)
            else:
                self.put(ea, self.greg(reg, w), w)
            return
        if op == 0x8C:
            reg, ea = self.modrm(seg_ov)
            self.put(ea, s[self.SREG[reg & 3]], 1); return
        if op == 0x8D:
            reg, ea = self.modrm(seg_ov)
            self.s16(reg, ea[2]); return
        if op == 0x8E:
            reg, ea = self.modrm(seg_ov)
            s[self.SREG[reg & 3]] = self.get(ea, 1); return
        if op == 0x8F:
            reg, ea = self.modrm(seg_ov)
            self.put(ea, self.pop(), 1); return
        if op == 0x90:
            return
        if 0x91 <= op <= 0x97:
            i = op & 7
            r['ax'], v = self.g16(i), r['ax']
            self.s16(i, v); return
        if op == 0x98:
            r['ax'] = (r['ax'] & 0xFF) | (0xFF00 if r['ax'] & 0x80 else 0); return
        if op == 0x99:
            r['dx'] = 0xFFFF if r['ax'] & 0x8000 else 0; return
        if op == 0x9A:
            ip, cs = self.fw(), self.fw()
            self.push(s['cs']); self.push(self.ip)
            s['cs'], self.ip = cs, ip; return
        if op in (0x9C, 0x9D, 0xFA, 0xFB, 0x9B):
            if op == 0x9C:
                self.push(0)
            elif op == 0x9D:
                self.pop()
            return
        if 0xA0 <= op <= 0xA3:
            off = self.fw()
            seg = s[seg_ov] if seg_ov else s['ds']
            w = op & 1
            if op < 0xA2:
                self.sreg(0, self.rw(seg, off) if w else self.rb(seg, off), w)
            elif w:
                self.ww(seg, off, r['ax'])
            else:
                self.wb(seg, off, r['ax'])
            return
        if op in (0xA4, 0xA5, 0xAA, 0xAB, 0xAC, 0xAD):
            w = op & 1
            n = 2 if w else 1
            d = -n if f['d'] else n
            count = r['cx'] if rep else 1
            src = s[seg_ov] if seg_ov else s['ds']
            for _ in range(count):
                if op in (0xA4, 0xA5):
                    v = self.rw(src, r['si']) if w else self.rb(src, r['si'])
                    (self.ww if w else self.wb)(s['es'], r['di'], v)
                    r['si'] = (r['si'] + d) & 0xFFFF; r['di'] = (r['di'] + d) & 0xFFFF
                elif op in (0xAA, 0xAB):
                    (self.ww if w else self.wb)(s['es'], r['di'], r['ax'] if w else r['ax'] & 0xFF)
                    r['di'] = (r['di'] + d) & 0xFFFF
                else:
                    v = self.rw(src, r['si']) if w else self.rb(src, r['si'])
                    self.sreg(0, v, w)
                    r['si'] = (r['si'] + d) & 0xFFFF
            if rep:
                r['cx'] = 0
            return
        if op in (0xA8, 0xA9):
            w = op & 1
            b = self.fw() if w else self.fb()
            self.alu(4, r['ax'] if w else r['ax'] & 0xFF, b, w); return
        if 0xB0 <= op <= 0xB7:
            self.s8(op & 7, self.fb()); return
        if 0xB8 <= op <= 0xBF:
            self.s16(op & 7, self.fw()); return
        if op in (0xC0, 0xC1, 0xD0, 0xD1, 0xD2, 0xD3):
            reg, ea = self.modrm(seg_ov)
            w = op & 1
            n = self.fb() if op in (0xC0, 0xC1) else (1 if op in (0xD0, 0xD1) else r['cx'] & 0xFF)
            self.put(ea, self.shift(reg, self.get(ea, w), n, w), w); return
        if op == 0xC2:
            n = self.fw(); self.ip = self.pop(); r['sp'] = (r['sp'] + n) & 0xFFFF; return
        if op == 0xC3:
            self.ip = self.pop(); return
        if op in (0xC4, 0xC5):
            reg, ea = self.modrm(seg_ov)
            self.s16(reg, self.rw(ea[1], ea[2]))
            s['es' if op == 0xC4 else 'ds'] = self.rw(ea[1], ea[2] + 2); return
        if op in (0xC6, 0xC7):
            reg, ea = self.modrm(seg_ov)
            w = op & 1
            self.put(ea, self.fw() if w else self.fb(), w); return
        if op == 0xC8:
            size, lvl = self.fw(), self.fb()
            self.push(r['bp']); r['bp'] = r['sp']; r['sp'] = (r['sp'] - size) & 0xFFFF; return
        if op == 0xC9:
            r['sp'] = r['bp']; r['bp'] = self.pop(); return
        if op == 0xCA:
            n = self.fw(); self.far_ret(n); return
        if op == 0xCB:
            self.far_ret(); return
        if op == 0xE2:
            d = self.fsb(); r['cx'] = (r['cx'] - 1) & 0xFFFF
            if r['cx']:
                self.ip = (self.ip + d) & 0xFFFF
            return
        if op == 0xE3:
            d = self.fsb()
            if r['cx'] == 0:
                self.ip = (self.ip + d) & 0xFFFF
            return
        if op == 0xE8:
            d = self.fw(); self.push(self.ip); self.ip = (self.ip + d) & 0xFFFF; return
        if op == 0xE9:
            d = self.fw(); self.ip = (self.ip + d) & 0xFFFF; return
        if op == 0xEA:
            ip, cs = self.fw(), self.fw(); self.ip, s['cs'] = ip, cs; return
        if op == 0xEB:
            d = self.fsb(); self.ip = (self.ip + d) & 0xFFFF; return
        if op in (0xF8, 0xF9):
            f['c'] = op & 1; return
        if op in (0xFC, 0xFD):
            f['d'] = op & 1; return
        if op == 0xF5:
            f['c'] ^= 1; return
        if op in (0xF6, 0xF7):
            reg, ea = self.modrm(seg_ov)
            w = op & 1
            v = self.get(ea, w)
            if reg == 0:
                self.alu(4, v, self.fw() if w else self.fb(), w)
            elif reg == 2:
                self.put(ea, ~v, w)
            elif reg == 3:
                self.put(ea, self.alu(5, 0, v, w), w); f['c'] = int(v != 0)
            elif reg in (4, 5):
                if w:
                    a, b = r['ax'], v
                    if reg == 5:
                        a = a - 0x10000 if a & 0x8000 else a
                        b = b - 0x10000 if b & 0x8000 else b
                    res = (a * b) & 0xFFFFFFFF
                    r['ax'], r['dx'] = res & 0xFFFF, res >> 16
                    f['c'] = f['o'] = int(r['dx'] != 0)
                else:
                    a, b = r['ax'] & 0xFF, v
                    if reg == 5:
                        a = a - 0x100 if a & 0x80 else a
                        b = b - 0x100 if b & 0x80 else b
                    r['ax'] = (a * b) & 0xFFFF
            elif reg in (6, 7):
                if w:
                    num = (r['dx'] << 16) | r['ax']
                    den = v
                    if reg == 7:
                        num = num - (1 << 32) if num & 0x80000000 else num
                        den = den - 0x10000 if den & 0x8000 else den
                    q = int(num / den) if reg == 7 else num // den
                    rem = num - q * den
                    r['ax'], r['dx'] = q & 0xFFFF, rem & 0xFFFF
                else:
                    num = r['ax']
                    q, rem = divmod(num, v)
                    r['ax'] = (q & 0xFF) | ((rem & 0xFF) << 8)
            return
        if op in (0xFE, 0xFF):
            reg, ea = self.modrm(seg_ov)
            w = op & 1
            if reg in (0, 1):
                c = f['c']
                self.put(ea, self.alu(0 if reg == 0 else 5, self.get(ea, w), 1, w), w)
                f['c'] = c
            elif reg == 2:
                t = self.get(ea, 1); self.push(self.ip); self.ip = t
            elif reg == 3:
                ip, cs = self.rw(ea[1], ea[2]), self.rw(ea[1], ea[2] + 2)
                self.push(s['cs']); self.push(self.ip); s['cs'], self.ip = cs, ip
            elif reg == 4:
                self.ip = self.get(ea, 1)
            elif reg == 5:
                self.ip, s['cs'] = self.rw(ea[1], ea[2]), self.rw(ea[1], ea[2] + 2)
            elif reg == 6:
                self.push(self.get(ea, 1))
            return
        raise Halt('unimplemented opcode %02X at %04X:%04X' % (op, *start))
