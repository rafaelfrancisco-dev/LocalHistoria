# 30 · Prompt cancellation and exception-safe turn jobs

**Status:** planned, 2026-09-24. **Priority:** P1.
**Audit coverage:** issues 4 and 6. **Dependencies:** none; use plan 29's build when available.

## Objective and evidence

Cancelling a month should promptly release its active model request and turn worker. Every
accepted job must also reach a terminal state even when preparation fails before queueing.

`LocalProvider::call` checks cancellation before and after a blocking transport call;
`httpJson` waits in `sendRequest`. A cancellation does not interrupt that wait. In the turn
HTTP handler, `Store::submit` creates a running job before `TurnRunner::plan`, but exception
cleanup only surrounds worker insertion. An exception during planning can strand the job.
The latter is a source-confirmed failure path, not a failure reproduced during the audit.

## Read and change

- `include/localhistoria/providers.h`, `src/inference/transport.cpp`.
- `include/localhistoria/turn.h`, `src/turn/run.cpp`, `src/turn/plan.cpp`, `src/turn/worker.h`.
- `src/server/campaign_handlers.cpp`, `src/store/store.cpp`, `src/server/routes.cpp`.
- `tests/turn_tests.cpp`, `tests/core/`, `tests/integration.py`, `tests/browser.mjs`, `web/app.js`.
- `docs/API.md`, `docs/ARCHITECTURE.md`, `docs/MODELS.md`.

## Implementation

1. Give transport an explicit request-scoped cancellation/deadline contract, including the
   injectable test transport. Use supported Drogon/Trantor request-lifetime and connection-close
   facilities after checking the installed APIs. An asynchronous callback behind another blocking
   wait is insufficient unless cancellation releases both the waiter and underlying request.
2. Resolve response, timeout, cancellation and shutdown races exactly once. Late callbacks must
   not reference destroyed objects or overwrite terminal status. No detached request thread,
   busy loop, process-wide model kill, or cross-request connection cancellation.
3. Keep provider serialization, but release it promptly after cancellation. Apply cancellation
   to properties/tokenizer calls and optional chronicles as well as model completion. Prevent
   retries once cancelled and retain the attempt's timing/status audit.
4. Put an exception guard around every step after job insertion, including planning, frozen
   reference preparation and queue insertion. Transfer ownership of terminal-state responsibility
   to the worker only after queueing succeeds. Preserve original errors and idempotency behavior;
   cleanup errors must be logged without hiding the first failure. Process-crash recovery remains
   necessary when storage itself is unavailable.
5. Keep the existing commit/cancel ordering under the shared mutex: an acknowledged pre-commit
   cancellation cannot publish state. A completed month stays complete; optional narration
   failure/cancellation cannot roll it back. Do not enlarge public cancellation semantics without
   corresponding API/UI changes.

## Verification and acceptance

- A controllable mock must signal that completion has started, then remain blocked. Cancel
   **after** that signal. Proposed target: terminal cancellation and worker availability within
   1 second locally, independent of the mock's much longer timeout. Use synchronization, not a
   sleep that might only test cancellation before the request starts.
- Verify unchanged campaign state, a new successful submission without restart, no retry after
   cancellation, and no late commit. Cover cancel/response and cancel/commit races, queued jobs,
   deadline expiry, narration and shutdown with small deterministic tests.
- Inject preparation failure after `submit`, plus queue rejection. Verify terminal failed job,
   useful error, repeat-key behavior, new-key recovery and no state advance. No production HTTP
   test-only fault switch; inject at the service/test boundary.
- Run CTest and HTTP/browser integration with disposable saves; replay committed months.
- Separately verify real llama-server cancellation: observe server slot activity or server logs
   showing generation stopped after disconnect, then submit another request. A fast client return
   alone does not prove GPU work stopped. Record model/server build and platform; if unsupported,
   document that remaining limitation and keep this acceptance item open.

## Boundaries

No gameplay or replay-rule change; retain existing API job states where possible. Never stop an
unrelated model process or touch the user's campaign. No per-request CPU polling overhead.
Do not claim completion if job cleanup passes but active generation still cannot be cancelled.
