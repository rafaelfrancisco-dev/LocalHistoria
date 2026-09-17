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
Json::Value operationSchema(const WorldState &, const Json::Value &facts, const std::vector<std::pair<std::string, std::string>> &actions);
// Physically impossible operations (reach, pacing, ownership) block only their event.
WorldDelta compileOperations(const WorldState &, const Json::Value &response, const Adjacency &adjacency);
int constructionMonths(const std::string &kind);
// Whether an actor can project force into a region this month (bordering controlled land, or a nearby army/fleet).
bool reaches(const WorldState &, const Adjacency &, const std::string &actor, const std::string &region);
bool containsPoint(const Json::Value &geometry, double longitude, double latitude);
}
