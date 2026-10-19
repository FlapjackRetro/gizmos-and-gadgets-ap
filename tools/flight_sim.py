#!/usr/bin/env python3
"""Aircraft race model + exact simulation (overlay O0E7D), and per-level win requirements.

Usage: python flight_sim.py GAMEDIR > data/flight_requirements.tsv
Status: validated live on Aircraft L3 (stats exact; predicted the live loss), 2026-10-19.

Stats (O0E7D_0037(racer*, partlist*)), all 16.16 fixed point:
  A = DS:3DBE[level]; B = 0; for each part: group g (0x0D..0x2F) with tier t -> one of
      A -= T[t]  /  B += T[t]  /  B = T[t]      (tables in DGROUP, see GROUPS below)
  level 1 (glider): caps = [1.5, 5.0, 1.5], accels = [-B, 0.3, -B]; other levels: caps = [A]*3, accels = [B]*3.
  Racer struct: +0x0E speed, +0x12 track index, +0x14 phase, +0x16 finished, +0x18 track ptr,
                +0x1C caps[3], +0x28 accels[3], +0x34 position (16.16).
Race (O0E7D_05E4): player DS:BB64 (parts DS:6616, track DS:3B96[level]), Morty DS:BB28 (parts [6634], track
  DS:3BA0[level]). Each frame: tick (speed += accel[phase]; clamp to cap from above if accel >= 0, from below if
  accel < 0), position += speed, step int(new) - int(old) records. Ends when either finishes; player finished ->
  win (ties go to the player).
Track records (6 bytes x, y, code); step events (O0E7D:05C7, index code+14): -12 skip (idx += x-1), -11/-9 phase 0,
  -8 phase 1, -10 phase 2, -7/-6 no-op, -2 finish, -1 end; -5/-4/-3 and >= 0 are positions.
"""
import itertools
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
from race_model import Tables, level_table  # noqa: E402
from resources import Archives  # noqa: E402

DG = 0xEA7 * 16
# group -> (op, table offset in DGROUP); extracted from the O0E7D:0381 jump table
GROUPS = {
    0x0D: ('A-', 0x3BAA), 0x0E: ('A-', 0x3BB6), 0x0F: ('B=', 0x3BC6), 0x10: ('B+', 0x3BCE),
    0x11: ('B+', 0x3BDE), 0x12: ('B+', 0x3BEE), 0x13: ('B+', 0x3BFA), 0x14: ('B+', 0x3C0A),
    0x15: ('B+', 0x3C1A), 0x16: ('B+', 0x3C2A), 0x17: ('B+', 0x3C3A), 0x18: ('A-', 0x3C4A),
    0x19: ('A-', 0x3C4A), 0x1A: ('A-', 0x3C6A), 0x1B: ('A-', 0x3C7A), 0x1C: ('A-', 0x3C8A),
    0x1D: ('A-', 0x3C9A), 0x1E: ('A-', 0x3D2A), 0x1F: ('A-', 0x3D3A), 0x20: ('B+', 0x3D4A),
    0x21: ('A-', 0x3D5A), 0x24: ('A-', 0x3D6A), 0x25: ('A-', 0x3D7A), 0x26: ('A-', 0x3D8A),
    0x27: ('A-', 0x3D9A), 0x28: ('A-', 0x3CAA), 0x29: ('A-', 0x3CBA), 0x2A: ('A-', 0x3CCA),
    0x2B: ('A-', 0x3CDA), 0x2C: ('B+', 0x3CEA), 0x2D: ('B+', 0x3CFA), 0x2E: ('A-', 0x3D0A),
    0x2F: ('A-', 0x3D1A),
}


def flight_stats_raw(T, level, parts):
    A = T.raw(0x3DBE, level)
    B = 0
    for g, t in parts:
        if g not in GROUPS:
            continue
        op, tbl = GROUPS[g]
        v = T.raw(tbl, t)
        if op == 'A-':
            A -= v
        elif op == 'B+':
            B += v
        else:
            B = v
    if level == 1:
        return (0x18000, 0x50000, 0x18000), (-B, 0x4CCD, -B)
    return (A, A, A), (B, B, B)


def load_track(a, rid):
    t, d = a.get(rid)
    return [struct.unpack_from('<hhh', d, i) for i in range(0, len(d) - 5, 6)]


def finish_time(track, caps, accels, max_frames=200000):
    idx, phase, speed = 0, 0, 0
    pos = track[0][0] << 16
    for frame in range(max_frames):
        acc, cap = accels[phase], caps[phase]
        speed += acc
        if acc >= 0:
            speed = min(speed, cap)
        else:
            speed = max(speed, cap)
        old = pos >> 16
        pos += speed
        for _ in range((pos >> 16) - old):
            finished = False
            while True:
                idx += 1
                if idx >= len(track):
                    return None, speed
                x, y, code = track[idx]
                if code == -12:
                    idx += x - 1
                elif code in (-11, -9):
                    phase = 0
                elif code == -8:
                    phase = 1
                elif code == -10:
                    phase = 2
                elif code in (-7, -6, -14, -13):
                    pass
                elif code == -2:
                    finished = True
                elif code == -1:
                    break
                else:
                    break
            if finished:
                return frame, speed
    return None, speed


def analyse(a, T, level):
    v = 5 + level
    w = lambda o: struct.unpack_from('<H', T.img, DG + o)[0]
    ents, morty = level_table(a, v)
    P, M = load_track(a, w(0x3B96 + 2 * level)), load_track(a, w(0x3BA0 + 2 * level))
    mt = finish_time(M, *flight_stats_raw(T, level, [ents[i] for i in morty]))
    groups = {}
    for g, t in ents:
        if g in GROUPS:
            groups.setdefault(g, set()).add(t)
    keys = sorted(groups)
    cache = {}

    def win(tiers):
        st = flight_stats_raw(T, level, list(zip(keys, tiers)))
        if st not in cache:
            p = finish_time(P, *st)
            cache[st] = p[0] is not None and (mt[0] is None or p[0] <= mt[0])
        return cache[st]

    combos = list(itertools.product(*[sorted(groups[g]) for g in keys]))
    results = {c: win(c) for c in combos}
    share = sum(results.values()) / len(results)
    best = tuple(max(groups[g]) for g in keys)
    safe_k = None
    for k in range(4):
        sub = [c for c in combos if all(t >= min(k, max(groups[g])) for g, t in zip(keys, c))]
        if sub and all(results[c] for c in sub):
            safe_k = k
            break
    per = {}
    for i, g in enumerate(keys):
        for t in sorted(groups[g]):
            c = list(best); c[i] = t
            if results[tuple(c)]:
                per[g] = t
                break
        else:
            per[g] = None
    return keys, share, safe_k, per, mt, results[best]


def main():
    gamedir = sys.argv[1]
    a, T = Archives(gamedir), Tables(gamedir)
    print('vehicle\tbuilding\tlevel\twin_share\tall_slots_tier_ge\tmin_tier_per_slot_others_max\tmorty_finish_frame\tmax_build_wins')
    for level in range(5):
        keys, share, k, per, mt, bw = analyse(a, T, level)
        per_s = ','.join(f'{g:#04x}:{per[g]}' for g in keys)
        print(f'{5 + level}\tAircraft\t{level + 1}\t{share:.3f}\t{k}\t{per_s}\t{mt[0]}\t{bw}')


if __name__ == '__main__':
    main()
