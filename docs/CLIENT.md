# The apworld and the client

`apworld/gizmos_and_gadgets/` is the Archipelago world (generation) and the client (play). The game side is the
recomp mod (`docs/MOD.md`); the client connects it to the server through the files in `<game folder>/ap/`.

## Install

```sh
python tools/build_apworld.py            # -> dist/gizmos_and_gadgets.apworld
```
Copy the `.apworld` into Archipelago's `custom_worlds/` folder. Requires Archipelago 0.6.9 or newer. The data in
`apworld/gizmos_and_gadgets/data.py` is generated from `data/*.tsv` by `tools/make_apworld_data.py`.

## The world (v0.3.0)

| | |
|---|---|
| Locations | 15 races, 532 part boxes (the box that holds each part). Puzzle doors are left out for now |
| Items | the 532 parts (per level: a part only works in its own level, as in the game), `5 Bananas` as filler |
| Access | option `level_access`: `race_progression` (default: a building's level *n*+1 opens when its level *n* race is won; won levels stay open) or `open` (all 15 levels from the start). The in-game level picker shows the open levels |
| Race rule | every non-cosmetic slot of the level built, stat slots with a part of tier ≥ min(k, best tier) — the thresholds the race simulations proved sufficient (`data/race_requirements.tsv`, `data/flight_requirements.tsv`) |
| Classification | parts that meet their slot's threshold: progression; other non-cosmetic parts: useful; paint/decals: filler |
| Option `cyberchimps` | on (default): play with Cyberchimps; off: none. Replaces the game's question for a new player (the mod shows the setting) and is applied on every level entry |
| Option `races_required` | 1–15 (default 15): how many different races must be won |
| Goal | `races_required` different races won |

Ids are the mod's (`docs/MOD.md`), e.g. part 4 of Aircraft level 1 = location and item `0x47471144`.
Item names: `"<Building> Level <n> - <part name>"` (plus `#<part>` when a level has two parts of that name);
box locations are the item name plus ` (box)`.

## The client

Launcher → **Gizmos and Gadgets Client**, or
`python -m worlds.gizmos_and_gadgets.client --gamedir <game folder> --connect <host:port> --name <slot>`.

- Forwards new lines of `ap/checks.txt` as `LocationChecks`, and `goal` as the goal status.
- Passes the options to the mod (`config.txt`): `chimps`, `races_required`, level availability.
- Keeps `ap/received.txt` equal to the server's received-items list (own items included: boxes never give their
  part directly), so the mod grants each one once, in order.
- Writes `ap/config.txt`: settings (`chimps` from the `cyberchimps` option) and `avail.<building>` (which levels
  the picker offers), updated when a race location is checked.
- **Game connection:** watches `ap/game.txt` (the mod rewrites it every second): logs "Game connected" with the
  signed-in player, place and how many received items are in the game, and "Game not responding" when it stops.
- Binds the game folder to one seed and slot (`ap/seed.txt`), and records the in-game player that plays it; another
  seed in the same folder is refused, and a different signed-in player gets a warning. This is what makes picking
  up a half-played seed safe: same folder, same player, and the client says when something does not match.
- Commands: `/gamedir <path>`, `/game` (game status), `/levels` (open levels).

## Tested (2026-10-22, Archipelago 0.6.9, this workspace)

- `test/` (WorldTestBase): all-state reachability, fill, counts, mod ids, first race needs its
  parts, level 2 locked until the race: 21 tests pass (default and `level_access: open`).
- Generation with Gizmos and Gadgets + APQuest, from the folder and from the packaged `.apworld`.
- End to end on a local MultiServer: a server `/send` of an Aircraft L1 part → `received.txt` → the mod granted it
  in the warehouse ("needed"); a box opened in the headless recomp → `checks.txt` → client → server
  ("Flapjack sent 5 Bananas to Flapjack (Aircraft Level 1 - fat envelope (box))") → the bananas came back and were
  granted; game connected / not responding detected, in-game player recorded; a race location checked → the next
  level opened in the picker.

## To do
- In-game text for found items (other players' items show nothing yet).
- Level unlock items (a third `level_access` mode), if wanted.
- DeathLink / hints display / GUI tab, if wanted.
