#!/usr/bin/env python3
"""Print DOSBox-X debugger commands for Gizmos & Gadgets, given the program's load segment.

Usage: python dbgaddr.py <load_segment_hex>

Get the load segment by starting the game with `DEBUGBOX SSG.EXE` in DOSBox-X: the debugger stops at the
entry point (image offset 0000:0000), so the CS shown at that moment *is* the load segment.
All other code/data addresses are load segment + the paragraph offsets from docs/RE_NOTES.md.
"""
import sys

DGROUP = 0x0EA7          # data segment, relative to load segment
RACE_SEG = 0x02E0        # resident segment holding R02E0_0564 (race dispatcher / win counter)

# (DS offset, description) — player record fields, see RE_NOTES §3
WATCH = [
    (0x4592, 'current building (0-2)'),
    (0x4596, 'last race won idx, building 0'),
    (0x4598, 'last race won idx, building 1'),
    (0x459A, 'last race won idx, building 2'),
    (0x459C, 'warehouse in-progress flag, building 0'),
    (0x5C0E, 'total races won (15 = ending)'),
]


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    load = int(sys.argv[1], 16)
    ds = load + DGROUP
    cs_race = load + RACE_SEG
    print(f'; load segment {load:04X}  ->  DGROUP {ds:04X}   race code {cs_race:04X}')
    print(f'; sanity check: D {ds:04X}:0004 should show "Borland C++ - Copyright 1991"')
    print(f'; and D {ds:04X}:1F6E should show "players.dat"\n')
    print('; --- race result (code breakpoints, resident code, always valid) ---')
    print(f'BP {cs_race:04X}:0564   ; race dispatcher entry (R02E0_0564)')
    print(f'BP {cs_race:04X}:05A3   ; after race: [455E]=aborted, [4560]=0 means player won')
    print(f'BP {cs_race:04X}:05B7   ; WIN: inc building race idx (patch target, file 0x45B7)')
    print('\n; --- player record watches (memory-change breakpoints) ---')
    for off, desc in WATCH:
        print(f'BPM {ds:04X}:{off:04X}   ; {desc}')
    print('\n; --- dumps for tools/memdiff.py ---')
    print(f'MEMDUMPBIN {ds:04X}:0000 10000   ; whole DGROUP (64 KB)')
    print(f'MEMDUMPBIN {ds:04X}:4562 16BD    ; active player record only')


if __name__ == '__main__':
    main()
