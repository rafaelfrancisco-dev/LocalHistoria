# Plan 31 implementation record

Plan 31 adds versioned consequence accounting and distinct domestic project records.

Consequence version 1 stores bounded, deterministic identities per actor and outcome kind. Exact
repeats receive no additional effect; unstructured repeats share a monthly envelope and diminish
across active months. Validated operations retain their own identities. Project version 1 gives
reforms and investments stable IDs derived from actor, category, target and supported benefit.
Projects compete with construction and replacement for the existing allowance, expose progress and
delays, support explicit continuation and cancellation, and pay completion benefits once.

Legacy category efforts migrate on the working copy to `legacy` projects. Lifecycle references record
both rule versions, and replay validates consequence records and project snapshots. Failed commits do
not publish migration or accounting.

Validation:

- 19/19 CTest tests passed, including the new domestic and replay fixtures.
- HTTP integration passed with disposable saves, restart, cancellation, stale requests, malformed
  model responses, project journal entries, and replay.
- The browser flow was extended to inspect project objective/progress and event filtering. The final
  phone layout check compares action controls with the journal rather than assuming a fixed page
  height, because project facts add legitimate inspector content.
- A Qwen3.5-4B Q4_K_M disposable two-month scenario committed and replayed two turns identically.
  Submission-to-commit was 107.42s and 124.72s. The model created separate infrastructure and
  administration projects in month one; no unsupported benefit was accepted. This is a single
  scenario, not a general quality or performance claim.
- Fixed synthetic benchmark tooling is in `tools/domestic_benchmark.cpp`; it reports compiler,
  monthly, schema and state sizes for matched legacy/current runs. The current 16-actor, 2,000-run
  workload measured 309ms compile and 892ms monthly for policy inputs, and 266ms/863ms for project
  inputs. The legacy checkout benchmark binary was not available in the final environment, so no
  comparative regression claim is made.

The real model was run only against temporary probe saves. `saves/campaign.sqlite` was not opened or
modified.
