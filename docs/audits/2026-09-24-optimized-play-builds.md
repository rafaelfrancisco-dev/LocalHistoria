# Optimized play builds — initial implementation, 2026-09-24

Plan 29 now selects optimized application builds for normal macOS/Linux play. This is
an initial implementation, not a completed performance acceptance report.

## Changes

- `make`, `make build`, direct fresh single-configuration CMake, and `start.sh` default
  to RelWithDebInfo in `build/`. Normal configure upgrades an existing Debug cache in place.
- `make debug` builds in `build-debug/`. `BUILD_TYPE=Debug` selects that directory by
  default in Make and the launcher. Explicit `BUILD_DIR` and `BUILD_TYPE` remain supported.
- `make build` and the launcher do not regenerate imported data. Existing `make` data
  prerequisites are preserved. Validation used the existing imported world database.
- Integration accepts `--binary` and `--replay-binary`; living_world_probe already did.
  Optimized C++ suites retain their throwing checks (no assert/NDEBUG-dependent checks
  found in C++ test sources). Integration refuses Python `-O`.
- Windows presets and launcher remain Release / `build/Release/localhistoria.exe`.
  Model parameters, simulation rules and save/replay formats are unchanged.

## Executed checks

Host: Apple silicon macOS 27.0 (26A428), AppleClang 21.0.0. Optimized flags in the
CMake cache: `-O2 -g -DNDEBUG`; Debug uses `-g`.

```sh
make build
ctest --test-dir build --output-on-failure
make debug
ctest --test-dir build-debug --output-on-failure
python3 tests/integration.py --binary build/localhistoria --replay-binary build/replay_verify
```

Both builds succeeded; both configurations passed all 17 CTest checks. Observed total
suite times were 38.76 s optimized and 48.12 s Debug. These were single runs overlapping
other work, not controlled performance samples and not evidence of a quantified speedup.

The existing `build/` Debug cache now reports RelWithDebInfo. A fresh configure at
`/tmp/localhistoria-plan29 fresh build` without a build-type argument also reports
RelWithDebInfo. `build-debug/` reports Debug. No build tree was deleted.

`bash -n start.sh`, Make dry runs, and isolated launcher executions with fake CMake,
HTTP readiness and game executables passed for RelWithDebInfo, Debug, and a custom Release
build directory containing spaces. These verify configure/build/launch path selection,
not live model startup. The Bash 3.2 check caught an unbraced variable adjacent to a
Unicode ellipsis; it was fixed and all three cases rerun successfully.

Integration initially could not bind loopback inside the sandbox; rerun with the required
local networking access. All integration campaigns use temporary saves. The user's
`saves/campaign.sqlite` was never opened or modified. HTTP checks passed, including
four-turn replay, legacy replay, restart and failure recovery. Browser checks also passed:
selection, free-text cards, draft persistence, map targets, progress, events and reload,
with no external requests or JavaScript errors. The browser campaign replayed all eight
turns identically and survived a server restart. Missing fixture flag 404s were expected.

## Remaining gates

- At least five controlled warm samples for process readiness, campaign load, context
  preparation and deterministic monthly simulation, with medians/ranges, fixture sizes,
  cross-build state/event equivalence, and the no-regression gate. Cold startup must be
  reported separately. No speedup is claimed from the build-flag change alone.
- Windows execution and launcher acceptance; source inspection confirms the existing
  Release selection but does not establish runtime behavior on Windows.
- No real-model performance comparison was run or required for this initial build change.
