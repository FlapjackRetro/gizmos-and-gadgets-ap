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
The mod is `ap_mod.c` + `picker.c` + `hud.c` + `inventory.c` + `ui.c` (game-style dialogs, shared). Copy `ap_mod.dll` (Windows) or `ap_mod.so` into `<game dir>/mods/`, or run `gizmos --mod path/to/ap_mod.so`.
The mod must be 64-bit like the game (see the recomp's MODDING.md for MinGW/MSVC command lines).

## What it does (v0)

| Game event | Mod |
|---|---|
| Building clicked | `open_world=1`: always let the player in, even when all 5 races are won |
| Building chosen | the **level picker** (below); "Return to Technology Center" stays on the Technology Center screen |
| Level about to load | loads the picked level; restores its saved warehouse if it was played before, else generates it (once) |
| Level ready | saves at once (the game's own save), so a level is never generated twice |
| Box opened, holding a part | sends the box's location; the box disappears and the game gives nothing (the item comes back from the server, also for our own items); the HUD shows what the box held (`labels.txt`) |
| Box opened, empty | vanilla (+5 bananas), not a check |
| Box opened, holding a part the player recycled or once held (box sprite `0x333`/`0x344`) | vanilla pickup, not a check |
| Door opened (puzzle solved) | sends the door's location only with `door_checks=1` (off: doors are not locations for now) |
| Race started | keeps a copy of the level's warehouse (a won race discards it); a won level is restored from it |
| Podium after a win | Rerun Race refused; Next Level only to an open level, else the Technology Center (site `podium_choice`, recomp PR #3) |
| Won level, assembly room | race door stays shut even with the vehicle complete; clicking it says the race was won (below) |
| Race won | sends the race location; keeps the game's race counters right for any order; `goal` once all 15 levels are won |
| In the warehouse / assembly room | grants received bananas (+5 each), one about every 1.5 s. Received parts go to the Archipelago inventory |
| Assembly room / blueprint | draws in the bottom-left of the HUD: the "Pieces" box with the number of received parts of this level in the Archipelago inventory, and a Cyberchimp (future: checks held by chimps). `hud.c`; built at run time from the game's own door-puzzle background `0x2084`, digits `0x204F` and chimp frame `0x103B`/1, colours remapped to the assembly room's |
| Pieces box clicked (assembly room) | the **Archipelago inventory** dialog (below) |
| New player created | with `chimps` known: instead of the game's "With Chimps / Without Chimps" question (site `chimps_prompt`, recomp PR #2), a message states the YAML's setting, which the game stores (`DS:4578`) |
| Level about to load (also) | `chimps` known: `DS:4578` set from it, so an older player file follows the YAML too |
| Cyberchimp clicked (assembly room) | "Cyberchimps": the checks this level's chimps carry, and how many of the player's own parts they have (never checks). Playing without chimps: the sprite is grey and not clickable |
| About once a second | writes `ap/game.txt` (heartbeat for the client) |

## Archipelago inventory

Received parts are not put into the game by themselves: they wait in the Archipelago inventory, outside the
game's inventory lists, so chimps cannot take them and the two-of-a-kind limit never loses one. In the
assembly room, a click on the Pieces box opens a dialog in the game's style (`inventory.c`, drawn with
`ui.c`) listing every received part that is not in the game yet:

- Only the parts of the level that is loaded (a part only works in its own level; the others are listed when
  their level is). Six rows; Up/Down move, PgUp/PgDn/Home/End
  and the Up/Down buttons scroll, Enter or a click picks, Esc or Close closes. Names come from the game's own
  part tables (resource `0x2F5 + v`, entry `+0x16` = the name's text resource).
- Picking a part checks the game's limit (two parts of a kind, `R053F_0216` on the player's list as the pickup
  does). When full: "No room on the conveyor -- recycle one of them to make space", and nothing else happens.
  Otherwise the part is given (`api->give_part`, PARTS NEEDED counts down), leaves the list and the dialog stays
  open for more picks.
- On closing, the conveyor is updated in place (sprites loaded, the room's lay-out run again, the loop's own
  belt copy updated: RE_NOTES §4.14) and the level is saved. A moved part is an ordinary part: floor or recycle
  bin, never back into the Archipelago inventory.
- A part a Cyberchimp carries as a real record (only possible without the chimp hook sites) is taken out of the
  chimp's list before it is given (`R0834_0990`).
- How the click is seen: the box is a mouse region in the room's own list with click code `0xF00`; the room's
  loop leaves unknown codes below `0xFA0` alone and keeps the last one in `[bp-0x22]`, which the mod reads at the
  next loop head (site `assembly_tick`).

## Level picker

After a building is clicked at the Technology Center, a dialog in the game's style lists
`Level 1 - <vehicle>` … `Level 5 - <vehicle>` and `Return to Technology Center`. Vehicle names come from the
game's blueprint resources (`Level 1 - Blimp`, `Level 4 - Class III Gas-Powered Racer`). Levels that are not open
are greyed out and cannot be focused or picked. Keys: Up/Left and Down/Right move (skipping greyed entries,
wrapping), Enter picks, Esc returns, `1`–`5` and `r` are hotkeys; the mouse works as in the game's dialogs
(hover moves the focus, a click picks). Open levels come from the client (`avail.<b>` in `config.txt`, from the
seed's `level_access`); without a client: level 1, each level after a won one, and won levels.

How: the site `building_selected` (`R02E0_0148` at `0x463`, `mov [bp-6], 1`) runs the picker; returning 1 skips
the instruction, so the select loop continues as if nothing was clicked. The game's dialog runner cannot be used
as is (one row of buttons; its structs are passed by value, beyond `call_far`'s 8 words), so `mod/picker.c`
repeats its steps with the game's own primitives (RE_NOTES §4.12): same box, buttons, focus, press animation,
mouse regions and input loop.

## Cyberchimps and check boxes

A box holds a part *number*; each level's part table has one record per part, and inventories are lists of
those records. The record of an unopened check box's part is only a stand-in for the box's location: the
player may already hold the real part, received through the Archipelago inventory. Opening a box by the
player is intercepted (box events above); a chimp opening one is intercepted too, so the stand-in never moves:

| Site | Mod |
|---|---|
| `chimp_box_open` (`O0E0D_0000`, after `R053F_02BB` = parts the chimp holds) | check box (normal sprite): the mod removes the box, notes "chimp n carries the check of part p" and the chimp's real inventory stays as it was. A chimp that already carries two checks is full: any part box it opens (check or recycled) is moved to a random room instead (`R0622_0F4E`, as the game does with an empty box a chimp opens; a check box keeps its normal sprite). Moving the box rather than refusing matters: refusing made a chimp pick at one box forever (seen in play), since its real inventory stays empty and the game's chimp logic keeps going for boxes. Recycled boxes (chimp not full) and empty boxes stay vanilla |
| `chimp_steal` (`R08F5_0180`, a chimp touching the player, after the part to take is chosen; `di` = chimp) | a chimp holding two things (carried checks + real parts, `R053F_02BB`) steals nothing |
| `chimp_drop` (`O0E0D_00A1`, the part a banana-hit chimp drops) | carried checks drop **first**, one per hit, as a check box at the chimp's feet (normal sprite, even if the player holds the real part); then the chimp's real parts, as vanilla. Opening that box sends the check |

**At most two per chimp.** A chimp holds at most two things, carried checks and real parts together, so one
chimp can't run off with half a level. In vanilla a chimp takes a box only with empty hands, so the box route
gives at most two checks, or one check and one part; stealing from the player stops at two as well.

Carried checks are saved in `state.txt` (`chimp=v,actor,part`) and the game is saved at the next safe point, so
the warehouse and the list stay together; a fresh level starts with none. Verified headless: a game chimp
took a check box in normal play (the part record stayed), and a scripted chimp took one, dropped it after a
banana, and the player's opening sent the check (`TEST_CHIMP`). The cap (`TEST_CHIMP_FULL`): a chimp took two
check boxes, the third (a check box) went to another room with the normal sprite, the full chimp stole nothing
from the player, another chimp stole one part; in normal play a chimp with two checks moved the boxes it met.

## Bananas: one count per seed

The game keeps the banana count in the part of the player record that is saved with each warehouse (`rec+0x1C`,
`DS:457E`, in the block's 20-byte header), and level 1 of a building starts with 10. With a save per level, each
level would keep its own bananas, and received bananas would stay in the level that was loaded. The mod keeps one
count for the seed (`state.txt`: `bananas=`): it follows the game's counter in the warehouse and the assembly room
(picked up, thrown, received) and writes it into every level when it is ready. A new seed starts with 10.

## Won levels

A won level can be entered again (picker) for its remaining checks; it is restored from the copy taken at race
start, so the winning vehicle and the floor parts are there. Its race is locked: the race door (`O0E30_00A6`)
opens and the race starts (`0x12FA`) only while `[4584]` (slots filled) equals the level's slot count, so in a
won level the mod keeps `[4584]` 0x100 above the real count (at level ready and every assembly pass; parts can
still be placed and taken off, the count follows), keeps `[4586]` 0 (no +100 bonus, no "finished building"
hint), and gives the door's region (`DS:223C`) the code `0xF02`, which the loop head answers with "You won this
level's race already". After a win, the podium refuses Rerun Race, and Next Level goes to the building's next
level only when the seed has opened it (else to the Technology Center).

## One saved warehouse per level

The player record holds one warehouse per building. The mod keeps one per level in `ap/levels/<v>.bin` (a copy
of the building's 0x6BA-byte block) and swaps it in before the game loads a level, so the game restores it.
`state.txt` records which level the block in `PLAYERS.DAT` holds; it changes only after the game has saved that
level (at level ready), so a copy is never filed under the wrong level. Copies are taken before the block is
replaced, at race start, at level ready and after each granted part. Race counters: the game adds 1 to the
building's "last race won" and to the total after a win; the mod first sets the building's counter to its
distinct wins - 1 and does not let a replayed race count twice (`mod/tests/unit_race.c`).

## Title-screen logo

`python tools/title_logo.py <game dir> assets/archipelago_logo.png` writes `GIZMOAP1.DAT` (a copy of
`gizmo001.dat` with the Archipelago logo in the title sprite; the original is untouched) into the game folder.
At startup the mod finds it and points the game's archive-1 name (`DS:057E`, "gizmo001.dat") at it: the
recomp has loaded the game image before mods start and opens the archives later, so this is a memory write, not
an EXE patch. Without the file (or with `title_logo=0`) the game uses `gizmo001.dat` as usual. Verified on
the recomp, headless: the logo fades in with the main title. The logo is © the Archipelago project, CC BY-NC
4.0 (see `COPYING.md`), so `GIZMOAP1.DAT` is too.

## Files (`<game dir>/ap/`)

Each seed and slot has its own folder, `ap/<seed>_<team>_<slot>/` (the client makes it on the first connection).
The mod follows `ap/current.txt`: at startup, while it has no seed yet (memory writes only until then: without a
seed the game runs unmodified), and at every building click at the Technology Center, never inside a level. A
seed is bound to the first in-game player that enters a building with it (`state.txt` `player=`, and
`ap/players.txt` `<player>=<folder>`); entering a building with another player, or with a player of another seed,
is refused with a message (site `building_selected`), so an old player never picks up a new seed's warehouses.

| File | Written by | Content |
|---|---|---|
| `current.txt` (in `ap/`) | client | the folder of the connected seed/slot |
| `game.txt` (in `ap/`) | mod (every ~1 s) | `mod`, `frame`, `player`, `building`, `level`, `in_level`, `items` / `received` (received items the mod has read), `ap_inventory` (parts waiting in the Archipelago inventory), `session` (the folder in use) |
| `players.txt` (in `ap/`) | mod | `<in-game player>=<folder>`, one line per seed's player |
| `checks.txt` | mod (appends) | one location id per line; `goal` when the game is complete |
| `received.txt` | client (appends) | one item id per line, in the order the server sent them (item index = line number) |
| `config.txt` | client | `key=value`: `open_world`, `box_checks`, `title_logo` (1/0, default 1; `title_logo` read at startup), `door_checks` (default 0), `chimps` (1/0, from the YAML; absent = the game asks), `races_required` (goal, default 15), `avail.0`/`avail.1`/`avail.2` = five `0`/`1` (levels open in the picker). Re-read on every building click and level entry |
| `state.txt` | mod | level held by each building's block in `PLAYERS.DAT`, races won (bit per level), items in the game, checks carried by chimps, the seed's player |
| `levels/<v>.bin` | mod | the saved warehouse of level `v` |
| `labels.txt` | client | `<location id>=<text>`: what each location of this slot holds, shown when its box opens |
| `seed.txt` | client | the seed/slot this folder is for |

## Ids

Base `0x47470000` (1195835392). `v` = building × 5 + level (0–14).

| Kind | Id | Count |
|---|---|---|
| Race won | base + `0x0000` + v | 15 |
| Part box | base + `0x1000` + v × `0x40` + part | 532 (one per part of each level; the box follows its part) |
| Puzzle door (not used for now) | base + `0x2000` + v × `0x40` + rank | 653 |
| Item: part | base + `0x1000` + v × `0x40` + part | 532 |
| Item: +5 bananas | base + `0x0F00` | filler |
| Item: level access (`level_access: item_progression`) | base + `0x0E00` + v | 12 (levels 2–5 of each building) |

- **Boxes** are identified by the part inside, not by position: box placement is random per fresh warehouse, but
  every part of a level is in exactly one box (RE_NOTES §4.11), so a location travels with its part. The 90 empty
  boxes are not checks. How parts move (traced in code, 2026-10-22):
  - **Chimp opens a box** (`O0E0D_0000`): a part goes into the chimp's inventory (`R0834_08D7(chimp, part)`) and
    the box is removed. The chimp keeps it. An *empty* box opened by a chimp is moved to a random other room
    (`R0622_0F4E(1000)`).
  - **Chimp drops a part** when hit by a banana (confirmed in play): `O0E0D_00A1` removes a part from the chimp's
    inventory and creates a box holding it where the chimp stands (`R0622_0DF0`, normal sprite). The player walks
    over and opens it like any box → the normal box event with the same part → same location.
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
    (`0x1047`); the box is not removed and the part stays in it (also for a chimp-dropped box). The mod's box hook
    runs before that test, so a **check box always opens**: the check is sent and the box removed even when the
    player holds two of that kind, since nothing goes into the inventory. Recycled boxes keep the vanilla refusal.
  - **Part owner** (part table `+0x0C`): -1 = in a box, 0 = the player, n = actor n (chimps). E.g. a chimp may
    already hold a part when the level starts. This is how the mod can count checks held by chimps.
  - **Chimp steals from the player** (confirmed in play; `R08F5_0180`, a player-actor script command, slot
    `+0x6A`): colliding with a chimp that is not asleep moves a random part from the player's inventory to the
    chimp's; if the chimp already holds two of that kind, one of the chimp's goes into a box in a random room
    (`R0622_0F4E`). Parts the player has laid on the assembly-room floor are immune. A stolen part can come back
    later as a chimp-drop box, so **parts the player obtained are never checks again**: the mod sets the game's
    recycled bit on every part the player holds (`mark_obtained`, each warehouse/assembly pass and right after a
    grant), so any box the game makes for it later gets the recycled sprite and is a vanilla pickup. The part's
    original, unopened box was made earlier with the normal sprite and stays its location. The bit only changes the
    box sprite and skips the +20 score on picking the part up again; inventory moves keep it (`R0834_08D7`).
    Verified headless: a received part dropped as a box → vanilla, no check; a never-obtained part dropped the same
    way → its check.
- **Doors**: both sides of a door share a key (lower room, higher room, pair number `link >> 6`); the rank is the
  key's position among the level's puzzle doors (kind `+0x16 == 8`), sorted. Computed by the mod from the rooms in
  memory, and the same as `tools/warehouse.py`'s static count, so the apworld only needs the counts per level.

## Verified headless (recomp, 2026-10-22)

From a blank `PLAYERS.DAT`, Aircraft (v = 5..9), with `mod/tests/test_driver.c` (opens a box from code, forces
puzzles to pass, saves):
- received items (part 1 of v5, +5 bananas) granted on entering the warehouse; a granted part is still in the
  inventory in the next session and is not granted twice;
- box checks with suppression; empty, recycled and once-held parts' boxes vanilla; door ranks match the static data;
- picker: game-style dialog, greyed levels skipped by the arrows and ignored by clicks, Enter / Esc / mouse,
  Return keeps the Technology Center usable;
- per-level saves over four sessions (L1 new, L2 new, L1 restored, L1 restored): each level keeps its own
  warehouse (PARTS NEEDED 3 vs 6), an opened box stays gone after switching to L2 and back;
- race counters for out-of-order wins and replays: `mod/tests/unit_race.c` (host-side).

Race end and goal: tested with a forced win (the race runs; the test driver sets the slot count and the result).

## Known limitations / to do
- Chimp check carrying needs the recomp's hook sites `chimp_box_open` / `chimp_drop` (recomp PR #1); with an older
  recomp the mod logs it and chimps take check boxes as in vanilla (the stand-in record can then end up in two
  lists, see below). The two-per-chimp limit on stealing needs `chimp_steal` (recomp PR #7); without it chimps
  steal from the player as in vanilla.
