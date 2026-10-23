# The apworld and the client

`apworld/gizmosandgadgets/` is the Archipelago world (generation) and the client (play). The game side is the
recomp mod (`docs/MOD.md`); the client connects it to the server through the files in `<game folder>/ap/`.

## Install

```sh
python tools/build_apworld.py            # -> dist/gizmosandgadgets.apworld
```
Copy the `.apworld` into Archipelago's `custom_worlds/` folder (or double-click it). Players: see [setup.md](setup.md). Requires Archipelago 0.6.8 or newer. The data in
`apworld/gizmosandgadgets/data.py` is generated from `data/*.tsv` by `tools/make_apworld_data.py`.

## The world (v0.4.0)

| | |
|---|---|
| Locations | 15 races, 532 part boxes (the box that holds each part). Puzzle doors are left out for now |
| Items | the 532 parts (per level: a part only works in its own level, as in the game), `5 Bananas` as filler |
| Access | option `level_access`: `race_progression` (default: a building's level *n*+1 opens when its level *n* race is won; won levels stay open), `open` (all 15 levels from the start) or `item_progression` (level 1 of each building open; the other 12 open with their `<Building> Level <n> Access` item, base + `0x0E00` + v, in any order). The in-game level picker shows the open levels |
| Race rule | every non-cosmetic slot of the level built, stat slots with a part of tier ≥ min(k, best tier) — the thresholds the race simulations proved sufficient (`data/race_requirements.tsv`, `data/flight_requirements.tsv`) |
| Classification | parts that meet their slot's threshold: progression; other non-cosmetic parts: useful; paint/decals: filler |
| Option `cyberchimps` | on (default): play with Cyberchimps; off: none. Replaces the game's question for a new player (the mod shows the setting) and is applied on every level entry |
| Option `races_required` | 1–15 (default 15): how many different races must be won |
| Goal | `races_required` different races won |

Ids are the mod's (`docs/MOD.md`), e.g. part 4 of Aircraft level 1 = location and item `0x47471144`.
Item names: `"<Building> Level <n> - <part name>"` (plus `#<part>` when a level has two parts of that name);
box locations are `"<Building> Level <n> - Box <k>"`, k = 1 … the level's part count (the box of the level's part
k − 1 in the game's part table; named by number, since in a seed a box holds any item).

## The client

Launcher → **Gizmos and Gadgets Client**, or
`python -m worlds.gizmosandgadgets.client --gamedir <game folder> --connect <host:port> --name <slot>`.

- Forwards new lines of `ap/checks.txt` as `LocationChecks`, and `goal` as the goal status.
- Passes the options to the mod (`config.txt`): `chimps`, `races_required`, level availability.
- Scouts the slot's locations on connecting and writes `ap/labels.txt`, so an opened box shows its item.
- Keeps `ap/received.txt` equal to the server's received-items list (own items included: boxes never give their
  part directly), so the mod grants each one once, in order.
- Writes `ap/config.txt`: settings (`chimps` from the `cyberchimps` option) and `avail.<building>` (which levels
  the picker offers), updated when a race location is checked.
- **Game connection:** watches `ap/game.txt` (the mod rewrites it every second): logs "Game connected" with the
  signed-in player, place and how many received items are in the game, and "Game not responding" when it stops.
- Binds the game folder to one seed and slot (`ap/seed.txt`), and records the in-game player that plays it; another
  seed in the same folder is refused, and a different signed-in player gets a warning. This is what makes picking
  up a half-played seed safe: same folder, same player, and the client says when something does not match.
- Remembers the last game folder (`gizmos_and_gadgets_gamedir.txt` in Archipelago's user folder).
- Commands: `/gamedir <path>`, `/game` (game status), `/levels` (open levels).

## Tested (2026-10-22, Archipelago 0.6.9; 2026-10-23 also 0.6.8: tests, templates, generation, client)

- `test/` (WorldTestBase): all-state reachability, fill, counts, mod ids, first race needs its
  parts, level 2 locked until the race: 25 tests pass (default and `level_access: open`).
- Generation with Gizmos and Gadgets + APQuest, from the folder and from the packaged `.apworld`.
- End to end on a local MultiServer: a server `/send` of an Aircraft L1 part → `received.txt` → the mod granted it
  in the warehouse ("needed"); a box opened in the headless recomp → `checks.txt` → client → server
  ("Flapjack sent 5 Bananas to Flapjack (Aircraft Level 1 - fat envelope (box))") → the bananas came back and were
  granted; game connected / not responding detected, in-game player recorded; a race location checked → the next
  level opened in the picker.

## To do
- DeathLink / hints display / GUI tab, if wanted.
