# Development target: DOSBox-X

DOSBox-X is the reference emulator for development, chosen for its built-in debugger, save states and
`DEBUGBOX`. Other DOSBox flavors (Staging, vanilla, DOSBox Pure) will be covered later by
compatibility testing. The client's memory-location strategy (Borland signature scan, see RE_NOTES §2)
is deliberately emulator-agnostic.

## Setup
1. Install a current DOSBox-X release.
2. Mount the game: `mount c C:\DOS` then `c:` and `cd \GIZMOS`.
3. Keep a pristine copy of `ssg.exe` and `PLAYERS.DAT`; work on copies.
4. Suggested `dosbox-x.conf` settings for repeatable sessions:
   ```ini
   [cpu]
   core=normal        ; the debugger is most reliable on the normal core
   cycles=fixed 20000 ; deterministic speed; adjust to taste
   ```

## Starting a debug session
```
DEBUGBOX SSG.EXE -sb
```
The debugger breaks at the program entry point. Because the EXE's entry is `0000:0000`, **the CS shown
at that moment is the load segment**. Then run:

```
python tools/dbgaddr.py <CS value>
```
This prints ready-to-paste `BP`/`BPM`/`MEMDUMPBIN` commands with the actual segments for this run (they
change if the DOS environment or memory layout changes, so regenerate per session). Press **F5** to continue.

### Notes
- **The first hit after setting a `BPM` is a baseline hit** (BPM assumes the byte was `00`); just F5.
- **Resident code** (segments below `0x0DE0`, e.g. the race dispatcher at `02E0`) sits at a fixed
  load+offset, so `BP` works.
- **Overlay code** (`O0Exx_*` functions) is loaded by VROOMM into a buffer at runtime and may move or be
  evicted. Code breakpoints on overlays aren't reliable; break on **data** (`BPM`) instead, then look at
  the stack/CS when it fires.
- `BPM` **works** in stock DOSBox-X 2026.10.01 (it watches a single byte). If it ever doesn't fire, the build may not check memory breakpoints on every instruction (in classic
  DOSBox that needs a "heavy debug" build). Fall back to dump diffing (below).

## Recipe: find a hook by dump diffing
1. Save a DOSBox-X save state right before the action (e.g. standing next to a box).
2. Break into the debugger (Alt+Pause), `MEMDUMPBIN <DS>:0000 10000`, rename `MEMDUMP.BIN` → `a.bin`.
3. F5, do the single action, break, dump → `b.bin`. Load the state and repeat with a *different* box → `c.bin`.
4. `python tools/memdiff.py a.bin b.bin c.bin --words`. Offsets that change on every step are candidates.
5. Put a `BPM` on the best candidate, reload the state, repeat the action: the break lands in the code
   that performs it. Note CS:IP and the bytes there, and match them in `dis/` (`tools/disasm.py` output).

If nothing relevant changes in DGROUP, the state is in far heap memory: dump a wider linear range
(e.g. the 640 KB of conventional memory) and diff that.

## Hunt list (RE_NOTES §4–5)
| # | Goal | Start with |
|---|---|---|
| 1 | ~~Building index → category~~ **done**: 0 Automotive, 1 Aircraft, 2 Alt-Energy | — |
| 2 | Confirm race-win path | `BP <race>:05B7`, win any race |
| 3 | ~~Door / puzzle solved~~ **done**: `R02E0_0A24` sets door state 7→9 | `BP <load+02E0>:0A24` |
| 4 | ~~Part box opened~~ **done**: `R08F5_0002`; grant = `R0834_08D7(0, part)` | `BP <load+08F5>:0002` |
| 5 | Level generation vs restore on building entry | `BPM DS:459C` (+2b), enter/exit a building |
| 6 | Parts inventory layout (rec+0x40..0x27D) | dump diff `--base 0x4562` on record-only dumps while collecting parts |
