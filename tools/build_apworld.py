#!/usr/bin/env python3
"""Package apworld/gizmos_and_gadgets as gizmos_and_gadgets.apworld (a zip with the folder inside).

Usage: python tools/build_apworld.py [out_dir]     (default: dist/)
Install: copy the .apworld into Archipelago's custom_worlds/ folder (or double-click it).
"""
import json
import os
import sys
import zipfile

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
SRC = os.path.join(ROOT, 'apworld', 'gizmos_and_gadgets')


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'dist')
    os.makedirs(out_dir, exist_ok=True)
    dst = os.path.join(out_dir, 'gizmos_and_gadgets.apworld')
    with zipfile.ZipFile(dst, 'w', zipfile.ZIP_DEFLATED) as z:
        for base, dirs, files in os.walk(SRC):
            dirs[:] = [d for d in dirs if d != '__pycache__']
            for f in files:
                p = os.path.join(base, f)
                arc = os.path.join('gizmos_and_gadgets', os.path.relpath(p, SRC))
                if f == 'archipelago.json' and base == SRC:
                    # a packaged apworld's manifest also carries the container format versions
                    # (worlds/Files.py: APWorldContainer, compatible_version 7)
                    manifest = json.load(open(p))
                    manifest.update(version=7, compatible_version=7)
                    z.writestr(arc, json.dumps(manifest, indent=4))
                else:
                    z.write(p, arc)
    print('wrote', dst)


if __name__ == '__main__':
    main()
