# Gizmos & Gadgets (DOS v1.0) — Reverse-Engineering Notes for an Archipelago Randomizer

Source install: `C:\DOS\Gizmos\` (DOS v1.0). All offsets are for this `ssg.exe`
(213,200 bytes). Status tags: **[confirmed]** = read directly from code/data, **[hypothesis]** = consistent
with data but not yet verified in a running game.

---

## 1. Files

| File | Size | What it is |
|---|---|---|
| `ssg.exe` | 213,200 | Real-mode DOS MZ, **Borland C++ (1991)**, linked with **VROOMM overlays** (`FBOV` block appended). Internal name `gizmo.exe`. |
| `gizmo000-003.dat` | 260–350 KB | Resource archives, magic `MR0\x1a`..`MR3\x1a`, followed by a u32 offset table. (`Incorrect Resource File N` on mismatch.) |
| `gizmo004.dat` | 45 KB | Written at runtime (mtime 2026) — config/cache; starts `18 01 a3 00` then 0xCE fill. Not yet examined. |
| `PLAYERS.DAT` | 578,487 | Save file, all players. See §3. |
| `ssg.bat` | — | `ssg %1..%8`; errorlevel 5 = "not a 286". Switches: `-si -sb -sm -sn -gv`. |

Leaked source module names (assert strings): `\source\electric.c`, `\source\puzzler.c`, `\source\simple.c`.

## 2. Executable layout [confirmed]

- MZ header: 288 paragraphs (image at file `0x1200`), 976 relocations, entry `0000:0000`, SS:SP `1A78:0080`.
- Resident load image: file `0x1200`–`0x1BA00` (108,544 bytes).
- **DGROUP (DS) = segment `0x0EA7`** (image offset `0xEA70`). `DS:0x0004` = `"Borland C++ - Copyright 1991 Borland Intl."`.
- Overlay area: file `0x1BA00` = `FBOV` header (size `0x186C0`, seg table @ file `0xF030`, 82 entries).
  Overlay code for each stub begins at `0x1BA10 + fileoff`.
- **Segment table** (paragraph `0xDE3`): 8-byte entries `{seg, maxoff, flags, minoff}`. Far-call segment
  values inside overlays are *selectors* into this table (index × 8). flags=1 resident code,
  flags=3 overlay stub (call target is stub:`0x20+5k` → entry k).
- 26 resident code segments, 33 overlays. `tools/disasm.py` produces a symbolized listing
  (`R<seg>_<off>` for resident, `O<stub>_<off>` for overlay functions) in `dis/`.

### Runtime locating (for a memory-reading client)
Scan DOSBox's emulated RAM for `"Borland C++ - Copyright 1991 Borland Intl."`; DS linear base = hit − 4.
Sanity check: `"players.dat"` at DS+`0x1F6E`.

## 3. PLAYERS.DAT [confirmed]

```
0x000  "USR\x1a"
0x004  u16 version  = 0x000C (checked on load)
0x006  u16 players in use
0x008  100 × { char name[16]; u32 score; u16 slot; }    // 22 bytes
0x8A0  99 × player record, 0x16BD (5821) bytes; record k @ 0x8A0 + k*0x16BD
```

Save/load lives in overlay **O0E18** (`fopen("players.dat")`, seek to `slot*0x16BD+0x8A0`, read/write 0x16BD
bytes to/from **DS:0x4562**). **The active player's record is held verbatim in memory at `DS:0x4562`.**

### Player record (offset within record → DS address = 0x4562 + off)

| Rec off | DS addr | Type | Meaning | Status |
|---|---|---|---|---|
| 0x000 | 4562 | char[16] | Player name | confirmed |
| 0x010 | 4572 | u32 | Score (race-results screen adds to it) | confirmed |
| 0x014 | 4576 | u16 | Save slot | confirmed |
| 0x01A | 457C | 20 bytes | Live warehouse header (room, position…) — copied into building block +0x00 on save | confirmed copy; fields TBD |
| 0x02E | 4590 | u16 | Used by race engines + R0622/R053F | TBD |
| **0x030** | **4592** | u16 | **Current building: 0 = Automotive, 1 = Aircraft, 2 = Alternative Energy** (written when clicking a building on the select screen) | confirmed (live) |
| 0x032 | 4594 | u16 | Used heavily by race engine O0E7D | TBD |
| **0x034** | **4596** | s16[3] | **Last race won index per building** (−1 none … 4 = all five) | confirmed |
| **0x03A** | **459C** | u16[3] | **Warehouse-in-progress flag per building** | confirmed |
| 0x040–0x27D | 45A2… | u16 flags | Rows of 0/1 words (8 groups of 33?) — likely **puzzle variants solved** (rec+0x24C set on a door solve) | hypothesis (live evidence) |
| **0x27E** | **47E0** | 3 × 0x6BA | **Per-building warehouse blocks** (see below) | confirmed |
| **0x16AC** | **5C0E** | u16 | **Total races won; 15 ⇒ game complete** | confirmed |
| 0x16AE | 5C10 | … | Tail; used by race code | TBD |

Your saves check out: Flapjack `{1,1,−1}` → 2+2+0 = 4 races; Runs `{−1,4,−1}` → 5 races.

### Building block (0x6BA bytes, at rec+0x27E + b*0x6BA)
Written by the save path in O0E18 only when `[DS:0x94]` (dirty) is set:
- `+0x000` 20 bytes ← copy of rec+0x1A (warehouse header)
- `+0x014` → serialized by `R053F_0385`
- `+0x26C` → byte array of `07`/`09` values, serialized by `R0622_0FB3` (with +0x302). Length grows with level:
  **40 (level 1), 80 (level 3), 120 (level 5)** ⇒ 40 + 20·(level−1). [hypothesis: 07 = locked/unsolved, 09 = opened]
- `+0x302` → list of u16 triples containing x-coordinates 35,75,115,155,195,235,275 and 0–2 → **part boxes**
  (column x, shelf row, room); `1000` sentinel appears = opened/unused. Exact field order TBD.
- `+0x492` → serialized by `R0834_0B5E`

**Implication:** during play the live warehouse lives in other globals and is snapshotted into the block on
save/exit. The block is a snapshot; the rec header fields (§ table) are live.

## 4. Hook points

### 4.1 Race win — **[confirmed]**, resident segment 02E0, function `R02E0_0564`
```
R02E0_0564:  ...
  0x57F  mov ax,[0x4592]            ; building
         building 1 (Aircraft)              -> call O0E7D_0A23   (flight race)
         building 0, 2 (Automotive, Alt-E)  -> call O0E7A_0762   (ground race)
  0x5A0  mov [0x4560],ax            ; 0 = player won
  0x5A3  cmp word [0x455E],0 ; jnz   ; race aborted
  0x5AA  cmp word [0x4560],0 ; jnz   ; lost
  0x5B7  inc word [bx+0x4596]       ; FILE OFFSET 0x45B7  (FF 87 96 45)
  0x5BB  inc word [0x5C0E]          ; FILE OFFSET 0x45BB  (FF 06 0E 5C)
```
Results screen = overlay **O0E36** (uses `1st -`/`2nd -`, adds score, checks `[bldg idx] >= 4` and `total == 15`
for the ending; flag `[0xA0D4]`).

### 4.2 Door/puzzle solve — TBD
Puzzle modules: electric (circuits), puzzler, simple (simple machines) + balance, energy, force, gears, magnets.
Expect a write of `09` into the warehouse state array (live copy). Find with a DOSBox-X memory breakpoint.

### 4.3 Part box opened / part collected — TBD
Box list (+0x302) live copy; inventory flags rec+0x40.. (hypothesis). Same method.

### 4.4 Level generation / entering a building — TBD
Need to confirm: when entering building *b* with `459C[b]==0`, the game builds level `4596[b]+2`'s layout
(fresh); with `459C[b]==1` it restores from block *b*.

## 5. Live session log (DOSBox-X 2026.10.01)

### 2026-10-19 — building index
`DEBUGBOX SSG.EXE` → CS=`0823` (load segment), entry `mov dx,16CA` → DGROUP `16CA` (= 0823+0EA7 ✓).
Memory breakpoints (`BPM`) **work** in the stock DOSBox-X build (they watch one byte).
- `5C0E` 00→04 when Flapjack's save loads (record read into DS:4562).
- `4592` 00→02 on load, then set on each building click: Aircraft→01, Alt-Energy→02, Automotive→00.
- Levels shown match `level = 4596[b] + 2`: Automotive L3, Aircraft L3, Alt-Energy L1 (Flapjack {1,1,−1}).
- Automotive and Aircraft showed the level-intro blueprint; Alt-Energy (`459C[2]=1` in save) did not
  → supports §4.4: in-progress flag = restore saved warehouse, else fresh level + blueprint.

### 2026-10-19 — part box & door dump diffs (Alt-Energy L1, DGROUP 16CA)
Text `MEMDUMP` of DS:0000–FFFF. a = before box 1; b = after box 1; c = from a: solve 1 door puzzle + box 2.
- **Score** `DS:4572` (rec+0x10): +20 per part, +400 per solved puzzle.
- **Box candidates** (same change for both boxes): `DS:0964` and `DS:66AA` 06→05 (boxes remaining?);
  `DS:05F8..0650` = live box list for the room, 14-byte entries holding x-coords (35/75/150/235/275); entries
  shift down on removal; `DS:A028` 00→01 (box 1) / 00→03 (box 2) — picked part type?;
  `DS:7CA6` FFFF→0007, `DS:1AD6` FFFF→0009, `DS:7AFA`/`7BFA` 00→01.
- **Door candidate** (puzzle run only): `DS:47AE` = **rec+0x24C** 00→01 — inside the rec+0x40..0x27D flag rows,
  so those rows are likely *puzzle variants solved/used* (8 rows × 33), not the parts inventory.
- Picking up a part changes nothing in the record except score → part inventory lives in non-record globals
  during play (snapshotted on save).
- Next: `BPM` on 0964 / A028 / 47AE to land in the writers.

## 6. Next step: dynamic analysis checklist (DOSBox-X debugger build)
1. ~~Building→category mapping~~ (done, §5). Confirm race-win path with `BP <load+02E0>:05B7`.
2. In a warehouse, find the live 07/09 array (search memory for the block's 40-byte pattern) → `bpm` it,
   solve a puzzle → door-open function.
3. Open a box → find writer to the live box list / inventory → part-pickup function and part IDs.
4. Exit building → confirm snapshot into block; re-enter → confirm restore path & level generation entry.
5. Map rec+0x40..0x27D by collecting known parts and diffing saves (`tools/players_dat.py`).
