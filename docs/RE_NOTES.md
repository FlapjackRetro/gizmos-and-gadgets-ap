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
| 0x032 | 4594 | u16 | **Current level (0-based)** in the current building | confirmed (code) |
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
**Live-verified 2026-10-19** (Alt-Energy L1 race, won): `BP 0B03:0564` hit on entering the racetrack
(`[4592]=2`, SI=0x0A = vehicle index); `BP 0B03:05A3` at the end of the race with `[455E]=0`, `[4560]=0`;
`BP 0B03:05B7`: `[459A]` FFFF→0000 and `[5C0E]` 4→5; function returns 0x0C (→ state 12, results).

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

### 4.4 Main state machine & building select [static, 2026-10-19]
Far-handler table at **DS:00C6** (4 bytes each); each handler returns the next state index.
| state | handler | meaning |
|---|---|---|
| 0 | `O0E28` (stub off 0x43) | enter building (load / assembly station) |
| 1 | `O0E2D` | title / sign-in? |
| 2 | **`R02E0_0148`** | **Technology Center building select** |
| 3,4,6,7,9 | `R02E0_0820/096A/0619/09AA/0506` | TBD |
| 5 | `O0E30` | warehouse |
| 8 | `O0E60` | door puzzle (returns 5, or 16) |
| 10 | `R02E0_0564` | race (returns 12, or 14 if aborted) |
| 11–14 | `O0E36` | race results |
| 15,16 | `R02E0_012F/0141` | exit/quit? |

**Building select `R02E0_0148`**: event loop; `R098D_0106` returns a UI event code, looked up in a 6-word table
at `02E0:04EE` with handlers at `02E0:04FA`:
- `0x001B` (Esc) / `0x0BBC` → `0421`: menu (`O0E97_00B2`)
- `0x0BB8` → `0428`: leave with result 2
- `0x0BB9` Automotive → `0456`: `if [4596]==4 → msg 0x106E (building finished, entry refused)` else `[4592]=0`
- `0x0BBA` Aircraft → `0434`: same with `[4598]` → `[4592]=1`
- `0x0BBB` Alt-Energy → `0445`: same with `[459A]` → `[4592]=2`
- all three then **`0463`: `[bp-6]=1`** → loop exits, returns `[bp-2]` (0) → state 0 (enter building).
**Live-verified 2026-10-19**: `BP 0B03:0463` hit on clicking Aircraft with AX=`0BBA` (event code) and
`[4592]=1`; `[4596..459A]={1,1,-1}`, `[459C..45A0]={1,1,1}`.
**Level-picker hook point: `02E0:0463`** (file offset `0x1200+0x2E00+0x463 = 0x4463`). The "finished building"
refusal (`cmp [4596+2b],4 / jz 046A`) must be removed for AP.

### 4.5 Dialogs & UI text [static, 2026-10-19]
- **Hint boxes**: type-`H` resources (`FF 06 <dev comment> FF <text>`), shown by `O0E3D_0037(id,0,0,0)`
  (e.g. 0x104A "collected a part you need", 0x1044 decoration, 0x1047 already have 2 of that type, 0x1050 all parts).
- **Dialog resources**: type-`01` containers = `u16` offset table + tagged elements: `S 00 <text> NUL`,
  `I 00 <u16>` (button style?), `M`, `T`, `G`, `C`, `L`, `H`. Examples: 0x1064 Save & Exit, 0x1065 Sound,
  0x1066 Hints, 0x1067 Chimps (2 buttons), 0x106B Return to Technology Center, 0x106E building finished,
  0x1070 rank-up congratulations. **Blueprint layouts** 0x002C–0x0030 (Automotive L1–5), 0x00FB–0x00FF
  (Aircraft), 0x0269–0x026D (Alt-Energy) hold the vehicle title and **slot labels** (Body, Tires, …).
- **Dialog runner `O0E83_08EE(struct by value, 0x32 bytes)`** → core `O0E83_0655(1,1)`; returns button index / −1.
  Struct (as built for the chimps dialog at `O0E83_0C5E`): `+0x04` far ptr body text, `+0x0C` **button count**,
  `+0x0E` far ptr → array of far label ptrs (DS:3E2C → "With Chimps"/"Without Chimps"), `+0x12` far ptr →
  hotkey array (DS:3E16: y n c d o, PgDn/PgUp scan codes 0x151/0x149), `+0x1A..+0x20` −1 (auto layout?).
  Default box 260×120 at (19,14) (`O0E83_0907`). Button label pool at DS:3E39.. (to Go Back, Yes, Cancel, Done,
  See Next Page, Go Back a Page, With/Without Chimps).
- `O0E83_0907` = paged text viewer (See Next Page / Go Back a Page / Done) — a ready-made pager pattern.
- No existing dialog uses > 2 buttons; max button count/layout of `O0E83_0655` not yet analysed.

### 4.6 Entering a building / level setup — **[confirmed]** (traced 2026-10-19)
**`R02E0_0820` = state 3 (enter building)**, reached after building select. Live: `BPM 16CA:4590` fired 0A→0B at
`0B03:0855` when entering Alt-Energy after winning race 1; `[45A0]` was already 0 (cleared by the win), blueprint
auto-opened (fresh level).
```
0x83D  level   = [4596 + 2b] + 1 -> [4594]   (rec+0x32 = current level, 0-based)
0x84B  vehicle = b*5 + level     -> [4590]   (rec+0x2E)
0x858  R053F_0002()  load part table;  R0622_0085();  R0A04_02C2(7)
0x86A  if [459C + 2b] != 0:   RESTORE from block b (rec+0x27E + b*0x6BA):
          R08F5_05A3(); O0E0D_0635(); memcpy(rec+0x1A <- block+0, 20)
          R053F_03C9(block+0x14)   part states
          R0622_10E7(block+0x26C doors, block+0x302 boxes)   rooms
          R0834_0B95(block+0x492)  inventory;   [94] = 1
       else FRESH (0x8EB):
          R08F5_094D()  generate warehouse;  [4582] = R053F_0316()  (PARTS NEEDED)
          if level==0: [458A]=0, [457E]=10
          [458E] = rand(100) < 50;  O0E0D_0635();  [457C]=0, [4586]=0, [4588]=-1
          [4580] = DS:00B2[level];  [4584]=0;  O0E0D_0784()
0x965  return 5
```
- **The level loaded is derived only from `[4596+2b]`** → the picker can set it (or patch 0x83D–0x847 to read
  a picker variable instead of `race idx + 1`).
- Saved state per building = block b + flag `459C[b]`; switching to another level of the same building = swap
  block b and its flag before state 3 runs (flag 0 → fresh generation).
- The race win clears `459C[b]` (O0E36 results code), so the next entry generates the next level fresh.
Need to confirm: when entering building *b* with `459C[b]==0`, the game builds level `4596[b]+2`'s layout
(fresh); with `459C[b]==1` it restores from block *b*.

### 4.7 Race performance (ground races) [**validated live** on Alt-Energy L2, 2026-10-19]
- **`O0E7A_044E(racer*, partlist*)`** computes a racer's stats from installed parts (list of part ids, −1 end).
  Called twice per race: player (list DS:6616 → racer DS:BAE8) and **Morty (list at `[6634]` = right after the
  part-table entries in the level's resource → racer DS:BAAE)**. Morty's build is fixed per level
  (`data/morty_builds.tsv`).
- Uses only each part's **group (+0x08) and tier (+0x0A)** → 16.16 fixed-point tables in DGROUP
  (DS:3A54…3B30, 4 tiers each) and per-vehicle base `DS:3B34[v]` (Auto 6–10, Alt-E 5–9). Group switch: 19-entry
  table at `O0E7A:0716` (+0x26 handlers); vehicle switch at `O0E7A:06F8`. `O0E80_0000` = fixed-point multiply.
- Effects (tier 0→3): body 0x00 A−=1.8/1.2/0.6/0; bearings 0x01/0x35 A−=0.6/0.4/0.2/0; 0x03 B=4/5/6/7;
  tires 0x04/0x34 C+=.010–.025; differential 0x05 E=4/3/2/1; windshield 0x08 A−=.05–.02; engine 0x09 A−=1.2–0 &
  D=0.6–1.2; 0x30 C+=.020–.035; pedal body 0x32 A−=1.2–0; gears 0x33 C+=.020–.035 & A−=1.0–0; 0x37 C+=.02–.08 & A−=body;
  0x06/0x39 and 0x07/0x38/0x3A record tiers used per-vehicle (electric: D = DS:3AA4[min]). **Brakes, fuel tanks,
  paint, decals: no effect.**
- Per vehicle: v0 C=0.3; v1–4 C += D ÷ int(E) (power ÷ gear ratio; v2/v4 also B=A); v11–14 C += D ÷ int(E),
  D = DS:3AA4[min(motor tier, battery tier)]. **`O0E80_0000` = signed 16.16 ÷ integer-part divide, rounded** (not a multiply).
  int(E) means the 3½:1 and 3:1 differentials perform identically.
- Racer struct: `+0x22` = **top speed = min(A,B)**, `+0x32` = accel (C), `+0x26`/`+0x36` = second-mode cap/accel.
  Per tick (`O0E7A_0034`): speed += accel up to cap; if above cap, −0.2.
- **Live validation** (`BP 0B03:05A3`, `D 16CA:BAAE` Morty / `D 16CA:BAE8` player): Morty top 5.2 / accel 0.140,
  player (80-panel body, 12" tires, ball bearings, 50 kW battery, 60 kW motor, 3½:1 diff) top 5.8 / accel 0.145 —
  model output identical. +0x26/+0x36 equal +0x22/+0x32 for both racers on this level.
- Model in `tools/race_model.py`; per-level comparison in `data/morty_builds.tsv`. Morty mostly uses tier-2 parts;
  max-tier builds beat his top speed on most levels; Auto L3/L4 and Alt L4 tie on top speed (B cap) — winning
  there depends on acceleration. Differential is a trade-off (higher tier = lower ratio = less acceleration).
  **TBD**: how the race sim turns (top, accel) into a win (track/second mode), and the flight engine `O0E7D`.
- Part-name text: `\n` = forced line break, byte `0x1D` = the font's ½ glyph (only these two in part names).

### 4.8 Ground race simulation [static + live-checked, 2026-10-19]
`O0E7A_01CA` runs the race; returns 0 = player won (→ `[4560]`), 1 = lost. **Fully deterministic.**
- Racers: player `DS:BAE8` (id 1), Morty `DS:BAAE` (id 2). Struct: +0x00 id bit, +0x12 speed (16.16), +0x16 track
  index, +0x18 far ptr track, +0x1C mode, +0x1E finished, +0x20 laps left, +0x22 capB=min(A,B), +0x26 capA=A,
  +0x32/+0x36 accel.
- Tracks are resources of 6-byte records `(x, y, code)`: player `DS:39F4[v]`, Morty `DS:3A12[v]`; laps `DS:3A30[v]`.
  Auto L1 0x00E7/0x00E8 (no laps); Auto L2–3 + Alt L1–3 0x00E9/0x00EA (578 positions); Auto L4–5 + Alt L4–5
  0x00EB/0x00EC (995 positions).
- Event codes (jump table `O0E7A:01B0`, index = code+14): −5 lap (laps>0 → −1; index → 0), −4 mode 0, −3 mode 1,
  −2 finish (only when laps==0), −14/−13 sound; ≥0, −1, −12…−6 = positions.
- Frame: tick both (`O0E7A_0034`: mode 0 → speed=min(speed+accel, capA); mode 1 → if speed>capB: −0.2 else
  min(speed+accel, capB)); player moves **int(speed)** positions, then Morty; until someone finishes. Both in the same
  frame → player wins if speed ≥ Morty's. The fractional part of speed never moves a racer.
- `tools/race_sim.py` (exact + fast path, cross-checked), predicts the live Alt-Energy L2 win (frame 249).
- **`tools/race_logic.py` → `data/race_requirements.tsv`**: exhaustive per-level analysis. Max-tier builds win every
  ground level; safe rule "every performance slot ≥ tier 2" for Auto L1–2 and Alt L1–2, "≥ tier 3" (capped at the
  slot's best tier) for the rest. Winning share falls to 0.2–4% on Auto L4–5 / Alt L3–5.

### 4.9 Aircraft races (flight engine O0E7D) [**validated live** on Aircraft L3, 2026-10-19]
- Stats `O0E7D_0037(racer*, partlist*)`: **A = DS:3DBE[level]** (3.5/4/6/7/9, indexed by level within the building),
  B = 0; group switch over 0x0D–0x2F (`O0E7D:0381`): each group does `A −= T[tier]`, `B += T[tier]` or (blimp
  engine 0x0F) `B = T[tier]` — full map in `tools/flight_sim.py`. Paint/decal and a few groups (0x22/0x23) do nothing.
- Three phases: caps at racer +0x1C, accels at +0x28 (3 dwords each). Normal: caps A, accels B. **Glider (level 2)**:
  caps 1.5 / 5.0 / 1.5, accels −B / 0.3 / −B (towed at 1.5, flies up to 5.0, then sinks by its drag B).
- Tick `O0E7D_0428`: speed += accel[phase]; clamp to cap from above (accel ≥ 0) or below (accel < 0).
- Race `O0E7D_05E4`: player DS:BB64 (parts DS:6616, track `DS:3B96[level]`), Morty DS:BB28 (parts [6634], track
  `DS:3BA0[level]`). Position is 16.16 and accumulates speed (**fractional speed counts here**, unlike ground
  races); each frame steps int(new)−int(old) records. Step events (`O0E7D:05C7`, code+14): −12 skip (idx += x−1),
  −11/−9 phase 0, −8 phase 1, −10 phase 2 (unused), −7/−6 no-op, −2 finish, −1 end. Tracks: phase 0 → −8 → phase 1
  → −9 → phase 0 → finish. Ends when either finishes; player finished → win (ties to the player).
- **Live validation** (`BP 0B03:05A3`, `D 16CA:BB28` Morty / `D 16CA:BB64` player, caps at +0x1C, accels at +0x28):
  Morty caps 0x5970A ×3 / accels 0x23D8 ×3; player (2-blade prop, turbo flat engine, sharp-angle cockpit, medium-curve
  tail, elliptical wing, fowler flaps, GPS, wheel pants) caps 0x54CCD / accels 0x23D7 — model identical; simulator
  predicts player frame 337 vs Morty 321 → loss, matching the live loss (`[4560]=1`).
- `tools/flight_sim.py` → `data/flight_requirements.tsv`: max-tier wins all; safe rule ≥ tier 2 (L1–2), ≥ tier 3
  (L3–5). **Non-monotonic tiers**: blimp envelope (best tier 2) and engine (best tier 1); glider altimeter tier 3 is
  the worst (all glider parts add drag).

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

### 2026-10-19 — first patched build (`SSG_AP.EXE`)
`race_no_advance` confirmed: after winning Aircraft L3 and returning to the Technology Center, re-entering Aircraft
started L3 again from the beginning (fresh level), as predicted (race win clears `459C[b]`, level stays `4596[b]+2`).
`no_finished_refusal` with `[4596]=4` (SM): Automotive entered as "Level-6 BLIMP" — level 5 / vehicle 5 overflow into
Aircraft L1 (expected from `R02E0_0820`); needs the level picker.

## 6. Next step: dynamic analysis checklist (DOSBox-X debugger build)
1. ~~Building→category mapping~~ (done, §5). Confirm race-win path with `BP <load+02E0>:05B7`.
2. In a warehouse, find the live 07/09 array (search memory for the block's 40-byte pattern) → `bpm` it,
   solve a puzzle → door-open function.
3. Open a box → find writer to the live box list / inventory → part-pickup function and part IDs.
4. Exit building → confirm snapshot into block; re-enter → confirm restore path & level generation entry.
5. Map rec+0x40..0x27D by collecting known parts and diffing saves (`tools/players_dat.py`).
