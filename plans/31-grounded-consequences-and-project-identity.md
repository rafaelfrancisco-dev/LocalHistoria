# 31 · Grounded consequences and distinct domestic projects

**Status:** planned, 2026-09-24. **Priority:** P1 for phase A; P2 for project depth.
**Audit coverage:** issues 2 and 8. **Dependencies:** none; shared foundation for plan 32.

## Objective and evidence

An action should matter because of its distinct outcome, not because it occupies another card.
Keep free-text play and consequential events, while preventing repeated statements from farming
national statistics and giving different reforms/investments separate progress.

A deterministic audit probe compiled eight identical policy outcomes into eight `slight_growth`
stability effects: +4 in the monthly effect accumulator before later stability rules. The
per-month cap permits this. Separately, efforts are keyed only by `reform` or `investment`, so
unrelated projects of the same category share progress and generic completion benefits.

## Read and change

- `src/world/compiler/events.cpp`, `operation_compiler.h`, `pending_store.h`, `compiler.cpp`.
- `src/status/monthly/economy.cpp`, `monthly_status.cpp`, `src/status/status_internal.h`.
- `src/world/schema.cpp`, `src/world/delta_checks.cpp`, `src/inference/prompts.cpp`.
- `src/core/lifecycle.cpp`, `include/localhistoria/lifecycle.h`, `src/core/simulation.cpp`.
- `src/turn/adjudication_input.cpp`, `web/app.js`, `tools/replay_verify.cpp`.
- `tests/world/events.cpp`, `tests/status/economy.cpp`, `tests/lifecycle_tests.cpp`.

## Phase A — Bound repeated consequences

1. Define a versioned consequence identity from actor, bounded outcome kind, relevant target and
   validated project/operation identity. C++ owns IDs. Canonicalized text catches exact repeats;
   it is not sufficient to claim semantic equivalence of arbitrary prose. For unstructured
   policies/talks, add a conservative per-actor/per-kind monthly envelope and cross-month
   diminishing returns so trivial paraphrases cannot multiply rewards.
2. Apply the rule symmetrically to player, reactive and independent actions. Positive and negative
   duplicate consequences must not multiply merely through repeated descriptions. Keep genuinely
   distinct validated operations, casualties and separately funded projects distinct.
3. Preserve events as meaningful records: a repeat can continue a programme, maintain an existing
   stance or record an unsuccessful attempt without granting another national-stat increment.
   Record a compact reason such as continuation, duplicate or diminished consequence. Do not
   revert to blanking the action or requiring every duplicate to move an economy/stability axis.
4. Bound the persisted recent-consequence ledger by documented expiry and capacity. No embedding,
   semantic-model call or scan of the complete journal for deduplication. Explain residual
   paraphrase ambiguity; do not invent a perfect text-equivalence detector.

## Phase B — Separate project identity from category

1. Introduce deterministic project IDs and a bounded record: category, short objective, target,
   start turn, funding/progress, completion condition, supported benefit and originating event.
   Category determines eligible rules, not unique identity. Extend schema minimally, referring to
   existing project IDs for continuation; validate actor/target ownership and allowed fields.
2. Two different reforms remain distinct and compete under the existing implementation allowance;
   repeating the same project continues it. A completion benefit applies once. Renaming, cancelling,
   restarting or paraphrasing a programme must not reset its reward history or refund spent money.
3. Preserve existing funding priorities and bounded procurement/construction rules. Define a small
   supported benefit vocabulary tied to actual simulator variables; unsupported objectives must
   not be narrated as achieved. Keep project count and prompt summaries bounded.
4. Display objective, progress, funding delay and actual completion in the inspector/journal, and
   feed the same authoritative records to subsequent model contexts.

## Phase C — Migration and replay

Introduce explicit persisted rule versions for consequence/project semantics through the shared
lifecycle selector; do not silently reuse the current event-text version. Record selected rules
in turn references and engine state. Old turns replay unchanged. On the first new-rules month,
migrate each category-keyed effort once to a deterministic legacy project, preserving paid work,
remaining duration and old completion benefit. Never infer its missing objective from prose.
Use the same migration in live turns and replay, transactionally; failed turns cannot publish it.

## Acceptance

- One versus eight identical policies receives the same aggregate consequence; paraphrases cannot
  exceed the bounded envelope. Repeating across months does not farm unbounded confidence.
- Independent funded actions still earn their own supported progress; repeated negative stories
  do not multiply penalties; failed operations do not gain rewards through reclassification.
- Two differently targeted reforms progress independently; one repeated reform progresses once;
  over-budget projects stall deterministically; completion and restart cannot duplicate benefits.
- Test old-save migration, save/reopen, failure/retry and mixed old/new replay, comparing events
  as well as state. Use small fixture worlds, not extra long real-world CTest loops.
- Run CTest and HTTP/browser integration with one existing flow extended to inspect project
  progress. Reuse plan 34's live scenario checks for model attribution; mocks alone do not prove it.
- Measure compiler/monthly costs before/after with fixed orders. Proposed gate: no >10% reproducible
  CPU regression on representative fixtures, bounded ledger/project memory, and no extra model
  calls. Report schema/token changes separately rather than assuming they are free.

Update `README.md`, `docs/EVOLVING_WORLD.md`, `docs/API.md` and `docs/MODELS.md`. Never touch
`saves/campaign.sqlite`; preserve all historical replay fixtures. Phase A can ship independently
with its own recorded rule version before phase B is complete.
