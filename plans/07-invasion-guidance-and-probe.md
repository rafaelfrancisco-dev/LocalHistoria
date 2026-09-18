# 07 · Make "Invade X" produce `occupy`, and prove it with the probe

**Status: done (2026-09-18).** `move_entity` enumerates only active armies and fleets; attack orders get an occupy hint via `attackGuidance`. Probe "invasion" matched 2/3 runs, "treaty_consent" 3/3 (expects the pact until plan 01 lands); table in `docs/EVOLVING_WORLD.md`.

## Goal
The 4B model should turn an invasion order into `occupy` on a reachable region (plus `open_conflict`), not
`move_entity` on a city. The probe should test this and fail on semantically wrong but valid output.

## Evidence
- Save, month 1: "Invade pakistan" → `open_conflict` + `move_entity` targeting a Pakistani city entity
  (dropped: "only its owner can move Gujranwala Division"). No region changed control.
- Schema: `operationSchema` (`src/world.cpp:184–186`) puts every entity in relevant regions, cities included,
  into the `move_entity`/`set_entity_status` entity enumeration (line 219–220).
- Instruction example exists (`src/providers.cpp:200`) but is far from the action text in a 6K-token prompt.
- `consequential()` stems (`src/main.cpp:93–104`) already detect "invad", "occup", "attack", "annex"…
- `tools/world_probe.cpp` scenarios (lines 9–16) have "conflict" (Spain occupies West) but no plain
  "Invade <country>"; its check for `conflict` is territory-based (line 36), which is right.

## Steps
1. `operationSchema`: build a second enumeration `movable` = entities whose `kind` is `army` or `fleet` and
   status `active`; use it for `move_entity.entity`. Keep the full `entity` enumeration for
   `set_entity_status`. If `movable` is empty, do not emit `move_entity` at all.
2. Per-intention guidance in `adjudicationInput` (`src/main.cpp:216–231`): when the action text matches the
   attack stems ("invad", "occup", "attack", "offensive", "conquer", "seize", "annex", "bombard"), append to
   `item["action"]` after the targets: " [attack: use occupy on one or two regions whose within_reach_of
   lists <actor>, plus open_conflict unless already at war; never move_entity on cities]". Keep it under 160
   chars; it is read once. When the actor has no reach into any listed region, append instead
   " [no region within reach: describe preparations, no occupy]".
3. Probe: add to `tools/world_probe.cpp` scenarios `{"invasion", {"PT", "Invade Spain."}}` with expected
   `s.territories["region-east"]["controller"] == "PT"` and one open conflict; and
   `{"treaty_consent", {"ES", "Sign a defence pact with Portugal."}}` expecting **no** commitment (PT's
   intention this month does not mention Spain; requires plan 01) — if plan 01 is not merged, expect the
   commitment and note it. Run three times with the model up; record the table in
   `docs/EVOLVING_WORLD.md` "Measured model results".
4. Unit test: `tests/world_tests.cpp` near line 52: with an `army` and a `city` entity present, assert
   `move_entity.entity.enum` contains only the army.

## Acceptance
Probe "invasion" scenario matches in ≥ 2 of 3 runs; unit test passes; docs table updated.
