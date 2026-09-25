# Evolving campaign world

Campaigns now persist actor identities, fixed territories with independent ownership/control, located entities, conflicts, commitments, and recorded model deltas. Political state and identity revisions are additive; existing saves and replay exports remain readable.

## Geography

The checked-in `data/source/gbOpen/index.json` pins the geoBoundaries gbOpen ADM1 API response. Individual URLs contain source commits; `manifest.json` retains source licenses, attribution, dates, source checksums, coverage status, rejected overlaps and the final geometry checksum. `licenses/geoBoundaries-NOTICE.md` gives attribution. This is a modified game geography, not an authoritative boundary product.

The importer clips ADM1 polygons against supplied country outlines, rejects overlapping subdivisions, and emits uncovered or unresolved territory explicitly. Source countries without a usable downloaded subdivision snapshot remain single territories. A final reconciliation removes overlap slivers and retains uncovered claim slivers as named residuals. Coordinates use a 0.00000001-degree grid. Geometry and source IDs remain fixed during play; ownership/control are campaign data. Initial affiliation never changes.

The reference database retains all supplied cities and separately indexes locations verified inside a region. Campaigns seed the three largest located cities per initial actor. New ports, bases, armies, cities and event sites persist in campaign state. Entity requests apply viewport/zoom filters and cap responses at 200. Unseeded reference cities are retained as reference data, not campaign entities.

**Authoritative vs. display geometry.** The full geometry lives only in `data/world.sqlite` `regions.geometry`. `containsPoint`, adjacency, interiors and replay use it; `manifest.json` `geometry_sha256` is its checksum (`display_geometry.geometry_digest`). The browser gets a derived copy, `tools/display_geometry.py`:

- `web/regions.topojson` (8.5 MB, 3.0 MB gzipped, from 124 MB of GeoJSON). All region boundaries are snap-rounded together on a 0.000001° grid and re-polygonized, so neighbours share every border vertex. Rings are cut into arcs at junctions, each shared arc is stored once, and each arc is simplified once (Douglas-Peucker, 0.01°), so borders cannot open gaps. Arcs are then quantized to 10⁶ steps and delta-encoded. Polygon parts narrower than 0.005° or smaller than 0.000025 deg² are dropped for display, always keeping each region's largest part. These are almost all residual slivers where subdivision coastlines miss the country outline. Some simplified polygons self-touch; the canvas fills even-odd, and `regions_test.py` checks each one against its authoritative outline.
- `web/world.geojson` (3.1 MB, 0.9 MB gzipped), the pre-campaign country outlines simplified per feature at 0.02°.

Each is written with a deterministic `.gz` that Drogon serves as-is (`gzip_static`). `/api/v1/map` versions the region URL by content hash, so the browser caches it as immutable. HTML, JS and CSS are `no-cache` (revalidated by Last-Modified). The browser builds the region layer once and on later months only replaces properties and restyles. It keeps the topology's arcs so the Countries map mode can mesh national borders (and dashed occupation lines) from current ownership.

Rebuild with `make geography-env` then `make data` when inputs change; `make display` rebuilds only the display files from the existing SQLite (about 1.5 minutes). For an explicit rebuild, run `.venv-geography/bin/python tools/import_data.py` followed by `.venv-geography/bin/python tools/import_regions.py` (which ends with the display pass). Importing existing pinned files is offline and verifies their checksums. `--download` explicitly permits fetching missing pinned source files. Run `make geography-test` for exhaustive overlap, coverage and source verification.

## Monthly simulation

There are no fixed action types or costs. National status (population, GDP, stability) grounds decisions and evolves deterministically; see [National status and war](#national-status-and-war). Each month the player writes up to eight free-text actions, each optionally pinned to up to four region/entity/actor targets (clicking a map region adds one to the selected action). A turn:

1. Reserves two independent national actions and one independent regional action — the regional slot going to a region with something at stake, a front, occupied land or an unanswered demand, before a quiet one — then picks up to four reactive AI nations by urgency (Plan 26 J): nations owing an answer to a pending treaty proposal (oldest first), then nations fighting the player, then linked nations in an acute domestic crisis (collapse under way, stability below 20, or a rising in the last three months), then linked nations with a war of their own, then nations the player's actions name (matched on whole words) or target, then other linked nations (conflict and small-commitment parties, an enemy's allies, one rotating neighbour). Within a class the nation served least recently goes first, so a nation left out stays at the front of its class next month and a finite backlog is always served. The month's choice is saved (`monthly_reactions`), and a cancelled or retried month naming the same nations asks the same ones. Each writes one free-text action of its own, without thinking; nations the player's actions name or that are in an open conflict with the player write up to two, since those are the months that matter. An independent slot whose answer is a ceremony — a town hall, a heritage or awareness declaration, a festival — is asked once more for something at stake, and the second answer stands whatever it is; a slot spent on a gesture is a decision call and an adjudicated outcome that change nothing.
2. Runs `resolve`: advances the month and completes construction whose `completes_turn` has arrived, recording a factual completion event.
3. Asks the model to adjudicate every action together and compiles its answer into a validated delta. It uses a bounded reasoning pass when an action concerns war, military moves, borders, territory, independence, sanctions or treaties, or when state-derived offensive options are present for an acting nation. The latter covers wording such as “drive to the capital” without adding attack keywords; quiet months without either signal are answered directly.
4. Applies `applyMonthlyStatus`: demographic and economic trends, model effects, occupation, autonomous front advances, war fronts, casualties, displacement and stability. Advances change control only and produce authoritative `front_advance` events before the toll.
5. Commits state, delta, events and inference records in one transaction, then requests narration.

Actor IDs are independent of country codes. A landless actor keeps playing.

`WorldDelta` contains `changes` and structured `events`. Changes contain `kind`, `id`, exact `before`, full `after`, supporting `event`, and `rationale`. Supported collections are actor, territory, entity, conflict, commitment, regional politics and treaty proposals. Deltas are what is validated, saved and replayed.

The model does not write deltas directly. Asking a 4B model for exact `before`/`after` objects produced invalid output in most scenarios. Instead it returns compact **operations nested in events**, and `compileOperations` in `src/world.cpp` derives the exact delta:

- **Events.** Each event has `actor`, `background`, `blocked_by`, `causes`, `description` (one sentence, at most 160 characters) and `operations`, plus optional `effects`.
  - The event ID is `t<TURN>-<slug of description>`, deduplicated.
  - Related actors, regions and entities are derived from its operations.
  - An event may have no operations: reforms, trade, research and negotiations are recorded as history.
  - At most three background events are kept.
- **Operations.** Each is named by `action`: `build`, `transfer_territory`, `occupy`, `restore_control`, `move_entity`, `set_entity_status`, `create_actor`, `open_conflict` (with `kind`, `name` and starting `intensity`), `set_conflict_intensity` (parties only), `resolve_conflict`, `add_commitment` (with `kind` and `name`), `resolve_commitment`. Alliances and treaties are commitments; armies and fleets are entities.
- **Effects.** An event may list up to three `effects`: `{actor, economy, stability}`, each one of `severe_decline`, `decline`, `none`, `growth`, `strong_growth`. Absent means none; a world event must list at least one. Invalid or unknown effects are dropped; blocked or rolled-back events have none. An event keeps its effects when only some of its operations were dropped.
  - C++ corrects the sign when the accepted operations make it obvious, since the small model rewards aggression and punishes victims at random. Parties to a war or civil war opened this month, to any conflict opened at or escalated to `high` intensity, and whoever took a region by force, get economy at most `decline`, and the attacked parties (every party but the event's actor) stability at most `decline`. Parties to a `trade_agreement` or `union` get economy at least `growth`. An owner whose region was occupied or annexed gets stability at most `decline`. A missing entry counts as `none`/`none` before these bounds apply, so an omitted aggressor ends with `decline`/`none` and an omitted victim with `decline`/`decline`. Forced entries survive the three-effect cap; the model's own weakest ones are dropped first.
- **Schema constraints.**
  - Region, entity, event-actor, conflict, commitment and cause references are restricted to IDs present in the supplied facts.
  - `blocked_by` accepts only `none` or an open commitment the event's actor is party to, so a denial cannot cite an invented rule or someone else's treaty. C++ also ignores a commitment cited by a non-party, and records no breach when every operation of the event was dropped.
  - Each outcome is pinned to its action's actor and order and opens with the action's first six words (`action`). Without an opening the model repeated one outcome for all of a nation's actions; repeating the whole text cost dozens of tokens per event.
- **IDs and placement.**
  - C++ assigns entity IDs (`e<TURN>-<name slug>`) and places new or moved entities at the region's verified interior point.
  - Conflict and commitment IDs are also assigned by C++.
  - New actor IDs come from the actor name. Later operations may refer to a new actor by its requested ID or its name.
- **Key order.** jsoncpp serializes schema keys alphabetically, and llama.cpp grammars keep that order. Key names are chosen so `description` precedes `operations` and `action` leads each operation.

### Realistic pacing

One turn is one month. The prompt tells the model to advance large goals by a step. C++ enforces physical limits it cannot talk its way around, judging everything against the start-of-month world:

- **Reach.** Force (`occupy`, an owner retaking land, destroying another nation's entity) reaches a region only if it borders land the actor controls, or holds or borders one of its active armies or fleets. Residual sliver regions grant no reach. Under the plan 26 lifecycle a fleet reaches a coastal region along a sea route of at most 1,500 km (`sea_routes`, see [DATA.md](DATA.md)), not across a land border.
- **Strength.** Quality is `clamp(sqrt(GDP per capita / 20000), 0.6, 1.4)`. Local active armies plus 20% of uncommitted personnel (30% in an open war/civil war), 30% of combat aircraft × 200, and eligible naval support form attack strength. Defence uses 25%/40% uncommitted personnel, 5% of reserves and 50% aircraft divided across distinct enemies, a 1.5 advantage, and the same naval support. Naval support is 30% of warships × 1,500 where the region is coastal, a port/fleet/port-marked city exists, or the nation's fleet is nearby. Civil war multiplies defence by 0.6; stability below 25 by 0.8. An attacker in a shared conflict at least twelve months old gets ×0.6. Peaceful neighbours are not enemies. Under the plan 26 lifecycle, supply bounds this: the uncommitted-personnel and aircraft shares reach an attack only through a *supplied* region the attacker holds on the target's border, one linked to its *heartland* (its most populous region that it owns and controls) through land it may enter, sea routes, or an ocean basin shared by coasts it may enter, at any distance. An enclave with no friendly way home (Nakhchivan) is cut off; Hawaii, across the Pacific, is not. A nation with no heartland is never cut off. An army in an unsupplied region counts at half strength. Overseas, only troops embarked with a fleet whose routes reach the target count: armies sharing that fleet's region, up to 500 troops per warship. An empty fleet carries no land force. A defender's region cut off from home gets half of its national share.
- **Tempo.** Attack/defence ratios of at least 1.5 allow two regions; at least 0.75 allows one; below 0.75 allows none. Limits apply per attacker/defender, alongside a global maximum of two regions per actor. Formations are subtracted from uncommitted pools, never counted twice. Open `war` and `border_conflict` fronts also accumulate deterministic pressure at capacity 2: declared medium/high intensity gives `0.25/0.45 × log2(clamp(ratio, 1.5, 8) / 1.5) × jitter` per month. One pressure point takes one reachable region; ordered and autonomous gains share the same ceiling. Capacity 0/1 bleeds pressure instead. Low intensity and open peace agreements prevent autonomous advances. Both sides need inventories. Civil wars and insurgencies remain model-adjudicated because inventory ratios alone do not represent defections or popular support.
- **Capacity.** Concurrent construction is `max(1, floor(GDP / US$150 billion))`, excluding event sites. Armies reserve 10% of active personnel rounded to 1,000, clamped to 5,000–150,000 and available personnel; fleets reserve 25% of warships rounded to a whole ship, clamped to 1–30 and available ships. Unfinished formations also reserve inventory; inactive units release it. Destroyed units lose their allocation once. Reactivation requires available inventory.
- **Annexation.** An owner may cede land. Anyone else must have controlled the region since before this month, so conquest takes two steps: occupy, then annex a later month. Under the plan 26 lifecycle an owner hands land directly only to a new state it founds or to the nation already holding the region; any other nation receives land only through a treaty `cede` term it signs.
- **Movement.** Armies move one neighboring region per month and fleets up to 1,500 km. Other entities cannot relocate. Only an entity's owner moves it, and never while it is unfinished. Under the plan 26 lifecycle, an army may enter only land its nation holds, land nobody holds, or land whose controller granted it access by an open treaty's `grant_access` term; an alliance alone grants none. Hostile ground must first be taken by `occupy`, earlier in the same month or before. An army may also sail to a coast within a fleet's range when a fleet in its region can lift all of it. A fleet sails only to a coastal region along a sea route, into waters it may use or off an enemy coast; a fleet saved inland may put to sea on a bordering coast. Revoking access strands troops without removing them.
- **Construction.** `build` needs control of the region (except event sites). It creates `under_construction` entities finishing after: army 2 months, base 3, fleet 4, port 6, city 24. Status changes cannot complete construction or restore destroyed entities. Under the plan 26 lifecycle fleets and ports need a coastal region, and construction follows control. An agreed transfer (a cession, an agreed occupation, its withdrawal or expiry) hands the previous holder's unfinished port, base or city to the new holder on the same schedule, and disbands its unfinished armies and fleets, returning their troops and ships to its pool; a disbanded formation cannot muster on land its owner may not enter. Any other loss of control pauses the project with its remaining months. It keeps its builder and any reserved troops or ships, and resumes when the builder holds the region again. The new controller finishes a paused port, base or city only by building the same kind there, which takes it over and uses one of its construction slots. The owner may abandon a paused project, and an enemy in reach may destroy it. Pauses are engine-owned; no delta may create, alter or complete one.
- **Event sites.** An `event_site` marks where a notable incident happened. It is active at once with `expires_turn` three months ahead (`eventMapMonths`), and `resolve` makes it `inactive` when that month arrives, without a journal line. A later feasible event renews it for another three months when it involves the site or cites the site's originating event as a cause. Only active event sites carry `expires_turn`; sites saved before expiry existed keep none until an event touches them, so replay is unchanged.
- **New states.** At most one new state per month, and it must receive territory in the same event. A new state takes the model's `#rrggbb` colour, or one from the fixed actor palette when the colour is missing or invalid. A new campaign colours every country from that palette so that no two bordering countries share a colour.

**Authority.** An actor changes only what it has a say in:
- Only a party can open, escalate, scale down or end a war, and only a party can sign or end a treaty.
- A treaty needs consent from every other party: that party's own outcome this month (not a world event) must name the proposer as a whole word. A state created in the same event needs no consent. Wars need none.
- A nation builds only for itself (or for a state it creates in that event); event sites follow the same rule.
- Destroying another nation's entity needs an open war between the two, from before this month or declared earlier in the same month's outcomes, on top of reach.

**Treaty terms and duration.** The model drafts what a treaty changes when it writes `add_commitment`: `months` (how long it lasts, 0 = open-ended, at most 120) and up to four `terms`. The parties' signatures authorize the terms, and each clause is checked on its own; an impossible one is dropped (`dropped_operations` action `term <clause>`) and the treaty still stands:
- `end_conflict`: ends a war whose every side is a party (a peace treaty).
- `cede`: a party that owns a region hands it to another party, without the occupy-then-annex steps force needs.
- `occupy`: a party that owns a region lets another party hold it while the treaty lasts (an agreed occupation).
- `withdraw`: a party occupying another party's region hands it back.

Clauses that took effect are kept on the treaty as `terms` (a cession records the previous owner as `from`), and `expires_turn` marks the end of its term. Trade and other economic treaties usually carry no terms: signing one lifts the parties' economies, and ending one early costs them. An open ceasefire or peace treaty silences its war's toll (see `status.cpp`), so a war resumes when its ceasefire expires or breaks.

**Expiry.** `resolve` closes a treaty when its `expires_turn` month arrives, with a journal line ("… expired."). An agreed occupation ends with its treaty: the region returns to its owner if the occupier still holds it. The same happens when a treaty ends any other way.

**Amendment and renewal.** `amend_commitment` lets a party renew a treaty in force (`months` from now; 0 keeps its end date), revise its `text` and add `terms`, with the same consent rule as signing. At most eight terms are kept.

**Breach.** Acting against a treaty partner, by opening a war, occupying its land, destroying its units or escalating a war with it beyond low intensity, breaches any open ceasefire, peace treaty, non-aggression pact, alliance, defence pact or union between them. The event records `breaches` and becomes major. A two-party treaty is also broken and ended, which revives its war and ends its agreed occupations; a bloc such as NATO keeps standing.

**Allies.** A nation's decision facts list up to four wars that its alliance or defence-pact partners fight without it (`allied_wars`). Each month's reacting nations include the allies of the player's war opponents: every partner in a pact of up to three, and the largest economy of a larger bloc. Every treaty in the facts carries `months_left` and its terms in words.

**Treaty proposals.** A treaty or amendment an AI nation agrees with the player, when the player's own actions that month do not name that nation, becomes a proposal instead of being dropped. It is stored in `proposals` (`{from, actors, kind, name, text, months, terms, status, proposed_turn, event}`, plus `amends` for an amendment). A new treaty is keyed by the commitment ID it will take; an amendment by `<treaty>-r<turn>`. The event text adds that it awaits the player's answer, and its terms do nothing yet. The next month the player answers it in the side panel; `POST /turns` carries `responses`. `compileOperations` settles every due proposal first, as the month opens, before the month's actions:
- accepted: the treaty is created (or amended), its terms are carried out against the current map, and the player gets an event citing the offer. A trade agreement or union lifts every party's economy, as when signed outright.
- declined: an event by the player saying so.
- unanswered, or no longer possible (a party is gone or the amended treaty has ended): the proposal lapses with an event by the proposer.

Only these settlements carry the internal `settle_proposal` operation, so a model cannot answer for the player. Proposals are answered once, only in a later month, and are part of the recorded delta, so replay needs no extra input. Pending proposals appear in each party's decision facts (`treaty_proposals`), and the proposer acts in the month the answer arrives.

### Consent, outcomes and hostile pairs (realism lifecycle)

Under the realism lifecycle (Plan 26 B and C), and as corrected by the
[re-audit](audits/2026-09-23-simulation-realism-reaudit.md):
- **Explicit signatures.** Every treaty, amendment or cession between nations is a proposal with
  immutable terms, a `revision`, `required_signatories`, `answers` and `expires_turn` (12 months;
  the player still answers the next month). An AI proposer signs its own offer. Every other
  signature must be an explicit answer to that proposal ID and revision. The player answers with
  `responses` on `POST /turns`. A reacting AI nation answers in its decision's `responses`
  object, which its schema requires to hold one boolean per offered proposal: true signs, false
  declines. Narration, a mention, a refusal or a blocked event conveys nothing. An amendment opens
  a new revision that needs fresh answers. A settlement whose material terms have become
  impossible lapses as a whole. The same offer (kind, parties and terms) is not opened again while
  one awaits answers.
- **Outcomes without a map change.** Effects come from successful operations or from a supported
  independent result. The adjudication schema requires `outcome_kind` on every outcome. It follows
  `operations`, so the model writes its operations first; placed before them, a `negotiation`
  label talked it out of writing the treaty. Under the older project rules an effort starts only from an outcome with no
  successful operation. Under project version 1, validated project terms start funded work; an
  accompanying event-site marker does not replace the project. A setback starts no project. A protest can only cost stability. A world event keeps its text and its bounded
  consequence. An outcome whose every operation failed reads "The proposed action could not be
  carried out."
  - **Event text version 2** (plan 28; new turns). Every outcome does something. The kinds are
    `reform`, `investment`, `negotiation`, `protest`, `security` (deployments, policing, martial
    law) and `policy` (laws, decrees, appointments, statements); there is no `none`, and the
    older schema requires at least one effect that moves an axis. Consequence version 1 also permits
    an empty rating or `none`/`none`: a continuation need not move a statistic.
    Every outcome keeps its description. Without a map
    change a `security` or `policy` outcome moves stability by at most a step and can cost the
    economy a step but never grow it (growth is what a reform or investment pays for), and a
    negotiation moves only its author's stability, by at most a step, and reads "… No agreement
    was recorded." when it recorded no offer. So does any outcome without a successful operation
    whose description narrates a signing, treaty, pact, accord, agreement or alliance. A
    description the 160-character cap cut mid-sentence ends in "…". A `security`, `policy` or
    `negotiation` outcome without a successful operation is felt slightly: its `decline` and
    `growth` are recorded as `slight_decline` and `slight_growth`, half as heavy, so routine acts do
    not add up to a crisis. When the model rates nothing usable for an act
    without operations, its author gets the kind's small default and the event records
    `effect_source: "default"`: security costs economy and steadies stability, policy and talks
    steady stability, a protest costs it. A failed attempt gets no default, and a `security`,
    `policy` or `negotiation` outcome whose every operation failed keeps no rating of its own either:
    its consequence is the failure.
  - **Version 1** (older turns, replayed unchanged). Kinds are the first four or `none`. An outcome
    marked `none` with no successful operation reads "No validated change resulted from the
    proposed action."; a negotiation moves nothing and, with no offer recorded, reads
    "Preliminary diplomatic talks took place; no treaty offer or agreement was recorded."
- **Hostile pairs.** A war records who fights whom (`hostile_pairs`) and each party's own posture
  (`postures`). A co-belligerent is never an enemy, and a ceasefire, peace treaty or
  non-aggression pact between two parties pauses only their pair. No single party can end the
  whole war: `resolve_conflict` needs a settlement covering every remaining pair. Duplicate
  declarations do not add exposure. Older multi-party wars get a documented fallback
  (`pair_provenance`). The national status lists a war's enemies as `against` (only the pairs
  actually fighting) and paused ones as `ceasefire_with`; the Wars & treaties dialog shows who
  fights whom when a war has more than two parties or a partial ceasefire.

An operation that breaks a limit is dropped on its own. The rest of its event still happens, the event text stays the model's description, and the reasons are kept only in the event's `dropped_operations` (`{action, reason}`) for the audit, never in text that players or later prompts read. The event is saved with `partial: true` when some of its operations (or treaty terms) were dropped, or `failed: true` when every operation was. The Events dialog shows a "partly happened" or "did not happen" badge, and the journal line starts with "Partial · " or "Failed · ". Later prompts read what actually happened: related history, regional history, recent events and chronicle inputs append a short clause built from the dropped actions, such as "Invade Pakistan (In fact: no forces moved.)", or "(In fact: not every region changed hands.)" when some but not all of an event's seizures went through. The saved text, the journal and the narration keep the model's description. Operations that depend on a dropped one (such as handing land to a state that was not created) fail and are dropped too. Only a new state left without territory rolls back its whole event: it is saved as `blocked_by: "infeasible"` and kept out of the journal, narration and later history (related history, regional history and chronicle inputs all skip it).

Events also record the wars (`conflicts`) and treaties (`commitments`) their operations opened, escalated or ended.

**Significance.** The model does not label significance; C++ derives `major` on the event from what it did (an earlier `significance` field cost tokens on every event and was mostly called `major`):
- Events whose operations changed the map or a war are always major. That covers occupation, handover or restored control, wars opened, escalated or ended, new states, identity changes, treaties other than trade agreements and `other`, ended commitments, event sites, destroyed entities, and treaty breaches.
- An event with a `severe_decline` or `strong_growth` effect is a declared headline. Declared headlines are capped at three a month, keeping those with the weightiest effects.
- World events without any effect are dropped, so background developments exist only for what changed something. A world event whose description repeats one already in the history the model was shown this month is dropped too: a small model re-reports an ongoing crisis month after month, and the instruction alone does not stop it. Only background events are dropped this way; an intention's own outcome stays one per action.
- A regional initiative that files no demand and leaves the region's relations as they were carries no effects: the model tends to attach a national economic decline to a purely ceremonial event.
- Blocked and rolled-back events are never major.
- Events saved before `major` existed count as major only for a breach, a new flag or a severe effect.

**Events on the map.** The map derives event markers from saved history without changing state. Only major events appear. A major event is on the map for three months after the latest of its own month and the month of the last feasible event citing it as a cause, and for as long as a war it shaped is open. War tolls are routine (fronts already show in the Wars & treaties map mode), and of construction only finished cities are major. Markers sit at the event's regions and entities, or at the acting nation's most populous region.

Adjacency is the `region_neighbors` table (`tools/region_neighbors.py`): regions that touch, or lie within 0.05° across a narrow strait. The `regions.coastal` flag (`tools/region_coastal.py`) marks regions within 0.05° of the outline of the landmass, the union of the country polygons; large inland seas count, because their flotillas are real. The adjudication prompt receives:

- the regions targeted or named by actions;
- the bordering regions of nations an action addresses (up to 24 regions in all);
- regions of the acting nations' occupations, armies and fleets;
- the fronts of open conflicts that this month's actors fight (up to 36 regions in all).

Entities in those regions are listed too, except cities outside the regions actions name and the fronts.

A region comes with `within_reach_of` when an acting nation can project force there, listing those nations.

Invasion handling never tests the player's vocabulary. Each action's schema permits ordinary `build` only in regions its actor already controls, and `occupy` only in reachable foreign regions with `by` pinned to that actor. Event sites remain a separate build variant allowed anywhere listed. State-derived `offensive` facts list takeable regions and the monthly ceiling; `within_reach_of` supplies strength assessments. The compiler still enforces strength, treaties and the shared two-region ceiling. `move_entity` only offers active armies and fleets and is absent when there are none.

Validation checks collection/ID references, preconditions, duplicate writes, locations (including polygon holes), construction schedules, fixed geography, new actor land allocation and event associations. Destruction is represented by status, not deletion. Conflicts and commitments use actor IDs, text, and open/resolved status.

Validation runs on a copy. The complete state, accepted delta, indexed event relationships and turn records commit in one SQLite transaction. The following fail without advancing the month:

- unknown causes;
- invalid model output;
- tokenizer failure;
- unavailable inference;
- oversized mandatory context.

Narration runs after commit and cannot mutate state; failed narration leaves the factual summary.

## National status and war

`src/status.cpp` holds all rates as named constants. Everything here is deterministic: jitter of ±30% comes from a stable hash of campaign, month and region, so replay recomputes it exactly after applying recorded deltas.

**Political systems.** Every actor carries a `government`, one of twelve values seeded from
`data/source/governments_2026.json`: `parliamentary_republic`, `presidential_republic`,
`semi_presidential_republic`, `constitutional_monarchy`, `absolute_monarchy`, `theocracy`,
`one_party_state`, `dominant_party_state`, `military_junta`, `transitional_government`,
`dependent_territory` and `uninhabited_territory`, plus `government_since`, the year that order
began (0 when it is long-standing or not well established). Seeded non-state actors are
transitional, as is any state founded during a campaign. `src/status/regimes.cpp` gives each system
a `vent` (how much grievance legitimate channels discharge) and a `repression` (how much dissent is
suppressed rather than resolved). These modulate **unrest only**, never `driftStability`: the seeded
stability figures are real observations, and a regime-dependent baseline would pull all 241 nations
away from their January values in the first month. Only a `change_identity` with
`change_type: refounding`, or the engine's own collapse rule, changes a nation's system.

**Seeding.** `seedStatus` gives each actor its `country_stats` row and each region its `region_stats` share. New campaigns also receive the January 2026 wars, occupations and treaties. A save from before national status is seeded (without the 2026 world) at its next month, and that turn freezes the inputs in `reference.status_seed` for replay.

**Grounding.** `context()` adds a compact `status` to every decision, adjudication and narration input:

- population, GDP in local currency and USD, GDP per capita, stability label and economic trend;
- wars and treaties by name, kind, intensity and totals (blocs above six members show only their size).

Relevant regions show their population. The adjudication prompt asks the model to judge feasibility against economy size, population, stability and war burden.

**Names.** `realisticName` keeps a specific proposed name. A name made only of generic words (war, treaty, pact…), or one repeating the description, is replaced with "Portugal–Spain War", "Civil War in X", "X Insurgency", "Treaty of <largest city>" or "A–B Defence Pact". Names already used, even by ended entries, get "Second", "Third"…

**Each month**, after the accepted delta:

1. **Trend.** Region population and GDP grow by the owner's annual rates ÷ 12 (clamped to ±5% and ±10% a year).
2. **Model effects.** Economy: −1.0 / −0.3 / 0 / +0.2 / +0.5% GDP. Stability: −6 / −2 / 0 / +1 / +3. The recorded `slight_decline` and `slight_growth` (event text version 2) are −0.15% / −1 and +0.1% / +0.5. Per-actor totals are capped at ±1.5% and ±8, and a quarter of the stability effect shifts the baseline.
3. **Occupation.** Occupied regions lose 2% of output a month, down to 40% of their value when occupation began. Seeded occupations start at that floor.
4. **Fronts.** Before drawing fronts, conventional wars advance from frozen force inputs where decisive superiority has accumulated enough pressure. Only control changes, and the defender can retake land if the balance reverses. Each open conflict's affected regions, capped at 12, are:
   - regions whose control changed between its parties this month;
   - regions of the parties where a party's entity was destroyed;
   - seeded theatre regions still held by a party;
   - regions controlled by a party that border a region controlled by another party.

   Effective intensity is `none` under an open ceasefire or peace treaty between two parties, `high` when control changed or an entity was destroyed this month, `low` when there is no front, and otherwise the declared intensity. A conflict started less than 3 months ago stays at most `medium` until control changes or an entity is destroyed. `static_months` counts months without territorial gains; after six, the parties see a stalemate and are encouraged to negotiate. A static front does not automatically cool: casualties continue at the declared intensity until a party scales it down or a ceasefire takes effect.
5. **Toll**, per month by intensity low / medium / high, scaled by kind (war and civil war 1.0, border conflict 0.4, insurgency 0.3):

   | | low | medium | high |
   | --- | --- | --- | --- |
   | Military deaths as share of committed personnel per party | 0.05% | 0.4% | 1.5% |
   | Civilian deaths, share of each front region | 0.0003% | 0.0015% | 0.005% |
   | Displacement, share of each front region (plus a one-off 8% when a region changes hands) | 0.03% | 0.15% | 0.4% |
   | Front GDP loss | 0.1% | 0.4% | 1% |
   | GDP drag across the parties' land | 0.005% | 0.02% | 0.05% |

   - Committed personnel are the party's active armies in or adjacent to the conflict's affected regions plus its 20%/30% share of uncommitted personnel divided by distinct enemies. Landless actors use their active inventory and no territory-based formations. Inputs are frozen before this month's operations; deterministic jitter is 0.7–1.3.
   - Deaths reduce active personnel first, then reserves. Allocated armies and uncommitted personnel share active losses proportionally. Armies falling below 2,000 are destroyed; their remainder leaves active strength as dispersed/captured personnel, without counting it again as deaths. A journal entry names the destroyed formation.
   - With opposing aircraft, high/medium intensity removes floor(30% × aircraft × 1%/0.3%); opposing ships similarly removes floor(30% × ships × 0.5%/0.15%). Low intensity has no equipment attrition. High-intensity war/civil-war participants move floor(2% × starting reserves) to active once per month, after losses.
   - Military deaths also reduce recruiting-region population where available, with the existing 1%-per-region allocation bound; recorded military deaths are bounded by military inventory rather than that demographic approximation.
   - The one-off 8% on seizure applies to the people the fighting reaches: at most 20 residents per soldier the seizing and losing sides committed to the region (the same committed-personnel measure, frozen at the start of the month). A small region is emptied of its full 8%; a province of 117 million taken by India from Pakistan loses about 1% (roughly 12.5 million people exposed). Turns from before military inventories keep the full 8%.
   - 35% of displaced people cross into bordering non-party countries; the rest go to the owner's unaffected regions. With nowhere safe, they go anywhere else in the country.
   - Half of per-capita output moves with them.
   - Once a region is calm and back under its owner, 3% of its displaced return each month. Under the realism lifecycle they return only from their recorded hosts (see below).
   - Population is conserved except for deaths.
6. **Stability** moves 8% of the way toward a target each month, by at most 10 points. The target is the baseline, minus 3 / 8 / 15 for the most intense war, minus 25 × the occupied share of population, minus 5 when GDP fell over 1% that month. Deaths above 0.01% of population cost another point.
7. **Unrest**, per region, on the same 0–100 scale, charged against whoever controls it. It drifts toward what the situation justifies, `100 × stress × grievance / 2`, closing `16% × (1 − 0.8 × vent)` of the gap when rising and `20% × (1 + vent)` when falling, at most 10 points a month.
   - `stress` is 0 unless the controlling nation has fallen at least 8 points below `seeded_stability`, the January figure frozen at seeding; it then rises to 1 at a 22-point fall. That ceiling is set by what the month step can actually deliver: stability reverts toward `baseline − war` by 8% a month and so never arrives, which over two years is a 6.9-point fall for a medium-intensity war (never reaching the gate at all), 13.0 for a high-intensity one, and 21.6 for a high-intensity war with a quarter of the population occupied. A higher ceiling puts the top of the scale out of reach and the chain never runs. `baseline_stability` cannot serve as that reference because it ratchets with every model effect.
   - `grievance` is 0 unless the region has one the campaign produced, and then sums: 1.6 for occupation by another power (plus 0.8 where over 2% of its people are displaced), 1.0 for `strained` relations with the capital, 0.5 per pending demand reaching full weight after six months, and −1.5 for `cooperative` relations. **Being a front in a war its own controller is fighting is deliberately not a grievance**: invaded populations rally and only occupied ones resist, and war already costs national stability at step 6.
   - The gate is what keeps the seeded world still. `stability` is the World Bank's *political stability and absence of violence* percentile rank — North Korea reads 71.4, Yemen 18.9 — so it measures repression as much as fragility. An absolute threshold would set the Sahel, Yemen and Somalia alight in the first year of every campaign. Two idle years of the real January 2026 world produce no revolt, no breakaway and no unrest event anywhere, and peak at 23/100 in RSF-held West Kordofan — a region under occupation in a live high-intensity war, still below the 35 anything is reported at. Yemen, Mali, Syria, Somalia and Afghanistan, chronically low but steady, stay at exactly zero.
   - Regions report at 35 (`restive`), 55 (`unrest`), 70 (`in revolt`) and 80 (`in open rebellion`). A band change is a `domestic` journal event, and unrest at or above 35 raises a region's stake for the month's regional initiative slot.
8. **Rebellion, secession and collapse**, all bounded to one of each a month campaign-wide.
   - A region held for 4 consecutive months at or above `70 + 25 × repression`, and not already inside an open conflict, raises rebels: a new actor with a flag, a transitional government and an army that **defects out of the state's**, 25% of the personnel committed there scaled by unrest and by `1 + repression`, plus 0.2% of the population, between 1,500 and 150,000. It opens an `insurgency`, or a `civil_war` where the defectors are at least a quarter of the army, named by `realisticName` as usual. The state loses 6 stability.
   - Rebel ground moves on its own rule, not on a force ratio: `advanceConflict` still refuses to advance insurgency and civil-war fronts, because measured conventionally an army always walks into its own insurgency. A rebellion takes one region a month that borders what it holds, is at 80 unrest or more, and is garrisoned by fewer than 0.004 troops per resident. It runs in step 4c with the conventional advances, so the toll, displacement and front drawing treat it identically. Control only, never ownership.
   - Ground held for 6 consecutive months, with no ceasefire or peace treaty, becomes a state: ownership transfers, the movement takes a state name and a new identity revision, and the parent loses 8 stability.
   - A nation below 15 stability and party to an open civil war for 6 consecutive months becomes a `transitional_government`.

Status version 4 adds all of this. Turns recorded at 3 or below gain no unrest at all, so existing campaigns replay unchanged.

### Force allocation and attrition (realism lifecycle)

Plan 26 F makes every use of military strength draw on one allocation per month.

- **Engagements.** An actor's engagements are the enemies it is fighting and can meet, plus
  neighbours disputing ground with it. Two enemies meet when land they hold borders, an active army
  stands in or next to the other's land, a fleet's routes reach its coast, or a seeded theatre has
  them fighting; a war declared on a nation nobody can reach claims none of the pool. A war declared this month counts: the compiler judges seizures with
  this month's wars and treaties in force, and the month step reads its forces from the start of
  the month but its wars from after the orders. The national pool (20%, or 30% when mobilised, of
  uncommitted personnel), the air arm and the fleet are divided across every engagement, attacking
  as well as defending. A second front therefore costs offensive strength: in the access fixture, a
  war opened against a neutral earlier in the month halves Aland's odds against Bland, and a
  seizure it could otherwise make fails.
- **Formations.** An army in or next to the fight splits evenly among the distinct enemies it
  faces, so it cannot fight two battles at full strength. An army facing no one garrisons where it
  stands. `force::engaged` is the one calculation behind assaults, advances, garrison checks,
  seizure displacement and casualties. Summed over an actor's enemies, it never exceeds its active
  strength.
- **Personnel losses** fall on what was engaged against that enemy, in proportion: the armies next
  to the front and the pool's share. An army at home, or one still assembling, loses nobody. Deaths
  reduce the population once, and a destroyed army's remaining troops leave the inventory once, as
  dispersed or captured.
- **Aircraft** are exposed when the actor holds supplied ground on or next to the front: 30% of the
  air arm, divided by engagements. Enemy aircraft cost them 1% (high) or 0.3% (medium) a month;
  ground fire alone costs half that. **Ships** are exposed in fleets within sailing range of a
  coastal front region, and the uncommitted fleet only off its own front-line coast. They are lost
  at 0.5% or 0.15% a month when the enemy has ships or aircraft engaged there. A fleet out of reach
  loses nothing, whatever the enemy owns. Low intensity costs no equipment.
- **Fractions carry forward.** Each front's losses are summed per actor before rounding, and what
  is below one soldier, aircraft or ship is kept in `status.attrition` for next month. Under the old
  rounding, 30 aircraft at high intensity never lost one; now they lose about one a year.

On the real world, 36 idle months move no front differently from Phase E. The Russo-Ukrainian War
still kills about 196,000 a year. Russia now loses about 33 aircraft and one ship a year, its Black
Sea coast being on the front and within reach of Ukrainian aircraft; Ukraine's 97 aircraft fall to 90.

### Displacement by origin and host (realism lifecycle)

Under the realism lifecycle (Plan 26 G), people displaced from a region are recorded where they
shelter. Each origin territory carries an engine-owned `displacement` ledger, `hosts` (host region
→ people), and its `displaced` count is derived from it at the end of every month.

- **Flight.** Every movement in the month step goes through one helper, which moves population,
  output and ledger rows together. A front region's residents become displaced from it. People it
  was already hosting move on still displaced from their own origin, so a second flight is not a
  second displaced person. Anyone arriving at their own origin has returned.
- **Deaths and decline.** Deaths in a region, and any fall in its population from negative growth,
  take hosted people in proportion to their share, so no ledger row outnumbers the living. Births
  count as the host's own residents.
- **Return.** Once an origin is calm, held by its owner and not held by force under a control
  record, 3% of its outstanding people go home each month. They come from the hosts that recorded
  them, foreign hosts included, and only what actually moved is taken off the ledger. An origin
  that is occupied or annexed by force keeps its balance unchanged.
- **Borders.** Transfers of territory do not change the ledger: a host that changes hands still
  hosts the same people.
- **Older campaigns.** A count carried from before the ledger existed is kept as
  `displacement.unknown`. It is never given a host, never returned automatically and never
  erased. The region tooltip lists where people shelter and how many are of unknown whereabouts.
- **Cumulative totals.** A conflict's `displaced` still counts every movement it caused;
  outstanding displacement is the regions' ledgers.

Measured on a deterministic fixture (`tests/status/displacement.cpp`): six months of high-intensity
war displace about 24,000 people from a border province of a million, 8,300 of them abroad. A year
of peace brings 7,300 home, each one out of the host that recorded them. On the real January 2026
world, 36 idle months leave the fronts and forces exactly as in Phase F. Ukraine's outstanding
displacement grows to 2.2 million, 55,000 of them recorded abroad; nobody returns to the occupied
east. Flight across a border still reaches only bordering non-party regions, so Ukraine's recorded
refugees abroad are far fewer than the real figure.

### Implementation, trade and recovery (realism lifecycle)

Under the realism lifecycle (Plan 26 H), what a nation builds, reforms and replaces competes for one
monthly **implementation allowance**. It is a gameplay estimate, not a treasury: GDP stays output,
and nothing is borrowed. The coefficients live in `src/status/status_internal.h`.

- **Allowance.** 8% of effective annual output, less half the recorded military spending (the
  running cost of the forces a nation already has), but never below a quarter of that 8%, divided
  by 12. On the seeded world: US$164 billion a month for the United States, US$2.1 billion for
  Portugal, US$0.30 billion for Sudan and Ukraine (Ukraine at the floor). The median actor has
  US$163 million, and 142 of 242 have less than one port-month. Most of those are microstates,
  movements and the poorest countries. Every coefficient scales these figures in proportion.
- **Priority.** Mobilising reservists in a high-intensity war comes first, at US$5,000 each, and
  only what is paid for reports. Then work already under way, oldest first; then this month's new
  work in recorded order; then replacement orders. Whatever is left pays for as much of the month
  as it covers. A project behind by a whole month finishes a month later (`arrears`, `unfunded`),
  and the first shortfall is reported as `construction_stalled`. Paused work costs nothing.
- **Construction costs** per month, at the builder's price level (the force quality factor,
  0.6–1.4): army US$50 million, fleet 100 million, base 150 million, port 300 million, city
  500 million. Construction slots still apply. Projects under way when a campaign moves to these
  rules keep their progress and pay only from then on.
- **Legacy efforts (project version 0).** A `reform` or `investment` outcome (`outcome_kind`) starts an effort, unless the model
  rates it a setback for its author (`status.efforts`), priced by C++, not the model. An investment costs 0.6% of annual
  output over 12 months and raises output 1% once, on paid completion. A reform costs 0.3% over 18
  months and raises the stability baseline by 3 points, divided by one plus the number of reforms
  completed in the previous five years, with 2 points of immediate stability on top. Starting one
  may bring a month's confidence (its stability effect); its economic effect is dropped. Asking
  again while it runs continues it and earns nothing. Positive model stability effects no longer
  ratchet the baseline; setbacks still do.
- **Trade.** Signing a trade agreement or union no longer forces growth, and ending one no longer
  forces a decline. Instead, each partner gained since the campaign's first realism month raises
  growth by 0.25% a year, times its share of the pair's combined output. Each partner lost lowers
  it by the same rule, and the change in each direction is capped at 3 partner-weights. A partner
  counts once however many treaties link them, so a renamed copy adds nothing. Expiry, withdrawal
  and breach end the same future benefit and keep growth already earned. The seeded treaties are
  the baseline (`status.trade_reference`), because 2026 growth already includes them.
- **Replacement.** The inventory at the start of the first realism month, before its losses, is
  the establishment
  (`military.establishment`). Losses below it open one paid order of each kind (`status.procurement`),
  at most 5% of the establishment each: recruits (US$30,000 each, 6 months), aircraft (US$60
  million, 18 months) and warships (US$600 million, 36 months). Aircraft and warships also take a
  construction slot, and none are ordered while construction fills every slot. Deliveries arrive only
  on paid completion. Recruits change classification, not population. Assembling a formation
  still only allocates existing forces.
- **Demobilisation.** After 12 months without fighting anywhere (`military.peace_months`), excess
  active personnel return to the reserve at 2% of the establishment a month, never below what
  formations hold.

Measured on deterministic fixtures (`tests/status/economy.cpp`):
- **Rich and poor:** a rich nation pays for two ports without delay. A poor one finishes its older
  port a little late and its newer one much later, but finishes both.
- **Efforts:** an investment raises output exactly 1% once, after 12 paid months. A second reform
  within five years is worth half the first.
- **Trade:** a large partner counts for about 500 times a microstate. Duplicate treaties add
  nothing. Expiry and withdrawal agree.
- **Replacement:** recruits arrive only after 6 paid months and stop at the establishment.
- **Demobilisation:** excess troops return to the reserve after a year of peace, never below the
  formations and never during a war.

On the real world, 36 idle months take and lose exactly the same regions as before. Sudan, the RSF
and Myanmar now replace their losses in batches, so their armies hold roughly steady instead of
shrinking by about 10%. Russia orders replacement aircraft and a warship; Ukraine orders aircraft.
Mobilisation stays affordable for every belligerent.

### Pacing: exhaustion, capture effort and the whole front (realism lifecycle)

Under the realism lifecycle (Plan 26 I), pacing follows from the accounting above rather than from
calendar cut-offs.

- **Exhaustion** replaces the old rule that cut attacking strength to 60% on a war's first
  anniversary. Each nation's `military.exhaustion` (0–1) lowers attacking strength by up to half.
  Each month it rises by 0.005 / 0.012 / 0.02 for the intensity fought at, by 5 × the share of
  active personnel lost, and by 0.01 while any army is cut off from home. It recovers by a share of
  itself: 3% a month while fighting, 6% with replacement fully funded, 10% in peace (a half-life of
  about seven months). With Russia's losses it settles near 0.8, which is exactly the old
  multiplier. A new war reaches it gradually instead of in one step. Nations already a year into a
  war when a campaign moves to these rules start at 0.8.
- **One seizure threshold.** Orders and momentum meet the same bar: an attack/defence ratio of
  1.5 × the region's capture effort. One region a month at that ratio, two at twice it, and at most
  two a month in all. An order no longer takes ground at the old 0.75:1. An ordered seizure also
  spends one region's worth of the progress momentum had banked against that enemy.
- **Capture effort** multiplies three proxies, none of which is terrain:
  - **Extent:** `sqrt(extent / 150 km)`, clamped to 0.6–2.0. Extent is the median distance from the
    region's interior to its neighbours'. The map has no areas; the seeded median is 136 km,
    Donetsk 187, Texas 691.
  - **Population:** `(population / 1 million)^0.1`, clamped to 0.7–1.4.
  - **Supply distance:** up to +50% for targets more than 1,500 km from the attacker's heartland.

  Donetsk comes to about 1.3 for Russia; a small city district to about 0.7.
- **The whole front.** A front is simulated over all its regions. Up to 12 regions each bear the
  full toll as before. A longer front shares that total across every region by exposure (one plus
  its enemy-held neighbours, plus two where ground changed hands), so no region is dropped
  because of its ID. A conflict still reports at most 12 regions, the most exposed first.

Measured on deterministic fixtures (`tests/status/pacing.cpp`, a 20-pair border):
- **Smooth wear:** a new high-intensity war reaches exhaustion 0.53 in two years. No month
  changes attacking strength by more than 6%. Seven months of peace halve it.
- **Parity:** three years at 1:1 move no ground and cost 173,000 lives.
- **Superiority:** at 5:1 the attacker takes 8 regions in year one, 6 in year two and 19 in three
  years, never more than two a month.
- **Renaming:** renaming every region changes the toll by less than 15%, and every front region
  bears a share.
- **Effort:** a small district needs 0.72 and a vast region 2.55.

On the real world, 36 idle months take and lose the same regions, with the same Russo-Ukrainian
toll and displacement: its front has at most 12 regions, so every region still counts fully.
Russia's and Ukraine's exhaustion stay between 0.80 and 0.84.

### Conquest and resistance (realism lifecycle)

Under the realism lifecycle (Plan 26 E), a region held by force carries an engine-owned `control`
record: `claimant` (who it was taken from), `holder`, `since` (the month it was taken), `cause` and
`integration` (0–100). One rule, `recordControl` in `src/status/control.cpp`, keeps it in step with
whoever holds the region. It runs at the start of each turn and again in the month step, after the
fronts, so every seizure is covered: an order, a front advance, a treaty, a rebellion. Replay
reproduces it exactly. A delta still cannot write it.

- **What counts.** A controller other than the owner opens a record unless the owner lent the region
  in an open treaty (`occupy` term), or the holder is a movement from that land (an unfounded rebel,
  or a side in a civil war or insurgency with the owner). The RSF in Darfur and M23 in North Kivu are
  therefore not foreign occupiers; Russia in Crimea, Donetsk and Luhansk is. Occupations present
  before the history began are recorded with `cause: "recorded"` and no `since`, never given an age.
- **Annexation keeps it.** Annexing a region changes its owner, but the record stays. So do the
  output drain, the resistance, and the claimant's stability penalty for the population it lost.
  Only three things end a record: the claimant regaining the region, a local movement taking it,
  or full integration of an annexed region. Losing the region to a third party gives the new
  holder a fresh record, and the claim stays with the original claimant.
- **Integration** needs annexation, peace between holder and claimant, no fighting in the region
  and unrest short of revolt. It takes 10 years (0.83 a month); `cooperative` relations double the
  rate and `strained` relations halt it. Fighting in the region costs 10 points. At 100 the record
  is removed and an `integrated` domestic event is reported.
- **Usable output.** Strength (`quality`) and construction capacity use `effectiveOutput`: an
  undisputed region counts in full, a lost one counts for nobody, and a conquest counts at
  30% + 70% × integration for its holder alone. Territorial totals in the national status stay
  descriptive and unchanged.
- **Resistance** is its own unrest target, independent of the occupier's decline:
  `60 × (1 − integration) × (1 + 0.5 × occupier stress)`, then +10 where over 2% of the people are
  displaced, +10 for `strained` relations, −15 for `cooperative` ones, and × 0.8 where the occupier
  garrisons at least 0.004 troops per resident. The region's target is the highest of this, the
  domestic path above (which no longer counts foreign occupation) and **ignored demands**: 20 per
  fully aged pending demand, capped at 40. That cap is enough to report a region as restive but
  never enough to arm it. Only a nation's real decline takes it further.
- **Status version 5** (plan 28; new turns, older ones replay unchanged):
  - **One climb for every occupier.** When the occupation target is the one being chased, unrest
    rises by 30% of the gap a month whoever occupies. The occupier's vent describes how a regime
    channels its own people's grievance, not a foreign population's. Scaling by it left a region
    taken by a presidential republic calm for half a year; now an unstressed occupation (target
    56–60) is restive in its third or fourth month. The fall is unchanged.
  - **Old occupations start where they are.** An occupation recorded when the campaign began
    (`cause: "recorded"`) starts at its target once, marked `seeded` on the control record, instead
    of climbing from calm as if it began in January 2026.
  - **Land held by armed movements.** A region held against its owner by an actor that owns no land
    at all (the RSF in Darfur, the M23 in North Kivu) gets the same resistance target, though the
    control history counts it as local rule. A rising the game raised is excluded: it takes ground
    only where the population is already with it. No new rising forms against a movement, because
    its own civil war already represents the region.
  - **Map.** In the stability & unrest mode an occupied region still calm is filled "Occupied",
    never in its occupier's stability colour, and its tooltip names who holds it. The regional
    politics panel lists the land a nation occupies with its unrest, however low.
- **Rising.** The duplicate guard now covers only the relevant rising. A region is skipped only when
  a civil war or insurgency its controller is fighting already reaches it, or when rebels against
  that controller hold neighbouring ground. Another war passing through no longer suppresses a
  revolt. A rising against an occupier is raised from local militia, not by defection from the
  occupier's army. It takes the claimant's economic character, records `resisting_for`, is always
  an `insurgency`, and never declares a state of its own.
- **Cession and third parties.** An owner may sign away its claim to land a third party holds, but
  the third party's forces stay. An `occupy` term cannot evict a controller that is not a party.

Measured on deterministic fixtures (`tests/status/conquest.cpp`):
- **Stable occupier:** four months of occupation bring unrest to 39 whether the occupier is a
  republic or a junta (status version 5). Two years bring it to 60, or 48 with a strong garrison
  (46 and 37 under status version 4). Under the legacy rules the same occupation stays at 0.
- **Integration:** a cooperative annexed province integrates in 60 months, and its unrest falls
  from 60 to 16 by month 31.
- **War theatre:** an occupied, devastated and strained war theatre under a stable republic rises
  after 12 months, once (46 under status version 4).
- **Armed movement:** land a movement holds through a civil war with its owner settles restive,
  short of revolt, and raises no rising against the movement.
- **Ignored demands:** five years of ignored demands in a steady junta plateau at 40.
- **Invaded, not occupied:** a defended front stays at 0.

On the real January 2026 world:
- **Occupied Ukraine:** under status version 4, Crimea, Donetsk and Luhansk became restive in
  month 10 and reached "unrest" (57 after two years, 68 after five). Under version 5 they start at
  their target and stand at 63.5 after two idle years. Russia's armed line is 84, so they never
  reach it.
- **Land held by armed movements:** the RSF's Darfur and West Kordofan and the M23's North Kivu,
  calm under version 4, are in "unrest" (at least 55.5) after two idle years, short of revolt.
- **Everything else:** unchanged. No actor, conflict, revolt or fallen government appears in 60
  idle months, and the front trajectories are identical to the build before this phase.

A year of the inventory-seeded Russo-Ukrainian War at high intensity produces roughly 195,000 deaths in the deterministic reference fixture (the test accepts 80,000–300,000); civilian displacement retains the earlier model. That is the order of magnitude of 2023–25 estimates. Under the legacy lifecycle, displacement within the country is simplified: people displaced from a region are tracked there, but refugees who crossed a border are not sent home. The realism lifecycle's ledger, above, records every host.

Status version 3 records autonomous advances and stalemates. Replay passes each turn's recorded version, so existing version-2 campaigns keep their original monthly consequences.

Limitations: conflicts have parties, not sides. Autonomous advances skip pairs joined by an open alliance, defence pact, union, non-aggression pact, ceasefire or peace treaty; other multi-party relationships remain approximate. Military inventories include explicitly labelled estimates and mixed source years. Regional splits are estimates.

## Historical preparation and context

“Prepare campaign references” can explicitly refresh Wikipedia references for the player, initial neighbors and involved conflict/commitment actors. The result reports coverage and failures. Individual actor refresh remains available. Automatic background preparation also prioritizes upcoming independent actors at creation/resume and after commits. Monthly inference never waits for reference downloads; submission freezes available material. Fictional actors have no real Wikipedia identity.

AI nation decisions, adjudication and narration share campaign context. SQLite indexes events by actor, region, entity and cause; a recursive query retrieves relevant earlier events and their causes. Recent events and older related events accompany current facts and commitments. Real references are marked untrusted and subordinate to fictional state; missing references remain unavailable. Geometry and the full journal are never model inputs.

Every inference reads the context size from `/props` (at least 8,192 tokens, cached for 60 seconds). A request whose byte estimate fits well inside it is sent at once; otherwise the server chat template is applied and its tokenizer counts the tokens. The request reserves 768 output tokens, or for adjudication 768 plus 192 per outcome (bounded to 1,536–4,096) plus a reasoning budget of 256 plus 128 per consequential action (384–768, zero when no action is consequential), and 256 safety tokens inside the reported context. Adjudication reasons only when the context holds at least 16,384 tokens. Optional reference excerpts and then lower-ranked historical events are removed until the request fits. Current facts and commitments are never clipped. Exact selected messages, schema, token counts, template, model/build metadata, raw response and latency are saved for diagnosis. Turn snapshots retain original cached source revisions. Replay uses recorded deltas and requires no inference.

## API additions

- `GET /api/v1/map`: `geometry_url` (`/regions.topojson?v=<content hash>`, served gzipped with `Cache-Control: immutable`), campaign `territories`, and `turn`; join the topology's `regions` geometries by permanent ID. The browser caches geometry and refreshes campaign properties. Map moves are debounced (300 ms) and refetch entities only outside the last padded viewport or in a new month; event markers refetch only in a new month.
- `GET /api/v1/campaign` carries an `ETag` (hash of the response) and answers `If-None-Match` with 304.
- `GET /api/v1/entities?west=...&east=...&south=...&north=...&zoom=...`: at most 200 entities; cities appear at zoom 4+. `?id=...` retrieves an individual entity regardless of viewport.
- `GET /api/v1/prepare`: reference coverage; `POST` with `{}` starts explicit preparation and returns a pollable job.
- `GET /api/v1/inference?job=...&since=...`: exact recorded model requests/results for a job, tagged `kind` (`decide`/`adjudicate`/`narrate`), `actor` and the parsed `answer`. Each request/adjudication/decision flushes as soon as it lands, before the month is committed, so a running turn can be polled for a live log; `since` (the highest `id` already seen) returns only what is new.
- Replay exports include accepted deltas/context snapshots in each turn's `reference`, and only the geometry needed to validate changed entities.

## Measured model results

**Plan 24 follow-up (2026-09-22).** Qwen3.5-4B, one 32,768-token slot. The three invasion-wording
cases use synthetic Portuguese superiority (800,000 active, 500 aircraft) to isolate comprehension;
`weak_raid` and `overreach` keep the real force inventories and both leave Spain unoccupied in 3/3
runs. Final invasion/finish/capital adjudications took 17.1–23.8 seconds with 6,954–6,988-byte schemas.
Capital-push initially failed 0/3 without reasoning; extra prompt instruction alone also failed. Enabling
the existing bounded reasoning pass when the acting nation has state-derived offensive options produced
3/3. A separate secession check passed, preserving creation and land transfer in one response.
These are map-outcome checks, not a guarantee of good prose: some descriptions contradict their accepted
operations or contain truncated/mixed-language text. Reports are local ignored `logs/plan24-final-*`
and `logs/plan24-v3-capital_push-*`; they are not shipped as reference data.


Model: Qwen3.5-4B Q4_K_M via `llama serve` (32,768-token context, `--cache-ram 1024 --ctx-checkpoints 8`) on a 16 GB Apple silicon MacBook Pro. Adjudication used thinking with a bounded budget; AI actions and narration did not. Measured 2026-09-17.

`build/world_probe out.json` runs nine isolated free-text scenarios on a four-region map. Each checks the outcome, not just validity. Prompts are about 1,460–1,590 tokens and each adjudication takes 3–26 s (measured 2026-09-18). Sampling (temperature 0.6) makes results vary between runs. Over the final three runs:

| Scenario (action) | Intended outcome | Runs matching |
| --- | --- | --- |
| construction: "Build a port in West." | port under construction in West | 3/3 |
| conflict (Spain): "Send the army across the border to occupy West." | West controlled by Spain, still owned by Portugal | 3/3 |
| secession: "Grant West independence as a new state named West Republic." | new state owns West | 2/3 (once created the state without land; the game blocked it) |
| long_history: "Construct a military base in West." under a treaty forbidding it | no base | 3/3 |
| reform: "Launch a national education reform." | recorded event, no map change | 3/3 |
| overreach: "Conquer all of Spain this month." | Spain's interior untouched, nothing annexed | 3/3 |
| capital: "Build a new city in North to serve as the future capital." | city under construction in North | 3/3 (2/3 on 2026-09-17, once narrated only) |
| invasion: "Invade Spain." (plan 24 synthetic superior force) | East controlled by Portugal, still owned by Spain; interior untouched | 3/3 |
| finish_invasion: "Finish the invasion of Spain to avoid the war stalling." | Same bounded occupation | 3/3 |
| capital_push: "Drive straight through to Spain's capital to topple the regime." | Reachable East occupied; distant interior untouched | 3/3 |
| treaty_consent (Spain): "Sign a defence pact with Portugal." | no pact; one proposal awaiting Portugal (the player), whose own actions do not mention Spain | not re-run since authority checks (earlier expectation, a pact, matched 3/3) |

The first design iterations failed in informative ways:

- **Unbounded reasoning.** Thinking ran over 4,000 tokens (2+ minutes) for one construction order.
- **Unaligned events.** With a free events array, the model skipped this month's actions and re-adjudicated earlier months. The fix is one outcome per action, each opening with the action's text as a schema `const`.
- **World events with operations.** Background developments started construction and treaties for the player's nation.
- **Commitment blocks.** A cited commitment suppressed an outcome whose operations were valid. Now a breach is recorded instead.
- **Temperature 0.** AI nations repeated identical actions every month.

End-to-end campaign (disposable save, Portugal on real ADM1 regions; one AI nation, Spain, per month). Each turn took 109–122 s, including decisions, adjudication (1,500–3,500 prompt tokens) and narration:

1. **Turn 1.** "Build a port in Viana do Castelo", "Launch a national education reform", "Conquer all of Spain this month":
   - the port started construction (due in 6 months);
   - the reform was recorded without map changes;
   - Historical pre-inventory result: the conquest became a declaration of war, occupation of bordering Galicia and an expeditionary army under construction. Plan 19 supersedes this: Portugal is now too weak to occupy Galicia.
2. **Turn 2.** "Occupy Comunitat Valenciana", "Raise a new army in Guarda":
   - the model judged the distant occupation impossible until the army was ready;
   - the Guarda army started construction.
   - Spain's attempt to build in occupied Galicia was blocked by the game.
3. **Turn 3.** "Grant Guarda independence as a new state named Free Lusitania":
   - Free Lusitania was created and owns Guarda;
   - the expeditionary army completed on schedule;
   - Spain accepted a ceasefire, resolving the war.

Turn output diet (measured 2026-09-18, `tools/living_world_probe.py --months 2`, Portugal with no player orders, so each month adjudicates four intentions: two independent national, one regional and one reactive neighbor). Before: 159 s and 212 s to commit, with adjudication generating about 2,650 tokens (950 reasoning) for a 13-intention month. After reactive nations dropped to one or two actions, `significance` left the schema, descriptions shrank to 160 characters and reasoning became conditional: 94.6 s and 87.5 s to commit; adjudication generated 1,218 and 1,294 tokens including reasoning (both months reasoned, since "trade accord" and "data sovereignty" match the consequential stems), prompts of 4,327 and 4,581 tokens, 37–40 s of generation; each decision generated 48–215 tokens. Both months replayed identically.

Earlier runs showed the model's typical mistakes. Historically it sometimes chose `build` in enemy land for an invasion. The per-actor schema now excludes that operation; a mismatched reason or a legal but unhelpful outcome remains possible. In those cases the game blocks the event, and the journal says why.

## Validation boundaries
 Structural validity is separate from whether a model outcome sensibly follows a player's action. The deterministic force model limits ordered attacks and advances overwhelmingly superior conventional fronts autonomously; the model still judges the outcome of an individual order within those limits. A small model may misunderstand an action or narrate background events of little consequence. Campaign history retrieval retains 20 relevant historical events plus current commitments; it is not an unlimited-history prompt.

See [living political world](../README.md#a-living-political-world) and [architecture](ARCHITECTURE.md#independent-politics-and-identity-persistence) for regional authorization, identity operations, deterministic flags and compatibility. The model timing results above predate the three additional independent monthly actions.

### Military definitions and compatibility

Active personnel are active-duty people, excluding gendarmeries, state police and other paramilitary forces; reserves are trained reserve personnel. The World Bank series counts active plus paramilitary and is no longer used for `active`. Warships are frigate-size or larger principal surface combatants, carriers and amphibious assault ships plus submarines, excluding corvettes, patrol boats, mine warfare, landing craft and auxiliaries. Combat aircraft are fixed-wing fighters, attack aircraft and bombers, excluding trainers, helicopters, transports and drones. Source discrepancies and estimated scenario overrides are documented in [DATA.md](DATA.md).

Strength labels use quality × (active + aircraft × 200 + ships × 1,500): below 20,000 negligible; below 150,000 minor; below 750,000 regional power; below 2 million major power; otherwise superpower. The seed labels US/CN superpowers, Russia/India/South Korea/North Korea major powers and Brazil a regional power.

Legacy formation defaults are 10,000 personnel / 3 ships. Military backfill preserves current GDP, population, wars and inventories, and proportionally reduces overallocated legacy formations to the available national inventory. Old destroyed units are treated as already accounted for. New actors receive estimated inventories rather than cloning a parent's forces. Frozen military seed references and the historical no-inventory casualty branch preserve replay.

The pinned plan-24 fixture gives India roughly 198,000 against 209,000 (**limited**) and the long-war Russian attack roughly 380,000 against 484,000 (one region if ordered, no autonomous advance). Adding field armies also decreases the uncommitted pool.


### Grounded consequences and domestic projects (Plan 31)

Turn references and `engine` independently record `consequence_version: 1` and
`project_version: 1`. Missing versions select the old rules. The shared lifecycle selector
migrates on the working copy, so failure/cancellation publishes neither migration nor accounting.
Historical state **and event** replay remains unchanged.

For unstructured outcomes, the compiler records consequence identity from the author, bounded
kind, relevant targets and normalized description. Normalization handles casing, spacing and
punctuation, not arbitrary semantic equivalence. Each affected actor/kind admits at most one
unstructured rated consequence per month, of either sign. Across active months its multipliers
are 1, 1/2, 1/4, then zero. Exact repeats get zero immediately. Repeats refresh the window, so
continuous restatements cannot regenerate confidence. After 24 months with no entry refresh,
that entry expires. Each actor retains at most 64 ledger entries, including the protected kind
buckets; oldest exact identities are evicted first, with lexical tie-breaking. No journal scan,
embedding or additional model call is used. This intentionally groups different unstructured
stories conservatively; it does not claim to detect all paraphrases. Effects and event records
retain the applied weight and reason (duplicate, monthly envelope, or diminished consequence).

Accepted operation effects use their validated change identity instead of that prose envelope;
war casualties, procurement and paid project progress keep their existing deterministic rules.
Failed operations cannot gain independent rewards by being relabelled. Decorative event sites
can accompany a domestic project but provide no immediate national-stat reward.

A domestic project has a C++ ID derived from actor, category, target and supported purpose:
`administration`/`public_services` for reforms, `infrastructure`/`production` for investments.
The objective is a short display description, not an extra programmable benefit. Projects with
that same identity continue regardless of title; different targets or supported purposes progress
separately. This deliberately treats paraphrases and different descriptions of the same supported
intervention as one project. Targets are the actor itself or a region it owns and controls.

Costs retain the old rates: investment costs 0.6% of target annual output over 12 paid months;
reform costs 0.3% over 18. They compete with construction and replacement under the existing
priority order. Regional work pauses when ownership/control is lost. Investment completion raises
the target's output by 1%; reform completion raises national baseline stability using the existing
diminishing reform rule, with regional benefits scaled by population share. Those are the only
supported benefits; completing funded work does not establish arbitrary claims in its objective.
Starting or continuing a project pays no immediate confidence bonus.

Cancellation stops future funding and retains paid progress/spending. Explicit continuation by ID
resumes it; a renamed start does not un-cancel it. Completed projects cannot restart or pay twice.
All records, including cancelled and completed work, count toward a 32-project-per-actor capacity;
records are never evicted to reopen rewards. At capacity, new projects are reported as unrecorded.
The inspector, journal and model context show the same authoritative progress and delays.

On upgrade, each category-keyed effort becomes a deterministic legacy project. Its paid work,
remaining paid months, monthly price and old national completion benefit are preserved. Its label
explicitly says the objective was not recorded; no objective is inferred from historical prose.

### Fulfillable regional demands (Plan 32)

Turn references and `engine` record `demand_version: 1`, which requires project version 1.
Missing versions select the earlier rules, and historical state and event replay is unchanged.

**Agreeing to a demand is not delivering it.** A regional initiative names its demand's kind:
`political`, `regional_project` (with a supported purpose) or `independence`. C++ derives
bounded, immutable terms from that kind and the region. A funded request without a supported
purpose becomes a political one. Regions only propose; they never execute national powers.
Answer windows are unchanged: three months in the player's regions and six elsewhere.

What accepting does depends on the terms:

- **Political** (and every demand filed before terms existed, such as a free-text
  `sovereign_recognition`): accepted for negotiation only. No law, border or budget changes, and
  the event says so. Relations ease one step, as before.
- **Regional project:** the answering ruler must own and hold the region. Acceptance starts, or
  links to an active, plan 31 project with the region as its target and the demand's purpose. It
  competes for the same monthly allowance as other work, stalls when underfunded and pauses
  while the region is lost. Relations ease one step on acceptance. The demand is **fulfilled**
  as the month after completion opens, with one more step. It **fails** (a broken promise, one
  step back) if the ruler cancels the work, or still holds the region when the deadline
  passes (twice the work's months). It **lapses** without blame if the region passes to another
  owner, or is occupied at the deadline. Work that is already completed or cancelled cannot
  answer a new demand, and a full project record blocks it.
- **Independence:** only the owner, holding the region and keeping other land, can grant it.
  Acceptance founds the state and cedes that one region in the same month, through the ordinary
  `create_actor` and `transfer_territory` rules (one new state a month; civilian works pass with
  the land). If any rule refuses, the whole grant rolls back and the demand is recorded as
  **blocked** with the reason, with no relations gain. An occupier cannot give away another
  country's sovereignty.

A blocked acceptance earns nothing, so accepting what cannot be done is never a free gain.
Relations still move at most one step a month. Under version 1 a regional initiative also moves
its own stance only one step, so it cannot erase a settlement's step in the same month.
AI rulers answering with `respond_regional` follow the same rules. The compiler appends what
actually happened to their description. Demand records stay bounded at eight per region, and an
accepted demand whose work is under way is never evicted. Acceptance and fulfilment use no
extra model call.
