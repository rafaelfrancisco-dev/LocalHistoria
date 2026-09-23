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
// A nation that may react to the player's month, and how urgently: 0 owes an answer to a pending
// treaty proposal, 1 is fighting the player, 2 is in an acute domestic crisis, 3 has a war of its
// own to decide, 4 was named by the player, 5 is otherwise linked (a pact partner, an enemy's ally,
// the rotating neighbour).
// `age` is how long the decision has waited, where known; `order` the routine order it arrived in.
struct ReactiveCandidate {
    std::string actor;
    int urgency = 5, age = 0, order = 0;
};
// Classifies the player's linked nations (in routine order) and every party to a pending proposal.
std::vector<ReactiveCandidate> reactiveCandidates(const WorldState &,
                                                  const std::vector<std::string> &linked,
                                                  const std::set<std::string> &mentioned);
// The month's reactive nations, at most `cap`: most urgent first; within an urgency class the
// longest-waiting decision, then whoever was served least recently (never served first), then the
// routine order and ID. A nation left out stays urgent and rises next month as it ages, so a finite
// backlog is always served; an unbounded stream of new emergencies can still delay routine ones.
std::vector<std::string> selectReactive(const std::vector<ReactiveCandidate> &,
                                        const std::map<std::string, int> &lastService, int turn,
                                        size_t cap);
Json::Value flagDesignSchema();
Json::Value makeFlag(const Json::Value &design, const std::string &seed, const std::string &event,
                     const std::string &previous = "");
std::string renderFlag(const Json::Value &);
void reviseIdentity(Json::Value &actor, const Json::Value &design, const std::string &seed,
                    const std::string &event);
Json::Value regionalState(const WorldState &, const std::string &region);
} // namespace lh
