# 10 · Cancel a running month, and bound its total time

## Goal
A stuck or slow month can be cancelled from the UI, and a month never runs longer than a configured total
budget. Today each model call has its own `timeout_seconds` (240 s) and a month makes up to ~11 calls, so the
worst case is tens of minutes; the client gives up polling after ~10 minutes (`web/app.js:375`) with no way to
stop the job.

## Code map
- Turn worker lambda: `src/main.cpp:584–689`; model calls at 617 (chronicle), 639 (decide), 646 (decide),
  656 (adjudicate), 675 (narrate). Commit at 668. Failure path at 680–688 sets `jobs.status='failed'`.
- `Worker` class: `src/main.cpp:22–67`. Provider: `LocalProvider::completion` (`src/providers.cpp:39–115`)
  calls `transport` four times per inference.
- Jobs table: `src/store.cpp:21–29`; `/api/v1/jobs` route `src/main.cpp:502–508`.
- Client: `poll()` `web/app.js:362–391`; `setBusy` 331–335; End month handler 503–525.

## Steps
1. Cancellation flag. Add to `LocalProvider` an `std::atomic<bool> cancelRequested` with
   `requestCancel()` / `clearCancel()`; check it at the start of `completion()` and between the transport
   calls (throw `ValidationError("Cancelled")`). Also give the Transport a way to abort a request in flight:
   `httpJson` uses `client->sendRequest(req, timeout)` synchronously; keep it, but reduce damage by passing
   the *remaining month budget* as the timeout (step 2) so one hung call cannot exceed it.
2. Month deadline. Config key `turn_budget_seconds` (default `2 × timeout_seconds`, max 1800) parsed next to
   `timeout_seconds` (`src/main.cpp:266–268`). In the worker, record `started`; before each model call compute
   `remaining = budget - elapsed`; if `remaining <= 0` throw `ValidationError("Month exceeded its time
   budget")`; pass `min(timeout, remaining)` to the provider (add an optional `deadline` parameter to
   `decide/chronicle/adjudicate/narrate`, or a provider-level `setCallTimeout` guarded by the `settings` mutex).
3. Route `POST /api/v1/turns/cancel` with body `{job: "<id>"}` (add to the field list at line 378):
   under `mutex`, if the job is `running`, set `cancel_requested` in a small in-memory set keyed by job id,
   call `provider.requestCancel()`, reply 202. The worker, on catching the cancellation before commit, marks
   the job `failed` with error "Cancelled by the player" (no state change, same as any failure) and clears
   the flag. After commit, cancellation only skips narration.
4. Client: while busy, show a "Cancel" button next to the status line; on click `POST turns/cancel`; `poll()`
   then sees `failed` and shows the error. Show elapsed seconds and the current phase in the status line
   (derive the phase from the last `/inference` record kind: "Deciding for <actor>", "Adjudicating",
   "Writing the journal"; count decisions as "3/6" using `allocations.length + reactive count` if exposed;
   or simply show the kind).
5. Recovery: `Store::recover()` already marks running jobs interrupted on restart. Nothing to add.
6. Docs: `docs/API.md` (new route, `turn_budget_seconds`), README config paragraph, `config/local-model.json`.

## Tests
- `tests/integration.py`: with `Mock.mode='slow'` (1.5 s per call) and `timeout_seconds=5`, submit a turn,
  immediately `POST turns/cancel`, poll → `failed`, error non-empty, `state` unchanged. Also set
  `turn_budget_seconds: 3` in a server config and check a slow month ends `failed` with the budget message.
- `tests/browser.mjs`: optional; the Cancel button exists while busy (`#cancel-turn` visible after clicking
  End month with a slow mock is racy; skip if flaky).

## Acceptance
Cancel works before commit with no state change; a month can never exceed the budget by more than one call's
remaining time; docs updated.
