#include "status/monthly/monthly_status.h"
#include <algorithm>
#include <cmath>
namespace lh {
namespace detail {
MonthlyStatus::MonthlyStatus(const Resolution &base, const WorldDelta &delta,
                             const Adjacency &adjacency, int version)
    : base(base), delta(delta), adjacency(adjacency), version(version), r(base), w(r.state),
      T(w.territories), cT(w.territories), cActors(w.actors) {}
Resolution MonthlyStatus::run() {
    freezeForceInputs();
    accountDestroyed();
    regionIds = T.getMemberNames();
    inheritStatus();
    if (hasMilitary(forceStart) && !hasMilitary(w))
        seedMilitary(w, StatusSeed{});
    before = totals(w);

    // 1. Baseline demographic and economic trend.
    baselineTrend();
    // 2. Qualitative model effects, bounded per actor per month.
    modelEffects();
    // 3. Occupation drains the output of occupied land, down to a floor.
    occupationDrain();

    // 4. War fronts: where each open conflict is actually fought this month.
    collectSeized();
    // 4b. Ground the balance of forces takes on its own, before the fronts are drawn: a momentum
    // seizure has to count as fighting and displace people exactly like an ordered one.
    advanceFronts();
    // 4c. Rebel ground, on the same tick as conventional momentum so the toll, the displacement and
    // the front drawing treat a seizure by insurgents exactly like any other.
    advanceRebellions();
    for (auto &id : w.conflicts.getMemberNames())
        addFront(id);

    // 5. Casualties, displacement and war damage.
    auto current = totals(w);
    for (auto &f : fronts)
        warToll(f, current);

    // 5b. Apply all inventory changes after computing every conflict from the same frozen inputs.
    for (const auto &a : w.actors.getMemberNames())
        applyInventory(a);
    accountDestroyed();

    // 6. Displaced people return home once their region is calm and back under its owner's control.
    returnDisplaced();

    // 7. Stability drifts toward what the nation's situation supports; model effects leave a
    // lasting trace.
    driftStability();

    // 8. Unrest, and what sustained unrest eventually does: revolt, secession, collapse. Last,
    // because it reads the stability this month finished with.
    unrestStep();
    return r;
}
// Freeze force inputs before any operations, growth, deaths or mobilisation this month.
void MonthlyStatus::freezeForceInputs() {
    forceStart = base.state;
    for (const auto &change : delta.changes) {
        auto kind = change["kind"].asString(), id = change["id"].asString();
        Json::Value *collection = kind == "actor"       ? &forceStart.actors
                                  : kind == "territory" ? &forceStart.territories
                                  : kind == "entity"    ? &forceStart.entities
                                  : kind == "conflict"  ? &forceStart.conflicts
                                                        : nullptr;
        if (collection) {
            if (change["before"].isNull())
                collection->removeMember(id);
            else
                (*collection)[id] = change["before"];
        }
    }
}
void MonthlyStatus::accountDestroyed() {
    for (auto &e : w.entities) {
        if (e["status"] != "destroyed" || e.get("loss_accounted", false).asBool() ||
            (e["kind"] != "army" && e["kind"] != "fleet"))
            continue;
        if (!static_cast<const WorldState &>(w)
                 .actors[e["actor"].asString()]["status"]["military"]
                 .isObject())
            continue;
        auto &m = w.actors[e["actor"].asString()]["status"]["military"];
        const char *field = e["kind"] == "army" ? "active" : "ships";
        m[field] = std::max<Int>(0, integer(m, field) -
                                        integer(e, e["kind"] == "army" ? "personnel" : "ships"));
        e["loss_accounted"] = true;
    }
}
const Json::Value &MonthlyStatus::ownerStatus(const Json::Value &t) const {
    return cActors[t["owner"].asString()]["status"];
}
// New states inherit the economic character of the land they were carved from.
void MonthlyStatus::inheritStatus() {
    for (auto &a : w.actors.getMemberNames()) {
        if (w.actors[a].isMember("status"))
            continue;
        std::string largest;
        Int best = -1;
        for (auto &id : regionIds)
            if (cT[id]["owner"] == a && integer(cT[id], "population") > best) {
                best = integer(cT[id], "population");
                largest = id;
            }
        if (largest.empty())
            continue;
        auto parent = cT[largest]["initial_affiliation"].asString();
        if (!cActors[parent]["status"].isObject())
            continue;
        auto s = cActors[parent]["status"];
        s["stability"] = s["baseline_stability"] = std::min(number(s, "stability", 50), 45.0);
        s["seeded_stability"] = s["stability"];
        // A state born out of a secession is not a copy of the constitution it just repudiated.
        s["government"] = "transitional_government";
        s["government_since"] = 2026 + int(std::floor(w.turn / 12.0));
        s.removeMember("collapse_months");
        s.removeMember("last_gdp_change");
        s.removeMember("last_population_change");
        // A new actor cannot copy its parent's entire military inventory.
        s.removeMember("military");
        s.removeMember("military_spend_usd");
        w.actors[a]["status"] = s;
    }
}
void MonthlyStatus::baselineTrend() {
    for (auto &id : regionIds) {
        auto &t = T[id];
        if (!cT[id].isMember("population"))
            continue;
        auto &s = ownerStatus(cT[id]);
        if (!s.isObject())
            continue;
        t["population"] =
            Int(std::llround(double(integer(t, "population")) *
                             (1 + std::clamp(number(s, "pop_growth"), -5.0, 5.0) / 1200)));
        t["gdp_usd"] =
            Int(std::llround(double(integer(t, "gdp_usd")) *
                             (1 + std::clamp(number(s, "gdp_growth"), -10.0, 10.0) / 1200)));
    }
}
void MonthlyStatus::modelEffects() {
    for (const auto &e : delta.events)
        for (const auto &x : e["effects"]) {
            static const std::map<std::string, std::pair<double, double>> scale{
                {"severe_decline", {-1.0, -6}},
                {"decline", {-0.3, -2}},
                {"none", {0, 0}},
                {"growth", {0.2, 1}},
                {"strong_growth", {0.5, 3}}};
            auto a = x["actor"].asString();
            if (scale.contains(x["economy"].asString()))
                effectGdp[a] += scale.at(x["economy"].asString()).first;
            if (scale.contains(x["stability"].asString()))
                effectStability[a] += scale.at(x["stability"].asString()).second;
        }
    for (auto &[a, pct] : effectGdp) {
        pct = std::clamp(pct, -rate::maxEffectGdp, rate::maxEffectGdp);
        for (auto &id : regionIds)
            if (cT[id]["owner"] == a)
                T[id]["gdp_usd"] =
                    Int(std::llround(double(integer(cT[id], "gdp_usd")) * (1 + pct / 100)));
    }
    for (auto &[a, s] : effectStability)
        s = std::clamp(s, -rate::maxEffectStability, rate::maxEffectStability);
}
void MonthlyStatus::occupationDrain() {
    for (auto &id : regionIds) {
        auto &t = T[id];
        if (!cT[id].isMember("population"))
            continue;
        if (cT[id]["owner"] != cT[id]["controller"] && cT[id]["owner"] != "") {
            if (!t.isMember("occupation_gdp"))
                t["occupation_gdp"] = integer(t, "gdp_usd");
            auto floor = double(integer(t, "occupation_gdp")) * rate::occupationGdpFloor;
            t["gdp_usd"] = Int(
                std::llround(std::max(double(integer(t, "gdp_usd")) * (1 - rate::occupationGdpLoss),
                                      std::min(floor, double(integer(t, "gdp_usd"))))));
        } else
            t.removeMember("occupation_gdp");
    }
}
} // namespace detail
Resolution applyMonthlyStatus(const Resolution &base, const WorldDelta &delta,
                              const Adjacency &adjacency, int version) {
    return detail::MonthlyStatus(base, delta, adjacency, version).run();
}
} // namespace lh
