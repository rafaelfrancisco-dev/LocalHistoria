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
