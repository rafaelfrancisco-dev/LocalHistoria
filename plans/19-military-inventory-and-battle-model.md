# 19 · Real military inventories and a deterministic battle model

Do plans 01 and 02 first. When this plan lands, it **replaces** plan 02's population-based military death
formula with one based on the forces actually committed (section "Casualties" below); plan 02 is the interim.
The rules and constants below are decided; implement them as written and tune only where a test in this
plan cannot be satisfied.

## Goal
Every nation has a real military inventory: active personnel, reserve personnel, major warships and combat
aircraft, seeded from real 2025 figures. Armies and fleets are formations drawn from that inventory with a
stated manpower or ship count, not abstract tokens. C++ decides from these numbers how many regions an
attacker can seize from a defender in a month, how many people and machines each side loses, and what a
nation can build at once. The model receives the same numbers in plain words and only describes.

## Data

### Definitions (write them into `docs/DATA.md` and `docs/EVOLVING_WORLD.md`)
- `active`: active-duty military personnel (people). `reserve`: trained reserve personnel.
- `ships`: principal surface combatants (frigate size and larger, including carriers and amphibious assault
  ships) **plus** submarines. Patrol boats, mine warfare and auxiliaries are excluded.
- `aircraft`: fixed-wing combat aircraft: fighters, attack aircraft and bombers. Helicopters, transports,
  trainers and drones are excluded.

### Sources and import
1. Personnel and spending from the World Bank, through the existing pipeline in `tools/import_stats.py`
   (`INDICATORS` at line 10; `--download` refreshes `data/source/country_stats.json`, CC BY 4.0):
   add `MS.MIL.TOTL.P1` → `military_personnel` (note: World Bank counts active plus paramilitary; that is
   acceptable as `active`) and `MS.MIL.XPND.CD` → `military_spend_usd`. Both come with a year; keep it.
2. New hand-curated file `data/source/military_2026.json` for reserves, ships and aircraft, with a `source`
   note per entry, covering every actor that has a navy or an air force of note (about 70 nations; the rest
   default to 0 ships and 0 aircraft, which is true for most micro-states and land-locked small nations).
   Sources to transcribe from (CC BY-SA, cite in `docs/DATA.md` and `licenses/`):
   Wikipedia "List of countries by number of military and paramilitary personnel" (reserves),
   "List of countries by number of military aircraft" (World Air Forces 2025 columns fighters + attack +
   bombers), "List of navies by fleet size" (principal surface combatants and submarines).
   Shape: `{"countries": {"IN": {"reserve": 1155000, "ships": 47, "aircraft": 640, "source": "…"}, …}}`.
3. Fallbacks, all flagged `estimated: 1` and listed in `stats_overrides.json` style:
   missing `active` → `0.3 % of population` (min 200, so micro-states are not zero);
   missing `reserve` → `0.5 × active`; missing `ships`/`aircraft` → 0.
   Non-state seeded actors (`rapid-support-forces`, `national-unity-government`, `afc-m23`): curated in
   `world_2026.json` `actors` entries (`active`, `reserve`; RSF ≈ 100,000 / 0; NUG-PDF ≈ 65,000 / 0;
   AFC/M23 ≈ 6,000 / 0), ships and aircraft 0.
4. `import_stats.py` writes a `military_stats` table (`code, active, reserve, ships, aircraft,
   spend_usd, year, estimated, source`). `main.cpp` loads it into `StatusSeed` (new member `military`), and
   `seedStatus` (`src/status.cpp:206`) writes per actor
   `status.military = {active, reserve, ships, aircraft, estimated}` and `status.military_spend_usd`.
   Saves without it are seeded on their next month exactly like `status_seed` backfill (`src/main.cpp:520–526`,
   store `military_seed` in `refs` for replay; `tools/replay_verify.cpp:24` applies it).

### Reference figures to check the import against (2024–25, rounded; verify against the sources above)

| actor | active | reserve | ships | aircraft | gdp per capita (US$) |
|---|---|---|---|---|---|
| US | 1,330,000 | 800,000 | 240 | 2,700 | 90,000 |
| CN | 2,035,000 | 510,000 | 200 | 1,900 | 13,900 |
| IN | 1,455,000 | 1,155,000 | 47 | 640 | 2,700 |
| RU | 1,320,000 | 2,000,000 | 90 | 1,500 | 17,800 |
| KR | 500,000 | 3,100,000 | 50 | 470 | 36,200 |
| KP | 1,280,000 | 600,000 | 75 | 450 | 1,050 |
| PK | 660,000 | 550,000 | 25 | 400 | 1,600 |
| UA | 900,000 | 1,200,000 | 0 | 100 | 5,500 |
| IR | 610,000 | 350,000 | 25 | 330 | 3,900 |
| IL | 170,000 | 465,000 | 12 | 340 | 60,300 |
| ES | 133,000 | 15,000 | 14 | 140 | 38,600 |
| PT | 28,000 | 210,000 | 7 | 27 | 32,100 |
| MG | 13,500 | 0 | 0 | 0 | 600 |
| KM | 1,000 | 0 | 0 | 0 | 2,050 |

## The model

All inputs are start-of-month state so replay is exact. Add to `src/status.cpp` in a `namespace force`.

### Quality
Equipment and training quality from wealth: `Q(a) = clamp(sqrt(gdp_per_capita(a) / 15000), 0.5, 1.6)`.
US 1.6, ES 1.6, PT 1.46, RU 1.09, UA 0.61, IN 0.5, PK 0.5, KP 0.5, KM 0.5.

### Formations
- `build army` by `a` in region R: takes `personnel = clamp(round(0.10 × active(a), to 1,000), 5,000,
  150,000)` from `active`, but never more than `uncommitted(a) = active − Σ personnel of a's existing armies`
  (drop the operation "no troops left to form an army" when `uncommitted < 5,000`). Entity fields:
  `personnel`. Completes in 2 months as today (mobilisation and movement).
- `build fleet`: `ships = clamp(round(0.25 × ships(a)), 1, 30)`, never more than uncommitted ships; drop
  when the nation has 0 uncommitted ships ("no major warships available"). Entity field `ships`.
- A destroyed army loses its personnel from `active` (they are dead, captured or dispersed); a destroyed
  fleet loses its ships. An `inactive` formation returns its personnel/ships to the uncommitted pool.
- Legacy entities without the field decode as `personnel = 10,000` / `ships = 3` (write the field when
  first touched so replay of old saves is unchanged; do it in `seedStatus` backfill for existing saves).

### Committed force at a region
For attacker `A` seizing region R controlled by `D` (start-of-month controller; owner when empty):
```
land(A, R)  = Σ personnel of A's active armies in R or adjacent to R
              + share(A) × uncommitted(A)
share(A)    = 0.20 in peace, 0.30 when A is a party of an open war/civil_war (mobilised)
air(A)      = 0.30 × aircraft(A) × 200          (a third of the air force can support one front)
sea(A)      = (R is coastal or an active fleet of A is in/adjacent to R) ? 0.30 × ships(A) × 1500 : 0
CP_attack   = Q(A) × (land + air + sea)

land(D, R)  = Σ personnel of D's active armies in R or adjacent to R
              + share_def(D) × uncommitted(D) / max(1, fronts(D))
share_def   = 0.25 in peace, 0.40 when D is a party of an open war/civil_war
fronts(D)   = number of distinct enemies D currently borders or fights (≥ 1)
air(D)      = 0.50 × aircraft(D) × 200 / max(1, fronts(D))
sea(D)      = same rule as sea(A) with D's ships
CP_defend   = 1.5 × Q(D) × (land + air + sea)      (defender's advantage: terrain, fortification, supply)
```
"Coastal" = a region with at least one adjacency across a strait or a coastline flag; if the importer has no
coastline flag, approximate with "region has a `port`/`fleet` entity or a seeded city with a port" — or add a
`coastal` column in `tools/import_regions.py` from the geometry touching the ocean mask (preferred).

`ratio r = CP_attack / CP_defend`. **Regions A may seize from D this month**: `r ≥ 1.5` → 2 (superior);
`0.75 ≤ r < 1.5` → 1 (limited); `r < 0.75` → 0 (too weak). The existing global tempo cap (2 by force per
actor per month) still applies. Defender modifiers: `D` in an open `civil_war` → `CP_defend × 0.6`;
`stability(D) < 25` → `× 0.8`. Landless attackers/defenders use their `active` as `uncommitted` and no
territory-based terms.

Worked results with the table above (`F` from adjacency only where stated):
- ES → PT (one border region, PT at peace): attack ≈ 1.6 × (26,600 + 8,400 + 0) = 56k; defend ≈ 1.5 × 1.46 ×
  (7,000 + 2,700) = 21k; r = 2.6 → **2 regions** (the probe's "Spain occupies West" keeps passing).
- PT → ES: attack ≈ 1.46 × (5,600 + 1,620) = 10.5k; defend ≈ 1.5 × 1.6 × (33,250 + 14,000) = 113k;
  r = 0.09 → **too weak**. Portugal cannot invade Spain; "Conquer all of Spain" becomes a failed or aborted
  attempt (update the README "Play" example accordingly: it currently claims occupation of Galicia).
  With one Portuguese army (5,000) adjacent: r = 0.16, still too weak. Correct.
- IN → PK (PK at peace): attack ≈ 0.5 × (291,000 + 38,400) = 165k; defend ≈ 1.5 × 0.5 × (165,000 +
  40,000) = 154k; r = 1.07 → **1 region**. Once PK is at war: defend ≈ 1.5 × 0.5 × (264,000 + 40,000) =
  228k; r = 0.72 → too weak without armies; IN with two field armies (145,000 each) adjacent: attack ≈ 0.5 ×
  (291,000 + 290,000 + 38,400) = 310k; r = 1.36 → 1 region. A grinding war, as it should be.
- PK → IN: attack ≈ 0.5 × (132,000 + 24,000) = 78k; defend ≈ 1.5 × 0.5 × (364,000 + 64,000) = 321k;
  r = 0.24 → too weak; with three PK armies (66,000 each): 177k → r = 0.55 → still too weak; four → 0.65;
  Pakistan cannot take Indian regions without an Indian collapse (stability < 25 or civil war). Correct.
- RU → UA (both at war): attack ≈ 1.09 × (396,000 + 90,000) = 530k; defend ≈ 1.5 × 0.61 × (360,000 +
  10,000) = 339k; r = 1.56 → 2 regions/month. Too fast versus reality; to slow it, Ukraine's armies count:
  the seed should place **two Ukrainian field armies** (90,000 each) and **two Russian** on the Donbas front
  in `world_2026.json` (`entities` list per conflict), which the seeder creates as active `army` entities.
  Then defend ≈ 1.5 × 0.61 × (180,000 + 360,000 + 10,000) = 503k; attack ≈ 1.09 × (264,000 + 396,000 +
  90,000) = 818k; r = 1.6 → 2. Still 2: apply the long-war rule below.
- **Long-war rule**: for a conflict open ≥ 12 months at the start of the month, `CP_attack × 0.6`
  (exhaustion, minefields, adapted defence). RU → UA: r = 0.98 → **1 region/month**, in line with 2024–25.
- UA → RU: attack ≈ 0.61 × (180,000 + 270,000 + 6,000) × 0.6 = 167k; defend ≈ 1.5 × 1.09 × (264,000 +
  396,000 + 150,000) = 1.32 M; r = 0.13 → too weak (Kursk-style raids are what the model may *describe*;
  no region changes hands). Acceptable.
- KP → KR: attack ≈ 0.5 × (256,000 + 27,000 + 0) = 141k; defend ≈ 1.5 × 1.55 × (125,000 + 47,000 + 0) =
  400k; r = 0.35 → too weak; with four North Korean armies (128,000 each): 397k → r ≈ 1.0 → 1 region. Fine.
- KM → MG: attack ≈ 0.5 × 200 = 100; defend ≈ 1.5 × 0.5 × 3,400 = 2.5k; r = 0.04 → too weak.

### Casualties (replaces the military rows of plan 02; civilians and displacement stay as in plan 02)
Per open conflict per party `p`, per month, from the force actually committed to that conflict:
```
committed(p) = Σ personnel of p's active armies in or adjacent to the conflict's affected regions
               + share(p) × uncommitted(p) / max(1, fronts(p))
deaths(p)    = committed(p) × deathRate[intensity] × kindMultiplier × jitter
deathRate    = {low 0.0005, medium 0.004, high 0.015}      (per month)
```
Deaths are removed from `active` first (and from the armies' `personnel` proportionally), then from
`reserve` if `active` is exhausted; a formation whose `personnel` falls under 2,000 becomes `destroyed`.
Check against the existing bound (`tests/status_tests.cpp:188`, RU+UA 80k–300k in 12 months):
RU committed ≈ 0.30 × 1.32 M + 180k = 576k → 8,600/month; UA ≈ 0.30 × 900k + 180k = 450k → 6,750/month;
total ≈ 185k/year. Passes.
Equipment attrition per month at high intensity when the other side has aircraft or ships: each party loses
`1 %` of committed aircraft (0.30 × aircraft) and `0.5 %` of committed ships, rounded down; medium
`0.3 % / 0.15 %`; low 0. Mobilisation: a party of an open war/civil_war at high intensity moves `2 %` of
`reserve` into `active` each month (Ukraine and Russia grow their armies while fighting).

### Construction capacity
`cap(a) = max(1, floor(gdp_bn(a) / 150))` projects `under_construction` at once (event sites excluded).
Armies and fleets additionally need uncommitted personnel/ships (above). IN 26, ES 12, PT 2, UA 1, KM 1.

### What the model reads
- `status.military` (in `buildStatus`, `src/status.cpp:89`): one string, e.g.
  "Active 1.46 million (1.02 million uncommitted), reserve 1.16 million, 47 major warships, 640 combat
  aircraft; field armies: Punjab 145,000, Rajasthan 145,000; fleets: Arabian Sea 12 ships" and a label from
  `CP` at full commitment: `< 20k` "negligible", `20k–200k` "minor", `200k–1M` "regional power",
  `1M–5M` "major power", `≥ 5M` "superpower" (compute `Q × (active + aircraft × 200 + ships × 1500)`).
- `within_reach_of` entries (`src/main.cpp:200–203`): "IN (superior: about 165,000 troops with air support
  against 154,000 defenders; up to 2 regions)", "PK (too weak: 78,000 against 321,000)". Round to
  thousands. The schema's region enumeration is unchanged.
- Entities in facts (`context()` and `adjudicationInput`): include `personnel` / `ships` on formations.
- Instruction (`src/providers.cpp:187`): "within_reach_of gives each nation's real strength here; a nation
  marked too weak cannot occupy: describe a failed, aborted or symbolic action. Losses are computed by the
  game; do not state numbers."

### UI
Stats panel (`web/app.js:194–200`): replace "armies · fleets 0 · 0" with "Active troops", "Reserve",
"Warships", "Combat aircraft", each with the uncommitted share as detail; entity popups
(`web/app.js:552`) show "First Army · 145,000 troops" / "Home Fleet · 12 ships". Country inspector shows the
`estimated` flag as today for GDP.

## Code changes
1. `tools/import_stats.py`: indicators, `military_2026.json`, `military_stats` table, fallbacks, and
   `coastal` flag in `tools/import_regions.py` (or an approximation, see above). Data test in
   `tests/data_test.py`: every actor has a `military_stats` row; ships/aircraft ≥ 0; the fourteen reference
   rows above within ±15 %.
2. `include/localhistoria/status.h` / `src/status.cpp`: `StatusSeed.military`; `seedStatus` writes
   `status.military`; new functions `double quality(w, a)`, `Json::Value committedForce(w, adjacency, a,
   region, asDefender)`, `int assaultCapacity(w, adjacency, attacker, defender, region)`,
   `std::string militaryLabel(...)`, `int constructionCapacity(w, a)`, `int uncommittedPersonnel(w, a)`,
   `int uncommittedShips(w, a)`; `applyMonthlyStatus` step 5 uses `committed()` for deaths, adds attrition
   and mobilisation (step 5b), and marks depleted formations destroyed (record a `war_toll`-style journal
   line "First Army was destroyed in Punjab").
3. `compileOperations` (`src/world.cpp`): `take()` consults `assaultCapacity` per (attacker, defender) with
   a per-pair counter; `build army/fleet` sets `personnel`/`ships` and checks the pools; `set_entity_status
   destroyed` by a non-owner requires `assaultCapacity ≥ 1` against the owner at that region.
4. `applyDelta` (`src/world.cpp:99–111`): validate `personnel`/`ships` are non-negative integers on
   `army`/`fleet` entities.
5. Prompt facts as above; `docs/EVOLVING_WORLD.md` "Realistic pacing" (Strength, Casualties, Capacity),
   README "Play" (limits list and the Spain example), `docs/DATA.md` (sources, definitions, licences),
   `docs/API.md` (`status.military`, entity fields).

## Tests
- `tests/status_tests.cpp` (real world section, lines 155–190): after seeding, assert the fourteen reference
  actors' `status.military` within ±15 % of the table; `assaultCapacity`: ES→PT 2, PT→ES 0, IN→PK 1 (peace),
  PK→IN 0, KM→MG 0, RU→UA 1 (after the seeded armies and the long-war rule), UA→RU 0; labels IN
  "superpower"? (`0.5 × (1.455 M + 128k + 70k) ≈ 0.83 M` → "regional power"; US `1.6 × (1.33 M + 540k +
  360k) ≈ 3.6 M` → "major power"; CN `0.96 × (2.0 M + 380k + 300k) ≈ 2.6 M` → "major power") — assert
  US and CN "major power", IN "regional power", KM "negligible" (adjust label thresholds only if needed and
  update the docs table). 12-month Russo-Ukrainian toll stays within 80k–300k; Ukraine's `active` shrinks by
  deaths and grows by mobilisation.
- `tests/world_tests.cpp`: fixture actors get `status.military` and `status` GDP/population; `far` (1,000
  active) occupying a `neighbor` region → dropped "too weak"; `old-state` building a third army when
  `uncommitted < 5,000` → dropped; two builds by an actor with cap 1 → second dropped; event sites never
  count; a destroyed army removes its `personnel` from `active` in the following `applyMonthlyStatus`.
- `tools/world_probe.cpp`: "conflict" (Spain occupies West) still matches; "overreach" (Portugal conquers
  Spain) must now yield no occupation and a description of a failed attempt; add
  `{"weak_raid", {"PT", "Send the army to occupy Galicia."}}` expecting no control change.
- `tests/integration.py`: a new campaign's `national_status['PT']` has `military` with `active` ≈ 28,000.

## Acceptance
All suites pass; the worked ratios hold within the stated tolerances; the fourteen reference rows import
within ±15 %; docs and README example updated; a fresh India campaign with "Invade Pakistan" takes at most one
region and reports a toll in the low thousands.
