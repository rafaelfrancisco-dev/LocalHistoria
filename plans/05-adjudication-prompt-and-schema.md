# 05 · Adjudication prompt diet, `$defs` schema, fewer HTTP round trips

## Goal
Halve the adjudication prompt (12,420 tokens in month 2 → ~6,000) and shrink the JSON schema from 194 KB to
~20 KB, without removing anything the model needs for the player's actions.

## Evidence (month 2 user message, bytes)
`facts.actors` 14,517 (7 actors: status ~370, cities `entities` ~340, `related_history` up to 1,948,
`past_events` up to 529, `commitments` up to 528 each); `facts.territories` 5,908 (37 regions);
`reference_only` 12,919 (all 7 actors got a chronicle or excerpt); `intentions` 2,629; system prompt 5,862.
Schema: 193,658 bytes; each of the 13 `prefixItems` repeats the full `operations.items.anyOf` list
(~12 KB) with the region/actor/entity enumerations.

## Code map
- `adjudicationInput`: `src/main.cpp:149–238`. Per-actor facts at 167–172, references at 179–180
  (`briefReference(c.reference, 600, grounded||fighting ? history : 0)`), territory caps `< 40` at 164 and
  `< 60` at 191, entities in relevant regions at 210–215.
- `context()` builds per-actor facts: `src/simulation.cpp:168–211`; `Store::campaignContext` adds
  `related_history` (LIMIT 20) at `src/store.cpp:174–176`.
- Schema: `operationSchema` `src/world.cpp:177–259`. Ops list `ops` built at 194–224; the event object at
  225–239; `prefixItems` loop at 244–251 copies `event` (with the whole `ops` array) per action.
- Transport loop: `src/providers.cpp:62–100` (`/props`, then loop `/apply-template` + `/tokenize`).
- Trim order when over budget: `providers.cpp:85–99`.

## Steps
1. References only where they matter (`main.cpp:179–180`): give `reference_only[a]` only when
   `a == w.player || fighting || grounded.contains(a)`; otherwise omit the key entirely (no 600-char excerpt).
   Keep 600 intro / 1200 chronicle for those.
2. Per-actor facts (`main.cpp:167–172`): for actors that are neither the player nor `grounded` nor
   `fighting`: remove `entities`, `regional_politics`, cap `related_history` to 4 and `past_events` to 2. For
   the others cap `related_history` to 8 and `past_events` to 4. Do the capping here, not in
   `campaignContext` (decisions still want 20).
3. Territories (`main.cpp:164, 191`): lower caps to 24 (addressed nations' border regions) and 36 (fronts).
   Only include `within_reach_of` when non-empty (omit the empty array).
4. Entities in relevant regions (`main.cpp:210–215`): skip `city` entities unless the region is targeted by an
   action or is a front region; keep armies, fleets, bases, ports, event sites.
5. Schema `$defs` (`world.cpp`): build `schema["$defs"]["op_<name>"] = <op object>` once for each operation
   and set `event["properties"]["operations"]["items"]["anyOf"]` to `[{"$ref":"#/$defs/op_build"}, …]`.
   Do the same for `flagDesignSchema()` (used twice) and the `effects` item. llama.cpp's
   `json_schema_to_grammar` resolves local `$ref`s (`#/$defs/...`). The regional override in
   `LocalProvider::adjudicate` (`providers.cpp:171–179`) currently finds the `regional_initiative` op by
   scanning `anyOf[*].properties.action.enum[0]`; change it to read `schema["$defs"]["op_regional_initiative"]`,
   copy it, set the `region` const, and assign the copy as `items` for that prefix item.
   Verify once against the live server: `curl -s 127.0.0.1:8081/v1/chat/completions` with a tiny `$ref`
   schema, or run `build/world_probe` for one scenario.
6. Fewer round trips (`providers.cpp:62–100`): cache the `/props` result in `LocalProvider` for 60 s (member
   `cachedProps`, `propsAt`; guarded by the `inference` mutex already held). Before the loop, estimate
   `tokens ≈ bytes(system + user) / 3.2 + 64`; if `estimate + max_tokens + 256 <= 0.8 × contextLimit`, skip
   `/apply-template` + `/tokenize` and record `prompt_tokens_estimated = true` in the audit record (the exact
   count is still available from `response.usage.prompt_tokens`; store that as `prompt_tokens` when present).
   Only when the estimate is close, run the exact loop as today.
7. Docs: `docs/MODELS.md` "Speed" and "Requests" (what each actor gets), `docs/EVOLVING_WORLD.md`
   "Adjacency … The adjudication prompt receives:" list.

## Tests
- New `tests/context_tests.cpp` case: build a fixture world with 7 actors, 13 intentions (one consequential
  for the player naming one nation, others routine), long `related_history` (20 items of 200 chars) and
  chronicles for all; call `adjudicationInput` (make it non-static and declare it in a new
  `include/localhistoria/turn.h`, or move it to `world.cpp`) and assert `dump(input).size() < 30000` and that
  `reference_only` has keys only for the player, the named nation and fighting nations.
- Schema: assert `dump(operationSchema(...)).size() < 30000` for the 13-action fixture and that every
  `prefixItems[i].properties.operations.items.anyOf[j]` is a `$ref`. Update
  `tests/world_tests.cpp:52` (`anyOf.size()==10`) accordingly and `tests/context_tests.cpp:30–31`
  (regional schema check reads through `$defs` now).
- `tests/core_tests.cpp` transport fixtures return `/props` every time; with caching they are called once per
  provider; adjust any counting expectations (none exist today, but the `context_tests.cpp:10` tokenizer
  fixture relies on `/tokenize` being called → make its prompt large enough to force the exact path, or set
  the estimate threshold via a constructor parameter for tests).

## Acceptance
Suites pass; one live adjudication accepted by llama.cpp with the `$ref` schema; month-2-sized prompt under
~6,500 tokens in `tools/living_world_probe.py` output (`prompt_tokens` per record).
