#!/usr/bin/env python3
"""Symbolized disassembly of Gizmos & Gadgets ssg.exe (Borland C++ 1991, VROOMM overlays).

Usage: python disasm.py path/to/ssg.exe [outdir]      (requires `ndisasm` from NASM on PATH)

Writes one .asm per code segment into outdir (default ./dis):
  R<seg>.asm  resident code segment <seg> (paragraph within the load image)
  O<stub>.asm overlay whose stub segment is <stub>
Far calls are rewritten as `call R<seg>_<off>` / `call O<stub>_<off>` so cross-references are greppable.
Data references are DGROUP-relative (DGROUP = segment 0x0EA7).
"""
import os
import re
import struct
import subprocess
import sys

EXE = sys.argv[1] if len(sys.argv) > 1 else 'ssg.exe'
OUT = sys.argv[2] if len(sys.argv) > 2 else 'dis'

d = open(EXE, 'rb').read()
assert d[:2] == b'MZ'
lastp, pages, _, hdrparas = struct.unpack_from('<HHHH', d, 2)
image_end = (pages - 1) * 512 + lastp if lastp else pages * 512
img = d[hdrparas * 16:image_end]
assert d[image_end:image_end + 4] == b'FBOV', 'expected Borland VROOMM overlay block'
_, segtab_fileoff, segcount = struct.unpack_from('<IIH', d, image_end + 4)
OVL_BASE = image_end + 0x10
T = segtab_fileoff - hdrparas * 16          # segment table inside the load image

segtab = {}
for i in range(segcount):
    seg, maxoff, flags, minoff = struct.unpack_from('<HHHH', img, T + 8 * i)
    segtab[i * 8] = (seg, maxoff, flags)

ovl = {}
for sel, (seg, _, flags) in segtab.items():
    if flags != 3 or seg in ovl:
        continue
    _, fo, cs, fs, ne = struct.unpack_from('<HIHHH', img, seg * 16 + 2)
    ents = [struct.unpack_from('<H', img, seg * 16 + 0x20 + 5 * k + 2)[0] for k in range(ne)]
    ovl[seg] = dict(code=d[OVL_BASE + fo:OVL_BASE + fo + cs], ents=ents)


def stub_target(seg, off):
    k, r = divmod(off - 0x20, 5)
    if r == 0 and 0 <= k < len(ovl[seg]['ents']):
        return 'O%04x_%04x' % (seg, ovl[seg]['ents'][k])
    return 'O%04x?%04x' % (seg, off)


def resolve(a, b, is_ovl):
    if is_ovl:                      # overlay far calls hold selectors into the segment table
        if a not in segtab:
            return None
        a = segtab[a][0]
    if a in ovl:
        return stub_target(a, b)
    return 'R%04x_%04x' % (a, b)


callre = re.compile(r'call (0x[0-9a-f]+):(0x[0-9a-f]+)')
os.makedirs(OUT, exist_ok=True)


def dis(code, name, is_ovl):
    p = os.path.join(OUT, name + '.bin')
    open(p, 'wb').write(code)
    txt = subprocess.run(['ndisasm', '-b16', p], capture_output=True, text=True, check=True).stdout
    os.remove(p)

    def sub(m):
        r = resolve(int(m.group(1), 16), int(m.group(2), 16), is_ovl)
        return 'call ' + r if r else m.group(0)
    open(os.path.join(OUT, name + '.asm'), 'w').write(callre.sub(sub, txt))


starts = sorted(set(s for s, _, _ in segtab.values()))
resident = sorted(set(seg for seg, _, fl in segtab.values() if fl == 1 and seg not in ovl))
for seg in resident:
    nxt = next(s for s in starts if s > seg)
    dis(img[seg * 16:nxt * 16], 'R%04x' % seg, False)
for seg, o in ovl.items():
    if o['code']:
        dis(o['code'], 'O%04x' % seg, True)
print('resident segments:', len(resident), ' overlays:', sum(1 for o in ovl.values() if o['code']))
