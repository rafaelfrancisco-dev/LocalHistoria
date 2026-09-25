# Plan 32 implementation record

Plan 32 separates agreeing to a regional demand from delivering it. Demand version 1 (turn
references and `engine.demand_version`, requiring project version 1) gives each new demand
revision 1 and immutable terms. The kinds are `political`, `regional_project` or `independence`,
derived in C++ from a required `demand_kind`/`project_benefit` pair on `regional_initiative`.
Accepted demands record a `fulfillment` state: `political`, `blocked`, `in_progress`,
`fulfilled`, `failed` or `lapsed`. Rules and wire format: `docs/EVOLVING_WORLD.md` and
`docs/API.md`.

Design choices the plan left open:

- **Independence consent is the owner's alone, and it must hold the region.** Occupation (or a
  third party's ownership) blocks the grant with a reason rather than opening a new multi-party
  signature flow. A grant that would leave the ruler with no land is blocked. Execution reuses
  `create_actor` and `transfer_territory` inside a nested pending frame. A refusal from those
  rules, for example a second new state in one month, rolls back to `blocked`.
- **Blocked acceptances earn no relations step.** Only a fulfillable acceptance eases relations,
  so accepting the impossible is never a free gain.
- **Pauses and control changes.** Funded work already pauses under plan 31 when the target is
  lost. A promise lapses without blame if the owner changes, or if the region is occupied at the
  deadline (twice the work's months). It fails, one relations step back, when the ruler
  cancels the work or holds the region at the deadline without completing it.
- **Fulfilment is recognised as the month after completion opens.** The project completes in the
  monthly pass and the compiler records fulfilment from the committed state, so replay derives
  it from state and delta alone.
- **Legacy demands** (no terms) are answered without a revision and, under version 1, accepted
  for negotiation only. Nothing is inferred from their text, such as `sovereign_recognition`.
- **Regional initiatives move relations one step** under version 1, so a same-month initiative
  cannot erase a settlement's step.

Validation (2026-09-25, M1 Pro, RelWithDebInfo):

- CTest: 21/21 passed, including the new `demands` fixture suite and the `demand_replay`
  export (an old-rules month, then filing, acceptance, 17 funded months and fulfilment). The
  fixture covers the plan's acceptance list: independence creating the state and moving only its
  region; occupation and last-region blocks; the second-founding rollback; the legacy
  `sovereign_recognition` answer; funded progress, underfunding, completion and fulfilment
  exactly once; decline, expiry, duplicate, stale and revision-less answers; answering a settled
  demand; cancellation, a missed deadline, occupation and owner change; the same-month
  initiative; an AI `respond_regional` acceptance; save/reopen; and tampered terms or outcomes
  in a delta.
- HTTP integration: the mock files a typed `regional_project` demand whenever a Portuguese region
  gets the regional slot. The test waits up to four extra months for one; the scheduler seed
  depends on the random campaign ID. It then checks the terms from `/campaign`, rejects a stale
  revision and a revision-less answer (409), accepts at revision 1, and checks the project and
  the honest journal text. It rejects a second answer, runs another funded month, restarts the
  server and replays the export identically.
- Browser: decision cards show terms for a legacy and a typed demand, and `turnResponses` sends
  the revision for the typed one. That page state is staged; the end-to-end server-filed path is
  the HTTP step above.
- Size: the two schema fields add 244 bytes. A typed demand adds up to about 300 bytes of state:
  terms of about 100–190 bytes and fulfilment of about 110. Demands stay capped at eight per
  region, and accepted work under way is never evicted. Acceptance and fulfilment add no model
  call.

Live model (2026-09-25, Qwen3.5-4B Q4_K_M via `llama serve`, M1 Pro): an A/B with
`tools/living_world_probe.py`, 2 months per arm, Portugal, orders "Reform national education and
teacher training" then "Continue the national education reform". Arm A was this change; arm B
was the previous commit (4c9d654). Each arm created its own campaign, because the probe's
`--prepare-only` fixture was saved without its write-ahead log and has no campaign table (a
pre-existing tool bug). Allocations therefore differ between arms.

| Arm | Month | Commit | Adjudication prompt / output tokens | Latency | Schema |
|---|---|---|---|---|---|
| A (plan 32) | 1 | 90.7 s | 5,960 / 1,613 | 68.8 s | 20,954 B |
| A (plan 32) | 2 | 110.1 s | 8,706 / 1,783 | 84.7 s | 26,989 B |
| B (before) | 1 | 100.9 s | 6,840 / 1,721 | 75.7 s | 23,859 B |
| B (before) | 2 | 137.3 s | 10,242 / 2,010 | 96.0 s | 31,778 B |

- No echo in either arm: every event matched its month's intention, with no verbatim repeats of
  the previous month. Both arms replayed identically. Size differences follow each world's
  content, not the change.
- The model filled the required `demand_kind` and `project_benefit` on both regional
  initiatives, choosing `political` both times. That fits the requests: a garrison coordination
  and a border halt. One wrote `demand: "none"` to mean no demand. That filed a junk demand,
  so C++ now treats a demand spelled "none" as none, with a fixture test.
- Two samples cannot show that the model ever uses `regional_project` or `independence`
  unprompted, or rule out first-value bias. That still needs a longer played campaign that
  counts demand kinds.

Not verified: funded and independence demands from the live model, and anything longer than
two months. `saves/campaign.sqlite` was not opened or modified.
