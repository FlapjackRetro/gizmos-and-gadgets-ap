# Gizmos & Gadgets — Archipelago

An [Archipelago](https://github.com/ArchipelagoMW/Archipelago) randomizer for *Super Solvers: Gizmos & Gadgets!* (The Learning Company, DOS, v1.0).

**Status:** reverse-engineering / design. Nothing playable yet.

**Development target:** DOSBox-X. Other DOSBox flavors will be tested later.

## Contents
- `docs/RE_NOTES.md`: executable layout, `PLAYERS.DAT` save format, in-memory player record, hook points (race win confirmed).
- `docs/AP_DESIGN.md`: draft locations, items, progression, multi-level access plan and client architecture.
- `docs/DOSBOX_X_DEV.md`: DOSBox-X dev setup, debugger workflow and hook hunt list.
- `tools/dbgaddr.py`: prints DOSBox-X breakpoint commands for a given load segment.
- `tools/memdiff.py`: diffs MEMDUMPBIN dumps to find the variables an in-game action changes.
- `tools/parts_table.py`: decodes a level's part table from a MEMDUMP.
- `tools/part_names.py`: lists all part names from `gizmo000.dat` (output in `data/part_names.tsv`).
- `tools/players_dat.py`: dump players/progress from a `PLAYERS.DAT`.
- `tools/disasm.py`: symbolized disassembly of `ssg.exe`, including VROOMM overlays (needs `ndisasm`).

Bring your own copy of the game; no game files are included in this repository.
