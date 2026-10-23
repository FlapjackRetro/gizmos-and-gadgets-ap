# Gizmos & Gadgets Archipelago Setup Guide

This guide covers installing the Gizmos & Gadgets randomizer and its APWorld for
[Archipelago](https://archipelago.gg).

> **Status:** early development, not yet fully playtested. Expect rough edges and please report problems on the
> [issue tracker](https://github.com/FlapjackRetro/gizmos-and-gadgets-ap/issues).

## What you need

- **Your own copy of *Super Solvers: Gizmos & Gadgets!*** (The Learning Company, 1993), the **DOS version 1.0**:
  the folder with `ssg.exe` and `gizmo000.dat` … `gizmo004.dat`. No game files come with this project.
- **The native recompilation** of the game, `gizmos.exe`, from
  [gizmos-and-gadgets-recomp](https://github.com/FlapjackRetro/gizmos-and-gadgets-recomp). It runs the game
  without DOSBox and loads mods. You build it from your own `ssg.exe` (step 2). Use a build made from the recomp's
  current version: the randomizer relies on hook points added to it on 2026-10-23.
- **[Archipelago](https://github.com/ArchipelagoMW/Archipelago/releases)** 0.6.8 or newer, to generate games, host
  them and run the Gizmos and Gadgets client.
- From this project's [Releases page](https://github.com/FlapjackRetro/gizmos-and-gadgets-ap/releases):
  - `ap_mod.dll`, the game mod (Windows)
  - `gizmosandgadgets.apworld`, the Archipelago world and client

> **Linux and macOS:** the recompilation and the mod build on Linux (macOS is untested). There is no prebuilt
> Linux mod yet; build `ap_mod.so` yourself (see [docs/MOD.md](MOD.md)). The rest of this guide works the same,
> with `/` instead of `\`.

## 1. Find your game folder

The *game folder* is the folder that holds `ssg.exe` and `gizmo000.dat` … `gizmo004.dat`, e.g.
`C:\DOS\Gizmos`. The game keeps its saved players (`PLAYERS.DAT`) there too.

**Use a separate game folder for each Archipelago seed.** The randomizer keeps its own files in an `ap` folder inside
the game folder, and the client refuses a folder that already belongs to another seed. Copying the whole folder is
the easiest way to start a new seed.

## 2. Build the game (`gizmos.exe`)

On Windows:

1. Download the recompilation from its GitHub page (**Code → Download ZIP**) and unzip it anywhere.
2. Double-click **`install-windows.bat`**. It downloads the tools it needs (no admin rights, nothing installed
   system-wide), asks for your `ssg.exe`, and builds `gizmos.exe`. This takes several minutes the first time.
3. When it asks, let it copy `gizmos.exe` into your game folder.

Start `gizmos.exe` once to check that the game runs. A console window opens next to the game window; it shows the
log, which is useful in bug reports.

> **"Windows protected your PC":** Microsoft Defender SmartScreen may warn about `gizmos.exe` or the mod because
> they are new and unsigned. Click **More info → Run anyway**.

`gizmos.exe` contains code generated from your copy of the game: keep it to yourself, like the game.

## 3. Install the mod

1. In your game folder, create a folder named **`mods`**.
2. Download `ap_mod.dll` from the [Releases page](https://github.com/FlapjackRetro/gizmos-and-gadgets-ap/releases)
   and put it in that folder:

   ```
   C:\DOS\Gizmos\
     gizmos.exe
     ssg.exe
     gizmo000.dat ... gizmo004.dat
     mods\
       ap_mod.dll
   ```
3. Start `gizmos.exe`. The console should show lines like:

   ```
   [mod] ap: Archipelago mod ready (...\ap, 0 item(s) received so far)
   [mod] loaded C:\DOS\Gizmos\mods\ap_mod.dll
   ```

   The mod creates the `ap` folder in the game folder on its first start.

## 4. Install the APWorld

The APWorld goes into your Archipelago installation: on every computer that **plays** (it contains the client),
and on the one that **generates** the game (when someone else hosts, give them the file).

- **Easiest:** double-click `gizmosandgadgets.apworld` and Archipelago installs it.
- **Manually:** copy `gizmosandgadgets.apworld` into Archipelago's `custom_worlds` folder, e.g.
  `C:\ProgramData\Archipelago\custom_worlds\`.

Restart the Archipelago Launcher afterwards so it picks up the new world.

## 5. Create your options file

1. Open the **Archipelago Launcher** and click **Generate Template Options**. A folder opens containing
   `Gizmos and Gadgets.yaml`.
2. Copy it, set your `name`, and change any options:

   | Option | Default | What it does |
   |---|---|---|
   | `level_access` | `race_progression` | `race_progression`: a building's next level opens when you win the race of the level before it (as in the game; won levels stay open). `open`: all 15 levels can be entered from the start. `item_progression`: level 1 of each building is open; each other level opens when you receive its *Level Access* item (e.g. `Aircraft Level 3 Access`) |
   | `cyberchimps` | on | Play with Cyberchimps in the warehouse. They open boxes and carry off what they find, Archipelago checks too, and steal parts from you. Off: no chimps |
   | `races_required` | 15 | How many different races you must win to finish. Any races count, in any building |

3. Put the YAML in Archipelago's `Players` folder and generate as usual (or send it to whoever is hosting).

## 6. Connect and play

1. In the Archipelago Launcher, open **Gizmos and Gadgets Client**.
2. Tell it where your game folder is (once; it remembers it):

   ```
   /gamedir C:\DOS\Gizmos
   ```
   Do this before you create the player in the game: the game reads your settings (Cyberchimps) from the folder.
3. Connect to the server: type the address (e.g. `/connect archipelago.gg:38281`) or use the box at the top, and
   enter your slot name. The client binds the game folder to this seed and slot.
4. Start `gizmos.exe`. The client says **Game connected** once the game is running with the mod; `/game` shows the
   game's status at any time and `/levels` which levels are open.
5. In the game, create a **new player** for this seed (the client remembers which player plays it and warns if you
   sign in as someone else). Instead of the game's "With Chimps / Without Chimps" question you see your
   `cyberchimps` setting.

Keep the client running while you play: it sends your checks and passes your items to the game.

### What you'll see

- **Picking a level.** Clicking a building at the Technology Center opens a list of its five levels plus *Return to
  Technology Center*. Levels that aren't open yet are greyed out. Each level keeps its own warehouse: leave and
  come back, and it's as you left it.
- **Boxes are checks.** Opening a box that holds a part sends that box's location; the box disappears and the HUD
  shows what it held, for example `5 Bananas`, `fat envelope` or `Shield for Quester`. Empty boxes still give
  bananas. In hints and spoilers the boxes are called `<Building> Level <n> - Box <k>`; the boxes carry no
  numbers in the game, so search the level's warehouse.
- **Your parts arrive in the Pieces box.** Received parts don't appear in the warehouse: they wait in the
  **Pieces** box at the bottom left of the assembly room's HUD, whose counter shows how many are there for this
  level. Click it to open the Archipelago inventory: pick a part of this level to put it on the conveyor belt, as
  many as you like, then **Close**. Parts of other levels are shown greyed; a part only works in its own level.
  The game's rule of at most two parts of a kind still applies: if you already have two, recycle one first.
  Bananas you receive are added by themselves.
- **Parts you move are ordinary parts.** Lay them on the assembly floor or drop them in the recycle bin; they never
  go back into the Pieces box. A recycled part lands in a box somewhere in the warehouse, which you can pick up
  again like in the game (it is not a check).
- **Cyberchimps.** A chimp that opens a box with a check carries the check off; hit it with a banana and it drops a
  box with that check (checks first, then any parts it stole from you). Chimps can still steal parts from you, but
  not from the assembly floor or the Pieces box. The Cyberchimp next to the Pieces box tells you how many checks
  this level's chimps carry. Without chimps it is grey.
- **Races.** Building the vehicle and winning the race is a check, and opens the building's next level (with
  `race_progression`; with `item_progression` levels open only with their Access items). On the podium, **Next Level** goes to that level if it is open, otherwise to the Technology
  Center. A race you have won can't be raced again: *Rerun Race* is refused, and when you go back into a won level
  for its remaining checks, its race door stays shut even with the vehicle complete. You can still take the
  vehicle apart.
- **Finishing.** Once you have won `races_required` different races, the client tells the server you are done.

### Optional: the Archipelago logo on the title screen

The mod can show the Archipelago logo on the title screen. It needs `GIZMOAP1.DAT`, a copy of your `gizmo001.dat`
with the logo drawn in, which you build from your own game files (it can't be downloaded, since it contains game
data). You need Python 3.8 or newer (for example from the Microsoft Store) and a
copy of this repository (**Code → Download ZIP** on GitHub).

- **Windows:** double-click **`tools\title-logo.bat`** and pick your game folder. It installs the Pillow image
  library if it's missing and writes `GIZMOAP1.DAT` into the game folder (`title-logo.bat -GameDir C:\DOS\Gizmos`
  skips the folder dialog).
- **Any system:** `pip install pillow`, then
  `python tools/title_logo.py <game folder> assets/archipelago_logo.png`.

Your `gizmo001.dat` is left untouched. Start the game: the console says `ap: title logo on`. Delete
`GIZMOAP1.DAT` to turn the logo off. The logo is © the Archipelago project, CC BY-NC 4.0 (see
[COPYING.md](../COPYING.md)), so `GIZMOAP1.DAT` is for non-commercial use too.

## Troubleshooting

- **The console shows no `[mod] ap:` lines.** Check that `ap_mod.dll` is in `<game folder>\mods\` and that you
  started that folder's `gizmos.exe`.
- **The console says a hook site is missing** ("site missing (recomp too old)"). Your `gizmos.exe` was built from an
  older recompilation; download it again and rebuild (step 2).
- **The client says the game folder belongs to another seed.** Use a new game folder for each seed (step 1).
- **"Game not responding" in the client.** The game was closed, or the mod isn't loaded.
- For a bug report, include the game's console output and the client's log.

## Uninstalling

Delete `mods\ap_mod.dll` from the game folder to play the normal game again. The `ap` folder holds the randomizer's
state for that seed; delete it (and that player from the game's sign-in list) to start over in the same folder.

---

## For maintainers: building a release

1. Build the mod for Windows against the recompilation's `include/gg_mod.h` (64-bit, like the game). With
   MinGW-w64 or llvm-mingw:

   ```
   cmake -S mod -B build-mod -DGG_RECOMP_DIR=path/to/gizmos-and-gadgets-recomp -DCMAKE_BUILD_TYPE=Release
   cmake --build build-mod
   ```

   (cross-compiling: add `-DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc -DCMAKE_SYSTEM_NAME=Windows`). This writes
   `build-mod/ap_mod.dll`. See [docs/MOD.md](MOD.md) for other compilers.
2. Package the APWorld:

   ```
   python tools/build_apworld.py
   ```

   This writes `dist/gizmosandgadgets.apworld`. Keep `world_version` in `apworld/gizmosandgadgets/archipelago.json`
   in step with the release.
3. Create a GitHub release and attach `ap_mod.dll` and `gizmosandgadgets.apworld`.
