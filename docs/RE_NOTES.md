# Gizmos & Gadgets (DOS v1.0) — Reverse-Engineering Notes for an Archipelago Randomizer

Source install: `C:\DOS\Gizmos\` (Home of the Underdogs repack, 2001). All offsets are for this `ssg.exe`
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

### 1.1 Resource archives (MR) [confirmed — decoded 2026-10-19]
- **Resource id = (archive << 12) | index** (`R04FF_000D`); archives `gizmo000..003.dat` opened at startup
  (`R04FF_0167`, which also checks resources 0x36D/0x11DE/0x214E for magic `A55A`).
- Archive: `"MRn\x1a"` then `u32 offsets[]`. Resource *i*: seek `offsets[i] + 4`, read **type byte**, **compressed
  flag byte**, then payload (raw, or compressed: `u32 unpacked size & 0x3FFFFFFF` + MSB-first bitstream).
- Decompressor `R0B48_02AA(handle, far out)`: Huffman + LZ, 286-symbol literal/length alphabet (DEFLATE-like,
  custom bit order / tables). **We don't reimplement it** — `tools/resources.py` runs the game's own routine in a
  minimal 8086 interpreter (`tools/x86emu.py`), stubbing file read `R0000_23F7`, `farmalloc R0536_0007` and the
  keyboard poll `R098D_00D4`. Output verified byte-exact against live memory.
- Types seen: `S` strings (part names, 0x278…), `D` part tables (0x2F5–0x303), `G` graphics?, `LPX` images.
- `R04FF_00FE(id)` = load resource into a new far block.

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
| 0x020 | 4582 | u16 | **PARTS NEEDED** (HUD counter for current level) | confirmed (code) |
| 0x06E | 45D0 | u16[8][33] | **Puzzle variant solved table** (type*0x42 + variant*2) | confirmed (code) |
| 0x01A | 457C | 20 bytes | Live warehouse header (room, position…) — copied into building block +0x00 on save | confirmed copy; fields TBD |
| 0x02E | 4590 | u16 | Used by race engines + R0622/R053F | TBD |
| **0x030** | **4592** | u16 | **Current building: 0 = Automotive, 1 = Aircraft, 2 = Alternative Energy** (written when clicking a building on the select screen) | confirmed (live) |
| 0x032 | 4594 | u16 | Used heavily by race engine O0E7D | TBD |
| **0x034** | **4596** | s16[3] | **Last race won index per building** (−1 none … 4 = all five) | confirmed |
| **0x03A** | **459C** | u16[3] | **Warehouse-in-progress flag per building** | confirmed |
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

### 3.3 Part tables [confirmed live + decoded from resources, 2026-10-19]
- **Loader `R053F_0002`**: loads resource `0x2F5 + [DS:4590]` where `DS:4590` = rec+0x2E = **vehicle index
  v = building×5 + (level−1)**. The resource *is* the table; the loader only fills pointers (header +0x25/+0x29,
  entry +0x82 icon from DS:6644 table, +0x86 name = resource `[entry+0x16]`, +0x8C = index).
- **All 15 tables extracted statically**: `data/parts_all.tsv` (`tools/extract_parts.py`), 532 parts.
  Counts — Automotive 20/36/40/40/40, Aircraft 20/32/40/40/44, Alt-Energy 24/36/36/40/44 (L1..L5).
  Matches the three live dumps exactly.
- `DS:6638` far ptr → level parts header (`+2` = part count); `DS:663C` far ptr → part table.
  Loaded **per level** (Alt-Energy L1: 24 parts at `5F96:0039`, header `5F96:0004`).
- Entry = 0x8E bytes: `+0x08` group (= vehicle slot), `+0x0A` **quality tier** within the group (0 = basic; parts sharing a tier are interchangeable copies; single-tier groups such as brakes/fuel tank/decals are required-or-cosmetic), `+0x1E`/`+0x26` most likely the **x/y draw position** of the part on the vehicle picture (paint = 0/0; cosmetic decals have values) — *not* performance stats; real performance fields TBD,
  `+0x2E` unknown (not the slot — group `+0x08` identifies the slot), `+0x36` flags (0x4000/0x8000 tested by pickup/race code), `+0x82` far ptr → blueprint
  icon (`LPX\x1a` image), `+0x86` far ptr → name, `+0x8C` part id (0-based, level-local).
- Alt-Energy L1 = 6 slots × 4: paint(0) wheels(1) brakes(2, all four identical) gears(3) body(4) decal(5).
  **Cosmetic groups: `0x0B` paint and `0x0C` decals have no performance value** → AP filler, excluded from
  any vehicle-performance calculation (user-confirmed game knowledge).
  Decoded table: `data/parts_alt_energy_L1.tsv` (`tools/parts_table.py`).
- **All part names** are uncompressed `S <id> <text> NUL` records in `gizmo000.dat` (245 total,
  `data/part_names.tsv` via `tools/part_names.py`). File-order clusters:
  - 49955–51197 Automotive (tires, bushings, bodies, differentials, 45–180 hp engines, windshields)
  - 58665–59036 decals (shared); paints at 49955
  - 125909 Aircraft blimp · 132765 glider · 141595 propeller plane · 148911 jet · 156971 helicopter
  - 244963 Alt-Energy pedal racer (L1) · 258007 solar / batteries / electric / fuel cell · 274631 electric
  Decoded: `data/parts_automotive_L3.tsv`, `data/parts_aircraft_L3.tsv` (dumped from `5A5F`, table `5C5F:0041`).
  Automotive L3 and Aircraft L3: 40 parts each (10 groups × 4); header at tableseg:0004 = {?, count, groups, needed?}.
  Aircraft groups 0x16–0x1D + paint 0x0B + decal 0x0C. Names/icons are allocated in other segments (may be
  below the table) — dump from `tableseg-0x200`.
  Level-by-level slot/stat tables still need capturing (one MEMDUMP per level) or decoding the level resource.

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

### 4.2 Door/puzzle solve — **[confirmed]** (traced 2026-10-19)
**Puzzle dispatcher = overlay O0E60** (entry `O0E60_0000`):
- `[B13A]` puzzle type 0–7 → jump table: 0 `O0E41_08A1` (electric.c), 1 `O0E6A_0000`, 2 `O0E6D_0000`,
  3 `O0E70_1ED0`, 4 `O0E46_0AA0`, 5 `O0E5D_1138`, 6 `O0E4E_0000`, 7 `O0E51_1C03`.
- `[B138]` variant index, chosen randomly preferring unsolved ones; `[3334+type*2]` = variants per type.
- `[B13C]` result. If `rec+0x6E + type*0x42 + variant*2` is 0 it is set to the result
  → **rec+0x6E..0x27D = 8×33 "puzzle variant solved" table** (DS:45D0).
- Each puzzle overlay awards +400 itself via `O0E60_03BF` (4 × `R0A04_0D36(100)`).
- Then **`R02E0_0A24(result)`** (resident) opens the door:
  ```
  R02E0_0A24: if !result return
     door = R0622_11F7(room [7AA4], door [7AC0])   ; [7AA4] = current room index
     if door.state(+0x10) == 7 (locked):
  0xA71  door.state = 9 (open); door.sprite(+0x0E) = 0x30E/0x33C
         other = door.(+0x12) & 0x0F  -> destination room; matching door there opened too (R0622_087F)
  ```
  Door objects live in the far room data (segment seen as 6257); the 07/09 arrays in the building blocks
  (+0x26C) are their serialized states (7 locked, 9 open).

**Live-verified 2026-10-19** with code BPs (load 0823): `BP 0B03:0A24` hit after a correct puzzle;
`BP 0B03:0A71` hit next with door object at `6257:0906`, state 7→9, link `+0x12 = 0x0487` (dest room 7);
`[B144]=1` → player moved into dest room (`[7AA4] = 7`).

### 4.3 Part box opened / part collected — **[confirmed]** (traced 2026-10-19)
**`R08F5_0002`** (resident) = player opens a box:
```
obj = R0622_1265(room [7AA4], object [7ACA]);   ; cdecl: last push = first arg  if obj.taken(+8) return
contents = obj.(+0x0C+0x12)        ; 1000 = empty, else part id (0x8E-byte entries in far table [663C])
empty : R02E0_0AEA(5); R0622_0D8F(remove); dec [7A00]
part  : needed = R053F_0189(part); isnew = R0834_08D7(actor 0, part)   ; ADD TO INVENTORY
        R0A04_076B(part) (show name); msg 0x104A/0x1044; R0622_0D8F(remove)
        if isnew==0 && needed: dec [4582]  ; rec+0x20 = PARTS NEEDED (HUD)
        if [4582]==0: msg 0x1050 ; part flag 0x4000 clear -> R02E0_0ACE(20) score
```
- **Live-verified 2026-10-19**: `BP 1118:0002` hit on box pickup (room `[7AA4]=6`, object `[7ACA]=3`).
- `R0622_0D8F(room, obj)` removes an object: obj.taken=1, room.count(+0x9E)--, and `[0964]`-- if it is the
  current room (`[08C2]`). Rooms: far ptr table at DS:7A04 (+room*4), room data in far memory (seg 6257 this session). Current room copied to DS:08C6; `[08C2]` = current room for redraw purposes, `[7AA4]` = player's room.
- `R0622_0B0D..` adds an object to a room; box contents −1 = random part (`R053F_0203`), 1000 = empty.
- **Item grant primitive:** `R0834_08D7(actor, part)` — actors at DS:7A8A + actor*0xA0 (0 = player);
  actor+0x70 = item count, actor+0x72 = far list. Chimps use the same (O0E0D_0000 steals from boxes).
- `R02E0_0ACE(points, flag)` adds score.

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
