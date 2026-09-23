#pragma once
#include "status.h"
namespace lh {
// Version 1 is reserved for the unpublished Plan 26 release. Missing metadata always
// selects the historical lifecycle, independently of the monthly status version.
constexpr int legacyLifecycleVersion = 0, realismLifecycleVersion = 1;
struct LifecycleRules {
    int lifecycle = legacyLifecycleVersion;
    int monthly = 0;
};
LifecycleRules lifecycleRules(const Json::Value &reference);
Json::Value lifecycleReference(const WorldState &);
// Both functions operate on copies. Only Store::commit publishes migration or monthly work.
Resolution beginTurn(const WorldState &, const Json::Value &reference);
Resolution finishTurn(const Resolution &, const WorldDelta &, const Adjacency &,
                      const Json::Value &geography, const Json::Value &reference);
} // namespace lh
