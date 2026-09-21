#include "status/monthly/monthly_status.h"
#include <algorithm>
#include <cmath>
namespace lh::detail {
// Losses reach the national inventory, the armies and fleets that carry them, and the reserve
// mobilises for an escalated war.
void MonthlyStatus::applyInventory(const std::string &a) {
    if (!static_cast<const WorldState &>(w).actors[a]["status"]["military"].isObject())
        return;
    auto &m = w.actors[a]["status"]["military"];
    Int active = integer(m, "active"), lost = std::min(active, militaryLosses[a]);
    std::map<std::string, double> allocations;
    Int assigned = 0;
    for (const auto &id : w.entities.getMemberNames()) {
        const auto &e = w.entities[id];
        if (e["actor"] == a && e["kind"] == "army" &&
            (e["status"] == "active" || e["status"] == "under_construction")) {
            allocations[id] = number(e, "personnel");
            assigned += integer(e, "personnel");
        }
    }
    allocations[""] = double(std::max<Int>(0, active - assigned));
    for (const auto &[id, count] : distribute(lost, allocations))
        if (!id.empty()) {
            auto &e = w.entities[id];
            e["personnel"] = std::max<Int>(0, integer(e, "personnel") - count);
            if (integer(e, "personnel") < 2000) {
                e["status"] = "destroyed";
                e.removeMember("completes_turn");
                r.events.push_back({a, "war_toll", id,
                                    e["name"].asString() + " was destroyed in " +
                                        w.territories[e["region"].asString()]["name"].asString() +
                                        "."});
            }
        }
    m["active"] = active - lost;
    m["reserve"] = std::max<Int>(0, integer(m, "reserve") - (militaryLosses[a] - lost));
    m["aircraft"] = std::max<Int>(0, integer(m, "aircraft") - aircraftLosses[a]);
    m["ships"] = std::max<Int>(0, integer(m, "ships") - shipLosses[a]);
    // Attrition cannot leave fleets promising more ships than exist nationally.
    std::map<std::string, double> fleets;
    Int fleetTotal = 0;
    for (const auto &id : w.entities.getMemberNames()) {
        const auto &e = w.entities[id];
        if (e["actor"] == a && e["kind"] == "fleet" &&
            (e["status"] == "active" || e["status"] == "under_construction")) {
            fleets[id] = number(e, "ships");
            fleetTotal += integer(e, "ships");
        }
    }
    if (fleetTotal > integer(m, "ships")) {
        auto remaining = distribute(integer(m, "ships"), fleets);
        for (const auto &[id, weight] : fleets) {
            auto &e = w.entities[id];
            e["ships"] = remaining[id];
            if (!remaining[id]) {
                e["status"] = "destroyed";
                e["loss_accounted"] = true;
                e.removeMember("completes_turn");
                r.events.push_back({a, "war_toll", id,
                                    e["name"].asString() + " was destroyed in " +
                                        w.territories[e["region"].asString()]["name"].asString() +
                                        "."});
            }
        }
    }
    if (mobilising.contains(a)) {
        Int moved = std::min(
            integer(m, "reserve"),
            Int(std::floor(number(forceStart.actors[a]["status"]["military"], "reserve") * 0.02)));
        m["reserve"] = integer(m, "reserve") - moved;
        m["active"] = integer(m, "active") + moved;
    }
}
void MonthlyStatus::returnDisplaced() {
    for (auto &id : regionIds) {
        auto &t = T[id];
        auto waiting = integer(cT[id], "displaced");
        if (waiting <= 0 || allAffected.contains(id) || cT[id]["owner"] != cT[id]["controller"])
            continue;
        auto count = std::min(
            waiting, std::max<Int>(1, Int(std::llround(double(waiting) * rate::returnShare))));
        std::map<std::string, double> sources;
        for (auto &other : regionIds)
            if (other != id && cT[other]["owner"] == cT[id]["owner"] &&
                cT[other]["controller"] == cT[id]["owner"] && integer(cT[other], "population") > 0)
                sources[other] = double(integer(cT[other], "population"));
        Int back = 0;
        for (auto &[from, n] : distribute(count, sources))
            back += move(from, id, n);
        t["displaced"] = waiting - (sources.empty() ? count : back);
    }
}
void MonthlyStatus::driftStability() {
    auto after = totals(w);
    for (auto &a : w.actors.getMemberNames()) {
        if (!cActors[a]["status"].isObject())
            continue;
        auto &s = w.actors[a]["status"];
        auto baseline =
            std::clamp(number(s, "baseline_stability", 50) + 0.25 * effectStability[a], 0.0, 100.0);
        double war = 0;
        for (auto &f : fronts)
            if (f.parties.contains(a) && f.intensity >= 0)
                war = std::max(war, rate::stabilityWar[f.intensity]);
        Int occupiedPopulation = 0;
        for (auto &id : regionIds)
            if (cT[id]["owner"] == a && cT[id]["controller"] != a)
                occupiedPopulation += integer(cT[id], "population");
        auto &b = before[a];
        auto &n = after[a];
        double gdpChange = b.gdp > 0 ? (double(n.gdp) - double(b.gdp)) / double(b.gdp) * 100 : 0;
        double target = baseline - war -
                        (n.population > 0 ? rate::occupiedStability * double(occupiedPopulation) /
                                                double(n.population)
                                          : 0) -
                        (gdpChange < -1 ? rate::contractionStability : 0);
        auto stability = number(s, "stability", 50);
        double shock =
            n.population > 0 && double(actorDeaths[a]) / double(n.population) > 0.0001 ? 1 : 0;
        auto step =
            std::clamp(rate::stabilityReversion * (target - stability) + effectStability[a] - shock,
                       -rate::maxStabilityStep, rate::maxStabilityStep);
        s["stability"] = round1(std::clamp(stability + step, 0.0, 100.0));
        s["baseline_stability"] = round1(baseline);
        s["last_gdp_change"] = std::round(gdpChange * 100) / 100;
        s["last_population_change"] = n.population - b.population;
    }
}
} // namespace lh::detail
