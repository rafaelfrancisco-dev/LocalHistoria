# Plans

One file per issue from `docs/AUDIT.md`. Each plan is self-contained: read it, then the files it names, and
implement it without reading the other plans. Conventions for every plan:

- Build and test: `make test` (C++ suites + data test). Full check: `python3 tests/integration.py`
  (needs `npm ci && npx playwright install chromium` once). Never touch `saves/campaign.sqlite`.
- Do not reformat code you are not changing (plan 11 does the one-time reformat). Match the surrounding style.
- Keep replay determinism: anything that changes saved state must be derived from the state, the recorded
  delta and adjacency, never from the clock or the model at replay time. `./build/replay_verify` on an export
  from the integration test must still pass.
- Update the docs paragraph that describes the behaviour you change (README, docs/EVOLVING_WORLD.md,
  docs/MODELS.md, docs/API.md). The docs are specific; keep them accurate.
- One commit per plan, message ends with `Co-Authored-By: Claude <noreply@anthropic.com>` style line as in git log.

Recommended order (dependencies in parentheses):

| # | Plan | Area | Size | Status |
|---|---|---|---|---|
| 01 | authority-checks | correctness | S | |
| 02 | war-toll-calibration | correctness | S | |
| 03 | partial-failed-events | correctness + UI | M | |
| 04 | turn-output-diet | performance | S | done 2026-09-18 |
| 05 | adjudication-prompt-and-schema | performance | M | |
| 06 | chronicles | performance + correctness | S | |
| 07 | invasion-guidance-and-probe | model quality | S | |
| 08 | map-geometry | UI performance | M | |
| 09 | ui-cleanups | UI | M | |
| 10 | turn-cancel-and-deadline | UI + server | M | |
| 11 | clang-format | code | S (do alone, separate commit) | |
| 12 | split-main (after 11) | code | L | |
| 13 | operation-compiler (after 11, 01, 03) | code | L | |
| 14 | typed-keys-and-small-fixes (after 11) | code | M | |
| 15 | parallel-slots (after 04, 05; same 4B model, no 9B) | performance | M | |
| 16 | windows-followups | portability | S | |
| 17 | independent-slot-relevance | simulation | S | |
| 18 | effect-overrides (after 04) | simulation | S | done 2026-09-18 |
| 19 | military-inventory-and-battle-model (after 01, 02; supersedes 02's military deaths) | simulation + data | L | |
| 20 | release-build-and-docs (last) | build + docs | S | |

Baseline numbers to beat (real save, India, Qwen3.5-4B, M1 Pro): month 1 159 s, month 2 212 s;
adjudication 100 s / 123 s with 6,047 / 12,420 prompt tokens and ~2,650 generated tokens; schema 99 / 194 KB.
