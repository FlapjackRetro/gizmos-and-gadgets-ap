# Gizmos & Gadgets — Archipelago

An [Archipelago](https://archipelago.gg) randomizer for *Super Solvers: Gizmos & Gadgets!* (The Learning Company,
1993, DOS v1.0).

The randomizer is a mod for the game's native recompilation,
[gizmos-and-gadgets-recomp](https://github.com/FlapjackRetro/gizmos-and-gadgets-recomp), which runs the game
without DOSBox and loads mods. Together with an APWorld and a client, it turns the warehouses' part boxes and the
races against Morty into Archipelago checks.

**Installing / playing:** see the [Setup Guide](docs/setup.md). The mod (`ap_mod.dll`) and the APWorld
(`gizmosandgadgets.apworld`) are on the [Releases page](https://github.com/FlapjackRetro/gizmos-and-gadgets-ap/releases).

> **Status:** early development, not yet fully playtested. Bring your own copy of the game: no game files or game
> art are included in this repository; everything the mod draws is built at run time from your own files.

## How it plays

- **Locations:** every part box (532) and every race (15). A box is a check: opening it sends its location and
  shows what it held. Empty boxes still give bananas.
- **Items:** the 532 parts (each works only in its own level, as in the game) and bananas as filler. Received
  parts wait in the **Archipelago inventory** (the Pieces box in the assembly room) until you move them onto the
  conveyor, so the game's two-of-a-kind limit never loses one.
- **Levels:** a level picker after clicking a building; `level_access` opens levels race by race (as in the game)
  or all at once. Each level keeps its own warehouse. Won levels can be revisited for their remaining checks, but
  their race is locked.
- **Cyberchimps** (optional): a chimp that opens a check box carries the check until hit with a banana.
- **Logic:** a race is in logic once you hold parts that are guaranteed to beat Morty's build. The thresholds come
  from exact simulations of the game's race code (`data/race_requirements.tsv`, `data/flight_requirements.tsv`).
- **Goal:** win `races_required` different races (1–15).

## Layout

| Path | What it is |
|---|---|
| `mod/` | The game mod (C), built against the recomp's `include/gg_mod.h`: checks, items, level picker, per-level saves, the Archipelago inventory, Cyberchimps, won-level lock, item labels. See [docs/MOD.md](docs/MOD.md) |
| `mod/tests/` | A headless test helper mod and a host-side unit test |
| `apworld/gizmosandgadgets/` | The Archipelago world and client (Python). Package it with `python tools/build_apworld.py`. See [docs/CLIENT.md](docs/CLIENT.md) |
| `data/` | Tables extracted from the game: parts, race requirements, warehouse counts |
| `docs/setup.md` | Player setup guide |
| `docs/MOD.md`, `docs/CLIENT.md` | The mod and the APWorld/client in detail: behaviour, files, ids, tests |
| `docs/AP_DESIGN.md` | Design decisions and the to-do list |
| `docs/RE_NOTES.md` | Reverse-engineering notes on the game |
| `tools/` | Extraction, race simulation, packaging and analysis tools (below) |
| `assets/` | The Archipelago logo for the optional title-screen logo |

## Building

The mod (any 64-bit C compiler; see [docs/MOD.md](docs/MOD.md)):

```
cmake -S mod -B build-mod -DGG_RECOMP_DIR=path/to/gizmos-and-gadgets-recomp
cmake --build build-mod            # ap_mod.dll / ap_mod.so
```

The APWorld:

```
python tools/build_apworld.py      # dist/gizmosandgadgets.apworld
```

The release checklist is in the [Setup Guide](docs/setup.md#for-maintainers-building-a-release).

## Tools

- `tools/resources.py`: lists and extracts resources from `gizmo00x.dat`, decompressing with the game's own routine
  run in `tools/x86emu.py` (a minimal 8086 interpreter).
- `tools/extract_parts.py`, `tools/part_names.py`, `tools/parts_table.py`: part tables and names → `data/`.
- `tools/race_model.py`, `tools/race_sim.py`, `tools/race_logic.py`, `tools/flight_sim.py`: race models, exact
  simulations and per-level win requirements.
- `tools/warehouse.py`: static box/door counts per level.
- `tools/make_apworld_data.py`, `tools/build_apworld.py`: the APWorld's data file and packaging.
- `tools/lpx.py`, `tools/title_logo.py`, `tools/pieces_box.py`: the game's sprite format; the optional title-screen
  logo (`GIZMOAP1.DAT`); the Pieces box sprite.
- `tools/players_dat.py`: dump players and progress from a `PLAYERS.DAT`.
- `tools/disasm.py`, `tools/dbgaddr.py`, `tools/memdiff.py`: disassembly and debugging aids.
- `tools/patcher.py`, `docs/DOSBOX_X_DEV.md`, `docs/KNOWN_ISSUES.md`: the earlier DOSBox-X approach (EXE patches,
  emulator memory polling), kept for reference only.

## Third-party assets

The Archipelago logo in `assets/` (and the generated `GIZMOAP1.DAT`) is © the Archipelago project: the original
logo is MIT, the newer logos are derivative works under CC BY-NC 4.0 (version assumed). See
[`COPYING.md`](COPYING.md). *Gizmos & Gadgets* is © 1993 The Learning Company.
