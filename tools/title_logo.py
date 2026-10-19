#!/usr/bin/env python3
"""Build the Archipelago title-screen logo: a patched copy of gizmo001.dat (GIZMOAP1.DAT) in which title sprite
container 0x1028 element 2 ("A Super Solvers Science Adventure", 124x26 at x=99, y=138) is widened to the right
screen edge and the bottom, with the Archipelago logo drawn in the bottom-right corner. No code changes besides
pointing ssg.exe at the new archive (patcher.py 'title_logo').

Logo: assets/archipelago_logo.png, (c) the Archipelago project, CC BY-NC (derivative of the MIT original); see
assets/LICENSE.md. The generated GIZMOAP1.DAT contains it, so it is non-commercial too.

Usage: python title_logo.py GAMEDIR LOGO.png [OUTDIR] [--preview out.png]
Colours: letters use index 15 (white, fixed), shading the grey ramp 242..250 and the outline 241 - the same
indices as the GIZMOS & GADGETS logo, so it fades in with it (palette 0x1004 blanks 241..250, 0x1000 shows them).
"""
import argparse
import os
import struct
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(__file__))
from lpx import decode, encode_rle, palette  # noqa: E402
from resources import Archives  # noqa: E402

RID = 0x1028
ELEM = 2
ELEM_X, ELEM_Y = 99, 138
NEW_W, NEW_H = 320 - ELEM_X, 200 - ELEM_Y          # 221 x 62
LOGO_W = 140
MARGIN_R, MARGIN_B = 4, 3
SHADES = [15, 250, 249, 248, 247, 246, 245]         # light -> dark
OUTLINE = 241


def container_split(d):
    n = struct.unpack_from('<H', d, 0)[0] // 2
    offs = list(struct.unpack_from('<%dH' % n, d, 0))
    return [d[offs[k]:offs[k + 1]] for k in range(n - 1)]


def container_join(elems):
    n = len(elems) + 1
    offs, p = [], 2 * n
    for e in elems:
        offs.append(p); p += len(e)
    offs.append(p)
    return struct.pack('<%dH' % n, *offs) + b''.join(elems)


def logo_pixels(path, pal):
    im = Image.open(path).convert('RGBA')
    h = round(im.height * LOGO_W / im.width)
    im = im.resize((LOGO_W, h), Image.LANCZOS)
    px = im.load()
    lum = {i: sum(pal[3 * i:3 * i + 3]) / 3 for i in SHADES}
    rows = [[None] * LOGO_W for _ in range(h)]
    for y in range(h):
        for x in range(LOGO_W):
            r, g, b, a = px[x, y]
            if a < 110:
                continue
            l = (r + g + b) / 3
            rows[y][x] = min(SHADES, key=lambda i: abs(lum[i] - l))
    # 1-px dark outline (8-neighbourhood), like the game's own title text; grow the canvas by 1 on each side
    out = [[None] * (LOGO_W + 2) for _ in range(h + 2)]
    for y in range(h):
        for x in range(LOGO_W):
            out[y + 1][x + 1] = rows[y][x]
    for y in range(h + 2):
        for x in range(LOGO_W + 2):
            if out[y][x] is None and any(
                    0 <= y + dy - 1 < h and 0 <= x + dx - 1 < LOGO_W and rows[y + dy - 1][x + dx - 1] is not None
                    for dy in (-1, 0, 1) for dx in (-1, 0, 1)):
                out[y][x] = OUTLINE
    return out


def build_sprite(a, logo_path):
    pal = palette(a.get(0x1000)[1], palette(a.get(0x1094)[1]))
    t, d = a.get(RID)
    elems = container_split(d)
    tag = elems[ELEM][:2]
    w, h, rows = decode(elems[ELEM][2:])
    canvas = [[None] * NEW_W for _ in range(NEW_H)]
    for y in range(h):
        canvas[y][:w] = rows[y]
    logo = logo_pixels(logo_path, pal)
    lh, lw = len(logo), len(logo[0])
    ox, oy = NEW_W - lw - MARGIN_R, NEW_H - lh - MARGIN_B
    assert oy >= h, 'logo would overlap the Super Solvers text'
    for y in range(lh):
        for x in range(lw):
            if logo[y][x] is not None:
                canvas[oy + y][ox + x] = logo[y][x]
    elems[ELEM] = tag + encode_rle(canvas)
    return t, container_join(elems), pal


def rebuild_archive(src, index, typ, payload):
    """Replace resource `index` with an uncompressed (flag 0) resource; offsets are relative to file+4."""
    first = struct.unpack_from('<I', src, 4)[0]
    offs = list(struct.unpack_from('<%dI' % ((first - 4) // 4), src, 4))
    start, end = offs[index], offs[index + 1]
    new = bytes([typ, 0]) + payload
    delta = len(new) - (end - start)
    out = bytearray(src[:start + 4]) + new + src[end + 4:]
    for k in range(index + 1, len(offs)):
        struct.pack_into('<I', out, 4 + 4 * k, offs[k] + delta)
    return bytes(out)


def preview(a, elems_blob, pal, path):
    t, bg = a.get(0x1002)
    canvas = [list(bg[14 + 320 * y:14 + 320 * (y + 1)]) for y in range(200)]
    elems = container_split(elems_blob)
    for k, (x, y) in enumerate([(59, 9), (52, 51), (ELEM_X, ELEM_Y), (0, 181)]):
        w, h, rows = decode(elems[k][2:])
        for j, row in enumerate(rows):
            for i, c in enumerate(row):
                if c is not None and 0 <= y + j < 200 and 0 <= x + i < 320:
                    canvas[y + j][x + i] = c
    im = Image.new('P', (320, 200)); im.putdata([c for r in canvas for c in r]); im.putpalette(pal)
    im.convert('RGB').resize((640, 400), Image.NEAREST).save(path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('gamedir'); ap.add_argument('logo'); ap.add_argument('outdir', nargs='?')
    ap.add_argument('--preview')
    o = ap.parse_args()
    a = Archives(o.gamedir)
    typ, blob, pal = build_sprite(a, o.logo)
    if o.preview:
        preview(a, blob, pal, o.preview)
    src = open(os.path.join(o.gamedir, 'gizmo001.dat'), 'rb').read()
    out = rebuild_archive(src, RID & 0xFFF, typ, blob)
    dst = os.path.join(o.outdir or o.gamedir, 'GIZMOAP1.DAT')
    open(dst, 'wb').write(out)
    print(f'wrote {dst} ({len(out)} bytes; resource {RID:#06x} now {len(blob)} bytes uncompressed)')


if __name__ == '__main__':
    main()
