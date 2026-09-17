#pragma once
#include "world.h"
namespace lh {
// Reference inputs frozen into a campaign: national statistics keyed by actor code, regional population/GDP
// shares keyed by territory ID, and the January 2026 wars and treaties.
struct StatusSeed {
    Json::Value countries{Json::objectValue}, regions{Json::objectValue}, world;
};
Json::Value encode(const StatusSeed &);
StatusSeed decodeSeed(const Json::Value &);
// Adds national status and regional population/GDP; with worldEvents, also the seeded wars, treaties,
// occupations and non-state actors. Only for new campaigns or saves that predate national status.
void seedStatus(WorldState &, const StatusSeed &, bool worldEvents);
bool hasStatus(const WorldState &);
// Compact, model-ready and UI-ready national situation derived from the regions an actor owns.
Json::Value nationalStatus(const WorldState &, const std::string &actor);
Json::Value allNationalStatus(const WorldState &);
std::string formatMoney(double usd, const Json::Value &status);
// A conflict or commitment as the model and UI should see it: name, kind, parties, intensity and totals.
Json::Value describeAgreement(const WorldState &, const std::string &kind, const std::string &id);
std::string displayName(const WorldState &, const std::string &kind, const std::string &id);
// Historians' naming: keeps a specific proposed name, replaces generic ones and numbers repeats.
std::string realisticName(const WorldState &, const std::string &kind, const std::string &type,
                          const std::string &proposed, const std::vector<std::string> &parties,
                          const std::string &text, const std::set<std::string> &taken);
bool validConflictKind(const std::string &);
bool validCommitmentKind(const std::string &);
bool validIntensity(const std::string &);
bool validEffect(const std::string &);
// Deterministic monthly consequences after the accepted delta: growth, war fronts, casualties,
// displacement, GDP and stability. Replay runs it again, so it needs no inference.
Resolution applyMonthlyStatus(const Resolution &, const WorldDelta &, const Adjacency &);
constexpr int statusVersion = 1;
} // namespace lh
