#!/usr/bin/env python3
"""List the part-name text resources stored in gizmo000.dat.

Usage: python part_names.py path/to/gizmo000.dat > data/part_names.tsv

Part names are stored uncompressed as `S <id byte> <text> NUL` records. Output: file offset, text.
Records are grouped by level in file order (see docs/RE_NOTES.md §3.3 for the cluster → level mapping).
"""
import re
import sys

d = open(sys.argv[1] if len(sys.argv) > 1 else 'gizmo000.dat', 'rb').read()
print('offset\tname')
for m in re.finditer(rb'S(.)([\x20-\x7e]{3,})\x00', d, re.S):
    print(f'{m.start()}\t{m.group(2).decode().strip()}')
