# Evolving campaign world

Campaigns now persist actor identities, fixed territories with independent ownership/control, located entities, conflicts, commitments, and recorded model deltas. Existing development saves were removed with the application stopped and its exclusive save lock held. No migration is provided.

## Geography

The checked-in `data/source/gbOpen/index.json` pins the geoBoundaries gbOpen ADM1 API response. Individual URLs contain source commits; `manifest.json` retains source licenses, attribution, dates, source checksums, coverage status, rejected overlaps and the final geometry checksum. `licenses/geoBoundaries-NOTICE.md` gives attribution. This is a modified game geography, not an authoritative boundary product.

The importer clips ADM1 polygons against supplied country outlines, rejects overlapping subdivisions, and emits uncovered or unresolved territory explicitly. Source countries without a usable downloaded subdivision snapshot remain single territories. A final reconciliation removes overlap slivers and retains uncovered claim slivers as named residuals. Coordinates use a 0.00000001-degree grid. Geometry and source IDs remain fixed during play; ownership/control are campaign data. Initial affiliation never changes.

The reference database retains all supplied cities and separately indexes locations verified inside a region. Campaigns seed the three largest located cities per initial actor. New ports, bases, armies, cities and event sites persist in campaign state. Entity requests apply viewport/zoom filters and cap responses at 200. Unseeded reference cities are retained as reference data, not campaign entities.

Rebuild with `make geography-env` then `make data` when inputs change. For an explicit rebuild, run `.venv-geography/bin/python tools/import_data.py` followed by `.venv-geography/bin/python tools/import_regions.py`. Importing existing pinned files is offline and verifies their checksums. `--download` explicitly permits fetching missing pinned source files. Run `make geography-test` for exhaustive overlap, coverage and source verification.

## Monthly simulation

There are no fixed action types, costs or national statistics. Each month the player writes up to eight free-text actions, each optionally pinned to up to four region/entity/actor targets (clicking a map region adds one to the selected action). A turn:

1. Picks up to four AI nations to act: nations the player's actions name (matched on whole words) or target, open conflict and commitment parties, then one rotating neighbor. Each writes up to three free-text actions of its own, without thinking.
2. Runs `resolve`: advances the month and completes construction whose `completes_turn` has arrived, recording a factual completion event.
3. Asks the model to adjudicate every action together (with thinking) and compiles its answer into a validated delta.
4. Commits state, delta, events and inference records in one transaction, then requests narration.

Actor IDs are independent of country codes. A landless actor keeps playing.

`WorldDelta` contains `changes` and structured `events`. Changes contain `kind`, `id`, exact `before`, full `after`, supporting `event`, and `rationale`. Supported collections are actor, territory, entity, conflict and commitment. Deltas are what is validated, saved and replayed.

The model does not write deltas directly. Asking a 4B model for exact `before`/`after` objects produced invalid output in most scenarios. Instead it returns compact **operations nested in events**, and `compileOperations` in `src/world.cpp` derives the exact delta:

- **Events.** Each event has `actor`, `background`, `blocked_by`, `causes`, `description` and `operations`.
  - The event ID is `t<TURN>-<slug of description>`, deduplicated.
  - Related actors, regions and entities are derived from its operations.
  - An event may have no operations: reforms, trade, research and negotiations are recorded as history.
  - At most three background events are kept.
- **Operations.** Each is named by `action`: `build`, `transfer_territory`, `occupy`, `restore_control`, `move_entity`, `set_entity_status`, `create_actor`, `open_conflict`, `resolve_conflict`, `add_commitment`, `resolve_commitment`. Alliances and treaties are commitments; armies and fleets are entities.
- **Schema constraints.**
  - Region, entity, event-actor, conflict, commitment and cause references are restricted to IDs present in the supplied facts.
  - `blocked_by` accepts only `none` or an open commitment ID, so a denial cannot cite an invented rule.
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
- **New states.** At most one new state per month, and it must receive territory in the same event.

An event that breaks a limit is rolled back as a whole. It is kept as `blocked_by: "infeasible"` with the text "Could not happen this month (reason): …", and the rest of the month proceeds.

Adjacency is the `region_neighbors` table (`tools/region_neighbors.py`): regions that touch, or lie within 0.05° across a narrow strait. The adjudication prompt receives:

- the regions targeted or named by actions;
- the bordering regions of nations an action addresses;
- regions of the acting nations' occupations, armies and fleets;
- regions of open conflicts.

Each region comes with `within_reach_of`, listing the acting nations that can project force there.

Validation checks collection/ID references, preconditions, duplicate writes, locations (including polygon holes), construction schedules, fixed geography, new actor land allocation and event associations. Destruction is represented by status, not deletion. Conflicts and commitments use actor IDs, text, and open/resolved status.

Validation runs on a copy. The complete state, accepted delta, indexed event relationships and turn records commit in one SQLite transaction. The following fail without advancing the month:

- unknown causes;
- invalid model output;
- tokenizer failure;
- unavailable inference;
- oversized mandatory context.

Narration runs after commit and cannot mutate state; failed narration leaves the factual summary.

## Historical preparation and context

“Prepare campaign references” explicitly caches Wikipedia references for the player, initial neighbors and involved conflict/commitment actors. The result reports coverage and failures. Individual actor refresh remains available. No reference downloads occur as an implicit consequence of advancing a turn. Fictional actors have no real Wikipedia identity.

AI nation decisions, adjudication and narration share campaign context. SQLite indexes events by actor, region, entity and cause; a recursive query retrieves relevant earlier events and their causes. Recent events and older related events accompany current facts and commitments. Real references are marked untrusted and subordinate to fictional state; missing references remain unavailable. Geometry and the full journal are never model inputs.

Every inference reads the context size from `/props` (at least 8,192 tokens), applies the server chat template, then calls its tokenizer. The request reserves 768 output tokens, or 2,048 plus a reasoning budget of 256 tokens per action plus 256 (512–2,048) for adjudication, and 256 safety tokens inside the reported context. Adjudication reasons only when the context holds at least 16,384 tokens. Optional reference excerpts and then lower-ranked historical events are removed until the request fits. Current facts and commitments are never clipped. Exact selected messages, schema, token counts, template, model/build metadata, raw response and latency are saved for diagnosis. Turn snapshots retain original cached source revisions. Replay uses recorded deltas and requires no inference.

## API additions

- `GET /api/v1/map`: immutable `geometry_url`, campaign `territories`, and `turn`; join features by permanent ID. The browser caches geometry and refreshes campaign properties.
- `GET /api/v1/entities?west=...&east=...&south=...&north=...&zoom=...`: at most 200 entities; cities appear at zoom 4+. `?id=...` retrieves an individual entity regardless of viewport.
- `GET /api/v1/prepare`: reference coverage; `POST` with `{}` starts explicit preparation and returns a pollable job.
- `GET /api/v1/inference?job=...`: exact recorded model requests/results for a job.
- Replay exports include accepted deltas/context snapshots in each turn's `reference`, and only the geometry needed to validate changed entities.

## Measured model results

Model: Qwen3.5-4B Q4_K_M via `llama serve` (32,768-token context, `--cache-ram 1024 --ctx-checkpoints 8`) on a 16 GB Apple silicon MacBook Pro. Adjudication used thinking with a bounded budget; AI actions and narration did not. Measured 2026-09-17.

`build/world_probe out.json` runs seven isolated free-text scenarios on a four-region map. Each checks the outcome, not just validity. Prompts are about 740–820 tokens and each adjudication takes 20–32 s. Sampling (temperature 0.6) makes results vary between runs. Over the final three runs:

| Scenario (action) | Intended outcome | Runs matching |
| --- | --- | --- |
| construction: "Build a port in West." | port under construction in West | 3/3 |
| conflict (Spain): "Send the army across the border to occupy West." | West controlled by Spain, still owned by Portugal | 3/3 |
| secession: "Grant West independence as a new state named West Republic." | new state owns West | 2/3 (once created the state without land; the game blocked it) |
| long_history: "Construct a military base in West." under a treaty forbidding it | no base | 3/3 |
| reform: "Launch a national education reform." | recorded event, no map change | 3/3 |
| overreach: "Conquer all of Spain this month." | Spain's interior untouched, nothing annexed | 3/3 |
| capital: "Build a new city in North to serve as the future capital." | city under construction in North | 2/3 (once narrated only) |

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

Earlier runs showed the model's typical mistakes. It sometimes picks the wrong operation for an action (e.g. `build` in an enemy region for an invasion) or a mismatched reason. In those cases the game blocks the event, and the journal says why.

## Validation boundaries
 Structural validity is separate from whether a model outcome sensibly follows a player's action. There is no combat strength model: reach and tempo limit what force can do, but whether an attack succeeds is the model's judgment. A small model may misunderstand an action or narrate background events of little consequence. Campaign history retrieval retains 20 relevant historical events plus current commitments; it is not an unlimited-history prompt.
