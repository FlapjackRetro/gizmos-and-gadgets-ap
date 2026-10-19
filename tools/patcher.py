#!/usr/bin/env python3
"""Apply Archipelago patches to Gizmos & Gadgets' ssg.exe (DOS v1.0).

Usage: python patcher.py path/to/ssg.exe [out.exe] [--only name,name] [--list]

Writes a patched copy (default SSG_AP.EXE next to the input); the original is never modified.
Each patch checks the original bytes first, so a different game version is refused instead of corrupted.
File offset = 0x1200 (MZ header) + segment*16 + offset for resident code (see docs/RE_NOTES.md).
"""
import argparse
import hashlib
import os
import sys

KNOWN_SHA1 = 'a91464b806e3751ab7ceea9e5b7f085c853c570a'   # Home of the Underdogs repack, v1.0

# name: (description, [(file_offset, original_bytes_hex, new_bytes_hex), ...])
PATCHES = {
    'race_no_advance': (
        'Winning a race no longer moves the building up a level (R02E0_05B7: inc word [bx+4596] -> NOP). '
        'The total-races counter [5C0E] still increments.',
        [(0x45B7, 'ff879645', '90909090')],
    ),
    'no_finished_refusal': (
        'Buildings whose 5 races are won can still be entered (R02E0 building select: drop the three '
        '"cmp [4596+2b],4 / jz refuse" jumps).',
        [(0x4439, '742f', '9090'),     # Aircraft     (cmp [4598],4)
         (0x444A, '741e', '9090'),     # Alt-Energy   (cmp [459A],4)
         (0x445B, '740d', '9090')],    # Automotive   (cmp [4596],4)
    ),
}


def apply(data, names):
    data = bytearray(data)
    for name in names:
        desc, edits = PATCHES[name]
        for off, orig, new in edits:
            orig_b, new_b = bytes.fromhex(orig), bytes.fromhex(new)
            cur = bytes(data[off:off + len(orig_b)])
            if cur == new_b:
                continue                       # already applied
            if cur != orig_b:
                raise SystemExit(f'{name}: unexpected bytes at {off:#x}: {cur.hex()} (expected {orig})')
            data[off:off + len(new_b)] = new_b
    return bytes(data)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('exe', nargs='?')
    ap.add_argument('out', nargs='?')
    ap.add_argument('--only', help='comma-separated patch names (default: all)')
    ap.add_argument('--list', action='store_true')
    a = ap.parse_args()
    if a.list or not a.exe:
        for n, (d, e) in PATCHES.items():
            print(f'{n}: {d}')
        return
    src = open(a.exe, 'rb').read()
    sha = hashlib.sha1(src).hexdigest()
    if sha != KNOWN_SHA1:
        print(f'warning: unrecognised ssg.exe (sha1 {sha}); patches are byte-checked anyway', file=sys.stderr)
    names = a.only.split(',') if a.only else list(PATCHES)
    out = a.out or os.path.join(os.path.dirname(a.exe) or '.', 'SSG_AP.EXE')
    open(out, 'wb').write(apply(src, names))
    print(f'wrote {out} with patches: {", ".join(names)}')


if __name__ == '__main__':
    main()
