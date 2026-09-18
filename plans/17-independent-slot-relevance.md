# 17 · Relevance-weighted independent slots

**Status: done (2026-09-18).** Revised after a first live run scored 0/12: Portugal's only neighbour, Spain, is its reactive neighbour every month (independent slots rank reactive actors last), and its only pacts, NATO and the EU, exceeded the six-party tier. The two national slots now have roles. `scheduleInitiatives(w, reactive, closeness, powers)`: the first ranks `(recent, reactive, -closeness, hash, id)` with closeness 3 neighbour (and the player, for regions), 2 war or ≤6-party pact partner, 1 fellow member of a larger bloc; the second prefers `powers`, the top-12 `gdp_usd` actors cached per campaign in `main.cpp` `relevance()`, except every third month when it prefers nations neither close nor powerful. Governors use closeness ≥ 2; `single` (whole-country) territories no longer take regional slots. Live `living_world_probe --months 6` as Portugal: 6/6 complete (56–120 s), replay verified, **7/12** national actors neighbours or partners (GB CA MT DE BG FI EE), plus great powers RU BR IN and distant picks SC PK; regional slots PT ×2, KZ, TW, ZA, LU.

## Goal
The two independent national slots and one regional slot per month should mostly involve nations the player
can see and care about, not a uniform sample of 241 actors.

## Evidence
- Save (India campaign): independent actors were Netherlands, Germany, Comoros, Slovenia; regional slots in
  Sri Lanka and Oman. Each costs a decision call and an adjudicated outcome (and, before plan 06, a chronicle).
- `src/politics.cpp:42–105` `scheduleInitiatives`: ranks all non-player actors by
  `(recent, reactive, stableHash(seed+scope+id), id)`; governors for regional slots the same way. It is
  deterministic per `(campaignId, turn)` and frozen in `monthly_allocations` (`src/main.cpp:554–556`), so
  changing the ranking only affects future months. `prepareReferences` (`src/main.cpp:318`) also calls it to
  prefetch references for upcoming actors.
- `WorldState` has no neighbour data; the `neighbors` table is in `data/world.sqlite` (used at
  `src/main.cpp:547`). `Adjacency` (region graph) is available in `main.cpp`.

## Steps
1. Extend the signature: `scheduleInitiatives(const WorldState&, const std::set<std::string> &reactive,
   const std::map<std::string,int> &weight = {})` where `weight[actor]` is a small integer tier
   (0 = default, 1 = great power / large economy, 2 = treaty or conflict partner of the player, 3 =
   neighbour). Rank by `(recent, reactive, -weight, hash, id)`; pass `weight` for the region scheduler too
   (governor ranking). Keep deterministic tie-breaking by hash. Do not exclude tier 0 entirely: to keep the
   "living world" flavour, make the second national slot ignore `weight` every third month
   (`(turn + 1) % 3 == 0`) so distant nations still act sometimes.
2. Compute `weight` in `main.cpp` (both call sites) from: `neighbors` table rows for the player (tier 3);
   open conflicts/≤6-party commitments with the player (tier 2); top 12 actors by `nationalStatus(...).gdp_usd`
   — compute once per campaign load from `totals()` (expose `allNationalStatus` is already available; pick the
   top 12 `gdp_usd`) (tier 1).
3. Regional slots: prefer regions of the player and of tier ≥ 2 nations (same weight map by controller).
4. Tests (`tests/politics_tests.cpp`): with weights `{ES:3}` and `PT` as player, the first national slot is
   `ES` unless recent; determinism check unchanged; the every-third-month rule yields a tier-0 actor when the
   pool has one. Existing "Country balanced regional sampling" test (lines 69–84) passes with an empty map.
5. Docs: README "A living political world", `docs/ARCHITECTURE.md` "Independent politics".

## Acceptance
Suites pass; over a 6-month `tools/living_world_probe.py` run as Portugal, ≥ 4 of the 12 independent national
actors are neighbours or partners of Portugal.
