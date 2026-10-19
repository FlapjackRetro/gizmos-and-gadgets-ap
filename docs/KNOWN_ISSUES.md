# Known issues / observations

| Date | Observation | Build | Status |
|---|---|---|---|
| 2026-10-19 | **Music stopped while solving a door puzzle.** | `SSG_AP.EXE` (patches `race_no_advance`, `no_finished_refusal`) in DOSBox-X 2026.10.01, likely under `DEBUGBOX` | **Unconfirmed cause.** The applied patches only touch building select (`02E0:0439/044A/045B`) and the race-win increment (`02E0:05B7`), neither of which runs during puzzles, so the patches are unlikely to be the cause. Candidates: native game behaviour, or debugger pauses / `DEBUGBOX` disturbing sound timing. **To check:** repeat in unpatched `SSG.EXE`, launched normally (not `DEBUGBOX`). |
