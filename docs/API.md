# API v1

All routes use JSON at `http://127.0.0.1:8080/api/v1`. POST requires `Content-Type: application/json`; request body limit 32 KiB. Structured errors are `{"error":{"code":"validation","message":"..."}}` with HTTP 409, or `internal` with HTTP 500. Validation applies to the complete action list and never partially applies it.

| Route | Method | Behavior |
|---|---|---|
| `/campaign` | GET | State or null, latest 24 journal entries, running jobs, current model endpoint |
| `/campaign` | POST | `{country:"PT",confirm_replace:false}`; confirmation required for existing campaign |
| `/countries` | GET | Country codes and names; no cities or national populations |
| `/country?code=PT` | GET | Metadata, top 15 city records, cached reference |
| `/turns` | POST | `{campaign_id,expected_turn,idempotency_key,orders}`, where `orders` is the month's free-text actions; returns persisted job ID, HTTP 202 |
| `/jobs?id=1` | GET | `running`, `complete`, `failed` or `interrupted`; error message on failure; auxiliary jobs also contain results |
| `/model-config` | POST | `{llama_endpoint:"http://127.0.0.1:8081"}`; validates format, updates the live provider and persists it for future launches |
| `/history?code=PT` | GET | Cached reference only, no network |
| `/history` | POST | `{code:"PT"}` explicitly downloads/refreshes reference in worker; returns volatile job ID |
| `/replay` | GET | Initial state, current campaign and every saved turn with state, every actor's actions, events, frozen references and the accepted delta |

Example action list:

```json
[
  {"text": "Build a naval base in Faro", "targets": ["gb-24955722B93513210668499"]},
  {"text": "Propose a defensive alliance to Spain", "targets": ["ES"]},
  {"text": "Launch a national education reform"}
]
```

Each action has `text` (1–500 bytes, non-blank, unique within the list) and optional `targets`: up to four existing region, entity or actor IDs. Any other field is invalid. The list holds at most 8 actions and may be empty, which lets a month pass. There are no action types: the model decides outcomes. The game enforces realistic reach and pacing (see [Evolving world](EVOLVING_WORLD.md)), and an out-of-reach outcome is recorded as a failed attempt rather than an error. Campaign state contains `actors`, `territories`, `entities`, `conflicts` and `commitments`; there are no numeric national statistics.

Retry an uncertain turn submission with the same UUID key and identical body. A changed body with the same key is an error. A response from an older campaign UUID cannot affect its replacement. Interrupted turns retain their original failed job; resubmit with a new key. History and preparation jobs are bounded in-memory conveniences and may disappear after restart or eviction; request them again. The turn completes before optional narration; factual events are immediately available, and refreshing later can show model narration.
