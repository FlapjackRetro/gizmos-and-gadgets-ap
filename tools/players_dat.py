#!/usr/bin/env python3
"""Inspect Gizmos & Gadgets (DOS, v1.0) PLAYERS.DAT save files.

Usage: python players_dat.py PLAYERS.DAT [-v]

Layout (reverse-engineered from ssg.exe overlay 0E18):
  0x000  char[4]  "USR\\x1a"
  0x004  u16      version (must be 0x000C)
  0x006  u16      number of players in use
  0x008  dir[100] { char name[16]; u32 score; u16 slot; }   (22 bytes each)
  0x8A0  rec[99]  player records, 0x16BD (5821) bytes each
                  record k lives at 0x8A0 + k*0x16BD

While playing, the active record is held verbatim in memory at DGROUP:0x4562.

Per-building block (rec + 0x27E + b*0x6BA), written on save when the building is in progress
(see RE_NOTES §3.1; serializers traced in Ghidra 2026-10-22):
  +0x000  20 bytes   copy of rec+0x1A..0x2D (warehouse header)
  +0x014  {s16 owner, u16 flags} per part of the level  (R053F_0385 / restore R053F_03C9)
                     owner = actor holding the part (0 = player, -1 = nobody / still in a box)
  +0x26C  u8 per locked/open door, rooms 0..15 in order, objects in room order: 7 locked, 9 open
                     (R0622_0FB3 / restore R0622_10E7)
  +0x302  per room 0..15 that exists: u16 count, then count x {u16 row, u16 column, u16 contents}
                     contents = part id or 1000 (empty)
  +0x492  12 actors x 0x2E bytes = actor struct +0x14..+0x41 (positions/animation, not inventory)
                     (R0834_0B5E / restore R0834_0B95)
"""
import csv
import os
import struct
import sys

REC_SIZE = 0x16BD
REC_BASE = 0x8A0
BLOCK_BASE = 0x27E      # first per-building block inside a record
BLOCK_SIZE = 0x6BA      # three blocks, one per engineering building
B_PARTS, B_DOORS, B_BOXES, B_ACTORS = 0x014, 0x26C, 0x302, 0x492
EMPTY_BOX = 1000

BUILDINGS = ["Automotive", "Aircraft", "Alternative Energy"]
RANKS = ["Trainee", "Assistant", "Technician", "Engineer", "Doctor",
         "Professor", "Scientist", "Head Scientist"]


def s16(b, o):
    return struct.unpack_from('<h', b, o)[0]


def u16(b, o):
    return struct.unpack_from('<H', b, o)[0]


def load_part_counts():
    """vehicle index -> number of parts, from data/parts_all.tsv (None if missing)."""
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'data', 'parts_all.tsv')
    try:
        counts = {}
        with open(path, newline='') as f:
            for row in csv.DictReader(f, delimiter='\t'):
                v = int(row['vehicle'])
                counts[v] = counts.get(v, 0) + 1
        return counts
    except OSError:
        return None


def parse_doors(blk):
    raw = blk[B_DOORS:B_BOXES].rstrip(b'\0')
    return list(raw)


def parse_boxes(blk):
    """Stream of per-room groups. Rooms with no data pointer are skipped by the game, so groups
    are in room order but room indices can't be recovered from the save alone."""
    region = blk[B_BOXES:B_ACTORS]
    groups, o = [], 0
    while o + 2 <= len(region) and any(region[o:]):
        n = u16(region, o)
        o += 2
        if n > 40 or o + 6 * n > len(region):
            break                       # not a sane group: stop rather than misparse
        groups.append([struct.unpack_from('<3H', region, o + 6 * i) for i in range(n)])
        o += 6 * n
    return groups


def parse_parts(blk, count):
    return [struct.unpack_from('<hH', blk, B_PARTS + 4 * i) for i in range(count)]


def parse_record(r, part_counts=None):
    out = {
        'name': r[0:16].split(b'\0')[0].decode('latin-1'),
        'score': struct.unpack_from('<I', r, 0x10)[0],
        'slot': s16(r, 0x14),
        'live_warehouse_hdr': r[0x1A:0x2E].hex(),   # copied to/from block+0x00
        'parts_needed': s16(r, 0x20),
        'counter_1c': s16(r, 0x1C),                 # HUD 3-digit counter, see RE_NOTES §3.2
        'vehicle': s16(r, 0x2E),
        'current_building': s16(r, 0x30),
        'current_level': s16(r, 0x32),
        'last_race_won_idx': [s16(r, 0x34 + 2 * i) for i in range(3)],  # -1 none, 4 = all five
        'warehouse_in_progress': [s16(r, 0x3A + 2 * i) for i in range(3)],
        'races_won_total': s16(r, 0x16AC),      # 15 => game complete
        'per_vehicle_byte': list(r[0x16AE:0x16BD]),  # 15 bytes, index = vehicle; see RE_NOTES §3.2
        'buildings': [],
    }
    for b in range(3):
        blk = r[BLOCK_BASE + BLOCK_SIZE * b: BLOCK_BASE + BLOCK_SIZE * (b + 1)]
        level0 = out['last_race_won_idx'][b] + 1        # level the block belongs to (0-based)
        vehicle = b * 5 + level0 if level0 < 5 else None
        n = part_counts.get(vehicle) if (part_counts and vehicle is not None) else None
        doors = parse_doors(blk)
        boxes = parse_boxes(blk)
        info = {
            'in_progress': out['warehouse_in_progress'][b],
            'level': level0 + 1,
            'vehicle': vehicle,
            'hdr': blk[:0x14].hex(),
            'doors_open': doors.count(9),
            'doors_locked': doors.count(7),
            'box_groups': boxes,
            'boxes_left': sum(len(g) for g in boxes),
            'boxes_left_with_part': sum(1 for g in boxes for t in g if t[2] != EMPTY_BOX),
        }
        if n is not None:
            parts = parse_parts(blk, n)
            info['parts'] = parts
            info['parts_held_by_player'] = [i for i, (owner, _) in enumerate(parts) if owner == 0]
            info['parts_held_by_others'] = [(i, owner) for i, (owner, _) in enumerate(parts) if owner > 0]
        out['buildings'].append(info)
    return out


def main(path, verbose=False):
    d = open(path, 'rb').read()
    assert d[:4] == b'USR\x1a', 'not a PLAYERS.DAT'
    ver, count = struct.unpack_from('<HH', d, 4)
    print(f'version={ver:#x} players={count}')
    counts = load_part_counts()
    for k in range(100):
        o = 8 + 22 * k
        name = d[o:o + 16].split(b'\0')[0]
        if not name:
            continue
        score, slot = struct.unpack_from('<IH', d, o + 16)
        rec = d[REC_BASE + REC_SIZE * slot: REC_BASE + REC_SIZE * (slot + 1)]
        p = parse_record(rec, counts)
        print(f"\n[{slot}] {p['name']}  score={p['score']}  races won={p['races_won_total']}/15")
        print(f"   current building={p['current_building']} level={p['current_level'] + 1} vehicle={p['vehicle']}"
              f"  last race idx per bldg={p['last_race_won_idx']}  in-progress={p['warehouse_in_progress']}")
        print(f"   parts needed={p['parts_needed']}  counter(rec+0x1C)={p['counter_1c']}"
              f"  per-vehicle bytes={p['per_vehicle_byte']}")
        for i, b in enumerate(p['buildings']):
            if not b['in_progress']:
                print(f"   {BUILDINGS[i]}: no saved warehouse")
                continue
            line = (f"   {BUILDINGS[i]} L{b['level']}: doors {b['doors_open']} open / {b['doors_locked']} locked;"
                    f" boxes left {b['boxes_left']} ({b['boxes_left_with_part']} with a part)")
            if 'parts' in b:
                line += (f"; player holds parts {b['parts_held_by_player']}"
                         f"; others hold {b['parts_held_by_others']}")
            print(line)
            if verbose:
                for room, g in enumerate(b['box_groups']):
                    print(f"      room group {room}: " + ' '.join(f"r{r}c{c}:{'-' if x == EMPTY_BOX else x}"
                                                          for r, c, x in g))


if __name__ == '__main__':
    args = [a for a in sys.argv[1:] if a != '-v']
    main(args[0] if args else 'PLAYERS.DAT', '-v' in sys.argv)
