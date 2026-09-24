# Plans

One file per planned change. Each plan is self-contained: read it, then the files it names, and
implement it without reading the other plans. Conventions for every plan:

- Build and test: `make test` (C++ suites + data test). Full check: `python3 tests/integration.py`
  (needs `npm ci && npx playwright install chromium` once). Never touch `saves/campaign.sqlite`.
- Do not reformat code you are not changing (`make format` keeps the tree formatted). Match the surrounding style.
- Keep replay determinism: anything that changes saved state must be derived from the state, the recorded
  delta and adjacency, never from the clock or the model at replay time. `./build/replay_verify` on an export
  from the integration test must still pass.
- Update the docs paragraph that describes the behaviour you change (README, docs/EVOLVING_WORLD.md,
  docs/MODELS.md, docs/API.md). The docs are specific; keep them accurate.
- One commit per plan, message ends with `Co-Authored-By: Claude <noreply@anthropic.com>` style line as in git log.

Plans 01–14 (authority checks through typed keys, all done 2026-09-18) were deleted once implemented; the
code, tests and docs describe them, and `git log` has the originals. Dependencies in parentheses:

| # | Plan | Area | Size | Status |
|---|---|---|---|---|
| 15 | parallel-slots (same 4B model, no 9B) | performance | M | measured 2026-09-18, not adopted (no gain) |
| 16 | windows-followups | portability | S | |
| 17 | independent-slot-relevance | simulation | S | done 2026-09-18 |
| 18 | effect-overrides | simulation | S | done 2026-09-18 |
| 19 | military-inventory-and-battle-model (supersedes the old war-toll military deaths) | simulation + data | L | done 2026-09-18 |
| 20 | release-build-and-docs | build + docs | S | superseded by 29 |
| 21 | realistic-military-inventories (after 19) | data + simulation | M | done 2026-09-21 |
| 22 | coastal-regions-and-stability-mapping (after 21) | data + simulation | S | done 2026-09-21 |
| 23 | meaningful-initiative-slots (after 17, 18) | simulation | M | done 2026-09-22 |
| 24 | war-momentum-and-peace (after 19, 21, 23) | simulation | L | done 2026-09-22 |
| 25 | political-systems-and-unrest (after 22, 23, 24) | data + simulation + UI | L | done 2026-09-22 |
| 26 | [simulation-realism-and-consistency](26-simulation-realism-and-consistency.md) (after 19, 21–25) | simulation + correctness + replay + UI | XL, phased | done 2026-09-23 |
| 27 | [implementation record](../docs/PLAN_27.md) | inference correctness + measurement | L | implemented; see record for evidence |
| 28 | [consequential-events-and-occupation](28-consequential-events-and-occupation.md) (after 26, 27) | simulation + schema + UI | L, phased | implemented 2026-09-24 |
| 29 | [optimized-play-builds](29-optimized-play-builds.md) | performance + build | S | build changes implemented; performance/Windows gates pending |
| 30 | [cancellable-inference-and-job-recovery](30-cancellable-inference-and-job-recovery.md) | responsiveness + reliability | M | planned; P1 |
| 31 | [grounded-consequences-and-project-identity](31-grounded-consequences-and-project-identity.md) | simulation + replay | L, phased | planned; P1 rewards, P2 project depth |
| 32 | [fulfillable-regional-demands](32-fulfillable-regional-demands.md) (funded fulfillment after 31B) | simulation + UI + consent | L, phased | planned; P1 |
| 33 | [bounded-history-queries](33-bounded-history-queries.md) | performance + storage | M | planned; P2 |
| 34 | [faster-reliable-adjudication](34-faster-reliable-adjudication.md) (final acceptance after 31–32) | inference performance + quality | L, measured | planned; P1 |

## September 24 audit follow-up

Plans 29–34 cover all eight audit findings. Suggested delivery order: 29, 30, 31A, then 31B and
32; plan 33 is independent. Start plan 34's baseline/experiments early because inference is the
largest documented latency cost; perform its final adoption gate against the completed schema
and gameplay changes. Early honest demand-acceptance wording/terms in plan 32 need not wait for
funded projects. Dependencies describe work ordering, not a requirement to use parallel agents.

| Audit issue | Owning plan |
|---|---|
| 1. Adjudication cost and operation reliability | 34 |
| 2. Repeated-action rewards | 31A |
| 3. Demands granted without fulfillment | 32 |
| 4. Cancellation does not interrupt inference | 30 |
| 5. Normal macOS play builds Debug | 29 |
| 6. Preparation errors can strand running jobs | 30 |
| 7. History rebuild grows with campaign length | 33 |
| 8. Different reforms/investments share one effort | 31B |

The audit built the current checkout, passed all 17 CTest checks (47.19 seconds), HTTP/browser
integration and restart/replay checks, and reproduced issues 2 and 3 with deterministic probes.
It did not remeasure live inference speed or long-campaign scaling. Timing gates in new plans
are proposed acceptance targets, not established results. Keep all test saves disposable and
record unrun live-model/platform checks explicitly. Plans 31–32 must version behavioral changes
and preserve legacy state **and event** replay. No implementation is included in these plans.

Historical baseline (real save, India, Qwen3.5-4B, M1 Pro; refresh before comparing): month 1 159 s, month 2 212 s;
adjudication 100 s / 123 s with 6,047 / 12,420 prompt tokens and ~2,650 generated tokens; schema 99 / 194 KB.
