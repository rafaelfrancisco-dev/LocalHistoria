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
// Whether a region links back to the actor's own land (owned and controlled) through land it may
// enter and the sea routes between such coasts. An actor with no such land is never cut off.
bool supplied(const WorldState &, const Adjacency &, const std::string &actor,
              const std::string &region);
} // namespace lh
