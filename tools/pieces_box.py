#!/usr/bin/env python3
"""Cut the "Pieces" box out of the game's own door-puzzle background, for the Archipelago inventory sprite.

Usage: python tools/pieces_box.py GAMEDIR OUTDIR [--shadow] [--preview N]
Writes OUTDIR/pieces_box.png (RGBA, transparent around the box), OUTDIR/pieces_box.json (palette indices,
size, counter anchor) and, with --preview N, OUTDIR/pieces_box_preview.png with a stand-in numeral N.
Game art: the output comes from the player's own files and is not to be committed (like GIZMOAP1.DAT's source).

Source: resource 0x2084 (320x155 puzzle background, "Put the puzzle pieces together to make a machine"), palette
0x2085 over 0x1094. The box (with its lid) is part of the background, inside x 8..85, y 86..131; the surround is
index 198 (green), the shadow index 211. The puzzle draws the piece count under "Pieces"; in the puzzle
screenshot (2026-10-23) the numeral "2" sits at x 38..42, y 118..123 of the background, i.e. centred at
(40, 120.5).
"""
import argparse
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
from lpx import decode, palette  # noqa: E402
from resources import Archives  # noqa: E402

BG, PAL, BASE_PAL = 0x2084, 0x2085, 0x1094
AREA = (8, 86, 86, 132)          # x0, y0, x1, y1 (exclusive) searched for the box (not the blue frame)
SURROUND, SHADOW = 198, 211
COUNTER = (40.0, 120.5)          # centre of the numeral, background coordinates


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('gamedir'); ap.add_argument('outdir')
    ap.add_argument('--shadow', action='store_true', help='keep the dark green shadow')
    ap.add_argument('--preview', type=int)
    o = ap.parse_args()
    from PIL import Image
    a = Archives(o.gamedir)
    pal = palette(a.get(PAL)[1], palette(a.get(BASE_PAL)[1]))
    w, h, rows = decode(a.get(BG)[1])
    x0, y0, x1, y1 = AREA
    keep = lambda c: c is not None and c != SURROUND and (o.shadow or c != SHADOW)
    pts = [(x, y) for y in range(y0, y1) for x in range(x0, x1) if keep(rows[y][x])]
    bx0, by0 = min(p[0] for p in pts), min(p[1] for p in pts)
    bx1, by1 = max(p[0] for p in pts) + 1, max(p[1] for p in pts) + 1
    im = Image.new('RGBA', (bx1 - bx0, by1 - by0), (0, 0, 0, 0))
    idx = []
    for y in range(by0, by1):
        line = []
        for x in range(bx0, bx1):
            c = rows[y][x]
            if keep(c):
                im.putpixel((x - bx0, y - by0), tuple(pal[3 * c:3 * c + 3]) + (255,))
                line.append(c)
            else:
                line.append(None)
        idx.append(line)
    os.makedirs(o.outdir, exist_ok=True)
    im.save(os.path.join(o.outdir, 'pieces_box.png'))
    meta = {'source': f'{BG:#06x} palette {PAL:#06x}', 'origin_in_background': [bx0, by0],
            'size': [bx1 - bx0, by1 - by0], 'counter_centre': [COUNTER[0] - bx0, COUNTER[1] - by0],
            'pixels': idx}
    with open(os.path.join(o.outdir, 'pieces_box.json'), 'w') as f:
        json.dump(meta, f)
    print(f'box {bx1 - bx0}x{by1 - by0} at ({bx0},{by0}); counter centre {meta["counter_centre"]}')
    if o.preview is not None:
        pv = Image.new('RGBA', im.size, (88, 148, 24, 255))
        pv.alpha_composite(im)
        # stand-in 3x5 digits (the game draws its own), centred on the anchor
        glyph = {'0': '111101101101111', '1': '010110010010111', '2': '111001111100111', '3': '111001111001111',
                 '4': '101101111001001', '5': '111100111001111', '6': '111100111101111', '7': '111001001001001',
                 '8': '111101111101111', '9': '111101111001111'}
        text = str(o.preview)
        cx, cy = meta['counter_centre']
        x = round(cx - (4 * len(text) - 1) / 2)
        y = round(cy - 2.5)
        for ch in text:
            for k, bit in enumerate(glyph[ch]):
                if bit == '1':
                    pv.putpixel((x + k % 3, y + k // 3), (24, 24, 24, 255))
            x += 4
        pv.resize((pv.width * 4, pv.height * 4), Image.NEAREST).save(os.path.join(o.outdir, 'pieces_box_preview.png'))


if __name__ == '__main__':
    main()
