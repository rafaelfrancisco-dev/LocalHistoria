# 23 · Meaningful initiative slots

**Status: done (2026-09-22).** `make test` 8/8 and `python3 tests/integration.py` pass. Live
`tools/living_world_probe.py --country BR --months 6`: 6/6 regional slots on a region with stake, 0 ceremonial,
0 repeated background developments, 6 turns replayed identically; numbers in `docs/VERIFICATION.md`. Three
deviations from the plan as approved are marked **As built** in the steps below.

Do plans 17 and 18 first — both are done.

## Context

A month has exactly **three** independent slots (2 national + 1 regional, `src/core/politics.cpp:96,105,122`)
and each one costs a `decide` call plus an adjudicated outcome. In the current Brazil save
(`saves/campaign.sqlite`, player `BR`, turn 6, 47 events) the slots are being spent on local colour that
changes nothing:

| Turn | Slot | Event |
|---|---|---|
| 1 | regional | Bolívar holds a public town hall in San Cristóbal to solicit local grievances… |
| 2 | regional | The Santander Council convenes a public forum to address displacement concerns… |
| 3 | regional | Uruguay launches a localized 'Green Commute' initiative … in Lavalleja |
| 4 | regional | The Cojedes Regional Council convenes a public hearing to propose a local tourism initiative… |
| 5 | regional | Corrientes provincial authorities draft the regional taxation autonomy proposal… |
| 6 | regional | Marowijne council convenes a town hall to draft a local heritage resolution… |

Six of six regional slots are ceremonial. Meanwhile the campaign's real thread — the Brazil–Paraguay war,
the AMAMBAY/CONCEPCION occupation, Paraguay's Mercosur suspension → UNHCR appeal → regional ultimatum —
never gets a regional slot, even though AMAMBAY is an occupied front with a displaced population.

Three causes, all fixable:

1. **The decision prompt asks for exactly this.** `src/inference/prompts.cpp:20-23` says "propose civic
   initiatives, negotiations or autonomy demands", and the default regional agenda is literally
   `"Local civic wellbeing and representation"` (`src/core/politics.cpp:235`).
2. **The region is picked at random.** `scheduleInitiatives` ranks governors by closeness tier, then picks
   a region with `rank(rs, "region" + round, none)` — `none` is a constant-zero priority
   (`src/core/politics.cpp:104,109`), so among a governor's regions the choice is a pure hash. Suriname is a
   Brazilian neighbour (tier 3), so Marowijne beat AMAMBAY on a coin flip.
3. **The regional decision is blind.** `src/turn/plan.cpp:129` wipes `c.facts` to an empty object and
   rebuilds it with only `regional_identity`, `governing_authority`, `politics` and `related_history`. A
   district council is not told there is a war, who is occupying what, or where the displaced are going.

A fourth, related waste: **background developments repeat verbatim**. "A localized humanitarian crisis
emerges in AMAMBAY…" is emitted identically in T3, T4 and *twice* in T5; T5's war-crimes appeal is re-emitted
as a T6 background event. `compileEvent` already drops effectless background events
(`src/world/compiler/compiler.cpp:120-122`) but nothing checks them against history, and the adjudication
instruction's "never repeat or re-adjudicate it" is unenforced.

And these events are not even inert: the saved Marowijne document carries
`effects: [{actor: SR, economy: decline, stability: none}]`, which `applyMonthlyStatus` feeds into Suriname's
economy. A town hall about heritage measurably damages a national economy.

**Outcome:** every independent slot lands somewhere with stakes and produces something that could change the
actor's position; background developments never repeat one already recorded.

Out of scope (deliberately, per the user): off-theatre *national* initiatives (Japan/Arctic Council,
Senegal/Green Coast, Slovenia/Ljubljana) stay — the every-third-month distant pick is plan 17's design.
Also out of scope, noticed while investigating and worth their own plan: 9 of 47 event texts are hard-cut
mid-word by the `string(160)` grammar limit (`src/world/schema.cpp:265`), and one T2 event about protests in
Mexico is filed under `actor: US`.

## Note on existing saves

`monthly_allocations` freezes each month's slots at planning time (`src/turn/plan.cpp:105-111`), so a ranking
change affects only months not yet planned. The Brazil save keeps its history; month 7 onward improves.

Replay is safe: `tools/replay_verify.cpp:34-40` applies the **recorded delta** and never re-runs
`compileOperations`, so compiler-side filtering cannot break determinism. The `scheduleInitiatives` change is
deterministic per `(campaignId, turn)` as before.

## Steps

### 1. Rank regional slots by stake (`src/core/politics.cpp`)

Add a file-local helper computing a small integer stake per region from `WorldState` alone — no new
parameters, so `src/inference/references.cpp:49` (`prepare`) keeps compiling unchanged:

```
stake(r) = max of:
  3  r is in affected_regions of an open conflict (isOpen(w.conflicts[*]))
  3  w.territories[r]["owner"] != w.territories[r]["controller"]        // occupied
  2  w.regionalPolitics[r] has a demand with status == "pending"
  2  w.regionalPolitics[r]["relations"] == "strained"
  1  w.regionalPolitics[r]["supporting_events"] is non-empty            // an ongoing thread
  0  otherwise
```

**As built, stake counts only what happens inside the region.** The plan first also scored "controlled by
the player" and "controlled by a nation at war with the player". Both are properties of the governor, which
the governor ranking already weighs, and counting them again at region level made the player's own regions
win every draw — it broke the existing "Country balanced regional sampling" test (`tests/politics_tests.cpp`
lines 110-124), which is exactly the guarantee that test exists to protect. They are gone.

Then:

- **Governor ranking** (line 103): replace the `tier(id) >= 2 ? tier(id) : 0` priority with
  `bestStake(governor's regions) * 4 + (tier >= 2 ? tier : 0)`, so Paraguay (fronts) outranks Suriname
  (neighbour, nothing happening). **As built this is a lexicographic pair, not a `max`:** with `max` a
  stake-3 front tied with a tier-3 player province and fell through to the hash, so the front won only half
  the time. Stake leads, closeness breaks its ties, and the player's regions still win when nothing is at
  stake.
- **Region ranking** (line 109): replace `none` with `stake`, and use a comparator that puts `-stake`
  **before** `recent`, so a live front is not rotated away by the two-month cooldown. Keep `recent` as the
  next key so equally-staked regions still rotate, and keep `stableHash(seed + scope + id)` and `id` as the
  final tie-breakers — determinism is unchanged.

Per the user's choice, the slot is always filled: with stake ranking a Marowijne-type region is picked only
when nothing better exists anywhere.

- **Default agenda** (line 235, `regionalState`): `"Local civic wellbeing and representation"` primes exactly
  the output we are removing. Replace with something neutral that does not suggest a subject, e.g.
  `"No standing agenda recorded"`.

### 2. Give the regional decision its situation (`src/turn/plan.cpp:127-153`)

Add `c.facts["region_situation"]`: `{ occupied: bool, on_front: bool, war: <name when on a front>,
unanswered_demands: <count> }`, derived from the same conflict/territory data as `stake` in step 1. Keep the
existing exclusions (no national military inventory, no player orders).

**As built, the governing nation's `conflicts` and `commitments` are not copied across.** The plan proposed
carrying them over from the pre-wipe context, on the assumption the region was blind to the war. It is not:
`nationalStatus(w, a)` at line 139 builds `wars` and `treaties` itself
(`src/status/helpers.cpp:134-162`) — it is `campaignContext` that strips them, and this is the raw call.
Copying them would have duplicated the list and lengthened every regional prompt for nothing. What the region
genuinely did not know is what its *own ground* is living through, which is what `region_situation` adds.

### 3. Raise the bar in the decision instruction (`src/inference/prompts.cpp:19-30`)

Replace the regional sentence:

> For independent_regional you represent only regional_identity, not its governing nation: propose civic
> initiatives, negotiations or autonomy demands; never national military orders, treaties or armed rebellion.

with one that names the stakes and rules out the ceremony:

> For independent_regional you represent only regional_identity, not its governing nation: act on what is
> actually contested between the region and its capital or its neighbours — revenue and resources, autonomy,
> borders, displaced people, garrisons, war spillover, or a demand the capital has left unanswered. Never
> national military orders, treaties or armed rebellion.

and add one sentence covering both independent kinds:

> An independent initiative must be something that changes this actor's position, its relations or its
> territory — never a ceremony, festival, heritage or cultural declaration, awareness campaign, town hall or
> symbolic announcement.

### 4. Reject and retry a ceremonial decision (`src/world/geometry.cpp`, `src/inference/prompts.cpp`)

Add a sibling to the existing `consequential()` (`src/world/geometry.cpp:53-70`), declared in
`include/localhistoria/world.h` next to it and using the same `lowercase` / `containsWord` helpers:

```cpp
// An initiative that announces something rather than doing it: worth one re-ask from an
// independent slot, which has only one action a month.
bool ceremonial(const std::string &text);
```

Two lists, as `consequential()` does:
- **ceremonial stems** — `town hall`, `heritage`, `ceremon`, `festival`, `commemorat`, `awareness`,
  `celebrat`, `symbolic`, `monument`, `mural`, `anniversar`, `beautif`, `public forum`, `public hearing`,
  `civic wellbeing`, `outreach`, `signage`, `proclaim`, plus `commute`, `transit`, `tourism`, `recycl` and
  `clean-up`. The last five were added after the real "Lavalleja launches a localized 'Green Commute'
  initiative to incentivize public transit use" escaped the first list: soft civic programmes are the same
  waste as ceremonies and the first draft caught only the ceremonies.
- **substantive stems** — `budget`, `revenue`, `tax`, `levy`, `autonom`, `demand`, `border`, `refugee`,
  `displac`, `garrison`, `troop`, `evacuat`, `blockad`, `strike`, `protest`, `election`, `quota`, `tariff`,
  `mining`, `oil`, `water`, `port`, `sanction`, `ultimatum`, `conscript`.

`ceremonial(text)` returns true only when a ceremonial stem matches **and** neither a substantive stem nor
`consequential(text)` does — so "a public forum on displacement and refugee resettlement" survives, while
"a town hall to draft a local heritage resolution" does not.

In `LocalProvider::decide` (`src/inference/prompts.cpp:4-41`), when `initiative_type != "reactive"` and every
returned action is `ceremonial`, re-ask **once** with a nudge appended to the instruction — the same
retry-once idiom `adjudicate` uses at lines 197-204:

> The previous answer was a symbolic gesture. Write instead what this actor does about something at stake for
> it this month.

Accept whatever the second answer is. Never loop, never fail the month over it — a slot that stays ceremonial
is a worse month, not a broken one.

### 5. Drop repeated background developments (`src/world/compiler/`, `src/inference/prompts.cpp`)

Event IDs are already `t<turn>-<slug(description, 32)>` (`src/world/compiler/compiler.cpp:129`), so a
description slug is an exact text-duplicate test.

- Add a defaulted parameter to `compileOperations` in `include/localhistoria/world.h:20` and
  `src/world/compiler/compiler.cpp:289`: `const std::set<std::string> &priorEvents = {}` — event IDs already
  in campaign history. Thread it into `OperationCompiler`.
- In `compileEvent` (`src/world/compiler/compiler.cpp:120-122`), beside the existing effectless-background
  drop, discard a background event whose `slug(description, 32)` matches the slug suffix of any
  `priorEvents` ID. Only background events — an intention's outcome must stay 1:1 with its intention
  (`src/inference/prompts.cpp:205`).
- In `LocalProvider::adjudicate` (`src/inference/prompts.cpp:225`), build that set from the `id` fields of
  `input["facts"]["actors"][*]["related_history"]`, which `Store::campaignContext`
  (`src/store/store.cpp:239-243`) fills with full event documents. Both real duplicate chains in the save
  belong to PY, which is `central` and therefore carries `related_history`
  (`src/turn/adjudication_input.cpp:94,104`).

### 6. Stop a ceremony from moving a national economy (`src/world/compiler/events.cpp`)

The Marowijne event carried `economy: decline` for all of Suriname. In `settleEffects` (line 101), drop the
model's `effects` for an event whose only operation is a `regional_initiative` that recorded **no** demand and
left `relations` unchanged from the region's prior state. Forced effects (war, trade, seizure parties) are
unaffected — a regional initiative produces none of those. A regional event that files a real demand or
strains relations keeps its effects.

### 7. Docs

- `README.md:126` ("A living political world") — the regional slot now favours fronts, occupied land and
  regions with a pending demand, and independent initiatives are not ceremonial.
- `docs/EVOLVING_WORLD.md:26` (turn step 1) and the "Significance" section around line 103 — background
  developments that repeat a recorded event are dropped alongside effectless ones.
- `docs/ARCHITECTURE.md` "Independent politics" — the stake ranking.

## Tests

- `tests/politics_tests.cpp` — a world with an open conflict whose `affected_regions` include a region of a
  tier-0 governor, plus a quiet region of a tier-3 governor: the front takes the regional slot. A pending
  demand outranks a quiet sibling region. Determinism check unchanged. The existing "Country balanced
  regional sampling" case (lines 110-124) has no conflicts and no regional politics, so every stake is 0 and
  it must still pass unmodified — treat a failure there as a bug in the comparator.
- `tests/core/main.cpp` — `ceremonial()` returns true for the six real filler lines quoted in Context and
  false for "Paraguay presents a unified regional ultimatum to Bolivia and Argentina demanding the immediate
  withdrawal of Brazilian forces" and for "The Santander Council convenes a public forum to address
  displacement concerns".
- `tests/world/events.cpp` — a background event whose description matches a prior event ID slug is dropped;
  one that does not is kept; a non-background event with the same text is *not* dropped.
- `tests/world/events.cpp` — a `regional_initiative` with no demand and unchanged relations loses its
  model-supplied effects; one with a demand keeps them.
- `tests/context_tests.cpp` — a regional slot's facts carry the governing nation's open conflicts and
  `region_situation`.

## Verification

1. `make test` and `python3 tests/integration.py` pass (the latter needs `npm ci && npx playwright install
   chromium` once).
2. `tools/living_world_probe.py` hardcodes `'PT'` at line 22 (`api('campaign', {'country':'PT'})`). Add a
   `--country` argument defaulting to `PT`, then run with the local model up:
   `python3 tools/living_world_probe.py --country BR --months 6 logs/probe-23.json`.
3. From `logs/probe-23.json`, over the 6 regional slots: **≥ 4** land in a region with stake > 0, and
   **≤ 1** outcome description satisfies `ceremonial()`. Over all 6 months: **0** background events whose
   description slug repeats an earlier event's.
4. `./build/replay_verify` on the probe's export still reports all turns replayed identically.
5. Record the numbers in `docs/VERIFICATION.md` alongside the plan 17 and 18 entries, and mark row 23 done in
   `plans/README.md`.
6. Never touch `saves/campaign.sqlite` — the probe runs against its own temp save.

## Acceptance

Suites pass; the probe meets the thresholds in step 3; replay verifies; docs updated; one commit.
