#pragma once
#include "world/world_internal.h"
#include <set>
namespace lh::detail {
// applyDelta's checks, in the order it runs them.
void checkEvents(const WorldDelta &d, const WorldState &w, std::set<std::string> &eventIds);
void checkActorChange(const Json::Value &c);
void checkRegionalChange(const Json::Value &c, const std::string &id, WorldState &w);
void checkProposalChange(const Json::Value &c, WorldState &w);
void checkTerritoryChange(const Json::Value &c);
void checkTerritories(WorldState &w);
void checkEntities(const Resolution &base, WorldState &w, const Json::Value &geo,
                   const std::set<std::string> &eventIds);
void checkProposals(const Resolution &base, WorldState &w);
void checkNewActors(const Resolution &base, WorldState &w);
void checkAgreements(const Resolution &base, WorldState &w);
} // namespace lh::detail
