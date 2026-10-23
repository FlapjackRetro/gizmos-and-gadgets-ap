# The apworld and the client

`apworld/gizmos_and_gadgets/` is the Archipelago world (generation) and the client (play). The game side is the
recomp mod (`docs/MOD.md`); the client connects it to the server through the files in `<game folder>/ap/`.

## Install

```sh
python tools/build_apworld.py            # -> dist/gizmos_and_gadgets.apworld
```
Copy the `.apworld` into Archipelago's `custom_worlds/` folder. Requires Archipelago 0.6.9 or newer. The data in
`apworld/gizmos_and_gadgets/data.py` is generated from `data/*.tsv` by `tools/make_apworld_data.py`.

## The world (v0.1.0)

| | |
|---|---|
| Locations | 15 races, 532 part boxes (the box that holds each part), optional 653 puzzle doors (`door_checks`, off by default) |
| Items | the 532 parts (per level: a part only works in its own level, as in the game), `5 Bananas` as filler |
| Access | vanilla order: a building's level *n*+1 opens when its level *n* race is won; earlier levels stay open (`/level`) |
| Race rule | every non-cosmetic slot of the level built, stat slots with a part of tier ≥ min(k, best tier) — the thresholds the race simulations proved sufficient (`data/race_requirements.tsv`, `data/flight_requirements.tsv`) |
| Classification | parts that meet their slot's threshold: progression; other non-cosmetic parts: useful; paint/decals: filler |
| Goal | all 15 races won |

Ids are the mod's (`docs/MOD.md`), e.g. part 4 of Aircraft level 1 = location and item `0x47471144`.
Item names: `"<Building> Level <n> - <part name>"` (plus `#<part>` when a level has two parts of that name);
box locations are the item name plus ` (box)`.

## The client

Launcher → **Gizmos and Gadgets Client**, or
`python -m worlds.gizmos_and_gadgets.client --gamedir <game folder> --connect <host:port> --name <slot>`.

- Forwards new lines of `ap/checks.txt` as `LocationChecks`, and `goal` as the goal status.
- Keeps `ap/received.txt` equal to the server's received-items list (own items included: boxes never give their
  part directly), so the mod grants each one once, in order.
- Writes `ap/config.txt` (`door_checks` from the slot data, level choices).
- Binds the game folder to one seed and slot (`ap/seed.txt`); another seed in the same folder is refused.
- `/gamedir <path>`, `/level` (show), `/level <automotive|aircraft|alt> <1-5|auto>` (which level the building loads
  next; only unlocked levels).

## Tested (2026-10-22, Archipelago 0.6.9, this workspace)

- `test/` (WorldTestBase): all-state reachability, fill, counts with and without doors, mod ids, first race needs its
  parts, level 2 locked until the race: 17 tests pass.
- Generation with Gizmos and Gadgets + APQuest, from the folder and from the packaged `.apworld`.
- End to end on a local MultiServer: a server `/send` of an Aircraft L1 part → `received.txt` → the mod granted it
  in the warehouse ("needed"); a box opened in the headless recomp → `checks.txt` → client → server
  ("Flapjack sent 5 Bananas to Flapjack (Aircraft Level 1 - fat envelope (box))") → the bananas came back and were
  granted; `/level aircraft 2` refused (not unlocked), `/level aircraft 1` written to the config.

## To do
- Race end and goal not exercised end to end yet (needs a built vehicle headless).
- In-game text for found items (other players' items show nothing yet).
- Level picker in game instead of `/level`; options for open world (all levels open) and level unlock items.
- DeathLink / hints display / GUI tab, if wanted.
