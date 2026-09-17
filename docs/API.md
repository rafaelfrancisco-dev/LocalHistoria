# API v1

All routes use JSON at `http://127.0.0.1:8080/api/v1`. POST requires `Content-Type: application/json`; request body limit 32 KiB. Structured errors are `{"error":{"code":"validation","message":"..."}}` with HTTP 409, or `internal` with HTTP 500. Validation applies to the complete order list and never partially applies orders.

| Route | Method | Behavior |
|---|---|---|
| `/campaign` | GET | State or null, latest 24 journal entries, running jobs, provider mode |
| `/campaign` | POST | `{country:"PT",confirm_replace:false}`; confirmation required for existing campaign |
| `/countries` | GET | Country codes and names; no cities or national populations |
| `/country?code=PT` | GET | Metadata, top 15 city records, cached reference |
| `/proposals` | POST | `{campaign_id,expected_turn,text}`; returns a volatile job ID; result contains editable orders only |
| `/turns` | POST | `{campaign_id,expected_turn,idempotency_key,orders}`; returns persisted job ID, HTTP 202 |
| `/jobs?id=1` | GET | `running`, `complete`, `failed` or `interrupted`; fallback flag/error; auxiliary jobs also contain results |
| `/history?code=PT` | GET | Cached reference only, no network |
| `/history` | POST | `{code:"PT"}` explicitly downloads/refreshes reference in worker; returns volatile job ID |
| `/replay` | GET | Initial state, current campaign and every saved turn with state, resolved intentions, events and frozen references |

Example order list:

```json
[
  {"action":"economy"},
  {"action":"improve","target":"ES"},
  {"action":"offer","target":"ES"},
  {"action":"posture","value":"neutral"}
]
```

Other actions: `army`, `navy`, `accept`, `reject`, `priority`. Domestic actions cannot contain a target; diplomatic actions require an existing other actor. Unexpected fields and non-string action fields are invalid. `posture` values: defensive/neutral/assertive. `priority`: balanced/prosperity/welfare. Only accept/reject existing incoming offers. Arrays have at most 8 actions and cannot contain duplicate action/target pairs or accept+reject the same offer.

Retry an uncertain turn submission with the same UUID key and identical body. A changed body with the same key is an error. A response from an older campaign UUID cannot affect its replacement. Interrupted turns retain their original failed job; resubmit with a new key. Proposal/history jobs are bounded in-memory conveniences and may disappear after restart or eviction; request them again. The turn completes before optional narration; factual events are immediately available, and refreshing later can show model narration.
