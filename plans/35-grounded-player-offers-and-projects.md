# 35 · Grounded player offers and reform projects

**Status:** implemented 2026-09-25. Reforms are adopted. Offer narrowing is the opt-in `offers` experiment after a failed live A/B ([record](../docs/PLAN_35.md)). **Priority:** P1 reliability. **Dependencies:** plan 34 (sector key,
invented-id resolution, category from sector). **Audit coverage:** plan 34's open gaps.

## Objective and evidence

A clear treaty offer in a player order must produce an `add_commitment` proposal with coherent
kind and clauses. A clear domestic reform must produce a project.

- Offers:
  - The `access_ceasefire` fixture produced no access offer in either the baseline or the grounding arm (`docs/PLAN_34.md` grounding screen).
  - The one `operations` run recorded a `peace_treaty` granting Spain access, and needed a truncation retry.
- Projects:
  - The baseline peace scenario missed with a null project (`docs/PLAN_34.md` final screen).
  - The schema requires `project` but puts `null` first (`src/world/schema.cpp:348-357`). The prompt begins "set project=null except…" (`prompts.cpp:351`).
  - `prepareProject` rejects a null project as "unsupported or unspecified project" (`src/world/compiler/domestic.cpp:24-27`), and no test covers that branch.
  - `kind` is generated before `terms` (keys are generated alphabetically), so the model picks the kind before it has drafted any clause.

## Read and change

- `src/inference/prompts.cpp`: `adjudicate` narrowing loop, `clearlyDomestic`, response post-processing (the `effects` / `none` renames).
- `src/world/schema.cpp`: `add_commitment` / `amend_commitment` defs, `domestic_project`, per-intention `prefixItems`.
- `src/world/compiler/domestic.cpp` (`prepareProject` gate), `treaty_ops.cpp` (`opOpen`), `treaty_terms.cpp`.
- `src/world/geometry.cpp`: `consequential`. `src/turn/adjudication_input.cpp`: `mentioned`.
- `src/core/lifecycle.cpp`: engine version keys.
- Tests: `tests/context_tests.cpp`, `domestic_tests.cpp`, `consent_tests.cpp`, `access_tests.cpp`.
- Docs: `docs/MODELS.md`, `docs/EVOLVING_WORLD.md`.

## Design and implementation

**A. Offer intentions**

1. Add `treatyOffer(w, actor, text)` next to `clearlyDomestic`. It returns the counterparty actor
   ids. It fires only when all of these hold:
   - an offer verb (offer, propose);
   - a treaty noun (treaty, ceasefire, armistice, pact, alliance, agreement, accord, access);
   - at least one foreign actor named or targeted, found with `mentioned()`.

   Otherwise it returns an empty list. Ambiguous text is never narrowed.
2. For a matching intention `n`, in the narrowing loop of `adjudicate`:
   - Set `operations.prefixItems[0]` to a copy of `op_add_commitment`, with `actors.items`
     narrowed to `{actor} ∪ counterparties` and `grant_access.to` narrowed to those parties.
   - Keep the existing `items` so a second operation (such as withdraw) is still possible.
   - Set `minItems: 1`.

   Only offer outcomes pay the extra tokens. Plan 34's `operations` arm forced every outcome and
   was 17% slower.
3. **Clauses before kind.** In the schema, rename the `terms` key to `clauses` for
   `add_commitment` and `amend_commitment`, so the model writes clauses before choosing `kind`.
   The provider renames `clauses` back to `terms` before compiling, following the
   `effects`/`sector` precedent. A request carrying both keys is rejected. Update the prompt
   glossary word, but add no new sentences.
4. **Kind must match the clauses.**
   - Case: `ceasefire`, `peace_treaty` or `non_aggression_pact` whose clauses are only `grant_access`.
   - Why it's wrong: these kinds pause fighting (`status/conflicts.cpp:69-73`) and the terms don't say that.
   - Fix: `opOpen` relabels such a proposal `other` and adds a line to the event text.

   Gate this behind a new engine version key, so legacy replays compile unchanged.

**B. Domestic intentions**

5. For an intention where `clearlyDomestic` is true, replace that item's `project` `anyOf` with
   the non-null `domestic_project` ref. Also narrow `project.id` to `""` plus that actor's
   existing project ids, which removes invented ids at the source; the plan 34 resolution stays
   for legacy deltas. In `schema` experiment mode, emit the narrowed def separately from the
   shared `outcome_project_N`.
6. `prepareProject` currently silently drops a non-null project on a `policy` outcome
   (`domestic.cpp:6-8`). With the gate below it is accepted, and the sector decides the category
   (commit 4dd06c1).
   - Accept a non-null project when `outcome_kind` is `policy`.
   - Keep the setback check and the `happened` / no-failed-ops conditions.
   - Version-gate the change with the same new key.
7. Leave the prompt unchanged apart from item 3's rename. Non-domestic outcomes keep `null`.

## Replay and compatibility

- Schema and prompt changes do not affect replay, which applies recorded deltas.
- Compiler changes (items 4 and 6) apply only when the new engine key is set. Legacy saves keep their
  current behaviour, and `./build/replay_verify` must pass on an integration export.
- Player signature flow is unchanged: the player signs their own offer in the UI the following month.

## Implementation notes

- llama.cpp's schema converter ignores `prefixItems` when `items` is present, so a narrowed offer
  outcome allows only the narrowed `add_commitment` (`minItems` 1, `maxItems` 2), following the
  regional narrowing, rather than a required first item plus free extras.
- The relabel covers `ceasefire` and `peace_treaty` only; a non-aggression pact granting access is
  plausible in peacetime. The note is appended as the event's verdict, because factual proposal
  text (event text version ≥ 1) replaces the event text.
- After the live A/B, offer narrowing (with `maxItems: 1` and the offering nation first) and the
  `clauses` key moved behind `inference_experiment: "offers"`; the base prompt is unchanged.
- Engine key `intent_version` (0/1), selected for new turns by `lifecycleReference`, gates both
  compiler rules. `narrowIntentions` and `treatyOffer` are in `src/inference/prompts.cpp`.

## Acceptance

- **Deterministic (CTest, fixture worlds, bounded):**
  - `context_tests`: an offer intention gets a required narrowed `add_commitment` as the first
    operation, with `clauses` sorting before `kind`. A domestic intention gets a non-null project
    whose id is narrowed to its own projects. Other intentions are byte-identical to before.
  - `domestic_tests`: new coverage for a null project rejected on reform, and a project on a
    `policy` outcome accepted under the new key but ignored under the legacy key.
  - `consent_tests` / `access_tests`: `clauses` is renamed back to `terms`; a request with both
    keys is rejected; a grant_access-only ceasefire or peace treaty is relabelled `other`, and
    legacy compile is unchanged.
  - `make test`, `python3 tests/integration.py` and `replay_verify` pass. Restore
    `world.sqlite` before committing.
- **Live (short; ask the user before running):**
  - Scenarios: `peace` and `access_ceasefire` fixtures, 2 months each, baseline vs plan 35.
  - Must hold:
    - The access offer exists with `grant_access.to == PT`.
    - The ceasefire offer still exists.
    - The peace scenario starts or continues a public_services reform in both months.
  - Latency: median s/month regression ≤ 10% per scenario. Record the results in `docs/PLAN_34.md` style
    under a new `docs/PLAN_35.md`.
- Update `docs/MODELS.md` (project contract, offer narrowing, clauses key) and
  `docs/EVOLVING_WORLD.md` (kind/clause consistency). Windows remains unverified.

