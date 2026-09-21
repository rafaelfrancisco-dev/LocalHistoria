#include "localhistoria/politics.h"
#include "status/status_internal.h"
#include <algorithm>
#include <cmath>
namespace lh {
using namespace detail;
bool hasMilitary(const WorldState &w) {
    for (const auto &a : w.actors)
        if (!a["status"]["military"].isObject())
            return false;
    return !w.actors.empty();
}
void seedMilitary(WorldState &w, const StatusSeed &seed) {
    auto t = totals(w);
    for (const auto &a : w.actors.getMemberNames()) {
        auto &s = w.actors[a]["status"];
        if (s["military"].isObject())
            continue;
        const auto &row = seed.military[a];
        Json::Value m;
        int active = row.isObject()
                         ? row.get("active", 0).asInt()
                         : std::max(200, int(std::llround(double(t[a].population) * 0.003)));
        m["active"] = active;
        m["reserve"] = row.get("reserve", active / 2);
        m["ships"] = row.get("ships", 0);
        m["aircraft"] = row.get("aircraft", 0);
        m["estimated"] = row.get("estimated", 1);
        s["military"] = m;
        s["military_spend_usd"] = row.get("spend_usd", Int(0));
    }
    for (auto &e : w.entities) {
        if (e["kind"] == "army" && !e.isMember("personnel"))
            e["personnel"] = 10000;
        if (e["kind"] == "fleet" && !e.isMember("ships"))
            e["ships"] = 3;
        // Previously destroyed legacy units have already left the old simulation.
        if (e["status"] == "destroyed" && (e["kind"] == "army" || e["kind"] == "fleet"))
            e["loss_accounted"] = true;
    }
    // Legacy campaigns may have more abstract formations than their imported inventory can supply.
    // Reduce their allocations proportionally rather than invent additional national personnel or
    // ships.
    for (const auto &a : w.actors.getMemberNames())
        for (const auto *kind : {"army", "fleet"}) {
            const char *field = std::string(kind) == "army" ? "personnel" : "ships";
            const char *total = std::string(kind) == "army" ? "active" : "ships";
            std::map<std::string, double> weights;
            Int assigned = 0;
            for (const auto &id : w.entities.getMemberNames()) {
                const auto &e = w.entities[id];
                if (e["actor"] == a && e["kind"] == kind &&
                    (e["status"] == "active" || e["status"] == "under_construction")) {
                    weights[id] = number(e, field);
                    assigned += integer(e, field);
                }
            }
            auto available = integer(w.actors[a]["status"]["military"], total);
            if (assigned > available) {
                auto allocated = distribute(available, weights);
                for (const auto &[id, weight] : weights)
                    w.entities[id][field] = allocated[id];
            }
        }
}

Json::Value encode(const StatusSeed &s) {
    Json::Value j;
    j["countries"] = s.countries;
    j["regions"] = s.regions;
    j["world"] = s.world;
    j["military"] = s.military;
    return j;
}
StatusSeed decodeSeed(const Json::Value &j) {
    if (!j.isObject() || !j["countries"].isObject() || !j["regions"].isObject())
        throw ValidationError("Invalid status seed");
    return {j["countries"], j["regions"], j["world"],
            j.get("military", Json::Value(Json::objectValue))};
}
bool hasStatus(const WorldState &w) {
    for (auto &id : w.actors.getMemberNames())
        if (w.actors[id].isMember("status"))
            return true;
    return false;
}
void seedStatus(WorldState &w, const StatusSeed &seed, bool worldEvents) {
    auto statusFrom = [](const Json::Value &c) {
        Json::Value s;
        s["stability"] = round1(number(c, "stability", 50));
        s["baseline_stability"] = s["stability"];
        s["gdp_growth"] = std::round(number(c, "gdp_growth") * 100) / 100;
        s["pop_growth"] = std::round(number(c, "pop_growth") * 100) / 100;
        s["currency"] = c.get("currency", "USD");
        s["symbol"] = c.get("symbol", "US$");
        s["usd_rate"] = number(c, "usd_rate", 1);
        s["estimated"] = number(c, "estimated") != 0;
        return s;
    };
    for (auto &a : w.actors.getMemberNames())
        if (seed.countries.isMember(a))
            w.actors[a]["status"] = statusFrom(seed.countries[a]);
    for (auto &id : w.territories.getMemberNames()) {
        auto &t = w.territories[id];
        t["population"] =
            seed.regions.isMember(id) ? integer(seed.regions[id], "population") : Int(0);
        t["gdp_usd"] = seed.regions.isMember(id) ? integer(seed.regions[id], "gdp_usd") : Int(0);
        t["displaced"] = Int(0);
    }
    if (!worldEvents || !seed.world.isObject()) {
        if (!seed.military.empty())
            seedMilitary(w, seed);
        return;
    }
    for (auto &a : seed.world["actors"].getMemberNames()) {
        auto &v = seed.world["actors"][a];
        auto base = v["base"].asString();
        w.actors[a]["name"] = v["name"];
        w.actors[a]["color"] = v["color"];
        if (seed.countries.isMember(base)) {
            auto s = statusFrom(seed.countries[base]);
            s["stability"] = s["baseline_stability"] = 25.0;
            s["gdp_growth"] = s["pop_growth"] = 0.0;
            w.actors[a]["status"] = s;
        }
    }
    int n = 0;
    for (auto &c : seed.world["conflicts"]) {
        Json::Value v;
        for (auto k : {"name", "kind", "actors", "text", "started_turn", "affected_regions"})
            v[k] = c[k];
        v["status"] = "open";
        v["set_intensity"] = v["intensity"] = c["intensity"];
        v["theatre"] = c["affected_regions"];
        v["casualties"] = v["displaced"] = Int(0);
        for (auto &occupier : c["occupied"].getMemberNames())
            for (auto &r : c["occupied"][occupier])
                if (w.territories.isMember(r.asString())) {
                    // Long-standing occupations already did their economic damage before 2026.
                    auto &t = w.territories[r.asString()];
                    t["controller"] = occupier;
                    t["occupation_gdp"] =
                        Int(std::llround(double(integer(t, "gdp_usd")) / rate::occupationGdpFloor));
                }
        w.conflicts["c0-" + std::to_string(++n)] = v;
        int unit = 0;
        for (auto e : c["entities"]) {
            e["kind"] = "army";
            e["status"] = "active";
            e["origin_event"] = "seed";
            auto region = e["region"].asString();
            e["longitude"] = w.territories[region].get("longitude", 0);
            e["latitude"] = w.territories[region].get("latitude", 0);
            w.entities["military-seed-" + std::to_string(n) + "-" + std::to_string(++unit)] = e;
        }
    }
    n = 0;
    for (auto &c : seed.world["commitments"]) {
        Json::Value v;
        for (auto k : {"name", "kind", "actors", "text", "started_turn"})
            v[k] = c[k];
        v["status"] = "open";
        w.commitments["k0-" + std::to_string(++n)] = v;
    }
    if (!seed.military.empty())
        seedMilitary(w, seed);
}
} // namespace lh
