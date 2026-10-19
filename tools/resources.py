#!/usr/bin/env python3
"""Read resources from Gizmos & Gadgets MR archives (gizmo000-003.dat), decompressing with the game's own
routine (R0B48_02AA) run under tools/x86emu.py.

Resource id = (archive << 12) | index.   Archive layout:
  "MRn\\x1a", then u32 offsets[]; resource i spans offsets[i]..offsets[i+1]
  at offsets[i]: 4 bytes (unused here), type byte, compressed flag byte, payload
  compressed payload: u32 (unpacked size, top 2 bits are flags) + bitstream

Usage:
  python resources.py GAMEDIR list [archive]
  python resources.py GAMEDIR get <id_hex> [out_file]
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
from x86emu import CPU  # noqa: E402

LOAD = 0x1000            # where we load ssg.exe's image
DGROUP = 0x0EA7
DECOMP = (0x0B48, 0x02AA)
STUB_READ = (0x0000, 0x23F7)
STUB_MALLOC = (0x0536, 0x0007)
STUB_POLL = (0x098D, 0x00D4)
OUT_SEG = 0x7000
HEAP_SEG = 0x9000
SENTINEL = (0xF000, 0x0000)


class Archives:
    def __init__(self, gamedir):
        self.gamedir = gamedir
        self.files = []
        for n in range(4):
            d = open(os.path.join(gamedir, 'gizmo%03d.dat' % n), 'rb').read()
            first = struct.unpack_from('<I', d, 4)[0]
            offs = list(struct.unpack_from('<%dI' % ((first - 4) // 4), d, 4))
            self.files.append((d, offs))
        self.cpu = None

    def raw(self, rid):
        d, offs = self.files[rid >> 12]
        i = rid & 0xFFF
        start = offs[i]
        end = offs[i + 1] if i + 1 < len(offs) else len(d)
        return d[start + 4:end + 4] if i + 1 < len(offs) else d[start + 4:]

    def info(self, rid):
        r = self.raw(rid)
        return r[0], r[1], len(r)

    # ---------- emulator ----------
    def _cpu(self):
        if self.cpu:
            return self.cpu
        exe = open(os.path.join(self.gamedir, 'ssg.exe'), 'rb').read()
        lastp, pages, nrel, hdr = struct.unpack_from('<HHHH', exe, 2)
        relo = struct.unpack_from('<H', exe, 0x18)[0]
        end = (pages - 1) * 512 + lastp if lastp else pages * 512
        img = bytearray(exe[hdr * 16:end])
        for k in range(nrel):
            off, seg = struct.unpack_from('<HH', exe, relo + 4 * k)
            p = seg * 16 + off
            struct.pack_into('<H', img, p, (struct.unpack_from('<H', img, p)[0] + LOAD) & 0xFFFF)
        c = CPU()
        c.mem[LOAD * 16:LOAD * 16 + len(img)] = img
        c.image_len = len(img)
        c.stubs[(LOAD + STUB_READ[0], STUB_READ[1])] = self._stub_read
        c.stubs[(LOAD + STUB_MALLOC[0], STUB_MALLOC[1])] = self._stub_malloc
        c.stubs[(LOAD + STUB_POLL[0], STUB_POLL[1])] = lambda cpu: (cpu.r.__setitem__('ax', 0), cpu.far_ret())
        self.cpu = c
        return c

    def _stub_read(self, c):
        sp, ss = c.r['sp'], c.s['ss']
        off, seg, n = c.rw(ss, sp + 6), c.rw(ss, sp + 8), c.rw(ss, sp + 10)
        chunk = self._stream[self._pos:self._pos + n]
        self._pos += len(chunk)
        base = c.lin(seg, off)
        c.mem[base:base + len(chunk)] = chunk
        c.r['ax'] = len(chunk)
        c.far_ret()

    def _stub_malloc(self, c):
        sp, ss = c.r['sp'], c.s['ss']
        size = c.rw(ss, sp + 4) | (c.rw(ss, sp + 6) << 16)
        seg = self._heap
        self._heap += (size + 15) // 16 + 1
        c.r['ax'], c.r['dx'] = 0, seg
        c.far_ret()

    def get(self, rid):
        r = self.raw(rid)
        typ, comp, payload = r[0], r[1], r[2:]
        if not comp:
            return typ, payload
        size = struct.unpack_from('<I', payload, 0)[0] & 0x3FFFFFFF
        c = self._cpu()
        c.mem[LOAD * 16 + c.image_len:] = bytes(len(c.mem) - LOAD * 16 - c.image_len)   # clear BSS/heap
        # restore initialised DGROUP data each call (decompressor may modify tables)
        self.cpu = None
        c = self._cpu()
        self._stream, self._pos, self._heap = payload, 0, HEAP_SEG
        c.s.update(ss=0xE000, ds=LOAD + DGROUP, es=LOAD + DGROUP)
        c.r.update(sp=0xFFF0, bp=0)
        for v in (OUT_SEG, 0x0000, 1):          # R0B48_02AA(handle, out far ptr)
            c.push(v)
        c.push(SENTINEL[0]); c.push(SENTINEL[1])
        c.s['cs'], c.ip = LOAD + DECOMP[0], DECOMP[1]
        c.run(SENTINEL)
        base = OUT_SEG * 16
        return typ, bytes(c.mem[base:base + size])


def main():
    gamedir, cmd = sys.argv[1], sys.argv[2]
    a = Archives(gamedir)
    if cmd == 'list':
        arcs = [int(sys.argv[3])] if len(sys.argv) > 3 else range(4)
        for n in arcs:
            for i in range(len(a.files[n][1]) - 1):
                rid = (n << 12) | i
                t, comp, ln = a.info(rid)
                print('%04X\t%s\t%d\t%d' % (rid, chr(t) if 32 <= t < 127 else '%02X' % t, comp, ln))
    elif cmd == 'get':
        rid = int(sys.argv[3], 16)
        t, data = a.get(rid)
        out = sys.argv[4] if len(sys.argv) > 4 else None
        if out:
            open(out, 'wb').write(data)
        print('type %r, %d bytes' % (chr(t), len(data)))


if __name__ == '__main__':
    main()
