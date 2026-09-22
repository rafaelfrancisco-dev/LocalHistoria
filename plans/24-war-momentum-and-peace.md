# 24 · Wars that move on their own, stall, and end

## Implementation corrections (2026-09-22)

The design below records the original plan. Testing during implementation changed these points:

- **No automatic intensity decay.** Claude removed C2 after the seeded Russo-Ukrainian annual-toll test
  showed that an unmoving front can remain intensely fought. `static_months` supplies motivation to
  negotiate; only an explicit intensity change or agreed truce cools the fighting. A destroyed formation
  does not reset the count of months without territorial gains.
- Autonomous movement covers conventional `war` and `border_conflict` only. Applying inventory ratios
  to insurgency/civil-war fronts incorrectly rolled the seeded Congo/M23 front. These stay adjudicated.
- `reserveDefence` is **0.05**, not 0.10, preserving RU→UA capacity 1. ES→PT changes from 2 to 1 because
  Portugal's reserves now help defend. Both opponents need an inventory for autonomous movement.
- `transfer_territory.to` keeps its validated ID syntax because a preceding `create_actor` can introduce
  the recipient in the same response; an enum of existing actors would break independence.
- Each attacker gains pressure once per conflict per month on its best eligible axis. Open alliances,
  defence pacts, unions, non-aggression pacts and peace agreements exclude allied pairs.
- `front_advance` includes its exact `region`, preserving unique event IDs, historical map placement and
  the journal's What changed summary. Ownership remains unchanged.
- Reachable offensive options also enable the existing bounded reasoning pass. Live testing found
  that “drive to the capital” otherwise skipped thinking and ignored the force facts; adding more
  instruction text alone did not fix it.
- The three successful-invasion probes use explicitly synthetic Portuguese superiority; `weak_raid`
  and `overreach` retain real inventories. The production adjudication-input builder is used in each.
- Live probes are offline for references, use temporary saves, accept `--first-order`, and save replay
  exports alongside their reports. A single twelve-month run checks both the six-month advance and
  longer passive behavior. Evidence and any unmet model targets are recorded in `docs/VERIFICATION.md`.

The six-turn existing Brazil save was exported read-only and replayed identically under status version 2.


Do plans 19, 21 and 23 first — all are done.

## Context

In the current Brazil save (`saves/campaign.sqlite`, player `BR`, turn 6) a Brazil–Paraguay war took exactly
**one** province — AMAMBAY, in turn 1 — and then stalled for five months. Paraguay never counterattacked and
never sued for peace. Nothing on the map moved again; the war just billed 275–1,339 dead a month.

Nothing in the simulation was refusing it. The turn-5 adjudication prompt offered Brazil four adjacent
Paraguayan provinces, each marked:

```
"within_reach_of": ["BR (superior: about 133,000 effective strength against 7,000; up to 2 regions)", …]
```

Brazil had 760,530 active against Paraguay's 28,934. `take()` would have allowed two provinces that month.

Four causes, all in scope here.

### 1. A dictionary of words decides whether a war moves

`attackGuidance()` (`src/world/geometry.cpp:93`) appends the hint that tells the 4B model to emit `occupy`,
but only when the intention text matches one of eight stems:

```cpp
static const std::vector<std::string> stems{"invad",   "occup", "attack", "offensive",
                                            "conquer", "seize", "annex",  "bombard"};
```

What the player typed, and what reached the model:

| Turn | Intention as sent to the adjudicator | Hint | Result |
|---|---|---|---|
| 1 | `Invade paraguay` **`[attack: use occupy on one or two regions…]`** | yes | `occupy` AMAMBAY |
| 2 | `Drive straight through to Paraguay's capital to topple the regime…` | **no** | `build` army → dropped |
| 3, 4, 6 | *(no order submitted)* | — | nothing |
| 5 | `Finish the invasion of paraguai…` | **no** | `build` army → dropped |

Turn 5 is the sharp case: **"invasion" does not contain the stem "invad"**. `consequential()`
(`src/world/geometry.cpp:53`) carries *both* `"invad"` and `"invasion"` — someone already hit this — but
`attackGuidance()` was never updated. Turn 2 fails differently: "drive through", "topple", "puppet state" is
an unmistakable invasion order that matches no stem at all.

Without the hint the model reached for the wrong verb both times. Its raw turn-5 operation:

```json
{"action": "build", "actor": "BR", "kind": "army",
 "name": "Highland Division", "region": "gb-63826900B74384941565078"}
```

That is ALTO PARAGUAY — Paraguayan ground. `opBuild` dropped it (`can only build in regions it controls`),
and the narration rationalised the empty result as "halted by defensive positions and logistical
constraints". The probe already records this failure mode: `docs/EVOLVING_WORLD.md` scores the `invasion`
scenario 2/3, the miss being "the model narrated the invasion as repelled and tried to build armies in East,
which the game dropped".

Keying the game's central affordance on the player's vocabulary is the wrong mechanism. The model should read
the situation and decide; the game should make the wrong operation *impossible* rather than coach the right
one.

### 2. A war has no momentum of its own

`applyMonthlyStatus` runs `addFront` and `warToll` for every open conflict every month, so an open war
reliably produces casualties, displacement and GDP drag. It never moves a front. Territory control changes
**only** through an LLM-emitted `occupy` / `transfer_territory` / `restore_control`; the sole other writer of
`["controller"]` outside the compiler is an expiring treaty occupation term
(`src/core/simulation.cpp:217`).

So a war between a regional power and a negligible one is, mechanically, a casualty generator. The force
model that already knows Brazil is ~19× Paraguay's strength on that border is consulted only when the model
happens to ask it for permission.

### 3. A stalled war never ends, and never cools

`addFront` has **no intensity decay**: `f.intensity = max(declared, 0)` whenever a front exists, so a war
declared `high` stays `high` forever unless a party explicitly scales it down. And nothing tells a belligerent
that it is bleeding for nothing. Paraguay had a reactive slot with two actions **every month** (parties to an
open conflict with the player always do, `src/turn/plan.cpp:22-26,71-80`) and spent all of them on Mercosur
notifications, a UNHCR appeal and a regional ultimatum. It never once offered a ceasefire, because nothing in
its facts said the front was frozen or what the war was costing it relative to what it was gaining.

### 4. Reserves are inert, and the inventory reaches the model only as prose

`committedForce` (`src/status/force.cpp:95`) does fold in personnel, aircraft (×200) and ships (×1500). But
`src/core/simulation.cpp:280-283` collapses the structured inventory into one prose string before the model
sees it, and the only numeric consequence it is shown is the `within_reach_of` verdict.

And `reserve` contributes **nothing** to strength anywhere — not in `committedForce`, not in `assault`, not in
`militaryLabel`. It is only a loss sink and a 2%/month mobilisation source. Brazil's 124,600 reservists and
Paraguay's 14,500 are decorative.

**Outcome:** an open war advances, stalls or reverses from the balance of forces each month without anyone
ordering it; a war that cannot move cools down and gives its belligerents a reason to talk; no word list
decides whether ground can change hands; and manpower, fleets and aircraft are what determines the answer.

## Design decisions taken

- **Momentum is fully autonomous** — every open conflict is evaluated each month from the force model alone.
- **Momentum is symmetric** — an occupier has to hold what it took, and a defender can push back.
- **The stem list goes away entirely**, replaced by a schema restriction (making `build` in unheld land
  ungrammatical) plus state-derived facts.
- **Momentum moves `controller` only, never `owner`.** Annexation stays deliberate: occupy, then annex a
  later month, or cede by treaty.
- **Peace is offered, never imposed.** A stalled war pushes its belligerents to negotiate; whether they do is
  the model's call, and when the offer reaches the player it goes through the existing accept/decline panel.

## Calibration: momentum only at decisive superiority

The first draft of this plan advanced a front whenever one side had any edge, which would have had Russia
grinding forward in Ukraine every campaign. That is wrong: the real front has been close to frozen for years,
and a simulation that unfreezes it "out of the blue" is worse than one that never moves.

`force::assault` (`src/status/force.cpp:137-159`) already bands the answer, and plan 21 tuned those bands
against the real world:

| band | ratio | meaning today |
|---|---|---|
| capacity 2 | `attack ≥ 1.5 × defend` | "superior … up to 2 regions" |
| capacity 1 | `0.75 × defend ≤ attack` | "limited … 1 region" |
| capacity 0 | below | "too weak" |

**Momentum requires capacity 2.** Capacity 1 is exactly the contested case: an attacker that can take ground
if it commits and is ordered to, but cannot roll a front on its own. That reuses calibration that already
exists rather than inventing a second one, and it produces the right answers for every war the game seeds —
`tests/military_tests.cpp:190-199` pins these today:

| pair | capacity | momentum | reality |
|---|---|---|---|
| RU → UA | 1 | **none** | front broadly frozen since 2023 |
| UA → RU | 0 | none (pressure decays) | correct |
| IN → PK | 1 | **none** | frozen line of control |
| PK → IN | 0 | none | correct |
| KM → MG | 0 | none | correct |
| BR → PY (this save) | 2, ratio ≈19 | ~1 province/month | overwhelming invasion of a state with 28k active, no aircraft, no ships |

So on a new campaign **none of the seeded January-2026 wars move on their own**. They keep costing, they now
cool toward a lull (step C), and their parties get a reason to negotiate (step D). That is the intended
result, and step "Tests" makes it an assertion rather than a hope.

Within capacity 2 the rate scales with how far past the 1.5 threshold the ratio sits, so "superior" grinds and
"overwhelming" rolls:

```
gain = pressureGain[intensity] * log2(clamp(ratio, 1.5, 8.0) / 1.5) * jitter
```

| ratio | log2(r/1.5) | medium (0.25) | high (0.45) | months per region |
|---|---|---|---|---|
| 1.5 | 0 | 0 | 0 | never — the band's own floor |
| 2.0 | 0.415 | 0.10 | 0.19 | 5–10 |
| 4.0 | 1.415 | 0.35 | 0.64 | 1.6–3 |
| ≥8.0 | 2.415 | 0.60 | 1.09 | 1–2 (capped at 2/month) |

Brazil at ratio 19 clamps to 8: about a province a month, two in a high-intensity month, ~17 provinces in
well over a year once occupation duties and the ×0.6 fatigue `assault()` already applies past 12 months bite.
Below capacity 2 the gain is negative, so a repelled attacker bleeds off banked pressure instead of hoarding
it.

## Note on existing saves

Momentum and intensity decay are gated twice, so nothing already recorded changes:

- on the turn's recorded `status_version` (step A5) — turns played before this plan replay exactly as saved;
- on `hasMilitary(forceStart)` — actors without inventories never advance, which is why the whole
  `tests/status/main.cpp` fixture (no `military` object) is unaffected.

## Steps

### A. Autonomous front momentum

#### A1. Expose the assault ratio (`src/status/force.cpp`, `src/status/status_internal.h`)

`assault(w, adj, attacker, defender, region)` (`force.cpp:137`) already returns `{capacity, attack, defend}`
but is file-local; only `assaultCapacity` is public. Declare it in `src/status/status_internal.h` under
`namespace force`, beside the existing `committedPersonnel` declaration. No behaviour change.

#### A2. New rates (`src/status/status_internal.h:9-28`)

```cpp
// A front moves only where one side is decisively superior (assaultCapacity 2, i.e. at least
// 1.5x); a merely contested front costs lives and gains nothing, which is what most real wars
// do. Pressure per month scales with how far past that threshold the ratio sits.
constexpr double pressureGain[] = {0.0, 0.25, 0.45}; // low / medium / high
constexpr double pressureFloor = 1.5;                // the capacity-2 threshold, in ratio terms
constexpr double pressureRatioCap = 8.0;             // a walkover is still one front at a time
constexpr double pressureTake = 1.0;                 // pressure spent per region taken
// A front that has not moved for this long is a stalemate: it cools, and its belligerents are
// told so.
constexpr int warLullMonths = 6;
```

#### A3. `advanceFronts()` (`src/status/monthly/fronts.cpp`, `monthly_status.h`)

Add a private method to `MonthlyStatus` (declared beside `collectSeized`/`addFront` in
`src/status/monthly/monthly_status.h`), called from `run()` (`src/status/monthly/monthly_status.cpp:26-29`)
**between** `collectSeized()` and the `addFront` loop:

```cpp
    // 4. War fronts: where each open conflict is actually fought this month.
    collectSeized();
    // 4b. Fronts the balance of forces moves on its own, before the fronts are drawn.
    advanceFronts();
    for (auto &id : w.conflicts.getMemberNames())
        addFront(id);
```

That placement is load-bearing: after `collectSeized()` so the month's compiled seizures already count
against the per-attacker cap and momentum cannot re-take a region the model just took; before `addFront()` so
a momentum seizure joins the front region set, flips `f.fighting` (hence `intensity = 2`), and is picked up
by `seizedShare()` for the one-off 8% displacement in step 5.

Per open conflict `c` — skipped when `!hasMilitary(forceStart)`, when a `ceasefire`/`peace_treaty` commitment
shares ≥2 of its parties (lift the existing check at `fronts.cpp:95-103` into a helper both call), or when
`level(c.get("set_intensity","medium")) == 0` — for each **ordered** pair of parties (A attacking,
D defending). Iterating ordered pairs is what makes counterattack and reconquest fall out for free:

1. **Candidates**: regions `r` with `cT[r]["controller"] == D`, `cT[r]["kind"] != "residual"`, and
   `reaches(forceStart, adjacency, A, r)` (`src/world/geometry.cpp:164`).
2. **Best axis**: the candidate with the highest `attack/defend` from
   `force::assault(forceStart, adjacency, A, D, r)`, ties broken by region ID so the choice is deterministic.
3. **Accrue** — nothing at all unless that candidate's `capacity == 2`:
   ```
   ratio = defend > 0 ? attack / defend : (attack > 0 ? pressureRatioCap : 0)
   gain  = capacity == 2
             ? pressureGain[declared] * log2(clamp(ratio, pressureFloor, pressureRatioCap) / pressureFloor)
                                      * jitter(forceStart, cid + ":pressure:" + A)
             : -pressureGain[declared] * 0.5      // a stalled or losing attacker bleeds off what it banked
   pressure[A] = max(0, pressure[A] + gain)
   ```
4. **Take** while `pressure[A] >= pressureTake` and the caps allow: set `T[r]["controller"] = A` on the best
   remaining candidate, `pressure[A] -= pressureTake`, and record it in `seized` / `seizedBy` (as `{A, D}`)
   so steps 4–5 treat it exactly like a compiled seizure. **Never touch `owner`.**
5. **Caps**, mirroring `take()` (`src/world/compiler/force_ops.cpp:145-169`): `force::assaultCapacity` per
   defender, and at most 2 regions per attacker per month **counting the seizures already in `seizedBy` with
   `taker == A`**.

Pressure lives on the conflict as `w.conflicts[cid]["pressure"]`, an object keyed by attacker actor ID.
It survives months in saved state, round-trips through `encode`/`decode` (both copy `conflicts` wholesale,
`src/core/simulation.cpp:95`), and passes `src/world/delta_checks.cpp:223-272`, which validates only named
keys. Drop a party's entry when it reaches 0 so the field stays small.

#### A4. Let `addFront` see momentum seizures (`src/status/monthly/fronts.cpp:50-62`)

`addFront` currently re-scans `delta.changes` to decide `f.fighting`, so a momentum seizure would be invisible
to it. Replace that scan with a `seizedBy` lookup — `collectSeized()` already fills it with
`{new controller, previous controller}` and `advanceFronts()` appends to it:

```cpp
    for (auto &region : seized) {
        auto it = seizedBy.find(region);
        if (it == seizedBy.end())
            continue;
        const auto &[taker, loser] = it->second;
        if (f.parties.contains(taker) &&
            (f.parties.contains(loser) || f.parties.contains(cT[region]["owner"].asString()))) {
            f.fighting = true;
            add(region);
        }
    }
```

This keeps the original's "or the previous *owner* was a party" clause and is a straight simplification.

#### A5. Replay and versioning

`tools/replay_verify.cpp:41` re-runs `applyMonthlyStatus(result, delta, adjacency)` and compares both state
and events byte-for-byte, so any behaviour change here breaks replay of existing exports. Gate it:

- `include/localhistoria/status.h:57` — add a defaulted parameter:
  `Resolution applyMonthlyStatus(const Resolution &, const WorldDelta &, const Adjacency &, int version = statusVersion);`
  The default keeps `src/turn/run.cpp:172` and every existing test call site compiling unchanged.
- `include/localhistoria/status.h:58` — bump `constexpr int statusVersion = 3;`
- `src/status/monthly/monthly_status.cpp:171` — thread `version` into `MonthlyStatus`; `advanceFronts()` and
  the lull decay return immediately when `version < 3`.
- `tools/replay_verify.cpp:40-41` — pass the recorded value instead of only presence-checking the key:
  `result = applyMonthlyStatus(result, delta, adjacency, turn["reference"]["status_version"].asInt());`
  `src/server/campaign_handlers.cpp:184` already writes `refs["status_version"] = statusVersion` as an int
  per turn, so existing exports carry `2` and replay unchanged.

Everything `advanceFronts()` reads is state + delta + adjacency + deterministic `jitter`, so it needs no delta
entry of its own — the same contract `warToll` already relies on.

#### A6. The event (`src/status/monthly/fronts.cpp`, `src/store/event_log.cpp`, `web/app.js`)

Emit one `ResolvedEvent` per region taken, shaped like the `war_toll` event at
`src/status/monthly/casualties.cpp:71-78`:

```cpp
r.events.push_back({A, "front_advance", cid,
                    w.actors[A]["name"].asString() + " forces took " +
                        cT[region]["name"].asString() + " from " +
                        w.actors[D]["name"].asString() + " in the " +
                        displayName(w, "conflict", cid) + "."});
```

`src/store/event_log.cpp:144` needs a `front_advance` branch beside `war_toll`: `major = true` (unlike a toll,
ground changing hands is news and belongs on the map), `category = "war"`, and the conflict, its actors and
the taken region attached. `web/app.js:388-406` needs no change — a non-`war_toll` action already falls into
the main journal list — but add a `"Front · "` prefix beside the existing `"War toll · "` at `app.js:401`.

### B. Delete the attack-word dictionary; restrict the schema instead

#### B1. Remove `attackGuidance`

Delete the function (`src/world/geometry.cpp:93-119`), its declaration (`include/localhistoria/world.h:54`)
and its only call site (`src/turn/adjudication_input.cpp:188`), leaving:

```cpp
item["action"] = action.text + (targets.empty() ? "" : " (targets: " + targets + ")");
```

`consequential()` and `ceremonial()` stay: they gate prompt depth and the reasoning budget, not whether
ground can change hands.

#### B2. Per-actor region enums in `operationSchema` (`src/world/schema.cpp:36-250`)

The `events` array is already a `prefixItems` tuple with `actor` pinned per item, and `blocked_by` is already
narrowed per item (`schema.cpp:303-311`); `prompts.cpp:110-121` already swaps `operations.items` wholesale
for regional slots. Per-item operation narrowing is an established pattern, not a new one.

For each distinct acting actor `a` among the tuple items, register:

- `op_build_<a>` — `kind` enum **without** `event_site`, `region` enum = regions in `facts["territories"]`
  with `controller == a`, `actor` pinned `const: a`. Omitted when that set is empty.
- `op_occupy_<a>` — `region` enum = regions with `controller != a` and `reaches(w, adjacency, a, r)`, `by`
  pinned `const: a`. Omitted when that set is empty.

Keep one global `op_build_site` (`kind` const `event_site`, full region enum) because an event site
deliberately does not need control (`src/world/compiler/force_ops.cpp:5-44`) — splitting it out states that
rule in the grammar instead of burying it. Drop the global `op_build` and `op_occupy`. Point each tuple item's
`operations.items` at the `$defs` subset for its own actor.

While here, narrow `transfer_territory.to` from the loose `actorId` regex to the actor enum
(`schema.cpp:215`) — the model can currently name an actor that does not exist, unlike `build.actor`.

**Cost.** Measured from the real turn-5 record: the schema was 26,551 bytes with 7 actions, 6 actors and 11
territories; `op_build` was 778 B and `op_occupy` 586 B with the full region enum. Per-actor copies carry
*smaller* enums, so ≈750 B per acting actor, ≈ +4.5 KB (~17%) for six, less the two globals removed.
Acceptable, and it does not eat the context budget — `prompt_tokens` estimation sums only the two message
contents and ignores the schema (`src/inference/transport.cpp:125-135`) — but it does grow the GBNF, so
measure schema bytes and adjudication latency in the probe and fall back to a single pair of defs narrowed to
the union over acting actors if the grammar cost bites.

#### B3. State-derived offensive facts (`src/turn/adjudication_input.cpp`)

After `input["facts"]["territories"]` is finalised (`adjudication_input.cpp:141-152`), add to each acting
actor's facts:

```json
"offensive": {"can_take_this_month": 2, "regions": ["gb-63826900B27882980507950", "…"]}
```

from the same `reaches` / `force::assault` data — the regions it could take and how many, capped at the
`take()` limit of 2. Omit the key when there are none. Region IDs are already in the payload, so this costs a
few dozen tokens and replaces a hint keyed on spelling with one keyed on the map.

#### B4. Prompt (`src/inference/prompts.cpp:186-189`)

The `build` parenthetical currently swallows the strength guidance in a run-on — `within_reach_of gives each
nation's real strength here` is stranded inside the `build` clause. Split it so:

- `build` says plainly it needs a region the actor **already controls** (event sites excepted);
- `occupy` carries the `within_reach_of` / "a nation marked too weak cannot occupy" guidance and the
  `offensive` block;
- one new sentence records the new reality: a war where one side is decisively superior advances on its own,
  so an outcome may describe fighting without any operation, and `set_conflict_intensity` is how a party
  presses or eases the tempo.

### C. A stalled war cools down

#### C1. Track the stalemate (`src/status/monthly/fronts.cpp`)

On each open conflict keep `static_months`: incremented every month in which no region changed hands between
its parties (nothing in `seizedBy` whose taker and loser are both parties), reset to 0 when one does. Computed
in `addFront`, so it is deterministic from state + delta + adjacency like everything else in the step.
`opIntensity` (`src/world/compiler/treaty_ops.cpp:230-251`) also resets it to 0 — a party declaring a new
offensive gets a fresh start, which keeps `set_conflict_intensity` a meaningful lever.

#### C2. Decay (`src/status/monthly/fronts.cpp:104-117`)

Today `f.intensity = max(declared, 0)` whenever a front exists, so a `high` war stays `high` forever. Add,
after the existing rules and before the new-conflict cap: when not `fighting` and `static_months >=
rate::warLullMonths`, cap the effective intensity one level below `declared`; at `2 * warLullMonths`, cap it
at low. Fighting in any month lifts the cap immediately (it already forces intensity 2). This is exhaustion,
and it feeds directly into the toll table, the GDP drag and `stabilityWar`.

### D. A stalled war gets talked about

The player-consent machinery already exists and needs no new plumbing: `consent()`
(`src/world/compiler/treaty_terms.cpp:89-100`) turns any commitment an AI offers the player into a
`proposal` when the player's own actions that month do not name that AI, `awaiting()` adds "It awaits
<player>'s answer." to the event text, and the side panel answers it next month via `POST /turns`
`responses` (`docs/EVOLVING_WORLD.md`, "Treaty proposals"). A ceasefire or peace treaty is just
`add_commitment` with that `kind`.

What is missing is motivation and information. Paraguay already had a reactive slot with two actions every
month and never used one to ask for a ceasefire, because nothing in its facts said the front was frozen or
what the war was costing it.

#### D1. Put the stalemate in the facts (`src/status/agreements.cpp:67-74`)

`describeAgreement`'s conflict block already emits `intensity`, `casualties`, `displaced` and
`affected_regions`. Add two fields, both straight from state:

```cpp
        d["months_without_gains"] = v.get("static_months", 0);
        if (integer(v, "static_months") >= rate::warLullMonths)
            d["front"] = "stalemate: neither side has taken ground in " + ... + " months";
```

This reaches every decision, adjudication and narration input at once, because they all read `conflicts`
through `describeAgreement`.

#### D2. Say that ending a war is a move (`src/inference/prompts.cpp:18-35`)

The decide instruction currently offers no reason to stop fighting. Add one sentence: a war that has cost
heavily and taken no ground for months is a reason to seek a ceasefire, a peace treaty, a withdrawal or
outside mediation — not only to escalate or appeal; an exhausted nation proposes terms it can actually live
with. Keep it short; this prompt is read by every AI nation every month.

The adjudicate instruction (`prompts.cpp:157-159`) already explains `set_conflict_intensity`,
`add_commitment` with kind `ceasefire`/`peace_treaty`, and `resolve_conflict`. Add only that a stalemated
front is a plausible reason for a party to open talks, so the adjudicator does not reject the offer as
out of character.

No change to `src/turn/plan.cpp`: parties to an open conflict with the player already get a reactive slot
with two actions (`plan.cpp:22-26,71-80`).

### E. Make reserves count (`src/status/force.cpp:95`, `src/status/status_internal.h`)

Add `constexpr double reserveDefence = 0.10;` and fold it into the **defence** term of `committedForce` only
— reservists are a mobilisation base for holding home ground, not an offensive pool available next month:

```cpp
land += share * (noLand ? number(m, "active") : uncommittedPersonnel(w, a)) / count;
if (defending)
    land += rate::reserveDefence * number(m, "reserve") / count;
```

**Risk, to settle by measurement before committing.** `tests/military_tests.cpp:190-199` pins
`capacity("IN","PK",1)`, `capacity("PK","IN",0)`, `capacity("RU","UA",1)`, `capacity("UA","RU",0)`,
`capacity("KM","MG",0)` against the real seeded world, and **step A now depends on those exact bands** —
if `reserveDefence` pushed any attacker from capacity 2 to 1 it would silently freeze a war that should
move, and vice versa. Ukraine has 1,176,000 reserves, so 10% could move `capacity("RU","UA")` to 0. The
first choice is to lower `reserveDefence` until every assertion above still holds; only change an assertion
if the new value is defensibly more realistic, and say so in a comment. `militaryLabel` is untouched, so the
strength-label assertions at `tests/military_tests.cpp:202-208` cannot move.

## Tests

- **`tests/military_tests.cpp`** — a momentum suite on the existing `fixture()` world (actors `A` and `D`,
  one adjacent region each, both with inventories):
  - A at 150,000 active vs D at 15,000 (capacity 2), open `war` at `set_intensity` medium → D's region flips
    within two months; `controller == "A"` and `owner` still `"D"`.
  - **A tuned to capacity 1 → no flip over 24 months**, and `pressure` never reaches 1. This is the
    Russo-Ukrainian case in miniature and is the assertion that keeps a stalled war stalled.
  - Equal strength → no flip; `set_intensity` low → no advance; an open `ceasefire` between A and D → no
    advance.
  - A takes D's region, then D's active is raised well above A's → D retakes it (symmetry).
  - `applyMonthlyStatus(..., 2)` → no advance (version gate); a world without a `military` object → no
    advance (inventory gate).
  - A momentum seizure produces the one-off displacement *and* a `front_advance` event, and `intensity` reads
    `high` that month.
  - Two runs of the same month encode identically, matching the existing check at `:214`.
  - **Real-world pacing, the headline assertion:** run the seeded 2026 fixture forward **12 months** with no
    deltas and assert that **no region changes controller in any seeded conflict** — specifically that every
    Ukrainian oblast still has `controller == "UA"` and every Pakistani region `controller == "PK"`. Then
    assert the Russo-Ukrainian conflict's `static_months >= 12` and its effective `intensity` has decayed
    below `high`.
  - Re-check the existing real-world month at `:209-215` and the `seize` shares at `:217-245`: momentum now
    runs in that fixture, so re-measure the Ukraine mobilisation bounds and displacement shares and widen
    them deliberately if they move.
- **`tests/status/main.cpp`** — unchanged (no `military` object, so no momentum). Add one check stating that
  contract: a war between inventory-less actors still moves no ground.
- **`tests/world/operations.cpp`** — `operationSchema` per-item enums: an item's `op_build_<a>.region`
  contains only regions that actor controls and its `kind` enum excludes `event_site`; `op_occupy_<a>.region`
  excludes its own regions and includes an in-reach foreign one; `occupy.by` is a `const`; an actor with
  nothing in reach has no occupy op in its item; `op_build_site` still accepts any listed region.
- **`tests/world/treaties.cpp`** — a ceasefire an AI offers the player while a war is stalled becomes a
  pending `proposal` with `awaiting` text, and accepting it next month opens the commitment and silences the
  toll. (Exercises the existing path against the new motivation; catches any regression in consent.)
- **`tests/context_tests.cpp`** — a stalled conflict carries `months_without_gains` and the `front`
  stalemate line into an actor's facts; an acting actor with a reachable enemy region gets an `offensive`
  block and one without does not; no intention text carries a `[attack: …]` suffix any more.
- **`tools/world_probe.cpp`** — keep `invasion`; add two regression scenarios for the exact real failures:
  `{"finish_invasion", {"PT", "Finish the invasion of Spain to avoid the war stalling."}}` and
  `{"capital_push", {"PT", "Drive straight through to Spain's capital to topple the regime."}}`, both
  expecting `region-east` controlled by PT and owned by ES. `weak_raid` must still produce no seizure and
  `overreach` must still leave Spain's interior untouched.

## Verification

1. `make test` and `python3 tests/integration.py` pass (the latter needs `npm ci && npx playwright install
   chromium` once).
2. `./build/world_probe out.json` with the local model up, three runs: `invasion`, `finish_invasion` and
   `capital_push` each match in ≥2 of 3; `weak_raid` and `overreach` match 3/3. Record the table in
   `docs/EVOLVING_WORLD.md` "Measured model results", replacing the current `invasion` row and its
   "tried to build armies in East" note.
3. `python3 tools/living_world_probe.py --country BR --months 6 logs/probe-24.json`, submitting an invasion
   order in month 1 and **nothing** thereafter. Over the 6 months: at least 2 further Paraguayan provinces
   change hands without an order, at least one `front_advance` event appears, and no month fails. Record
   adjudication latency and schema bytes in `docs/VERIFICATION.md` against the baseline in `plans/README.md`.
4. A second probe run of 12+ months where the player stays passive after month 1: check that the war
   eventually reaches `static_months >= 6`, that its intensity decays, and that Paraguay (or Brazil) offers
   terms — the offer to the player must appear in `proposals` as `pending` and be answerable in the panel.
   If the model never offers in 12 months, the D2 wording needs another pass; record what it did instead.
5. `./build/replay_verify` on each probe export reports all turns replayed identically.
6. Export the existing 6-turn Brazil campaign and confirm `replay_verify` still passes on it (its turns carry
   `status_version` 2).
7. Never touch `saves/campaign.sqlite`; the probes run against their own temp saves.

## Docs

- `README.md` — the war paragraph: a war where one side is decisively superior advances on its own; a
  contested front costs lives and moves nothing; a front frozen for months cools and its belligerents look
  for terms, which reach the player as an offer to accept or decline.
- `docs/EVOLVING_WORLD.md` — the "Monthly simulation" turn steps (new step 4b); "Realistic pacing" →
  **Strength** (reserves now defend) and **Tempo** (the capacity-2 gate and the pressure curve); the
  "National status and war" monthly list (front advance before the toll, and the lull decay); and **delete**
  the paragraph beginning "An action that reads as an attack (invade, occupy, attack, offensive, conquer,
  seize, annex, bombard) gets a short hint appended", replacing it with the schema restriction and the
  `offensive` facts block.
- `docs/ARCHITECTURE.md:25` — the "Deeper warfare" future-work note is now partly delivered; restate what
  remains (supply, multi-side conflicts).
- `plans/README.md` — add row 24 and mark it done.

One commit, message ending `Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>`.
