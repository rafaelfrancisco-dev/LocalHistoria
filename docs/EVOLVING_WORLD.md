# Evolving campaign world

Campaigns now persist actor identities, fixed territories with independent ownership/control, located entities, conflicts, commitments, and recorded model deltas. Existing development saves were removed with the application stopped and its exclusive save lock held. No migration is provided.

## Geography

The checked-in `data/source/gbOpen/index.json` pins the geoBoundaries gbOpen ADM1 API response. Individual URLs contain source commits; `manifest.json` retains source licenses, attribution, dates, source checksums, coverage status, rejected overlaps and the final geometry checksum. `licenses/geoBoundaries-NOTICE.md` gives attribution. This is a modified game geography, not an authoritative boundary product.

The importer clips ADM1 polygons against supplied country outlines, rejects overlapping subdivisions, and emits uncovered or unresolved territory explicitly. Source countries without a usable downloaded subdivision snapshot remain single territories. A final reconciliation removes overlap slivers and retains uncovered claim slivers as named residuals. Coordinates use a 0.00000001-degree grid. Geometry and source IDs remain fixed during play; ownership/control are campaign data. Initial affiliation never changes.

The reference database retains all supplied cities and separately indexes locations verified inside a region. Campaigns seed the three largest located cities per initial actor. New ports, bases, armies, cities and event sites persist in campaign state. Entity requests apply viewport/zoom filters and cap responses at 200. Unseeded reference cities are retained as reference data, not campaign entities.

Rebuild with `make geography-env` then `make data` when inputs change. For an explicit rebuild, run `.venv-geography/bin/python tools/import_data.py` followed by `.venv-geography/bin/python tools/import_regions.py`. Importing existing pinned files is offline and verifies their checksums. `--download` explicitly permits fetching missing pinned source files. Run `make geography-test` for exhaustive overlap, coverage and source verification.

## Monthly simulation

Reviewed economic/diplomatic orders remain supported. New intentions are `construction`, `movement`, `conflict`, and `territorial_diplomacy`; each carries an existing region/entity/actor `target` and a 1–500-byte `value` describing the intended outcome. The map copies selected region IDs into the intention controls. Proposals populate the reviewable draft and never advance the campaign.

A turn obtains relevant actor decisions, resolves existing economic rules, then asks the model to adjudicate consequences and up to three background events. Direct targets and ongoing disputes/commitments are included; neighbors rotate by turn, with a rotating actor when there are no other candidates. Actor IDs are independent of country codes. A landless actor retains its resources and diplomatic play.

`WorldDelta` contains `changes` and structured `events`. Changes contain `kind`, `id`, exact `before`, full `after`, supporting `event`, and `rationale`. Supported collections are actor, territory, entity, resources, conflict and commitment. Deltas are what is validated, saved and replayed.

The model does not write deltas directly. Asking a 4B model for exact `before`/`after` objects produced invalid output in most scenarios, so it returns compact **operations nested in events** (`compileOperations` in `src/world.cpp`), and C++ derives the exact delta:

- Each event has `actor`, `background`, `blocked_by`, `causes`, `description` and `operations`. The event ID is `t<TURN>-<slug of description>`, deduplicated. Related actors, regions and entities are derived from its operations.
- Operations: `build`, `spend`, `transfer_resources`, `adjust_stability`, `transfer_territory`, `occupy`, `restore_control`, `move_entity`, `set_entity_status`, `create_actor`, `open_conflict`, `resolve_conflict`, `add_commitment`, `resolve_commitment`. Each is named by `action`.
- The schema restricts region, entity, actor, conflict, commitment and cause references to IDs present in the supplied facts. `blocked_by` accepts only `none` or an open commitment ID, so a denial cannot cite an invented rule.
- C++ assigns entity IDs (`e<TURN>-<name slug>`), places new or moved entities at the region's verified interior point, and assigns conflict/commitment IDs. New actor IDs are derived from the actor name; later operations may refer to the new actor by its requested ID or its name.
- Affordability is checked by C++. If an event's spending or transfers exceed the actor's resources, that event's operations are rolled back and it is recorded with `blocked_by: "cannot_pay"`. Exact repeated operations within one event are ignored.
- jsoncpp serializes schema keys alphabetically and llama.cpp grammars keep that order. Key names are chosen so `description` precedes `operations` and `action` leads each operation.

Adjudication receives only the regions targeted by intentions (plus regions of open conflicts), without coordinates. Intentions are rendered as sentences such as `Military action targeting Comunitat Valenciana (region gb-…): Occupy …`; the raw order name `conflict` led the model to demand a pre-existing conflict.

Validation checks collection/ID references, preconditions, duplicate writes, locations (including polygon holes), resource bounds, fixed geography, new actor land allocation and event associations. New actors start with zero resources. Total treasury, income, army and navy cannot increase through adjudication, so donors must fund new states explicitly. The economic stage remains responsible for resource growth. Destruction is represented by status, not deletion. Conflicts and commitments use actor IDs, text, and open/resolved status.

Validation runs on a copy. The complete state, accepted delta, indexed event relationships and turn records commit in one SQLite transaction. Unknown causes, invalid model output, tokenizer failure, unavailable inference, and oversized mandatory context fail without advancing the month. Narration runs after commit and cannot mutate state; failed narration leaves the factual summary.

## Historical preparation and context

“Prepare campaign references” explicitly caches Wikipedia references for the player, initial neighbors and involved conflict/commitment actors. The result reports coverage and failures. Individual actor refresh remains available. No reference downloads occur as an implicit consequence of advancing a turn. Fictional actors have no real Wikipedia identity.

Proposals, actor decisions, adjudication and narration share campaign context. SQLite indexes events by actor, region, entity and cause; a recursive query retrieves relevant earlier events and their causes. Recent events and older related events accompany current facts and commitments. Real references are marked untrusted and subordinate to fictional state; missing references remain unavailable. Geometry and the full journal are never model inputs.

Every inference verifies `/props` exposes at least 8,192 context tokens, applies the server chat template, then calls its tokenizer. The request reserves 768 output tokens (2,048 for adjudication) and 256 safety tokens inside an 8,192-token budget. Optional reference excerpts and then lower-ranked historical events are removed until the request fits. Current facts and commitments are never clipped. Exact selected messages, schema, token counts, template, model/build metadata, raw response and latency are saved for diagnosis. Turn snapshots retain original cached source revisions. Replay uses recorded deltas and requires no inference.

## API additions

- `GET /api/v1/map`: immutable `geometry_url`, campaign `territories`, and `turn`; join features by permanent ID. The browser caches geometry and refreshes campaign properties.
- `GET /api/v1/entities?west=...&east=...&south=...&north=...&zoom=...`: at most 200 entities; cities appear at zoom 4+. `?id=...` retrieves an individual entity regardless of viewport.
- `GET /api/v1/prepare`: reference coverage; `POST` with `{}` starts explicit preparation and returns a pollable job.
- `GET /api/v1/inference?job=...`: exact recorded model requests/results for a job.
- Replay exports include accepted deltas/context snapshots in each turn's `reference`, and only the geometry needed to validate changed entities.

## Measured model results

Model: Qwen3.5-4B Q4_K_M via llama-server on port 8081 (8,192-token context, temperature 0). Measured 2026-09-17.

`build/world_probe out.json` runs four isolated two-region scenarios and checks the outcome, not just validity. Prompts are about 570–730 tokens and latency is 4–8 s. Results were the same across repeated runs:

| Scenario | Valid | Intended outcome |
| --- | --- | --- |
| construction (port funded from treasury) | yes | yes (port entity + treasury spend) |
| conflict (ES occupies PT region) | yes | yes (controller ES, owner PT, conflict opened) |
| secession (grant independence with funds) | yes | yes (new actor owns region; treasury conserved; transferred 1 income instead of the requested 3) |
| long_history (base forbidden by turn-1 treaty) | yes | yes (blocked by `treaty-1`, no base) |

Earlier formats scored lower: full before/after objects 2/4 valid, six-element tuples 0/4.

End-to-end campaign turns (disposable save, Portugal, real ADM1 regions; each turn 16–22 s including actor decisions and narration; adjudication prompts 760–1,030 tokens):

- Construction in Viana do Castelo: committed port entity and treasury spend.
- Occupation of Comunitat Valenciana with equal armies (10 vs 10): the model denied it, citing lack of military superiority. The turn still advanced; this is a judgment call, not a validation failure.
- Independence for Guarda as Free Lusitania: committed a new actor owning the region.

Before trimming the prompt to relevant regions, adjudication prompts were about 5,200 tokens, and the model denied almost every intention with invented prerequisites.

## Validation boundaries
 Structural validity is separate from whether a model outcome sensibly follows a player's intention. No fixed combat engine or automatic correction of rejected deltas is implemented. A small model may reject, misunderstand, or fail to produce a valid outcome. Campaign history retrieval retains 20 relevant historical events plus current commitments; it is not an unlimited-history prompt.
