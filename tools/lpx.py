#!/usr/bin/env python3
"""LPX images (Gizmos & Gadgets sprites/backgrounds) and VGA palettes.

LPX header (14 bytes): 'LPX\\x1a', u16 format (0 raw, 1 RLE), u16 width, u16 height, u16 1, u16 2 (= offset from +12
to the pixel data). Raw: width*height bytes. RLE (decoder R0399_0465), per row until `width` pixels are produced:
byte n != 0 -> n literal pixels follow; byte 0, k -> skip k pixels (transparent). Width must be <= 255.
Palette resources ('P'): u16 byte offset, u16 byte count, then 6-bit VGA RGB bytes.
"""
import struct

MAGIC = b'LPX\x1a'


def decode(data):
    """Returns (width, height, pixels) with pixels as a list of rows; None = transparent."""
    assert data[:4] == MAGIC
    fmt, w, h = struct.unpack_from('<HHH', data, 4)
    p = 12 + struct.unpack_from('<H', data, 12)[0]
    rows = []
    for _ in range(h):
        row = []
        if fmt == 0:
            row = list(data[p:p + w]); p += w
        else:
            while len(row) < w:
                n = data[p]; p += 1
                if n:
                    row += list(data[p:p + n]); p += n
                else:
                    k = data[p]; p += 1
                    row += [None] * k
        rows.append(row)
    return w, h, rows


def encode_rle(rows):
    """rows: list of equal-length lists of palette indices or None (transparent). Width <= 255."""
    h, w = len(rows), len(rows[0])
    assert w <= 255, 'RLE sprites are limited to 255 px wide'
    out = bytearray(MAGIC + struct.pack('<HHHHH', 1, w, h, 1, 2))
    for row in rows:
        i = 0
        while i < w:
            if row[i] is None:
                j = i
                while j < w and row[j] is None and j - i < 255:
                    j += 1
                out += bytes([0, j - i])
            else:
                j = i
                while j < w and row[j] is not None and j - i < 255:
                    j += 1
                out += bytes([j - i]) + bytes(row[i:j])
            i = j
    return bytes(out)


def palette(data, base=None):
    """Apply a palette resource onto `base` (list of 768 0-255 values); returns a new 768-list."""
    off, cnt = struct.unpack_from('<HH', data, 0)
    p = list(base) if base else [0] * 768
    for i in range(cnt):
        p[off + i] = min(255, data[4 + i] * 4)
    return p
