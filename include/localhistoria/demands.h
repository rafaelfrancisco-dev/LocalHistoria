#pragma once
#include "simulation.h"
namespace lh {
// Demand version 1 (plan 32): a new regional demand carries immutable, bounded terms, and
// accepting it is recorded apart from delivering it. Terms are one of: political (a request the
// ruler may agree to negotiate; nothing changes), regional_project (funded work in the region,
// fulfilled only when the project completes) or independence (the region becomes a state, which
// only its owner, holding it, can grant).
constexpr int demandVersion = 1, demandRevision = 1;
bool typedDemands(const WorldState &);
// A new demand's terms from what a regional initiative asked for. An unsupported or incomplete
// request becomes a political one; the result is always valid.
Json::Value demandTerms(const WorldState &, const std::string &region, const std::string &kind,
                        const std::string &benefit, const std::string &text);
bool validDemandTerms(const Json::Value &terms);
// The project category a regional_project benefit belongs to, or "" when it has none.
std::string demandCategory(const std::string &benefit);
// Months an accepted regional project has to complete before the promise counts as broken.
int fulfillmentMonths(const Json::Value &terms);
} // namespace lh
