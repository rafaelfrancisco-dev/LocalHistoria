#pragma once
#include "status.h"
namespace lh {
// Version 1 is reserved for the unpublished Plan 26 release. Missing metadata always
// selects the historical lifecycle, independently of the monthly status version.
constexpr int legacyLifecycleVersion = 0, realismLifecycleVersion = 1;
struct LifecycleRules {
    int lifecycle = legacyLifecycleVersion;
    int monthly = 0;
    int consequences = 0, projects = 0;
};
LifecycleRules lifecycleRules(const Json::Value &reference);
// The one rule for construction when control changes, whatever changed it (an occupation, a front
// advance, a treaty, a rebellion): a project whose region its builder no longer controls is paused
// with its remaining months, keeping its builder and any reserved troops or ships; it resumes when
// the builder holds the region again. Realism lifecycle only; run wherever control may have moved.
void reconcileProjects(WorldState &, std::vector<ResolvedEvent> &events);
// What an agreed change of control (a cession, an agreed occupation, its withdrawal or expiry) does
// to one of the previous holder's unfinished projects in that region: a port, base or city passes
// to the new holder on its schedule, while an army or fleet is disbanded, returning its reserved
// troops or ships. Returns null when the entity is not such a project. Hostile capture instead
// pauses.
Json::Value handOverProject(const Json::Value &entity, const std::string &from,
                            const std::string &to, int turn);
Json::Value lifecycleReference(const WorldState &);
// Both functions operate on copies. Only Store::commit publishes migration or monthly work.
Resolution beginTurn(const WorldState &, const Json::Value &reference);
Resolution finishTurn(const Resolution &, const WorldDelta &, const Adjacency &,
                      const Json::Value &geography, const Json::Value &reference);
} // namespace lh
