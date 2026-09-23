# 26 · Simulation realism and consistency

**Status: implementation in progress — unpublished.** Drafted 2026-09-22.

2026-09-23: Phases A, B and C gates passed — 16 CTest cases (adding `consent` and
`conflicts`), legacy v0/v2/v3/v4 and mixed-version state/event replay, the disposable
HTTP integration suite, the browser run and a server restart, and `make format-check`.
Lifecycle version 1 is reserved for this complete unpublished release; monthly status
version 4 remains unchanged for legacy turns. No campaign save was modified.
Phase C fixed a latent defect found by the browser gate: `buildEvent` named
`event["effects"]` on a failed event, and jsoncpp's non-const `operator[]` inserted a
null member that `checkDelta` then rejected, failing the whole month instead of the
event.

2026-09-23: Phase D gate passed — 17 CTest cases (adding `access`, 35 checks), sea-route
fixtures in the `data` test, the HTTP integration suite with browser, restart and replay of
exports that now carry the sea graph, and a 36-month run of the seeded world whose
trajectories match the pre-D build exactly. Known limits, by design for this pass:
- The sea graph is region-grained: a region on two seas (Colombia's Chocó) lets a fleet based
  there use either, and erosion opens isthmuses under about 18 km (Corinth, Perekop).
- Supply ignores naval interdiction; carrier aviation is not modelled.
- Embarkation is the explicit co-location of an army and a fleet, capped at 500 troops per
  warship; there is no separate embark operation. Access is national in scope.
- A takeover of a captured project costs a construction slot; its resource cost arrives with H. A paused military
  project can be abandoned (releasing its reservation) or destroyed, not evacuated.
- `committedPersonnel`, used for seizure displacement, still uses the old pool; F replaces it
  (done: it is now `force::engaged` under the realism rules).

2026-09-23: Phase D follow-up resolved the three decisions left open above.
- Agreed transfers (cession, agreed occupation, withdrawal, expiry) hand the previous holder's
  unfinished port, base or city to the new holder on schedule and disband its unfinished
  armies and fleets back to its pool; only hostile loss of control pauses.
- Supply traces to the heartland (most populous owned-and-controlled region) by land, sea
  routes, or an ocean basin (`sea_basins`, 16 bodies of water: one world ocean, the Caspian
  apart). Nakhchivan is cut off; Kaliningrad, Hawaii, Alaska and Russian-held Crimea are not.
  Offshore waypoints now let routes round capes; before them Cabo da Roca split Portugal's
  coast, so Galicia had no route to Lisboa. The 36-month seeded world is unchanged; the search
  costs about 0.25 s per simulated month on the real map.
- The two prompt glosses were A/B tested on the live model and kept (`docs/MODELS.md`): better
  on access treaties and sealift, equal on regressions, no echo in two played months. Both
  prompts often narrate treaties without a treaty operation, a Phase J concern.

2026-09-23: Phase E gate passed. There are 17 CTest cases. `access` now has 51 checks: cession
around a third party, eviction by an `occupy` term, and an accepted access treaty. `status` gains
the conquest scenarios, run over 24–60 deterministic months, and the plan 26 run of the idle real
world. Also passing: the HTTP integration suite with browser, restart and replay, the legacy
v0/v2/v3/v4 replays, and `make format-check`.
- **Control history.** Regions held by force keep an engine-owned `control` record (claimant,
  holder, since, cause, integration) through annexation. Annexation no longer ends the output
  drain, the resistance or the claimant's stability penalty. Integration takes ten peaceful years,
  five with cooperative relations.
- **Usable output.** Strength and construction capacity count the output a nation actually
  commands, so lost land counts for nobody. A conquest counts at 30% for its holder until it is
  integrated.
- **Resistance.** Occupation now produces resistance whatever the occupier's own stability: two
  years under a stable occupier reach 46. Ignored demands can make a province restive, up to 40.
  The duplicate-rebellion guard covers only the relevant rising, so an occupied war theatre rose
  once, after 46 months. Resistance is raised from local people, not by defection from the
  occupier's army, and declares no state.
- **Third parties.** A cession or `occupy` term no longer evicts a controller that did not sign.
- **Real world.** Occupied Ukraine is restive at month 10 and reaches 57–68, below Russia's armed
  line of 84. RSF- and M23-held land is not foreign occupation. The 36-month fronts match Phase D
  exactly, and the month step is faster than before because the per-actor occupied-population loop
  now runs once.
- **Phase D bug fixed.** An accepted treaty with a `grant_access` term failed delta validation
  ("Invalid treaty term"), which failed the whole month. The term is now valid under realism.
- **Known limits.** Displaced people still return to annexed land from the new owner's regions
  (Phase G). A resistance that holds ground does not hand it back to its claimant. Integration does
  not yet answer to explicit policy orders beyond regional relations.

2026-09-23: Phase F gate passed. There are 17 CTest cases. `access` now has 53 checks, adding a
second front declared earlier in the same month blocking a seizure. `status` gains the force
scenarios, run over 24–36 deterministic months. Also passing: the HTTP integration suite with
browser, restart and replay, the legacy v0/v2/v3/v4 replays, and `make format-check`.
- **One allocation.** `force::engaged` divides the national pool, air arm and fleet across every
  engagement, attack and defence alike. An army split between two enemies serves each at half.
  Compiler seizures, automatic advances, garrison checks, seizure displacement and casualties all
  use it. Engagements include wars declared this month, both in the compiler (`engagementWorld`)
  and in the month step (the frozen force inputs take the post-order wars and treaties).
- **Exposure-based losses.** Losses fall only on the armies and pool engaged against that enemy;
  armies at home or assembling lose nobody. Aircraft losses need supplied ground at the front, and
  ground fire alone threatens them at half rate. Ship losses need a fleet in sailing range, or the
  uncommitted fleet off its own front-line coast, plus an enemy ship or aircraft engaged.
- **Fractions carried.** Each front's fractional losses are summed per actor and kept in
  `status.attrition`. They survive save and reload, and duplicate conflicts cannot add a pair twice.
- **Real world.** The 36-month fronts match Phase E. The Russo-Ukrainian toll is about 196,000 a
  year. Russia loses about 33 aircraft and a ship a year; Ukraine's small air arm now wears down too.
- **Bug fixed.** Realism `atWar` now sees wars declared earlier in the month.
- **Known limits.** Reserve mobilisation is still 2% of starting reserves in a high-intensity war
  (support constraints and demobilisation arrive with H). Carrier aviation and naval blockade are
  not modelled. Formation creation stays allocation, not procurement.
Next: Phase G (displacement by origin and host).

Follows completed plans 19 and 21–25. Addresses all fourteen findings in the
[simulation realism audit](../docs/audits/2026-09-22-simulation-realism.md).
Use that report for the evidence and the
[standalone probe](../docs/audits/2026-09-22-realism-probe.cpp) for reproduction.
This plan supersedes earlier plans where they prescribe name-based treaty consent, occupation
penalties based only on ownership mismatch, or the combat rules explicitly replaced below.

## Goal and scope

Make the existing simulation obey consistent rules across diplomacy, combat, territorial control,
unrest, population movement, construction, and economic consequences. A rejected action must not
grant its success rewards; a treaty must not bind a nonconsenting party; a unit or person must not
be spent or moved twice; conquest must not become peaceful integration through a label change.

Keep deterministic C++ authoritative. Retain coarse regions, existing military inventories,
regional politics, treaties, construction, bounded model effects, and the current local-model
budget. Add the persistent accounting needed to make these features work together. A sectoral
economy, tactical combat, detailed weapon classes, and a replacement map engine are outside scope.

Implement in the phases below. Each phase must reach its own acceptance gate before proceeding;
this is a substantial follow-up, not one combined rewrite. Do not mark the plan done after only
the first correctness fixes.

## Coverage and dependencies

| Phase | Deliverable | Audit findings | Depends on |
|---|---|---|---|
| A | Regression fixtures and replay compatibility | All | Existing implementation |
| B | Explicit consent and effects tied to accepted outcomes | 1, 3 | A |
| C | Hostile pairs and scoped conflict lifecycle | 2 | B |
| D | Access, sea movement, and captured construction | 4, 13 | C |
| E | Persistent conquest consequences and local resistance | 5, 6 | C, D |
| F | Shared force allocations and fractional attrition | 7, 8 | C, D |
| G | Displacement provenance and actual return migration | 9 | A; integrate with E |
| H | Ongoing effort, trade lifecycle, and military replacement | 7, 10, 11 | B, D, F |
| I | Combat pacing based on exposure and exhaustion | 12 | E, F, H |
| J | Urgent reactions, presentation, and end-to-end acceptance | 14; all surfaces | B–I |

Recommended implementation order: A → B → C → D → E → F → G → H → I → J.
Do not tune combat thresholds before force accounting and economic recovery are in place.

## A · Establish regression evidence and version the full turn lifecycle

### Work

1. Inspect the checkout before editing. The audit included existing uncommitted changes in the
   compiler, tests, and web UI; preserve unrelated work and recheck whether any findings were fixed.
2. Convert the audit's eight observations into meaningful regression fixtures in the existing
   world/status/military suites. Add regression assertions alongside each implementation phase;
   do not merge a permanently failing suite or assert buggy behavior as the desired contract.
3. Capture synthetic, sanitized exports of pre-change turns, including versions predating unrest,
   and a version-4 campaign with construction completion, treaty expiration, war, and displacement.
4. Extend replay rule selection beyond `applyMonthlyStatus`. `resolve()` currently runs without a
   version and performs construction completion and treaty expiry before the recorded delta is
   applied. Gate changes there as well. Preserve the exact legacy branch for exports lacking the
   new lifecycle version. Use named constants and explicit version dispatch rather than scattered
   numeric comparisons.
5. Record the rule selection and any migration payload in each turn's frozen references. Apply the
   same migration at the same point before resolution during live play and replay. Loading an old
   export for replay must not silently upgrade its initial state.
6. Keep historical accepted deltas replayable. The operation compiler is not rerun by replay;
   stronger validation of newly generated operations must not retroactively reject old deltas.
   Version any tightened delta validation that conflicts with historical output.
7. Make optional state additions round-trip through encode/decode, exports, saves, cancellation,
   retry, and restart. Reject model attempts to write engine-owned allocation, migration,
   displacement, integration, budget, or attrition fields directly.

Use a new rule version for each independently shipped behavior-changing phase, or one version
only if the entire release remains unpublished until all phases land. Never reuse a shipped
version number for different rules.

### Files

`include/localhistoria/{simulation,status,world}.h`, `src/core/simulation.cpp`,
`src/status/monthly/monthly_status.cpp`, `src/world/{delta,delta_checks}.cpp`,
`src/server/campaign_handlers.cpp`, `src/turn/run.cpp`, `tools/replay_verify.cpp`,
`tests/turn_tests.cpp`, `tests/integration.py`, `tests/fixtures/`.

### Gate

Old exports replay to identical state **and events**. A campaign upgraded on a new month also
replays identically across that boundary. A cancelled/retried turn cannot migrate or charge twice.
Do not touch `saves/campaign.sqlite`; use disposable databases and synthetic exports.

## B · Make consent explicit and bind consequences to successful outcomes

### Treaty decisions

- Remove `collectConsent`'s inference from names in generated descriptions.
- Generalize the existing proposal flow to all signatories. Store immutable proposed terms,
  a proposal revision, required signatories, explicit answers, and expiry.
- Player authorization comes from the submitted proposal answer, never generated narration.
  A player-originated offer authorizes only that submitted offer; changed material terms require
  a new answer. Bind AI acceptance to the exact proposal ID/revision in its structured decision.
- Use the existing scheduled decision call to answer AI proposals on the next eligible month;
  do not add a global negotiation inference round. Record AI answers with the turn inputs.
- An amendment creates a new proposal revision and invalidates consent to changed terms. A
  refusal, unrelated mention, regional demand, or blocked action conveys no signature.
- Revalidate terms on settlement. If a material clause became impossible, fail/lapse settlement
  and request revised terms in a later decision. Do not silently apply half of an accepted bargain.
- Preserve the current visible player expiry policy initially, but make expiry explicit in facts
  and UI. Give AI proposals a bounded recorded lifetime that accommodates scheduler delay.

### Event effects

- Keep successful operation IDs/results inside the compiler. Derive deterministic operation
  effects from those results, not attempted operations or the original success description.
- When every operation fails, drop its claimed success benefits. Any cost or diplomatic penalty
  must come from a supported failed-attempt rule, not automatically preserve arbitrary effects.
- For partial events, allow effects only from successful operations or a separately represented
  non-structural result. Keep narrative-only reforms/negotiations possible; H adds their effort
  accounting. Do not solve this by dropping every event without a map change.
- Make the event flags, headline selection, prose corrections, and recorded effects agree.

### Files and acceptance

`src/world/compiler/{compiler,treaty_ops,treaty_terms,events}.cpp`, compiler headers,
`src/world/schema.cpp`, `src/inference/prompts.cpp`, `src/turn/{plan,run,adjudication_input}.cpp`,
proposal API/UI handling, `tests/world/{treaties,events,operations}.cpp`, `tests/turn_tests.cpp`.

Test rejection-as-consent, player mention bypass, stale/revised proposals, missing signatures,
retry idempotence, and an impossible material clause. The audit's impossible city grants no
growth. A valid independent consequence in a partially failed event survives.

## C · Give conflicts explicit hostile relationships

### Work

- Add canonical unordered hostile pairs to each conflict; retain `actors` for participants and
  compatibility. Derive opposing fronts from pairs rather than treating every participant as an
  enemy. Allied participation alone must never create hostility.
- Each pair has scoped ceasefire/settlement status and each participant has its own declared
  operational intensity. Active fighting uses the higher hostile posture; one party choosing low
  does not silence another's offensive. An accepted ceasefire pauses only its covered pairs.
- Route reach/attack eligibility, front generation, casualties, mobilization, pressure, stability
  exposure, treaty breach, and status descriptions through the same relationship helpers.
- `resolve_conflict` must require an accepted settlement covering its remaining hostile pairs.
  For this pass, do not introduce an automatic capitulation shortcut: an actor losing all land
  does not by itself prove agreement or the end of a landless insurgency.
- A unilateral withdrawal changes that participant's posture/deployment; it cannot clear the
  other party's hostility. Resolve the whole conflict only when no active hostile pair remains.
- Prevent duplicate declarations from multiplying a pair's simultaneous exposure and losses.
  Normalize overlapping conflicts into one monthly engagement per pair/theatre and attribute the
  result to a canonical conflict record without duplicating population or inventory changes.
- Seed explicit pairs for known scenario conflicts. For older ambiguous multi-party records,
  migrate with a documented deterministic fallback based on frozen participants and existing
  peace/alliance relations; retain provenance rather than claiming historically known sides.

### Files and acceptance

`src/world/compiler/{treaty_ops,treaty_terms,territory_ops}.cpp`, `src/world/schema.cpp`,
`src/status/{force,seed,agreements}.cpp`, `src/status/monthly/{fronts,casualties,recovery}.cpp`,
`data/source/world_2026.json`, `tests/world/treaties.cpp`, `tests/status/`, `tests/military_tests.cpp`.

A–B ceasefire leaves B–C fighting; A and B on the same side do not attack each other or divide
forces as enemies. Movement and toll agree about peace. Duplicate declarations do not double
losses. A single participant cannot end the whole war or set every other participant's posture.

## D · Enforce access and define what capture does to projects

### Movement and supply

- Add explicit military-access terms to existing commitments, scoped to beneficiaries and
  territory. Do not assume every alliance permits basing or transit.
- Army movement requires controlled land or valid access. Hostile entry uses the occupation/
  battle path and its shared budget; `move_entity` cannot bypass it.
- Fleets require coastal locations and a navigable sea connection within the distance allowance.
  Precompute/freeze a small coastal-route graph from available geography. Coastal flags alone
  are insufficient: do not allow a straight segment through intervening land. Validate difficult
  straits/islands with fixtures; record the graph revision in campaign reference inputs.
- Require an explicit deployed embarkation allocation for overseas attacks, capped by existing
  personnel and conservative transport capacity derived from participating fleets. This is a
  coarse gameplay bound, not a claim that every warship is a troop transport. An empty fleet
  marker must not unlock the full national pool.
- Derive supply connectivity from friendly/access-controlled land and validated sea links.
  Isolated forces remain present but receive reduced effective support and no instant relocation
  of national reserves. Revoked access stops new transit; it does not delete trapped formations.

### Capture and completion

- Use one controller-change handler for ordered capture, automatic advances, treaty changes,
  and rebellion. Recheck control at completion as a final safeguard.
- Pause unfinished armies/fleets when their builder loses control; preserve their reserved
  inventory and prohibit activation. Define later evacuation/disbandment through validated
  operations, without creating replacement personnel or ships.
- Pause civilian infrastructure construction under hostile control. The physical project stays
  in its region, with builder and progress preserved; resumption by a different controller needs
  an explicit takeover and the remaining resource cost. Returning control can resume it.
- Keep the existing entity status vocabulary where practical; represent paused construction
  explicitly and stop advancing its completion schedule. In H, progress uses paid work months.

### Files and acceptance

`src/world/{geometry,schema,delta}.cpp`, `src/world/compiler/{force_ops,treaty_terms}.cpp`,
`src/core/simulation.cpp`, `src/status/monthly/{fronts,rebellion}.cpp`, reference import/loading,
`tests/world/movement.cpp`, `tests/regions_test.py`, `tests/military_tests.cpp`.

No inland fleets, neutral transit without access, or hostile movement without adjudicated combat.
Routes cannot cross land. Offshore strength respects embarked capacity. A captured recruiting
project does not finish behind enemy lines. Reservations, pauses, and recapture survive replay.

## E · Preserve conquest consequences and allow local resistance

### Work

- Add engine-owned territorial control history: prior legitimate owner, controlling actor,
  control-change turn, cause (hostile seizure versus consent), and bounded integration progress.
  Do not equate `initial_affiliation` with an immutable legitimate owner for all future history.
- Annexation changes the legal claim but retains hostile-acquisition history, resistance, and
  impaired productive access. Integration progresses gradually through sustained peaceful control
  and supported policies; renewed fighting interrupts it. Transfer back to the displaced owner
  removes foreign-occupation pressure without erasing physical damage or all local grievance.
- Cession changes ownership but preserves a nonconsenting third party's actual control. An agreed
  occupation cannot evict that controller without its agreement or a successful military action.
- Preserve territorial GDP/population totals as descriptive measures. Introduce an effective
  controlled-output measure for affordability and military support, so occupied-away production
  is not fully available to its legal owner and conquered output is not immediately fully usable.
  Avoid double-counting that output for owner and controller.
- Separate occupation resistance from domestic stress. Hostile occupation supplies a nonzero
  local pressure independently of the occupier's stability. National stress, regime, garrison,
  integration, and credible regional responses modify its rate.
- Give sustained unanswered domestic demands a bounded local pressure path; low seeded stability
  alone remains insufficient. War damage to a defending region alone must not trigger separatism.
- Narrow duplicate-rebellion suppression to an existing relevant rebellion against the current
  controller; another war's theatre must not suppress occupation resistance.
- Migrate recorded occupations conservatively from frozen scenario/control information. Do not
  fabricate ages or historical grievance. Revise plan-25 idle-world expectations where they would
  categorically forbid the resistance this phase intentionally adds; retain the quiet-country and
  invaded-population regression protections.

### Files and acceptance

`src/world/compiler/{territory_ops,treaty_terms}.cpp`, `src/status/{seed,helpers,force}.cpp`,
`src/status/monthly/{monthly_status,unrest,rebellion,recovery}.cpp`,
`tests/status/{unrest,real_world}.cpp`, `tests/world/treaties.cpp`.

Annexation does not reset conquest penalties. Stable occupiers can face resistance. Existing war
theatres can produce a relevant insurgency without duplicate rebel actors. Quiet low-stability
countries do not fragment solely from their initial score. Legal cession never teleports a third
party's forces away. Test these over 24–60 deterministic months, not just a forced one-month state.

## F · Allocate forces once and carry fractional losses forward

### Work

- Build an immutable monthly engagement allocation from start-of-month inventories, deployed
  formations, hostile pairs, supply, and declared posture. Share this calculation between compiler
  feasibility, automatic advances, garrison checks, and casualty calculations. Same-month new
  declarations must reserve from the same remaining pool, not obtain an additional full force.
- Reserve personnel, aircraft, and ships across engagements. No formation or national pool may
  contribute more than its total simultaneously. An idle formation adjacent to two targets is
  eligible for allocation, not fully committed to both.
- Allocate casualties to exposed formations and supporting personnel only. Under-construction
  formations do not take front-line casualties unless their own location suffers an explicitly
  modeled attack. Separate destroyed/captured personnel from deaths and do not charge twice.
- Persist fixed-point/fractional attrition remainders by actor and equipment class. Aggregate
  exposure before rounding; do not floor each conflict independently. Preserve residuals across
  ceasefires and prevent duplicate conflict records from accelerating losses.
- Make aircraft/naval attrition depend on deployment and a supported threat class in the theatre.
  Ground combat may threaten deployed aircraft without enemy aircraft; no enemy ship should sink
  an undeployed fleet on the other side of the world merely by existing.
- Keep reserve mobilization constrained by available reservists and effective support. H adds
  replacement and demobilization; formation creation remains allocation, not procurement.

### Files and acceptance

`src/status/force.cpp`, `src/status/monthly/{monthly_status,fronts,casualties,recovery}.cpp`,
`src/world/compiler/force_ops.cpp`, relevant headers, `tests/military_tests.cpp`, `tests/status/`.

Assert allocation sums never exceed inventories; second fronts consume offensive capacity;
unexposed formations survive unrelated combat. Small deployed inventories eventually incur losses
under sustained exposure. Fractional balances survive save/reload, and losses/destroyed formations
are charged once. Preserve population accounting when fatalities are modeled.

## G · Track displaced people by origin and host

### Work

- Add an engine-owned world displacement ledger keyed by origin region and current host region.
  Each movement updates population, the ledger, and the existing output-transfer rule atomically.
- Return people only from their recorded host when origin conditions permit. Foreign-host returns
  are supported. If no return happens, outstanding displacement does not decrease.
- Derive current displaced totals from outstanding balances; retain separately labeled cumulative
  movement totals on conflicts. Secondary displacement moves the relevant hosted balances rather
  than inventing a second simultaneously displaced person.
- Apply a deterministic proportional accounting rule to deaths among hosted populations so ledger
  counts never exceed living residents. Territorial ownership changes do not change provenance.
  Population growth counts births as host residents rather than silently expanding refugee cohorts.
- Legacy saves lack host provenance. Retain their old outstanding amount as explicitly unknown
  provenance, exclude it from automatic return transfers, and expose this limitation. Never invent
  a domestic host, erase the count, or migrate people to make the old data fit.

### Files and acceptance

`include/localhistoria/simulation.h`, `src/core/simulation.cpp`,
`src/status/monthly/{casualties,recovery}.cpp`, `src/status/helpers.cpp`,
`src/world/delta_checks.cpp`, `tests/status/`, export/context code.

Test domestic and foreign returns, no safe origin, no recorded host, repeated flight, host
casualties, border changes, and legacy balances. All transfers conserve global population and
the ledger never exceeds its host population. The audit's 1,000-person balance cannot fall to
970 without an actual movement or separately accounted demographic loss.

## H · Make effort, trade, and replacement ongoing commitments

### Resource and project accounting

- Add one bounded monthly public implementation allowance derived from effective output, with
  existing military spending represented as an ongoing claim on that envelope. Label this as a
  scenario/gameplay estimate; GDP is not a treasury balance. Use named configurable coefficients,
  with documented units and measured sensitivity rather than asserting fiscal precision.
- Use a single accounting model: spending reserves implementation capacity and advances projects;
  it does not directly subtract annual GDP as though GDP were cash. Maintenance/overextension may
  influence growth through bounded explicit rules, charged only once.
- Fixed priority: essential maintenance, existing obligations, then newly accepted work in stable
  recorded order. Unfunded work pauses; do not introduce debt, borrowing, or instantaneous collapse
  in this pass. Make shortages visible in the result and facts.
- Track bounded ongoing efforts for construction, reforms, and procurement: actor, normalized
  category/target, start, paid progress, remaining duration, maintenance, and awarded completion
  benefits. Equivalent repeated requests continue the effort instead of collecting another bonus.
- Keep all numeric costs, durations, limits, and benefit bounds in C++. The model may select a
  supported effort and explain it; it must not choose its own cost or award unrestricted benefits.
- Apply completion benefits once. Limit repeat stability-baseline changes from equivalent reforms;
  distinguish temporary confidence effects from durable institutional change.

### Trade lifecycle

- Key economic relationships by canonical partners and a bounded agreement category. Amendments
  update the relationship; a renamed copy does not create another benefit.
- Replace automatic signing rewards with bounded ongoing benefits while active, scaled by partner
  effective output and capped for overlapping agreements. Count trade unions and their overlapping
  bilateral deals without unlimited stacking.
- Expiry, withdrawal, and breach use the same lifecycle helper and remove the same future benefit;
  apply additional breach penalties only when explicitly justified. Do not cancel historical GDP
  growth already earned or re-award it every time a signature is renewed.

### Military recovery

- Add slow recruitment and equipment-replacement projects consuming this same allowance and
  construction capacity. Initial scope replaces losses toward a recorded target establishment;
  reserve capacity for a later deliberate expansion feature instead of unconstrained auto-growth.
- Recruitment changes civilian/military classification, not total population. Equipment delivery
  increases national inventory only on paid completion. Formation assembly cannot also produce it.
- After sustained peace, gradually demobilize excess active personnel toward the establishment,
  returning eligible personnel to reserve. Do not remove personnel already committed to formations
  without reconciling those allocations, and never demobilize on a partial ceasefire elsewhere.
- Migrate legacy construction with recorded progress preserved and no retroactive spending bill.
  Freeze target establishments and model coefficients in migration/reference data for replay.

### Files and acceptance

`src/core/simulation.cpp`, `src/world/compiler/{force_ops,events,treaty_ops,treaty_terms}.cpp`,
`src/status/{helpers,force,seed}.cpp`, `src/status/monthly/{monthly_status,recovery}.cpp`,
`src/world/schema.cpp`, `tests/world/`, `tests/status/`, `tests/military_tests.cpp`.

Projects compete for capacity; unfinished/paused work receives no completion reward; repeated
reforms and equivalent treaties cannot farm output. Expiry and equivalent withdrawal agree.
Replacement requires time and resources; peace permits gradual demobilization without erasing
formations or creating population. Rich and poor fixtures differ in capacity without exempting
either from costs. Set numeric calibration bands after baseline scenario measurement and record
their rationale before accepting this phase.

## I · Recalibrate pacing using the corrected accounting

- Replace the month-twelve attack multiplier cliff with bounded exhaustion derived from sustained
  operations, actual attrition, supply, and funded recovery. Rest and peace recover gradually.
- Use one capture-progress/budget rule for explicit offensives and automatic momentum. Orders
  can set priority and posture, but cannot bypass a strength threshold the autonomous process uses.
- Keep coarse regions and a bounded monthly capture cap. Weight required progress by available
  area/population/geographic extent and supply distance, so very different administrative regions
  are not all equal effort. Document proxy limits; do not present population alone as terrain.
- Replace the first-twelve-regions cutoff with exposure-weighted accounting over the real front.
  Bound display/context lists separately from simulation coverage. Use stable IDs only for exact
  ties, never as the primary reason one region suffers all losses while another is omitted.
- Measure 24–60 month scenarios before setting new constants: parity/stalemate, overwhelming
  superiority, two fronts, overseas expedition, occupation, insurgency, and recovery after peace.
  Compare distributions and conservation, not only a handful of favorable matchups.

Files: `src/status/force.cpp`, `src/status/status_internal.h`, `src/status/monthly/`,
`src/world/compiler/force_ops.cpp`, `tests/military_tests.cpp`, `tests/status/real_world.cpp`.

Gate: no anniversary discontinuity; deterministic tie handling; materially equivalent renamed
fixtures have equivalent aggregate exposure/losses; tiny and huge regions require different effort;
explicit and automatic advances share accounting. Do not weaken existing assertions merely to
get green tests—document which old calibration assumptions were intentionally superseded.

## J · Preserve timely decisions and make the rules visible

### Scheduling

- Keep the four reactive-actor cap and existing independent-slot budget. Rank outstanding treaty
  answers, direct attacks, acute domestic crises, and unresolved war decisions above routine name
  mentions. Persist pending decision age and last service turn so retries use the same selection.
- Use urgency plus aging/fair rotation within urgency classes. If urgent actors exceed capacity,
  queue rather than silently drop them. Test finite backlogs for bounded service; do not promise
  every actor a same-month decision under an unbounded stream of new emergencies.
- Deterministic war, project, treaty-expiry, and population maintenance runs for every actor each
  month regardless of whether it receives a model slot.

### Context and UI

- Expose proposal signatures/revisions, hostile parties and partial peace, valid access, deployment
  and supply limits, occupation/integration, actual refugee returns, paused work, available effort,
  and ongoing trade/replacement concisely through shared status builders.
- Update country/region inspectors, treaty answers, and journal summaries where a player needs
  the information to choose an action. Avoid raw accounting internals in the normal interface.
- Keep model facts compact and remove obsolete prompt instructions rather than accumulating long
  prose rules. Measure schema bytes, prompt tokens, latency, and malformed/failed outcome rates on
  the current configured model; keep the existing context size and call budget unless evidence
  establishes a necessary change. Use honest failure/partial text after deterministic rejection.

Files: `src/turn/{plan,adjudication_input}.cpp`, `src/core/politics.cpp`,
`src/status/{helpers,agreements}.cpp`, `src/inference/prompts.cpp`, `src/store/event_log.cpp`,
`web/app.js` and relevant UI assets, `tests/{politics_tests,context_tests,turn_tests}.cpp`,
`tests/browser.mjs`.

Gate: unrelated mentions cannot crowd out a direct opponent indefinitely; pending proposals and
regional demands receive service; cancel/retry preserves selection. UI facts agree with actual
rules and accepted outcomes. No misleading victory, return, completion, or treaty claim remains
after the engine rejects the corresponding operation.

## Final verification and delivery

1. Run targeted suites after each changed subsystem, then `make test` and the repository format
   check for the finished implementation. Record actual suite counts rather than copying the
   audit's eight-suite baseline.
2. Run `python3 tests/integration.py` against disposable saves with captured logs. Cover proposals,
   partial peace, migrations, project pauses, new state persistence, cancellation, and restart.
3. Run browser checks for revised treaty/war/project/displacement surfaces. Keep browser evidence
   distinct from build/unit evidence.
4. Export and run `./build/replay_verify <export.json>` for old fixtures, each new scenario, and a
   mixed-version upgrade campaign. Compare state and events without model calls.
5. Run deterministic 24–60 month scenario tests for each conservation and pacing invariant. Report
   population, inventories, displacement, GDP, stability, control changes, and unrest trajectories.
   Review idle seeded-world behavior alongside stressed synthetic cases.
6. Run a bounded live-model campaign matrix on temporary saves: rejected treaty, accepted/revised
   treaty, attempted overreach, sustained war/peace, regional demands, and repeated economic orders.
   Repeat key adjudication cases at least three times. Record successes, failures, token/context
   usage, and latency; no live-model test should be confused with deterministic correctness proof.
7. Update `README.md`, `docs/EVOLVING_WORLD.md`, `docs/MODELS.md`, `docs/API.md`,
   `docs/ARCHITECTURE.md`, and `docs/VERIFICATION.md` for the final behavior and evidence. Update the
   audit with resolution pointers rather than erasing its original observations.
8. Mark completed phases and remaining limitations here. Mark Plan 26 done in `plans/README.md`
   only when all fourteen findings have an implemented outcome and the relevant acceptance gates
   pass. An unavailable model/browser dependency must be reported as incomplete verification.

Primary risks are save compatibility, double-charging across monthly stages, unrealistic migration
assumptions, excessive prompt/schema growth, and calibration that suppresses all activity. The
phase gates above are intended to catch those risks before further features are built on them.

## Implementation checklist

- [x] A — Legacy fixtures, complete lifecycle versioning, and deterministic migrations.
- [x] B — Explicit proposal consent and outcome-grounded effects.
- [x] C — Hostile pairs, scoped peace/intensity, and conflict deduplication.
- [x] D — Valid access/sea routes, supply bounds, and captured construction.
- [x] E — Persistent conquest consequences and local resistance.
- [x] F — Conserved force allocations and fractional, exposure-based attrition.
- [ ] G — Origin–host displacement and actual return migration.
- [ ] H — Paid progress, recurring trade, replacement, and demobilization.
- [ ] I — Evidence-based combat pacing and exhaustion.
- [ ] J — Fair urgent reactions, accurate presentation, and complete verification.
- [ ] Post-implementation audit — every original finding and phase checked against fresh evidence;
      unresolved items recorded explicitly.

## Post-implementation audit prompt

Run this prompt after phases A–J are implemented, before declaring the plan fully accepted.
The audit is a separate verification pass: completed checkboxes and passing existing tests are
inputs, not proof that nothing was missed.

```text
Re-audit LocalHistoria's simulation after implementation of
plans/26-simulation-realism-and-consistency.md. Check that no original realism gap, planned
requirement, acceptance condition, or important interaction was left behind, and look for new
regressions introduced by the implementation.

Read Plan 26, the original audit at docs/audits/2026-09-22-simulation-realism.md, its standalone
probe, and the current implementation and tests. If the original audit/probe is unavailable in
this checkout, use Plan 26's fourteen-finding coverage and phase acceptance criteria as the
baseline, explicitly record the missing evidence, and reconstruct focused reproductions.
Inspect the current branch and working tree first. Preserve unrelated edits and use disposable
campaign databases; never modify saves/campaign.sqlite. This request is an audit, not permission
to silently fix production code or recalibrate the simulation.

Produce a traceability matrix covering all fourteen original findings, every phase A–J, and
their acceptance conditions. For each item give its status (resolved, partial, unresolved,
regressed, or unverified), current code references, the relevant test/probe and actual result,
and any remaining limitation. Do not mark an item resolved solely because documentation or a
test name says it is implemented.

Reproduce the original failures against the new rules and add focused adversarial probes where
coverage is missing. Check rejection/stale-consent handling, partial peace in multi-party wars,
failed-operation rewards, access and inland/overseas movement, annexation and occupation
resistance, duplicate conflicts, shared force budgets and fractional attrition, displacement
and actual returns, economic/reform/trade repetition, captured projects, equipment replacement,
demobilization, combat pacing, and urgent reaction fairness.

Pay special attention to cross-system sequences: capture then annex then unrest; ceasefire
with one opponent while another attacks; treaty changes while proposals await answers;
third-party occupation during cession; capture/recapture during construction; refugee flight,
secondary displacement, death and return across changing borders; simultaneous fronts drawing
on one inventory; repeated orders and retries charging costs or benefits twice.

Run appropriate build/unit, integration, browser, and bounded live-model checks. Use deterministic
24–60 month scenarios for conservation, exhaustion/recovery, economic compounding, resistance,
and quiet-country behavior. Verify old exports and mixed-version upgraded campaigns replay to
identical state and events without inference, including lifecycle operations before monthly
status. Check save/reload, cancellation, retry, and migration idempotence. Inspect whether model
schemas, API responses, context, and UI describe what the engine actually permits.

Compare model call counts, prompt/schema size, latency, and adjudication reliability with the
recorded baseline when comparable evidence exists. Separate source inspection, deterministic
tests, integration/browser evidence, live-model evidence, and external calibration. If a tool,
fixture, or model is unavailable, report the exact gap as unverified rather than passing it.
Passing a gameplay invariant does not by itself establish historical realism.

Save a dated follow-up report in docs/audits/ with the traceability matrix, prioritized remaining
and newly discovered findings, reproduction steps, expected versus actual results, exact current
code references, suggested bounded fixes, and verification limitations. Distinguish confirmed
defects from design tradeoffs and optional improvements. State whether Plan 26 is fully accepted;
if not, list precisely what remains. Do not erase or rewrite the original audit evidence, and do
not claim that absence of observed failures proves the absence of all realism gaps.
```
