# 28 · Consequential events, war report, answerable demands, occupation unrest

**Status: implemented 2026-09-24 (phases A–D), not yet committed.** Plan 27 is in `docs/PLAN_27.md`.

## Implementation notes (where the work departed from the draft)

- **A — false signings.** The A/B showed the model labelling a pact it never wrote as `policy`
  and narrating it as signed. Version 2 therefore appends "No agreement was recorded." to any
  outcome with no successful operation whose description narrates a signing, treaty, pact, accord,
  agreement or alliance (`claimsAgreement` in `events.cpp`), world events included. Results are in
  `docs/MODELS.md`.
- **A — prompt.** The sentence about `outcome_kind`/`effects` was replaced rather than extended,
  so the prompt did not grow.
- **A — schema.** Required effects alone mostly came back as `none`/`none` (8 of 19 in a live month),
  leaving the consequence to the C++ default. Each entry must now move an axis, and the list is
  named `outcome_effects` so it sorts after `operations`. Sorted before them, it cut the ordered
  treaty and war operations to 4 of 12. The provider renames it back to `effects`; world events
  keep `effects`.
- **A — bounds.** A `policy` or `security` outcome can cost the economy but never grow it, so a
  reform labelled as a policy earns no growth without paying for an effort.
- **A — slight effects.** Forced to move an axis, the model often rated routine acts `decline`
  (−2 stability), so talks and decrees would have added up to a crisis. A `security`, `policy` or
  `negotiation` outcome without a successful operation records `slight_decline`/`slight_growth`
  (−0.15% / −1 and +0.1% / +0.5 in the status version 5 scale). The model's vocabulary is unchanged.
- **A — cut descriptions.** A description the 160-character cap cut mid-sentence ends in "…"
  before "No agreement was recorded."
- **C — gating.** Demand deadlines and settlement ride on event text version 2 (the compiler
  version), not a separate flag.
- **C — browser test.** The integration flow never gives a regional slot to a Portuguese region,
  so the demand card, chips and response payload are checked on a demand staged in the page. The
  once-a-month popup is checked on the real treaty offer; server-side demand validation in the HTTP
  part.
- **D — armed movements.** The seeded RSF and M23 carry a `status`, so "a controller without
  status" matched nothing. An armed movement is an actor that owns no land and is not a rising the
  game raised.
- **D — validation.** The real-world status test (24 idle months on the January 2026 world) now
  also asserts that movement-held land is restive short of revolt. That replaces the separate probe.
- **Tests.** Fixing `tests/integration.py` for the Somaliland/Northern Cyprus actor count (242 →
  244) was needed before any of this could be measured.


## Context

Playing the current save (`saves/campaign.sqlite`, Angola, 4 months) exposed four problems:

1. **Blanked events.** In month 4 the model wrote "Namibia deploys 200 Border Police officers to the Caprivi border sector" with `outcome_kind: "none"` and no operations. `src/world/compiler/events.cpp:90-94` replaced it with "No validated change resulted from the proposed action.", and the recap repeated that line. Every negotiation without a treaty op is likewise replaced with "Preliminary diplomatic talks took place; no treaty offer or agreement was recorded." (`events.cpp:115-116`). That is 9 events in 4 months. The user's rule: **every event must do something grounded** (a slight political or economic change, up to a major one like a war), rated by the LLM from history and context, never a blank.
2. **War tolls are buried.** They sit inside "Authoritative events" (`web/app.js:490-510`). They should get their own collapsed section, which also lists **provinces that changed hands** that month.
3. **Regional demands look like debug output and cannot be answered.** North-Western (Zambian, occupied by Angola) has `{status:"pending", text:"sovereign_recognition", id:"t2-…"}`. It is printed raw at `app.js:381`. There is no popup, no button and no expiry (`delta_checks.cpp:95-100` allows only pending/accepted/rejected). The player can only answer via free text. Accepting has no mechanical effect, and a pending demand adds unrest indefinitely. (The save has no treaty offers: `proposals` is `{}`. The "treaty" the user saw was this demand.)
4. **Occupied regions look stable.**
   - Unrest does rise toward an occupation target of 48–56 (`unrest.cpp:94-116`). But the rise rate is scaled by the *occupier's* vent (`unrest.cpp:139`), so Angola (presidential, vent 0.8) moves 5.8%/month: North-Western is at 11.8 after 4 months, and Russian-held Donetsk is at 17.7.
   - Below 35, the map fills a region with the **occupier's** national stability colour (`app.js:174-194`).
   - Regions held by non-state actors (North Kivu/M23, Darfur/RSF) never accrue unrest at all, because `accrueUnrest` skips controllers without `status` (`unrest.cpp:131`).

User decisions: a player-controlled region's demand lapses after **3 months** unanswered, and a lapse counts as a refusal. Events must always carry a consequence.

Follow `plans/README.md` conventions: `make test`, `python3 tests/integration.py`, replay determinism, docs updated, never touch `saves/campaign.sqlite` (copy it for live checks). Memories to honour:
- the 4B model skips optional schema fields, so require them;
- the adjudication prompt is crowdable, so A/B it on a copied save;
- `world.sqlite` churns, so restore it before committing;
- validate the unrest numbers against the real seeded wars.

Commit each phase separately. Add `plans/28-…md` with this content and a row in `plans/README.md`.

---

## Phase A — Every event has a grounded consequence (engine + schema)

Gate on a new `event_text_version = 2`. Set it in `lifecycleReference` (`src/core/lifecycle.cpp:83`), accept it in `lifecycleRules` (`:96-99`), and copy it into `w.engine`. Old turns replay unchanged.

**Schema** (`src/world/schema.cpp:292-312`), under version 2:
- Replace `none` in the `outcome_kind` enum with two kinds that cover the missing cases:
  - `security`: deployments, martial law, policing;
  - `policy`: laws, decrees, appointments, statements.
  - The enum becomes `reform, investment, negotiation, protest, security, policy`. Keep it after `operations`; the ordering note at `:304-307` explains why.
- Make `effects` required with `minItems: 1`, as the `development` schema already does at `:421-424`.
- Update the comment at `:293` ("Optional: most events change nothing").
- The effect vocabulary (`severe_decline … strong_growth`) and its bounded scale (`monthly_status.cpp:238-262`) stay as they are.

**Compiler** (`src/world/compiler/events.cpp`), under version 2:
- `supportedIndependent` (`:179-182`) gains `security` and `policy`, so the author's rated effect is kept.
- Per-kind bounds, alongside the existing ones at `:198-218`:
  - `security` and `policy`: at most `growth`/`decline` on each axis (no `strong_*`/`severe_*` without a map change).
  - `negotiation`: stability only, at most `growth`/`decline`. Talks read at home as progress or weakness. Replace today's zeroing at `:198-199`.
  - `protest`: unchanged.
- **Deterministic fallback.** If the model's effects are unusable after filtering, C++ applies a default for the author and records `effect_source: "default"` on the event for the audit. Defaults:

  | Kind | Default effect |
  |---|---|
  | `security` | stability growth, economy decline (cost) |
  | `policy` | stability growth |
  | `negotiation` | stability growth |
  | `protest` | stability decline |
  | `reform` / `investment` | starts its effort (existing path) |

- **Text.** Drop both replacements at `:90-94` and `:115-116` for version 2: a happened, non-failed event keeps the model's description. A negotiation that recorded no proposal gets the suffix " No agreement was recorded." after the description.
  - The failed/partial texts (`:79-89`) stay.
  - The factual proposal text (`:95-114`) stays.
- **Bug fix.** The dead `event["major"] = false` at `:93` is overwritten at `:127`; remove it.
- Under version 2 the `outcome_kind == "none"` branch no longer exists. Keep it for version ≤1.

**Prompt** (`src/inference/prompts.cpp`, adjudication instructions): add one short sentence. It says every outcome has a consequence the model rates from the facts and history, and that a minor act gets a minor effect.
- Keep it to one sentence (the prompt is crowdable).
- A/B on a copied save against the previous commit, as `docs/MODELS.md:127-135` did. Measure:
  - events with `effect_source: "default"` (target: rare);
  - no echo of the previous month;
  - latency and output tokens;
  - treaty ops not lost to the `negotiation` label.

**Tests**
- `tests/consent_tests.cpp:136,237-250`: version 1 keeps the old strings. Add version-2 cases: description kept; negotiation suffix; default effect applied when effects are `none`; bounds clamp `strong_growth` for `security`.
- `tests/integration.py:141` currently asserts the "No validated change" string; update it to the version-2 behaviour.
- `world_probe`: `reform` still starts an effort.

**Docs:** `docs/EVOLVING_WORLD.md:113-121` ("Outcomes without a map change") and `docs/MODELS.md` (new A/B table).

---

## Phase B — War report section in the journal

**Server** (`src/server/campaign_handlers.cpp:22-39`): add `month["territory_changes"]` as `[{region, name, owner_before, owner_after, controller_before, controller_after}]`.
- It comes from diffing territory `owner`/`controller` between the month's saved `turns.state` and the previous month's (`initial_world` for turn 1).
- To avoid parsing 1.2 MB states on every GET, compute it in `Store::commit` (`src/store/store.cpp:141-180`) from the input and result worlds, and save it in the turn's `reference` as `territory_changes`.
- The journal reads it from there. For older turns that lack it, fall back to a one-time diff in `Store::journal()`.
- This covers every path: delta seizures and transfers, `front_advance`, rebel takes, occupation `expiry` returns and all regions of a `secession`. Today only some of these reach `changes` (`simulation.cpp:260`, `rebellion.cpp:194-210`).
- Check that `replay_verify` ignores or reproduces the new reference key. It is derived from states, so it is deterministic.

**UI** (`web/app.js` `load()`, `:483-511`):
- Stop nesting tolls in `details.events`.
- Add a sibling `<details class="war">`, collapsed by default and remembered open per turn like `expanded` at `:451`. Its summary is e.g. "War report · 5 wars · 2 provinces changed hands". It contains:
  - toll lines (unchanged text);
  - a "Changed hands" list: "North-Western (Zambia) — taken by Angola", "… — returned to Zambia", "… — annexed by X", or "… — independent as Y", built from before/after owner and controller using `actorName`.
- Omit the section when both lists are empty.
- Remove `kind: territory` entries from the "What changed this month" list (`:465-475`) so they are not duplicated.

**CSS:** style `details.war` like `details.tolls` (`web/style.css:287-291`).

**Tests:** in `tests/browser.mjs:261-263`, move the "Portugal → Tagus Republic" check into the war section's changed-hands list, and assert the section is closed by default and contains a toll line. `tests/integration.py:142,148`: add a `territory_changes` assertion for the integration war.

**Docs:** `docs/API.md` journal entry fields.

---

## Phase C — Answerable, expiring regional demands and a decisions popup

**Engine**
- **Status set** (`delta_checks.cpp:95-100`): add `expired`.
- **Creation** (`identity_ops.cpp:12-33`): record `answer_by_turn`:
  - 3 months (`turn + 3`) when the region's controller is the player;
  - 6 months (`rate::demandAgeMonths`) otherwise.
  - Old demands without the field default to their id turn plus the same window.
- **Answer or lapse.** Reuse the existing `respond_regional` path (`identity_ops.cpp:34-50`). It applies at most one relations step per region per month:
  - `accepted`: relations one step better (strained→neutral→cooperative);
  - `rejected` or `expired`: one step worse.

  So ignoring a demand is never better than refusing it. Pending pressure (`unrest.cpp:118-126`) stops once the demand is answered or expired, and the relations change carries the consequence forward through `occupationPressure`/`regionalGrievance`.
- **Settlement at month start.** Run a trusted settlement in the compiler beside `settleExplicitProposals` (`compiler.cpp:14,50-96`, pattern at `proposals.cpp:27-88`, `s.trusted`). It:
  - applies the player's structured answers;
  - expires demands past `answer_by_turn`;
  - lapses a demand whose region changed controller since creation. The addressee is gone, so it counts as `expired` with `lapse_reason`, without the relations penalty.

  It emits one factual event per answer, e.g. "Angola granted North-Western's demand for sovereign recognition." or "North-Western's demand for sovereign recognition lapsed unanswered."
- **API** (`campaign_handlers.cpp:188-211`): extend `responses` items to allow `{demand, region, accept}` next to `{proposal, accept, revision}`. Validate that:
  - the region's controller is the player;
  - the demand is pending;
  - there are at most 8 responses in total.

  Pass them through `refs` like `proposal_responses`, and update `docs/API.md:11,65`.
- **Prompt:** no change. The adjudicator's `respond_regional` stays available for free-text answers.

**UI** (`web/app.js`)
- **Regional politics** (`:376-382`): add a `humanize(text)` helper next to `kindLabel` (`:1055`) that turns `_`/`-` into spaces and uppercases the first letter. Use it for `agenda`, `relations` and demand text, and never print ids.
  - A pending line reads e.g. "Demands sovereign recognition · raised Nov 2026 · answer by Feb 2027".
  - Answered and lapsed demands read "Granted", "Refused" or "Lapsed".
  - For the player's regions, show Grant/Refuse chips. They reuse the proposal answer store (`readAnswers`/`setAnswers`, `:751-762`, keyed `demand:<id>`) and are sent in `responses` by the End-month handler (`:827-829`).
- **Decisions box:** rename `#proposals` (`web/index.html:91`) to a "Needs your decision" box. It lists treaty offers (`renderProposals`) and pending demands from the player's regions, with the same chips.
- **Popup:** on `load()`, if unanswered offers or demands exist that the player hasn't seen this turn, open a `<dialog>` listing them with the same cards. Remember "seen" in sessionStorage per campaign and turn, following the `readAnswers` pattern. Also show a count badge on the controls.
  - Build the cards once with a shared function so the dialog and the box stay in sync.
  - Use the `showModal` pattern of the settings/diplomacy dialogs (`:647`, `:1223`).
- **Notes:** "Lapses at the end of <month>" for demands; the treaty note stays.

**Tests**
- `tests/politics_tests.cpp:166-234`: demand gets `answer_by_turn`; lapse after 3 months for a player region and 6 for AI; lapse applies one strained step; grant improves relations; a controller change lapses without penalty; an invalid response is rejected by validation.
- `tests/consent_tests.cpp`: a mixed proposal and demand `responses` body validates.
- `tests/browser.mjs`: the popup opens once per turn, Grant is sent in `responses`, and no raw id or underscore appears in `#regional-politics`.

**Docs:** `docs/EVOLVING_WORLD.md` regional politics paragraph, `docs/API.md`.

---

## Phase D — Occupied regions are visibly less stable

Gate on `statusVersion = 5` (`include/localhistoria/status.h:101-110`) with a new `occupationUnrestVersion = 5`, checked inside `MonthlyStatus` as `unrestStep` checks `unrestVersion`.

**Faster occupation climb** (`unrest.cpp:137-140`): when `occupationPressure(id)` sets the target, the rise uses a fixed `rate::occupationRise` (start at 0.30, in `status_internal.h`), not the occupier's vent. Vent models how a regime channels its own people's grievances, not a foreign population's. The fall rate is unchanged.
- Expected climb:
  - North-Western (target 56): 17 → 28 → 36, "restive" by month 3.
  - Donetsk under a Russian garrison (target 48): 14 → 24 → 31 → 36.

**Seed long-held occupations.** In `beginTurn`'s migration (`lifecycle.cpp:93-130`, beside `recordControl(w, true)`), start `cause: "recorded"` regions at their occupation target once, instead of at 0. A region occupied since 2014 or 2022 should not start at 0.

**Non-state controllers** (`unrest.cpp:131`): don't skip a controller without `status` when the region is under hostile control. Compute `occupationPressure` with stress 0 and a neutral rate, so North Kivu and Darfur accrue unrest.
- Check this against `represented()` (`:191-207`). Their own civil war already represents a rising, so no duplicate rebellion forms.

**Validate against the seeded wars** (per memory):
- Run the conquest probe style of `tests/status/conquest.cpp:121-158` plus a 12-month run on a copied real save.
- Occupied Ukraine should stay "restive"/"unrest" (35–70), not revolt.
- Rebel-held Darfur and North Kivu should stay short of a new rebellion.
- Record the numbers in `docs/EVOLVING_WORLD.md:425-470` (replacing "two years under a stable occupier → 46").

**Map** (`web/app.js:174-194`):
- Under hostile control, a region with unrest below 35 gets a distinct "Occupied" fill (new legend row at `:1013-1022`), not the occupier's national stability. Fix the comment at `:175` ("owner's" → "controller's").
- Tooltip (`:900-907`): "Occupied by Angola · unrest 11.8/100".
- Regional politics (`:379`): always show unrest for occupied regions, not only when it is ≥35.

**Tests:** in `tests/status/conquest.cpp`, the occupation rise no longer depends on occupier government, and the ≥35-after-24-months bounds get tightened to the new timing. Also cover the seeded migration value, a non-state controller accruing unrest, and a legacy `statusVersion 4` replay staying unchanged.

---

## Test time budget (applies to every phase)

Measured 2026-09-24 (Debug build, at the plan's base commit): `make test` runs its suites in 46 s. `status` takes 32 s and `military` 13 s, because both load the real `data/world.sqlite` and loop 12–120 months over it (`tests/status/real_world.cpp`, `tests/status/main.cpp`, `tests/military_tests.cpp`); every other suite takes under 0.4 s. (An earlier draft misread CTest's cost-data file as 0.34 s.) The other slow parts are live-model play, multi-month runs and the browser integration run. Rules:

- **Unit suites stay fast.** New cases use the small fixture worlds already in each suite (`tests/status/conquest.cpp` `world()`, `tests/consent_tests.cpp` `fixture()`, `tests/politics_tests.cpp`). Do not load the real world or loop many months in CTest.
  - New cases add under 1 s to their suite; nothing in this plan makes `status` or `military` slower.
  - Speeding up the two real-world suites is separate work, not part of this plan.
  - Add `set_tests_properties(<suite> PROPERTIES TIMEOUT 60)` for every suite in `CMakeLists.txt`, so a hang fails instead of running forever.
- **Integration.** Add assertions to the turns `tests/integration.py` already plays (mock model), and to the existing `tests/browser.mjs` flow. Add no new played months or new server restarts.
  - Time it before and after with `time python3 tests/integration.py`; it must not get more than 10% slower.
  - Every Playwright wait gets an explicit short timeout (≤5 s), not the default.
- **No live model in automated tests.** The 12-month unrest validation (Phase D) runs the monthly engine only: `beginTurn`/`finishTurn` with an empty delta on a copy of the real world. It is a one-off probe in `docs/audits/` like `2026-09-22-realism-probe.cpp`, not a CTest, and should finish in seconds.
- **Live checks are capped and backgrounded.**
  - The Phase A prompt A/B: 2 months per arm on a copied save.
  - The final playtest: 2 months.
  - Each runs in the background with a `timeout` wrapper (about 10 min per arm) and is watched rather than blocked on. If a month exceeds the recorded baseline (about 210 s), stop and report instead of waiting.
- **No geometry rebuild.** None of this touches the regions schema, so the ~20 min geometry rebuild must not be triggered.

## Verification

1. `make test` (C++ suites + data test), then `python3 tests/integration.py` (browser included), then `./build/replay_verify` on the integration export.
2. Copy `saves/campaign.sqlite` to the scratchpad and play 2 months as Angola on that copy.
   - No "No validated change" or "Preliminary diplomatic talks" text; every non-failed event has an effect or effort. Count `effect_source: "default"`.
   - The journal shows a collapsed War report listing tolls and any provinces that changed hands.
   - The popup lists North-Western's demand with humanized text and Grant/Refuse. Answer it, and check the factual event and the relations change. Leave another demand unanswered for 3 months and confirm it lapses.
   - North-Western's unrest climbs about 10 points a month, and the map shows it as Occupied, not in Angola's stable colour.
3. A/B the adjudication prompt (Phase A) as in `docs/MODELS.md`, and record the table.
4. Restore `data/world.sqlite` if tests rewrote it before committing.
