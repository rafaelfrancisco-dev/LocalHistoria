#pragma once
#include <json/json.h>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
namespace lh {
struct ValidationError : std::runtime_error {
    using runtime_error::runtime_error;
};
struct WorldState {
    int turn = 0;
    std::string player, campaignId;
    Json::Value actors{Json::objectValue}, territories{Json::objectValue},
        entities{Json::objectValue}, conflicts{Json::objectValue}, commitments{Json::objectValue};
    Json::Value regionalPolitics{Json::objectValue}, activity{Json::objectValue};
    // Treaties an AI nation offered the player, keyed by the commitment ID reserved for them;
    // answered the next month.
    Json::Value proposals{Json::objectValue};
    bool hasActor(const std::string &id) const { return actors.isMember(id); }
};
// A free-text action written by a player or an AI nation, optionally pinned to map targets.
struct Order {
    std::string text;
    std::vector<std::string> targets;
};
using TurnOrders = std::vector<Order>;
struct ResolvedEvent {
    std::string actor, action, target, text;
};
struct CountryContext {
    std::string actor;
    Json::Value facts, reference;
};
struct Resolution {
    WorldState state;
    std::vector<ResolvedEvent> events;
};
constexpr size_t maxActions = 8, maxTargets = 4, maxActionBytes = 500;
// Months an event stays on the map, and an event site stays active, unless a later related event
// renews it.
constexpr int eventMapMonths = 3;
Json::Value encode(const WorldState &);
WorldState decode(const Json::Value &);
Json::Value encode(const TurnOrders &);
Json::Value encode(const std::vector<ResolvedEvent> &);
TurnOrders parseOrders(const Json::Value &);
void validate(const WorldState &, const std::string &, const TurnOrders &);
// Advances one month: construction that is due completes and expired event sites fade; everything
// else is adjudicated.
Resolution resolve(const WorldState &);
CountryContext context(const WorldState &, const std::string &, const Json::Value &,
                       const Json::Value &);
// A reference cut at word boundaries: the introduction keeps its start and the history its most
// recent end; history 0 removes it.
Json::Value briefReference(const Json::Value &, size_t excerpt, size_t history);
std::string dump(const Json::Value &);
Json::Value parse(const std::string &);
} // namespace lh
