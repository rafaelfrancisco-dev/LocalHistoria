#pragma once
#include "simulation.h"
namespace lh {
struct WorldDelta {
    Json::Value changes{Json::arrayValue}, events{Json::arrayValue};
};
WorldDelta parseDelta(const Json::Value &);
Json::Value encode(const WorldDelta &);
Resolution applyDelta(const Resolution &, const WorldDelta &, const Json::Value &geography);
// Small models emit compact operations nested in events; C++ derives exact before/after changes.
// Region borders, plus the frozen coastal-route graph: for each coastal region, the coastal regions
// a fleet can reach within a month and the route's length in whole kilometres. The graph's revision
// is recorded in each turn's references so replay uses the same one.
struct Adjacency : std::map<std::string, std::set<std::string>> {
    using std::map<std::string, std::set<std::string>>::map;
    std::map<std::string, std::map<std::string, int>> sea;
    // Bodies of water a supply line crosses at any distance, and the coastal regions on each.
    std::map<std::string, std::set<int>> basins;
    std::map<int, std::vector<std::string>> basinRegions;
    std::string seaRevision;
};
// A fleet's monthly allowance, and the troops one major warship can conservatively carry.
constexpr int fleetRangeKm = 1500, troopsPerShip = 500;
// Length of the sea route between two coastal regions, 0 for the same region, -1 for none.
int seaKm(const Adjacency &, const std::string &from, const std::string &to);
// The sea routes and their revision for a replay export: each route once, from the lower ID.
Json::Value encodeSeaRoutes(const Adjacency &);
void decodeSeaRoutes(Adjacency &, const Json::Value &);
// actions: (actor, text) pairs in the order the model sees them; the schema requires one outcome
// per action. The first six words of an action, which open its adjudicated outcome.
std::string actionOpening(const std::string &);
// adjacency narrows build and occupy per acting actor: it can only build where it already governs
// and only occupy what it can physically reach, so the grammar cannot express the wrong operation.
Json::Value operationSchema(const WorldState &, const Json::Value &facts,
                            const std::vector<std::pair<std::string, std::string>> &actions,
                            const Adjacency &adjacency = {}, bool narrow = false);
// Physically impossible operations (reach, pacing, ownership) block only their event.
// responses: the player's [{proposal, accept}] answers to last month's treaty offers, settled
// first; unanswered offers lapse. priorEvents: IDs already in campaign history, so a background
// development that repeats one is dropped.
WorldDelta compileOperations(const WorldState &, const Json::Value &response,
                             const Adjacency &adjacency,
                             const Json::Value &responses = Json::Value(Json::arrayValue),
                             const std::set<std::string> &priorEvents = {});
// For an event with partial or failed set: " (In fact: no forces moved.)", from its dropped
// operations; "" otherwise. Prompts read it after the event text; the saved text stays the model's
// description.
std::string droppedClause(const Json::Value &event);
int constructionMonths(const std::string &kind);
// A colour from the fixed actor palette, picked by hashing the actor ID.
std::string paletteColor(const std::string &id);
// Colours every actor from the palette so that no two bordering actors share one.
void assignActorColors(WorldState &,
                       const std::vector<std::pair<std::string, std::string>> &neighbors);
// The same region, or regions that share a border.
bool adjacent(const Adjacency &, const std::string &from, const std::string &to);
// Whether an actor can project force into a region this month (bordering controlled land, or a
// nearby army/fleet). Under the realism lifecycle a fleet reaches coastal regions along its sea
// routes rather than across land borders.
bool reaches(const WorldState &, const Adjacency &, const std::string &actor,
             const std::string &region);
bool containsPoint(const Json::Value &geometry, double longitude, double latitude);
// Whole-word match on lowercased text.
bool containsWord(const std::string &text, const std::string &word);
// Wars, invasions, territory and treaties: an action whose outcome history can change, worth
// grounding and reasoning about.
bool consequential(const std::string &text);
// An initiative that announces something rather than doing it: a town hall, a heritage or awareness
// declaration, a ceremony. An independent slot has one action a month, so one of these is a month
// thrown away and is worth a single re-ask.
bool ceremonial(const std::string &text);
} // namespace lh
