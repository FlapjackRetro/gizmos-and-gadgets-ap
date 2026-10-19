# Gizmos & Gadgets — Archipelago

An [Archipelago](https://github.com/ArchipelagoMW/Archipelago) randomizer for *Super Solvers: Gizmos & Gadgets!* (The Learning Company, DOS, v1.0).

**Status:** reverse-engineering / design. Nothing playable yet.

## Contents
- `docs/RE_NOTES.md`: executable layout, `PLAYERS.DAT` save format, in-memory player record, hook points (race win confirmed).
- `docs/AP_DESIGN.md`: draft locations, items, progression, multi-level access plan and client architecture.
- `tools/players_dat.py`: dump players/progress from a `PLAYERS.DAT`.
- `tools/disasm.py`: symbolized disassembly of `ssg.exe`, including VROOMM overlays (needs `ndisasm`).

Bring your own copy of the game; no game files are included in this repository.
