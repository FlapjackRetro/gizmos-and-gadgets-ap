# Gizmos and Gadgets setup

1. Build the native recompilation (gizmos-and-gadgets-recomp) from your own `ssg.exe`.
2. Copy `ap_mod.dll` (or `ap_mod.so`) into `<game folder>/mods/`. Optional: run
   `tools/title_logo.py` to put `GIZMOAP1.DAT` (Archipelago title logo) into the game folder.
3. Start the **Gizmos and Gadgets Client** from the Archipelago Launcher, pick the game folder
   (`/gamedir <path>`), and connect to the server.
4. Start the game. Use one player name per seed.

In the client, `/level <building> <n>` chooses which level a building loads next (1-5, among the levels
you have unlocked), `/level <building> auto` returns to the game's own choice.
