#!/usr/bin/env python3
"""Ground-race vehicle stats model (reimplementation of O0E7A_044E) + Morty's builds per level.

Usage: python race_model.py GAMEDIR            -> data/morty_builds.tsv style output
Status: transcribed from disassembly; NOT yet validated against live values (see RE_NOTES §4.7).
Aircraft (vehicles 5-9) use the flight engine O0E7D, not modelled here.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
from resources import Archives  # noqa: E402

DGROUP = 0xEA7


class Tables:
    def __init__(self, gamedir):
        exe = open(os.path.join(gamedir, 'ssg.exe'), 'rb').read()
        hdr = struct.unpack_from('<H', exe, 8)[0] * 16
        self.img = exe[hdr:]

    def t(self, off, tier):
        return struct.unpack_from('<i', self.img, DGROUP * 16 + off + 4 * tier)[0] / 65536


def vehicle_stats(T, v, parts):
    """parts: iterable of (group, tier). Returns (top_speed, accel) = (+0x22, +0x32) of the racer struct."""
    A = B = T.t(0x3B34, v)
    C = D = E = 0.0
    F = di = 0
    for g, t in parts:
        if g == 0x37:
            C += T.t(0x3A74, t); A -= T.t(0x3A64, t)
        elif g == 0x00:
            A -= T.t(0x3A64, t)
        elif g in (0x01, 0x35):
            A -= T.t(0x3A84, t)
        elif g == 0x03:
            B = T.t(0x3A54, t)
        elif g in (0x04, 0x34):
            C += T.t(0x3AC4, t)
        elif g == 0x05:
            E = T.t(0x3AD4, t)
        elif g in (0x06, 0x39):
            F = t
        elif g in (0x07, 0x38, 0x3A):
            di = t
        elif g == 0x08:
            A -= T.t(0x3AE4, t)
        elif g == 0x09:
            A -= T.t(0x3AB4, t); D = T.t(0x3A94, t)
        elif g == 0x30:
            C += T.t(0x3AF4, t)
        elif g == 0x32:
            A -= T.t(0x3B04, t)
        elif g == 0x33:
            C += T.t(0x3B14, t); A -= T.t(0x3B24, t)
        # everything else (brakes 0x02/0x31, fuel tank 0x0A/0x3B, paint 0x0B, decal 0x0C): no effect
    if v == 0:
        C = 0.3
    elif v in (1, 2, 3, 4):
        if v in (2, 4):
            B = A
        C += D * E
    elif v >= 11:
        D = T.t(0x3AA4, min(F, di))
        if E:
            C += D * E
    return min(A, B), C


def level_table(a, v):
    t, d = a.get(0x2F5 + v)
    count, groups = struct.unpack_from('<H', d, 2)[0], struct.unpack_from('<H', d, 6)[0]
    base = 0x2D + groups * 2
    ents = [struct.unpack_from('<HH', d, base + 0x8E * k + 8) for k in range(count)]
    morty, p = [], base + 0x8E * count
    while p + 2 <= len(d):
        x = struct.unpack_from('<h', d, p)[0]
        if x == -1:
            break
        morty.append(x)
        p += 2
    return ents, morty


def main():
    gamedir = sys.argv[1]
    a, T = Archives(gamedir), Tables(gamedir)
    print('vehicle\tmorty_part_ids\tmorty_top\tmorty_accel\tmax_tier_top\tmax_tier_accel')
    for v in list(range(5)) + list(range(10, 15)):
        ents, morty = level_table(a, v)
        best = {}
        for g, t in ents:
            best[g] = max(best.get(g, 0), t)
        ms = vehicle_stats(T, v, [ents[i] for i in morty])
        bs = vehicle_stats(T, v, best.items())
        print(f'{v}\t{",".join(map(str, morty))}\t{ms[0]:.3f}\t{ms[1]:.3f}\t{bs[0]:.3f}\t{bs[1]:.3f}')


if __name__ == '__main__':
    main()
