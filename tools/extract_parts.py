#!/usr/bin/env python3
"""Extract every vehicle's part table from the game's resources (no live dumps needed).

Usage: python extract_parts.py GAMEDIR > data/parts_all.tsv

Part table for vehicle v (0-14) is resource 0x2F5+v; v = building*5 + (level-1),
building 0 Automotive, 1 Aircraft, 2 Alternative Energy (rec+0x2E holds the current v).
Table = header (0x2D bytes; +2 part count, +6 group count) + groups*2 + count * 0x8E-byte entries.
Entry: +0x08 group (slot), +0x0A tier, +0x16 name resource id, +0x1E/+0x26 likely x/y draw position (not stats), +0x8C part id.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
from resources import Archives  # noqa: E402

COSMETIC = {0x0B, 0x0C}   # paint, decals: no performance value -> AP filler
BUILDINGS = ['Automotive', 'Aircraft', 'Alternative Energy']


def tables(a):
    for v in range(15):
        t, d = a.get(0x2F5 + v)
        count, groups = struct.unpack_from('<H', d, 2)[0], struct.unpack_from('<H', d, 6)[0]
        base = 0x2D + groups * 2
        rows = []
        for k in range(count):
            e = base + 0x8E * k
            w = lambda o: struct.unpack_from('<H', d, e + o)[0]
            nt, name = a.get(w(0x16))
            rows.append(dict(id=k, group=w(0x08), tier=w(0x0A), name_res=w(0x16),
                             stat1e=w(0x1E), stat26=w(0x26),
                             name=' '.join(name.split(b'\0')[0].decode('latin-1').split())))
        yield v, struct.unpack_from('<4H', d, 0), rows


def main():
    a = Archives(sys.argv[1])
    print('vehicle\tbuilding\tlevel\tid\tgroup\ttier\tdrawX\tdrawY\tcosmetic\tname')
    for v, hdr, rows in tables(a):
        for r in rows:
            print(f"{v}\t{BUILDINGS[v // 5]}\t{v % 5 + 1}\t{r['id']}\t{r['group']:#04x}\t{r['tier']}\t"
                  f"{r['stat1e']}\t{r['stat26']}\t{int(r['group'] in COSMETIC)}\t{r['name']}")


if __name__ == '__main__':
    main()
