# Gizmos & Gadgets — Archipelago Design Draft

Companion to `RE_NOTES.md`. Draft for discussion; anything that depends on unconfirmed RE is marked.

## Game structure (vanilla)
- 3 engineering buildings (index 0 Automotive, 1 Aircraft, 2 Alternative Energy — confirmed live), each with
  **5 levels**. A building only ever has **one active level**: level = last-race-won-index + 2 (capped at 5; index 4 = building finished).
- Each level: a warehouse with puzzle-locked doors and part boxes. Collect parts → build vehicle → race Morty.
- Winning race *n* advances that building to level *n+1*. 15 races total; 15/15 = ending.

## Locations
| Group | Count | Detection |
|---|---|---|
| Race wins (bldg × level) | 15 | Hook at `R02E0_05B7`, or client watches `DS:4596[b]` / `DS:5C0E` |
| Part boxes opened | **622** fixed per level (22–54); 532 hold a part (each part exactly once), 90 empty — `data/warehouse_counts.tsv` | placement/contents random per new level |
| Doors / puzzles solved | **653** puzzle doors (19 / 39–40 / 59–61 per level), fixed by the room layouts — likely too many as individual checks; options: per-door, every Nth, or "N doors opened" milestones | layout fixed; puzzle variant random |

Recommendation for v0.1: **race wins + part boxes only**; add doors later as an option once the live
array is understood.

## Items
- **Vehicle parts** (each distinct part type/quality is an item). The box you open sends a check; the part
  you receive comes from the multiworld. Needs: part-ID table + a way to grant parts (write inventory flag).
- **Level access** — `Progressive <Building> Level` ×4 per building (12 total) — the core progression item.
- **Cosmetic parts are filler:** paint (group `0x0B`) and decals (group `0x0C`) have no performance value, so
  they never gate a race and don't count toward performance logic. (3 buildings × 5 levels × 8 = 120 filler items.)
- Other filler: score / bananas (if banana count is a field), traps: "Cyberchimp" (steal a part), etc.

## Progression / logic
**Race logic is now data-driven** (`data/race_requirements.tsv`, from an exact simulation of the race code):
- Ground races are deterministic: player vs Morty's fixed build. A race is winnable iff the installed parts give a
  finish no later than Morty's (tie → higher speed).
- Only performance slots matter (not brakes, fuel tanks, paint, decals). Later levels need nearly every
  performance slot at the best tier → **progressive quality items per slot** fit well, with logic
  "all performance slots ≥ tier 2" (L1–2 of every building) or "≥ tier 3" (L3–5). Aircraft:
  `data/flight_requirements.tsv` (same pattern; some aircraft slots are non-monotonic, so "progressive" must
  follow the *performance* order, not the tier number).

- Race *L* in building *B* requires: access to level L of B **and** the parts needed to build a vehicle that
  can win that race (logic per race: e.g. "a body, an engine, wheels of at least tier X"). Tier data must
  come from the game's part tables (resource files) — RE TBD.
- Goal options: win all 15 races (vanilla ending, `5C0E == 15`), or win the final race of each building.

## Breaking the "one level per building" rule
The game's own state is small and lives in the player record, so this can mostly be done **without
rewriting game code**:

1. **Decouple race wins from advancement**: NOP `inc word [bx+0x4596]` at file `0x45B7` (4 bytes →
   `90 90 90 90`). Winning just sends a check; `DS:5C0E` still counts wins (or NOP `0x45BB` too and let
   the client own the counter for the goal).
2. **Level selection** (see *Level select & "open world"* below; originally planned as a client command):
   - The client keeps a snapshot of each building block for every (building, level) the player has
     visited — 15 slots, 0x6BA bytes each.
   - To switch building *B* to level *L*: save the current block *B* to its slot, write
     `DS:4596[B] = L−2`, then either restore the stored block for (B,L) and set `459C[B]=1`, or clear
     `459C[B]=0` so the game generates level L fresh. **(Needs verifying in §4.4 of the RE notes.)**
   - UI: client command `/level automotive 3`, or later an in-game patch (e.g. hijack a menu item).
3. Alternative if (2) proves fragile: patch the building-entry code to show a level picker. More work,
   since that code is likely in an overlay.

## Level select & "open world" (implemented, docs/MOD.md)
- After clicking a building at the Technology Center, a dialog in the game's style offers `Level 1 - <vehicle>` …
  `Level 5 - <vehicle>` and `Return to Technology Center`; arrow keys and the mouse select, locked levels are
  greyed out. YAML `level_access`: `open` (all levels) or `race_progression` (next level after the previous race).
- A level is generated once and saved at once; afterwards it is always restored (one saved warehouse per level,
  kept by the mod in `ap/levels/`; PLAYERS.DAT itself only holds one per building).
- The client checks the game is running with the mod (heartbeat) and that the in-game player matches the seed.

## Client architecture
**Target: the native recompilation** (`gizmos-and-gadgets-recomp`), not DOSBox. The game side is a recomp mod
(`mod/ap_mod.c`, see `docs/MOD.md`) that hooks the game's events directly: building click, level enter/ready,
box open, puzzle result, door open, race end, plus safe points to grant items with the game's own routines.
- Mod ↔ client: files in `<game dir>/ap/` (`checks.txt`, `received.txt`, `config.txt`); a localhost socket can
  replace them later without changing the ids.
- Python AP client (CommonClient-based): forwards checks, appends received items, writes level unlocks/selection.
- Save handling: one AP seed ↔ one player slot in PLAYERS.DAT; client verifies the player name matches the slot.
- **Legacy:** the DOSBox-X route (`tools/patcher.py`, `docs/DOSBOX_X_DEV.md`, memory polling) is kept for
  reference; the RE notes and hook addresses it produced are what the recomp's mod API is built on.

## Future items
### In-game Archipelago item labels
Swap part names shown in-game for Archipelago item labels (e.g. "Bob's Progressive Wings"), so a box shows what
it actually sends/holds. What we know:
- Part names are type-`S` string resources (ids from `0x278`), referenced by part-table entry `+0x16`.
- At load time `R053F_0002` loads each name into its own far block and stores the pointer at entry `+0x86`;
  the pickup message reads it from there (`R0A04_076B`).
- Options: (a) the client writes replacement text into those blocks at runtime (simplest; length-limited to the
  original allocation unless we reallocate), (b) patch the `S` resources in a copy of `gizmo000.dat`
  (needs an archive rewriter), (c) point `+0x86` at a client-managed buffer.
- TBD: find other places text is drawn (blueprint screen labels, part inventory screen) and their length limits.

### Box pickups as Archipelago checks
Opening a box that holds an Archipelago check must **show the item's text** (the AP item name, from the label
work above) but **must not add anything to the inventory** unless the item belongs to this player's own game.
Own-game items arrive through the normal AP receive path and are granted then.
Where this hooks in (`R08F5_0002`, see RE_NOTES §4.3):
- Keep: box removal (`R0622_0D8F`), score (+20, `R02E0_0ACE`) if wanted, name display (`R0A04_076B(part)`).
- Skip for AP checks: `R0834_08D7(0, part)` (inventory add) and the PARTS NEEDED decrement (`[DS:4582]`), which
  depends on its return value. The "you found a part you need / don't need" message (`0x104A`/`0x1044`) should be
  replaced by an AP message (e.g. "Found X for Player").
- Granting a received item (own game) = the same `R0834_08D7(0, part)` call, plus the PARTS NEEDED bookkeeping.
  This runs inside the game, so it needs a small injected routine the client can trigger (mailbox in memory),
  not just memory pokes.
- **Chimps:** `O0E0D_0000` lets Cyberchimps take parts from boxes and drop them later; that path must not hand
  AP-check placeholders to the player as real parts either.

**Decided — other players' items:** a box holding another game's item simply disappears from G&G when it is
opened (the check is sent, the item text is shown, nothing enters the inventory).

### Cyberchimps carry checks around (done)
Chimps open boxes, so in a chimp-enabled game a check is not fixed to one box: a chimp may carry it until hit with
a banana. A hint can only point to a building and level. Implemented (MOD.md, "Cyberchimps and check boxes"): a
chimp carries the check, not the part; it drops carried checks first; the assembly room's Cyberchimp sprite tells
how many checks the level's chimps carry; the YAML option `cyberchimps` can turn chimps off.

### Limited player inventory vs. received items (done: the Archipelago inventory)
The vanilla inventory holds only **two items per part type**, so items received from the multiworld can arrive
when that slot is full. The player must never lose access to a received check. Options:
1. **Accept-or-recycle prompt (EXE-side):**
   1. The client sends the part to the EXE (mailbox, see "Box pickups as Archipelago checks").
   2. The EXE checks for free space in the inventory for that part type; if there is room, it accepts the part.
   3. If that type is full, the EXE raises an interrupt that pauses the game and asks the player to either accept
      the new part and recycle one they are holding, or recycle the part just received.
2. **Inventory managed outside the vanilla system:** a dialog that holds received parts and can send them to the
   conveyor (assembly) or recycle them to the warehouse.
- **Decided — chimp theft:** chimps steal only from the vanilla player inventory (on collision, unless asleep
  from a banana), as in vanilla; parts laid on the assembly-room floor stay immune. Parts in the static Archipelago
  inventory can never be taken by chimps. So that inventory must live outside the game's actor inventory lists
  (which chimps steal from) until the player moves a part into the vanilla inventory.
- **Done 2026-10-23 (option 2, `mod/inventory.c`):** received parts stay in the Archipelago inventory (no more
  automatic grants in the warehouse; bananas still arrive by themselves). Clicking the Pieces box in the
  assembly room opens a dialog in the game's style listing every received part not in the game yet: this
  level's parts can be picked, other levels' are greyed with their level in front. Six rows, scrolled with
  Up/Down/PgUp/PgDn/Home/End, the Up and Down buttons, or the mouse. Picking a part checks the game's own limit
  (two of a kind): when full, a message says to recycle a part first and nothing else happens; otherwise the part
  goes into the vanilla inventory (counts for PARTS NEEDED) and leaves the list. The dialog stays open for more
  picks until Close / Esc. The conveyor is updated in place when the dialog closes (no room reload; see
  RE_NOTES §4.14) and the level is saved. A moved part is the player's like any other: it can be laid on the
  floor or recycled, and chimps can steal it, but it never goes back to the Archipelago inventory.
- **Decided:** no recycle action in the dialog: every part goes Archipelago inventory -> conveyor -> the vanilla
  recycle bin.
- **Decided 2026-10-23 — chimps and check boxes:** a chimp that opens a check box carries the *check*, not the
  part (the part record is only the box's stand-in and may be the player's real part by then). Hit with a
  banana, it drops carried checks first, then real parts it stole -- a player who left a moved part on the
  conveyor instead of the floor has to hit the chimp more than once to get it back. Done in the mod with two
  recomp hook sites (MOD.md, "Cyberchimps and check boxes").
- Recycling (the vanilla bin) marks the part `+0x36 |= 0x4000` and puts it in a box in a random room with the
  recycled sprite; the mod treats every recycled box as a vanilla pickup, never a check (docs/MOD.md). Recycled
  parts can only exist after the player obtained the part, so they are never locations.

### Assembly-room sprite for the Archipelago inventory (done)
Add a representative sprite (an object the player can click) in the assembly room that opens the static
Archipelago inventory dialog for management: move a stored part into the vanilla inventory, or recycle it (see
"limited player inventory"). The static inventory itself stays out of the chimps' reach.
- Graphic: reuse the **"Pieces" box with its counter** from the door puzzle "Put the puzzle pieces together to
  make a machine"; the counter shows how many parts the Archipelago inventory holds. Located 2026-10-23: it is
  part of that puzzle's background, resource `0x2084` (320×155, palette `0x2085` over `0x1094`), box (lid
  included) 66×36 at (15, 94); surround index 198 (green), shadow index 211. Counter centre (25, 26.5) in the
  sprite, measured from an in-game screenshot. `tools/pieces_box.py GAMEDIR OUTDIR --preview N` cuts it out of the
  player's own files (PNG + JSON with the palette indices); like other game art it is not committed.
- **Done 2026-10-23 (`mod/hud.c`):** drawn in the unused bottom-left of the assembly room HUD (x 0–217, y 167–199,
  33 rows): rows 12–14 of the box (plain texture) dropped to fit, colours remapped to assembly-room indices that
  have the same colour in all three buildings (greys exact, browns to the grey of equal brightness), the counter
  drawn with the puzzle's own digit sprites (`0x204F`), at (4, 167). Shows the received parts of the current
  level that are not in the game yet. Also drawn on the blueprint screen (same loop, same free corner).
- **Done:** clicking it opens the inventory dialog (above). The chimps sprite is to its right.

### Assembly-room sprite for checks held by chimps (done)
Add a second representative sprite in the assembly room that opens an informational dialog: how many checks the
level's chimps are currently holding. The part
table's owner field (`+0x0C`: n = actor n) tells which parts a chimp holds (`docs/MOD.md`).
- Graphic (chosen 2026-10-23): a Cyberchimp, container `0x103B` frame 1 (31×31, facing front; `0x103B` and
  `0x103F` are the chimps' animation sets, palette `0x100A` = the warehouse's). **Done (`mod/hud.c`):** drawn on the
  assembly room HUD to the right of the Pieces box (x 76, bottom row 198), colours remapped to assembly-room
  indices keeping the hue (bright green face, red body).
- **Done 2026-10-23 (`mod/inventory.c`, `chimp_dialog`):** clicking it opens "Cyberchimps": how many Archipelago
  checks this level's chimps carry (the mod's carried-check list), how many of the player's own parts they have
  (part owner = an actor; stolen or recycled parts, never checks), and "Hit one with a banana to make it drop."
  **Decided:** it does not say what the carried checks hold.

### Chimps setting from the YAML (done 2026-10-23)
When a new player file is started, the game asks whether to play with or without chimps (dialog 0x1067, "With
Chimps" / "Without Chimps", `O0E83_0C5E`, called from `O0E2D` 0x1307; the answer is stored at `DS:4578`, player
record +0x16, 1 = with). Now: apworld option `cyberchimps` (default on) -> slot data -> the client's `config.txt`
(`chimps=1/0`) -> the mod skips the question (site `chimps_prompt`, recomp PR #2) and shows a message with the
setting instead; it also sets `DS:4578` on every level entry. Without chimps, the Cyberchimp on the assembly room
HUD is drawn in grayscale (each index to the assembly-room grey of the same brightness) and is not clickable.
Verified headless for both settings (message, `[4578]`, chimps in the warehouse or none, grey sprite).

### To-do: Microsoft Defender SmartScreen warning
Windows shows "Windows protected your PC … Unknown publisher" when starting `gizmos.exe` (the recomp build).
See what can be done. Options to evaluate (not yet checked):
- SmartScreen judges files that carry the "downloaded from the internet" mark (Mark of the Web) and are unsigned
  or have no reputation yet. Find out why a locally built `gizmos.exe` gets it (e.g. the install script copying
  a downloaded file, or the folder it was unpacked from), and whether the install script can avoid it.
- Code signing for released binaries: a certificate from a code-signing service (paid, or a free programme for
  open-source projects), set up in the release build. Reputation still builds up over time per certificate.
- Until then: document "More info → Run anyway" in the setup guide.
This concerns the recomp's `gizmos.exe` (and `ap_mod.dll` once distributed), so the work may belong in the recomp
repository.

### Re-entering won levels, without re-racing (done 2026-10-23)
Won levels stay enterable for their remaining checks, but their race is locked out:
1. On the podium after a win: **Rerun Race** is refused (message); **Next Level** goes to this building's next
   level when the seed has opened it, else to the Technology Center (message); **Tech Center** and **Exit** as in
   the game (recomp site `podium_choice`, PR #3).
2. The win counts towards the goal: YAML option `races_required` (1-15, default 15) different races.
3. The won level keeps the warehouse copied at race start (vehicle built, floor parts) and is restored on re-entry.
4. In a won level the race door never opens, even when the vehicle is completed again: the slot counter `[4584]`
   is kept 0x100 above the real count (no door, no race, no +100 bonus, no "finished building" hint), and the
   door region gets its own code: clicking it says the race was won already.
5. The vehicle can still be taken apart and its parts recycled; the warehouse stays open.
Verified headless with a forced win (test driver `TEST_COMPLETE` + `TEST_WIN`): race run and won, check and goal
(`races_required=1`) sent, Rerun refused, Next Level open → loads level 2, not open → Technology Center, the won
level restored with its door shut and the door click answered by the message.

### Archipelago-themed decal
Replace one decal with an Archipelago-themed one.
- Decals are group `0x0C` (cosmetic). Each entry has sprite resource ids at `+0x12`/`+0x14` (e.g. `0x00B6`/
  `0x00A1`) and a blueprint icon (`LPX\x1a` image, ptr at `+0x82`).
- TBD: decode the `LPX` image format and the vehicle-sprite resources, then add/patch one decal's graphics
  (and its name resource). Needs an archive rewriter that can store resources uncompressed (flag byte 0) so we
  don't need a compressor.

## Open questions for you
1. ~~Which DOSBox?~~ **Decided:** DOSBox-X is the development target (see `docs/DOSBOX_X_DEV.md`); other
   flavors get compatibility testing later in development.
2. Doors as locations: per door, per puzzle type, or skip for v0.1?
3. Should parts stay per-level (vanilla) or become a global pool usable in any building/level?
