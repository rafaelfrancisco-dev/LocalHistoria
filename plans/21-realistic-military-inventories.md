# 21 · Realistic military inventories for every actor

**Status: done 2026-09-21.** Every actor now carries a curated active-duty strength. The strength
bands and the quality multiplier were re-based at the same time. Spending, ships and aircraft were
left on their existing sources except where a gap had to be filled.

## Goal

Replace the World Bank active-plus-paramilitary personnel series with active-duty strengths for
every actor, and re-tune the strength constants that were fitted to the inflated numbers.

## Evidence

A new campaign showed Brazil with 762,000 active troops against a standing army near 360,000.
`tools/import_stats.py` took `active` from `MS.MIL.TOTL.P1`, which counts active duty **plus**
paramilitary forces — for Brazil ~400k state Policia Militar, for France ~100k Gendarmerie, for
Italy the Carabinieri, for Mexico the Guardia Nacional.

Plan 19 diagnosed this and corrected fourteen reference countries, leaving 228 untouched. The
resulting table:

| Problem | Rows (of 242) |
|---|---|
| `active` from the 2020 active-plus-paramilitary series | 157 |
| `active` invented as `max(200, 0.3% of population)` | 64 |
| `active` from 1999–2000 (BT, MV, SZ, ST) | 4 |
| `reserve` invented as `active / 2` | 163 |
| `spend_usd` unknown, stored as 0 | 79 |

Brazil ranked 7th in the world by combat power, ahead of Turkey, Italy, Japan, France and Ukraine.

## Decisions

- Active-duty strengths are **rounded model estimates**, flagged `estimated: 1`, each saying so in
  its `source`. They are the weakest provenance in the database and are meant to be replaced with
  cited figures later. No new external dependency was added.
- `military_2026.json` is the single home for curated military data. The fourteen plan-19 `active`
  values moved there verbatim from `stats_overrides.json`, which keeps `military_fallbacks` and
  stays the higher-priority patch layer.
- Military spending keeps its World Bank observation wherever one exists; curation fills gaps only.
  This preserves `PT.spend_year == 2024`, which `tests/data_test.py` pins.
- States with no armed forces are an explicit `0`, not a population-derived garrison: Costa Rica
  (abolished 1949), Panama (1990), Iceland, Mauritius, and the dependent territories.
- Scope was limited to `military_stats`. `country_stats` is genuinely World Bank 2025 and was left
  alone, as were the regional shares.

## Changes

- `tools/import_stats.py` — `military()` reads `active` from `stats_overrides.json`, then
  `military_2026.json`, then the World Bank series, and `active_year` follows whichever layer won,
  so provenance stays truthful. `spend_usd` gained a curated fallback below the World Bank
  observation. The 0.3%-of-population fallback remains for actors added later.
- `data/source/military_2026.json` — `active` for all 239 countries, `reserve` where it had been
  `active / 2`, spending for Taiwan and Venezuela.
- `data/source/stats_overrides.json` — the `military` block removed.
- `src/status/force.cpp` — `quality()` narrowed from `clamp(sqrt(gdppc/15000), 0.5, 1.6)` to
  `clamp(sqrt(gdppc/20000), 0.6, 1.4)`, and the `militaryLabel` bands re-based to
  20,000 / 150,000 / 750,000 / 2,000,000.

## Outcome

Largest corrections: Laos −77%, Belarus −70%, Iraq −58%, Brazil −53%, Italy −51%, Egypt −47%,
Indonesia −41%, France −33%. Taiwan rose 141% (it had been on the 0.3%-of-population invention).
No row now uses the active or reserve fallback.

The old bands left the top tier permanently empty and ranked South Korea above India, because a
3.2× quality spread let wealth outweigh mass. Tier distribution moved from
0 superpower / 4 major / 27 regional / 72 minor / 135 negligible to
**2 / 4 / 25 / 61 / 146**, with the US and China as superpowers and Russia, India, South Korea and
North Korea as major powers.

All seven pinned assault capacities (ES→PT, PT→ES, IN→PK, PK→IN, KM→MG, RU→UA, UA→RU) were
unchanged by the re-tuning, because `assault()` compares a ratio and is scale-invariant.

## Tests

- `tests/military_tests.cpp` — "Reference strength labels" updated to the intended tiers.
  The battle-threshold fixture now uses a GDP per head of 20,000 so `quality` is exactly 1.0: the
  "Superior at exact 1.5 threshold" case had been passing on floating-point luck at the old 1.6
  cap, and tested rounding rather than the threshold.
- `tests/data_test.py`, `tests/status/real_world.cpp` — unchanged and passing. The ±15% reference
  table still holds because the fourteen values were migrated verbatim.
- `make test` 8/8; `python3 tests/integration.py` passes, 6 turns replayed identically.

## Known gaps

- Syria (post-2024 reconstitution), Yemen, Libya and Afghanistan are genuinely uncertain.
- Reserve definitions vary: Vietnam 5,000,000 and Taiwan 1,660,000 are large mobilisation pools
  rather than IISS-style trained reserves, and they make those actors strong in a long war.
- ~~`src/status/force.cpp` reads a `coastal` field on territories that nothing ever writes.~~
  Fixed in plan 22.
- ~~`country_stats.stability` has implausible entries (Syria 81.6, North Korea 71.4).~~ Fixed in
  plan 22. Syria was genuinely corrupted, by a different cause than guessed here; North Korea's
  71.4 turned out to be the World Bank's own published value.
