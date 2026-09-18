#pragma once
#include "simulation.h"
namespace lh {
struct WorldDelta { Json::Value changes{Json::arrayValue}, events{Json::arrayValue}; };
WorldDelta parseDelta(const Json::Value &);
Json::Value encode(const WorldDelta &);
Resolution applyDelta(const Resolution &, const WorldDelta &, const Json::Value &geography);
// Small models emit compact operations nested in events; C++ derives exact before/after changes.
using Adjacency = std::map<std::string, std::set<std::string>>;
// actions: (actor, text) pairs in the order the model sees them; the schema requires one outcome per action.
// The first six words of an action, which open its adjudicated outcome.
std::string actionOpening(const std::string &);
Json::Value operationSchema(const WorldState &, const Json::Value &facts, const std::vector<std::pair<std::string, std::string>> &actions);
// Physically impossible operations (reach, pacing, ownership) block only their event.
// responses: the player's [{proposal, accept}] answers to last month's treaty offers, settled first; unanswered offers lapse.
WorldDelta compileOperations(const WorldState &, const Json::Value &response, const Adjacency &adjacency, const Json::Value &responses = Json::Value(Json::arrayValue));
int constructionMonths(const std::string &kind);
// Whether an actor can project force into a region this month (bordering controlled land, or a nearby army/fleet).
bool reaches(const WorldState &, const Adjacency &, const std::string &actor, const std::string &region);
bool containsPoint(const Json::Value &geometry, double longitude, double latitude);
std::string lowercase(const std::string &);
// Whole-word match on lowercased text.
bool containsWord(const std::string &text, const std::string &word);
// Wars, invasions, territory and treaties: an action whose outcome history can change, worth grounding and reasoning about.
bool consequential(const std::string &text);
// For an attack order, a short hint appended to the intention so the model reaches for occupy rather than moving a city.
// territories: the adjudication facts with each region's within_reach_of list.
std::string attackGuidance(const std::string &actor, const std::string &text, const Json::Value &territories);
}
