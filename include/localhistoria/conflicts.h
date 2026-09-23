#pragma once
#include "status.h"
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
} // namespace lh
