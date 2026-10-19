#!/usr/bin/env python3
"""Exact simulation of Gizmos & Gadgets ground races (O0E7A_01CA), using the stats model in race_model.py.

Usage:
  python race_sim.py GAMEDIR                 # every ground level: Morty vs all-max-tier build
  python race_sim.py GAMEDIR <vehicle> id,id,...   # simulate a specific player build (level-local part ids)

Mechanics (RE_NOTES §4.8):
- Racers: player (DS:BAE8, id bit 1) and Morty (DS:BAAE, id bit 2), each on its own track resource
  (DS:39F4[v] / DS:3A12[v]); laps = DS:3A30[v]; start: speed 0, track index 0, mode 0.
- Each frame: tick both speeds (O0E7A_0034), then advance the player int(speed) steps, then Morty int(speed) steps
  (O0E7A_00E0), until someone has finished. A step moves to the next *position* record, processing event records
  on the way: -5 lap (laps>0 -> laps-1; index -> 0), -4 mode 0, -3 mode 1, -2 finish line (if laps == 0),
  -14/-13 sound. Codes >= 0, -1 and -12..-6 are positions.
- Tick: mode 0: speed = min(speed + accel, capA).  mode 1: if speed > capB: speed -= 0.2 else
  speed = min(speed + accel, capB).  (capA = +0x26 = A, capB = +0x22 = min(A,B), accel = C for both modes.)
- Result: only player finished -> win; only Morty -> loss; same frame -> win if player speed >= Morty speed.
All arithmetic is 16.16 fixed point, as in the game.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
from race_model import Tables, level_table, vehicle_stats_raw  # noqa: E402
from resources import Archives  # noqa: E402

DG = 0xEA7 * 16
FX = 65536
DECEL = 0x3333


class Racer:
    def __init__(self, track, laps, capA, capB, accel):
        self.track, self.laps = track, laps
        self.capA, self.capB, self.accel = capA, capB, accel
        self.speed = 0
        self.idx = 0
        self.mode = 0
        self.finished = False

    def tick(self):
        if self.mode:
            if self.speed > self.capB:
                self.speed -= DECEL
            else:
                self.speed = min(self.speed + self.accel, self.capB)
        else:
            self.speed = min(self.speed + self.accel, self.capA)

    def step(self):
        """Advance to the next position record; returns True if the finish line was crossed."""
        fin = False
        while True:
            self.idx += 1
            if self.idx >= len(self.track):
                return fin
            code = self.track[self.idx]
            if code == -5:
                if self.laps:
                    self.laps -= 1
                self.idx = 0
            elif code == -4:
                self.mode = 0
            elif code == -3:
                self.mode = 1
            elif code == -2:
                if self.laps == 0:
                    self.finished = True
                    fin = True
            elif code in (-14, -13):
                pass
            else:
                return fin


def load_track(a, rid):
    t, d = a.get(rid)
    return [struct.unpack_from('<h', d, i + 4)[0] for i in range(0, len(d) - 5, 6)]


def race(gamedir_or_ctx, v, player_parts, morty_parts, max_frames=100000):
    a, T = gamedir_or_ctx
    w = lambda o: struct.unpack_from('<H', T.img, DG + o)[0]
    laps = w(0x3A30 + 2 * v)
    pt, mt = load_track(a, w(0x39F4 + 2 * v)), load_track(a, w(0x3A12 + 2 * v))
    racers = []
    for track, parts in ((pt, player_parts), (mt, morty_parts)):
        capA, capB, accel = vehicle_stats_raw(T, v, parts)
        racers.append(Racer(track, laps, capA, capB, accel))
    p, m = racers
    for frame in range(max_frames):
        p.tick(); m.tick()
        if not p.finished:
            for _ in range(p.speed >> 16):
                if p.step() or p.finished:
                    break
        if not m.finished:
            for _ in range(m.speed >> 16):
                if m.step() or m.finished:
                    break
        if p.finished or m.finished:
            if p.finished and not m.finished:
                return True, frame
            if m.finished and not p.finished:
                return False, frame
            return p.speed >= m.speed, frame
    return None, max_frames


def main():
    gamedir = sys.argv[1]
    a, T = Archives(gamedir), Tables(gamedir)
    if len(sys.argv) > 3:
        v = int(sys.argv[2])
        ents, morty = level_table(a, v)
        ids = [int(x) for x in sys.argv[3].split(',')]
        print(race((a, T), v, [ents[i] for i in ids], [ents[i] for i in morty]))
        return
    print('vehicle\tmax_build_wins\tframes')
    for v in list(range(5)) + list(range(10, 15)):
        ents, morty = level_table(a, v)
        best = {}
        for g, t in ents:
            best[g] = max(best.get(g, 0), t)
        print(v, *race((a, T), v, list(best.items()), [ents[i] for i in morty]), sep='\t')


if __name__ == '__main__':
    main()


# ---------------------------------------------------------------- fast path (single racer, precomputed track)
def expand_track(track, laps):
    """Walk the track exactly like Racer.step with no speed limit; returns (mode_after_step[], finish_step)."""
    r = Racer(track, laps, 0, 0, 0)
    modes = []
    while True:
        fin = r.step()
        modes.append(r.mode)
        if fin or r.finished:
            return modes, len(modes)
        if len(modes) > 100000:
            raise RuntimeError('track never finishes')


def finish_time(expanded, capA, capB, accel, max_frames=100000):
    """Frame index at which this racer finishes and its speed then (racers don't interact)."""
    modes, fin = expanded
    pos, mode, speed = 0, 0, 0
    for frame in range(max_frames):
        if mode:
            speed = speed - DECEL if speed > capB else min(speed + accel, capB)
        else:
            speed = min(speed + accel, capA)
        n = speed >> 16
        if n:
            if pos + n >= fin:
                return frame, speed
            pos += n
            mode = modes[pos - 1]
    return None, speed


def wins(p, m):
    """p, m = (frame, speed) from finish_time; True if the player wins."""
    if p[0] is None:
        return False
    if m[0] is None or p[0] < m[0]:
        return True
    if p[0] > m[0]:
        return False
    return p[1] >= m[1]
