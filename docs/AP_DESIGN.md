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
| Part boxes opened | ~18–31 per level (≈ 300–400 total) | TBD (live box list) |
| Doors / puzzles solved | state array is 40→120 cells per level — too many as-is; likely per-puzzle-door subset, or "N doors opened" milestones | TBD |

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
2. **Client-managed level selection** (per building, only while the player is in the Technology Center hub):
   - The client keeps a snapshot of each building block for every (building, level) the player has
     visited — 15 slots, 0x6BA bytes each.
   - To switch building *B* to level *L*: save the current block *B* to its slot, write
     `DS:4596[B] = L−2`, then either restore the stored block for (B,L) and set `459C[B]=1`, or clear
     `459C[B]=0` so the game generates level L fresh. **(Needs verifying in §4.4 of the RE notes.)**
   - UI: client command `/level automotive 3`, or later an in-game patch (e.g. hijack a menu item).
3. Alternative if (2) proves fragile: patch the building-entry code to show a level picker. More work,
   since that code is likely in an overlay.

## Client architecture
- Python AP client (CommonClient-based) + a DOSBox memory interface:
  - Windows: attach to `dosbox-x.exe` (dev target; other flavors later) with pymem, find emulated RAM, scan for the Borland
    signature → DS base. Read/write words at DS+offset.
  - Poll at ~5–10 Hz: building, level indices, races-won, box/door state, inventory.
- Patcher: apply byte patches to a copy of `ssg.exe` (a `.apgg` patch or simple byte list).
- Save handling: one AP seed ↔ one player slot in PLAYERS.DAT; client verifies the player name matches the slot.

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
