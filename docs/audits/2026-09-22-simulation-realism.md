# Simulation realism audit — 22 September 2026

The largest realism gaps are in the connections between existing systems: consent, war participation, territorial control, force allocation, displacement, and economic effects. Closing these gaps would improve plausibility more than adding another layer of simulation.

This is an audit of the current working tree, including the existing uncommitted compiler changes. No production code was changed. Findings marked **probed** were exercised against the current core library; **source-confirmed** means the rule is visible in code but its campaign frequency was not measured. Improvements are design recommendations, not claims of historically calibrated outcomes.

## Highest priority: rules that admit contradictory outcomes

### 1. Mentioning a partner counts as consenting to its treaty — probed

`collectConsent` scans model-generated outcome descriptions for another nation's name. It does not distinguish acceptance from rejection, threats, or unrelated references. `consent` then treats that mention as agreement to the proposed terms. This can also bypass the player's explicit proposal flow when a player event mentions the proposer.

**Probe:** “Bland rejects Aland's trade offer” authorized an open Aland–Bland trade agreement.

**Improve:** use explicit acceptance tied to a particular proposal and its terms. Player acceptance must come from submitted player input. AI acceptance should be a structured decision, not inferred from narration. Retain pending proposals until accepted, rejected, or expired.

**Acceptance:** rejection, threats, blocked events, and an unrelated mention never authorize a treaty or cession; an explicit acceptance authorizes only the accepted terms.

Sources: [compiler.cpp](../../src/world/compiler/compiler.cpp), `collectConsent`; [treaty_terms.cpp](../../src/world/compiler/treaty_terms.cpp), `consent`.

### 2. War termination and ceasefires have the wrong scope — probed

Any party can call `resolve_conflict` and end the entire war without agreement or a defeat condition. Any party can also set the whole conflict's intensity. In a three-party war, a ceasefire involving any two parties suppresses casualties and displacement for all parties. Meanwhile, automatic advances check peace pair by pair, so movement and war toll can disagree.

**Probes:** Aland alone resolved its war with Bland. An Aland–Bland ceasefire set a war involving Cland to `intensity: none`, with no affected regions.

There is no explicit coalition/side model: force calculations and border drawing generally treat other listed parties as opponents, even though automatic movement has a treaty-based exception for allies.

**Improve:** represent hostile pairs or sides within existing conflicts. Scope ceasefires, intensity, losses, and withdrawals to those pairs. Require an accepted settlement or a defined capitulation condition to resolve the whole war. Deduplicate overlapping declarations so the same fighting is not charged twice.

**Acceptance:** A–B peace leaves B–C fighting active; allied participants never count as enemy fronts; unilateral withdrawal cannot order the opponent to stop fighting.

Sources: [treaty_ops.cpp](../../src/world/compiler/treaty_ops.cpp), `opResolve`, `opIntensity`, `opOpen`; [fronts.cpp](../../src/status/monthly/fronts.cpp), `atPeace`, `addFront`; [force.cpp](../../src/status/force.cpp), `fronts`.

### 3. Failed operations can still deliver success bonuses — probed

An event whose operations all fail gets `failed: true`, but `happened` remains true and its model-provided effects are applied. Partial events also retain effects without tying them to the surviving operations.

**Probe:** building a city in foreign-controlled territory was rejected, but the event retained `strong_growth` for both economy and stability and was marked a major event.

**Improve:** derive effects from accepted operations and separately validated non-structural outcomes. Failed attempts can have costs or diplomatic consequences, but should not keep the benefits of the rejected action.

**Acceptance:** an impossible construction cannot increase GDP because of that construction; a successful negotiation within a partially failed event can retain its independently justified effect.

Source: [events.cpp](../../src/world/compiler/events.cpp), `buildEvent`, `settleEffects`.

### 4. Units can move through foreign territory without access, and fleets can move inland — probed/source-confirmed

Army movement checks adjacency and readiness, but not military access or hostile entry. Fleet movement checks only straight-line distance, not coast, sea route, or access. `reaches` then allows active formations to provide an invasion origin. A friendly border or a fleet marker consequently grants access to national offensive strength without a transport or supply constraint.

**Probe:** a fleet moved from an inland region into another country's inland region within 1,500 km. The move required no war or access agreement.

**Improve:** use existing controllers, coastal flags, adjacency, and treaties to validate destinations. Separate peaceful transit from contested entry. Require a coastal destination for fleets and a bounded embarked force for overseas attacks; even a coarse supply-connectivity rule would help.

**Acceptance:** no neutral transit without access, no inland fleets, and no fleet marker delivering the country's entire available offensive force to a distant coast.

Sources: [force_ops.cpp](../../src/world/compiler/force_ops.cpp), `moveEntity`; [geometry.cpp](../../src/world/geometry.cpp), `reaches`; [force.cpp](../../src/status/force.cpp), `committedForce`.

### 5. Annexation erases the mechanical distinction between conquest and integration — source-confirmed

After holding a region before the current month, an occupier can set both owner and controller to itself. Occupation GDP drain, occupation grievance, and the occupied-population stability penalty key off `owner != controller`; annexation removes that condition immediately. Existing unrest can remain, but its occupation driver disappears. Population and GDP also become the conqueror's national totals immediately.

Related sovereignty hole: an owner can cede a region controlled by a third party, and the transfer overwrites that third party's control. Treaty cession/occupation clauses similarly do not require the current third-party controller to agree.

**Improve:** preserve conquest origin, occupation duration, and integration state independently of legal ownership. Keep resistance and impaired productive access after unilateral annexation. Cession should transfer the claim without evicting an uninvolved occupier.

**Acceptance:** annexing yesterday's conquest does not erase resistance or restore its full productive contribution; signing away occupied land does not teleport the third party out.

Sources: [territory_ops.cpp](../../src/world/compiler/territory_ops.cpp), `transfer`; [treaty_terms.cpp](../../src/world/compiler/treaty_terms.cpp), `applyTerm`; [monthly_status.cpp](../../src/status/monthly/monthly_status.cpp), `occupationDrain`; [unrest.cpp](../../src/status/monthly/unrest.cpp), `regionalGrievance`; [helpers.cpp](../../src/status/helpers.cpp), `totals`.

### 6. Occupation resistance depends on the occupier's domestic decline, and existing wars can block revolt — probed/source-confirmed

Regional unrest targets multiply grievance by the controller's decline from its campaign-start stability. If that decline is no more than eight points, even severe occupation grievances produce a zero unrest target. Separately, `formRebellions` excludes every region in any open conflict's theatre or affected regions, rather than only regions already represented by the relevant rebellion.

**Probe:** an occupied region under a stable foreign controller accumulated no unrest. The existing-war exclusion is source-confirmed.

**Improve:** let foreign occupation generate local resistance independently of the occupier's national stress, with national stress acting as a modifier. Narrow the duplicate-rebellion guard to the relevant insurgency. Domestic political demands should also have a bounded path to unrest without requiring an unrelated national collapse first.

**Acceptance:** a stable occupying state can face resistance; an occupied war theatre can develop an insurgency; a quiet country does not spontaneously fragment merely because its seeded stability is low.

Source: [unrest.cpp](../../src/status/monthly/unrest.cpp), `nationalStress`, `accrueUnrest`, `formRebellions`.

## High value improvements within the current feature set

### 7. Equipment attrition rounds away, while military recovery is largely absent — probed/source-confirmed

Aircraft and ship losses are floored independently every month. At high intensity, fewer than 334 aircraft or 667 ships produce zero losses per conflict from these formulas. Fractions never accumulate. Opposing aircraft/ships trigger these losses without checking their local presence; ground threats alone cannot cause aircraft losses through this rule.

**Probe:** after 24 high-intensity months, an initial 100,000 active personnel fell to 89,883, while 100 aircraft and 30 ships remained unchanged.

There is no ordinary procurement/recruitment recovery loop in the monthly rules. Building an army or fleet allocates existing personnel or ships; it does not replace losses. High-intensity war moves reserves into active service, but peace has no corresponding demobilization mechanism.

**Improve:** persist fractional attrition, tie exposure to the theatre, and add slow capped replacement and demobilization using existing military spending/inventories. Keep formation assembly distinct from procurement.

**Acceptance:** small forces accumulate equipment losses over time; quiet theatres do not sink distant ships; replacement takes time and competes with existing construction capacity.

Sources: [casualties.cpp](../../src/status/monthly/casualties.cpp), `partyLosses`; [recovery.cpp](../../src/status/monthly/recovery.cpp), `applyInventory`; [force_ops.cpp](../../src/world/compiler/force_ops.cpp), `allocateForce`.

### 8. Force availability and losses are not consistently tied to deployment — source-confirmed

Defending land/air strength divides by the number of enemies; attacking strength uses a divisor of one. Naval strength is not divided either way. Local armies can contribute to each neighboring target calculation without a shared committed-force budget. Conversely, personnel losses are distributed across all active and under-construction armies, regardless of which were exposed.

**Improve:** allocate the existing national pool once per month across hostile fronts; reserve explicit deployed formations for those fronts. Apply losses first to the exposed formations and their supporting pool. Use the same accounting for assault, garrison, and casualty calculations.

**Acceptance:** opening a second front reduces available attacking strength; a formation cannot fully cover several simultaneous battles; an army being assembled at home does not take front-line deaths.

Sources: [force.cpp](../../src/status/force.cpp), `committedForce`, `committedPersonnel`; [recovery.cpp](../../src/status/monthly/recovery.cpp), `applyInventory`.

### 9. Refugee returns have no origin–destination accounting — probed/source-confirmed

Flight moves people into specific domestic or foreign regions, but only an aggregate displaced count remains at the origin. Return migration draws population from any same-owner region, not the actual host. It never retrieves refugees from foreign hosts. If no domestic source exists, it still decreases the displaced counter without moving anyone home.

**Probe:** an isolated region's displaced count fell from 1,000 to 970 with no change to its population.

**Improve:** record small origin–host displacement balances and return from those balances only. Keep cumulative displacement events separate from people currently away.

**Acceptance:** return migration conserves people, retrieves them from the recorded host, and does not decrement outstanding displacement when nobody returned.

Sources: [casualties.cpp](../../src/status/monthly/casualties.cpp), `displace`, `move`; [recovery.cpp](../../src/status/monthly/recovery.cpp), `returnDisplaced`.

### 10. Economic outcomes lack resource competition and durable implementation — source-confirmed

Construction is limited by concurrent project count derived from GDP, but has no financial or upkeep cost. GDP is productive output, so treating its size as the only affordability gate cannot distinguish several competing promises. Non-map reforms and investments rely on model effects rather than tracked progress. Positive stability effects also ratchet the long-run baseline upward.

The GDP effect cap is ±1.5% per month. Reaching the positive cap repeatedly would compound to roughly 19.6% extra output in a year before baseline growth. This is a bound, not evidence that normal play reaches it; the risk is that repeated cheap actions can earn benefits without persistent costs or diminishing returns.

**Improve:** add a modest monthly spending/implementation allowance derived from existing output, concurrent commitments, and military spending. Track promised reforms as ongoing efforts with completion and recurrence limits. Apply maintenance drag through the existing monthly economy rules. A full sectoral economy is unnecessary for this pass.

**Acceptance:** several projects compete for resources; repeating a reform does not repeatedly collect its implementation reward; unfinished projects cannot claim completed benefits.

Sources: [force.cpp](../../src/status/force.cpp), `constructionCapacity`; [force_ops.cpp](../../src/world/compiler/force_ops.cpp), `opBuild`; [monthly_status.cpp](../../src/status/monthly/monthly_status.cpp), `modelEffects`; [recovery.cpp](../../src/status/monthly/recovery.cpp), `driftStability`.

### 11. Trade rewards signing, with inconsistent end-of-treaty treatment — source-confirmed

Each new trade agreement/union forces a growth effect, with no equivalent-agreement check or scale based on partner size. Existing treaties do not supply an ongoing deterministic economic relationship. Explicitly ending one forces a decline, while natural expiration in `resolve` does not run that trade-loss path.

**Improve:** deduplicate by partners and economic purpose, make amendments update the existing relationship, and derive a small continuing benefit from its active state. Withdrawal and expiration should use the same lifecycle consequences, with diminishing returns for overlapping agreements.

**Acceptance:** re-signing the same deal cannot repeatedly increase output; expiry and equivalent withdrawal have consistent effects; adding a tiny partner does not automatically equal adding a major market.

Sources: [treaty_ops.cpp](../../src/world/compiler/treaty_ops.cpp), `opOpen`; [events.cpp](../../src/world/compiler/events.cpp), `settleEffects`; [treaty_terms.cpp](../../src/world/compiler/treaty_terms.cpp), `ending`; [simulation.cpp](../../src/core/simulation.cpp), `resolve`.

### 12. Combat pacing follows administrative boundaries and an abrupt war-age penalty — source-confirmed

Movement and capture limits count regions, regardless of their size. Fronts truncate to the first twelve affected regions in stable insertion/ID order. Assault strength drops by 40% once a shared conflict reaches twelve months, irrespective of supplies, losses, or replacements. Explicit orders can capture at capacity one, while automatic momentum requires capacity two; sparse AI activation therefore affects how effectively similarly balanced forces advance.

**Improve:** retain coarse regions, but weight capture progress by area/population and supply distance; use meaningful frontage/exposure weights when limiting affected regions. Replace the anniversary penalty with gradual pressure from observed losses, duration, and recovery. Align ordered and automatic advance budgets.

**Acceptance:** renaming region IDs does not change which population bears the war; crossing month twelve does not instantly reduce an otherwise unchanged army's effectiveness by 40%.

Sources: [force.cpp](../../src/status/force.cpp), `assault`; [fronts.cpp](../../src/status/monthly/fronts.cpp), `accrue`, `addFront`; [force_ops.cpp](../../src/world/compiler/force_ops.cpp), `take`, `moveEntity`.

### 13. Captured construction can complete for its original builder — source-confirmed

`resolve` activates every due construction without checking whether the builder still controls the region. A newly built army can therefore become active in enemy-controlled territory; captured ports, bases, and cities likewise have no automatic capture/interruption policy. This depends on the adjudicator explicitly handling every affected project.

**Improve:** pause, capture, evacuate, or destroy construction when control changes, according to entity kind. Recheck control before completion.

**Acceptance:** an army cannot finish recruitment behind enemy lines merely because its timer expired; civilian construction has an explicit ownership/control outcome.

Source: [simulation.cpp](../../src/core/simulation.cpp), `resolve`.

### 14. Limited AI slots can crowd out urgent reactions — source-confirmed

Reactive actors are capped at four. Player mentions are considered before ongoing wars and their allies, apart from pending-proposal insertions. Enough named actors can therefore crowd out an unmentioned belligerent's reactive slot. Independent national slots help, but do not guarantee that belligerent acts. Most background nations receive trend/war updates without frequent decisions about peace, reform, or regional demands.

**Improve:** keep the inference budget, but reserve reaction priority for attacks, treaty answers, acute unrest, and unresolved war decisions. Age unanswered decisions so they eventually outrank routine activity. Cheap deterministic maintenance can run between model decisions.

**Acceptance:** mentioning several unrelated countries cannot suppress an opponent's urgent response; long-running wars and demands eventually receive a decision without player prompting.

Sources: [plan.cpp](../../src/turn/plan.cpp), `TurnRunner::plan`; [politics.cpp](../../src/core/politics.cpp), `scheduleInitiatives`.

## Recommended sequence

1. Correct consent, conflict scope, and failed-event effects. These are rule-integrity fixes.
2. Correct movement/access, annexation persistence, and occupation resistance.
3. Correct fractional attrition, force allocation, and displacement balances.
4. Connect economic effort and treaty lifecycle to ongoing consequences; handle captured projects and reaction priority.
5. Run multi-year scenario comparisons before recalibrating combat pacing. Preserve versioned replay semantics when changing monthly rules.

Keep the existing deterministic C++ authority, explicit inventories, ownership/control split, regional history, and bounded model effects. They provide the foundations needed for these changes.

## Verification and limits

- Current checkout built successfully with `cmake --build build -j4`.
- `ctest --test-dir build --output-on-failure`: **8/8 suites passed**, 26.21 seconds.
- Eight focused observations were produced by [the standalone audit probe](2026-09-22-realism-probe.cpp). These exercise the operation compiler and monthly rules directly; they do not estimate how frequently a live model generates the problematic inputs.
- No browser, API integration, live-model campaign, or external historical-data validation was run. Existing tests passing does not establish realism; several findings are accepted behaviors that the current tests do not challenge.
- Production files and pre-existing user edits were left unchanged. Only this report and its probe were added.

To reproduce on the current Homebrew-based macOS setup after building:

```sh
c++ -std=c++20 -Iinclude -I/opt/homebrew/include \
  docs/audits/2026-09-22-realism-probe.cpp build/libhistoria_core.a \
  -L/opt/homebrew/lib -ljsoncpp -lsqlite3 -o /tmp/localhistoria-realism-probe
/tmp/localhistoria-realism-probe
```

The include/library paths may need adjustment on another platform. The probe uses synthetic countries and does not read or modify campaign saves.
