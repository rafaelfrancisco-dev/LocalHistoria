#pragma once
#include "localhistoria/status.h"
#include "localhistoria/store.h"
namespace lh {
// Region geometry keyed by territory ID, for placing entities.
Json::Value loadGeography(Store &reference);
Adjacency loadAdjacency(Store &reference);
// National statistics, regional shares and the January 2026 wars and treaties.
StatusSeed loadStatusSeed(Store &reference);
// Every reference country as an actor, every region at its initial affiliation, the three largest
// cities of each nation, and seeded national status and world events.
WorldState newCampaign(Store &reference, const StatusSeed &, const std::string &country);
} // namespace lh
