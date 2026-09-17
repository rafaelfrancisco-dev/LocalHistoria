#pragma once
#include "simulation.h"
#include <cstdint>
namespace lh {
bool identityCorrection(const std::string &, const std::string &);
uint64_t stableHash(const std::string &);
// Slots contain governing actor, optional region, initiative type and exactly one action.
Json::Value scheduleInitiatives(const WorldState &, const std::set<std::string> &reactive = {});
Json::Value flagDesignSchema();
Json::Value makeFlag(const Json::Value &design, const std::string &seed, const std::string &event,
                     const std::string &previous = "");
std::string renderFlag(const Json::Value &);
void reviseIdentity(Json::Value &actor, const Json::Value &design, const std::string &seed,
                    const std::string &event);
Json::Value regionalState(const WorldState &, const std::string &region);
} // namespace lh
