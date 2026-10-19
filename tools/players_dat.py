#!/usr/bin/env python3
"""Inspect Gizmos & Gadgets (DOS, v1.0) PLAYERS.DAT save files.

Usage: python players_dat.py PLAYERS.DAT

Layout (reverse-engineered from ssg.exe overlay 0E18):
  0x000  char[4]  "USR\\x1a"
  0x004  u16      version (must be 0x000C)
  0x006  u16      number of players in use
  0x008  dir[100] { char name[16]; u32 score; u16 slot; }   (22 bytes each)
  0x8A0  rec[99]  player records, 0x16BD (5821) bytes each
                  record k lives at 0x8A0 + k*0x16BD

While playing, the active record is held verbatim in memory at DGROUP:0x4562.
"""
import struct
import sys

REC_SIZE = 0x16BD
REC_BASE = 0x8A0
BLOCK_BASE = 0x27E      # first per-building block inside a record
BLOCK_SIZE = 0x6BA      # three blocks, one per engineering building

RANKS = ["Trainee", "Assistant", "Technician", "Engineer", "Doctor",
         "Professor", "Scientist", "Head Scientist"]


def s16(b, o):
    return struct.unpack_from('<h', b, o)[0]


def parse_record(r):
    out = {
        'name': r[0:16].split(b'\0')[0].decode('latin-1'),
        'score': struct.unpack_from('<I', r, 0x10)[0],
        'slot': s16(r, 0x14),
        'live_warehouse_hdr': r[0x1A:0x2E].hex(),   # copied to/from block+0x00
        'current_building': s16(r, 0x30),
        'last_race_won_idx': [s16(r, 0x34 + 2 * i) for i in range(3)],  # -1 none, 4 = all five
        'warehouse_in_progress': [s16(r, 0x3A + 2 * i) for i in range(3)],
        'races_won_total': s16(r, 0x16AC),      # 15 => game complete
        'tail': r[0x16AE:].hex(),
        'buildings': [],
    }
    for b in range(3):
        blk = r[BLOCK_BASE + BLOCK_SIZE * b: BLOCK_BASE + BLOCK_SIZE * (b + 1)]
        state = blk[0x26C:0x302].rstrip(b'\0')
        out['buildings'].append({
            'hdr': blk[:0x14].hex(),
            'state_array_len': len(state),        # 40 / 80 / 120 seen; grows with level
            'state_09': state.count(9),           # hypothesis: 09 = opened / solved
            'state_07': state.count(7),           # hypothesis: 07 = locked
        })
    return out


def main(path):
    d = open(path, 'rb').read()
    assert d[:4] == b'USR\x1a', 'not a PLAYERS.DAT'
    ver, count = struct.unpack_from('<HH', d, 4)
    print(f'version={ver:#x} players={count}')
    for k in range(100):
        o = 8 + 22 * k
        name = d[o:o + 16].split(b'\0')[0]
        if not name:
            continue
        score, slot = struct.unpack_from('<IH', d, o + 16)
        rec = d[REC_BASE + REC_SIZE * slot: REC_BASE + REC_SIZE * (slot + 1)]
        p = parse_record(rec)
        print(f"\n[{slot}] {p['name']}  score={p['score']}  races won={p['races_won_total']}/15")
        print(f"   current building={p['current_building']}  last race idx per bldg={p['last_race_won_idx']}"
              f"  in-progress={p['warehouse_in_progress']}")
        for i, b in enumerate(p['buildings']):
            print(f"   bldg {i}: state[{b['state_array_len']}] 09={b['state_09']} 07={b['state_07']}  hdr={b['hdr']}")


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'PLAYERS.DAT')
