# 25 · Political systems, unrest, rebellion and breakaway states

**Status: done 2026-09-22.** `make test` 8/8, `python3 tests/integration.py` passes with 6 turns
replaying identically, `make format-check` clean.

Do plans 22, 23 and 24 first — all are done.

## Goal

Give every nation a political system, seed it from January 2026, feed it to the model and the UI,
and give stability something to do. A region accrues **unrest**, sustained unrest raises real
**rebels**, a rebellion that holds ground becomes a **breakaway state**, and a government that
cannot put one down is replaced.

## Context

Nothing in the state said how a nation was governed. A 4B model asked to adjudicate "hold early
elections" in Saudi Arabia, or "the opposition wins a majority" in North Korea, had nothing in its
facts to push back with.

And stability was a dead end. It rose and fell, it multiplied a defender's strength by 0.8 below 25
(`src/status/force.cpp:48`), and that was all. A nation could sit at 19/100 — the label is
"collapsing" — for a decade of campaign months and nothing would ever happen inside it. Meanwhile
plan 23 gave regions agendas, relations and demands, and plan 17 gave them a slot to voice them in,
but a demand the capital ignored had no consequence at all. Both gaps close here: unrest is what an
unanswered demand turns into.

### The measurement problem that shapes the whole design

`stability` is the World Bank's WGI **Political Stability and Absence of Violence**, a *percentile
rank*. It is not regime fragility, and the seeded values say so plainly:

| | score | | score |
|---|---|---|---|
| North Korea | **71.4** | France | 61.8 |
| Saudi Arabia | 65.1 | United States | 64.3 |
| Yemen | 18.9 | Mali | 23.3 |
| Syria | 25.4 | Somalia | 27.2 |

A "stability below 30 → rebellion" rule would ignite the entire Sahel, Yemen and Somalia in the
first year of every campaign — rewriting the seeded world before the player had done anything —
while leaving North Korea permanently untouchable. Plan 22 already recorded this: 71.4 for North
Korea is upstream and correct, because a closed authoritarian state really does score well on the
absence of organised violence.

So unrest is **earned**, never inherited. It needs the product of two things:

1. the controlling nation has fallen at least 8 points below `seeded_stability`, the January figure
   frozen at seeding (`baseline_stability` cannot serve — it ratchets with every model effect at
   `recovery.cpp:101`); and
2. the region itself has a grievance this campaign produced.

A country that is chronically low but *steady* is at its equilibrium, and this leaves it there.

### Rally, don't revolt

A region that is a front in a war its own controller is fighting contributes **no** grievance.
Invaded populations rally; occupied ones resist. Without this rule Kharkiv and Zaporizhia revolt
against Kyiv, which is exactly the ahistorical behaviour plan 24 was rejected for the first time
round.

The same trap caught the first draft twice more:

- **Displacement.** A standalone `grievanceDisplaced` term looked reasonable and turned every front
  region against its own defender, because almost all displacement is war damage. It now only
  compounds occupation, inside the `owner != controller` branch.
- **The grievance cap.** At 3.0 the most an unoccupied region could present was 2.0 (strained
  relations plus two pending demands), so a junta's threshold of `70 + 0.8 × 25 = 90` was
  unreachable and no revolt could ever happen under the regimes that most deserve one. The cap is
  2.0: soured relations alone are 1.0 and top out below the revolt line however far a nation falls,
  so it takes grievance the capital has actually refused to answer.

### What the seeded world does

Two idle years of the real January 2026 world, no orders and no model, asserted in
`tests/status/real_world.cpp`:

| Seeded situation | Expected | Actual |
|---|---|---|
| Russo-Ukrainian War | No revolt; Ukraine's front regions raise nothing against Kyiv | as expected |
| Sudanese Civil War, Myanmar Civil War, Kivu Conflict | No new rebel actor, wars run exactly as before | as expected |
| Yemen 18.9, Mali 23.3, Syria 25.4, Somalia 27.2, Afghanistan 28.2 | Nothing: steady, and no regional politics recorded | as expected |
| Anywhere | No breakaway state, no new conflict, no government falls, no domestic event | as expected |
| Most restive region | — | **West Kordofan at 11/100**, RSF-held, far below the 35 reporting line |

West Kordofan and occupied Donetsk stir faintly because Sudan's and Russia's own stability slides
while they fight, which opens the gate a crack for the land they hold. That is the mechanic working
— an occupier bogged down in a long war is precisely who an occupied population turns on — and at
11/100 it is reported nowhere, raises no stake and does nothing. The war calibration from plans 19
and 21 is untouched: the Russo-Ukrainian fixture still lands inside its 80,000–300,000 band.

## 1 · The political system

`data/source/governments_2026.json`, curated like `military_2026.json`: 239 rows of
`{system, since, source}`, every row naming its basis, and rows that depart from the CIA World
Factbook's formal label saying why in their own note (Russia and Belarus by the absence of
competitive turnover, Cambodia by the CPP's monopoly). Twelve values, generated by a reviewable
per-system mapping rather than hand-typed:

| count | system | count | system |
|---|---|---|---|
| 51 | `presidential_republic` | 21 | `dominant_party_state` |
| 49 | `parliamentary_republic` | 15 | `semi_presidential_republic` |
| 36 | `dependent_territory` | 6 | `absolute_monarchy` |
| 35 | `constitutional_monarchy` | 6 | `one_party_state` |
| 6 | `military_junta` | 6 | `transitional_government` |
| 5 | `uninhabited_territory` | 3 | `theocracy` |

It lands in the **fast** import path (`Makefile:18`, the `data/.stats-imported` target): seconds of
Python, no geometry rebuild. `country_stats` gains `government` and `government_since`, and because
`loadStatusSeed` (`campaign_factory.cpp:20`) does `SELECT *` and strips only `code`, no C++ change
was needed to load it.

`src/status/regimes.cpp` gives each system a `vent`, a `repression` and an `armedFirst` flag.
**Deliberately, none of it touches `driftStability`**: the seeded stability numbers are real
observations, and a regime-dependent baseline would pull all 239 nations off their January values on
month one. The regime modulates the new layer and the prose the model reads, nothing else.

## 2 · Unrest and what it does

A new step 8 in `applyMonthlyStatus` (`src/status/monthly/unrest.cpp` and `rebellion.cpp`), after
`driftStability` so it reads the stability the month finished with. Rebel ground-taking runs earlier,
at **step 4c** beside `advanceFronts`, so the toll, displacement and front drawing treat a rebel
seizure exactly like any other — a seizure at step 8 would displace nobody, ever.

`territories[r].unrest` drifts toward `100 × stress × grievance / 2`, closing
`10% × (1 − 0.8 × vent)` of the gap rising and `20% × (1 + vent)` falling. Formulas, weights and the
full rebellion, secession and collapse rules are in
[docs/EVOLVING_WORLD.md](../docs/EVOLVING_WORLD.md#national-status-and-war); every constant is named
in `src/status/status_internal.h`.

`advanceConflict`'s refusal to move insurgency and civil-war fronts (`fronts.cpp:67-73`) **stays
exactly as it is** — its reasoning is right, an inventory ratio walks Congo's army into M23's Kivu.
Rebel ground moves on its own rule instead: population with it, and a garrison below 0.004 troops
per resident.

The model-facing independence gate (`territory_ops.cpp:46-60`) is also untouched. It already permits
independence once a relevant conflict is established, so an adjudicated secession and an
engine-declared one now converge on one rule rather than competing.

### Engine-owned by construction

`status.government` and `territories[].unrest` cannot be written by a model delta:
`checkActorChange` (`delta_checks.cpp:32`) requires everything outside `name`,
`identity_revisions` and `flag_revision` to compare equal, and `checkTerritoryChange` allows only
`owner` and `controller`. The one exception is deliberate and narrow — a `change_identity` with
`change_type: refounding` may set `government`, and only when it also founds a new identity
revision, with every other status key still compared equal. `define()` in `schema.cpp` makes every
property required, so the field is carried by an explicit `"none"`, the same idiom as `blocked_by`.

## 3 · Surfaces

- `buildStatus` (`helpers.cpp:84`) is one place feeding both the API and every model prompt via
  `simulation.cpp:282`; `government`, `government_since` and a conditional `unrest` go there.
- New `domestic` event category for `unrest`, `revolt`, `secession` and `regime_change`. Engine
  events never pass through a delta, so `campaign()` appends the structural ones to **What changed**
  alongside `front_advance`, which already worked this way.
- Inspector tile, per-region unrest in the regional-politics block, the political system in the
  country list, and a fourth **Stability & unrest** map mode with its own legend and tooltips.
- `regionStake` (`politics.cpp:46`) raises for a restive region, so the month's one regional voice
  goes where something is boiling.

## Verification

- `tests/status/unrest.cpp`: the gate from both sides, regime differentiation (a one-party state
  stores more than twice what a republic vents over the same year, and cools slower), the Ukraine
  rally regression, the whole chain end to end with personnel conserved between army and rebels, the
  one-a-month caps, version gating at `unrestVersion - 1`, and byte-identical determinism.
- `tests/status/real_world.cpp`: the table above, plus every actor classifying and the existing war
  calibration unchanged.
- `tests/politics_tests.cpp`: a refounding sets the system and nothing else in status; a correction
  and an invented system are refused; a restive region takes the regional slot.
- `tests/data_test.py`: all 239 rows classify into the twelve values, plausible `since`, and a
  sanity check on the spread so a future curation drift is caught.
- `tests/browser.mjs`: the inspector tile, the list badge, the fourth map mode and its legend.
- `python3 tests/integration.py`, including the legacy-save path, which now also exercises
  `seedGovernment`/`government_seed` and still replays byte-for-byte.

## Cost

Measured, not estimated:

| | before | after | added |
|---|---|---|---|
| Adjudication schema (5 actors, 20 regions, 8 actions) | 13,958 B | 14,284 B | **+326 B** |
| Prompt instruction text across `decide`, `adjudicate` and the operations reference | 10,636 B | 11,005 B | **+369 B** |

Against the plan-24 baseline of a 99–194 KB schema and a 6,047–12,420 token adjudication prompt,
that is well under 1% of the schema and roughly 1–3% of the prompt, before the per-actor facts,
which gain `government`, `government_id` and `government_since` (about 60 B an actor) plus `unrest`
only where a nation actually has some.

Measured live on Qwen3.5-4B over five played months: the largest adjudication was **10,579 prompt
tokens** and **12,677 total**, against a 32,768 context, and llama.cpp reported `truncated = 0` on
every request of the campaign. Months took 140–180 s, inside the 159 s / 212 s plan-24 baseline. The
context size does not need raising, which matters because the Windows target is a 6 GB RTX 3060 the
model only just fits on.

## Calibration against a played campaign

Five months were played as **Myanmar** (military junta, seeded stability 30.2, already in a civil
war) with deliberately ruinous orders. Three things came out of it, two of them faults.

**1. The adjudication prompt additions broke the 4B.** With the text as first committed, months 2
and 3 both came back with the previous month's event texts copied verbatim and the actual orders
ignored. It was not truncation — `truncated = 0` throughout. Replaying the identical save and orders
against the pre-plan-25 prompt adjudicated them correctly, which isolated the cause to the three
sentences this plan added to the `adjudicate` instruction block. Trimmed to one sentence, and the
sentence about answering regional unrest dropped entirely (unrest is engine-owned, so the model
could not act on it anyway), two further months adjudicated every order correctly and distinctly.
The lesson is the one `docs/MODELS.md` already states and this plan underestimated: on a 4B,
instruction text is not free, and adding to the adjudication block costs accuracy, not just tokens.

**2. `unrestStressCap` was set above anything the month step can produce.** Stability reverts toward
`baseline − war` by `stabilityReversion` (8%) a month and so never arrives. Over two campaign years
that means a medium-intensity war produces a fall of 6.9 — it never reaches `unrestDrop` at all —
a high-intensity one 13.0, and a high-intensity war with a quarter of the population occupied 21.6.
With the cap at 30, two years of high-intensity war produced a maximum unrest target of 23/100,
below the 35 an unrest event is even reported at: the entire chain was unreachable outside a
synthetic test that forces the deficit directly. The cap is now **22**.

**3. `unrestRise` was too slow for a target that opens slowly.** Even with the cap corrected, the
worst sustainable war reached only 29.3/100 after 24 months, still short of being reported, because
a nation needs about six months of it before clearing `unrestDrop` at all and unrest then chased the
remainder at a tenth of the gap a month. It is now **0.16**.

`revoltMonths` and `revoltThreshold`, the other two constants the plan flagged, were left alone: the
binding constraint was the height of the target, not the time spent above a line or the line itself.

After both changes the played campaign behaves: by month 5 Myanmar had fallen 30.2 → 19.6, past the
gate, with Saigang at 0.8 and Kachin at 0.1 and climbing. `tests/status/unrest.cpp` gained a
reachability case that drives stability down only through `driftStability` — no forced deficit — and
asserts an aggrieved province reaches the reporting line, so a future change that puts the top of
the scale out of reach again fails the suite.

The idle seeded world was re-checked at every step. Its most restive region moved from 11 to 23 of
100 (RSF-occupied West Kordofan, in a live high-intensity war in which Sudan's own stability falls
31.3 → 18.5). That is the mechanic reading a real war correctly, and it still surfaces nothing: no
unrest event, no rebel, no breakaway, no government change, and `unrest_months == 0` everywhere. The
idle-world assertion was rewritten to say what is actually required — a peak below the 35 reporting
line, plus Yemen, Mali, Syria, Somalia and Afghanistan at exactly zero — rather than a bare
threshold that happened to hold.

**Still open.** A first rebellion was not reached in five months and, on this evidence, needs one to
two campaign years of sustained misrule plus a region whose relations actually sour to `strained`;
grievance stayed low in the played campaign because the regional slot kept Saigang at `neutral`.
Whether the regional initiative sours relations often enough for the chain to complete in a normal
campaign is the remaining unknown, and it is a plan-23 question about how demands and relations
evolve rather than an unrest-calibration one.

## Notes

- Existing saves are upgraded in place by `hasGovernment`/`seedGovernment`, frozen as
  `reference.government_seed` and reapplied by `replay_verify`, mirroring `military_seed` from plan
  19. Their `seeded_stability` is taken from wherever stability stands at the upgrade, not from
  January, so a long-running save's gate is measured from that point.
- `tests/military_tests.cpp` asserted the front-momentum gate as `statusVersion - 1`. That stopped
  meaning what it said the moment `statusVersion` moved to 4; it now names `frontMomentumVersion - 1`
  directly.
- `src/world/schema.cpp` is 392 lines, over the 350-line ceiling in `docs/ARCHITECTURE.md`. It was
  already 383 at plan 24, and this plan added 9. Splitting it is its own change.
