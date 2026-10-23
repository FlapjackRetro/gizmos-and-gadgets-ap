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

### To-do: Cyberchimps carry checks around
Chimps pick up parts all the time (`O0E0D_0000` takes parts from boxes; `R0622_0F4E` drops a part into a box in
a random room), so in a chimp-enabled game a check is not fixed to one box: it can move between rooms or be
held by a chimp. Consequences and plan:
- A hint can only point to a **building and level**; the player may have to search that whole level, chimps
  included.
- Needs a way to show that a chimp is holding a check. Likely a mod in the **assembly area**: an interaction that
  lists how many checks the chimps of that level are holding, and of what type (own-game part vs. other player's
  item).
- Still to decide: whether chimps may pick up AP checks at all (an option could exclude them), and how a chimp
  carrying a check is drawn or reported in the warehouse.

### To-do: limited player inventory vs. received items
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
- Still open: a "recycle" action in the dialog itself (not needed so far: the vanilla bin covers moved parts).
- **Decided 2026-10-23 — chimps and check boxes:** a chimp that opens a check box carries the *check*, not the
  part (the part record is only the box's stand-in and may be the player's real part by then). Hit with a
  banana, it drops carried checks first, then real parts it stole -- a player who left a moved part on the
  conveyor instead of the floor has to hit the chimp more than once to get it back. Done in the mod with two
  recomp hook sites (MOD.md, "Cyberchimps and check boxes").
- Recycling from the Archipelago inventory must use the game's own recycle mechanism (part `+0x36 |= 0x4000`,
  then `R0622_0F4E(part)`): the part lands in a box in a random room with the recycled sprite, and the mod treats
  every recycled box as a vanilla pickup, never a check (docs/MOD.md). Recycled parts can only exist after the
  player obtained the part, so they are never locations.

### To-do: assembly-room sprite for the Archipelago inventory
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
- **Done:** clicking it opens the inventory dialog (above). Optional: the lid animation from `0x2087` (3 frames)
  on click. The chimps sprite is to its right.

### To-do: assembly-room sprite for checks held by chimps
Add a second representative sprite in the assembly room that opens an informational dialog: how many checks the
level's chimps are currently holding, and of what type (this game's parts vs. other players' items). The part
table's owner field (`+0x0C`: n = actor n) tells which parts a chimp holds (`docs/MOD.md`).
- Graphic (chosen 2026-10-23): a Cyberchimp, container `0x103B` frame 1 (31×31, facing front; `0x103B` and
  `0x103F` are the chimps' animation sets, palette `0x100A` = the warehouse's). **Done (`mod/hud.c`):** drawn on the
  assembly room HUD to the right of the Pieces box (x 76, bottom row 198), colours remapped to assembly-room
  indices keeping the hue (bright green face, red body).
- **Done 2026-10-23 (`mod/inventory.c`, `chimp_dialog`):** clicking it opens "Cyberchimps": how many Archipelago
  checks this level's chimps carry (the mod's carried-check list), how many of the player's own parts they have
  (part owner = an actor; stolen or recycled parts, never checks), and "Hit one with a banana to make it drop."
  Not shown: what the carried checks hold (this game's parts vs. other players' items) -- that needs location
  scouting through the client.

### To-do: chimps setting from the YAML
When a new player file is started, the game asks whether to play with or without chimps (dialog 0x1067, "With
Chimps" / "Without Chimps"). Replace that choice with an informational pop-up that states the setting read from the
player's YAML (slot data, passed to the mod through the client's `config.txt`), and apply that setting. Needs: a
`chimps` option in the apworld, and finding where the game stores the choice.
- When the YAML says to play **without chimps**, the chimps sprite on the assembly room HUD is drawn **in grayscale**
  (each index remapped to the assembly-room grey of the same brightness, as the Pieces box's browns are) and is
  **not clickable** (no mouse region, so no informational pop-up).

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

### To-do: re-entering won levels, without re-racing
Levels whose vehicle was built, raced and won must stay enterable (for their remaining checks), but the race must
be locked out afterwards, so a replay cannot cause unwanted game states. What should happen:
1. The player wins the race and is put on the podium. The options are **Next Level** (vanilla: the next level of
   the same building; for AP, if that level is not open, go to the Technology Center instead), **Tech Center**
   (vanilla) and **Exit** (vanilla).
2. The race counts as won, towards the number of races needed to finish the game. That number should become a
   YAML setting (goal conditions are not designed yet: today the goal is all 15 races).
3. The game is saved, keeping the parts obtained so far, the vehicle that was built and won, and the parts laid on
   the assembly room floor (the winning build is locked in).
4. Mod: the assembly room's race door (to the RACE-TRACK) is shown closed and the player can no longer race in
   that level.
5. The player may still take the race vehicle apart and recycle its parts.
6. The warehouse stays open, so the player can go on with that level's checks.
Notes for the implementation:
- Today a won race clears the building's saved-warehouse flag (`[459C+2b]`), so the next entry generates the next
  level; the mod already keeps each level's warehouse in `ap/levels/` and copies it at race start. The copy must be
  taken after the win instead (point 3: the vehicle and floor parts after the race), and restored on re-entry.
- The podium/results screens are main states 11–14 (`O0E36`, RE_NOTES §4.4); "Next Level" has to consult the
  level availability (`avail.<b>`) like the picker.
- The race already won is known to the mod (`won` in `state.txt`); the race door / race start must be blocked for
  those levels (find the door's hotspot in the assembly room and the race-start path `R02E0_0564`).
- Goal option, e.g. `races_required` (1–15), with the client sending the goal when that many distinct races are won.

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
