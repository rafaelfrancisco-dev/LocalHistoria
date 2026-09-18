# 18 · Deterministic overrides of model effects

**Status: done (2026-09-18).** Implemented in `compileOperations` with per-event `warParties`, `tradeParties` and `seizedOwners`; live check recorded in `docs/VERIFICATION.md`.

Do plan 04 first (it makes `effects` optional).

## Goal
Stop the small model from rewarding aggression and punishing victims arbitrarily. C++ should force the sign of
economy/stability effects when the month's operations make it obvious.

## Evidence
- Save month 1: India `effects: economy growth, stability decline` for invading Pakistan; month 2 the same
  for demanding Bangladesh join the war. `docs/VERIFICATION.md` already notes "the small model tends to
  attach mild growth effects to routine events, including Spain's economy for being attacked".
- Effects are validated but not overridden: `src/world.cpp:535–544` (kept per event), consumed in
  `applyMonthlyStatus` step 2 (`src/status.cpp:375–389`) with per-actor caps.

## Steps
1. In `compileOperations`, after operations for an event are compiled (before line 535), compute for the
   event: `openedWar` (an `open_conflict` of kind `war`/`civil_war` accepted, or `set_conflict_intensity` to
   `high`), its parties; `signedTreaty` (`add_commitment` of kind other than `other`), its parties;
   `seized` (accepted `occupy`/`transfer_territory` not a cession). Then adjust `effects`:
   - For every party of `openedWar`: `economy = min(economy, "none")`; for the *attacked* parties (all parties
     except `ev`) `stability = min(stability, "decline")`; if the model listed no effect for a party, add
     `{economy: "decline", stability: "none"}` for the aggressor and `{decline, decline}` for the others.
   - For parties of `signedTreaty` with kind `trade_agreement`/`union`: `economy = max(economy, "growth")`.
   - For an owner whose region was seized this month: `stability = min(stability, "decline")`.
   Use an ordinal helper `int rank(effect)` over `severe_decline < decline < none < growth < strong_growth`.
   Keep the 3-effect cap (drop the weakest model-supplied effect when adding).
2. In `applyMonthlyStatus` (status.cpp), nothing changes; the caps still apply.
3. Instruction (`providers.cpp:192`): shorten the effects sentence to "effects: only nations clearly affected;
   the game corrects wars and treaties."
4. Docs: `docs/EVOLVING_WORLD.md` "Effects" bullet.

## Tests (`tests/world_tests.cpp`)
- The `named` case (line 124): India-like aggressor `old-state` with `economy: growth` on `open_conflict` →
  compiled event effect for `old-state` has `economy == "none"` or lower and `neighbor` gets `decline/decline`.
- A `trade_agreement` between two actors with no effects → both get `economy: growth`.

## Acceptance
Suites pass; docs updated.
