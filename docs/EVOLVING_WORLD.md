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

1. Reserves two independent national actions and one independent regional action, then picks up to four reactive AI nations: nations the player's actions name (matched on whole words) or target, open conflict and commitment parties, then one rotating neighbor. Each writes one free-text action of its own, without thinking; nations the player's actions name or that are in an open conflict with the player write up to two, since those are the months that matter.
2. Runs `resolve`: advances the month and completes construction whose `completes_turn` has arrived, recording a factual completion event.
3. Asks the model to adjudicate every action together and compiles its answer into a validated delta. It thinks only when at least one action concerns war, military moves, borders, territory, independence, sanctions or treaties (`consequential()` in `src/world.cpp`); a month of reforms, talks and trade is answered directly.
4. Applies `applyMonthlyStatus`: demographic and economic trends, model effects, occupation, war fronts, casualties, displacement and stability.
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

- **Reach.** Force (`occupy`, an owner retaking land, destroying another nation's entity) reaches a region only if it borders land the actor controls, or holds or borders one of its active armies or fleets. Residual sliver regions grant no reach.
- **Tempo.** At most two regions per actor change hands by force per month.
- **Annexation.** An owner may cede land. Anyone else must have controlled the region since before this month, so conquest takes two steps: occupy, then annex a later month.
- **Movement.** Armies move one neighboring region per month and fleets up to 1,500 km. Other entities cannot relocate. Only an entity's owner moves it, and never while it is unfinished.
- **Construction.** `build` needs control of the region (except event sites). It creates `under_construction` entities finishing after: army 2 months, base 3, fleet 4, port 6, city 24. Status changes cannot complete construction or restore destroyed entities.
- **Event sites.** An `event_site` marks where a notable incident happened. It is active at once with `expires_turn` three months ahead (`eventMapMonths`), and `resolve` makes it `inactive` when that month arrives, without a journal line. A later feasible event renews it for another three months when it involves the site or cites the site's originating event as a cause. Only active event sites carry `expires_turn`; sites saved before expiry existed keep none until an event touches them, so replay is unchanged.
- **New states.** At most one new state per month, and it must receive territory in the same event.

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

An operation that breaks a limit is dropped on its own. The rest of its event still happens, the event text stays the model's description, and the reasons are kept only in the event's `dropped_operations` (`{action, reason}`) for the audit, never in text that players or later prompts read. The event is saved with `partial: true` when some of its operations (or treaty terms) were dropped, or `failed: true` when every operation was. The Events dialog shows a "partly happened" or "did not happen" badge, and the journal line starts with "Partial · " or "Failed · ". Later prompts read what actually happened: related history, regional history, recent events and chronicle inputs append a short clause built from the dropped actions, such as "Invade Pakistan (In fact: no forces moved.)". The saved text, the journal and the narration keep the model's description. Operations that depend on a dropped one (such as handing land to a state that was not created) fail and are dropped too. Only a new state left without territory rolls back its whole event: it is saved as `blocked_by: "infeasible"` and kept out of the journal, narration and later history (related history, regional history and chronicle inputs all skip it).

Events also record the wars (`conflicts`) and treaties (`commitments`) their operations opened, escalated or ended.

**Significance.** The model does not label significance; C++ derives `major` on the event from what it did (an earlier `significance` field cost tokens on every event and was mostly called `major`):
- Events whose operations changed the map or a war are always major. That covers occupation, handover or restored control, wars opened, escalated or ended, new states, identity changes, treaties other than trade agreements and `other`, ended commitments, event sites, destroyed entities, and treaty breaches.
- An event with a `severe_decline` or `strong_growth` effect is a declared headline. Declared headlines are capped at three a month, keeping those with the weightiest effects.
- World events without any effect are dropped, so background developments exist only for what changed something.
- Blocked and rolled-back events are never major.
- Events saved before `major` existed count as major only for a breach, a new flag or a severe effect.

**Events on the map.** The map derives event markers from saved history without changing state. Only major events appear. A major event is on the map for three months after the latest of its own month and the month of the last feasible event citing it as a cause, and for as long as a war it shaped is open. War tolls are routine (fronts already show in the Wars & treaties map mode), and of construction only finished cities are major. Markers sit at the event's regions and entities, or at the acting nation's most populous region.

Adjacency is the `region_neighbors` table (`tools/region_neighbors.py`): regions that touch, or lie within 0.05° across a narrow strait. The adjudication prompt receives:

- the regions targeted or named by actions;
- the bordering regions of nations an action addresses (up to 24 regions in all);
- regions of the acting nations' occupations, armies and fleets;
- the fronts of open conflicts that this month's actors fight (up to 36 regions in all).

Entities in those regions are listed too, except cities outside the regions actions name and the fronts.

A region comes with `within_reach_of` when an acting nation can project force there, listing those nations.

An action that reads as an attack (invade, occupy, attack, offensive, conquer, seize, annex, bombard) gets a short hint appended to its text: use `occupy` on one or two regions whose `within_reach_of` lists the actor, plus `open_conflict` unless already at war, and never `move_entity` on cities. When no listed foreign region is within reach, the hint says to describe preparations instead. The general example sits far from the action in a long prompt; the hint puts it next to the action the model is adjudicating. For a short order such as "Invade Spain." the six-word outcome opener includes the start of the hint, so the model restates it before answering. `move_entity` only offers the active armies and fleets among the listed entities, and is absent from the schema when there are none.

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

**Seeding.** `seedStatus` gives each actor its `country_stats` row and each region its `region_stats` share. New campaigns also receive the January 2026 wars, occupations and treaties. A save from before national status is seeded (without the 2026 world) at its next month, and that turn freezes the inputs in `reference.status_seed` for replay.

**Grounding.** `context()` adds a compact `status` to every decision, adjudication and narration input:

- population, GDP in local currency and USD, GDP per capita, stability label and economic trend;
- wars and treaties by name, kind, intensity and totals (blocs above six members show only their size).

Relevant regions show their population. The adjudication prompt asks the model to judge feasibility against economy size, population, stability and war burden.

**Names.** `realisticName` keeps a specific proposed name. A name made only of generic words (war, treaty, pact…), or one repeating the description, is replaced with "Portugal–Spain War", "Civil War in X", "X Insurgency", "Treaty of <largest city>" or "A–B Defence Pact". Names already used, even by ended entries, get "Second", "Third"…

**Each month**, after the accepted delta:

1. **Trend.** Region population and GDP grow by the owner's annual rates ÷ 12 (clamped to ±5% and ±10% a year).
2. **Model effects.** Economy: −1.0 / −0.3 / 0 / +0.2 / +0.5% GDP. Stability: −6 / −2 / 0 / +1 / +3. Per-actor totals are capped at ±1.5% and ±8, and a quarter of the stability effect shifts the baseline.
3. **Occupation.** Occupied regions lose 2% of output a month, down to 40% of their value when occupation began. Seeded occupations start at that floor.
4. **Fronts.** Each open conflict's affected regions, capped at 12, are:
   - regions whose control changed between its parties this month;
   - regions of the parties where a party's entity was destroyed;
   - seeded theatre regions still held by a party;
   - regions controlled by a party that border a region controlled by another party.

   Effective intensity is `none` under an open ceasefire or peace treaty between two parties, `high` when control changed or an entity was destroyed this month, `low` when there is no front, and otherwise the declared intensity.
5. **Toll**, per month by intensity low / medium / high, scaled by kind (war and civil war 1.0, border conflict 0.4, insurgency 0.3):

   | | low | medium | high |
   | --- | --- | --- | --- |
   | Military deaths, share of each party's national population (landless parties: a quarter of the front's population) | 0.0001% | 0.002% | 0.008% |
   | Civilian deaths, share of each front region | 0.0003% | 0.0015% | 0.005% |
   | Displacement, share of each front region (plus a one-off 8% when a region changes hands) | 0.03% | 0.15% | 0.4% |
   | Front GDP loss | 0.1% | 0.4% | 1% |
   | GDP drag across the parties' land | 0.005% | 0.02% | 0.05% |

   - Deaths come out of the population a party recruits from, at most 1% of a region per month.
   - 35% of displaced people cross into bordering non-party countries; the rest go to the owner's unaffected regions. With nowhere safe, they go anywhere else in the country.
   - Half of per-capita output moves with them.
   - Once a region is calm and back under its owner, 3% of its displaced return each month.
   - Population is conserved except for deaths.
6. **Stability** moves 8% of the way toward a target each month, by at most 10 points. The target is the baseline, minus 3 / 8 / 15 for the most intense war, minus 25 × the occupied share of population, minus 5 when GDP fell over 1% that month. Deaths above 0.01% of population cost another point.

A year of the seeded Russo-Ukrainian War at high intensity produces about 180,000 deaths and 0.9 million displaced, and roughly 55,000 deaths in the Sudanese Civil War. That is the order of magnitude of 2023–25 estimates. Displacement within the country is simplified: people displaced from a region are tracked there, but refugees who crossed a border are not sent home.

Limitations: conflicts have parties, not sides, so co-belligerents listed in one war count as opposing each other. Military deaths are not tied to entities. Regional splits are estimates.

## Historical preparation and context

“Prepare campaign references” can explicitly refresh Wikipedia references for the player, initial neighbors and involved conflict/commitment actors. The result reports coverage and failures. Individual actor refresh remains available. Automatic background preparation also prioritizes upcoming independent actors at creation/resume and after commits. Monthly inference never waits for reference downloads; submission freezes available material. Fictional actors have no real Wikipedia identity.

AI nation decisions, adjudication and narration share campaign context. SQLite indexes events by actor, region, entity and cause; a recursive query retrieves relevant earlier events and their causes. Recent events and older related events accompany current facts and commitments. Real references are marked untrusted and subordinate to fictional state; missing references remain unavailable. Geometry and the full journal are never model inputs.

Every inference reads the context size from `/props` (at least 8,192 tokens, cached for 60 seconds). A request whose byte estimate fits well inside it is sent at once; otherwise the server chat template is applied and its tokenizer counts the tokens. The request reserves 768 output tokens, or for adjudication 768 plus 160 per outcome (bounded to 1,536–4,096) plus a reasoning budget of 256 plus 128 per consequential action (384–768, zero when no action is consequential), and 256 safety tokens inside the reported context. Adjudication reasons only when the context holds at least 16,384 tokens. Optional reference excerpts and then lower-ranked historical events are removed until the request fits. Current facts and commitments are never clipped. Exact selected messages, schema, token counts, template, model/build metadata, raw response and latency are saved for diagnosis. Turn snapshots retain original cached source revisions. Replay uses recorded deltas and requires no inference.

## API additions

- `GET /api/v1/map`: `geometry_url` (`/regions.topojson?v=<content hash>`, served gzipped with `Cache-Control: immutable`), campaign `territories`, and `turn`; join the topology's `regions` geometries by permanent ID. The browser caches geometry and refreshes campaign properties. Map moves are debounced (300 ms) and refetch entities only outside the last padded viewport or in a new month; event markers refetch only in a new month.
- `GET /api/v1/campaign` carries an `ETag` (hash of the response) and answers `If-None-Match` with 304.
- `GET /api/v1/entities?west=...&east=...&south=...&north=...&zoom=...`: at most 200 entities; cities appear at zoom 4+. `?id=...` retrieves an individual entity regardless of viewport.
- `GET /api/v1/prepare`: reference coverage; `POST` with `{}` starts explicit preparation and returns a pollable job.
- `GET /api/v1/inference?job=...&since=...`: exact recorded model requests/results for a job, tagged `kind` (`decide`/`adjudicate`/`narrate`), `actor` and the parsed `answer`. Each request/adjudication/decision flushes as soon as it lands, before the month is committed, so a running turn can be polled for a live log; `since` (the highest `id` already seen) returns only what is new.
- Replay exports include accepted deltas/context snapshots in each turn's `reference`, and only the geometry needed to validate changed entities.

## Measured model results

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
| invasion: "Invade Spain." | East controlled by Portugal, still owned by Spain, one open war | 2/3 (once the model narrated the invasion as repelled and tried to build armies in East, which the game dropped) |
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
   - the conquest became a declaration of war, occupation of bordering Galicia and an expeditionary army under construction.
2. **Turn 2.** "Occupy Comunitat Valenciana", "Raise a new army in Guarda":
   - the model judged the distant occupation impossible until the army was ready;
   - the Guarda army started construction.
   - Spain's attempt to build in occupied Galicia was blocked by the game.
3. **Turn 3.** "Grant Guarda independence as a new state named Free Lusitania":
   - Free Lusitania was created and owns Guarda;
   - the expeditionary army completed on schedule;
   - Spain accepted a ceasefire, resolving the war.

Turn output diet (measured 2026-09-18, `tools/living_world_probe.py --months 2`, Portugal with no player orders, so each month adjudicates four intentions: two independent national, one regional and one reactive neighbor). Before: 159 s and 212 s to commit, with adjudication generating about 2,650 tokens (950 reasoning) for a 13-intention month. After reactive nations dropped to one or two actions, `significance` left the schema, descriptions shrank to 160 characters and reasoning became conditional: 94.6 s and 87.5 s to commit; adjudication generated 1,218 and 1,294 tokens including reasoning (both months reasoned, since "trade accord" and "data sovereignty" match the consequential stems), prompts of 4,327 and 4,581 tokens, 37–40 s of generation; each decision generated 48–215 tokens. Both months replayed identically.

Earlier runs showed the model's typical mistakes. It sometimes picks the wrong operation for an action (e.g. `build` in an enemy region for an invasion) or a mismatched reason. In those cases the game blocks the event, and the journal says why.

## Validation boundaries
 Structural validity is separate from whether a model outcome sensibly follows a player's action. There is no combat strength model: reach and tempo limit what force can do, but whether an attack succeeds is the model's judgment. A small model may misunderstand an action or narrate background events of little consequence. Campaign history retrieval retains 20 relevant historical events plus current commitments; it is not an unlimited-history prompt.

See [living political world](../README.md#a-living-political-world) and [architecture](ARCHITECTURE.md#independent-politics-and-identity-persistence) for regional authorization, identity operations, deterministic flags and compatibility. The model timing results above predate the three additional independent monthly actions.
