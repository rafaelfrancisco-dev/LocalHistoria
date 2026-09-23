# Simulation realism re-audit — 23 September 2026

This is the post-implementation audit of [Plan 26](../../plans/26-simulation-realism-and-consistency.md).
It checks the fourteen findings of the [original audit](2026-09-22-simulation-realism.md) and the
acceptance conditions of phases A–J, and looks for regressions, on branch `plan-26-realism` at
`c0dfce4` with a clean working tree. No production code was changed. Evidence is kept separate
below: source inspection, deterministic tests and probes, integration and browser runs, and
live-model runs. `saves/campaign.sqlite` was not opened; every campaign used a disposable database.

**Verdict at audit time: Plan 26 was not yet accepted.** (Resolved the same day; see [Resolution](#resolution-23-september-2026-same-day).) All fourteen original findings are resolved in the
engine and reproduce as fixed. Each phase gate passes again on fresh runs. The audit found five
confirmed defects that block acceptance (N1–N3, N7, N8) and two minor ones (N4, N5). N3 and N8 are
the most serious. Both show up only in live play: nearly every outcome without a map change is
blanked, and no AI nation ever signs a treaty. It also found
that `world_probe`'s war scenarios no longer measure the model under the Phase I thresholds (N6).
The [remaining work](#what-remains) is bounded.

## Resolution (23 September 2026, same day)

The findings below are kept as found. Each has since been fixed and verified:

| # | Fix | Evidence |
|---|---|---|
| N1 | `engagements()` counts an enemy only when the two can meet: bordering land, an army in or next to the other's land, fleet routes to its coast, or a shared seeded theatre (`force.cpp`, `contact`) | probe X5: the defence stays at 75,000 with the unreachable war; `conflicts`: "An unreachable war does not divide the defence" |
| N2 | `wars[].against` lists only pairs actually fighting and adds `ceasefire_with`; the map and the Wars dialog follow `hostile_pairs` | probe X6: `against: []`, `ceasefire_with: ["Bland"]`; two `conflicts` checks |
| N3 | Required `outcome_kind` (renamed from `independent_outcome`, sorted after `operations`); an effort starts from a named reform or investment unless the model rates it a setback, and only when no operation succeeded; world events keep their text and effect | live: blanked 37/60 → 4/65; `reform` 3/3 with an effort; 4 `consent` checks |
| N4 | The establishment is snapshotted from the start-of-month inventory | `status` economy check |
| N5 | `grant_access` terms described in facts | `conflicts` check |
| N6 | `world_probe` forces recalibrated (Portugal 2,500,000; Spain 450,000 for `conflict`); `reform` requires an effort | live: `conflict` 3/3, `invasion` 3/3 |
| N7 | A direct cession to another existing nation is refused under realism; founding a new state and recognising the current holder remain | 3 `consent` checks; `access` and the browser fixture now cede by treaty or to a new state |
| N8 | Decisions that offer proposals must answer each in a required `responses` object; an identical pending offer is not reopened | live: 5 structured answers, 2 AI–AI treaties; `consent` check |

After the fixes: 17/17 CTest (45.2 s, unchanged), format, and integration with browser, restart
and replay all pass. The seeded world's 12-month Russo-Ukrainian toll is unchanged (194,879 killed,
886,605 displaced). The six-month live campaign replayed identically without inference. The
figures are in [MODELS.md](../MODELS.md#measured-prompt-changes).

**Remaining limitations, not defects.** The model still often narrates a treaty offer without
the treaty operation (1 of 3 player offers became proposals); kept as a negotiation, the text can
read like an offer the engine never recorded. The fixes cost about 400 output tokens and 10–15 s
of adjudication per war month; the per-outcome cap is now 192 tokens. Treaty answers still rank
first for reactions, so a busy treaty month can defer a war opponent by one month (seen once);
the backlog now clears because the proposals settle.

**Plan 26 is accepted.**

## Evidence gathered

| Kind | What ran | Result |
|---|---|---|
| Build and unit | `make test` (17 CTest suites, including v0/v2/v3/v4 legacy replays, `lifecycle`, `mixed_replay`, `consent`, `conflicts`, `access`) | 17/17 passed, 45.6 s |
| Format | `make format-check` | clean |
| Original probe, rerun | [`2026-09-23-realism-reaudit-probe.cpp`](2026-09-23-realism-reaudit-probe.cpp): the eight 22 September observations under the realism lifecycle, via `beginTurn`/`finishTurn` as the turn runner calls them | all eight fixed |
| Adversarial probes | same file, X1–X6: capture → annex → 18 months; AI offer signatures; two fronts over 36 months; 24 months of war then 24 of peace; an unreachable war; the shared status of a coalition war | N1 and N2 found; the rest behave as specified |
| Multi-year scenarios | `status_tests` (24–60 months: conquest, forces, displacement, economy, pacing, the idle seeded world) | passed; figures quoted below |
| HTTP and browser | `python3 tests/integration.py` (disposable servers, controlled model, headless Chromium) | passed in 2 min 36 s: cancel/budget, 4+1+1+8 turns replayed without inference, restart, legacy upgrade. The only browser errors are the `fr.svg` 404s that `tests/browser.mjs` injects on purpose |
| Live model | Qwen3.5-4B (`llama serve`, `-c 32768`, M1 Pro): `world_probe` 15 scenarios × 3; the four war scenarios × 3 again with recalibrated forces; a six-month Ukraine campaign on a disposable save | see [Live model](#live-model) |

## Traceability: original findings

Status is resolved, partial, unresolved, regressed or unverified. "Probe" refers to the rerun probe.

| # | Finding | Status | Current code | Evidence (actual result) | Remaining limitation |
|---|---|---|---|---|---|
| 1 | Mention counts as consent | Resolved for treaties; **N7** for direct cessions | Name scan `collectConsent` now legacy-only (`src/world/compiler/compiler.cpp:34`); explicit `settleProposals` (`compiler.cpp:52`); proposal checks in `src/world/delta_checks.cpp:102` | Probe 2: "Bland rejects Aland's trade offer" records a pending proposal (`answers: {}`), no commitment. `consent` tests: rejection, player-mention bypass, stale revision, missing signature, retry, impossible clause rollback, amendment revisions, concurrent change | The 4B model often narrates a treaty without the operation (live: see below). A direct cession needs no recipient consent (N7) |
| 2 | War end/ceasefire scope | Resolved in the engine; **presentation partial (N2)** | `fightingPair` (`src/status/conflicts.cpp:59`); pair-scoped fronts (`src/status/monthly/fronts.cpp`) | Probe 1: unilateral `resolve_conflict` gives no change. Probe 6: an A–B ceasefire leaves the war open at high intensity, with losses on the remaining pairs. `conflicts` tests: posture, separate peace, duplicate declarations, legacy fallback | `nationalStatus().wars[].against` still lists allies and ceasefire partners as enemies (N2) |
| 3 | Failed ops keep success bonuses | Resolved | `buildEvent`/`settleEffects` (`src/world/compiler/events.cpp:77–118`) | Probe 3: impossible city → `failed: true`, no effects, `major: false`, text "The proposed action could not be carried out." | — |
| 4 | Transit without access; inland fleets | Resolved | `moveEntity` (`src/world/compiler/force_ops.cpp:113`); `mayEnter`/`supplied` (`src/status/conflicts.cpp`); embarkation in `committedForce` (`src/status/force.cpp:230`) | Probe 4: inland fleet move gives no change. `access` 53 checks, sea-route fixtures in `data` | Region-grained sea graph; no naval interdiction or carrier aviation (documented) |
| 5 | Annexation erases conquest | Resolved | `recordControl` (`src/status/control.cpp:65`); `outputShare` (`control.cpp:32`); `controlStep` (`src/status/monthly/unrest.cpp:212`) | X1: after annexation the `control` record stays, unrest 49.3 after 18 months, and integration stays at 0 while the war with the claimant is open. `status`: "Annexation does not end the drain or the resistance"; third-party cession tests in `access` | A resistance holding ground does not return it to its claimant (documented) |
| 6 | Resistance needs occupier decline | Resolved | `occupationPressure` (`unrest.cpp:94`); narrowed duplicate guard in `formRebellions` (`unrest.cpp:162`) | Probe 7: stable occupier, 24 months → unrest 40.0 (was 0). `status`: occupied theatre rose after 46 months; a quiet fragile country stays quiet; ignored demands reach 39.7 | — |
| 7 | Attrition rounds away; no recovery | Resolved; **minor N4** | `applyRealismInventory` (`src/status/monthly/recovery.cpp:81`); `fund`/replacement (`src/status/monthly/economy.cpp:130`) | Probe 5: 24 months → aircraft 100→95 with 0.999 carried. Ships stay at 30 because no fleet is within reach, as designed. X4: 24 months of peace restore active 99,585 and aircraft 100 | The establishment is snapshotted after the first realism month's losses (N4) |
| 8 | Force not tied to deployment | Resolved; **new defect N1** | `engaged`/`engagements` (`src/status/force.cpp:73,128`) | `status` force scenarios: allocations never exceed inventories, a second front costs offensive strength, home and assembling armies take no losses. X3: population loss equals recorded deaths (58,556) | An engagement with an enemy that cannot reach you still takes half the pool (N1) |
| 9 | Returns without provenance | Resolved | `returnDisplaced`/ledger (`recovery.cpp:151`); `recordDisplacement` (`control.cpp:98`) | Probe 8: a legacy 1,000 is kept as `displacement.unknown`, population unchanged, no decay. `status` displacement: foreign returns, host deaths, border changes, reload | Flight only to bordering regions (documented) |
| 10 | No resource competition or durable effort | Resolved in the engine; **not reachable live (N3)** | `fund` (`economy.cpp:130`); effort start in `settleEffects` (`events.cpp:145–187`) | `status` economy: allowance competition, one payout on completion, diminishing reforms, rich vs poor | Reform and investment orders start no effort unless the model volunteers `independent_outcome` (N3). No maintenance drag (documented; the plan said "may") |
| 11 | Trade rewards signing | Resolved | trade valuation in `src/status/monthly/economy.cpp`; signing reward skipped under realism (`events.cpp:210`) | `consent`: no one-off growth or decline. `status`: partner counted once, tiny partner ≠ major market, expiry = withdrawal | — |
| 12 | Pacing by admin boundaries and month-12 cliff | Resolved; **calibration note N6** | `captureEffort` (`force.cpp:292`); `exhaustionStep` (`economy.cpp:351`); `weighFront` (`fronts.cpp:342`) | `status` pacing: largest monthly attack change < 6%; renamed fronts within 15%; small district 0.72 vs vast region 2.55 effort; 1:1 for 3 years takes no ground and kills 173,111; 5:1 takes 19 regions | No terrain or area data; no defender entrenchment penalty (documented) |
| 13 | Captured construction completes | Resolved | `reconcileProjects`/`handOverProject` (`src/core/lifecycle.cpp:5,26`); pause checks in `delta_checks.cpp:220` | `access`: due army behind enemy lines does not complete; pauses keep reservations, replay identically, resume on recapture; handovers on cession | Evacuation of a paused military project is not modelled (abandon or destroy only) |
| 14 | Slots crowd out urgent reactions | Resolved (deterministic); **live interaction with N8** | `reactiveCandidates` (`src/core/politics.cpp:299`) | `politics`: mentions never crowd out the enemy, a finite backlog is served within bounded months, treaty answers first; `turn`: a retried month asks the same nations | Live: the enemy was served 5 of 6 months; unsettleable proposals took the cap once (N8) |

## Traceability: phases and gates

| Phase | Gate condition | Status | Evidence |
|---|---|---|---|
| A | Old exports replay to identical state and events; an upgraded campaign replays across the boundary; cancel/retry cannot migrate twice; engine-owned fields cannot be written by a delta | Resolved | `legacy_replay_v0/v2/v3/v4`, `mixed_replay`, `lifecycle` ("Retry repeats migration exactly", "Migration is idempotent", tampered `engine` change rejected). Territory deltas may change only owner and controller (`delta_checks.cpp:190`); actor status only a refounding's government |
| B | Rejection, mention bypass, stale/revised proposals, missing signatures, retry, impossible clause; failed city grants no growth; independent result survives | Resolved in the engine; **not in live play (N3, N7, N8)** | `consent` (19 checks); probe 2 and 3. Live: outcomes blanked, an unconsented cession, no AI signature |
| C | Partial ceasefire; allies not enemies; toll and movement agree; duplicates do not double; one party cannot end the war | Resolved in the engine; presentation partial (N2) | `conflicts`; probe 1 and 6 |
| D | No inland fleets, neutral transit or hostile movement; routes do not cross land; embarked capacity; captured projects; replay | Resolved | `access`, `data` sea-route fixtures, `regions_test` |
| E | Annexation keeps penalties; stable occupiers face resistance; theatre insurgency without duplicates; quiet countries stay quiet; cession does not teleport; 24–60 months | Resolved | `status` conquest scenarios; X1 |
| F | Allocations ≤ inventories; second fronts cost; unexposed formations survive; small inventories wear; fractions survive reload; charged once; population kept | Resolved; defect N1 | `status` forces; probe 5; X3, X5 |
| G | Domestic/foreign returns, no safe origin, no host, repeated flight, host deaths, border changes, legacy balances; conservation | Resolved | `status` displacement; probe 8 |
| H | Competition; no reward before completion; no farming; expiry = withdrawal; replacement takes time; demobilisation without erasing formations; rich/poor; calibration recorded | Resolved in the engine; N3, N4 | `status` economy; `consent`; X4; calibration in the plan's Phase H note |
| I | No anniversary jump; deterministic ties; renamed fixtures equivalent; tiny vs huge effort; shared order/momentum accounting; superseded calibration documented | Resolved; N6 | `status` pacing |
| J | Mentions cannot starve the opponent; proposals and demands get served; retry keeps selection; UI facts agree with rules; no misleading claims after rejection | Partial: N2 (wars list), N5 (access terms missing from facts), N8 (proposals crowd the cap) | `politics`, `turn`, browser; X6 |
| Final 1–5 | Suites, integration, browser, replay, 24–60 month scenarios | Passed | table above |
| Final 6 | Live-model matrix with repeats | Run in this audit | [Live model](#live-model) |
| Final 7 | Docs updated; audit resolution pointers | Done with this audit: README updated, and the original audit now carries resolution pointers | — |

## Findings

### Confirmed defects

**N1 — A war you cannot fight still halves your defence (medium-high, Phase F).**
`engagements()` (`src/status/force.cpp:73`) counts every hostile pair in `fightingPair`, whether
or not the enemy can reach any front. `engaged()` and `committedForce()` then divide the national
pool, air arm and fleet by that count.
- *Reproduce:* probe X5. Aland borders Bland; Cland borders only Bland. Aland at war with Bland
  alone: Bland's assault on Aland meets 75,000 defence. Add an Aland–Cland war (no front, no
  casualties, `affected_regions: []`): the same assault meets 37,500 and the ratio doubles.
- *Expected:* an engagement with no reachable front costs nothing, or a small reserve.
- *Actual:* any distant declaration, including one made by a third party, halves the target's
  strength on its real front. Over 36 months (X3) Aland also takes about half the losses against
  Bland.
- *Suggested bounded fix:* count an enemy in `engagements()` only when the pair has a front: a
  region either controls adjacent to the other, an active formation in reach, or a validated
  sea/embarked route (reuse `reaches`). Add an `access` or `status` check with an unreachable
  third war.
- The legacy defence divisor had the same shape; realism extended it to attack and losses.

**N2 — The shared status still names allies and ceasefire partners as enemies (medium, Phases C/J).**
`nationalStatus()` builds `wars[].against` from every other participant
(`src/status/helpers.cpp:203`) and gives the conflict-wide `intensity`. That list reaches the
country inspector and every prompt's `status`. `describeAgreement()` meanwhile gives the correct
`hostile_pairs` with `fighting` flags (`src/status/agreements.cpp:44`).
- *Reproduce:* probe X6. Aland and Cland fight Bland; Aland and Bland have a ceasefire. Aland's
  status reads `against: ["Bland","Cland"]`, `intensity: high`.
- *Expected:* against lists only the pairs Aland is actually fighting (none here), and says when a
  covered pair is at ceasefire.
- *Suggested bounded fix:* under realism, fill `against` from `fightingPair` and add the paused
  parties separately (e.g. `ceasefire_with`). Show the same in the Wars dialog.

**N3 — Narrative outcomes are blanked unless the model volunteers an optional field (high, Phases B/H/J).**
Under realism, an event keeps effects, starts an effort or keeps its own text without a successful
operation only when it carries a supported `independent_outcome` (reform, investment,
negotiation, protest) (`events.cpp:146–149`). The property is optional in the schema
(`src/world/schema.cpp:299`). Otherwise the description is replaced by "No validated change
resulted from the proposed action." (`events.cpp:88–92`) and its effects are dropped.
- *Reproduce, `world_probe reform`, three runs:* each time the model answered
  `effects: [{PT, economy: growth}]` without the field, and each time the event lost its effect,
  started no effort and showed the fallback text. The probe still reports `expected=1` because it
  checks only for "no map change".
- *Reproduce, live Ukraine campaign:* over six months the model set `independent_outcome` on
  **0 of 58** adjudicated events. All 11 of Ukraine's orders were blanked, among them three
  anti-corruption reforms, an investment, three treaty proposals, an offensive and a regional
  answer. So were most AI nations' diplomatic and domestic acts. The journal showed "No validated
  change resulted from the proposed action." for each.
- *Expected:* Plan 26 B says to keep narrative-only reforms and negotiations possible, and "do not
  solve this by dropping every event without a map change". Phase H efforts should start from
  reform orders.
- *Actual:* with the configured model, that is the practical effect. Phase H's effort machinery is
  unreachable, and the player sees nothing happen. This is a regression from the legacy lifecycle,
  which kept the description and bounded effects. The Phase J A/B did not catch it because both
  arms ran the realism rules.
- *Suggested bounded fix:* keep the model's description whenever the event is not failed (the
  fallback text only replaces claims of structural change). Make `independent_outcome` required
  under realism, since the enum already has `none`, or infer `reform`/`investment` from an event
  with no operations and positive author effects. Then A/B two played months on a copied save
  against the legacy lifecycle as well as the previous commit. Change `world_probe`'s `reform`
  check to require an `effort`.

**N7 — Any owner can hand a province to another nation without the recipient's consent (medium-high, Phase B scope; pre-existing).**
`transfer()` treats every `transfer_territory` by the owner as a cession (`cession = ev == owner`,
`src/world/compiler/territory_ops.cpp:45`), with no treaty and no signature from the recipient.
- *Reproduce, live Ukraine campaign month 1:* asked for a free trade agreement, the model wrote
  Poland's reply as "Poland signs a bilateral free trade agreement with Ukraine" with a single
  operation, `transfer_territory` of Subcarpathian Voivodeship to UA. The engine applied it, so
  the player received a Polish province, owner and controller, with no treaty and no answer.
- *Expected:* a cession to another existing state needs that state's signature, like a treaty
  `cede` clause (the consent Plan 26 B built).
- *Suggested bounded fix:* under realism, allow a direct `transfer_territory` only for annexing
  one's own occupation, returning land to its owner, or founding a new state. Route every other
  cession through a proposal with a `cede` term. Add a `consent` check.

### Minor defects

**N4 — The replacement establishment omits the first month's losses (low, Phase H).** `fund()` snapshots `establishment` from `cActors`, the post-casualty state (`economy.cpp:135`). Probe 5: 100,000 active → establishment 99,585. For the seeded Russia this is about 16,000 troops permanently below its January strength. Snapshot from `base.state` (or at migration in `beginTurn`) instead.

**N5 — Access treaties are invisible in the model's facts (low, Phase J).** `describeAgreement()` renders `end_conflict`, `cede`, `occupy` and `withdraw` terms but not `grant_access` (`agreements.cpp:61–73`). The UI shows it (`web/app.js:1061`). The model therefore sees access only if the treaty text happens to mention it. Add one line, "X grants Y military access".

### Tooling and calibration

**N6 — `world_probe`'s war scenarios no longer test the model (medium, verification gap).** Its four regions are about 1,100 km apart, so `captureEffort` is about 2 and Phase I asks for an effective ratio of 3.0 (`force::assault` on the probe fixture). With the fixture's 800,000-strong Portugal the ratio is 1.07; Spain's 133,000 against West is 0.65. Occupation is then correctly outside what either side may do: the reach facts say "too weak", and the model either falls back to `build` or `open_conflict`, or emits an `occupy` that the engine drops, so `conflict`, `invasion`, `finish_invasion` and `capital_push` scored 0/3 against 2/3 and 3/3 in the Phase D baseline. This is a stale fixture, not a model regression. With recalibrated forces, the invasion scenarios pass (see [Live model](#live-model)). Update the fixture (forces or region spacing) so that it again separates model comprehension from engine refusal.

Design note, not a defect: Spain, with 4.75× Portugal's active army, cannot take a single Portuguese border region in one month on either the synthetic or real-sized geography: a raw ratio of 1.29 against a 1.5 threshold. This follows from Phase I's documented decision (orders no longer take ground at 0.75:1) and from the 20% share of the pool sent on an offensive. It is consistent with the seeded wars' frozen fronts, but the calibration should be checked against a real short war before it is final.

## Live model

Qwen3.5-4B through `llama serve` (`-c 32768`, reasoning on), M1 Pro, 23 September 2026.

**`world_probe`, 15 scenarios × 3** (expected outcome / runs; Phase D baseline from `docs/MODELS.md`
in brackets):

| Scenario | Result | Note |
|---|---|---|
| construction, secession, long_history, capital, overreach, weak_raid, sea_move, overseas_landing | 3/3 each | `overseas_landing` [3/3], `sea_move` [3/3] |
| reform | 3/3 by the probe's check | but 0/3 started an effort (N3) |
| treaty_consent | 1/3 [2/3] | twice the model wrote no treaty operation |
| access_treaty | 1/3 [2/3] | twice no `grant_access` proposal |
| conflict, invasion, finish_invasion, capital_push | 0/3 each [2/3, 3/3, –, –] | fixture too weak under Phase I; the engine refuses (N6) |

With recalibrated forces (a scratch copy of the tool: Portugal at 2,500,000 active and Spain at
400,000 for `conflict`), `invasion` scored 3/3, `finish_invasion` 3/3, `capital_push` 1/3 and
`conflict` 0/3. Spain's 400,000 still fell just short, at 2.99:1 against the 3.0 needed; the facts
said "too weak" and the model complied. So the model follows the reach facts. Refusals come from the
engine, and the model does not ignore the rules.

Per scenario the prompt was 2,154–2,390 tokens and the adjudication schema 4.9–9.5 KB. Latency was
6–29 s, in line with the baseline's 17–22 s for comparable scenarios. There were no invalid outputs.

**Six-month Ukraine campaign** on a disposable save, through the real HTTP server. The orders were:
a trade proposal to Poland and an anti-corruption reform; the reform again, a declined AI offer and
a ceasefire proposal to Russia; the reform again, an accepted offer and "recapture all of Crimea and
the Donbas this month"; accepting a pending regional demand and a revised trade proposal; a quiet
month; and a power-grid investment.
- **Completion and replay.** All six months completed in 170–200 s each, with no failed or
  malformed turn. `replay_verify` on the export: "6 turns replayed identically without inference".
- **Model calls.** 6 adjudications: prompts of 10,978–16,782 tokens, up to 2,452 completion tokens,
  104–134 s, schema 38–53 KB. 42 decisions: 881–6,348 tokens, 3–20 s. 6 narrations, 2 chronicles.
  The call budget is unchanged: 4 reactive nations plus 3 independent slots a month.
- **Blanked outcomes (N3).** 0 of 58 adjudicated events carried `independent_outcome`, and 35
  had no operation. Per month, 8/11, 9/10, 8/12, 8/11, 1/7 and 3/9 model events were reduced to
  "No validated change resulted from the proposed action." All 11 of Ukraine's orders were blanked,
  and Ukraine never had an effort under way.
- **Treaties.** None of Ukraine's three treaty orders (Poland, Russia ceasefire, revised Poland)
  produced a proposal: the model narrated each without `add_commitment`. Poland's reply in month 1
  was instead an unconsented cession of Subcarpathian Voivodeship to Ukraine (N7). No AI–AI treaty
  formed in six months (N8).
- **Overreach.** "Recapture all of Crimea and the Donbas" gained nothing. It was blanked like the
  rest, rather than reported as a failed offensive.
- **Regional demand.** Accepting "Troop redistribution" produced no `respond_regional` operation
  and was blanked.
- **Scheduling (finding 14).** Russia, fighting the player, was served in months 1–4 and 6. In
  month 5 four nations owing treaty answers (Kazakhstan, Kyrgyzstan, Armenia, Belarus) filled the
  reactive cap, and those proposals could not settle (N8). Named nations (Poland, Russia) were
  served when named.

**N8 — AI nations never sign: they accept in prose, and proposals pile up (high, Phase B/J).**
The decision schema offers an optional `responses` array when a nation has `treaty_proposals`
(`src/inference/prompts.cpp:16–20`), and the instruction says prose conveys no signature. In the
campaign the model never filled it. Kyrgyzstan wrote "Accept the Kazakh trade agreement proposal
(k3-1, revision 1)" in months 4, 5 and 6, and Kazakhstan did the same for k5-2. Adjudication turned
each such action into another new proposal. After six months there were 8 pending proposals (6
between Kazakhstan and Kyrgyzstan) and no new commitment anywhere. The urgency scheduler ranks
treaty answers first, so these unanswerable proposals also crowd out war decisions (month 5 above).
- *Suggested bounded fix:* when `treaty_proposals` is non-empty, require `responses` with one entry
  per offered proposal (the answer may be `accept: false`). Stop the compiler from opening a new
  proposal when an equivalent pending one between the same parties exists: treat it as that
  proposal's continuation. A/B on a copied save.


## Cross-system sequences checked

- **Capture → annex → unrest (X1):** control and resistance survive annexation. Integration waits for peace with the claimant.
- **Ceasefire with one opponent while another attacks (probe 6, `conflicts`):** the uncovered pairs keep fighting and the covered pair takes no losses.
- **Treaty changes while proposals await answers (`consent`):** a concurrent change lapses the amendment, and a stale revision is rejected.
- **Third-party occupation during cession (`access`):** the claim moves; the occupier stays unless it signs.
- **Capture and recapture during construction (`access`):** the project pauses, keeps its reservation, and resumes on the new schedule.
- **Flight, onward flight, death and return across changing borders (`status` displacement):** conserved; the ledger never exceeds its hosts.
- **Simultaneous fronts on one inventory (`status` forces, X3):** conserved. Note N1.
- **Repeated orders and retries (`consent`, `lifecycle`, `turn`):** no double settlement, migration, effort or payout.

## Idle seeded world (`status_tests`, plan 26 lifecycle)

After 12 months the Russo-Ukrainian War has killed 194,879 and displaced 886,605. After two idle
years, occupied Ukraine is at 57.4/100 unrest and the most restive region elsewhere is West
Kordofan at 23. The fronts, toll and displacement over 36 months match each phase's recorded run.
These are gameplay invariants, not a claim of historical accuracy.

## What remains

Before Plan 26 is marked done:
1. Fix N3 and re-measure on the live model. It blanks most narrative outcomes and makes the
   Phase H effort path unreachable.
2. Fix N8. Without it no AI–AI treaty can form, and pending offers crowd out urgent reactions.
3. Fix N7. It lets a model grant land without the recipient's consent, which Phase B set out to
   prevent.
4. Fix N1 and N2. Both break Phase C/F/J acceptance conditions as written.
5. Recalibrate `world_probe`'s war and reform checks (N6, N3) so that the live matrix measures the
   model again, then rerun the six-month campaign above and compare.

Can follow as polish: N4 and N5.

Design trade-offs recorded as known limits, not defects: region-grained sea graph, no naval
interdiction or carrier aviation, no maintenance drag, procurement only replaces losses, no terrain
or area data, no defender entrenchment, a resistance keeps what it holds, refugees flee only to
bordering regions, and evacuation of paused military projects.

## Verification limits

- The live model is a 4B local model on one machine. Three repeats per scenario and a six-month
  campaign show tendencies; they are not reliability estimates.
- The Phase D baseline in `docs/MODELS.md` used the same tool and model, so the comparison is fair
  except for the war scenarios (N6).
- No external historical calibration was done. Passing an invariant does not establish realism, and
  finding no further failures does not prove there are none.

To reproduce the deterministic probe after `make`:

```sh
c++ -std=c++20 -Iinclude -I/opt/homebrew/include \
  docs/audits/2026-09-23-realism-reaudit-probe.cpp build/libhistoria_core.a \
  -L/opt/homebrew/lib -ljsoncpp -lsqlite3 -o /tmp/localhistoria-reaudit-probe
/tmp/localhistoria-reaudit-probe
```
