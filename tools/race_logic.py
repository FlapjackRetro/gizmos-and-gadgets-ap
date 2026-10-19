#!/usr/bin/env python3
"""Derive race-win requirements per ground level by exhaustive simulation (tools/race_sim.py fast path).

Usage: python race_logic.py GAMEDIR > data/race_requirements.tsv
For each level: share of winning builds, the smallest k such that *every* build whose performance parts are all
tier >= k wins (a simple, safe logic rule), and each slot's minimum tier when all other slots are maxed.
Slots with no performance effect (brakes, fuel tanks, paint, decals) are ignored.
"""
import itertools
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
from race_model import Tables, level_table, vehicle_stats_raw  # noqa: E402
from race_sim import DG, expand_track, finish_time, load_track, wins  # noqa: E402
from resources import Archives  # noqa: E402

NO_EFFECT = {0x02, 0x31, 0x36, 0x0A, 0x3B, 0x0B, 0x0C}
NAMES = ['Automotive', 'Aircraft', 'Alternative Energy']


def analyse(a, T, v):
    w = lambda o: struct.unpack_from('<H', T.img, DG + o)[0]
    ents, morty = level_table(a, v)
    laps = w(0x3A30 + 2 * v)
    P = expand_track(load_track(a, w(0x39F4 + 2 * v)), laps)
    M = expand_track(load_track(a, w(0x3A12 + 2 * v)), laps)
    mt = finish_time(M, *vehicle_stats_raw(T, v, [ents[i] for i in morty]))
    groups = {}
    for g, t in ents:
        if g not in NO_EFFECT:
            groups.setdefault(g, set()).add(t)
    keys = sorted(groups)
    cache = {}

    def win(tiers):
        st = vehicle_stats_raw(T, v, list(zip(keys, tiers)))
        if st not in cache:
            cache[st] = wins(finish_time(P, *st), mt)
        return cache[st]

    combos = list(itertools.product(*[sorted(groups[g]) for g in keys]))
    results = {c: win(c) for c in combos}
    share = sum(results.values()) / len(results)
    safe_k = None
    for k in range(4):
        sub = [c for c in combos if all(t >= min(k, max(groups[g])) for g, t in zip(keys, c))]
        if sub and all(results[c] for c in sub):
            safe_k = k
            break
    best = tuple(max(groups[g]) for g in keys)
    per_slot = {}
    for i, g in enumerate(keys):
        need = None
        for t in sorted(groups[g]):
            c = list(best); c[i] = t
            if results[tuple(c)]:
                need = t
                break
        per_slot[g] = need
    return keys, share, safe_k, per_slot, mt


def main():
    gamedir = sys.argv[1]
    a, T = Archives(gamedir), Tables(gamedir)
    print('vehicle\tbuilding\tlevel\twin_share\tall_slots_tier_ge\tmin_tier_per_slot_others_max\tmorty_finish_frame')
    for v in list(range(5)) + list(range(10, 15)):
        keys, share, k, per, mt = analyse(a, T, v)
        per_s = ','.join(f'{g:#04x}:{per[g]}' for g in keys)
        print(f'{v}\t{NAMES[v // 5]}\t{v % 5 + 1}\t{share:.3f}\t{k}\t{per_s}\t{mt[0]}')


if __name__ == '__main__':
    main()
