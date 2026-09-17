#pragma once
#include "simulation.h"
namespace lh {
struct WorldDelta { Json::Value changes{Json::arrayValue}, events{Json::arrayValue}; };
WorldDelta parseDelta(const Json::Value &);
Json::Value encode(const WorldDelta &);
Resolution applyDelta(const Resolution &, const WorldDelta &, const Json::Value &geography);
// Small models emit compact operations nested in events; C++ derives exact before/after changes.
Json::Value operationSchema(const WorldState &, const Json::Value &facts);
WorldDelta compileOperations(const WorldState &, const Json::Value &response);
bool containsPoint(const Json::Value &geometry, double longitude, double latitude);
}
