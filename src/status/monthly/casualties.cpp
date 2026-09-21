#include "status/monthly/monthly_status.h"
#include <algorithm>
#include <cmath>
namespace lh::detail {
Int MonthlyStatus::move(const std::string &from, const std::string &to, Int count) {
    auto &a = T[from];
    auto &b = T[to];
    count = std::min(count, integer(a, "population"));
    if (count <= 0)
        return Int(0);
    auto output = Int(std::llround(double(integer(a, "gdp_usd")) * double(count) /
                                   double(std::max<Int>(1, integer(a, "population"))) *
                                   rate::displacedOutput));
    a["population"] = integer(a, "population") - count;
    b["population"] = integer(b, "population") + count;
    a["gdp_usd"] = integer(a, "gdp_usd") - output;
    b["gdp_usd"] = integer(b, "gdp_usd") + output;
    return count;
}
Int MonthlyStatus::kill(const std::string &region, Int count) {
    count = std::min(count, integer(cT[region], "population") / 100);
    if (count <= 0)
        return Int(0);
    auto per = double(integer(cT[region], "gdp_usd")) /
               double(std::max<Int>(1, integer(cT[region], "population")));
    T[region]["population"] = integer(cT[region], "population") - count;
    T[region]["gdp_usd"] =
        std::max<Int>(0, integer(cT[region], "gdp_usd") - Int(std::llround(per * double(count))));
    actorDeaths[cT[region]["owner"].asString()] += count;
    return count;
}
void MonthlyStatus::warToll(const Front &f, std::map<std::string, Totals> &current) {
    auto &c = w.conflicts[f.id];
    c["intensity"] = f.intensity < 0    ? "none"
                     : f.intensity == 0 ? "low"
                     : f.intensity == 1 ? "medium"
                                        : "high";
    c["affected_regions"] = Json::arrayValue;
    for (auto &region : f.regions)
        c["affected_regions"].append(region);
    if (f.intensity < 0 || f.regions.empty()) {
        c["last_month_casualties"] = c["last_month_displaced"] = Int(0);
        return;
    }
    auto kind = c.get("kind", "war").asString();
    double multiplier = kind == "border_conflict" ? 0.4 : kind == "insurgency" ? 0.3 : 1.0;
    Int deaths = 0, displaced = 0;
    Int affectedPopulation = 0;
    for (auto &region : f.regions)
        affectedPopulation += integer(cT[region], "population");
    // Military losses grow with the front, capped by the population each party recruits from:
    // its whole country, or for a landless movement the front regions it fights in.
    for (auto &p : f.parties)
        deaths += partyLosses(f, p, current, affectedPopulation, multiplier, kind);
    for (auto &region : f.regions)
        displace(f, region, multiplier, deaths, displaced);
    for (auto &region : f.regions)
        T[region]["gdp_usd"] =
            Int(std::llround(double(integer(cT[region], "gdp_usd")) *
                             (1 - rate::frontGdpLoss[f.intensity] * multiplier)));
    for (auto &id : regionIds)
        if (f.parties.contains(cT[id]["owner"].asString()))
            T[id]["gdp_usd"] =
                Int(std::llround(double(integer(cT[id], "gdp_usd")) *
                                 (1 - rate::partyGdpDrag[f.intensity] * multiplier)));
    c["casualties"] = integer(c, "casualties") + deaths;
    c["displaced"] = integer(c, "displaced") + displaced;
    c["last_month_casualties"] = deaths;
    c["last_month_displaced"] = displaced;
    std::string where;
    for (size_t i = 0; i < f.regions.size() && i < 4; ++i)
        where += (i ? ", " : "") + cT[f.regions[i]]["name"].asString();
    if (f.regions.size() > 4)
        where += " and " + std::to_string(f.regions.size() - 4) + " more regions";
    r.events.push_back({*f.parties.begin(), "war_toll", f.id,
                        displayName(w, "conflict", f.id) + " (" + c["intensity"].asString() +
                            " intensity): about " + grouped(deaths) + " killed and " +
                            grouped(displaced) + " displaced this month in " + where + "."});
}
// One party's military losses on a front; returns the deaths they add to the conflict's toll.
Int MonthlyStatus::partyLosses(const Front &f, const std::string &p,
                               std::map<std::string, Totals> &current, Int affectedPopulation,
                               double multiplier, const std::string &kind) {
    auto pool = std::max(double(current[p].population), 0.25 * double(affectedPopulation));
    auto front = double(f.regions.size()) * rate::militaryPerFrontRegion[f.intensity] * multiplier *
                 jitter(w, f.id + ":military:" + p);
    auto losses = Int(std::llround(std::min(front, rate::militaryPopulationCap * pool)));
    const auto &m = static_cast<const WorldState &>(forceStart).actors[p]["status"]["military"];
    if (m.isObject()) {
        static constexpr double deathRate[]{0.0005, 0.004, 0.015};
        std::vector<std::string> affected;
        const auto &startConflict = forceStart.conflicts[f.id];
        for (const auto &region : startConflict["affected_regions"])
            affected.push_back(region.asString());
        if (affected.empty())
            for (const auto &region : startConflict["theatre"])
                affected.push_back(region.asString());
        // New wars use their declared front, but never this month's newly built or moved
        // forces.
        if (affected.empty())
            affected = f.regions;
        losses = Int(std::llround(force::committedPersonnel(forceStart, adjacency, p, affected) *
                                  deathRate[f.intensity] * multiplier *
                                  jitter(forceStart, f.id + ":military:" + p)));
        auto available = integer(w.actors[p]["status"]["military"], "active") +
                         integer(w.actors[p]["status"]["military"], "reserve");
        losses = std::min(losses, std::max<Int>(0, available - militaryLosses[p]));
        militaryLosses[p] += losses;
        bool enemyAircraft = false, enemyShips = false;
        for (const auto &enemy : f.parties)
            if (enemy != p) {
                const auto &other = forceStart.actors[enemy]["status"]["military"];
                enemyAircraft = enemyAircraft || integer(other, "aircraft") > 0;
                enemyShips = enemyShips || integer(other, "ships") > 0;
            }
        if (enemyAircraft)
            aircraftLosses[p] += Int(std::floor(number(m, "aircraft") * 0.30 *
                                                (f.intensity == 2   ? 0.01
                                                 : f.intensity == 1 ? 0.003
                                                                    : 0)));
        if (enemyShips)
            shipLosses[p] += Int(std::floor(number(m, "ships") * 0.30 *
                                            (f.intensity == 2   ? 0.005
                                             : f.intensity == 1 ? 0.0015
                                                                : 0)));
        if (f.intensity == 2 && (kind == "war" || kind == "civil_war"))
            mobilising.insert(p);
    }
    std::map<std::string, double> weights;
    for (auto &region : regionIds)
        if (cT[region]["owner"] == p && cT[region]["controller"] == p)
            weights[region] = double(integer(cT[region], "population"));
    if (weights.empty())
        for (auto &region : f.regions)
            if (cT[region]["controller"] == p)
                weights[region] = double(integer(cT[region], "population"));
    if (weights.empty())
        for (auto &region : f.regions)
            weights[region] = double(integer(cT[region], "population"));
    Int demographicLoss = 0;
    for (auto &[region, n] : distribute(losses, weights))
        demographicLoss += kill(region, n);
    return m.isObject() ? losses : demographicLoss;
}
// Civilian deaths in a front region, then flight to safe regions at home and across the border.
void MonthlyStatus::displace(const Front &f, const std::string &region, double multiplier,
                             Int &deaths, Int &displaced) {
    auto &t = T[region];
    auto population = double(integer(t, "population"));
    deaths += kill(region, Int(std::llround(population * rate::civilian[f.intensity] * multiplier *
                                            jitter(w, f.id + ":civilian:" + region))));
    auto remaining = double(integer(t, "population"));
    auto outflow = Int(std::llround(remaining * (rate::displacement[f.intensity] * multiplier *
                                                     jitter(w, f.id + ":displacement:" + region) +
                                                 seizedShare(region, remaining))));
    if (outflow <= 0)
        return;
    auto owner = cT[region]["owner"].asString();
    std::map<std::string, double> internal, abroad;
    for (auto &id : regionIds)
        if (id != region && cT[id]["owner"] == owner && cT[id]["controller"] == owner &&
            cT[id]["kind"] != "residual" && !allAffected.contains(id))
            internal[id] = double(integer(cT[id], "population")) + 1;
    if (adjacency.contains(region))
        for (auto &n : adjacency.at(region))
            if (cT.isMember(n) && cT[n]["kind"] != "residual" && cT[n]["owner"] != owner &&
                !f.parties.contains(cT[n]["controller"].asString()) && !allAffected.contains(n) &&
                cT[n].isMember("population"))
                abroad[n] = double(integer(cT[n], "population")) + 1;
    // With nowhere safe, people still flee to the rest of their country, front or not.
    if (internal.empty() && abroad.empty())
        for (auto &id : regionIds)
            if (id != region && cT[id]["owner"] == owner && cT[id]["controller"] == owner &&
                cT[id]["kind"] != "residual")
                internal[id] = double(integer(cT[id], "population")) + 1;
    if (internal.empty() && abroad.empty())
        return;
    auto refugees = internal.empty() ? outflow
                    : abroad.empty() ? Int(0)
                                     : Int(std::llround(double(outflow) * rate::refugeeShare));
    Int moved = 0;
    for (auto &[to, n] : distribute(refugees, abroad))
        moved += move(region, to, n);
    for (auto &[to, n] : distribute(outflow - refugees, internal))
        moved += move(region, to, n);
    t["displaced"] = integer(t, "displaced") + moved;
    displaced += moved;
}
} // namespace lh::detail
