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
| 20 | release-build-and-docs (last) | build + docs | S | |
| 21 | realistic-military-inventories (after 19) | data + simulation | M | done 2026-09-21 |
| 22 | coastal-regions-and-stability-mapping (after 21) | data + simulation | S | done 2026-09-21 |
| 23 | meaningful-initiative-slots (after 17, 18) | simulation | M | done 2026-09-22 |
| 24 | war-momentum-and-peace (after 19, 21, 23) | simulation | L | done 2026-09-22 |
| 25 | political-systems-and-unrest (after 22, 23, 24) | data + simulation + UI | L | done 2026-09-22 |
| 26 | [simulation-realism-and-consistency](26-simulation-realism-and-consistency.md) (after 19, 21–25) | simulation + correctness + replay + UI | XL, phased | done 2026-09-23 |

Baseline numbers to beat (real save, India, Qwen3.5-4B, M1 Pro): month 1 159 s, month 2 212 s;
adjudication 100 s / 123 s with 6,047 / 12,420 prompt tokens and ~2,650 generated tokens; schema 99 / 194 KB.
