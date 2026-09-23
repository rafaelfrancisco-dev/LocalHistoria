#include "localhistoria/conflicts.h"
#include "status/status_internal.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <tuple>
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
    double population = 0, gdp = 0;
    // Under the realism rules only output the nation actually commands counts: not land it has
    // lost, and conquests only as far as they are integrated.
    if (realismRules(w))
        std::tie(gdp, population) = effectiveOutput(w, a);
    else
        for (const auto &t : w.territories)
            if (t["owner"] == a) {
                population += double(integer(t, "population"));
                gdp += double(integer(t, "gdp_usd"));
            }
    // The spread is deliberately narrow: wealth buys better equipment and training, but it does
    // not outweigh mass. A wider range let a rich mid-sized force outrank the largest armies.
    return std::clamp(std::sqrt(population > 0 ? gdp / population / 20000 : 0), 0.6, 1.4);
}
int constructionCapacity(const WorldState &w, const std::string &a) {
    double gdp = 0;
    if (realismRules(w))
        gdp = effectiveOutput(w, a).first;
    else
        for (const auto &t : w.territories)
            if (t["owner"] == a)
                gdp += number(t, "gdp_usd");
    return std::max(1, int(std::min(double(INT_MAX), std::floor(gdp / 150e9))));
}
namespace {
// Whether two enemies can meet at all: land they hold borders, an active army stands in or next
// to the other's land, a fleet's routes reach its coast, or a seeded theatre already has them
// fighting. A war declared on a nation nobody can reach claims none of its forces.
bool contact(const WorldState &w, const Adjacency &adj, const Json::Value &c, const std::string &a,
             const std::string &b) {
    auto held = [&](const std::string &r, const std::string &x) {
        const auto &t = w.territories[r];
        return t["controller"] == x && t["kind"] != "residual";
    };
    bool theatreA = false, theatreB = false;
    for (const auto &r : c["theatre"]) {
        theatreA = theatreA || held(r.asString(), a);
        theatreB = theatreB || held(r.asString(), b);
    }
    if (theatreA && theatreB)
        return true;
    for (const auto &[r, neighbours] : adj) {
        bool ofA = held(r, a);
        if (!ofA && !held(r, b))
            continue;
        for (const auto &n : neighbours)
            if (held(n, ofA ? b : a))
                return true;
    }
    for (const auto &e : w.entities) {
        auto x = e["actor"].asString();
        if ((x != a && x != b) || e["status"] != "active")
            continue;
        auto other = x == a ? b : a;
        auto at = e["region"].asString();
        if (held(at, other))
            return true;
        if (e["kind"] == "army") {
            if (auto it = adj.find(at); it != adj.end())
                for (const auto &n : it->second)
                    if (held(n, other))
                        return true;
        } else if (e["kind"] == "fleet") {
            if (auto it = adj.sea.find(at); it != adj.sea.end())
                for (const auto &[to, km] : it->second)
                    if (km <= fleetRangeKm && held(to, other))
                        return true;
        }
    }
    return false;
}
} // namespace
std::set<std::string> engagements(const WorldState &w, const Adjacency &adj, const std::string &a) {
    std::set<std::string> enemies;
    for (const auto &c : w.conflicts)
        if (isOpen(c) && isParty(c, a))
            for (const auto &p : c["actors"])
                if (p != a && !enemies.contains(p.asString()) &&
                    (!realismRules(w) ||
                     (fightingPair(w, c, a, p.asString()) && contact(w, adj, c, a, p.asString()))))
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
    return enemies;
}
int fronts(const WorldState &w, const Adjacency &adj, const std::string &a) {
    return std::max(1, int(engagements(w, adj, a).size()));
}
namespace {
// The engagements an actor divides its national pool among when it faces `opponent`: everyone it
// is already fighting or disputing ground with, and the opponent itself.
std::set<std::string> engagedWith(const WorldState &w, const Adjacency &adj, const std::string &a,
                                  const std::string &opponent) {
    auto enemies = engagements(w, adj, a);
    if (!opponent.empty() && opponent != a)
        enemies.insert(opponent);
    return enemies;
}
// The share of an army that one engagement can draw on: an army facing several enemies at once
// splits evenly between them rather than fighting each battle at full strength. An army facing no
// one is a garrison and serves wherever it stands.
double armyShare(const WorldState &w, const Adjacency &adj, const std::set<std::string> &enemies,
                 const Json::Value &army, const std::string &opponent) {
    std::set<std::string> facing;
    if (!opponent.empty())
        facing.insert(opponent);
    auto at = army["region"].asString();
    auto consider = [&](const std::string &region) {
        auto holder = w.territories[region]["controller"].asString();
        if (enemies.contains(holder))
            facing.insert(holder);
    };
    consider(at);
    if (auto it = adj.find(at); it != adj.end())
        for (const auto &n : it->second)
            consider(n);
    return facing.empty() ? 1.0 : 1.0 / double(facing.size());
}
} // namespace
double engaged(const WorldState &w, const Adjacency &adj, const std::string &a,
               const std::vector<std::string> &regions, const std::string &opponent,
               std::map<std::string, double> *parts) {
    const auto &m = inventory(w, a);
    if (!m.isObject())
        return 0;
    auto enemies = engagedWith(w, adj, a, opponent);
    double count = std::max<double>(1, double(enemies.size()));
    bool noLand = landless(w, a);
    double pool = (mobilised(w, a) ? 0.30 : 0.20) *
                  (noLand ? number(m, "active") : double(uncommittedPersonnel(w, a))) / count;
    double total = pool;
    if (parts)
        (*parts)[""] += pool;
    if (!noLand)
        for (auto it = w.entities.begin(); it != w.entities.end(); ++it) {
            const auto &e = *it;
            if (e["actor"] != a || e["kind"] != "army" || e["status"] != "active")
                continue;
            bool near = false;
            for (const auto &r : regions)
                near = near || adjacent(adj, e["region"].asString(), r);
            if (!near)
                continue;
            double n = armyShare(w, adj, enemies, e, opponent) * e.get("personnel", 10000).asInt();
            total += n;
            if (parts)
                (*parts)[it.name()] += n;
        }
    return std::min(total, number(m, "active"));
}
double committedPersonnel(const WorldState &w, const Adjacency &adj, const std::string &a,
                          const std::vector<std::string> &regions, const std::string &opponent) {
    if (realismRules(w))
        return engaged(w, adj, a, regions, opponent);
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
                           const std::string &region, bool defending, const std::string &opponent) {
    const auto &m = inventory(w, a);
    const bool realism = realismRules(w);
    // Under the realism rules attack and defence draw on the same allocation: the pool, the air
    // arm and the fleet divided across every engagement including this one, and each army split
    // among the enemies it faces. A second front therefore costs offensive strength too.
    std::set<std::string> enemies;
    if (realism)
        enemies = engagedWith(w, adj, a, opponent);
    int count = realism ? std::max(1, int(enemies.size())) : defending ? fronts(w, adj, a) : 1;
    bool noLand = landless(w, a),
         naval = !noLand && w.territories[region].get("coastal", false).asBool();
    // National reinforcement arrives over supplied land: an attacker needs a supplied region on
    // the target's border, a defender a supplied region to defend. The full pool never sails.
    double pool = 1;
    if (realism && !noLand) {
        bool linked = false;
        if (defending)
            linked = supplied(w, adj, a, region);
        else if (auto at = adj.find(region); at != adj.end())
            for (const auto &n : at->second)
                if (w.territories[n]["controller"] == a && w.territories[n]["kind"] != "residual" &&
                    supplied(w, adj, a, n)) {
                    linked = true;
                    break;
                }
        pool = linked ? 1 : defending ? rate::isolatedSupport : 0;
    }
    double land = 0;
    if (!noLand) {
        std::set<std::string> counted;
        for (const auto &id : w.entities.getMemberNames()) {
            const auto &e = w.entities[id];
            if (e["status"] != "active")
                continue;
            auto at = e["region"].asString();
            if (at == region &&
                (e["kind"] == "port" || e["kind"] == "fleet" || e.get("port", false).asBool()))
                naval = true;
            if (e["actor"] != a)
                continue;
            if (e["kind"] == "army" && adjacent(adj, at, region)) {
                auto support = !realism ? 1.0
                                        : (supplied(w, adj, a, at) ? 1.0 : rate::isolatedSupport) *
                                              armyShare(w, adj, enemies, e, opponent);
                land += support * e.get("personnel", 10000).asInt();
                counted.insert(id);
            }
            auto km = realism ? seaKm(adj, at, region) : -1;
            if (e["kind"] == "fleet" &&
                (realism ? km >= 0 && km <= fleetRangeKm : adjacent(adj, at, region)))
                naval = true;
        }
        // Overseas, only troops already embarked count: armies sharing a region with a fleet whose
        // routes reach the target, up to what its warships can lift.
        if (realism && !defending && w.territories[region].get("coastal", false).asBool())
            for (const auto &f : w.entities)
                if (f["actor"] == a && f["kind"] == "fleet" && f["status"] == "active") {
                    auto from = f["region"].asString();
                    auto km = seaKm(adj, from, region);
                    if (km < 0 || km > fleetRangeKm)
                        continue;
                    double lift = double(f.get("ships", 3).asInt()) * troopsPerShip;
                    for (const auto &id : w.entities.getMemberNames()) {
                        const auto &e = w.entities[id];
                        if (lift <= 0 || e["actor"] != a || e["kind"] != "army" ||
                            e["status"] != "active" || e["region"] != from ||
                            !counted.insert(id).second)
                            continue;
                        double carried =
                            std::min(lift, armyShare(w, adj, enemies, e, opponent) *
                                               double(e.get("personnel", 10000).asInt()));
                        lift -= carried;
                        land += carried * (supplied(w, adj, a, from) ? 1.0 : rate::isolatedSupport);
                    }
                }
    }
    auto share = defending ? (mobilised(w, a) ? 0.40 : 0.25) : (mobilised(w, a) ? 0.30 : 0.20);
    land += pool * share * (noLand ? number(m, "active") : uncommittedPersonnel(w, a)) / count;
    // Reservists hold ground they already live on; they are not an expeditionary pool, so they
    // count only in defence. Without this a nation whose strength is mostly its reserve reads as
    // defenceless.
    if (defending)
        land += rate::reserveDefence * number(m, "reserve") / count;
    // Carrier aviation is not modelled: air power follows the same supplied land as the pool.
    double air = (realism && !noLand ? pool : 1) * (defending ? 0.50 : 0.30) *
                 number(m, "aircraft") * 200 / count;
    double sea = naval ? 0.30 * number(m, "ships") * 1500 / (realism ? count : 1) : 0;
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
namespace {
double km(const Json::Value &a, const Json::Value &b) {
    constexpr double rad = 3.14159265358979323846 / 180;
    double p1 = number(a, "latitude") * rad, p2 = number(b, "latitude") * rad;
    double dl = (number(b, "longitude") - number(a, "longitude")) * rad;
    double h = std::pow(std::sin((p2 - p1) / 2), 2) +
               std::cos(p1) * std::cos(p2) * std::pow(std::sin(dl / 2), 2);
    return 12742 * std::asin(std::sqrt(std::min(1.0, h)));
}
} // namespace
double captureEffort(const WorldState &w, const Adjacency &adj, const std::string &a,
                     const std::string &region) {
    const auto &t = w.territories[region];
    std::vector<double> spacing;
    if (auto at = adj.find(region); at != adj.end())
        for (const auto &n : at->second) {
            const auto &other = w.territories[n];
            if (other.isMember("longitude") && t.isMember("longitude"))
                spacing.push_back(km(t, other));
        }
    double extent = rate::effortExtentKm;
    if (!spacing.empty()) {
        std::sort(spacing.begin(), spacing.end());
        size_t mid = spacing.size() / 2;
        extent = spacing.size() % 2 ? spacing[mid] : (spacing[mid - 1] + spacing[mid]) / 2;
    }
    double size = std::clamp(std::sqrt(extent / rate::effortExtentKm), 0.6, 2.0);
    double people = number(t, "population");
    double crowd = people > 0 ? std::clamp(std::pow(people / 1e6, 0.1), 0.7, 1.4) : 1.0;
    double reach = 1;
    auto home = heartland(w, a);
    if (!home.empty() && t.isMember("longitude"))
        reach += std::clamp((km(w.territories[home], t) - rate::effortSupplyKm) /
                                rate::effortSupplyScaleKm,
                            0.0, 0.5);
    return size * crowd * reach;
}
double exhaustionFactor(const WorldState &w, const std::string &a) {
    return 1 - rate::exhaustionCap * std::clamp(number(inventory(w, a), "exhaustion"), 0.0, 1.0);
}
Json::Value assault(const WorldState &w, const Adjacency &adj, const std::string &a,
                    const std::string &d, const std::string &r) {
    double attack = committedForce(w, adj, a, r, false, d)["cp"].asDouble();
    double defend = committedForce(w, adj, d, r, true, a)["cp"].asDouble();
    // Under the realism rules one threshold decides every seizure, ordered or not: decisive
    // superiority scaled by the effort the region takes, and worn down by exhaustion rather than by
    // a war's first anniversary.
    if (realismRules(w)) {
        attack *= exhaustionFactor(w, a);
        double required = rate::pressureFloor * captureEffort(w, adj, a, r);
        double ratio = defend > 0 ? attack / defend : attack > 0 ? rate::pressureRatioCap : 0;
        Json::Value out;
        out["attack"] = attack;
        out["defend"] = defend;
        out["required"] = required;
        out["ratio"] = ratio / required * rate::pressureFloor;
        out["capacity"] = attack <= 0 ? 0 : ratio >= 2 * required ? 2 : ratio >= required ? 1 : 0;
        return out;
    }
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
