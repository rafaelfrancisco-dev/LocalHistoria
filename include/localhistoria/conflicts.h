#pragma once
#include "status.h"
#include "world.h"
namespace lh {
bool realismRules(const WorldState &);
// Canonical unordered pairs; missing historical sides use a documented conservative fallback.
Json::Value hostilePairs(const WorldState &, const Json::Value &conflict);
void initializeConflictPairs(WorldState &);
bool fightingPair(const WorldState &, const Json::Value &, const std::string &,
                  const std::string &);
bool fighting(const WorldState &, const std::string &, const std::string &);
int pairPosture(const Json::Value &, const std::string &, const std::string &);
std::string hostilePairKey(const std::string &, const std::string &);
// Peaceful entry: land the actor controls, land nobody holds, or land whose controller granted it
// military access in an open treaty. An alliance by itself grants no basing or transit.
bool mayEnter(const WorldState &, const std::string &actor, const std::string &region);
// An actor's most populous region that it both owns and controls (residual slivers excepted, ties
// by ID), or "" if it has none.
std::string heartland(const WorldState &, const std::string &actor);
// Whether a region links back to the actor's heartland through land it may enter, sea routes, or an
// ocean basin shared by coasts it may enter. An actor with no heartland is never cut off.
bool supplied(const WorldState &, const Adjacency &, const std::string &actor,
              const std::string &region);
} // namespace lh
