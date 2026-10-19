#!/usr/bin/env python3
"""Diff two or more DOSBox-X MEMDUMPBIN dumps to find the variables an in-game action changed.

Usage:
  python memdiff.py before.bin after.bin [--base 0x0000] [--words]
  python memdiff.py a.bin b.bin c.bin ...     # only offsets that change at EVERY step (a->b, b->c, ...)

--base   offset of the dump's first byte (e.g. 0x4562 for a record-only dump), so output shows DS offsets
--words  report aligned 16-bit words instead of bytes (most game variables are C ints)

Workflow: dump DGROUP, do exactly one thing in game (open one box / solve one door), dump again, diff.
Repeat the same action type 2-3 times and pass all dumps; consistent changers are your candidates.
"""
import argparse
import struct


def changes(a, b, words):
    step = 2 if words else 1
    out = {}
    for i in range(0, min(len(a), len(b)) - step + 1, step):
        if a[i:i + step] != b[i:i + step]:
            if words:
                out[i] = (struct.unpack_from('<h', a, i)[0], struct.unpack_from('<h', b, i)[0])
            else:
                out[i] = (a[i], b[i])
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dumps', nargs='+')
    ap.add_argument('--base', type=lambda x: int(x, 0), default=0)
    ap.add_argument('--words', action='store_true')
    a = ap.parse_args()
    data = [open(p, 'rb').read() for p in a.dumps]
    steps = [changes(x, y, a.words) for x, y in zip(data, data[1:])]
    common = set(steps[0])
    for s in steps[1:]:
        common &= set(s)
    for off in sorted(common):
        chain = [steps[0][off][0]] + [s[off][1] for s in steps]
        vals = ' -> '.join(str(v) for v in chain)
        print(f'DS:{a.base + off:04X}  {vals}')
    print(f'; {len(common)} offsets changed in all {len(steps)} step(s)')


if __name__ == '__main__':
    main()
