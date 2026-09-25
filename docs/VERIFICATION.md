# Verification evidence

Verified on macOS / Apple silicon on 2026-09-17, using AppleClang 21, Drogon 1.9.13, JsonCpp 1.9.8, SQLite and CMake. This describes executed checks, not a production-readiness claim.

## Build selection

Normal macOS/Linux builds now use `RelWithDebInfo`; `make debug` builds separately in
`build-debug/`. C++ suites use throwing checks rather than `assert`, so `NDEBUG` does
not disable their verification. Python integration rejects `-O` for the same reason.
Select both executables when checking another build (including Windows Release):

```sh
python3 tests/integration.py --binary build-debug/localhistoria --replay-binary build-debug/replay_verify
```

`tools/living_world_probe.py` already supports the same two flags. Current build-change
validation and remaining acceptance gates are recorded in
[audit evidence](audits/2026-09-24-optimized-play-builds.md).

## Core and data

`make test` builds the app and replay verifier and passes the core, data, world and context CTest suites. The core executable reports **68 checks passed**, covering identifier shapes, lowercasing, the conflict and adjacency helpers, month advance and scheduled construction completion, serialization, free-text action shape/length/target/duplicate validation, malformed/timeout model rejection, thinking and output budgets following the reported context size, invalid-endpoint construction rejection, constrained request shape, duplicate/idempotent submissions, overlap prevention, transaction rollback, narration isolation, save/reload, interrupted jobs, reference mappings and attribution, missing/ambiguous mappings, recorded Wikipedia response parsing, bounded introduction/history excerpts and the longer chronicle source, chronicles replacing history excerpts in brief references, and chronicle storage, event selection and clearing on a new campaign. The context suite also checks that chronicle calls run without thinking, trim the long history source first and reject extra fields, empty or over-long summaries.

The world suite covers delta validation (occupation, secession, entity placement inside polygons with holes, construction schedules), history retrieval and atomic rollback. It also covers every feasibility rule: unreachable regions, the two-regions-per-month limit, annexation only after a month of occupation, one-region army moves, unfinished units, construction by decree, building without control, one new state per month, new states needing land, narrative-only events, partial and failed events (their flags, and the factual clause that related history, recent events and chronicle inputs read while the journal keeps the model's text), narrative world events that cannot act for the player, and schema alignment of one outcome per action. It also checks event sites: their expiry, quiet fading, renewal by a related or citing event, and invalid expiries. Events record their wars and treaties. The event log is checked for categories, heartland locations, scope, category and text filters, leaving the map after three months, and paging: whole-month pages, `more` only when an older month matches, and a destroyed city's completion staying major. Its projection is checked too: a commit that fails after writing a month's items leaves none behind, a save without the projection is backfilled on opening and reads exactly like the log written month by month, and an interrupted backfill resumes and replaces what it half wrote. Rolled-back events stay out of related history. Significance is checked too: map and war changes are always major, model-declared major events are capped at three a month (weightiest first), routine world developments are dropped, and only major events reach the map. The status suite checks:

- exact regional sums and local-currency formatting;
- annual growth over 12 months;
- war fronts on shared borders, with casualties and displacement only there;
- population conserved except for deaths, refugees crossing into bystanders, and returns after a ceasefire;
- intensity from fighting and kind;
- the occupation GDP floor, capped model effects, and new-state inheritance;
- historians' naming and numbering;
- determinism through saved JSON;
- a 12-month run of the real seeded world, checking the Russo-Ukrainian War toll stays within realistic bounds.

World tests cover named wars and treaties, invalid kinds, effect validation, party-only intensity changes, ended dates, and rejection of unnamed new conflicts while legacy ones stay valid. Integration tests cover:

- national status and seeded wars on a new campaign;
- war tolls in the journal;
- a model-declared war getting its fallback name, a front and casualties;
- a save from before national status gaining it and still replaying.

The browser test covers the stats panel, the Wars & treaties dialog, the map mode colouring Donetsk as a front, the Countries mode (no subdivision outlines, meshed national borders), a JS heap below 300 MB after campaign load (153 MB measured), and the region layer surviving a new month without being rebuilt.

A live check on 2026-09-17 ran `./start.sh` (Qwen3.5-4B via `llama serve`), then a Portugal turn with "Declare war on Spain over the disputed Olivenza border and send troops to occupy Galicia" and "Sign a defence pact with Morocco":

- **Before the fix**, adjudication included the fronts of every seeded war, giving an 11.4k-token prompt that ran out of output tokens.
- **After the fix**, with fronts limited to wars of acting nations and an answer budget that scales with the number of actions, the prompt was 5.2k tokens and the turn took 181 s.
- **Outcome.** The model opened a low-intensity *Portuguese-Spanish Border War* (35 killed and 889 displaced along 12 border regions) and a *Portugal-Morocco Defence Pact*. The seeded wars reported their monthly tolls.
- **Weakness.** The small model tends to attach mild "growth" effects to routine events, including Spain's economy for being attacked; C++ caps keep this bounded, and since plan 18 the compiler corrects the sign for parties to new or escalated wars, owners of seized regions and parties to trade treaties.
- **Effect overrides (2026-09-18).** `build/world_probe` with the same model: before the override the `conflict` scenario (Spain occupies West, a high-intensity `border_conflict`) gave Spain `economy: growth`; after it, Spain and Portugal both get `decline`/`decline`. A one-month `tools/living_world_probe.py` run committed in 59 s and replayed identically.
- **Military inventories (2026-09-18).** `build/world_probe` with the same model: `conflict` (Spain occupies West) still occupies; `overreach` ("Conquer all of Spain") and `weak_raid` ("Send the army to occupy Galicia") change no control, and the model describes a symbolic or aborted attempt, 18–25 s each. A fresh India campaign with "Invade Pakistan" took 117 s: India occupied Punjab only (its second `occupy` was dropped for military strength) and the new high-intensity border conflict killed about 2,900 soldiers (India 2,156, Pakistan 761). Civilian deaths (~7,600) and displacement (9.6 million, almost all the one-off 8% of Pakistani Punjab's 117 million on seizure) came from the unchanged plan 02 rates. The export replayed identically. The one-off now scales with the troops committed to the seized region: the deterministic test in `tests/military_tests.cpp` gives 1.3% of Punjab (about 1.5 million including the monthly front rate) and still the full 8%+ for a Russian-seized oblast.
- **Active-duty inventories and coastal regions (2026-09-21).** Plan 21 replaced the World Bank active-plus-paramilitary series for every actor and re-tuned `quality()` and the strength bands; plan 22 added the `coastal` column and fixed a World Bank code-mapping bug. `make test` 8/8 and `python3 tests/integration.py` both pass, 6 turns replaying identically. The seven pinned assault capacities (ES→PT, PT→ES, IN→PK, PK→IN, KM→MG, RU→UA, UA→RU) are unchanged by the re-tuning, because `assault()` compares a ratio. Strength tiers moved from 0/4/27/72/135 to 2/4/25/61/146 across superpower→negligible. 1,640 of 3,421 regions are coastal; the chunked classifier returns the same set as the unoptimised reference. `tools/region_neighbors.py` moved from buffer-and-measure to a `dwithin` tree predicate: 1m53s instead of ~15 minutes, producing a byte-identical 27,128-pair adjacency set. Note that the India/Pakistan casualty figures recorded above predate the re-tuning and no longer describe current behaviour.

- **Meaningful initiative slots (2026-09-22).** Plan 23. `tools/living_world_probe.py --country BR --months 6` (Qwen3.5-4B, M1 Pro, no player orders): 6/6 months complete, 91.5–136.7 s to commit (mean 105.1 s), 6 turns replayed identically. All six regional slots landed on a region with something at stake and none was ceremonial: South Kordofan reinforcing garrisons against the RSF, Kherson ruling on displaced persons, M23 seizing a road junction in North Kivu, cross-border patrols in disputed Abyei, Kharkiv militia securing abandoned outposts, RSF humanitarian logistics at El Fasher. Sudan, Ukraine and the DRC are tier-0 for a Brazilian player, so under the previous ranking they could not have outranked Brazil's own regions or its neighbours' — they won only because stake now leads the governor ranking. No background development repeated an earlier one. The re-ask never fired in the run (24 decide calls over six months, exactly four a month), so the tightened instruction carried it alone and the retry stayed an unexercised safety net; `tests/turn_tests.cpp` covers it deterministically. For comparison, the real Brazil save this plan came from spent all six of its regional slots on ceremonies (a Marowijne heritage town hall, a Cojedes tourism hearing, a Lavalleja "Green Commute") and repeated one AMAMBAY humanitarian-crisis line four times.
- **Weakness.** `ceremonial()` is stem matching and inherits `consequential()`'s escape hatch, so a gesture that happens to contain a consequential stem stands: "New Caledonia establishes a new *autonomous* environmental protection zone … to generate tourism" was not re-asked, because `autonom` matched even though the word is an adjective about the zone. The instruction, not the predicate, is the primary lever.

The Python data suite verifies 258 unique feature IDs, France/Norway/Kosovo/Taiwan/Somaliland/Northern Cyprus normalization, unresolved metadata handling, 241 actor records, 152,970 city records, coastal classification (landlocked states have no coastal region, island states are entirely coastal), separate country/city Wikidata fields, absence of synthetic national population totals and complete supplied flag coverage.

## Real HTTP and browser

`python3 tests/integration.py` starts actual Drogon servers against disposable temporary databases. A controlled loopback model server drives real HTTP timeouts and malformed responses; an unreachable endpoint, a timeout and a malformed response each fail the whole turn outright with no state change and a non-empty error. Concurrent duplicate requests return one job. Competing keys, stale requests, unknown targets and typed (non-text) actions fail without state changes. The suite checks free-text actions end-to-end through a model response, frozen reference revisions after cache replacement, save/restart equivalence, process-kill recovery before commit, and that the `--llama-endpoint=` CLI flag and the `POST /api/v1/model-config` web-UI route both take effect immediately and persist across a restart with no flag given.

The replay executable reconstructs three turns from persisted initial state and intentions, comparing every resulting state and authoritative event. It rejects a tampered replay. No inference is needed for replay.

Headless Chromium verifies country-list selection, a coordinate click on the map, zoom, writing action cards, draft persistence across reload, adding and removing map-region target chips, ending the month, polling, journal events, a region transfer and port construction shown on the map, a failed event marked "Failed · " in the journal and "did not happen" in the Events dialog, page reload and resume. It also deliberately returns 404 for a flag to exercise the text fallback, selects Antarctica with no city records, and verifies persistence across server restart. External browser requests are blocked; none are attempted. No JavaScript runtime errors occur. The expected missing-flag 404s are fixture responses, not absent vendored files. The desktop screenshot was inspected. Screenshots remain in ignored `tests/artifacts/`.

## Wikipedia and model boundaries

A live call through the actual Drogon transport succeeded for Portugal: country code PT → Wikidata Q45 → English article Portugal, revision **1375265884**. The returned cache contains article URL, retrieval time, language, attribution, a bounded introduction and a separate bounded history excerpt. A shortened recorded API response is included with attribution under `tests/fixtures/`. Live source articles are not pinned to January 2026. Other countries and upstream outages are handled as unavailable; they were not exhaustively checked live.

Real-model behavior (Qwen3.5-4B via `llama serve`), memory use and turn latency are measured in [Evolving world](EVOLVING_WORLD.md#measured-model-results) and [Local inference](MODELS.md#context-size-and-memory). The automated suites use injected transport and a real loopback fixture HTTP server standing in for llama-server, and make no external network requests.

Not covered: full assistive-technology audit, other browser engines, large multi-year endurance runs, all live Wikimedia country mappings, production deployment or multiplayer. All source provenance limitations are documented in DATA.md.

## Environment note

Installing Drogon triggered a Homebrew automatic-update conflict in the existing `gromgit/fuse` tap. That task-started rebase was aborted and its original `main` branch and two local commits restored; its worktree was clean afterward. Drogon remained installed. No tap changes are part of this project.


## War momentum and invasion handling (plan 24, 2026-09-22)

The resumed implementation passes all eight CTest suites and the full HTTP/Chromium integration suite,
including replay, save/restart, treaty acceptance and explicit deadline/cancellation checks. The test
server now has a 15-second whole-turn budget independent of its 250ms transport timeout; the dedicated
three-second deadline test is unchanged. Desktop browser output was inspected; no new accessibility
coverage is claimed.

Deterministic checks cover overwhelming advances, contested fronts remaining fixed for 24 months,
retaking occupied land, low intensity and ceasefires, shared ordered/autonomous seizure limits,
missing inventories, displacement, unique map-event locations, stalled-war facts, pending peace and
accepted peace silencing the toll. The real seed (including coastal regions) keeps every controller
unchanged in every month of a passive year. A separate six-month Brazil–Paraguay fixture produces at
least two autonomous advances while preserving Paraguayan ownership. Reservists contribute 5% to
defence; RU→UA and IN→PK retain capacity 1, while ES→PT becomes 1.

The existing six-turn Brazil campaign was read only, exported, and replayed identically using each
turn's recorded status version 2. Version 3 gates new front movement and stalemate tracking. The
campaign file is not used by any running probe.

Final local-model tests: `invasion`, `finish_invasion`, `capital_push`, `weak_raid`, `overreach` each
match the expected map outcome 3/3; secession passes a separate smoke check. Successful invasion
fixtures deliberately use synthetic superiority, while weak-force cases retain real inventories.
The capital-push failure was a reasoning-budget vocabulary dependency; state-derived offensive
options now enable the existing bounded reasoning pass. Per-actor grammar definitions are emitted
only for actors with actions, and transfer recipients retain validated ID syntax so newly created
states can receive land. Details and prose-quality limits are in EVOLVING_WORLD.md.

Live twelve-month Brazil campaign evidence will be recorded here after completion.

## Plan 26 phases G–J (2026-09-23)

Each phase had its own gate and commit:
- **Tests:** 17 CTest suites, `make format-check`, and `python3 tests/integration.py` (HTTP,
  browser, 8 turns replayed identically without inference, restart).
- **Legacy replays:** the v0/v2/v3/v4 replays run inside CTest.
- **Real-world probe:** a scratch 36-month idle run of the seeded 2026 world under the realism
  lifecycle. Front lines were identical before and after every phase.
- **Deterministic scenarios** (`tests/status/`):
  - displacement ledger conservation, returns, and reload;
  - allowance competition, efforts, trade, replacement and demobilisation;
  - exhaustion, capture effort, whole-front weighting and renamed fronts over 24–36 months.
- **Scheduling** (`tests/politics_tests.cpp`, `tests/turn_tests.cpp`): mentions never crowd out
  the enemy, a finite urgent backlog is served within two months, treaty answers come first, and a
  retried month asks the same nations.
- **Live model:** see MODELS.md. This is a two-month A/B on a copied save, not a full campaign
  matrix.

## Plan 26 post-implementation audit (2026-09-23)

Full report: [re-audit](audits/2026-09-23-simulation-realism-reaudit.md).
- **Fresh deterministic evidence:** 17/17 CTest, format, integration with browser, and the original
  audit probe rerun under the realism lifecycle, where all eight observations are fixed.
- **Live Qwen3.5-4B evidence:** `world_probe` × 3, plus a six-month Ukraine campaign on a disposable
  save that replayed identically without inference.
- **Live-only defects found:** narrative outcomes are blanked (N3), AI nations never sign
  proposals (N8), and a cession needs no recipient consent (N7).
- **Fixed the same day:** these and N1, N2, N4–N6. After the fixes: 17/17 CTest (45.2 s),
  integration with browser, and a rerun of the six-month live campaign that replayed identically
  (blanked outcomes 37/60 → 4/65). Plan 26 is accepted.

## Plan 33 bounded history reads (2026-09-25)

`build/history_benchmark make <save> <months>` writes a disposable synthetic campaign: 3,420 regions,
190 nations, ~0.9 MB of state per month and ~23 journal items a month (structured events with
places and causes, front advances, war tolls, unrest, construction). `measure <save> 30` times reads
on a copy, 30 repetitions each, and then commits five more months. Measured on an Apple M1 Pro with a
RelWithDebInfo build. The "before" fixtures were written by the previous build; the "after" copies
were backfilled when opened.

| Read (p50 / p95 ms; statements) | 12 months before | 600 months before | 12 months after | 600 months after |
|---|---|---|---|---|
| First read after each commit | 28 (max 30) | 509 (max 519) | 7.0 (max 8.2) | 7.4 (max 8.0) |
| First read after opening | 36; 14 stmts | 1,011; 602 stmts, 4.9 MB | 8.1; 2 stmts | 7.0; 2 stmts, 39 KB |
| Common first page, repeated | 0.15 (cached) | 0.14 (cached) | 7.8 / 10.4; 2 | 7.1 / 7.5; 2 |
| Map (`on_map`, limit 500) | 0.09 | 1.7 | 3.5 / 3.9 | 15.4 / 18.1 |
| Related history (context) | 8.9 | 711 | 8.5 | 18.6 / 19.7 |
| Handler lock hold (decode + first page) | 19 / 20 | 20 / 21 | 27 / 29 | 27 / 28 |
| Peak RSS | 57 MB | 137 MB | 57 MB | 58 MB |

- **Gates:** the 600-month common-page p95 is 7.5 ms, against the 200 ms target and 2× the 12-month
  figure. A page costs 2 statements at every length, and the lock hold stays under 50 ms. Before,
  every read after a commit rebuilt all months (one query each). The old cache made repeated reads
  of one month faster (0.14 ms), but that cost was paid again after every commit.
- **Remaining scans (worst cases):** a text search with few matches and an actor scope with few
  events walk the whole projection in SQL. At 600 months, a rare word takes 10.7 ms and a nation
  with 38 events takes 16.5 ms, up from 0.16 ms and 1.6 ms at 12 months. There is no FTS.
- **Map and context:** the map returns its 500-item limit at 600 months because long open wars keep
  their front advances on it. Related history was quadratic: each causes step scanned every cause
  link through the `(kind,target)` index. Walking it by event key returns the same rows (checked on
  the 600-month fixture).
- **Backfill:** opening a save from before the projection backfills it once, 24 months per
  transaction: 57 ms at 12 months, 224 ms at 120 and 1.2 s at 600. Commit time is unchanged
  (~90 ms).
- **Equivalence:** a one-off harness compared the previous implementation with the new one on all
  three fixtures. It used 210 filter combinations covering scope, categories, text, major, map,
  limits and `before_turn`, walking every page. It ran against the saved state and against a
  changed current state: ended, reopened and removed wars, new war parties, land changing hands,
  moved and destroyed entities, and a removed nation. Events matched on every page, with no
  skipped or duplicated events. By design, `more` is now false when no older month matches, and a
  destroyed city's completion stays major.
- **Not measured:** the lock hold under real HTTP concurrency, and a real long campaign (none
  exists). Windows was not measured.

Plan 34 benchmark fixtures have a deterministic live-WAL regression:
`python3 tests/benchmark_fixture_tests.py` (also CTest `benchmark_fixture`). It checks committed
versus uncommitted data and rejects overwrites. The paired runner and remaining live-model,
semantic-quality and two-platform acceptance gates are documented in [PLAN_34.md](PLAN_34.md).
Fixture integrity and deterministic replay do not establish real-model operation reliability.
Plan 34 live results (single samples per arm, M1 Pro, Qwen3.5-4B Q4_K_M, llama b11011) and the
stopped final screen are recorded in [PLAN_34.md](PLAN_34.md). HTTP/browser integration and
restart/replay passed before screening. Windows was not measured.
