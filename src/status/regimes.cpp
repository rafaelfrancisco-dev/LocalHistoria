#include "status/status_internal.h"
#include <algorithm>
namespace lh {
namespace {
struct Entry {
    const char *system;
    const char *label;
    Regime regime;
};
// vent, repression, armedFirst, inhabited.
//
// These numbers do not touch driftStability: the seeded stability figures are real World Bank
// observations, and giving juntas a baseline penalty would pull all 239 nations away from their
// January values on the first month. A political system modulates only what discontent does.
const Entry table[] = {
    {"parliamentary_republic", "Parliamentary republic", {0.90, 0.10, false, true}},
    {"presidential_republic", "Presidential republic", {0.80, 0.15, false, true}},
    {"semi_presidential_republic", "Semi-presidential republic", {0.80, 0.15, false, true}},
    {"constitutional_monarchy", "Constitutional monarchy", {0.90, 0.10, false, true}},
    {"absolute_monarchy", "Absolute monarchy", {0.20, 0.70, true, true}},
    {"theocracy", "Theocracy", {0.20, 0.70, true, true}},
    {"one_party_state", "One-party state", {0.15, 0.85, true, true}},
    {"dominant_party_state", "Dominant-party state", {0.40, 0.55, false, true}},
    {"military_junta", "Military junta", {0.10, 0.80, true, true}},
    {"transitional_government", "Transitional government", {0.30, 0.20, true, true}},
    {"dependent_territory", "Dependent territory", {0.70, 0.20, false, true}},
    {"uninhabited_territory", "Uninhabited territory", {1.00, 0.00, false, false}},
};
const Entry *find(const std::string &system) {
    for (const auto &e : table)
        if (system == e.system)
            return &e;
    return nullptr;
}
} // namespace
const std::vector<std::string> &governments() {
    static const std::vector<std::string> all = [] {
        std::vector<std::string> v;
        for (const auto &e : table)
            v.emplace_back(e.system);
        return v;
    }();
    return all;
}
bool validGovernment(const std::string &system) { return find(system) != nullptr; }
std::string governmentLabel(const std::string &system) {
    const auto *e = find(system);
    return e ? e->label : std::string();
}
// An unknown or absent system is treated as a middling one rather than as an error: a save from
// before political systems existed keeps playing until seedGovernment backfills it.
const Regime &regime(const std::string &system) {
    static const Regime fallback{};
    const auto *e = find(system);
    return e ? e->regime : fallback;
}
std::string unrestDescription(double unrest) { return detail::unrestLabel(unrest); }
} // namespace lh
