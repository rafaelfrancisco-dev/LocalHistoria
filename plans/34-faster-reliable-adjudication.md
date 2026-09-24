# 34 · Faster adjudication with reliable operations

**Status:** planned, 2026-09-24. **Priority:** P1, largest documented latency opportunity.
**Audit coverage:** issue 1. **Dependencies:** measurement can start immediately; use plan 29 for
matched optimized builds and repeat final acceptance after plans 31–32 change schemas/semantics.

## Objective and evidence

Reduce time to a correctly committed month while retaining the free-text, living-world simulator.
The model proposes interpretations and operations; C++ remains authoritative for state, feasibility
and exact consent. Keep the three independent slots and existing reactive coverage.

Existing repository measurements, not a fresh audit benchmark, put adjudication at 120–129 seconds
in a war scenario, with 2,010–2,859 output tokens and one additional 49-second retry. Plan 28's
small probe recorded 0/3 access-treaty proposals and 1/3 AI treaty proposals. These demonstrate
remaining reliability concerns but do not establish a statistically significant regression.
Plan 27 already supplies request timing, shared contexts and separate `reasoning`, `schema`,
`compact` experiment switches; use those foundations.

## Read and change

- `src/inference/prompts.cpp`, `transport.cpp`, `include/localhistoria/providers.h`.
- `src/world/schema.cpp`, `src/world/compiler/events.cpp`, `proposals.cpp`, `treaty_ops.cpp`.
- `src/turn/adjudication_input.cpp`, `run.cpp`, `tools/world_probe.cpp`.
- `tools/living_world_probe.py`, `tests/context_tests.cpp`, `tests/turn_tests.cpp`, `tests/consent_tests.cpp`.
- `docs/PLAN_27.md`, `docs/MODELS.md`, `docs/VERIFICATION.md`, `plans/15-parallel-slots.md`.

## Phase A — Reproducible baseline and quality gates

1. Prepare closed, disposable starting fixtures and fixed order schedules for peace/domestic work,
   an invasion, access/ceasefire offers, multi-party consent, regional demands and a busy older
   campaign. Freeze references and record model hash/quantization, llama build, hardware, context,
   slot count, binary hash, compiler configuration and sampling settings.
2. Validate fixture copying first. Use a consistent SQLite backup from a synchronized source;
   immutable reads are valid only for a verified checkpointed, closed fixture. `--prepare-only`
   currently copies while the server is alive: close/checkpoint it or use a safe live backup.
   Verify copied campaign/turn counts and replay before timing. Never read the user's active save.
3. Record successful and failed attempts: submission-to-commit, finished-prose time, decisions,
   context/tokenization, adjudication, compilation, simulation and commit; prompt/output/reasoning
   tokens and schema bytes; retries, truncation, invalid attribution, and actual accepted operations.
   Report successful-turn latency and failure/retry cost together; fast failures are not speedups.
4. Judge outcomes by authoritative state/events: an offered treaty must exist, an accepted treaty
   must have all signatures for that revision, an invasion must obey capacity, a reform must start
   or continue the correct project, and demands must reflect actual fulfillment. Define permitted
   alternative outcomes per scenario before running the comparison.

## Phase B — Independent reductions

1. Test compact output first: omit duplicated mechanical prose and let C++ render accepted
   operations, rejected parts, proposal IDs/revisions and actual status. Retain short interpretive
   text where the operation alone cannot explain the event. Never reconstruct signatures from text.
2. Test schema reduction separately: factor repeated definitions and constrain candidate operations
   from validated facts without removing legitimate free-text intent paths. Preserve required
   outcome attribution, consent answers, regional scope and compiler validation. Measure actual
   token/grammar preparation and generation costs; fewer schema bytes alone proves no speedup.
3. Test reasoning selection separately with the existing domestic-action experiment. Keep adequate
   reasoning for feasibility and consent; evaluate total generation and error/retry cost. Do not
   simply shrink output limits until truncations transfer cost to a second request.
4. Address omitted operations through concise schema/prompt examples or bounded intent grounding,
   retaining normal feasible partial outcomes. Do not silently turn every mention of a treaty into
   a proposal or bypass C++ validation. Preserve the measured importance of field ordering.
5. Combine only independently successful reductions, then remeasure the combination. Avoid a new
   per-turn classifier/repair call unless its full cost and reliability beat the single adjudication
   path. Do not change model size, parallel slots or actor counts as part of the comparison.

## Phase C — Acceptance and rollout

- Run deterministic provider/compiler/consent tests, all CTest suites, HTTP/browser integration,
  and mixed-version replay first. Version compiler-produced event text when it changes; old replay
  text must remain stable. Recorded deltas remain the replay contract, not regenerating model output.
- Screen each experiment with a bounded paired run (two months per scenario/arm); reject clear
  quality failures early. Only finalists receive at least three paired repeats across peace, war
  and diplomacy, alternating baseline/candidate order. Report median/range and all failures;
  estimate p90 only with an adequate sample, otherwise label tail evidence insufficient.
- Proposed adoption gate: at least 15% lower median submission-to-commit time overall, no scenario
  median regression above 10%, no increase in retries/truncations, and no loss of required scenario
  outcomes. Zero unauthorized signatures, territorial changes or attribution errors. Small samples
  cannot establish general model quality; retain representative replayable evidence and limitations.
- Use identical model settings on M1 Pro and the Windows RTX 3060 6 GB target. A platform not tested
  remains an explicit acceptance gap. Treat unavailable hardware as unverified, not passed.
- Bound each live run and watch progress. Start with the repository's roughly 210-second historical
  per-month stop threshold; if a fresh baseline needs a different threshold, document it before
  comparing candidates. Persist partial reports, stop rejected experiments, and clean up only
  processes the benchmark owns. No live-model calls in routine CTest.
- Keep unsuccessful experiments opt-in. Enable a winner by default only after final schemas from
  plans 31–32 pass the same quality gate; retain a configuration rollback to baseline behavior.

Update `docs/MODELS.md` and `docs/VERIFICATION.md` with measured configurations, samples, operation
success rates and limitations. Do not present the old audit measurements as new results. No model
downloads, replacement-model selection or changes to `saves/campaign.sqlite` are required.
