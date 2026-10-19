# Gizmos & Gadgets — Archipelago

An [Archipelago](https://github.com/ArchipelagoMW/Archipelago) randomizer for *Super Solvers: Gizmos & Gadgets!* (The Learning Company, DOS, v1.0).

**Status:** reverse-engineering / design. Nothing playable yet.

**Development target:** DOSBox-X. Other DOSBox flavors will be tested later.

## Patching
`python tools/patcher.py path/to/ssg.exe` writes `SSG_AP.EXE` next to it (original untouched). `--list` shows
the patches; each one verifies the original bytes first.

## Contents
- `docs/RE_NOTES.md`: executable layout, `PLAYERS.DAT` save format, in-memory player record, hook points (race win confirmed).
- `docs/AP_DESIGN.md`: draft locations, items, progression, multi-level access plan and client architecture.
- `docs/KNOWN_ISSUES.md`: observed issues and their status.
- `docs/DOSBOX_X_DEV.md`: DOSBox-X dev setup, debugger workflow and hook hunt list.
- `tools/dbgaddr.py`: prints DOSBox-X breakpoint commands for a given load segment.
- `tools/memdiff.py`: diffs MEMDUMPBIN dumps to find the variables an in-game action changes.
- `tools/resources.py`: list/extract resources from `gizmo00x.dat`, decompressing with the game's own routine run in `tools/x86emu.py` (a minimal 8086 interpreter).
- `tools/extract_parts.py`: every vehicle's part table → `data/parts_all.tsv`.
- `tools/patcher.py`: byte patches for ssg.exe → `SSG_AP.EXE`.
- `tools/race_model.py`: ground-race stats model and Morty's builds → `data/morty_builds.tsv` (not yet live-validated).
- `tools/race_sim.py` / `tools/race_logic.py`: exact ground-race simulation and per-level win requirements → `data/race_requirements.tsv`.
- `tools/flight_sim.py`: aircraft race model + simulation → `data/flight_requirements.tsv` (validated live on Aircraft L3).
- `tools/parts_table.py`: decodes a level's part table from a MEMDUMP.
- `tools/part_names.py`: lists all part names from `gizmo000.dat` (output in `data/part_names.tsv`).
- `tools/players_dat.py`: dump players/progress from a `PLAYERS.DAT`.
- `tools/lpx.py` / `tools/title_logo.py`: LPX sprite codec; builds `GIZMOAP1.DAT` with the Archipelago logo on the title screen.
- `tools/warehouse.py`: static box/door counts per level → `data/warehouse_counts.tsv`.
- `tools/disasm.py`: symbolized disassembly of `ssg.exe`, including VROOMM overlays (needs `ndisasm`).

Bring your own copy of the game; no game files are included in this repository.

## Third-party assets
The Archipelago logo in `assets/` (and the generated `GIZMOAP1.DAT`) is © the Archipelago project: the original logo is MIT, the newer logos are derivative works under CC BY-NC 4.0 (version assumed). See [`COPYING.md`](COPYING.md).
