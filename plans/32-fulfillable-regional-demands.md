# 32 · Accepted regional demands must have fulfillable terms

**Status:** planned, 2026-09-24. **Priority:** P1 correctness.
**Audit coverage:** issue 3. **Dependencies:** plan 31 phase B for funded fulfillment; terms and
honest acceptance-state presentation can be implemented first without that dependency.

## Objective and evidence

Distinguish agreeing to a demand from delivering it. An audit probe accepted a demand whose text
was `sovereign_recognition`; the delta changed only its status and regional relations while the
event said sovereignty had been granted. No actor or territory change occurred.

## Read and change

- `src/world/compiler/identity_ops.cpp`, `compiler.cpp`, `territory_ops.cpp`, `proposals.cpp`.
- `src/world/compiler/operation_compiler.h`, `pending_store.h`, `src/world/delta_checks.cpp`.
- `src/world/schema.cpp`, `src/server/campaign_handlers.cpp`, `src/turn/plan.cpp`.
- `src/status/monthly/unrest.cpp`, `economy.cpp`, `src/core/lifecycle.cpp`.
- `web/app.js`, `web/index.html`, `tests/politics_tests.cpp`, `tests/consent_tests.cpp`,
  `tests/integration.py`, `tests/browser.mjs`, `tests/lifecycle_tests.cpp`.

## Design and implementation

1. Give new demands immutable, revisioned, bounded terms in addition to readable text. Support
   only outcomes the engine can represent: e.g. a funded regional project or a specific territorial
   settlement with explicit regions/beneficiaries. Do not add arbitrary executable instructions.
   Representation/autonomy requests without mechanics remain political requests, explicitly
   described as acceptance for negotiation, never as implemented constitutional change.
2. Regions may propose terms but never execute national powers. Granting validates current
   authority, ownership, third-party rights, affordability, existing conquest/secession limits and
   any required signatories using existing compiler rules. Occupation alone cannot authorize
   gifting another country's sovereignty. Multi-party terms require revision-bound consent.
3. Record `accepted` separately from `fulfilled`; allow pending, rejected, expired and an explicit
   failed/blocked fulfillment state. Preserve the current 3-month player/6-month AI response
   windows. Give accepted work a separate finite fulfillment deadline/progress rule, so it cannot
   remain a cost-free accepted promise forever. Specify handling of pauses and control changes.
4. Atomic immediate terms can fulfill in the acceptance month. Funded terms create/reference the
   deterministic projects from plan 31: IDs, target, funding/progress and completion conditions;
   fulfillment occurs only when those conditions hold. Share the existing allowance, rather than
   introducing free work. Repeated answers cannot execute or reward terms twice.
5. Apply a bounded initial relations response to acceptance; reserve any delivery benefit for
   actual fulfillment. Broken/expired promises have an explicit bounded consequence. A change of
   authority invalidates unanswered terms before accepting an answer from the new ruler; define
   transfer/pause/lapse rules for work already under way. Keep the one-step monthly relations
   limit and prevent a later regional initiative from erasing settlement consequences arbitrarily.
6. Extend structured API answers with the exact revision for new typed demands; reject stale or
   duplicate answers. Keep documented compatibility for legacy untyped demands. UI cards show
   the proposed change, cost/process and required consent before the player answers. Narration,
   compiler event text and context must say accepted, blocked, progressing or fulfilled accurately.

## Migration and replay

Persist new demand-rule versions through the shared lifecycle and frozen turn references.
Existing accepted demands stay historical political acceptance; do not retroactively create
states or projects. Legacy free-text demands must not be parsed as permission to change land:
acceptance is recorded honestly, and concrete terms require a new explicit proposal. Preserve old
event text and replay results. Store terms and fulfillment outcomes in authoritative state/deltas.

## Acceptance

- A typed, valid independence settlement creates the actor and transfers only the authorized land,
  or remains explicitly awaiting required signatures. Never report fulfillment without that delta.
- A legacy `sovereign_recognition` demand cannot silently trigger independence or claim it happened.
- A funded demand progresses within the common allowance, stalls when underfunded, and fulfills
  exactly once. Decline, expiry, duplicate answer, stale revision, controller change, lost authority,
  third-party ownership and failed operation rollback all have focused fixture coverage.
- Save/reload and replay preserve terms, consent, project links and fulfillment; migration is
  deterministic and failure-safe. State and event comparisons both pass.
- Add an actual server-created typed demand to HTTP/browser integration: inspect terms, answer,
  advance fulfillment and reload. DOM-only staging does not establish end-to-end behavior.
- Use at most a bounded number of records per region and no extra decision/adjudication call for
  acceptance or completion. Measure schema/context growth. Share live-model validation with plan 34.

Run CTest, integration and replay on disposable saves. Update `docs/API.md`,
`docs/EVOLVING_WORLD.md`, `README.md` and relevant model instructions. Keep the user's save untouched.
