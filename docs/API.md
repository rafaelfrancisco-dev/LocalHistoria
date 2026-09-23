# API v1

All routes except the SVG flag endpoint use JSON at `http://127.0.0.1:8080/api/v1`. POST requires `Content-Type: application/json`; request body limit 32 KiB. Structured errors are `{"error":{"code":"validation","message":"..."}}` with HTTP 409, or `internal` with HTTP 500. Validation applies to the complete action list and never partially applies it.

| Route | Method | Behavior |
|---|---|---|
| `/campaign` | GET | State or null, `national_status` for every actor, latest 24 journal entries (each with `changes`, `[{kind,id,summary}]` for the month's territory, unit, war, treaty and name changes in words, named as they are now), running jobs, current model endpoint |
| `/campaign` | POST | `{country:"PT",confirm_replace:false}`; confirmation required for existing campaign |
| `/countries` | GET | Country codes and names; no cities or national populations. During a campaign each row also has `has_flag` (a bundled static flag exists), `government` (the campaign's current system, readable) and `neighbor: true` for the player's land neighbors |
| `/country?code=PT` | GET | Metadata, top 15 city records, cached reference and, during a campaign, `status` |
| `/turns` | POST | `{campaign_id,expected_turn,idempotency_key,orders,responses?,narration?}`, where `narration: false` skips the optional model narration (the journal keeps the factual summary), and `orders` is the month's free-text actions and optional `responses` (at most 8, `[{proposal,accept}]`) answers pending treaty proposals in `state.proposals`; unanswered proposals lapse. Returns persisted job ID, HTTP 202 |
| `/turns/cancel` | POST | `{job:"<id>"}` cancels a running month, HTTP 202. The worker stops before its next model call, and a request already in flight finishes first. The job then fails with "Cancelled by the player" and no state change. A cancel accepted before commit always prevents the commit. A job that is not running (already complete, failed or unknown) is refused with 409 |
| `/jobs?id=1` | GET | `running`, `complete`, `failed` or `interrupted`; error message on failure; auxiliary jobs also contain results |
| `/model-config` | POST | `{llama_endpoint:"http://127.0.0.1:8081"}`; validates format, updates the live provider and persists it for future launches |
| `/history?code=PT` | GET | Cached reference only, no network |
| `/history` | POST | `{code:"PT"}` explicitly downloads/refreshes reference in worker; returns volatile job ID |
| `/entities` | GET | Campaign entities in a viewport (`west,east,south,north,zoom`; cities only from zoom 4), at most 200. Destroyed and inactive entities, such as faded event sites, are left out unless `include_inactive=1` |
| `/events` | GET | Every event that happened, newest month first: see below |
| `/replay` | GET | Initial state, current campaign and every saved turn with state, every actor's actions, events, frozen references and the accepted delta |

`GET /events` accepts `scope=all|actor` with `actor=<id>`, `category` (comma list of `player`, `nation`, `regional`, `domestic`, `world`, `war`, `construction`), `q` (text search), `major=1`, `on_map=1` (always major), `before_turn` and `limit` (at most 100, or 500 with `on_map`). It returns `{events, more, next_before_turn, turn}`. Pages hold whole months: pass `next_before_turn` as `before_turn` for older events. Each event has `turn`, `id`, `text`, `actor`, `category`, `major`, `actors`, `regions`, `entities`, `conflicts`, `commitments`, `causes`, `on_map`, `location` (up to four `{lat, lon}` points: its regions and entities, or else the actor's most populous region) and, when present, `effects`, `breaches`, `blocked_by`, `partial` (some operations were dropped), `failed` (every operation was dropped), `regional_actor`, `initiative_type` and `identity_changes`. An actor scope matches events it acts in, takes part in, or whose regions it owns or controls. Events rolled back as infeasible are never listed.

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

- Territories carry `population`, `gdp_usd` and `displaced` (people currently displaced from there), plus `occupation_gdp` while occupied, and `unrest` (0–100) with `unrest_months` once a region has begun to stir. Under the realism lifecycle a region held by force also carries `control`: `claimant`, `holder`, `since` (absent for occupations older than the history), `cause` (`hostile` or `recorded`) and `integration` (0–100). All are engine-owned: `checkTerritoryChange` lets a model delta alter only `owner` and `controller`, and `control` never reaches a model prompt.
- Actors carry `status`: `stability`, `baseline_stability`, `seeded_stability` (the January figure, frozen), `government`, `government_since` (a year, 0 when unknown), `gdp_growth`, `pop_growth` (annual %), `currency`, `symbol`, `usd_rate`, `estimated`, `last_gdp_change`, `last_population_change` and `collapse_months`. A rebel actor also carries `rebellion` with `against`, `origin`, `conflict`, `since_turn`, `held_since` and `state`.
- `national_status[actor]` is derived from owned regions: `population`, `population_text`, `gdp` (e.g. `"€306.8 billion (US$346.6 billion)"`), `gdp_usd`, `gdp_per_capita`, `stability` (label and score), `stability_score`, `economic_trend`, `government` (readable, e.g. `"Semi-presidential republic"`), `government_id`, `government_since`, `wars`, `treaties`, `occupied_regions`, `occupying_regions`, `displaced` and `estimated`. When any region it controls is at 35 unrest or more it also carries `unrest` (label, region and score), `unrest_score` and `unrest_region`; a calm nation carries none of the three.

Military inventory:

- `actors[id].status.military` contains non-negative integer `active`, `reserve`, `ships`, `aircraft` and an `estimated` flag. `status.military_spend_usd` retains reference spending; zero can mean unknown.
- `national_status[id].military` exposes those fields plus `uncommitted_personnel`, `uncommitted_ships` and `label`; `military_text` is the readable summary. Model context replaces the numeric `status.military` object with that summary. API clients continue to receive numbers.
- Army entities carry integer `personnel`; fleets carry integer `ships`. Active and unfinished formations reserve inventory; inactive formations release it. `loss_accounted` on destroyed formations prevents repeat deductions.
- Turns with `status_version: 2` use inventory-based combat and casualties. A legacy save receives a frozen `reference.military_seed` on its first upgraded turn; replay applies this without current reference data. Historical turns without inventories retain the old casualty calculation.
- `status_version: 4` adds regional unrest and the rebellion, secession and regime-collapse chain it feeds. An engine-raised rebellion emits `revolt` (a new rebel actor and an `insurgency` or `civil_war`), its seizures emit `front_advance`, `secession` transfers ownership and declares a breakaway state, `regime_change` replaces a collapsed government, and band changes emit `unrest`. All five are `domestic` category; all but `unrest` are major. Under the realism lifecycle an annexed conquest that completes integration emits a major `integrated` domestic event, and the `unrest` text for the 55–70 band reads "is in unrest". At most one rebellion and one new state a month, campaign-wide. Turns recorded at 3 or below gain no unrest at all. A save written before political systems existed receives a frozen `reference.government_seed` on its first later month, applied by replay without current reference data.
- `status_version: 3` also recomputes conventional front momentum and stalemate months. An autonomous seizure emits `front_advance` with `actor`, conflict `target`, exact `region` and factual `text`; it appears in the journal changes and as a major map event. Replay uses each turn's recorded version.

Wars and treaties:

- Conflicts have `name`, `kind` (`war`, `border_conflict`, `insurgency`, `civil_war`), `started_turn`, optional `ended_turn`, `set_intensity` (declared), `intensity` (effective this month: `none`, `low`, `medium`, `high`), `affected_regions`, cumulative `casualties` and `displaced`, and `last_month_casualties`/`last_month_displaced`. Seeded wars also have a `theatre`.
- Commitments have `name`, `kind` (`alliance`, `defence_pact`, `non_aggression_pact`, `trade_agreement`, `ceasefire`, `peace_treaty`, `union`, `other`), `started_turn` and optional `ended_turn`.
- Turns before 0 are months before January 2026.
- Entries from older saves may lack these fields, and clients should fall back to the parties' names.
- Journal events with action `war_toll` report each active war's monthly casualties and displacement.
- Replay exports include `adjacency` once a campaign has national status.

Retry an uncertain turn submission with the same UUID key and identical body. A changed body with the same key is an error. A response from an older campaign UUID cannot affect its replacement. Interrupted turns retain their original failed job; resubmit with a new key. History and preparation jobs are bounded in-memory conveniences and may disappear after restart or eviction; request them again. The turn completes before optional narration; factual events are immediately available, and refreshing later can show model narration. A month has a total time budget, `turn_budget_seconds` in the server config (default twice `timeout_seconds`, at most 1800). Each model request's timeout is capped at the time left, and a month that runs out fails with "Month exceeded its time budget" and no state change.

Independent activity adds no turn submission fields. The server reserves two non-player national initiatives and one regional initiative per month, redistributing missing slots when possible (maximum three). Every intention has a persisted outcome. `reference.allocations` records the selected slots; retries in the same campaign month reuse the persisted allocation.

State additionally contains `regional_politics` keyed by permanent territory ID and `activity` for selection cooldowns. Regional politics contains `agenda`, `relations`, up to eight demands (`pending`, `accepted`, `rejected`) and bounded `supporting_events`. Current governing authority is the territory's controller. Actors may contain `identity_revisions` and `flag_revision`; IDs never change with names. Journal entries include `structured_events`, with `initiative_type`, optional `regional_actor`, and `identity_changes` linking actor IDs to flag revisions. Structured events carry `partial` or `failed` when some or all of their operations were dropped; the matching journal event then has `prompt_text`, its text plus the factual clause later prompts read (see docs/EVOLVING_WORLD.md).

`GET /flag?campaign=<campaign_id>&actor=<actor_id>&revision=<revision>` serves SVG with immutable caching and restrictive content security policy. `revision=fallback` provides a deterministic legacy fictional flag. Existing countries retain their static `/flags/<code>.svg` until a qualifying identity event. `/countries` and `/country` return current campaign names.

Reference preparation starts automatically on creation/resume and anticipates upcoming actors on a separate bounded worker. Submission freezes only material already cached and never waits for reference downloads. Failed refreshes preserve valid cached material. Missing optional political fields in older saves/replays decode as empty collections.
