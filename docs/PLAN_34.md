# Plan 34: adjudication experiments and acceptance

The implementation keeps the same Qwen model, single inference slot, three independent actor
slots and reactive coverage. C++ still validates all operations and exact proposal revisions.
Production behavior is the baseline (`inference_experiment: ""`); experiments remain opt-in until
measured quality and timing gates pass. Windows acceptance is deferred at the user's request.

## Experiments

- `compact`: asks for brief mechanical descriptions and selects event text version 3 in the
  recorded turn reference. The compiler renders accepted territorial, entity, conflict and
  identity changes, actual construction status, rejected operations, and existing proposal IDs,
  revisions and exact signature verdicts. Interpretive descriptions remain for nonmechanical
  events. Versions 0–2 retain historical text; recorded deltas are the replay contract.
- `schema`: factors repeated outcome cause/effect/project schemas into shared definitions and
  retains the existing fact-based authority narrowing. Action opening, actor attribution, field
  ordering, regional scope, free-text actions and compiler checks remain intact. Schema byte
  reduction is not a speed claim; request preparation, server prompt/generation and complete
  month timing are measured independently.
- `reasoning`: uses the existing domestic-action reasoning selection independently, with the
  same output limits and bounded consequential reasoning. It adds no classifier or repair call.
- `grounding`: independently tests short explicit-offer/access/project examples. It distinguishes
  an actual offer from mentioning or discussing an agreement and permits feasible partial
  outcomes. It neither creates operations deterministically from text nor reconstructs signatures.

Only independently successful reductions may be combined. If no candidate passes screening,
no combination or finalist repeats are justified and the baseline remains the default.

## Reproducible fixtures

Build `adjudication_fixtures`, then run from the repository root:

```sh
./build/adjudication_fixtures /tmp/plan34-fixtures
```

The destination must be new and outside `saves/`. The generator uses the shipped reference world
and current C++ compiler/lifecycle to create six closed full-world databases, fixed two-month
schedules, explicit expected/permitted outcomes, and an empty frozen external-reference map:
peace/domestic reform, a synthetic superior-force invasion, access/ceasefire offers, a three-party
proposal, a funded regional demand, and a 36-month campaign with five authored events per month.
The older campaign advances through real deterministic monthly simulation; it is synthetic,
not evidence from a user's long-running save. All actor slots and reactive selection remain active.
External reference networking is disabled in the probe for every arm.

`living_world_probe.py` uses normal SQLite read-only backup with committed WAL frames, a pinned
source transaction, matching table counts, and destination integrity checks. It refuses existing
snapshot destinations, closes connections explicitly, and replays a starting fixture before timing.
This supersedes the immutable-copy advice in `PLAN_27.md`. The prepare-only path uses the same safe
backup. `tests/benchmark_fixture_tests.py` covers committed/uncommitted WAL data and overwrite refusal.

Complete `scenarios.json` with verified `model_sha256`, `quantization`, `llama_build`, `hardware`,
`context`, `slots`, `compiler`, and `sampling` metadata and save it as `manifest.json`. Optional
`binary` and `replay_binary` select a matched optimized build; scenario paths are relative to the
manifest. Keep hashes of reference data, schedules and fixture files alongside the manifest.

```sh
python3 tools/adjudication_compare.py /tmp/plan34-fixtures/manifest.json /tmp/plan34-baseline --baseline-only
python3 tools/adjudication_compare.py /tmp/plan34-fixtures/manifest.json /tmp/plan34-compact --experiment compact
# Only for a candidate which passed independent screening:
python3 tools/adjudication_compare.py /tmp/plan34-fixtures/manifest.json /tmp/plan34-finalist --experiment compact --repeats 3
```

Each screen runs baseline/candidate for two months per scenario. Odd repeats reverse arm order.
A candidate run/quality failure stops that experiment, preserving its partial report. A failed
baseline remains visible rather than preventing comparison. Output directories must be new.

## First screening attempt (2026-09-25, incomplete)

A sequential screen of all four experiments stopped at its first scenario each time: the
**baseline** also misses the peace outcome. Across five peace runs (baseline and candidates),
"Fund a national education reform to improve public services" never produced
`benefit: public_services`. The model chose `administration` (the first enum value) or
`project: null`. jsoncpp sorts keys, so `benefit` is generated before `objective`/`target`.
Peace timings were baseline 89–96 s/month, compact 99–100 s and schema 94 s (single samples, no
conclusion). The model server later exited, and no experiment reached a second scenario.

Screening now judges required outcomes against the paired baseline. A candidate stops on a failed
run, a hard violation (unauthorized signature, capacity/ownership breach or attribution), or
losing an outcome its baseline achieved. The adoption gate follows the same no-loss rule and
records baseline misses as `baseline_notes`. The benefit-ordering bias is a production
reliability defect, separate from the latency experiments. `grounding` addresses it in the
prompt; a structural fix would move context ahead of `benefit` or derive `benefit` in C++.

## Grounding screen (2026-09-25, one paired run, 3 of 6 scenarios)

This screen covers peace, access/ceasefire and multi-party only, to fit a 30-minute window.
Invasion, regional and the older campaign were not screened. Same frozen fixtures, binary and
model as the manifest; server restarted with identical flags.

| Scenario | Baseline outcomes | Grounding outcomes | Baseline s/month | Grounding s/month |
|---|---|---|---|---|
| peace | missed (administration benefit) | **both months correct** | 83.0–90.1 | 94.9–100.2 |
| access_ceasefire | ceasefire yes, access no | ceasefire yes, access no | 102.9–112.2 | 92.1–129.5 |
| multiparty | passed | passed | 118.0–131.8 | 144.8–150.7 |

Grounding fixed the education benefit and did not lose any outcome, but used 13–22% more output
tokens and was 7% slower overall (peace +13%, multiparty +18% medians). One AI decision retry
(`ceremonial_independent_action`) occurred in the grounding multiparty run; that request does
not receive the grounding prompt. Neither arm produced the military-access offer. Grounding
fails the latency adoption gate and stays opt-in; its reliability gain argues for a structural
benefit fix (context key ahead of `benefit`, or C++ derivation) rather than a longer prompt.

## Project sector ordering fix (2026-09-25)

The project schema now asks for `sector` instead of `benefit`. Keys generate alphabetically, so
the order is action, id, objective, **sector**, target. The model writes the objective before
choosing. The prompt defines administration (governance, taxation, justice) and public_services
(education, health, welfare). The compiler stores `sector` as the project's `benefit`; records,
facts, replay and the UI are unchanged, and a request carrying both keys is rejected.

Live check on the current build, same fixtures, baseline arm (three 2-month runs):

- Portugal's education order chose `public_services` in 5/5 adjudicated project events
  (previously 0/6 runs correct).
- peace run 1 and regional run 1 passed their gates. peace run 2 failed for a different reason: the model
  rated the reform `stability: decline`; the Plan 31 setback rule started no project, and month 2
  referenced an invented id (an event id), which the compiler rejected.
- Timings were 92–105 s (peace) and 102–117 s (regional) per month; too few to compare latency.

Remaining observed defects: invented project ids on start/continue, and setback self-ratings that
block a funded reform.

## Evidence and adoption

The probe records submission-to-commit and finished-prose times separately, every request audit,
preparation/tokenization/prompt/generation/compilation timing, turn stage timing, schema bytes,
retry reasons, failures/truncation and authoritative state/events in replay exports. Raw reasoning
is retained; its text is tokenized after the measured turn, excluding delimiters, with unavailable
counts labelled. Server grammar preparation remains unavailable when the server does not report
it; end-to-end timing includes that cost. Failure reports retain elapsed cost and available audits.
The default per-month bound is 210 seconds plus five seconds of polling grace. Only benchmark-owned
application processes are terminated. No model downloads or user campaign changes are needed.

The quality evaluator checks funded project identity, invasion control versus ownership, recorded
ceasefire/access proposals, exact-revision signatures, actual regional project fulfillment and
outcome attribution/scope. An accepted proposal requires every required party's affirmative
signature for that revision. Authoritative replay, not model prose, determines outcomes.

The adoption evaluator requires three paired repeats, all scenario outcomes, no failed runs,
no increased retries/truncations/failed attempts, at least 15% overall median improvement and
no scenario median regression above 10%. Reports retain median/range and every failed attempt;
small samples are explicitly insufficient for tail/p90 or general quality claims. Default adoption
remains a separate deliberate change; `inference_experiment: ""` is the rollback.

Deterministic validation includes all CTest suites, legacy/mixed replay, compiler text/version
regressions, shared-schema semantic equivalence and quality-gate negative controls. HTTP/browser
integration and restart/replay checks are also required before screening. Live model calls are
never part of routine CTest. Measured outcomes and configurations are recorded in the dated audit
report; a platform not tested remains unverified.
