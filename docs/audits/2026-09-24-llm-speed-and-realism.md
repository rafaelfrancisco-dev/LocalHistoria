# LLM speed and realism audit — 24 September 2026

Audited checkout: `eb28a3a`. Scope: turn planning/execution, all four LLM prompt types, context assembly, schema generation, transport/budgeting, operation compilation, simulation interfaces, and existing performance/acceptance evidence. This is an audit, not an implementation change.

**Conclusion:** meaningful speed improvements are plausible without simplifying the simulation. First reduce repeated context and optional work on the critical path, correct incomplete decision facts, and make narrative claims follow accepted operations. Do not begin by reducing the number of simulated countries, replacing the model, or disabling all reasoning.

## Evidence and current pipeline

The pipeline is: prepare frozen contexts → up to 3 chronicles when due → up to 4 reactive decisions → normally 3 independent decisions → one joint adjudication → deterministic compilation/monthly simulation/commit → optional narration. Decisions and adjudication are serial; `LocalProvider::completion` also holds one inference mutex. Narration follows commit but occupies the same turn worker until it finishes.

Sources: `src/turn/plan.cpp`, `src/turn/run.cpp`, `src/turn/worker.h`, `src/inference/{prompts,transport}.cpp`.

Historical measurements, not new measurements:

- `docs/audits/2026-09-23-simulation-realism-reaudit.md`: pre-fix Ukraine turns took 170–200 seconds; adjudication alone took 104–134 seconds, with 10,978–16,782 prompt tokens. That audit's defect list is not the current defect list.
- `docs/MODELS.md` records the subsequent fixes: adjudication took 120–129 seconds, with 2,010–2,859 output tokens; one output-cap failure added a 49-second retry. The later increase from 160 to 192 answer tokens per outcome was not measured live.
- The same document records about 350 prompt tokens/s and 34 generated tokens/s on the M1 Pro. These are historical approximate rates, useful for budgeting, not a current benchmark.
- `docs/benchmarks/2026-09-23-model-screening.md` contains zero completed scenario/full-turn samples. It cannot justify a model change.

Current code already has required `outcome_kind`, required per-proposal boolean responses, explicit revision-bound consent, unreachable-front handling, compact operation definitions, bounded reasoning, cached server properties, and a token-estimate fast path. Recommendations below account for those changes.

## Prioritized findings

### 1. High value: adjudication repeats context it could share once

`adjudication_input.cpp:85–115` copies actor contexts individually. Each carries its own conflict/commitment descriptions and related history. Events involving several participating actors can therefore appear several times. `past_events` can overlap `related_history`; central actors retain entity lists while relevant entities are also included at the top level.

**Change:** construct shared dictionaries keyed by event, agreement and entity ID, with compact actor references. Keep all relevant unique facts, ownership, control, consent revisions, causal IDs and force constraints. Use prompt-specific projections instead of passing full persisted event documents. Strip citation/licensing metadata from inference inputs while retaining it in the database/UI.

**Expected benefit:** less prompt processing and less contradictory/redundant material for the model. At the recorded rate, eliminating 3,000 actual uncached input tokens would save about 8.6 seconds per request. This is a conditional estimate; measure actual duplication before promising that reduction. A representation change can affect a small model, so require semantic A/B acceptance.

**Acceptance:** identical unique authoritative fact coverage; no missing treaty terms or target regions; live multi-actor treaty, war and construction outcomes at least as reliable as baseline.

### 2. High value: optional chronicles consume the mandatory turn budget

`run.cpp:48–111` generates up to three chronicles before any decisions. Exceptions are swallowed, but `transport.cpp` caps each call against the same whole-turn deadline. A slow optional chronicle can exhaust the budget and leave the following mandatory decision unable to run. Limiting the number of chronicles does not reserve time for the actual turn.

**Change:** prepare chronicles during idle time from a versioned committed snapshot; freeze the available version at turn submission. Give optional work a separate small budget and admission priority below active turns. Always retain current conflicts, proposals and recent events independently of the chronicle. Do not simply move work to another thread while leaving the shared inference mutex as an uninterruptible bottleneck.

**Expected benefit:** removes chronicle generation from affected turns' critical path and prevents an optional timeout from causing mandatory failure. Little benefit on turns where no chronicle is due. Narrative freshness must be protected by the current-fact overlay.

Narration is already after commit, and the UI already supports skipping it. A lower-priority narration queue could prevent it delaying the next submitted turn, but would not reduce inference computation by itself.

### 3. Confirmed realism gap: treaty signers do not see the complete offer

`src/core/simulation.cpp:356–373` projects pending proposals into decision facts with name, kind, text, revision, terms, signatures and expiry. It omits `actors`, `required_signatories`, `months`, and amendment identity. Those fields exist in proposal construction (`compiler/proposals.cpp`, `compiler/treaty_ops.cpp`). The offer's expiry is not the treaty's duration.

**Change:** provide all parties, duration/open-ended meaning, amendment target, and readable party/territory names alongside stable IDs. Keep explicit signatures tied to the exact revision. Validate that every offered ID actually receives a boolean in `decide`, rather than relying only on server grammar enforcement.

**Expected benefit:** substantially better-informed consent for a few extra tokens, no additional LLM call. This does not mean the current engine grants signatures implicitly; the defect is incomplete information at decision time.

### 4. Confirmed realism gap: chronicle truncation removes the newest history

`chronicle()` requests oldest-to-newest summaries up to 2,400 characters. Adjudication requests a 1,200-character chronicle. `briefReference()` (`simulation.cpp:49–90`) applies its generic prefix cut to chronicles, preserving the oldest part and potentially removing precisely the recent wars and unresolved tensions it asks the model to emphasize. Raw history excerpts, by contrast, retain their tail.

**Change:** store a short background summary separately from recent campaign developments and unresolved issues, or preserve the recent end with sentence boundaries and a compact background sentence. Keep unresolved obligations as authoritative facts, not merely prose.

**Expected benefit:** better continuity at essentially the same prompt length, no extra call. Separately, `chronicleEvents()` keeps only the latest 40 events while the saved summary is marked current through the turn: busy years can permanently omit earlier important events from the summary. Select milestone/unresolved events as well as recent ones within the same token budget.

### 5. High value: resolve the prompt's negotiation/operation ambiguity

`prompts.cpp:177–186` asks for a description followed by operations that make the map match it, and lists trade/negotiations among actions valid with no operations. Later instructions correctly require `add_commitment` for treaties. This leaves the model room to narrate a submitted ceasefire proposal without creating one. The post-fix live evidence in `docs/MODELS.md` still records only 1 of 3 player treaty orders becoming proposals.

**Change:** clearly distinguish preliminary talks from a concrete recorded offer. A concrete offer needs a proposal operation; mere discussion is a negotiation. Derive completion/acceptance language from compiler results and proposal IDs. Preserve genuinely narrative diplomatic activity. Do not infer consent from the player's prose.

An eventual wire format can select operations before writing outcome text, but field order is sensitive: the repository already measured regressions when `outcome_kind` moved before operations. A/B this rather than blindly rearranging fields.

**Expected benefit:** improved factual realism with the same number of calls; replacing contradictory instructions need not grow the prompt. Engine-generated short text for purely mechanical outcomes could also reduce output tokens. Keep names, political motives and genuinely interpretive outcomes model-authored where useful.

### 6. Medium value: use capability facts to reduce needless reasoning and invalid proposals

`prompts.cpp:267–271` counts each action as reasoning-worthy if its wording is consequential **or its actor has any offensive region**. Thus one reachable offensive option can make all of that actor's unrelated domestic actions increase the reasoning budget. This is intentional protection against paraphrases such as “drive to the capital”; removing it outright risks losing that protection.

**Change:** use action-specific target/operation evidence and explicit ambiguity classes. Retain reasoning for conflicting orders, territorial changes, difficult settlements and uncertain interpretation. Do not introduce an additional classifier call. Structured AI intents could carry their own operation family; free-text player orders still need a conservative path.

At the historical generation rate, 384–768 actually generated reasoning tokens represent roughly 11–23 seconds. The configured budget is a ceiling, not a guaranteed saving.

The schema already restricts building by control and occupying by reach. It still exposes many globally available operations/IDs per action, and reachable occupation choices are not strength-filtered there. Add deterministic legality narrowing where it preserves feasible sequences: actor-specific movement entities/destinations, applicable agreements, coastal construction and force limits. Account for same-turn prerequisite operations before removing a choice. Never hide a valid action merely because a keyword classifier missed it.

### 7. Confirmed efficiency gap: overflow trimming misses adjudication's past events

`adjudication_input.cpp:97–98` renames `recent_events` to `past_events`. The transport trimming loop only trims `related_history` and `recent_events`. Consequently optional past-event prose survives even when the adapter eventually reports that mandatory facts exceed the budget. Each individual removal also repeats `/apply-template` and `/tokenize`.

**Change:** explicitly classify optional fields for every prompt type, including `past_events`; trim chunks by priority and use bounded search rather than one event per two HTTP calls. Preserve unresolved commitments and causality required for the current action. Prioritize actors by relevance rather than alphabetical iteration.

**Expected benefit:** fewer overflow failures and fewer preparatory requests in long campaigns. Normally short prompts already take the estimate fast path, so their latency would barely change.

### 8. Low inference cost: persistence and grounded world events

Independent decisions must produce exactly one action and may be retried if all output is ceremonial. National actors have historical context but no general explicit objective/plan record comparable to regional agendas. This encourages action churn and repeated restatement of programmes.

**Option:** a small persisted objective with target, progress, review date and abandonment triggers. The monthly engine continues already authorized work; the model decides when to revise it. This could eventually reduce routine decision calls while increasing continuity, but is a medium-sized simulation change requiring replay/versioning and actor-fairness tests. It is not an immediate no-risk optimization.

World events currently allow the model to invent disasters/coups/elections among listed actors, with qualitative effects, but without a supplied event-specific hazard/calendar. Prefer bounded, seeded candidate events grounded in instability, election eligibility, exposure and cooldowns; let the model explain selected events. CPU cost can be small and no extra LLM call is necessary. Calibration/data work is real; do not present arbitrary probabilities as historical realism.

## Supporting improvements and things to avoid

- Reuse a journal snapshot and cached actor context within one immutable turn plan: `Store::campaignContext()` rereads `journal()` and issues a recursive event query on each call. Context planning happens in the request path under the server mutex. Measure this independently; there is no evidence it rivals model latency.
- Record transport errors/timeouts as inference attempts. Currently the audit record is appended only after chat completion returns; failed transport can consume time without a corresponding record. Extract prompt processing, generation, cached-token counts, output-cap finishes and retry reasons from the raw response.
- Current upstream llama.cpp documents chat-response `timings` and prefix-cache behavior: [server documentation](https://github.com/ggml-org/llama.cpp/blob/master/tools/server/README.md). Verify support against the installed build. Schema bytes are not automatically prompt tokens; measure schema/grammar preparation separately.
- Do not simply lower `max_tokens`: it is a ceiling, and the historical truncation retry already demonstrates the risk. Reduce generated content rather than chopping off required output.
- Do not remove the action-opening anchor without replacing its disambiguation benefit; the repository measured duplicated outcomes without it.
- Do not start with concurrent calls on the M1 Pro: historical two-slot measurements showed almost no net gain. Other hardware needs its own measurements. Batching unrelated decisions can leak knowledge between actors and increase coordination unrealistically.
- Do not drop C++ validation, explicit consent, atomic commits, monthly autonomous war resolution or replay. They supply the realism that permits the prompts to become smaller.
- Do not assume a larger context, more cache RAM or a new model is a free speed improvement. The repository already records cache memory pressure and incomplete model screening.

## Implementation order and acceptance

1. Add complete attempt/stage timing, plus prompt/token inventories from representative turns. Separate submit-to-commit from narration time.
2. Correct treaty projections, recent-history retention, optional trimming and negotiation wording. These are the cheapest realism/reliability changes.
3. Deduplicate context and move optional chronicles off the critical path with frozen provenance and bounded priority.
4. A/B compact outputs, narrower operation choices and action-specific reasoning independently.
5. Consider persistent national plans and grounded background-event candidates as separate versioned simulation changes.

Use identical disposable starting saves, orders and model/server configurations; repeat peaceful, multi-front war, treaty/amendment, regional, long-history and maximum-action scenarios. Measure median/p90 turn latency, actual tokens, retry/failure rates, accepted operation accuracy, consent correctness, factual prose and repeated-action behavior. Preserve legacy/mixed replay, long-run conservation and political-geography invariants. A faster run that silently loses a treaty or effort fails acceptance.

No percentage speedup is established by this audit. As a planning illustration only, saving 3,000 input tokens and 400 output tokens in adjudication would be about 20 seconds at the historical rates, before any chronicle or retry savings. Avoid adding overlapping estimates.

## Verification on this checkout

- Rebuilt current source with `cmake --build build -j 4`; successful, with an existing shadow warning at `src/turn/plan.cpp:192`.
- `ctest --test-dir build --output-on-failure`: all 17 suites passed in 46.16 seconds, including consent, conflicts, access, legacy replay and mixed replay.
- Local model health check at `127.0.0.1:8081` could not connect. No new real-model timing, quality A/B, browser or HTTP campaign acceptance is claimed.
- Existing untracked benchmark documentation was left unchanged; no gameplay source or campaign save was modified.
