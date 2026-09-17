# API v1

All routes except the SVG flag endpoint use JSON at `http://127.0.0.1:8080/api/v1`. POST requires `Content-Type: application/json`; request body limit 32 KiB. Structured errors are `{"error":{"code":"validation","message":"..."}}` with HTTP 409, or `internal` with HTTP 500. Validation applies to the complete action list and never partially applies it.

| Route | Method | Behavior |
|---|---|---|
| `/campaign` | GET | State or null, `national_status` for every actor, latest 24 journal entries, running jobs, current model endpoint |
| `/campaign` | POST | `{country:"PT",confirm_replace:false}`; confirmation required for existing campaign |
| `/countries` | GET | Country codes and names; no cities or national populations |
| `/country?code=PT` | GET | Metadata, top 15 city records, cached reference and, during a campaign, `status` |
| `/turns` | POST | `{campaign_id,expected_turn,idempotency_key,orders}`, where `orders` is the month's free-text actions; returns persisted job ID, HTTP 202 |
| `/jobs?id=1` | GET | `running`, `complete`, `failed` or `interrupted`; error message on failure; auxiliary jobs also contain results |
| `/model-config` | POST | `{llama_endpoint:"http://127.0.0.1:8081"}`; validates format, updates the live provider and persists it for future launches |
| `/history?code=PT` | GET | Cached reference only, no network |
| `/history` | POST | `{code:"PT"}` explicitly downloads/refreshes reference in worker; returns volatile job ID |
| `/entities` | GET | Campaign entities in a viewport (`west,east,south,north,zoom`; cities only from zoom 4), at most 200. Destroyed and inactive entities, such as faded event sites, are left out unless `include_inactive=1` |
| `/events` | GET | Every event that happened, newest month first: see below |
| `/replay` | GET | Initial state, current campaign and every saved turn with state, every actor's actions, events, frozen references and the accepted delta |

`GET /events` accepts `scope=all|actor` with `actor=<id>`, `category` (comma list of `player`, `nation`, `regional`, `world`, `war`, `construction`), `q` (text search), `major=1`, `on_map=1` (always major), `before_turn` and `limit` (at most 100, or 500 with `on_map`). It returns `{events, more, next_before_turn, turn}`. Pages hold whole months: pass `next_before_turn` as `before_turn` for older events. Each event has `turn`, `id`, `text`, `actor`, `category`, `major`, `actors`, `regions`, `entities`, `conflicts`, `commitments`, `causes`, `on_map`, `location` (up to four `{lat, lon}` points: its regions and entities, or else the actor's most populous region) and, when present, `effects`, `breaches`, `blocked_by`, `regional_actor`, `initiative_type` and `identity_changes`. An actor scope matches events it acts in, takes part in, or whose regions it owns or controls. Events rolled back as infeasible are never listed.

Example action list:

```json
[
  {"text": "Build a naval base in Faro", "targets": ["gb-24955722B93513210668499"]},
  {"text": "Propose a defensive alliance to Spain", "targets": ["ES"]},
  {"text": "Launch a national education reform"}
]
```

Each action has `text` (1–500 bytes, non-blank, unique within the list) and optional `targets`: up to four existing region, entity or actor IDs. Any other field is invalid. The list holds at most 8 actions and may be empty, which lets a month pass. There are no action types: the model decides outcomes. The game enforces realistic reach and pacing (see [Evolving world](EVOLVING_WORLD.md)), and an out-of-reach outcome is recorded as a failed attempt rather than an error. Campaign state contains `actors`, `territories`, `entities`, `conflicts` and `commitments`.

National status:

- Territories carry `population`, `gdp_usd` and `displaced` (people currently displaced from there), plus `occupation_gdp` while occupied.
- Actors carry `status`: `stability`, `baseline_stability`, `gdp_growth`, `pop_growth` (annual %), `currency`, `symbol`, `usd_rate`, `estimated`, `last_gdp_change` and `last_population_change`.
- `national_status[actor]` is derived from owned regions: `population`, `population_text`, `gdp` (e.g. `"€306.8 billion (US$346.6 billion)"`), `gdp_usd`, `gdp_per_capita`, `stability` (label and score), `stability_score`, `economic_trend`, `wars`, `treaties`, `occupied_regions`, `occupying_regions`, `displaced` and `estimated`.

Wars and treaties:

- Conflicts have `name`, `kind` (`war`, `border_conflict`, `insurgency`, `civil_war`), `started_turn`, optional `ended_turn`, `set_intensity` (declared), `intensity` (effective this month: `none`, `low`, `medium`, `high`), `affected_regions`, cumulative `casualties` and `displaced`, and `last_month_casualties`/`last_month_displaced`. Seeded wars also have a `theatre`.
- Commitments have `name`, `kind` (`alliance`, `defence_pact`, `non_aggression_pact`, `trade_agreement`, `ceasefire`, `peace_treaty`, `union`, `other`), `started_turn` and optional `ended_turn`.
- Turns before 0 are months before January 2026.
- Entries from older saves may lack these fields, and clients should fall back to the parties' names.
- Journal events with action `war_toll` report each active war's monthly casualties and displacement.
- Replay exports include `adjacency` once a campaign has national status.

Retry an uncertain turn submission with the same UUID key and identical body. A changed body with the same key is an error. A response from an older campaign UUID cannot affect its replacement. Interrupted turns retain their original failed job; resubmit with a new key. History and preparation jobs are bounded in-memory conveniences and may disappear after restart or eviction; request them again. The turn completes before optional narration; factual events are immediately available, and refreshing later can show model narration.

Independent activity adds no turn submission fields. The server reserves two non-player national initiatives and one regional initiative per month, redistributing missing slots when possible (maximum three). Every intention has a persisted outcome. `reference.allocations` records the selected slots; retries in the same campaign month reuse the persisted allocation.

State additionally contains `regional_politics` keyed by permanent territory ID and `activity` for selection cooldowns. Regional politics contains `agenda`, `relations`, up to eight demands (`pending`, `accepted`, `rejected`) and bounded `supporting_events`. Current governing authority is the territory's controller. Actors may contain `identity_revisions` and `flag_revision`; IDs never change with names. Journal entries include `structured_events`, with `initiative_type`, optional `regional_actor`, and `identity_changes` linking actor IDs to flag revisions.

`GET /flag?campaign=<campaign_id>&actor=<actor_id>&revision=<revision>` serves SVG with immutable caching and restrictive content security policy. `revision=fallback` provides a deterministic legacy fictional flag. Existing countries retain their static `/flags/<code>.svg` until a qualifying identity event. `/countries` and `/country` return current campaign names.

Reference preparation starts automatically on creation/resume and anticipates upcoming actors on a separate bounded worker. Submission freezes only material already cached and never waits for reference downloads. Failed refreshes preserve valid cached material. Missing optional political fields in older saves/replays decode as empty collections.
