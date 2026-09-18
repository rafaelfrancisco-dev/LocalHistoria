# 12 · Split `main.cpp` into server, turn runner and reference preparer

**Status: done (2026-09-18).** `main.cpp` 114 lines; `TurnRunner` (plan/run) in `turn.cpp`, `ReferencePreparer` (relevance, preparation, refresh/prepare jobs) in `references.cpp`, `newCampaign` and reference loading in `campaign_factory.cpp`, one named handler per route in `server.cpp`, `Worker` in `src/worker.h`. `relevance` is shared, so it lives on `ReferencePreparer` and `TurnDeps` also carries the reference store and preparer; request-specific refs stay in the `/turns` handler. `turn.cpp` moved to `historia_services`. New `turn_tests` runs a month through `TurnRunner` with a faked model.

Do plan 11 first.

## Goal
`src/main.cpp` (`main()` spans ~600 lines after formatting) holds config parsing, the file lock, reference
loading, the reference preparation scheduler, 13 routes as lambdas, the new-campaign world builder and the
entire turn pipeline nested inside the `/api/v1/turns` route. Split it so each piece can be read and unit
tested. No behaviour change.

## Target layout
- `include/localhistoria/turn.h`, `src/turn.cpp`: `struct TurnPlan` (player orders, reactive actors,
  allocations, contexts, refs) and `class TurnRunner` with `TurnPlan plan(const WorldState&, const TurnOrders&)`
  (today `src/main.cpp:540–582`, needs `Store&`, reference `Store&`, `StatusSeed`), and
  `void run(const TurnPlan&, const std::string &jobId)` (today the worker lambda 584–689; chronicles,
  decisions, resolve, `adjudicationInput`, adjudicate, apply, commit, narrate). Move `adjudicationInput`,
  `mentioned`, `consequential`, `campaignDate`, `Mentions`, `containsWord`, `lower` here (or `world.cpp`
  where plans 04/06 already suggest). Dependencies passed in a small `TurnDeps` struct: `Store &store`,
  `LocalProvider &provider`, `const Json::Value &geography`, `const Adjacency &adjacency`, `std::mutex &mutex`.
- `include/localhistoria/references.h`, `src/references.cpp`: `class ReferencePreparer` wrapping
  `prepareReferences` (`src/main.cpp:318–345`), the `preparing` set, `retryAfter` map, and the
  `referenceWorker`. Methods: `void prepare(const WorldState&)` (called under mutex), `Json::Value
  coverage(...)`, `std::string refresh(code)` (the `/api/v1/history` POST body), `std::string prepareAll(...)`
  (`/api/v1/prepare`).
- `src/worker.h` (or in `references.h`): the `Worker` class (`src/main.cpp:22–67`).
- `src/campaign_factory.cpp`: `WorldState newCampaign(Store &reference, const StatusSeed&, const std::string
  &country)` (`src/main.cpp:422–445`).
- `src/server.cpp`: `route()` helper, `reply()`, request validation, one named function per route
  (`getCampaign`, `postCampaign`, `getFlag`, …), `void registerRoutes(App&)`.
- `src/main.cpp`: argument parsing, config, lock, `Store` construction, wiring, `app().run()`. ≤ 120 lines.

## Steps
1. Create the headers with the exact signatures above; move code without editing logic. Keep the single
   `std::mutex` semantics: it is passed by reference and locked exactly where it is today.
2. Add the new sources to `historia_services` in `CMakeLists.txt` (they need Drogon for `httpJson` only via
   `providers.h`; `turn.cpp` should not include Drogon: use `Transport`/`LocalProvider` only).
3. Write `tests/turn_tests.cpp` (add to CMake): construct `TurnRunner` with an in-memory `Store`, a
   `LocalProvider` with an injected transport (see `tests/core_tests.cpp:73–83` for the pattern), a
   two-actor world, and run one month; assert the job is `complete`, the state turn advanced, and the
   `inference` table has `decide`/`adjudicate`/`narrate` rows. This is the missing unit test for the pipeline.
4. `python3 tests/integration.py` and `tests/browser.mjs` must pass unchanged (they are the behavioural spec).
5. Update `docs/ARCHITECTURE.md` first paragraph to name the new files.

## Acceptance
`main.cpp` ≤ 120 lines; no route lambda longer than 40 lines; new `turn_tests` passes; all existing suites
pass; `git diff` shows moves, not rewrites (use `git diff --color-moved`).
