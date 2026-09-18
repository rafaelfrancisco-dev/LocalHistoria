# 13 · Refactor `compileOperations` into an `OperationCompiler`

**Status: done (2026-09-18).** `compileOperations` (grown to ~1,290 lines after plans 18/19) is now a wrapper around `OperationCompiler` in an anonymous namespace in `src/world.cpp`. It has one method per operation (territory and entity operations split into prologue plus branch), a `PendingStore` with nested `open()/commit()/rollback()` frames that save only touched entries, and `EventScope`/`OpScope` for per-event and per-operation state. Scalars, aliases, names and the event's touched sets are still snapshots. The longest method is 70 lines (`settleEffects`). `applyDelta` and `operationSchema` are still over 80 lines and out of scope. A temporary dump hook showed all 90 compiled deltas from the unit suites byte-identical before and after. New `world_tests` case: rollback restores a change an earlier operation made (checked by mutating the restore).

Do plans 11, 01 and 03 first (they edit this function; refactor once they are in).

## Goal
`compileOperations` (`src/world.cpp:278–562`) is ~280 lines with nested try/catch, a per-operation snapshot of
eight containers (`std::make_tuple(pending, index, created, newActors, taken, aliases, names, actors, regions,
entities, createdHere, conflicts, commitments)` and `std::tie(...) = before` on failure), and one branch per
operation. Make it a class with one method per operation and a journaled pending store, with identical output.

## Design
```cpp
class OperationCompiler {
  const WorldState &w; const Adjacency &adjacency;
  PendingStore pending;          // kind:id -> {before, after, event, rationale}; begin()/rollback()/commit()
  std::map<std::string,std::string> aliases; std::set<std::string> names; int created=0, newActors=0;
  std::map<std::string,std::set<std::string>> taken;
  struct EventScope { std::string id, actor, regional; std::set<std::string> actors, regions, entities, createdHere, conflicts, commitments; bool structural=false; Json::Value dropped; };
public:
  WorldDelta compile(const Json::Value &response);
private:
  Json::Value compileEvent(const Json::Value &e);        // the loop body 315–549
  void apply(EventScope&, const Json::Value &op);       // dispatch by action
  void opRegional(EventScope&, const Json::Value&), opChangeIdentity(...), opCreateActor(...), opAgreement(...),
       opResolve(...), opIntensity(...), opBuild(...), opEntity(...), opTerritory(...);
  void renewEventSites(EventScope&, const Json::Value &causes);  // 509–518
  void finishRegionalHistory(EventScope&);                        // 519–526
  Json::Value buildEvent(const EventScope&, const Json::Value &e, ...);  // 527–549
};
```
`PendingStore::begin()` records the current set of keys and a copy of touched entries lazily (copy-on-write
per key inside `touch()` after `begin()`), so `rollback()` restores only what an operation touched instead of
copying every container. Scalars (`created`, `newActors`) and small sets (`taken`, `aliases`, `names`) can
stay snapshot-based inside `begin()`.

## Steps
1. Add the class in `src/world.cpp` (keep `compileOperations` as a thin wrapper with the same signature so
   headers and tests do not change).
2. Move each `if(name=="…")` branch into its method verbatim, then remove the `std::tie` restore in favour of
   `pending.rollback()` + restoring the scalar snapshot.
3. Keep `Infeasible` and every message string identical (tests match text, e.g. `tests/world_tests.cpp:75`
   expects `text=="Deep strike"` and `dropped_operations` present).
4. Run `make test`; the world, politics and context suites are the regression net. Then run
   `python3 tests/integration.py` and `./build/replay_verify` on the export the integration test writes
   (it does this already at line 125).
5. Delete the wrapper only if nothing else calls `compileOperations` (providers.cpp does; keep it).

## Acceptance
No test change needed (other than includes if the class is exposed); `world.cpp` has no function longer than
80 lines; replay of the integration export still verifies.
