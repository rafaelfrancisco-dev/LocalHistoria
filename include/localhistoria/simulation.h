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
struct CountryState {
    int treasury = 100, income = 10, stability = 60, army = 10, navy = 10;
    std::string posture = "defensive", priority = "balanced";
};
struct WorldState {
    int turn = 0;
    std::string player, campaignId;
    std::map<std::string, CountryState> countries;
    std::map<std::string, int> relations;
    std::set<std::string> alliances, offers;
};
struct Order {
    std::string action, target, value;
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
std::string pairKey(std::string a, std::string b);
Json::Value encode(const WorldState &);
WorldState decode(const Json::Value &);
Json::Value encode(const TurnOrders &);
Json::Value encode(const std::vector<ResolvedEvent> &);
TurnOrders parseOrders(const Json::Value &);
void validate(const WorldState &, const std::string &, const TurnOrders &);
Resolution resolve(const WorldState &, const std::map<std::string, TurnOrders> &);
TurnOrders scripted(const WorldState &, const std::string &);
CountryContext context(const WorldState &, const std::string &, const Json::Value &,
                       const Json::Value &);
std::string dump(const Json::Value &);
Json::Value parse(const std::string &);
} // namespace lh
