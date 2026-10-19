#!/usr/bin/env python3
"""Decode the current level's part table from a DOSBox-X MEMDUMP (text) or MEMDUMPBIN of its segment.

Usage:
  In the debugger:  D <DS>:6638      -> bytes 0-3 = far ptr to header, 4-7 = far ptr to table
                    MEMDUMP <tableseg-0200>:0000 FFFF   (names/icons may sit below or above the table)
  Then:             python parts_table.py MEMDUMP.TXT <dumpseg_hex> <tableseg_hex>:<tableoff_hex> [<count>]
  e.g.              python parts_table.py auto_L3.txt 5A5F 5C5F:0041

Each entry is 0x8E bytes:  +0x08 group (= vehicle slot)  +0x0A quality tier within the group (same tier = interchangeable copy)  +0x1E/+0x26 likely x/y draw position on the vehicle picture (not performance)
+0x2E unknown (not the slot; the group at +0x08 identifies the slot)
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
    path, seg = sys.argv[1], int(sys.argv[2], 16)
    if ':' in sys.argv[3]:
        tseg, toff = (int(x, 16) for x in sys.argv[3].split(':'))
    else:                                    # old form: table offset within the dump segment
        tseg, toff = seg, int(sys.argv[3], 16)
    d = load(path)
    lin = lambda s, o: (s - seg) * 16 + o
    off = lin(tseg, toff)
    count = int(sys.argv[4]) if len(sys.argv) > 4 else None
    if count is None:                        # header usually sits at tableseg:0004, count at +2
        count = struct.unpack_from('<H', d, lin(tseg, 0x0006))[0]
    print('id\tf2e\tgroup\tidx\tdrawX\tdrawY\tname')
    for k in range(count):
        e = off + 0x8E * k
        w = lambda o: struct.unpack_from('<H', d, e + o)[0]
        n = lin(w(0x88), w(0x86))
        name = d[n:d.index(b'\0', n)].decode('latin-1') if 0 <= n < len(d) else '<outside dump>'
        name = ' '.join(name.split())
        print(f'{w(0x8C)}\t{w(0x2E)}\t{w(0x08):#04x}\t{w(0x0A)}\t{w(0x1E)}\t{w(0x26)}\t{name}')


if __name__ == '__main__':
    main()
