# Architecture

`historia_core` depends only on JsonCpp and SQLite, never Drogon or inference. Typed `WorldState`, `CountryState`, `TurnOrders`, `ResolvedEvent` and `CountryContext` are in `simulation.h`. `DecisionProvider` and `NarrationProvider` are independent interfaces. `LocalProvider` implements both, accepting an injectable transport for tests; scripted decisions require no HTTP client.

The server validates a request, claims a persisted job under a mutex, snapshots the world and cached references, then queues work. HTTP event-loop threads do not run simulation or blocking inference. One bounded worker queue (8 waiting tasks) serializes turns, proposals, reference refreshes and narration. A provider mutex enforces one inference request at a time. A turn allows at most eight model decisions within a 60-second budget; actors beyond that allowance receive scripted decisions. Transport uses a dedicated event loop so shutdown does not cancel timeout dispatch. SQLite access is guarded separately; slow network work never holds the store mutex. A process-level file lock prevents two servers from owning the same save. The reference database opens read-only.

Every actor's intentions are checked against the same starting world. Administrative income and priority effects apply to all actors; action effects resolve in country-code and action/target/value order. No inference occurs inside `resolve`. Replay can feed recorded intentions directly to `resolve`, independent of model repeatability. A new offer cannot be accepted until a later turn. Relations are symmetric; offers are directional; alliances are symmetric. Acceptance clears reciprocal pending offers too.

Player requests carry campaign UUID, expected turn and idempotency key. Duplicate keys return the original job only if the entire request matches. A new key for a stale turn or a running campaign is rejected. Full validation precedes job creation. Model output is parsed strictly, rejects extra fields, and passes the same identifier, enum, duplicate, budget and diplomatic checks. A fallback decision is independently legal. Narration receives authoritative events only and has no mutating interface.

A transaction writes the resulting state, all intentions, events, frozen reference documents, factual summary, fallback flag and completed job status. Initial campaign state is saved separately for replay verification. Any failure rolls back all those changes. Optional narration replaces only its own row after commit. On restart, pre-commit running jobs become interrupted; their world did not advance. Resubmission uses a new key. Completed jobs remain completed even if narration was interrupted. New-game replacement clears the active campaign/journal/jobs atomically, retaining the reference cache. Job IDs are not reused.

Contexts contain national state, relevant relations/offers/treaties, a bounded selection of authoritative recent events, objectives, actions and valid target codes. Cached reference is explicitly separated and clipped. Requests larger than 6,000 UTF-8 bytes fall back instead of exceeding the conservative input allowance for an 8K model context; output is capped at 768 tokens. Cached material cannot authorize new actions. JSON schema is defense in depth; only C++ validation authorizes intentions.

Reference refresh uses fixed Wikimedia hosts, 12-second request timeouts, code validation and country-level ISO property P297. France, Norway, Kosovo and Taiwan use explicit entity overrides. English article titles come from entity sitelinks, never city IDs. Missing/ambiguous entities stay unavailable. Current references are not historically date constrained. The full document used by a turn is persisted before future refreshes can affect it.

## Extension points

- Combat: add typed legal orders, posture consequences and deterministic conflict resolution; preserve snapshot validation and record all random seeds.
- Historical scenarios: add versioned scenario datasets and explicit January-2026 source cutoffs; do not relabel current fictional stats as history.
- Enhanced models: implement the provider interfaces and retain strict local validation, request budgets and fallback.
- Reaction caching: key by provider/version, scenario, actor, full relevant state and frozen reference revision; cache intentions, not mutations.
- Training traces: export validated contexts, proposed/accepted intentions, rejection reasons and authoritative events from replay records; add consent and dataset licensing before training.
- Scale: separate reference/narration queues from turn resolution and add explicit cancellation, token accounting and a schema migration policy. This scaffold uses one worker to keep serialization clear.

No authentication or remote serving is provided. Host/origin checks and JSON-only mutation requests protect the local API from accidental cross-origin use. Do not expose the port publicly.
