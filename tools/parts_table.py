#!/usr/bin/env python3
"""Decode the current level's part table from a DOSBox-X MEMDUMP (text) or MEMDUMPBIN of its segment.

Usage:
  In the debugger:  D <DS>:6638      -> bytes 0-3 = far ptr to header, 4-7 = far ptr to table
                    MEMDUMP <tableseg>:0000 FFFF
  Then:             python parts_table.py MEMDUMP.TXT <tableseg_hex> <table_off_hex> [<count>]

Each entry is 0x8E bytes:  +0x08 group  +0x0A index-in-group  +0x1E/+0x26 performance stats (TBD)
+0x2E slot (0 paint, 1 wheels, 2 brakes, 3 gears, 4 body, 5 decal in Alt-Energy L1)
+0x82 far ptr -> blueprint icon (LPX image)   +0x86 far ptr -> name string   +0x8C part id
The header (far ptr at DS:6638) holds the count at +2.
"""
import re
import struct
import sys


def load(path):
    raw = open(path, 'rb').read()
    if raw[:4].decode('latin-1', 'replace').count(':') or b':' in raw[:10]:
        out = bytearray(0x10000)
        hi = 0
        for line in raw.decode('latin-1').splitlines():
            m = re.match(r'([0-9A-F]{4}):([0-9A-F]{4,8})\s+(.*)', line.strip())
            if not m:
                continue
            off = int(m.group(2), 16)
            bs = [int(x, 16) for x in m.group(3).split()[:16] if re.fullmatch(r'[0-9A-F]{2}', x)]
            out[off:off + len(bs)] = bytes(bs)
            hi = max(hi, off + len(bs))
        return bytes(out[:hi])
    return raw


def main():
    path, seg, off = sys.argv[1], int(sys.argv[2], 16), int(sys.argv[3], 16)
    d = load(path)
    count = int(sys.argv[4]) if len(sys.argv) > 4 else 24
    lin = lambda s, o: (s - seg) * 16 + o
    print('id\tslot\tgroup\tidx\tstat1E\tstat26\tname')
    for k in range(count):
        e = off + 0x8E * k
        w = lambda o: struct.unpack_from('<H', d, e + o)[0]
        n = lin(w(0x88), w(0x86))
        name = d[n:d.index(b'\0', n)].decode('latin-1')
        print(f'{w(0x8C)}\t{w(0x2E)}\t{w(0x08):#04x}\t{w(0x0A)}\t{w(0x1E)}\t{w(0x26)}\t{name}')


if __name__ == '__main__':
    main()
