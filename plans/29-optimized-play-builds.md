# 29 · Optimized builds for normal play

**Status:** planned, 2026-09-24. **Priority:** P1, small first delivery.
**Audit coverage:** issue 5. **Dependencies:** none. Supersedes plan 20.

## Objective and evidence

Make the normal local simulator build optimized, with a separate Debug build for diagnosis.
`Makefile` explicitly selects Debug on every `make`; `start.sh` selects it when configuring a
new build. The audited `build/CMakeCache.txt` was Debug. Windows presets already select Release.
This targets startup, simulation and context-preparation overhead. It does not promise faster
model generation. No Debug/optimized speedup was measured in the audit.

## Read and change

- `Makefile`, `start.sh`, `start.ps1`, `CMakePresets.json`, `CMakeLists.txt`.
- `tests/integration.py`, `tools/living_world_probe.py`: executable-path assumptions.
- `README.md`, `docs/MODELS.md`, `docs/VERIFICATION.md`.

## Implementation

1. Default single-configuration normal builds to `RelWithDebInfo` in `build/`. Retain explicit
   build-type overrides; provide a Debug target using `build-debug/`. Preserve Windows Release
   configuration and the launcher paths that already work there.
2. Reconfigure an existing Debug `build/` when normal play requests an optimized build. Checking
   only for the existence of CMakeCache is insufficient. Do not delete the user's build tree.
   Keep configure/build/launch selections consistent, including spaces in paths.
3. Let integration checks select game and replay executables explicitly, with current paths as
   defaults, so each build configuration can be verified without swapping binaries. Audit test
   assertions for `NDEBUG`: optimized tests must actually execute their checks.
4. Keep data generation separate from this change. Do not trigger geography regeneration merely
   to change compiler flags; use existing imported data for the measurement run.
5. Update build instructions and label existing timing tables by build type. Do not alter model
   slots, model size, prompts or simulation rules. In particular, keep the measured single-slot
   default unless a separate benchmark justifies a change.

## Verification and acceptance

- Fresh configuration and an existing Debug cache both produce the requested optimized binary;
  the explicit Debug workflow stays isolated. Check launcher selection on macOS and Windows.
- Build, run CTest, and run HTTP/browser integration and its replay/restart checks in the optimized
  configuration. Run focused checks in Debug too; do not silently disable assertions.
- On the same machine/data, compare at least five warm runs per build: process start to ready,
  campaign load, context preparation, and deterministic monthly simulation. Record compiler,
  flags, fixture size, median and range; report cold startup separately. No live inference is
  needed to establish this build change's CPU benefit.
- Accept only with identical deterministic state/events and no reproducible CPU-time regression
  above 10%. A noisy or absent gain must be reported as such, not as a proven speedup.
- Windows execution is a separate acceptance item; macOS results do not establish it.

## Boundaries and handoff

Use disposable saves; never open or modify `saves/campaign.sqlite`. No save format or replay
version change is required. Preserve prior plan files and historical benchmark evidence.
Record commands, selected binaries and measurements in a dated `docs/audits/` report, marking
unavailable platform checks explicitly. This plan replaces plan 20's obsolete instructions to
run the release change last, change parallel settings, and delete completed plans.
