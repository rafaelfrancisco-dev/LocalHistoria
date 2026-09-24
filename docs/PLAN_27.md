# Plan 27 implementation

This checkout implements the correctness and measurement portions of Plan 27.

Inference requests now use request-scoped deadlines, cancellation and audit records. Audits include attempt status, retry reason, stage timing, token provenance, schema size, model/build metadata, server timing fields when supplied, and compilation timing. Failed, cancelled, malformed and truncated attempts remain visible. Turn timing records distinguish context preparation, optional chronicles, mandatory decisions, adjudication, simulation, commit, and finished prose.

Decision facts now include all proposal parties, required signatories, revision, amendment target, duration and readable names. Responses are exact boolean maps; missing, unknown and non-boolean answers fail validation. Chronicles select complete sentences with background and recent-history budgets. Busy chronicle event selection retains unresolved and structural events before recent events. Adjudication context version 2 shares event, entity, conflict and agreement dictionaries while actor sections retain relevance references. Optional trimming includes `past_events` and preserves mandatory map and consent facts.

Chronicles are optional and use a measured mandatory reserve. With no measured reserve they are skipped and the cached revision is recorded. Slow optional work cannot extend into mandatory time. Narration remains after commit and failure leaves the authoritative fallback summary.

Compiler-owned proposal text reports actual proposal/agreement IDs and revisions. Preliminary talks are distinct from recorded offers. The event text version is persisted in turn references so replay keeps historical event text stable.

The `reasoning`, `schema`, and `compact` reductions are separate opt-in experiments via `inference_experiment`; production defaults remain conservative. `tools/living_world_probe.py` accepts disposable starting saves, fixed schedules, frozen references, binary selection, reserve/budget settings and records hashes/configuration.

Deterministic focused verification passes: core, context, turn, consent, conflicts, access, lifecycle and mixed replay. Full status and data suites were already passing before the final focused run. Live Qwen fixed scenarios were run against the unchanged binary five times; The probe's fixture copy was repaired (WAL fixtures need `immutable=1` for the read-only backup). One live 3-month repeat of each fixed scenario then ran on the original and current binaries, all replaying identically. The first current-binary run exposed a month-2 crash in `collectEventIds` (`src/world/schema.cpp`) on the shared adjudication context, now fixed with a regression test in `tests/context_tests.cpp`.

Seconds to commit per month, original vs current: peaceful 90/87/83 vs 94/72/86; war 192/151/164 vs 156/135/178; diplomacy 117/85/90 vs 93/80/127. This is a single repeat, so it shows no regression and no established speedup. The model server was started from the installed cached Qwen3.5-4B Q4_K_M file with the existing 32K/single-slot settings. No user campaign save was opened or modified.
