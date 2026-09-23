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
// The realism settlement: every front's fractional losses for this actor are summed with what the
// last months left over, and only whole soldiers, aircraft and ships leave the inventory. The rest
// is kept in status.attrition for next month, so small forces lose equipment steadily instead of
// never. Personnel losses fall on the armies that were exposed and on the pool, never on an army
// still assembling; ship losses on the fleets within reach and the uncommitted fleet off its own
// coast.
void MonthlyStatus::applyRealismInventory(const std::string &a) {
    if (!static_cast<const WorldState &>(w).actors[a]["status"]["military"].isObject())
        return;
    auto &status = w.actors[a]["status"];
    auto &m = status["military"];
    const auto &carried = std::as_const(status)["attrition"];
    auto settle = [&](const char *key, double loss) {
        double total = loss + number(carried, key);
        Int whole = Int(std::floor(total + 1e-9));
        return std::pair{whole, std::max(0.0, total - double(whole))};
    };
    auto [soldiers, soldiersLeft] = settle("personnel", personnelLoss[a]);
    auto [aircraft, aircraftLeft] = settle("aircraft", aircraftLoss[a]);
    auto [ships, shipsLeft] = settle("ships", shipLoss[a]);
    Int active = integer(m, "active"), lost = std::min(active, soldiers);
    for (const auto &[id, count] : distribute(lost, personnelWeights[a])) {
        if (id.empty() || !w.entities.isMember(id))
            continue;
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
    m["reserve"] = std::max<Int>(0, integer(m, "reserve") - (soldiers - lost));
    m["aircraft"] = std::max<Int>(0, integer(m, "aircraft") - aircraft);
    Int sunk = std::min(ships, integer(m, "ships"));
    m["ships"] = integer(m, "ships") - sunk;
    for (const auto &[id, count] : distribute(sunk, shipWeights[a])) {
        if (id.empty() || !w.entities.isMember(id))
            continue;
        auto &e = w.entities[id];
        e["ships"] = std::max<Int>(0, integer(e, "ships") - count);
        if (integer(e, "ships") == 0) {
            e["status"] = "destroyed";
            e["loss_accounted"] = true;
            e.removeMember("completes_turn");
            r.events.push_back({a, "war_toll", id,
                                e["name"].asString() + " was sunk off " +
                                    w.territories[e["region"].asString()]["name"].asString() +
                                    "."});
        }
    }
    // Remainders are kept only once there are any, so a peaceful nation saves nothing new.
    if (soldiersLeft > 0 || aircraftLeft > 0 || shipsLeft > 0 || carried.isObject()) {
        auto round6 = [](double v) { return std::round(v * 1e6) / 1e6; };
        Json::Value left;
        left["personnel"] = round6(soldiersLeft);
        left["aircraft"] = round6(aircraftLeft);
        left["ships"] = round6(shipsLeft);
        status["attrition"] = left;
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
    // Under the realism rules, each nation's population held by others, as {held, of which no
    // longer its own}: it still feels land taken from it after the taker annexes it, and does not
    // feel land it lent by treaty.
    std::map<std::string, std::pair<Int, Int>> lost;
    if (realism())
        for (auto &id : regionIds) {
            const auto &t = cT[id];
            auto owner = t["owner"].asString(), controller = t["controller"].asString();
            const auto &record = hostileControl(w, id);
            if (record.isObject()) {
                auto claimant = record["claimant"].asString();
                lost[claimant].first += integer(t, "population");
                lost[claimant].second += owner == claimant ? 0 : integer(t, "population");
            } else if (owner != controller && !agreedOccupation(w, id, controller))
                lost[owner].first += integer(t, "population");
        }
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
        if (realism())
            occupiedPopulation = lost[a].first;
        else
            for (auto &id : regionIds)
                if (cT[id]["owner"] == a && cT[id]["controller"] != a)
                    occupiedPopulation += integer(cT[id], "population");
        auto &b = before[a];
        auto &n = after[a];
        Int population = n.population + lost[a].second;
        double gdpChange = b.gdp > 0 ? (double(n.gdp) - double(b.gdp)) / double(b.gdp) * 100 : 0;
        double target = baseline - war -
                        (population > 0 ? rate::occupiedStability * double(occupiedPopulation) /
                                              double(population)
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
