# 33 · Bounded history reads for long campaigns

**Status:** implemented 2026-09-25 (measurements in docs/VERIFICATION.md). Planned 2026-09-24. **Priority:** P2, performance scaling.
**Audit coverage:** issue 7. **Dependencies:** none; benchmark with plan 29's optimized build.

## Objective and evidence

Keep browsing and context preparation responsive after years of play. `Store::eventLog` caches
all months by campaign/turn; the first read after every new turn rebuilds them. `eventLogMonths`
reads all turn events and issues an additional event query per month. A small requested page does
not limit this work. HTTP handlers use the shared store mutex. This is a confirmed scaling path;
the audit did not measure a long-campaign latency curve.

## Read and change

- `include/localhistoria/store.h`, `src/store/event_log.cpp`, `src/store/store.cpp`.
- `src/server/world_handlers.cpp`, `campaign_handlers.cpp`, `routes.cpp`.
- `src/turn/plan.cpp`, `src/core/simulation.cpp`, `web/app.js` event paging and map events.
- `tests/core/history_tests.cpp`, `tests/integration.py`, `tests/browser.mjs`.
- `docs/API.md`, `docs/ARCHITECTURE.md`, `docs/VERIFICATION.md`.

## Implementation

1. Measure first-page, filtered-page, map-event and context queries on disposable 12-, 120- and
   600-month fixtures. Separate cold open, first read after commit and repeated reads. Record
   query counts, decoded JSON bytes, mutex hold duration, latency and memory; use realistic event
   density and links, not an empty history.
2. Introduce an indexed, rebuildable projection of immutable event summaries, including engine
   events such as casualties, construction and front advances. Build it within each commit
   transaction. Use stable IDs and indexes aligned with month, actor/region links and filters;
   inspect query plans before adding speculative indexes.
3. Preserve whole-month pagination, newest-first ordering, search and filter combinations. Apply
   filters before deciding whether a page has more results. Avoid fetching every document before
   applying the limit. If using FTS, verify available SQLite support and preserve documented search
   semantics; otherwise retain a correct bounded-memory search with its scan cost reported.
4. Separate historical facts from current visibility. Event expiry, a later causal citation, open
   conflicts, current actor scope and entity movement/destruction affect today's map. Preserve the
   existing intended semantics explicitly; update indexed dependencies or compute visibility for
   candidates without reconstructing the entire journal on each month. Cache invalidation must
   account for narration updates and campaign replacement where relevant.
5. Move expensive JSON assembly outside the shared lock using a coherent immutable snapshot where
   feasible. Do not share one SQLite connection concurrently without synchronization. Keep each
   response internally consistent with one campaign/turn, including during commit/replacement.
6. Use `Store::migrate()` for schema changes. Old-save backfill must be resumable/idempotent and
   bounded in batches, with clear readiness/fallback behavior. Never rewrite authoritative events
   to populate a derived index. Keep replay exports and source turn records intact.

## Verification and acceptance

- Compare old/new outputs on fixtures covering filtered empty months, more-than-limit months,
  ties, search, actor/region changes, causal renewal, ended wars, destroyed entities, replacement
  and reload. No skipped/duplicated events across pages.
- Failed commits cannot leave index rows; interrupted migration resumes correctly. Rebuilt and
  incremental projections return equivalent results. Replay output remains unchanged.
- Run CTest and HTTP/browser integration with disposable saves; do not add 600-month simulation
  loops to routine unit suites. Generate performance fixtures offline once from valid records.
- Proposed gates after backfill: common first-page reads decode only candidate/page records;
  queries do not grow by one per historical month; bounded-memory paging; 600-month common-page
  p95 below 200 ms and no more than 2x the 12-month fixture on the same machine. Use at least 30
  repetitions for p95. Report map-wide/search worst cases separately and explain remaining scans.
- Target shared-lock holds below 50 ms for steady-state common reads. These are proposed targets,
  not current measurements; preserve correctness if tuning requires further work and report any
  missed gate rather than claiming a finished performance fix.

Keep inference call count unchanged. Update API cursor documentation if necessary, with client
compatibility, and record the measured scaling curve. Never touch `saves/campaign.sqlite`.
