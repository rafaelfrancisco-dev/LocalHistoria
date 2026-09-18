#pragma once
#include "simulation.h"
#include <cstdint>
#include <map>
namespace lh {
bool identityCorrection(const std::string &, const std::string &);
uint64_t stableHash(const std::string &);
// Slots contain governing actor, optional region, initiative type and exactly one action.
// `closeness` ranks the first national slot: 0 default, 1 large-bloc partner, 2 war or small-pact
// partner, 3 neighbour (the player's own entry, and tiers of at least 2, favour regional slots).
// The second slot prefers `powers`, or every third month a distant nation.
Json::Value scheduleInitiatives(const WorldState &, const std::set<std::string> &reactive = {},
                                const std::map<std::string, int> &closeness = {},
                                const std::set<std::string> &powers = {});
Json::Value flagDesignSchema();
Json::Value makeFlag(const Json::Value &design, const std::string &seed, const std::string &event,
                     const std::string &previous = "");
std::string renderFlag(const Json::Value &);
void reviseIdentity(Json::Value &actor, const Json::Value &design, const std::string &seed,
                    const std::string &event);
Json::Value regionalState(const WorldState &, const std::string &region);
} // namespace lh
