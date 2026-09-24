# 20 · Release build by default, and documentation refresh

**Superseded 2026-09-24 by [plan 29](29-optimized-play-builds.md).** The text below is historical;
use plan 29 for implementation and verification. Do not follow the obsolete parallel-setting or
completed-plan deletion instructions below.

Do last (after the performance and simulation plans), so the numbers written into the docs are final.

## Goal
Ship a `RelWithDebInfo` build by default and bring README/docs numbers and descriptions back in line with the
code and measurements.

## Evidence
- `Makefile:7` and `start.sh:51` configure `-DCMAKE_BUILD_TYPE=Debug`; `build/CMakeCache.txt` confirms Debug.
  `CMakePresets.json` (Windows) already builds Release, and `docs/MODELS.md` warns Debug is slow on MSVC.
- Every API call re-parses the 1.1 MB campaign JSON (`decode(store.campaign())`), `eventLog` rescans all
  months, `applyMonthlyStatus` iterates 3,421 regions × conflicts; all fine in Release, sluggish in Debug.
- README says "a month takes roughly one to two minutes on a 4B model"; measured 159 s and 212 s before the
  performance plans.
- `docs/EVOLVING_WORLD.md` "Measured model results" predates independent slots (it says so).
- `docs/MODELS.md` recommends `--parallel 1` (changes with plan 15).

## Steps
1. `Makefile`: `BUILD_TYPE ?= RelWithDebInfo`, use it in the `cmake -S` line; `start.sh` the same; add a
   `debug` target that configures `Debug` into `build-debug`. Keep `-Wall -Wextra -Wpedantic`.
   Re-run `make test` and `python3 tests/integration.py` in RelWithDebInfo (the integration test timeouts are
   tight at 0.25 s per mock call; check they still pass).
2. Re-measure with the model running: `tools/living_world_probe.py --months 3` as Portugal and one manual
   India month with "Invade Pakistan"; record total seconds, prompt/generated tokens, and outcomes.
3. Update: README "Run" (build type), "Play" (turn time sentence, action counts, authority/consent rules,
   failed/partial badges, settings dialog), "A living political world" (relevance weighting);
   `docs/EVOLVING_WORLD.md` "Monthly simulation", "Realistic pacing", "Significance", "Measured model
   results" (new table), "Validation boundaries"; `docs/MODELS.md` "Speed", "Thinking", "Requests",
   parallel flags and the 9B experiment; `docs/API.md` new fields/routes; `docs/VERIFICATION.md` new suites
   and the date; `docs/ARCHITECTURE.md` file layout after plan 12.
4. Delete `plans/` files that are done.

## Acceptance
Fresh clone → `make test` builds RelWithDebInfo and passes; docs contain no stale numbers (grep for
"one to two minutes", "--parallel 1", "predate").
