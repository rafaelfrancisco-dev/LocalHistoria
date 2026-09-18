# 03 · Partial and failed events: visible to players and truthful to later prompts

**Status: done (2026-09-18).** Events carry `partial`/`failed`; `droppedClause` appends "(In fact: …)" to related history, regional history, recent events (`prompt_text`) and chronicle inputs; Events badges and journal prefixes; `unreachable-fixture` in the integration/browser tests. Step 6 (model rewrite) skipped: the deterministic clause covers prompts without an extra call.

## Goal
When C++ drops one or all operations of an event, the player sees it, and later prompts read what actually
happened rather than the model's original claim. Reasons stay out of player-facing text (that decision from
commit `05c93dd` stands); only a short factual clause and a badge are added.

## Evidence
- Save, month 1: India "Invade pakistan" → model emitted `open_conflict` + `move_entity` (a Pakistani city).
  `move_entity` was dropped (`dropped_operations: [{"action":"move_entity","reason":"only its owner can move
  Gujranwala Division"}]`). Event text still says "…establishing forward operating bases in the Punjab region".
  Month 2: Pakistan's decision refers to "the occupation of Punjab" that never happened.
- `src/world.cpp:494–507`: dropped operations are recorded in `dropped` and attached at line 529 as
  `event["dropped_operations"]`; text unchanged. Whole-event rollback sets `blocked_by = "infeasible"`.
- Prompt inputs that read event text: `Store::campaignContext` (`src/store.cpp:168–178`, `related_history`),
  `Store::chronicleEvents` (`src/store.cpp:193–209`), `context()` recent events (`src/simulation.cpp:203–207`,
  reads `journal()` events text), regional history query in `src/main.cpp:575`.
- UI: `eventCard()` in `web/app.js:864–897` shows badges for `breaches` and `blocked_by` only. Journal lines
  at `web/app.js:308–315`.

## Steps
1. `compileOperations` (`src/world.cpp:529`): after `dropped` is known, set
   `event["partial"] = !dropped.empty() && blocked == "none"` and
   `event["failed"] = blocked == "none" && !seenOps.empty() && dropped.size() == seenOps.size()`
   (all attempted operations dropped). Store both only when true (keep documents small).
2. Deterministic clause. Add `std::string droppedClause(const Json::Value &event)` in `src/world.cpp`
   (declare in `world.h`): maps each `dropped_operations[].action` to a phrase, deduplicated:
   `occupy`/`transfer_territory`/`restore_control` → "no territory changed hands", `move_entity` → "no
   forces moved", `build` → "no construction began", `open_conflict` → "no war began", `add_commitment` →
   "no agreement was concluded", `resolve_*` → "nothing was ended", `create_actor` → "no new state formed",
   `set_entity_status` → "no unit was affected", `change_identity` → "no identity change took effect",
   regional ops → "no regional change was recorded". Result: " (In fact: no forces moved.)".
3. Apply the clause where prompts read text, not in the saved text:
   - `Store::campaignContext` after parsing each `related_history` row: if `partial`/`failed`, append the
     clause to `text`.
   - `Store::chronicleEvents`: same, on `description`.
   - `Store::journal()` is also used by `context()` for `recent_events`: append there too (it feeds prompts
     only; the UI uses `structured_events`/`events` from the same call, so add the clause under a separate key
     `prompt_text` in `journal()` and make `context()` (`src/simulation.cpp:206`) prefer `prompt_text`).
   - Regional history query in `src/main.cpp:575`: append after `parse(...)`.
   - `applyDelta` `ResolvedEvent.text` (`src/world.cpp:128`) stays the model text (it is the player-facing
     journal line).
4. Event log API (`Store::eventLog`, `src/store.cpp:261`): add `"partial"`, `"failed"` to the list of copied
   keys so `/api/v1/events` exposes them. Document in `docs/API.md` event fields.
5. UI: in `eventCard()` add badges `["did not happen", "blocked"]` when `ev.failed` and
   `["partly happened", "blocked"]` when `ev.partial` (reuse the `.badge.blocked` style). In the journal
   (`web/app.js:308–315`) prefix the line with "Failed · " / "Partial · " using `structured?.failed` /
   `structured?.partial`. Update `tests/browser.mjs` if the mock produces a dropped operation (the
   `transfer-and-build-fixture` builds then cedes the region; both succeed, so add a fixture in
   `tests/integration.py` Mock: if `'unreachable-fixture' in i['action']` emit `occupy` of a far region, then
   assert in the browser test that the card shows "did not happen").
6. Optional (only if cheap): a rewrite call for failed events. Skip unless plan 04 has landed and a month is
   under 100 s; if done, add `LocalProvider::rewrite(description, droppedActions)` with a 200-char schema,
   no thinking, temperature 0.3, and use its result as `text` only when it parses.

## Tests
- `tests/world_tests.cpp`: `far` test at line 74–75 (`Deep strike`): assert `events[0]["partial"] == true`,
  `!isMember("failed")`; `annexNow` (line 81–82): `failed == true`; `reform` (line 107): neither key.
  `droppedClause` on the `far` event contains "no territory changed hands".
- `tests/world_tests.cpp` store section (lines 161–177): after committing a delta with a partial event,
  `campaignContext(...).facts["related_history"][0]["text"]` ends with the clause.
- Integration + browser as in step 5.

## Acceptance
`make test` and `python3 tests/integration.py` pass; `docs/API.md` and `docs/EVOLVING_WORLD.md` ("An operation
that breaks a limit is dropped…") describe the flags and the prompt clause.
