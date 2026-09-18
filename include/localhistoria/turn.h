#pragma once
#include "world.h"
namespace lh {
struct Mentions {
    std::set<std::string> actors, regions;
};
// Free text names nations and regions; resolve those names to IDs so the model sees the relevant map.
Mentions mentioned(const WorldState &, const std::string &actor, const TurnOrders &);
// What the adjudication prompt reads: facts of every actor with a context, the regions and entities this month's
// actions concern, and the intentions, player first.
Json::Value adjudicationInput(const WorldState &, const std::map<std::string, CountryContext> &contexts,
                              const std::map<std::string, TurnOrders> &intentions, const Adjacency &);
} // namespace lh
