# The Archipelago mod (recomp)

The randomizer runs on the native recompilation of the game, `gizmos-and-gadgets-recomp`, as a mod
(`mod/ap_mod.c`). The recomp loads shared libraries from `<game dir>/mods/` and gives them typed game events
(see the recomp's `docs/MODDING.md` and `include/gg_mod.h`, API version 1). No `ssg.exe` patching and no
emulator memory scanning are needed.

## Build

```sh
cmake -S mod -B build-mod -DGG_RECOMP_DIR=/path/to/gizmos-and-gadgets-recomp
cmake --build build-mod            # ap_mod.so / ap_mod.dll
```
Copy `ap_mod.dll` (Windows) or `ap_mod.so` into `<game dir>/mods/`, or run `gizmos --mod path/to/ap_mod.so`.
The mod must be 64-bit like the game (see the recomp's MODDING.md for MinGW/MSVC command lines).

## What it does (v0)

| Game event | Mod |
|---|---|
| Building clicked | `open_world=1`: always let the player in, even when all 5 races are won |
| Level about to load | loads `level.<building>` from the config if set (else the game's choice, capped at level 5, which fixes the "Level-6 Blimp" overflow); a saved warehouse is only restored if it belongs to that level, otherwise a fresh one is built |
| Box opened, holding a part | sends the box's location; the box disappears and the game gives nothing (the item comes back from the server, also for our own items) |
| Box opened, empty | vanilla (+5 bananas), not a check |
| Box opened, holding a part the player recycled (box sprite `0x333`/`0x344`) | vanilla pickup, not a check |
| Door opened (puzzle solved) | sends the door's location |
| Race won | sends the race location; `goal` once the game's total-races counter reaches 15 |
| In the warehouse | grants received items, one about every 1.5 s: parts of the current level via the game's own pickup routine (name shown, PARTS NEEDED counts down), bananas anywhere |

Received parts wait until the player is in their level's warehouse. A part waits while the player already holds
two of that kind (the inventory limit; see the to-do in `AP_DESIGN.md`). When a level's warehouse is generated
fresh, the parts already received for it are granted again.

## Files (`<game dir>/ap/`)

| File | Written by | Content |
|---|---|---|
| `checks.txt` | mod (appends) | one location id per line; `goal` when the game is complete |
| `received.txt` | client (appends) | one item id per line, in the order the server sent them (item index = line number) |
| `config.txt` | client / user | `key=value`: `open_world`, `box_checks`, `door_checks` (1/0, default 1), `level.0` / `level.1` / `level.2` (0-based level to load for Automotive / Aircraft / Alt-Energy, -1 = game's choice). Re-read on every level entry |
| `state.txt` | mod | which level each building's saved warehouse belongs to, which received items are already in the game |

The Python client (to do) connects to the Archipelago server, forwards `checks.txt` lines as `LocationChecks`,
appends received items to `received.txt`, and writes `config.txt` (level unlocks / level picker).

## Ids

Base `0x47470000` (1195835392). `v` = building × 5 + level (0–14).

| Kind | Id | Count |
|---|---|---|
| Race won | base + `0x0000` + v | 15 |
| Part box | base + `0x1000` + v × `0x40` + part | 532 (one per part of each level; the box follows its part) |
| Puzzle door | base + `0x2000` + v × `0x40` + rank | 653 |
| Item: part | base + `0x1000` + v × `0x40` + part | 532 |
| Item: +5 bananas | base + `0x0F00` | filler |

- **Boxes** are identified by the part inside, not by position: box placement is random per fresh warehouse, but
  every part of a level is in exactly one box (RE_NOTES §4.11), so a location travels with its part. The 90 empty
  boxes are not checks. How parts move (traced in code, 2026-10-22):
  - **Chimp opens a box** (`O0E0D_0000`): a part goes into the chimp's inventory (`R0834_08D7(chimp, part)`) and
    the box is removed. The chimp keeps it. An *empty* box opened by a chimp is moved to a random other room
    (`R0622_0F4E(1000)`).
  - **Chimp drops a part** (`O0E0D_00A1`, an actor-script command run from `R0834` at 0x48F; the banana hit is the
    likely trigger, not yet confirmed): the part leaves the chimp's inventory and a new box holding it appears where
    the chimp stands (`R0622_0DF0`). Opening it fires the normal box event with the same part → same location.
  - **Recycled parts are never checks** (design rule): the player can only recycle a part they actually obtained
    (from their own game or received through the client), by dropping it on the recycle bin (warehouse HUD
    `R0A04_0A82`, or the assembly room) or, later, from the Archipelago inventory dialog. Recycling sets part
    `+0x36 |= 0x4000` and puts the part into a box in a random room; every box-creating routine (`R0622_0A76`,
    `R0622_0DF0`) then gives that box sprite `0x333`/`0x344` instead of `0x332`/`0x343`. The mod reads the
    **box's** sprite (box = `R0622_1265(room, object)`, sprite at `+0x1A`) and lets recycled boxes through as
    vanilla pickups. It checks the box, not the part flag, because the part's original box (its location) can
    still be unopened when the player recycles a received copy of it. Verified headless (a recycled part dropped
    as a box → vanilla pickup, no check). The Archipelago inventory's "recycle" must go through the same
    mechanism (set 0x4000, then `R0622_0F4E(part)`), so its boxes are marked too.
  - **Pickup with two of that kind already held** (`R08F5_0002` → `R053F_0216 == 2`): refused with a message
    (`0x1047`); the box is not removed and the part stays in it.
  - **Part owner** (part table `+0x0C`): -1 = in a box, 0 = the player, n = actor n (chimps). E.g. a chimp may
    already hold a part when the level starts. This is how the mod can count checks held by chimps.
  - **Open question:** `R08F5_0180` (a player-actor script command, slot `+0x6A`) moves a random part from
    another actor's inventory straight into the player's; if the player already holds two of that kind, one of
    theirs goes into a box in a random room (`R0622_0F4E`, not marked recycled). If this is how the player gets a
    part back from a chimp, it bypasses the box event (no check is sent, the real part is given). Needs a live
    test of what a banana hit does, then a hook.
- **Doors**: both sides of a door share a key (lower room, higher room, pair number `link >> 6`); the rank is the
  key's position among the level's puzzle doors (kind `+0x16 == 8`), sorted. Computed by the mod from the rooms in
  memory, and the same as `tools/warehouse.py`'s static count, so the apworld only needs the counts per level.

## Verified headless (recomp, 2026-10-22)

From a blank `PLAYERS.DAT`, Aircraft level 1 (v = 5), with `mod/tests/test_driver.c` (opens a box from code,
forces puzzles to pass):
- two items in `received.txt` (part 1 of v5, +5 bananas) were granted on entering the warehouse ("needed": PARTS
  NEEDED counted down);
- box 1 of room 1 (part 4) → location `0x47471144` sent, box removed, nothing given; box 0 (empty) → vanilla;
- puzzle door 49 of room 1 (room 1 ↔ 6, pair 16) → door rank 12, matching the static computation;
- `level.1=3` in `config.txt` → Aircraft level 4 loaded directly on a new player.

Not yet tested live: race end, restore of a saved warehouse, the goal line.

## Known limitations / to do
- No in-game text for found items yet (the box just disappears); needs the item-label work.
- A player who enters a later level directly starts with 0 bananas (the game sets 10 only on level 1).
- If the player quits without saving after an item was granted, the restored warehouse lacks it but the mod has
  it marked as granted. Fix: check the part table's owner field (`+0x0C`, 0 = player) instead of remembering.
- Level picker UI (pager dialog) instead of `config.txt`; inventory-full prompt; chimps carrying checks.
