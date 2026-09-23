#pragma once
#include "localhistoria/conflicts.h"
#include "localhistoria/status.h"
#include "localhistoria/store.h"
#include <iostream>
#include <tuple>
using namespace lh;
inline void check(bool b, const std::string &what) {
    if (!b)
        throw std::runtime_error(what);
}
inline Json::Int64 people(const WorldState &w) {
    Json::Int64 n = 0;
    for (auto &t : w.territories)
        n += t["population"].asInt64();
    return n;
}
inline WorldDelta events(const char *json) {
    WorldDelta d;
    d.events = parse(json);
    return d;
}
// The real January 2026 world from the reference database, and chronicle scheduling.
void realWorldTests();
// Regional unrest, and the rebellion, secession and regime-collapse chain it feeds.
void unrestTests();
// Plan 26 E: control history, annexation, occupation resistance and integration.
void conquestTests();
// Plan 26 F: one force allocation per month, exposure-based losses and carried fractions.
void forceTests();
// Plan 26 G: the origin-host displacement ledger and actual return migration.
void displacementTests();
// Plan 26 H: the implementation allowance, efforts, trade and military replacement.
void economyTests();
// Plan 26 I: exhaustion, capture effort and exposure-weighted fronts over 24-60 months.
void pacingTests();
