#include "localhistoria/conflicts.h"
#include "status/status_internal.h"
#include <algorithm>
#include <climits>
#include <cmath>
namespace lh {
using namespace detail;
namespace force {
namespace {
const Json::Value &inventory(const WorldState &w, const std::string &a) {
    return w.actors[a]["status"]["military"];
}
bool mobilised(const WorldState &w, const std::string &a) {
    for (const auto &c : w.conflicts)
        if (isOpen(c) && (c["kind"] == "war" || c["kind"] == "civil_war") && isParty(c, a)) {
            if (!realismRules(w))
                return true;
            for (const auto &p : c["actors"])
                if (fightingPair(w, c, a, p.asString()))
                    return true;
        }
    return false;
}
int uncommitted(const WorldState &w, const std::string &a, const char *kind, const char *field,
                const char *total, int legacy) {
    Int n = integer(inventory(w, a), total);
    for (const auto &e : w.entities)
        if (e["actor"] == a && e["kind"] == kind &&
            (e["status"] == "active" || e["status"] == "under_construction"))
            n -= e.get(field, legacy).asInt64();
    return int(std::clamp<Int>(n, 0, INT_MAX));
}
bool landless(const WorldState &w, const std::string &a) {
    for (const auto &t : w.territories)
        if (t["owner"] == a || t["controller"] == a)
            return false;
    return true;
}
} // namespace
int uncommittedPersonnel(const WorldState &w, const std::string &a) {
    return uncommitted(w, a, "army", "personnel", "active", 10000);
}
int uncommittedShips(const WorldState &w, const std::string &a) {
    return uncommitted(w, a, "fleet", "ships", "ships", 3);
}
double quality(const WorldState &w, const std::string &a) {
    Int population = 0, gdp = 0;
    for (const auto &t : w.territories)
        if (t["owner"] == a) {
            population += integer(t, "population");
            gdp += integer(t, "gdp_usd");
        }
    // The spread is deliberately narrow: wealth buys better equipment and training, but it does
    // not outweigh mass. A wider range let a rich mid-sized force outrank the largest armies.
    return std::clamp(std::sqrt(population > 0 ? double(gdp) / double(population) / 20000 : 0), 0.6,
                      1.4);
}
int constructionCapacity(const WorldState &w, const std::string &a) {
    double gdp = 0;
    for (const auto &t : w.territories)
        if (t["owner"] == a)
            gdp += number(t, "gdp_usd");
    return std::max(1, int(std::min(double(INT_MAX), std::floor(gdp / 150e9))));
}
int fronts(const WorldState &w, const Adjacency &adj, const std::string &a) {
    std::set<std::string> enemies;
    for (const auto &c : w.conflicts)
        if (isOpen(c) && isParty(c, a))
            for (const auto &p : c["actors"])
                if (p != a && (!realismRules(w) || fightingPair(w, c, a, p.asString())))
                    enemies.insert(p.asString());
    // Neighbours are not enemies simply because they share a peaceful border.
    for (const auto &[r, neighbours] : adj) {
        const auto &t = w.territories[r];
        if (t["controller"] != a)
            continue;
        for (const auto &n : neighbours) {
            const auto &other = w.territories[n];
            auto p = other["controller"].asString();
            if (!p.empty() && p != a && ((other["owner"] == a) || (t["owner"] == p)))
                enemies.insert(p);
        }
    }
    return std::max(1, int(enemies.size()));
}
double committedPersonnel(const WorldState &w, const Adjacency &adj, const std::string &a,
                          const std::vector<std::string> &regions) {
    if (landless(w, a))
        return (mobilised(w, a) ? 0.30 : 0.20) * number(inventory(w, a), "active") /
               fronts(w, adj, a);
    double n = (mobilised(w, a) ? 0.30 : 0.20) * uncommittedPersonnel(w, a) / fronts(w, adj, a);
    for (const auto &e : w.entities)
        if (e["actor"] == a && e["kind"] == "army" && e["status"] == "active")
            for (const auto &r : regions)
                if (adjacent(adj, e["region"].asString(), r)) {
                    n += e.get("personnel", 10000).asInt();
                    break;
                }
    return std::min(n, number(inventory(w, a), "active"));
}
Json::Value committedForce(const WorldState &w, const Adjacency &adj, const std::string &a,
                           const std::string &region, bool defending) {
    const auto &m = inventory(w, a);
    int count = defending ? fronts(w, adj, a) : 1;
    bool noLand = landless(w, a),
         naval = !noLand && w.territories[region].get("coastal", false).asBool();
    double land = 0;
    if (!noLand)
        for (const auto &e : w.entities)
            if (e["status"] == "active") {
                auto at = e["region"].asString();
                if (at == region &&
                    (e["kind"] == "port" || e["kind"] == "fleet" || e.get("port", false).asBool()))
                    naval = true;
                if (e["actor"] != a || !adjacent(adj, at, region))
                    continue;
                if (e["kind"] == "army")
                    land += e.get("personnel", 10000).asInt();
                if (e["kind"] == "fleet")
                    naval = true;
            }
    auto share = defending ? (mobilised(w, a) ? 0.40 : 0.25) : (mobilised(w, a) ? 0.30 : 0.20);
    land += share * (noLand ? number(m, "active") : uncommittedPersonnel(w, a)) / count;
    // Reservists hold ground they already live on; they are not an expeditionary pool, so they
    // count only in defence. Without this a nation whose strength is mostly its reserve reads as
    // defenceless.
    if (defending)
        land += rate::reserveDefence * number(m, "reserve") / count;
    double air = (defending ? 0.50 : 0.30) * number(m, "aircraft") * 200 / count;
    double sea = naval ? 0.30 * number(m, "ships") * 1500 : 0;
    double cp = quality(w, a) * (land + air + sea) * (defending ? 1.5 : 1);
    if (defending) {
        for (const auto &c : w.conflicts)
            if (isOpen(c) && c["kind"] == "civil_war" && isParty(c, a)) {
                cp *= 0.6;
                break;
            }
        if (number(w.actors[a]["status"], "stability", 50) < 25)
            cp *= 0.8;
    }
    Json::Value out;
    out["land"] = land;
    out["air"] = air;
    out["sea"] = sea;
    out["cp"] = cp;
    return out;
}
Json::Value assault(const WorldState &w, const Adjacency &adj, const std::string &a,
                    const std::string &d, const std::string &r) {
    double attack = committedForce(w, adj, a, r, false)["cp"].asDouble();
    double defend = committedForce(w, adj, d, r, true)["cp"].asDouble();
    for (const auto &c : w.conflicts)
        if (isOpen(c) && isParty(c, a) && isParty(c, d) && c["started_turn"].isInt() &&
            w.turn - c["started_turn"].asInt() >= 12) {
            attack *= 0.6;
            break;
        }
    int cap = attack <= 0                             ? 0
              : defend <= 0 || attack >= 1.5 * defend ? 2
              : attack >= 0.75 * defend               ? 1
                                                      : 0;
    Json::Value out;
    out["attack"] = attack;
    out["defend"] = defend;
    out["capacity"] = cap;
    return out;
}
int assaultCapacity(const WorldState &w, const Adjacency &adj, const std::string &a,
                    const std::string &d, const std::string &r) {
    return assault(w, adj, a, d, r)["capacity"].asInt();
}
std::string militaryLabel(const WorldState &w, const std::string &a) {
    const auto &m = inventory(w, a);
    auto cp = quality(w, a) *
              (number(m, "active") + number(m, "aircraft") * 200 + number(m, "ships") * 1500);
    return cp < 20000     ? "negligible"
           : cp < 150000  ? "minor"
           : cp < 750000  ? "regional power"
           : cp < 2000000 ? "major power"
                          : "superpower";
}
std::string reachDescription(const WorldState &w, const Adjacency &adj, const std::string &a,
                             const std::string &r) {
    if (!inventory(w, a).isObject())
        return a;
    auto d = w.territories[r]["controller"].asString();
    if (d.empty())
        d = w.territories[r]["owner"].asString();
    if (d == a)
        return a + " (controlled)";
    auto f = assault(w, adj, a, d, r);
    int cap = f["capacity"].asInt();
    return a + " (" +
           (cap == 2   ? "superior"
            : cap == 1 ? "limited"
                       : "too weak") +
           ": about " + grouped(Int(std::llround(f["attack"].asDouble() / 1000)) * 1000) +
           " effective strength against " +
           grouped(Int(std::llround(f["defend"].asDouble() / 1000)) * 1000) +
           (cap == 2   ? "; up to 2 regions)"
            : cap == 1 ? "; 1 region)"
                       : ")");
}
std::string militarySummary(const WorldState &w, const std::string &a) {
    const auto &m = inventory(w, a);
    std::string out = "Active " + people(integer(m, "active")) + " (" +
                      people(uncommittedPersonnel(w, a)) + " uncommitted), reserve " +
                      people(integer(m, "reserve")) + ", " + grouped(integer(m, "ships")) +
                      " major warships, " + grouped(integer(m, "aircraft")) + " combat aircraft; " +
                      militaryLabel(w, a);
    for (const auto &e : w.entities)
        if (e["actor"] == a && e["status"] == "active" &&
            (e["kind"] == "army" || e["kind"] == "fleet"))
            out += "; " + e["name"].asString() + " at " +
                   w.territories[e["region"].asString()]["name"].asString() + ": " +
                   grouped(e.get(e["kind"] == "army" ? "personnel" : "ships",
                                 e["kind"] == "army" ? 10000 : 3)
                               .asInt64()) +
                   (e["kind"] == "army" ? " troops" : " ships");
    return out;
}
} // namespace force
} // namespace lh
