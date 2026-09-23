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
// Under the realism rules every movement goes through here: the people who leave take their
// place in the ledger with them. Residents of `from` become displaced from it; people it was
// hosting move on still displaced from their own origin, not counted twice; anyone arriving back
// at their own origin has returned.
Int MonthlyStatus::flee(const std::string &from, const std::string &to, Int count) {
    auto population = integer(cT[from], "population");
    count = std::min(count, population);
    if (count <= 0)
        return Int(0);
    auto parts = cohorts(from, count, population);
    move(from, to, count);
    forget(from, parts);
    for (const auto &[origin, n] : parts) {
        auto home = origin.empty() ? from : origin;
        if (home != to && n > 0)
            hosted[to][home] += n;
    }
    return count;
}
std::map<std::string, Int> MonthlyStatus::cohorts(const std::string &region, Int count,
                                                  Int population) const {
    std::map<std::string, double> weights;
    Int guests = 0;
    if (auto at = hosted.find(region); at != hosted.end())
        for (const auto &[origin, n] : at->second)
            if (n > 0) {
                weights[origin] = double(n);
                guests += n;
            }
    if (guests == 0)
        return {{"", count}};
    weights[""] = double(std::max<Int>(0, population - guests));
    auto parts = distribute(std::min(count, std::max(population, guests)), weights);
    // A share is never larger than the cohort it comes from.
    Int over = 0;
    for (auto &[origin, n] : parts)
        if (!origin.empty() && n > Int(weights[origin])) {
            over += n - Int(weights[origin]);
            n = Int(weights[origin]);
        }
    parts[""] += over;
    return parts;
}
void MonthlyStatus::forget(const std::string &host, const std::map<std::string, Int> &gone) {
    auto at = hosted.find(host);
    if (at == hosted.end())
        return;
    for (const auto &[origin, n] : gone)
        if (!origin.empty() && at->second.contains(origin))
            at->second[origin] = std::max<Int>(0, at->second[origin] - n);
}
Int MonthlyStatus::kill(const std::string &region, Int count) {
    count = std::min(count, integer(cT[region], "population") / 100);
    if (count <= 0)
        return Int(0);
    // The dead include hosted people in proportion, so the ledger never outnumbers the living.
    if (realism())
        forget(region, cohorts(region, count, integer(cT[region], "population")));
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
    int intensity = realism() ? std::max(level(c.get("intensity", "none").asString()), f.intensity)
                              : f.intensity;
    c["intensity"] = intensity < 0    ? "none"
                     : intensity == 0 ? "low"
                     : intensity == 1 ? "medium"
                                      : "high";
    if (!realism())
        c["affected_regions"] = Json::arrayValue;
    for (auto &region : f.regions)
        if (std::find(c["affected_regions"].begin(), c["affected_regions"].end(),
                      Json::Value(region)) == c["affected_regions"].end())
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
    c["last_month_casualties"] = (realism() ? integer(c, "last_month_casualties") : 0) + deaths;
    c["last_month_displaced"] = (realism() ? integer(c, "last_month_displaced") : 0) + displaced;
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
    auto front =
        double(f.regions.size()) * rate::militaryPerFrontRegion[f.intensity] * multiplier *
        jitter(w, (realism() ? hostilePairKey(*f.parties.begin(), *f.parties.rbegin()) : f.id) +
                      ":military:" + p);
    auto losses = Int(std::llround(std::min(front, rate::militaryPopulationCap * pool)));
    const auto &m = static_cast<const WorldState &>(forceStart).actors[p]["status"]["military"];
    if (m.isObject() && realism())
        losses = realismLosses(f, p, multiplier, kind);
    else if (m.isObject()) {
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
        losses = Int(std::llround(
            force::committedPersonnel(forceStart, adjacency, p, affected) * deathRate[f.intensity] *
            multiplier *
            jitter(forceStart,
                   (realism() ? hostilePairKey(*f.parties.begin(), *f.parties.rbegin()) : f.id) +
                       ":military:" + p)));
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
// What one side of a pair's front puts in harm's way. Aircraft fly from supplied ground on or next
// to the front, at the pool's share for this engagement. Ships are exposed where a fleet lies
// within sailing range of a coastal front region, and the uncommitted fleet only off its own
// front-line coast: a navy on the other side of the world is not at risk.
MonthlyStatus::Exposure MonthlyStatus::exposure(const std::string &p, const std::string &q,
                                                const Front &f) const {
    Exposure out;
    const auto &world = static_cast<const WorldState &>(forceStart);
    const auto &m = world.actors[p]["status"]["military"];
    if (!m.isObject())
        return out;
    auto engaged = force::engagements(world, adjacency, p);
    engaged.insert(q);
    double count = double(engaged.size());
    bool landless = true, based = false, homeCoast = false;
    for (auto it = world.territories.begin(); it != world.territories.end() && landless; ++it)
        landless = (*it)["controller"] != p && (*it)["owner"] != p;
    for (const auto &region : f.regions) {
        std::vector<std::string> near{region};
        if (auto at = adjacency.find(region); at != adjacency.end())
            near.insert(near.end(), at->second.begin(), at->second.end());
        for (const auto &n : near)
            if (!based && world.territories[n]["controller"] == p &&
                supplied(world, adjacency, p, n))
                based = true;
        const auto &t = world.territories[region];
        homeCoast = homeCoast || (t["controller"] == p && t.get("coastal", false).asBool());
    }
    if (based || landless)
        out.air = 0.30 * number(m, "aircraft") / count;
    for (auto it = world.entities.begin(); it != world.entities.end(); ++it) {
        const auto &e = *it;
        if (e["actor"] != p || e["kind"] != "fleet" || e["status"] != "active")
            continue;
        for (const auto &region : f.regions) {
            auto km = seaKm(adjacency, e["region"].asString(), region);
            if (world.territories[region].get("coastal", false).asBool() && km >= 0 &&
                km <= fleetRangeKm) {
                out.fleets[it.name()] = number(e, "ships", 3);
                out.ships += number(e, "ships", 3);
                break;
            }
        }
    }
    if (homeCoast) {
        double pool = 0.30 * force::uncommittedShips(world, p) / count;
        out.fleets[""] = pool;
        out.ships += pool;
    }
    return out;
}
// One side's losses on a pair's front under the realism rules, kept fractional until the month's
// inventories are settled. Personnel losses fall on the forces engaged against this enemy: the
// armies next to the front at their share and the pool's share, never an army still assembling.
// Returns the rounded deaths for this front's toll.
Int MonthlyStatus::realismLosses(const Front &f, const std::string &p, double multiplier,
                                 const std::string &kind) {
    static constexpr double deathRate[]{0.0005, 0.004, 0.015};
    // Aircraft face enemy aircraft at the full rate and ground fire alone at half; ships face any
    // enemy warship or aircraft in reach. Low intensity costs no equipment.
    static constexpr double airRate[]{0, 0.003, 0.01}, shipRate[]{0, 0.0015, 0.005};
    std::string q;
    for (const auto &party : f.parties)
        if (party != p)
            q = party;
    std::map<std::string, double> parts;
    double exposed = force::engaged(forceStart, adjacency, p, f.regions, q, &parts);
    auto key = hostilePairKey(*f.parties.begin(), *f.parties.rbegin()) + ":military:" + p;
    double loss = exposed * deathRate[f.intensity] * multiplier * jitter(forceStart, key);
    const auto &now = w.actors[p]["status"]["military"];
    double available = number(now, "active") + number(now, "reserve") - personnelLoss[p];
    loss = std::clamp(loss, 0.0, std::max(0.0, available));
    for (const auto &[id, n] : parts)
        if (exposed > 0)
            personnelWeights[p][id] += loss * n / exposed;
    personnelLoss[p] += loss;
    auto mine = exposure(p, q, f), theirs = exposure(q, p, f);
    if (mine.air > 0)
        aircraftLoss[p] += mine.air * airRate[f.intensity] * (theirs.air > 0 ? 1 : 0.5);
    if (mine.ships > 0 && (theirs.ships > 0 || theirs.air > 0))
        for (const auto &[id, n] : mine.fleets) {
            shipLoss[p] += n * shipRate[f.intensity];
            shipWeights[p][id] += n * shipRate[f.intensity];
        }
    if (f.intensity == 2 && (kind == "war" || kind == "civil_war"))
        mobilising.insert(p);
    return Int(std::llround(loss));
}
// Civilian deaths in a front region, then flight to safe regions at home and across the border.
void MonthlyStatus::displace(const Front &f, const std::string &region, double multiplier,
                             Int &deaths, Int &displaced) {
    auto &t = T[region];
    auto population = double(integer(t, "population"));
    deaths += kill(
        region,
        Int(std::llround(
            population * rate::civilian[f.intensity] * multiplier *
            jitter(w, (realism() ? hostilePairKey(*f.parties.begin(), *f.parties.rbegin()) : f.id) +
                          ":civilian:" + region))));
    auto remaining = double(integer(t, "population"));
    auto outflow = Int(std::llround(
        remaining *
        (rate::displacement[f.intensity] * multiplier *
             jitter(w,
                    (realism() ? hostilePairKey(*f.parties.begin(), *f.parties.rbegin()) : f.id) +
                        ":displacement:" + region) +
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
        moved += realism() ? flee(region, to, n) : move(region, to, n);
    for (auto &[to, n] : distribute(outflow - refugees, internal))
        moved += realism() ? flee(region, to, n) : move(region, to, n);
    // Under the realism rules the outstanding count is derived from the ledger at the end of the
    // month; the conflict keeps counting every movement.
    if (!realism())
        t["displaced"] = integer(t, "displaced") + moved;
    displaced += moved;
}
} // namespace lh::detail
