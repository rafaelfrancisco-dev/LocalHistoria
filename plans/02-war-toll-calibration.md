# 02 · War toll calibration for large states

**Status: done (2026-09-18).** `militaryPerFrontRegion` {40, 400, 1,600} per party per front region, capped at `militaryPopulationCap` 0.008% of the recruiting pool; conflicts under 3 months old stay at most medium without fighting. High stayed at 1,600: Russo-Ukrainian War 187k deaths / 0.9 M displaced a year, Sudan 60k. Medium-war test bound widened to 2,500 (was 2,062). Live India probe not run; covered by the 1.4 bn border-conflict test.

## Goal
Interim fix. Plan 19 later replaces the military-death formula with one based on the forces actually
committed; keep this plan small and do not build on its constants elsewhere.

Monthly military deaths must reflect the fighting on the map, not the size of the country's population.

## Evidence
- Save: conflict `c1-1` "Punjab Border Conflict", kind `border_conflict`, declared `high`, 9 affected regions,
  **no region changed control**, no entity destroyed. Casualties 125,085 and 1,245,946 displaced after two
  months (52,462 in month 1). The seeded Russo-Ukrainian War at high intensity produced 16,075 and 15,125 the
  same months.
- `src/status.cpp:504–511`: `pool = max(current[p].population, 0.25 × affectedPopulation)`;
  `losses = pool × rate::military[intensity] × multiplier × jitter`. With India's 1.47 bn people and
  `military[2] = 0.00008`, `multiplier 0.4`: ~47,000/month.
- Rates: `src/status.cpp:12–27` (`namespace rate`). Intensity derivation: lines 446–459 (`ceasefire`,
  `fighting`, `declared`).

## Steps
1. Replace the population-proportional military pool with a front-based one. Add to `namespace rate`:
   `constexpr double militaryPerFrontRegion[] = {40, 400, 1600};` (deaths per affected region per month for
   low/medium/high, before kind multiplier and jitter) and `constexpr double militaryPopulationCap = 0.00008;`
   (never more than this share of the party's population per month, the old high rate).
   For each party p: `losses = min(regions.size() × militaryPerFrontRegion[intensity] × multiplier × jitter,
   cap × pool)` where `pool` is the existing expression. Split losses across regions with the existing
   `distribute` weights. Landless parties: same formula (they already fall back to front regions for weights).
2. Effective intensity (lines 454–458): when `!f.fighting` and the conflict has been open fewer than 3 months
   (`w.turn - c["started_turn"] < 3`, treat missing `started_turn` as old), cap `f.intensity` at 1 (medium).
   Seeded wars have `started_turn` far in the past and are unaffected.
3. Re-tune so the two existing calibration bounds still hold. The Russo-Ukrainian War front has ~12 regions:
   12 × 1600 × 1.0 ≈ 19,000/month before jitter, versus today's ~10k RU + ~3k UA military plus civilians.
   Adjust `militaryPerFrontRegion[2]` (try 1,000–1,200) until the 12-month total in
   `tests/status_tests.cpp:188` stays inside 80,000–300,000 and Sudan stays plausible (printed at line 186).
4. Update the table in `docs/EVOLVING_WORLD.md` "National status and war" step 5 (military deaths row) and
   the comment block at `src/status.cpp:7–11`.

## Tests (`tests/status_tests.cpp`)
- New case after line 108 ("Border conflicts are smaller"): a party with population 1.5 bn (set `a1`
  population to 1,400,000,000) in a `border_conflict` declared `high`, no seized regions, one front region
  pair: `last_month_casualties < 5000`.
- New case: same conflict with a seized region (fighting) is still `high` and larger.
- Existing: medium war deaths 100–2000 (line 85) still passes (2 front regions × 400 × jitter ≈ 560–1,040 plus
  civilians; adjust bounds only if needed and say so in the commit).
- Real-world 12-month check at lines 176–190 passes.

## Acceptance
`make test` passes; docs table updated; a fresh campaign with "Invade Pakistan" as India no longer reports tens
of thousands of deaths in a month with no map change (verify by reading `war_toll` text in `tests/integration.py`
style, or manually with `tools/living_world_probe.py`).
