# LocalHistoria audit (2026-09-18)

Scope: correctness of the simulation, turn performance, UI, and code quality, at commit `ba07b84` (after the
Windows commit). Everything below was checked against the code, the test suites, the docs, and the real
campaign save in `saves/campaign.sqlite` (India, two completed months, 21 recorded model calls). No code was
changed by this audit. Each finding has an ID so tasks can be handed off one at a time; the last section is an
ordered task list with acceptance criteria.

Verified on this machine (macOS 27, M1 Pro 16 GB, AppleClang 21, llama.cpp build 11011, Qwen3.5-4B Q4_K_M):
the project builds with zero warnings and all six CTest suites pass, before and after the pull.

---

## 1. Executive summary

The architecture is sound: strict C++ validation of everything the model says, deterministic replay, one
transaction per month, and a good separation between decisions, adjudication and consequences. The problems
are concentrated in four places:

1. **Turn time is dominated by one call.** A real month took 159 s and 212 s. Adjudication alone was 100 s
   and 123 s, and in the second month its prompt doubled to 12,420 tokens because every acting nation drags in
   references, history and cities. Most of the remaining time is spent on AI nations that do not matter to the
   player (Comoros, Slovenia, Netherlands, Germany acting in an India campaign). Section 3 lays out a plan that
   should roughly halve a month without changing the model.
2. **Authority gaps in `compileOperations`.** Any actor can end any war or treaty, open a war between two other
   nations, sign a treaty that binds the player, build in the player's regions, or destroy a foreign unit
   without being at war. The save already shows one of these: Bangladesh's action created an *alliance* with
   India that India never agreed to (S1).
3. **War tolls are wrong for large countries.** Military deaths scale with national population, so a
   "border conflict" between India and Pakistan with **zero regions changing hands** killed 125,085 people in
   two months, eight times the seeded Russo-Ukrainian War's monthly toll (S5).
4. **The journal narrates things that did not happen.** When C++ drops an operation, the model's description is
   kept verbatim. Month 1 says India "established forward operating bases in the Punjab region"; nothing moved
   on the map. Month 2's Pakistan then reacts to "the occupation of Punjab". The player cannot see that an
   action failed, and later prompts read the fiction as history (S2).

Code-wise the logic is careful but the files are hard to read: one-statement-per-line is abandoned in about half
of the C++ (lines up to 930 characters, `main()` is 800 lines), and the domain model is untyped JSON. Section 6.

---

## 2. Correctness and simulation

### S1. Operations lack authority checks (bug, high)

`src/world.cpp`, `compileOperations`:

| Operation | Line | What is missing | Evidence |
|---|---|---|---|
| `resolve_conflict`, `resolve_commitment` | 415–419 | The acting actor is never checked against the parties. Slovenia can end the Russo-Ukrainian War. | Code only |
| `open_conflict`, `add_commitment` | 399–414 | The acting actor need not be among `actors`; other parties' consent is never required. | Save: event by **BD** created `k1-2` "Dhaka Security Pact", kind `alliance`, parties `[IN, BD]`. The player (IN) never agreed. |
| `build` | 429–431 | `builder` may differ from the acting actor; only control of the region is checked. An AI action can start construction for the player. | Code only |
| `set_entity_status` → `destroyed` | 458 | A non-owner within reach can destroy a foreign entity with no open conflict between them. | Code only |
| `set_conflict_intensity` | 425 | Correct (party check present). Use it as the template. | — |

Fix: in each branch require `ev` to be a party (`open_conflict`, `add_commitment`, both `resolve_*`), require
`builder == ev` or `createdHere.contains(builder)` in `build`, and require an open conflict between `ev` and
the entity owner before `destroyed`. For `add_commitment`, the other parties must consent: accept the treaty
only if every other party is (a) an AI nation whose own actions this month name the acting actor or the
treaty (cheap text check in `adjudicationInput` via the existing `mentioned()`), or (b) the player, and one of
the player's actions this month names that actor. Otherwise drop the operation with reason
"needs the other party's agreement"; the event stays as a proposal. Add tests to `tests/world_tests.cpp`
mirroring the existing "Only parties change a war's intensity" check (line 132).

### S2. Dropped operations leave a false narrative (design bug, high)

`src/world.cpp:494–497` keeps the model's `description` unchanged when operations are dropped and only records
`dropped_operations` for the audit. Commit `05c93dd` removed the visible failure reason on purpose, but the
result is worse than a clumsy sentence:

- Save, month 1, India "Invade pakistan": the model emitted `open_conflict` + `move_entity` (it tried to move
  a Pakistani city). The move was dropped. The event text still reads "deploys a massive ground force …
  establishing forward operating bases in the Punjab region". No region changed control.
- Month 2: Pakistan's action reads "if the occupation of Punjab continues beyond 30 days". The 4B model now
  reasons from an occupation that never existed, and it is in `related_history` for every later prompt.
- The UI shows nothing: the Events dialog has no "failed"/"partial" badge (only `blocked_by` and `breaches`).

Fix, in order of cost:
1. Mark the event `partial: true` when any operation was dropped and `failed: true` when all of them were;
   store the dropped reasons as today. Show a badge in the Events dialog and journal, and tell later prompts:
   in `Store::campaignContext` and `chronicleEvents`, append a short deterministic clause to `description` of
   partial/failed events ("(the military movement did not take place)") built from `dropped_operations[].action`,
   so the model reads what happened, not what was claimed. Players see the badge, not the reason.
2. Optional, better text: for events with dropped operations only, one extra tiny model call (no thinking,
   `maxLength` 200, ~3 s) that rewrites the description given the list of operations that did not happen.

### S3. Invasion intent produces the wrong operation (model quality, medium)

Same event: "Invade pakistan" → `open_conflict` + `move_entity(city)`. The prompt's example says
"Invade neighbor X -> occupy one or two of its regions within reach + open_conflict". The schema lets the
model pick `move_entity` on a city (the entity enumeration includes every entity in the relevant regions,
cities included, `src/world.cpp:184–186`). Fixes: exclude non-movable kinds (`city`, `port`, `base`,
`event_site`) from the `move_entity` entity enumeration; when the player's action matches the invasion stems
in `consequential()` (`src/main.cpp:93`), add the sentence "This action is an attack: use occupy on one or two
regions listed in within_reach_of" to that intention's `action` text so the small model sees it right next to
the action. Add this scenario to `tools/world_probe.cpp` (it currently has "conflict" with Spain but no
plain "invade X").

### S4. AI nations can act against the player's interests without the player being able to respond first

Not a bug, but note the asymmetry: the player's actions and every AI action are adjudicated in one call, and
AI nations see `player_actions` (`src/main.cpp:638`) before deciding. The player never sees AI intentions
before they resolve. That is fine for a monthly sandbox; document it in README "Play".
Partly addressed (plan 01): treaties an AI nation offers the player become proposals the player accepts or
declines the next month.

### S5. War tolls scale with national population (calibration bug, high)

`src/status.cpp:504–511`: military losses per party per month = `max(national population, 0.25 × front
population) × rate::military[intensity] × kind multiplier × jitter`. For India (1.47 bn) at high intensity
with the 0.4 border-conflict multiplier that is ~47,000 deaths a month **with no fighting on the map**.
Save: `c1-1` Punjab Border Conflict, kind `border_conflict`, 9 affected regions, zero control changes,
`casualties` 125,085 and `displaced` 1,245,946 after two months. The seeded high-intensity
Russo-Ukrainian War produced 16,075 and 15,125 the same months.

The calibration constants were tuned for Russia/Ukraine (140 M / 38 M) and Sudan and do not transfer to
1.5 bn-population states. Fix: make the recruitment pool front-based, not nation-based:
`pool = min(national population, K × front population)` with K around 0.5, or better, replace the population
term with a fixed per-front-region rate (e.g. deaths per affected region per month by intensity:
low 30 / medium 300 / high 1,500, times the kind multiplier, times jitter), which is what the front model
already implies for civilians. Re-check the two calibration tests in `tests/status_tests.cpp:176–190` still
pass for Russia–Ukraine and Sudan, and add an India–Pakistan border-conflict case asserting < 5,000 deaths a
month at high intensity with no seized regions.

Also: declared `set_intensity` alone drives `high`. The model chose `high` for a conflict in which nothing
happened. Consider capping effective intensity at `medium` in months with no seized regions and no destroyed
entities, unless the conflict has been open for ≥ 3 months (`src/status.cpp:454–458`).

### S6. Effects: the model rewards aggressors (known weakness, medium)

Both months record `IN economy growth` for starting a war (`docs/VERIFICATION.md` already notes this). C++
should override, not just cap: in `applyMonthlyStatus` step 2, force `economy ≤ none` for any actor that
opened or escalated a conflict this month (read `delta.changes` with kind `conflict`), and `stability ≤ none`
for parties of a conflict that turned `high`. Alternatively remove `effects` from the model output entirely
and derive them from operations (war → decline for parties; treaty → growth; construction → none;
narrative-only → none). That also shortens the answer (see P2).

### S7. Independent slots pick nations at random from the whole world (design, medium)

`src/politics.cpp:56–79` ranks all non-player actors by a hash. In the India save the independent actors
were Netherlands, Germany, Comoros, Slovenia, plus regional slots in Sri Lanka and Oman. Each costs a
chronicle (first time), a decision, and an adjudicated outcome, and produces flavour the player never sees
outside the Events dialog. Fix: weight the ranking toward relevance: neighbors of the player
(`neighbors` table), conflict/commitment parties of the player, and a small fixed set of great powers;
keep the hash only as a tiebreaker. Keep the two-month cooldown.

### S8. Chronicles: a source-less chronicle sticks for a year (bug, medium)

`src/main.cpp:599–609`: a chronicle is `due` only when missing or ≥ 12 months old. When the Wikipedia
reference is not cached yet (common in month 1, the preparation queue has just started), the chronicle is
written from nothing and saved with `with_source=false`. The check that would re-source it
(`!current.get("with_source")`) is inside `if(due…)`, so it waits 12 months. Evidence: India's saved
chronicle reads "While the specific details of past wars, treaties, and unresolved tensions are not detailed
in the provided reference data…" and "SCO … active for nearly 295 months". Fix: `due = missing || age ≥ 12 ||
(!with_source && background.history_source available)`. Also convert `months_in_force` (`src/status.cpp:302`)
to a start year for seeded agreements (`"since 2001"`), so prose stops saying "295 months".

### S9. No combat strength or cost model (design limitation, medium)

Reach and tempo are the only limits on force; whether an attack succeeds is the 4B model's judgment.
Construction has no cost. A deterministic gate would remove that burden from the model:
- `occupy`: drop unless attacker's `gdp_usd × (1 + active armies)` ≥ 0.15 × defender's, or the defender is in
  a civil war / stability < 25. Put the ratio in `within_reach_of` so the model sees "IN (strong)".
- `build`: cap concurrent `under_construction` per actor at `max(1, gdp_usd / 100 bn)`.
Both are start-of-month, deterministic, replay-safe. Document in EVOLVING_WORLD.md "Realistic pacing".

### S10. Smaller correctness notes

- `context()` (`src/simulation.cpp:206`) matches `event["target"] == a` for recent events, but `target` is an
  event ID for adjudicated events; the condition is dead. Harmless.
- `resolve()` turn cap `12000` is fine; `month()` in `web/app.js:45` handles any turn.
- `applyMonthlyStatus` refugees who cross a border never return (documented). Fine to leave.
- Co-belligerents in one conflict are treated as enemies of each other for fronts (documented limitation);
  becomes visible as soon as the model adds a third party to a war. Consider `sides` in the conflict record.
- `Store::commit` accepts a cause that points to an event in the same turn (inserted earlier in the loop).
  Acceptable, but `compileOperations` schema only offers `related_history` IDs as causes, so it cannot happen.

---

## 3. Performance

### Measured baseline (real save, `inference` table; llama.cpp log confirms)

| Month | Calls | Total | Chronicles | Decisions | Adjudication | Narration |
|---|---|---|---|---|---|---|
| 1 (10 intentions) | 10 | 159 s | 3 calls, 22.5 s | 5 calls, 25.6 s | 100.3 s (6,047 prompt tok, 2,675 generated) | 11.2 s |
| 2 (13 intentions) | 11 | 212 s | 3 calls, 36.4 s | 6 calls, 38.6 s | 123.0 s (12,420 prompt tok, 2,614 generated) | 14.1 s |

Adjudication month 2, by phase: prompt 37.9 s (328 tok/s), reasoning ≈ 950 tokens ≈ 31 s, answer ≈ 1,660
tokens ≈ 54 s (30.8 tok/s). `cached_tokens` was 0 on every call: no prefix reuse happened.

Where the 12,420 prompt tokens came from (bytes of the user message, month 2): actors 14.5 KB (7 actors ×
~2 KB: status 370 B, cities 340 B, `related_history` up to 1.9 KB, `past_events` up to 0.5 KB, commitments
up to 0.5 KB), territories 5.9 KB (37 regions), `reference_only` 12.9 KB (7 chronicles/excerpts), intentions
2.6 KB, system prompt 5.9 KB. The JSON schema sent with the request was **194 KB** (99 KB in month 1).

The per-request reasoning budget works: reasoning was 950–1,000 tokens against a 1,024 budget both months.

### Plan, ordered by savings per unit of work

| ID | Change | Where | Est. saving (month 2) |
|---|---|---|---|
| P1 | Fewer AI actions to adjudicate. Reactive nations: 1 action, 2 if in an open conflict with the player or named by the player's actions; independent national slots stay at 1. | `src/main.cpp:638` (`action_count=3`), `src/main.cpp:73` (`maxAiActors`) | −25 s (fewer answer tokens and shorter decisions) |
| P2 | Shrink each outcome. `description` maxLength 300 → 160; remove `significance` from the model schema (C++ already marks structural events major; keep the "declared major" cap by treating `effects` with `severe_decline`/`strong_growth` as declared); make `effects` optional (absent = none). | `src/world.cpp:227–239` | −15 s |
| P3 | Prompt diet for adjudication: `reference_only` only for the player, fighting nations and `grounded` nations (today every actor gets a 600-char excerpt); drop per-actor `entities` (cities) unless the actor is acting with a consequential action; cap `related_history` at 8 per actor and `past_events` at 4; only include territories that are targeted, bordering an addressed nation, or on a front of an acting nation (already mostly so, but tighten the `< 40` / `< 60` caps to 24 / 36). | `src/main.cpp:149–210` | −18 s (12.4 K → ~6 K tokens) |
| P4 | Reason only when needed: `reasoningBudget = 0` unless `consequential()` is true for at least one intention; otherwise `128 × consequential actions + 256`, clamp 384–768. | `src/providers.cpp:205`, reuse `consequential()` from `src/main.cpp:93` (move it to `world.cpp`) | −30 s on quiet months, −10 s otherwise |
| P5 | Chronicles: write them only for the player and for nations in an open conflict or ≤3-party commitment with the player; independent-slot nations use the raw excerpt. Fix S8 at the same time. | `src/main.cpp:589–635` | −20 to −35 s most months |
| P6 | Narration: `maxLength` 2000 → 900, and pass only `status` and the events (it already drops territories). Consider a "Skip AI narration" setting. | `src/providers.cpp:151–160` | −6 s |
| P7 | Two llama.cpp slots. Start with `--parallel 2 --ctx-size 65536` (the app checks `n_ctx` per slot ≥ 16,384 for thinking; Qwen3.5's hybrid attention keeps the KV cost low), replace the `inference` mutex with a counting semaphore of 2, and issue decision/chronicle calls two at a time (`std::async` over the actor list). Batched decode of two sequences on Metal gives ~1.7× aggregate throughput for a 4B model. | `src/providers.cpp:42`, `src/main.cpp:589–650`, `start.sh:553`, `start.ps1:107` | −40 % of decision + chronicle time (≈ −25 s). Measured 2026-09-18: no gain (two concurrent requests each run at half speed); not adopted, see `docs/MODELS.md` |
| P8 | Schema with `$defs`: define each operation once under `$defs` and reference it from every `prefixItems` entry with `$ref`. llama.cpp's schema-to-grammar resolves local `$ref`s. Cuts the request from 194 KB to ~20 KB, shortens grammar compilation, and the same body is also posted to `/apply-template` on every trim loop iteration. | `src/world.cpp:177–259` | small but real; also removes the 6 KB × N duplication that grows with actions |
| P9 | Fewer HTTP round trips per call: cache `/props` for 60 s; estimate tokens locally (bytes / 3.2) and only call `/apply-template` + `/tokenize` when the estimate is within 20 % of the limit. | `src/providers.cpp:62–100` | −3 to −8 s per month |
| P10 | Build type: `Makefile:7` and `start.sh:51` configure `Debug`. Use `RelWithDebInfo`. Not on the turn's critical path, but every API call re-parses the 1.1 MB campaign JSON (`decode(store.campaign())`), and `/events` and `/entities` are called on every map move. | `Makefile`, `start.sh`, `CMakePresets.json` already uses Release | UI latency |

Rough total: 212 s → ~90–110 s with P1–P6 and P8–P9; ~80 s with P7. Do P1–P4 first: they are small edits
and give most of the gain. Measure with `tools/living_world_probe.py` (3 months, empty orders) before and after.

### Prefix caching (P11, optional)

`cached_tokens: 0` on all 21 calls. The system prompt of adjudication (~1,400 tokens) is identical every
month but is evicted by the intervening decision/chronicle prompts even with `--cache-ram 1024`. If P7 is
done, dedicate slot 0 to adjudication and narration and slot 1 to decisions; then the adjudication system
prefix stays cached. Gain ~4 s per month. Low priority.

---

## 4. Model choice

Keep **Qwen3.5-4B Q4_K_M** as the default. Reasons: the game is output-bound (≈ 60 % of adjudication is
generation at 31 tok/s), so a 9B model at roughly half the speed would cost more than it returns unless the
output diet (P1–P2) lands first. Qwen3.6 exists as of mid-2026 but only in 27B and 35B-A3B sizes, both far
beyond an 8 GB budget (see `docs/MODELS.md` constraints), so it is not an option for this machine.

Recommended path:
1. Land P1–P4. Re-measure with the 4B.
2. *(Decided 2026-09-18: not pursued, the machine has no memory for a second model.)* Trying **Qwen3.5-9B Q4_K_M (6.6 GB)** for **adjudication only**, keeping the 4B for decisions,
   chronicles and narration. Two servers (ports 8081/8082) fit under ~9 GB of weights plus small KV caches;
   if memory is tight, use the 9B for everything and accept slower decisions. The schema and validation are
   unchanged, so this is a config-level experiment: add a second endpoint setting (`adjudication_endpoint`)
   with the same validation as `llama_endpoint`.
3. Judge with `tools/world_probe.cpp` extended by the "invade X" scenario (S3) and by an authority scenario
   (an AI nation trying to sign a treaty with the player). The current probe passes structurally valid but
   semantically wrong outputs (`move_entity` on a city) as "valid".

Model-facing prompt fixes that help any size:
- Put the list of allowed operations for *this* intention in the intention itself when `consequential()` is
  true (S3).
- `months_in_force` → start year (S8).
- In `decide`, the 4B wrote actions quoting stability scores and exact population ("for all 797,112
  citizens"). Add "Never quote the numbers in status; write like a news brief" to the decision instruction
  (`src/providers.cpp:125–128`).

Sources consulted: [Unsloth Qwen3.5](https://unsloth.ai/docs/models/qwen3.5),
[Unsloth Qwen3.6](https://unsloth.ai/docs/models/qwen3.6),
[llama.cpp per-request reasoning budget](https://jasonvsthenoise.com/repowatch/2026-07-13-llama-cpp-reasoning-budget-per-request/),
[llama.cpp server README](https://github.com/ggml-org/llama.cpp/blob/master/tools/server/README.md).

---

## 5. UI

Checked in headless Chromium against a copy of the real save at 1440×1000 and 390×844.

### Data and performance

- **U1. The browser downloads a 124 MB GeoJSON on every campaign page load.** `web/regions.geojson`
  (3,421 features, 4.5 M vertices) is fetched by `web/app.js:561`; Leaflet then builds 3,421 canvas paths.
  JS heap after load: **853 MB**. Switching to the Wars & treaties mode restyles every layer and took
  466 ms. On a 16 GB machine that also runs the model this is the single biggest UI cost. Fix: simplify the
  geometry for display in `tools/import_regions.py` (Douglas-Peucker at ~0.01°, keeping topology; target
  < 10 MB), keep the full-resolution geometry only in `data/world.sqlite` for `containsPoint`, and serve the
  file gzipped (Drogon supports gzip static files; also set a long `Cache-Control` since the URL is immutable).
  `web/world.geojson` (12 MB) has the same issue for the pre-campaign screen.
- **U2. `/api/v1/events?on_map=1` and `/api/v1/entities` run on every map move**, and `/campaign` returns
  1.1 MB. Debounce map-move requests (300 ms) and add an `If-None-Match` / turn number check so the client
  skips refetching when `turn` has not changed.

### Layout and interaction

- **U3. The model endpoint field sits in the page header** (`web/index.html:28–32`). It is a developer
  setting. Move it into a Settings dialog together with "Skip narration" (P6) and a link to the inference log.
- **U4. "Prepare campaign references" is created in JS and prepended into the actions panel**
  (`web/app.js:945–952`). Put it in the HTML, under "Cities & reference sources", where the reference lives.
- **U5. Failed or partial actions are invisible** (S2). Add `partial` / `failed` badges in `eventCard()`
  and the journal, and a "What changed on the map this month" line under each journal entry (regions that
  changed control, entities started/finished, wars opened/ended), built from `structured_events`. The
  authoritative-events list currently mixes the player's outcomes with five distant war tolls every month.
  Order it: player outcomes first, then nations, then a collapsed "War tolls (5)" group.
- **U6. The "Layers" control** is Leaflet's icon replaced by a CSS `::after` label (`web/style.css:683–684`);
  it renders as a bare white box with dark text on the dark map (visible in the screenshots). Replace it
  with a small custom control styled like the Political / Wars & treaties toggle, or drop it: the only
  overlays are Entities and Events, which could be two toggles in the same bar.
- **U7. Phone width (390 px).** The header wraps into three rows with "Playing as" split across lines and the
  endpoint input squeezed; the country list shows two entries; the map is 350 px tall; the side panel
  (stats, actions) is pushed below the journal, so writing an action needs a long scroll past the narration.
  Fixes: hide the endpoint field on small screens (or U3), collapse the country list into the search input
  with a dropdown, and move the actions panel above the journal on narrow layouts (`web/style.css:556–586`).
- **U8. No way to cancel a running month, and no time estimate.** `poll()` gives up after ~10 minutes
  (`web/app.js:375`) and each of up to 11 model calls has its own 240 s timeout, so a stuck month can run for
  much longer than the config suggests. Add a turn-level deadline in the worker (fail the job after
  `timeout_seconds × 2`) and a Cancel button that hits a `POST /api/v1/turns/cancel` which sets a flag the
  worker checks between model calls (the state has not changed until commit, so cancel is safe before it).
  Show elapsed time and the phase ("Deciding for Pakistan (3/6)") in the turn log; the data is already there.
- **U9. The live turn log is hidden when the month completes** (`web/app.js:380`). Keep it collapsible under
  the journal entry as "How the model decided", with a link to `/api/v1/inference?job=`. It is the only
  place a player can learn why something happened.
- **U10. Country list.** 242 entries including "Rapid Support Forces" with no flag and dependencies like
  American Samoa. Group: "Your neighbors", "At war / in treaties with you", "Everyone". Show the player's
  nation pinned at the top.
- **U11. Stats panel.** "conflicts · treaties 1 · 2" and "armies · fleets 0 · 0" as one cell each is
  cryptic. Use two cells or labels. "GDP per capita US$2,740" is good. Add "last month" deltas for population
  and GDP (both are in `status.last_gdp_change` / `last_population_change` already).
- **U12. Journal date.** Entries are titled by `month(entry.turn - 1)`; the header shows the *upcoming*
  month. Players read "March 2026" in the header and "February 2026" in the journal for the month they just
  played. Label journal entries "Month played: February 2026 → now March 2026" or show both.

Accessibility is reasonable (roles, `aria-live`, focus outlines). The `#turn-log` `aria-live` region will
read every model line aloud; make it `aria-live="off"` while a screen reader is active or throttle it.

---

## 6. Code quality

### C1. Formatting is inconsistent and half the code is unreadable (high)

`.clang-format` exists (LLVM, 100 columns) but is not applied. Longest lines: `providers.cpp` 930,
`main.cpp` 596, `status.cpp` 569, `store.cpp` 496, `world.cpp` 407 characters. Dense one-liners such as
`src/main.cpp:460` (flag route: nine statements, two `if`s and a file read on one line) and
`src/world.cpp:373` (a `for` with an inner `if` and four assignments on one line) are the norm in `world.cpp`,
`store.cpp`, the second half of `main.cpp`, and the test files. Run `clang-format -i` over `src include tests
tools` once, commit that separately, and add a `make format` / pre-commit check. Formatting alone will not
fix nesting depth; see C2–C4.

### C2. `main()` is 800 lines (high)

`src/main.cpp:239–833` holds config parsing, the file lock, reference loading, a reference-preparation
scheduler (lambda), all 13 routes (lambdas), and the whole turn pipeline as a lambda nested inside the
`/api/v1/turns` route (lines 584–689). Split into:
- `Server` (Drogon setup, routes; one small function per route),
- `TurnRunner` (build contexts → chronicles → decisions → resolve → adjudicate → apply → commit → narrate),
- `ReferencePreparer` (queue, dedupe, backoff),
- `WorldFactory` (the new-campaign construction at lines 422–445).
Give `TurnRunner` a unit test with the injected transport (the integration test already covers it end to end).

### C3. `compileOperations` is 280 lines with copy-based transactions (medium)

`src/world.cpp:278–562`: a per-operation snapshot of eight containers via `std::make_tuple(pending, index,
…)` and `std::tie(...) = before` on failure. Correct, but O(ops × state) copying and hard to follow. Refactor
into an `OperationCompiler` class with one method per operation (`occupy()`, `build()`, …), a `Pending`
store with `begin()/rollback()/commit()` (a journal of changed keys is enough), and `EventBuilder` for the
event record. Keep the behaviour identical and use the existing `world_tests.cpp` as the regression net.

### C4. Untyped domain model (medium)

The "typed" `WorldState` (`include/localhistoria/simulation.h:12`) is six `Json::Value` blobs; every access
is a string key (`t["controller"]`, `e["status"]=="active"`). Typos compile. Cheap mitigation without a
rewrite: a `keys.h` with `inline constexpr const char *kController = "controller"` etc., and small
accessors (`controllerOf(t)`, `isOpen(v)`, `isParty(v, actor)`). Also dedupe helpers that exist twice or
more: `involves` (`src/simulation.cpp:163`, `src/status.cpp:32`), `lower`/`lowered`
(`src/main.cpp:80`, `src/status.cpp:35`, `src/store.cpp:216`, `src/world.cpp:313`), `slug`, `near`.

### C5. `std::regex` constructed inside hot paths (low)

`std::regex` construction costs milliseconds. Built per call at `src/world.cpp:53` (every change in every
delta) and `:81`, `src/politics.cpp:120` (per colour), `src/history.cpp:94,100,109,135`, `src/main.cpp:460,
789`. Make them `static const`, or replace `[A-Z]{2}` / `#[0-9a-fA-F]{6}` / `Q[0-9]+` with tiny hand
checks (`isCountryCode()`, `isHexColor()`).

### C6. JSON `operator[]` on non-const values (low, worth a rule)

`Json::Value::operator[]` on a non-const value inserts a null member. `status.cpp:343` already guards with
const views. Elsewhere reads go through non-const copies (`auto w = decode(...)` then `w.actors[t]["name"]`
in `src/main.cpp:237`), which is harmless today only because inputs are validated first. Adopt the rule
"read through `const Json::Value&`", and add `-Wshadow` to the warning set.

### C7. Threading and lifetime (mostly fine)

- One `std::mutex mutex` (`src/main.cpp:304`) guards the store and all shared maps; network work happens
  outside it. Good.
- `Worker` drains queued turns on shutdown (`drain=true`), so Ctrl+C in `start.sh` kills llama.cpp and the
  drained turn then fails on transport; harmless but slow to exit. Use `drain=false` for the turn worker too,
  since `recover()` marks the job interrupted on the next start anyway.
- `httpJson` creates a new `HttpClient` per request (no keep-alive). Fine on loopback; note for P9.
- The file lock descriptor is intentionally never closed; add a comment.

### C8. Store and SQL (fine, two notes)

- Parameters are bound everywhere; the only concatenated fragment is the constant `Store::happened`. Good.
- `Store::journal()` runs one query per month (N+1) and `eventLog()` re-reads all months' documents on every
  call (`src/store.cpp:237–249`); both are called on every page load and map move. Add an
  `events(turn)` index (exists) and a single `JOIN`, and cache `eventLog` results per `turn` in memory.
- Schema migrations are `CREATE TABLE IF NOT EXISTS` only; there is no version table. Add
  `PRAGMA user_version` now, before the first incompatible change.

### C9. Tests (good coverage, two gaps)

The suites are thorough on validation, pacing and status. Gaps: the authority checks in S1 (add negative
tests per operation), the toll calibration for a large state (S5), and any test of `adjudicationInput`'s
prompt budget (assert an upper bound on the user-message size for a 13-intention month, so P3 does not
regress). `tests/integration.py:55` hardcodes `build/localhistoria`, so it cannot run on Windows
(`build/Release/localhistoria.exe`).

### C10. Miscellaneous

- `src/world.cpp:275` now uses a hand-written π; use `std::numbers::pi` (`<numbers>`, C++20).
- `src/main.cpp:426–428` derives actor colours from a string hash; several nations get near-identical hues
  (visible on the map). Use a fixed palette indexed by hash, with a neighbour-aware assignment at import time.
- `include/localhistoria/providers.h:34` default `timeout = 20` s while the app passes 240; drop the default.
- `docs/API.md` says `/countries` returns codes and names; it returns 242 actors including non-state ones
  during a campaign. Fine, but the UI shows them as countries (U10).

---

## 7. Windows commit (`ba07b84`) review

- **W1.** `start.ps1:48` commits a machine-specific default:
  `C:\Users\PC\Downloads\llama-b11017-bin-win-cuda-13.4-x64`. Default to PATH lookup only and document
  `LLAMA_DIR`; never ship a personal path.
- **W2.** `CMakeLists.txt` skips `-Wall -Wextra -Wpedantic` on MSVC but adds nothing. Add `/W4 /permissive-`
  for MSVC so both compilers warn.
- **W3.** The `_sopen_s` lock (`src/main.cpp:277`) is correct and releases on process exit. The file paths
  `web/flags/…` and `data/world.sqlite` are relative to the working directory; `start.ps1` does
  `Set-Location`, so this works, but the README's manual command must be run from the repo root (it says so).
- **W4.** `tests/integration.py` and `tools/living_world_probe.py` cannot find the Windows binary (C9).
- **W5.** `-dev CUDA0` default fails on CPU-only llama.cpp builds; documented, but `start.ps1` could probe
  `llama-server.exe --list-devices` and fall back to `none`.
- The commit message says a full turn was not verified on Windows. Run `python3 tests/integration.py`
  there once the path in W4 is fixed.

---

## 8. Documentation drift

- README: "a month takes roughly one to two minutes on a 4B model". Measured: 2.7 and 3.5 minutes with the
  default three independent slots and four reactive nations. Update after P1–P4.
- `docs/EVOLVING_WORLD.md` "Measured model results" predates independent slots (it says so); re-run
  `tools/living_world_probe.py` after the performance work and replace the table.
- `docs/MODELS.md` still recommends `--parallel 1`; update with P7. (Kept: P7 measured no gain, and
  `docs/MODELS.md` records why.)

---

## 9. Ordered task list for handoff

Each task is self-contained. "Done when" is the acceptance check.

1. **S1 authority checks** in `compileOperations`. Done when: new `world_tests.cpp` cases show a non-party
   cannot resolve, open, or sign; `build` with `actor != ev` is dropped; `destroyed` on a foreign entity
   needs an open conflict; existing suites pass.
2. **S5 toll calibration.** Done when: a new status test with a 1.5 bn-population party in a high-intensity
   border conflict with no seized regions yields < 5,000 deaths/month, and the Russia–Ukraine and Sudan
   bounds at `tests/status_tests.cpp:188–189` still hold.
3. **S2 partial/failed events.** Done when: events with dropped operations carry `partial`/`failed`, the
   Events dialog and journal show a badge, `campaignContext` and `chronicleEvents` append the deterministic
   clause, and `world_tests.cpp` asserts the flags.
4. **P1 + P2 + P4** (fewer AI actions, smaller outcome schema, conditional reasoning). Done when:
   `tests/integration.py` passes with the mock, `living_world_probe.py --months 3` shows a lower
   `seconds_to_commit`, and generated tokens per adjudication drop below ~1,000 for a 10-intention month.
5. **P3 prompt diet + P8 `$defs` schema.** Done when: a new context test asserts the adjudication user
   message for a 13-intention fixture is under 30 KB and the schema under 30 KB; llama.cpp accepts the
   `$ref` schema (check with one live `world_probe` run).
6. **P5 + S8 chronicles.** Done when: independent-slot nations get no chronicle call (assert on the mock's
   request list in `integration.py`), and a chronicle saved without a source is rewritten the first month
   the source is present.
7. **S3 invasion guidance + probe scenario.** Done when: `world_probe` has an "Invade X" case that checks
   `controller` changed on a reachable region in ≥ 2 of 3 runs.
8. **U1 geometry simplification + gzip + caching.** Done when: `regions.geojson` < 10 MB, page load heap
   < 250 MB, `containsPoint` still uses full geometry from SQLite, `regions_test.py` passes.
9. **U3/U4/U5/U6/U12 UI cleanups** (settings dialog, badges, "what changed" summary, layer toggle, dates).
   Done when: `tests/browser.mjs` is updated and passes.
10. **U8 cancel + turn deadline.** Done when: integration test cancels a slow mock turn, job ends `failed`
    with a clear error, state unchanged.
11. **C1 clang-format** (separate commit) then **C2 split `main.cpp`** and **C3 `OperationCompiler`**,
    behaviour-preserving; all suites pass unchanged.
12. **P7 two slots + semaphore**, then **model experiment** (9B for adjudication) with `world_probe` and
    `living_world_probe` numbers recorded in `docs/MODELS.md`.
13. **W1/W2/W4** Windows follow-ups; run the integration suite on Windows once.
14. **S7 relevance-weighted independent slots**, **S6 effect overrides**, **S9 strength/cost gates**
    (design changes; agree on the rules first, then implement with tests).
