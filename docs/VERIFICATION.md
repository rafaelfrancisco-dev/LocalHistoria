# Verification evidence

Verified on macOS / Apple silicon on 2026-09-17, using AppleClang 21, Drogon 1.9.13, JsonCpp 1.9.8, SQLite and CMake. This describes executed checks, not a production-readiness claim.

## Core and data

`make test` builds the app and replay verifier and passes the core, data, world and context CTest suites. The core executable reports **54 checks passed**, covering month advance and scheduled construction completion, serialization, free-text action shape/length/target/duplicate validation, malformed/timeout model rejection, thinking and output budgets following the reported context size, invalid-endpoint construction rejection, constrained request shape, duplicate/idempotent submissions, overlap prevention, transaction rollback, narration isolation, save/reload, interrupted jobs, reference mappings and attribution, missing/ambiguous mappings, recorded Wikipedia response parsing, bounded introduction/history excerpts and the longer chronicle source, chronicles replacing history excerpts in brief references, and chronicle storage, event selection and clearing on a new campaign. The context suite also checks that chronicle calls run without thinking, trim the long history source first and reject extra fields, empty or over-long summaries.

The world suite covers delta validation (occupation, secession, entity placement inside polygons with holes, construction schedules), history retrieval and atomic rollback. It also covers every feasibility rule: unreachable regions, the two-regions-per-month limit, annexation only after a month of occupation, one-region army moves, unfinished units, construction by decree, building without control, one new state per month, new states needing land, narrative-only events, narrative world events that cannot act for the player, and schema alignment of one outcome per action. It also checks event sites: their expiry, quiet fading, renewal by a related or citing event, and invalid expiries. Events record their wars and treaties. The event log is checked for categories, heartland locations, scope, category and text filters, leaving the map after three months, and paging. Rolled-back events stay out of related history. Significance is checked too: map and war changes are always major, model-declared major events are capped at three a month (weightiest first), routine world developments are dropped, and only major events reach the map. The status suite checks:

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

The browser test covers the stats panel, the Wars & treaties dialog, and the map mode colouring Donetsk as a front.

A live check on 2026-09-17 ran `./start.sh` (Qwen3.5-4B via `llama serve`), then a Portugal turn with "Declare war on Spain over the disputed Olivenza border and send troops to occupy Galicia" and "Sign a defence pact with Morocco":

- **Before the fix**, adjudication included the fronts of every seeded war, giving an 11.4k-token prompt that ran out of output tokens.
- **After the fix**, with fronts limited to wars of acting nations and an answer budget that scales with the number of actions, the prompt was 5.2k tokens and the turn took 181 s.
- **Outcome.** The model opened a low-intensity *Portuguese-Spanish Border War* (35 killed and 889 displaced along 12 border regions) and a *Portugal-Morocco Defence Pact*. The seeded wars reported their monthly tolls.
- **Weakness.** The small model tends to attach mild "growth" effects to routine events, including Spain's economy for being attacked; C++ caps keep this bounded.

The Python data suite verifies 258 unique feature IDs, France/Norway/Kosovo/Taiwan normalization, unresolved metadata handling, 239 actor records, 152,970 city records, separate country/city Wikidata fields, absence of synthetic national population totals and complete supplied flag coverage.

## Real HTTP and browser

`python3 tests/integration.py` starts actual Drogon servers against disposable temporary databases. A controlled loopback model server drives real HTTP timeouts and malformed responses; an unreachable endpoint, a timeout and a malformed response each fail the whole turn outright with no state change and a non-empty error. Concurrent duplicate requests return one job. Competing keys, stale requests, unknown targets and typed (non-text) actions fail without state changes. The suite checks free-text actions end-to-end through a model response, frozen reference revisions after cache replacement, save/restart equivalence, process-kill recovery before commit, and that the `--llama-endpoint=` CLI flag and the `POST /api/v1/model-config` web-UI route both take effect immediately and persist across a restart with no flag given.

The replay executable reconstructs three turns from persisted initial state and intentions, comparing every resulting state and authoritative event. It rejects a tampered replay. No inference is needed for replay.

Headless Chromium verifies country-list selection, a coordinate click on the map, zoom, writing action cards, draft persistence across reload, adding and removing map-region target chips, ending the month, polling, journal events, a region transfer and port construction shown on the map, page reload and resume. It also deliberately returns 404 for a flag to exercise the text fallback, selects Antarctica with no city records, and verifies persistence across server restart. External browser requests are blocked; none are attempted. No JavaScript runtime errors occur. The expected missing-flag 404s are fixture responses, not absent vendored files. The desktop screenshot was inspected. Screenshots remain in ignored `tests/artifacts/`.

## Wikipedia and model boundaries

A live call through the actual Drogon transport succeeded for Portugal: country code PT → Wikidata Q45 → English article Portugal, revision **1375265884**. The returned cache contains article URL, retrieval time, language, attribution, a bounded introduction and a separate bounded history excerpt. A shortened recorded API response is included with attribution under `tests/fixtures/`. Live source articles are not pinned to January 2026. Other countries and upstream outages are handled as unavailable; they were not exhaustively checked live.

Real-model behavior (Qwen3.5-4B via `llama serve`), memory use and turn latency are measured in [Evolving world](EVOLVING_WORLD.md#measured-model-results) and [Local inference](MODELS.md#context-size-and-memory). The automated suites use injected transport and a real loopback fixture HTTP server standing in for llama-server, and make no external network requests.

Not covered: full assistive-technology audit, other browser engines, large multi-year endurance runs, all live Wikimedia country mappings, production deployment or multiplayer. All source provenance limitations are documented in DATA.md.

## Environment note

Installing Drogon triggered a Homebrew automatic-update conflict in the existing `gromgit/fuse` tap. That task-started rebase was aborted and its original `main` branch and two local commits restored; its worktree was clean afterward. Drogon remained installed. No tap changes are part of this project.
