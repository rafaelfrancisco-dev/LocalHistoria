# 14 · Typed keys, shared helpers and small C++ fixes

Do plan 11 first.

## Goal
Reduce stringly-typed risk and remove small pitfalls without changing behaviour.

## Items
1. **Key constants and accessors.** New `include/localhistoria/keys.h`:
   `namespace lh::key { inline constexpr const char *owner="owner", *controller="controller", *status="status",
   *actors="actors", *kind="kind", *region="region", *name="name", …; }` plus `inline bool isOpen(const
   Json::Value &v)`, `inline bool isParty(const Json::Value &v, const std::string &a)`, `inline std::string
   controllerOf(const Json::Value &t)`. Replace the duplicated helpers: `involves` (`src/simulation.cpp:163`,
   `src/status.cpp:32`) → `isParty`; `lower`/`lowered` (`src/main.cpp:80`, `src/status.cpp:35`,
   `src/store.cpp:216`, `src/world.cpp:313`) → one `lh::lowercase()` in `simulation.h`; the two `near`
   lambdas in `world.cpp` (267, 296) → `adjacent(adjacency, a, b)`. Do the replacement mechanically; do not
   convert every `["…"]` literal at once — start with `owner/controller/status/actors/kind/region`.
2. **Regex in hot paths.** Make `static const std::regex` or replace with hand checks:
   `src/world.cpp:53` (per change), `:81`; `src/politics.cpp:120`; `src/history.cpp:94,100,109,135`;
   `src/main.cpp:460,789`. Provide `bool isCountryCode(const std::string&)` (`[A-Z]{2}`),
   `bool isHexColor(...)`, `bool isEntityId(...)` (`Q[0-9]+`), `bool isPermanentId(...)` in `simulation.h`.
3. **Const reads.** Audit non-const `Json::Value::operator[]` reads that may insert null members:
   `src/main.cpp` turn route `w.actors[t]["name"]` (targets loop, line ~237 in `adjudicationInput`) and the
   flag route. Read through `const Json::Value &` (as `status.cpp:343` does). Add `-Wshadow` to
   `target_compile_options` in `CMakeLists.txt` and fix what it reports.
4. **`std::numbers::pi`** in `src/world.cpp:275` (`#include <numbers>`).
5. **Worker drain.** `Worker worker;` (`src/main.cpp:316`) defaults to `drain=true`, so a queued month runs to
   completion after Ctrl+C; use `Worker worker(false)`; `Store::recover()` marks it interrupted on restart.
   Add a comment on the never-closed lock descriptor (line 271–283).
6. **Provider default timeout.** `include/localhistoria/providers.h:34` `timeout = 20`; remove the default
   (callers pass it) or make it 240 to match the config default.
7. **Store.** `Store::journal()` N+1 (`src/store.cpp:165`): one query joining `events` by turn, group in C++.
   `eventLog()` (`src/store.cpp:210–330`) re-reads every month's documents per call: cache the computed
   `items` per `w.turn` in a member (`std::optional<std::pair<int, Json::Value>>`), invalidated when `turn`
   differs (filters are applied after the cache). Add `PRAGMA user_version = 1` in the constructor and a
   `migrate()` stub that switches on the version, so future schema changes have a home.
8. **Actor colours.** `src/main.cpp:426–428` hash → hue gives near-identical colours to neighbours. Replace
   with a 24-colour qualitative palette indexed by hash, then a greedy pass over the `neighbors` table that
   re-picks a colour when a neighbour already has it. Store the palette in `world.cpp` so `create_actor`
   fallbacks can use it too (today `str("color")` comes from the model).
9. **Docs.** `docs/ARCHITECTURE.md`: mention `keys.h` and the const-read rule.

## Tests
Existing suites; add checks for `isCountryCode`, `isHexColor`, `isPermanentId`, `lowercase` in
`tests/core_tests.cpp`; `journal()` output equality before/after (dump compare in `tests/world_tests.cpp`
store section).

## Acceptance
Builds with `-Wall -Wextra -Wpedantic -Wshadow` clean; suites pass; `grep -n "std::regex(" src` shows only
static definitions.
