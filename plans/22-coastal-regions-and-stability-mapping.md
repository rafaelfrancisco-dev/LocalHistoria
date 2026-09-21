# 22 · Coastal regions, and a World Bank code-mapping bug

**Status: done 2026-09-21.** Both issues were recorded as known gaps in plan 21.

## Goal

Make `regions.coastal` real so naval strength applies where a fleet could actually reach, and fix
the snapshot mapping that silently attributed one country's World Bank value to another.

## Issue 1 — `coastal` was read but never written

`src/status/force.cpp` decided naval support partly from
`w.territories[region].get("coastal", false)`, and nothing in the repo ever set that field. Plan 19
asked for a `coastal` column "from the geometry touching the ocean mask (preferred)" and it was
never added, so warships counted toward `militaryLabel` but contributed nothing to any assault at
campaign start, unless a port, a fleet or a port-marked city happened to be in the region.

**Fix.** `tools/region_coastal.py` unions the 258 country outlines in `countries.geojson` into a
single landmass. Interior borders dissolve in that union, so what remains of its boundary is
coastline. A region is coastal when it lies within 0.05° (about 5 km) of that line, a tolerance
that absorbs the misalignment between gbOpen ADM1 boundaries and the country outlines, which come
from different sources. Large inland seas count as coast, deliberately: Caspian and Great Lakes
flotillas are real.

The pass walks the coastline asking which regions it touches, rather than asking each of the 3,402
regions for its distance to the whole coastline, and regions drop out once marked. That is the
difference between 324 s and a few seconds; the naive form was too slow to sit in `make data`.

`regions` gained a `coastal` column, `tools/import_regions.py` runs the pass in its existing
geometry chain, and `src/server/campaign_factory.cpp` loads it into each territory. Existing saves
are unaffected: they have no `coastal` field and `get("coastal", false)` keeps today's behaviour.

## Issue 2 — the World Bank ships rows under another country's ISO3

Syria's political stability read 81.6, above Norway, while the World Bank's actual 2024 value is
25.4. The cause was not the name-matching fallback that plan 21 suspected. The API returns rows for
non-standard entities whose `countryiso3code` belongs to a different country:

| `country.id` | `country.value` | `countryiso3code` | landed on |
|---|---|---|---|
| *(empty)* | Taiwan, China | `SYR` | Syria |
| *(empty)* | Netherlands Antilles | `NLD` | Netherlands |

`download()` resolved `iso2.get(row['countryiso3code'])` first, so those two rows overwrote the
real countries.

**Fix.** Resolve the API's own two-letter `country.id`, fall back to the country name, and never
trust `countryiso3code`. A row with no id and an unrecognised name is an entity the game does not
model, so dropping it is correct. Verified against a live fetch: the strict rule resolves the same
207 codes, drops nothing, and yields Syria 25.4 and the Netherlands 73.2.

`download()` also dropped the hand-added `military_retrieved` key on every refresh; it is now
carried across.

The snapshot was then refreshed for real. Across all eight indicators the only changes were those
two values — no additions, no removals, no drift — which is what confirms the mapping bug was the
whole defect.

**North Korea was a false alarm.** Plan 21 flagged 71.4 as implausible. It matches the World Bank
exactly: their "Political Stability - Governance score" measures absence of organised violence
against the state, on which a closed authoritarian regime scores well. The United States at 64.3
is likewise upstream. Only Syria and the Netherlands were ever wrong.

## Verification

- `tests/data_test.py` gained coastal assertions: fourteen landlocked countries have no coastal
  region, four island countries are entirely coastal, and the global share stays in a sane band.
- Portugal's six interior districts, Japan's eight landlocked prefectures and Brazil's interior
  states all classify correctly, which is the check that the tolerance is not swallowing inland
  regions.
- `make test` 8/8; `python3 tests/integration.py` passes.

## Note

`docs/EVOLVING_WORLD.md` still described quality as `clamp(sqrt(gdppc/15000), 0.5, 1.6)` after plan
21 re-tuned it; corrected here to `clamp(sqrt(gdppc/20000), 0.6, 1.4)`.
