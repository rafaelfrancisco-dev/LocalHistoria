# 06 · Chronicles: fewer, and re-sourced when the reference arrives

**Status: done (2026-09-18).** Chronicles only for the player and reactive nations in an open conflict or ≤3-party open commitment with it (`chronicleNations` in the turn route); `chronicleDue` and `campaignDate` (negative turns fixed) in `status.cpp`; a source-less chronicle is rebuilt from the source with all campaign events once `history_source` exists; `describeAgreement` and national wars give `since` instead of month counts; chronicle prompt asks for years. Tests: core (`campaignDate`), status (`chronicleDue`, `since`), integration (Mock chronicle branch, no chronicle for independent-slot actors).

## Goal
Stop spending 22–36 s a month on chronicles for nations that do not matter, and fix the bug that keeps a
source-less chronicle for twelve months.

## Evidence
- Save: month 1 wrote chronicles for IN, BD, PK (7.7 + 8.4 + 6.4 s); month 2 for BT, KM, SI
  (5.3 + 15.8 + 15.3 s; KM/SI prompts were 3,300 tokens of `history_source`). Comoros and Slovenia were
  one-off independent actors in an India campaign.
- India's chronicle was written before its Wikipedia reference was cached (prompt 391 tokens, no source) and
  reads "While the specific details of past wars, treaties, and unresolved tensions are not detailed in the
  provided reference data…" and "SCO … active for nearly 295 months". It is saved with `with_source=false`.
- Code: `src/main.cpp:589–635`. `due` at line 599 = missing or ≥ 12 months. The re-source condition at line
  609 (`first || !with_source`) is *inside* `if (due …)`, so it never triggers before month 12.
  `nations` list at 589–591 includes the player, reactive actors and independent national slots.
  `maxChroniclesPerTurn = 3` at line 76.
- `months_in_force` comes from `describeAgreement` (`src/status.cpp:302`); for seeded treaties it is a large
  number that the model turns into prose.

## Steps
1. `due` (line 599): `bool sourceless = current["summary"].isString() && !current.get("with_source", false).asBool();`
   `bool due = !current["summary"].isString() || age >= chronicleInterval || (sourceless && background["history_source"].isString());`
   Read `background` before computing `due` (move line 607 up). When `sourceless` and re-sourcing, treat it as
   `first = true` (rebuild from the source, ignore the previous junk chronicle) and pass
   `campaign_events` from turn 0.
2. Who gets a chronicle: build `nations` as the player plus reactive actors that are in an open conflict or a
   ≤3-party open commitment with the player (reuse the `engaged` lambda from the turn route, line 544). Do
   **not** include independent national slots or reactive actors merely named by the player; they keep the
   raw excerpt (`briefReference` handles both). Keep `maxChroniclesPerTurn = 3`.
3. `describeAgreement` (`status.cpp:302`): replace `months_in_force` with `since` = campaign date string of
   `started_turn` via a shared `campaignDate(int turn)` (move it from `main.cpp:72–76` to `status.cpp`,
   declare in `status.h`; it already handles negative turns? `turn % 12` is negative for negative turns:
   fix with `((turn % 12) + 12) % 12` and `2026 + floor(turn / 12.0)`). Also use it in `buildStatus` wars
   (`status.cpp:122` `months` → `since`).
4. Chronicle instruction (`providers.cpp:140–143`): add "Write years, not month counts."
5. Docs: `docs/ARCHITECTURE.md` chronicle paragraph (who, when, re-sourcing), `docs/DATA.md` Chronicles row.

## Tests
- `tests/integration.py`: the Mock answers chronicle requests generically (`else` branch at line 43 returns
  `actions`; a chronicle schema expects `summary` → currently the chronicle call fails and is swallowed).
  Add a Mock branch: if `'previous_chronicle' in raw or 'campaign_events' in raw` → `{'summary': 'Fixture chronicle'}`.
  Then assert on `Mock.requests`: no chronicle request whose `facts.actor_name` belongs to an
  independent-slot actor (compare with `export['turns'][0]['reference']['allocations']`).
- New `tests/core_tests.cpp` check for `campaignDate(-3) == "October 2025"` and `campaignDate(13) == "February 2027"`.
- Re-sourcing: in `tests/status_tests.cpp` or a small new test, exercise the `due` rule by extracting it into a
  pure function `bool chronicleDue(const Json::Value &current, int turn, bool sourceAvailable)` in
  `status.cpp` (or `world.cpp`) and unit-test the three branches.

## Acceptance
Suites pass; a month with only routine independent actors makes ≤ 1 chronicle call; a chronicle written without
a source is rewritten the first month the source exists.
