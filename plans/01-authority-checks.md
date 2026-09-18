# 01 · Authority checks in `compileOperations`

## Goal
No actor can change a war, treaty, construction or foreign unit it has no authority over. Today any acting
actor can end any war or treaty, open a war between two other nations, sign a treaty binding the player,
build in the player's regions, or destroy a reachable foreign entity without being at war.

## Evidence
- `saves` (India campaign, month 1): Bangladesh's action "Foreign ministers from India and regional partners
  meet in Dhaka…" produced `add_commitment` → commitment `k1-2` "Dhaka Security Pact", kind `alliance`,
  parties `[IN, BD]`. The player (IN) never agreed.
- Code: `src/world.cpp` inside `compileOperations` (function starts at line 278):
  - `open_conflict` / `add_commitment`: lines 399–414. `ev` (the acting actor) is not required to be in
    `o["actors"]`; no consent from other parties.
  - `resolve_conflict` / `resolve_commitment`: lines 415–419. No party check at all.
  - `build`: lines 428–440. `builder = party("actor")` may differ from `ev`; only region control is checked.
  - `set_entity_status`: lines 454–460. A non-owner within reach may set `destroyed`/`inactive`.
  - `set_conflict_intensity`: lines 420–427 has the correct pattern (`std::find(parties…, Json::Value(ev))`).

## Steps
1. In `open_conflict`/`add_commitment` (line ~405, after `members` is built): if `ev` is not in `members`
   and `!createdHere.contains(ev)`, `throw Infeasible("only a party can open or sign this")`.
2. Consent for `add_commitment`. Build once, at the top of `compileOperations`, a map
   `consenting[actor] = set<actor>` from `response["events"]`: for every non-background event `e` with actor A
   whose `description` contains (case-insensitive, whole word, reuse the `lowered` lambda at line 313) the
   *name* of actor B (`w.actors[B]["name"]`), add B to `consenting[A]`. Then when compiling `add_commitment`
   by `ev`, require for every other member M: `consenting[M].contains(ev)` (M's own outcome this month names
   `ev`) OR `createdHere.contains(M)`. Otherwise `throw Infeasible("needs agreement from <name of M>")`.
   The event keeps its text as a proposal (plan 03 marks it partial). Note `response["events"]` is the
   model's answer: its descriptions are what the parties "said" this month, which is the right consent signal
   and needs no extra model call. Do not require consent for `open_conflict` (wars are unilateral).
3. `resolve_conflict`/`resolve_commitment` (line 417): after `collection.isMember(cid)`, require `ev` is in
   `collection[cid]["actors"]`, else `throw Infeasible("only a party can end " + displayName(w, kind, cid))`.
   Exception: a commitment of kind `ceasefire`/`peace_treaty` may also be resolved by a party of a conflict
   between the same parties (keeps "the war resumes" possible); skip this exception if it complicates things.
4. `build` (line 429): after `builder` is resolved, require `builder == ev || createdHere.contains(builder)`,
   else `throw Infeasible("can only build for itself")`. Keep `event_site` exempt from control but not from
   this rule.
5. `set_entity_status` (line 458): when `ev != owner` and `status == "destroyed"`, additionally require an
   open conflict in `w.conflicts` whose `actors` contain both `ev` and `owner`
   (`status == "open"`), else `throw Infeasible("not at war with " + w.actors[owner]["name"])`. Keep the
   existing reach check.
6. Update the adjudication instruction text in `src/providers.cpp` (lines 180–201) with one sentence:
   "Only a party can open, escalate, end or sign; a treaty needs every party's own action this month to
   mention the partner; nations only build for themselves." Keep it short (tokens cost time).
7. Docs: add a "Authority" bullet list under "Realistic pacing" in `docs/EVOLVING_WORLD.md`.

## Tests (`tests/world_tests.cpp`, follow the style at lines 131–132 "Only parties change a war's intensity")
- Non-party `far` resolving `c1-1` → dropped (`dropped_operations` size 1, no change).
- `far` opening a conflict between `old-state` and `neighbor` → dropped.
- `neighbor` adding a commitment with `old-state` while `old-state`'s outcome does not mention Neighbor →
  dropped; with an `old-state` event whose description contains "Neighbor" → accepted.
- `neighbor` `build` with `actor: old-state` in `west` → dropped.
- `neighbor` `set_entity_status destroyed` on `army-1` (owner `old-state`, reachable) with no conflict →
  dropped; with an open conflict between them → accepted.
- Existing expectations unchanged: run `make test` and `python3 tests/integration.py`. The integration mock
  (`tests/integration.py:36`) opens a PT–ES war from PT's action; still valid.

## Acceptance
All new checks pass, existing suites pass, `docs/EVOLVING_WORLD.md` documents the rules.
