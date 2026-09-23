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
// The one rule for construction when control changes, whatever changed it (an occupation, a front
// advance, a treaty, a rebellion): a project whose region its builder no longer controls is paused
// with its remaining months, keeping its builder and any reserved troops or ships; it resumes when
// the builder holds the region again. Realism lifecycle only; run wherever control may have moved.
void reconcileProjects(WorldState &, std::vector<ResolvedEvent> &events);
Json::Value lifecycleReference(const WorldState &);
// Both functions operate on copies. Only Store::commit publishes migration or monthly work.
Resolution beginTurn(const WorldState &, const Json::Value &reference);
Resolution finishTurn(const Resolution &, const WorldDelta &, const Adjacency &,
                      const Json::Value &geography, const Json::Value &reference);
} // namespace lh
