#!/usr/bin/env python3
"""Static warehouse layout per level: rooms, part boxes and puzzle doors (no live session needed).

Usage: python warehouse.py GAMEDIR > data/warehouse_counts.tsv

Level setup R0622_0085 (called from R02E0_0820), v = building*5 + level:
- rooms   = resource DS:09E8[v] ('A'): u16 room count, then per room a 0x120-byte header followed by its object
            lists. Header counts: +0x02+2i (i=3..0) x 32-byte objects (doors etc., ptr +0x7E+4i), +0x0A+2i (i=0..3)
            x 0x48, then +0x12/+0x36/+0x5A + 2i (i=0..5) x 10 bytes.
- boxes   = resource 0x350+v ('I'): u16 total, then u16 box count per room (room 0 skipped).
            Boxes are placed by R0622_0A76(room, count, -1); contents come from R053F_01B6: a shuffled
            permutation of 0..total-1 where values >= part count become 1000 (empty). So every part of the level is
            in exactly one box and the rest are empty. Counts are fixed; only placement and contents are random.
- doors   = 32-byte objects with state (+0x10) 7 (locked, needs a puzzle) or 9 (open). Each door appears in both
            rooms it connects (link +0x12: low nibble = destination room, >>6 = door index there) and is opened on
            both sides by one puzzle (R02E0_0A24), so puzzle doors = locked sides / 2.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
from resources import Archives  # noqa: E402

DG = 0x1200 + 0xEA7 * 16
BUILDINGS = ['Automotive', 'Aircraft', 'Alternative Energy']
W = lambda d, o: struct.unpack_from('<H', d, o)[0]


def rooms(r):
    out, p = [], 2
    for _ in range(W(r, 0)):
        h = r[p:p + 0x120]; p += 0x120
        objs = []
        for i in (3, 2, 1, 0):
            c = W(h, 2 + 2 * i)
            if i == 0:
                objs = [r[p + 32 * k:p + 32 * k + 32] for k in range(c)]
            p += 32 * c
        p += 0x48 * sum(W(h, 0xA + 2 * i) for i in range(4))
        p += 10 * sum(W(h, b + 2 * i) for b in (0x12, 0x36, 0x5A) for i in range(6))
        out.append(objs)
    assert p == len(r), 'room resource not fully parsed'
    return out


def level(a, exe, v, part_count):
    t, r = a.get(W(exe, DG + 0x9E8 + 2 * v))
    rs = rooms(r)
    locked = sum(W(o, 0x10) == 7 for objs in rs for o in objs)
    opened = sum(W(o, 0x10) == 9 for objs in rs for o in objs)
    assert locked % 2 == 0
    t, b = a.get(0x350 + v)
    per = struct.unpack_from('<%dH' % (len(b) // 2), b)
    assert sum(per[1:len(rs)]) == per[0]
    return dict(rooms=len(rs), boxes=per[0], parts=part_count, empty=per[0] - part_count,
                doors=locked // 2, open_doors=opened // 2, boxes_per_room=per[1:len(rs)])


def main():
    gamedir = sys.argv[1]
    a = Archives(gamedir)
    exe = open(os.path.join(gamedir, 'ssg.exe'), 'rb').read()
    parts = {}
    for line in open(os.path.join(os.path.dirname(__file__), '..', 'data', 'parts_all.tsv')).read().splitlines()[1:]:
        v = int(line.split('\t')[0]); parts[v] = parts.get(v, 0) + 1
    print('vehicle\tbuilding\tlevel\trooms\tboxes\tparts\tempty_boxes\tpuzzle_doors\topen_doors\tboxes_per_room')
    tot = dict(boxes=0, parts=0, empty=0, doors=0)
    for v in range(15):
        L = level(a, exe, v, parts[v])
        for k in tot:
            tot[k] += L[k]
        print(f"{v}\t{BUILDINGS[v // 5]}\t{v % 5 + 1}\t{L['rooms']}\t{L['boxes']}\t{L['parts']}\t{L['empty']}\t"
              f"{L['doors']}\t{L['open_doors']}\t{','.join(map(str, L['boxes_per_room']))}")
    print(f"total\t\t\t\t{tot['boxes']}\t{tot['parts']}\t{tot['empty']}\t{tot['doors']}", file=sys.stderr)


if __name__ == '__main__':
    main()
