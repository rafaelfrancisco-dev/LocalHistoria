# 04 · Turn output diet: fewer AI actions, smaller outcomes, reasoning only when needed

## Goal
Cut the tokens the model must generate per month. Generation runs at ~31 tok/s and is ~60 % of adjudication
time; month 2 generated ~2,650 tokens (950 reasoning + ~1,660 answer) for 13 outcomes.

## Evidence (real save)
- Month 2 intentions: 13 = player 1 + reactive BD 3 + BT 3 + PK 3 + independent KM 1 + OM 1 + SI 1.
  Only PK (at war with the player) and BD (named by the player) mattered.
- Answer JSON was 6,248 chars for 13 events: each event repeats `action` opening, `actor`, `blocked_by`,
  `causes`, `description` (≤300 chars, the model fills them), `effects`, `operations`, `significance`.
- Reasoning: 950–1,000 tokens against a 1,024 budget (budget works). Month 2 had no consequential player
  action ("Bring Bangladesh into the war" matches the `war` word, so it would still reason under the new rule).

## Code map
- Reactive action count: `src/main.cpp:638` (`c.facts["action_count"]=3`). Reactive set built at
  `src/main.cpp:540–552` (`candidates`, `maxAiActors = 4` at line 73). `engaged` lambda at line 544.
- Decision schema: `src/providers.cpp:120–123` (`maxItems` from `action_count`).
- Outcome schema: `src/world.cpp:225–259` (`event` object; `string(300)` at line 230; `significance` line 233;
  `effects` lines 235–237).
- Significance handling: `src/world.cpp:319` (background filter), `:547–554` (structural + declared cap).
- Reasoning budget: `src/providers.cpp:205` `clamp(256 + 128 × actions, 384, 1024)`; retry without thinking
  at line 209. `consequential()` heuristic lives in `src/main.cpp:93–104` (static).
- Answer reserve: `src/providers.cpp:70` `clamp(768 + 256 × outcomes, 2048, 6144)`.

## Steps
1. Reactive action counts (`src/main.cpp` turn route). Compute per reactive actor `a`:
   `count = 1; if (mentions.actors.contains(a)) count = 2; if (in an open conflict with the player) count = 2;`
   (use the `engaged` lambda on `w.conflicts`). Set `c.facts["action_count"] = count`. Keep the schema
   enforcing `maxItems = count` (already does). Update `docs/EVOLVING_WORLD.md` step 1 and README "Play" 3.
2. Outcome schema (`src/world.cpp`):
   - `description` → `string(160)`. Add to the instruction (providers.cpp:182): "description: one sentence,
     under 160 characters."
   - Remove `significance` from `event["required"]` and `properties`. Replace the declared-major logic
     (`world.cpp:548`): an event is "declared major" when any effect is `severe_decline`/`strong_growth`;
     keep the cap of 3 (lines 551–554). Delete the `significance` mention from the instruction (line 191) and
     from `world_events` handling (line 319: keep world events whose effects are non-empty; drop the rest).
   - Make `effects` optional: remove from `required`; treat absent as `[]`. Instruction already says "Most
     events have effects []"; change to "omit effects unless something changes".
   - Keep `action`, `actor`, `blocked_by`, `causes`, `operations` required (alignment and grounding).
3. Reasoning only when needed (`src/providers.cpp::adjudicate`): move `consequential()` from `main.cpp` into
   `world.cpp` (declare `bool consequential(const std::string &text)` in `world.h`; keep the stems list).
   In `adjudicate`, count intentions whose `action` is consequential: `n`. If `n == 0` → budget 0 (no
   thinking, no retry needed). Else budget `clamp(256 + 128 × n, 384, 768)`. `main.cpp` keeps calling the
   moved function.
4. Answer reserve (`providers.cpp:70`): with 160-char descriptions, `768 + 160 × outcomes` clamped to
   1536–4096 is enough. Keep `+256` safety in the fit check (line 84).
5. Tests: `tests/core_tests.cpp:99–100` checks `max_tokens == 2048 + 384` for a 1-action "Wait" fixture: the
   fixture action "Wait" is not consequential, so thinking is now off → update the expectation
   (`enable_thinking == false`, `max_tokens == 1536` or whatever the new clamp gives) and add a second fixture
   with action "Invade Spain" that expects thinking on when `ctx == 32768`. `tests/integration.py:144`
   accepts `768` or `2048..6144`; widen to the new range. `tests/world_tests.cpp:123` checks
   `required.size()==8` → 6. Mock in `integration.py:32` sets `significance`; harmless with
   `additionalProperties:false`? No: the mock *returns* extra fields and `compileOperations` only reads known
   keys; `parse()` of the model content does not reject extra keys (only `parseDelta` shape checks do). Verify
   by running; remove `significance` from the mock anyway.
6. Docs: `docs/MODELS.md` "Thinking" and "Requests" sections; `docs/EVOLVING_WORLD.md` "Significance"
   paragraph (major = structural or strong effect, capped at three).

## Acceptance
`make test` + `python3 tests/integration.py` pass. `tools/living_world_probe.py --months 2` (needs the model
running) shows fewer generated tokens per adjudication (< 1,000 for a 10-intention month) and a shorter
`seconds_to_commit` than the 159 s / 212 s baseline. Record numbers in `docs/EVOLVING_WORLD.md`.
