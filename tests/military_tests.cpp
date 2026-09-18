#include "localhistoria/status.h"
#include "localhistoria/store.h"
#include <cmath>
#include <iostream>
using namespace lh;
static void check(bool value, const std::string &message) { if (!value) throw std::runtime_error(message); }
static Json::Value event(const std::string &actor, const Json::Value &ops) {
    Json::Value e; e["actor"] = actor; e["description"] = "Military test"; e["blocked_by"] = "none"; e["background"] = false; e["causes"] = Json::arrayValue; e["operations"] = ops;
    Json::Value response; response["events"].append(e); return response;
}
static WorldState fixture(int active = 15000, double gdp = 1e12) {
    WorldState w; w.campaignId = "military"; w.player = "A"; w.turn = 1;
    w.actors = parse(R"({"A":{"name":"Alpha","color":"#777777"},"D":{"name":"Defender","color":"#888888"}})");
    for (auto a : {"A", "D"}) {
        auto &s = w.actors[a]["status"]; s["stability"] = s["baseline_stability"] = 50;
        s["military"] = parse(R"({"active":15000,"reserve":10000,"ships":4,"aircraft":100,"estimated":0})");
        auto &t = w.territories[a]; t["name"] = a; t["owner"] = t["controller"] = t["initial_affiliation"] = a;
        t["population"] = 1000000; t["gdp_usd"] = Json::Int64(gdp); t["longitude"] = t["latitude"] = 1;
    }
    w.actors["A"]["status"]["military"]["active"] = active;
    return w;
}
static Json::Value army(const std::string &actor, int n, const char *status = "active") {
    Json::Value e; e["name"] = "Field Army"; e["kind"] = "army"; e["actor"] = actor; e["region"] = actor; e["status"] = status; e["personnel"] = n; e["longitude"] = e["latitude"] = 1; e["origin_event"] = "seed"; return e;
}
int main() {
    try {
        Adjacency adj{{"A", {"D"}}, {"D", {"A"}}};
        auto w = fixture();
        w.entities["one"] = army("A", 5000); w.entities["two"] = army("A", 6000);
        auto build = parse(R"([{"action":"build","actor":"A","region":"A","kind":"army","name":"Third Army"}])");
        auto d = compileOperations(w, event("A", build), adj);
        check(d.changes.empty() && dump(d.events).find("no troops left") != std::string::npos, "Armies exhaust their national pool");
        w.entities["two"]["status"] = "inactive";
        d = compileOperations(w, event("A", build), adj);
        check(d.changes.size() == 1 && d.changes[0]["after"]["personnel"] == 5000, "Inactive armies release allocations");
        w = fixture(15000, 100e9); build.append(build[0]); build[1]["name"] = "Fourth Army";
        d = compileOperations(w, event("A", build), adj);
        check(d.changes.size() == 1 && dump(d.events).find("construction capacity") != std::string::npos, "Pending builds reserve capacity");
        auto site = build[0]; site["kind"] = "event_site"; site["name"] = "Memorial"; build.insert(0, site);
        d = compileOperations(w, event("A", build), adj);
        check(d.changes.size() == 2, "Event sites do not consume capacity");
        w = fixture(11000); build.removeIndex(0, nullptr);
        d = compileOperations(w, event("A", build), adj);
        check(d.changes.size() == 2 && d.changes[0]["after"]["personnel"] == 5000 && d.changes[1]["after"]["personnel"] == 5000, "Pending armies reserve personnel");
        build.append(build[1]); build[2]["name"] = "Fifth Army";
        d = compileOperations(w, event("A", build), adj);
        check(d.changes.size() == 2 && dump(d.events).find("no troops left") != std::string::npos, "Third army cannot reuse reservations");
        w.actors["A"]["status"]["military"]["ships"] = 0;
        build.resize(1); build[0]["kind"] = "fleet";
        check(compileOperations(w, event("A", build), adj).changes.empty(), "No fleets without warships");
        w = fixture(1000); w.actors["A"]["status"]["military"]["aircraft"] = 0;
        auto invade = parse(R"([{"action":"occupy","by":"A","region":"D"}])");
        d = compileOperations(w, event("A", invade), adj);
        check(d.changes.empty() && dump(d.events).find("too weak") != std::string::npos, "Weak raids rejected");
        w = fixture(); w.entities["one"] = army("A", 5000);
        auto destroyed = w.entities["one"]; destroyed["status"] = "destroyed";
        WorldDelta death; Json::Value change; change["kind"] = "entity"; change["id"] = "one"; change["before"] = w.entities["one"]; change["after"] = destroyed; death.changes.append(change);
        w.entities["one"] = destroyed;
        auto result = applyMonthlyStatus({w, {}}, death, adj);
        check(result.state.actors["A"]["status"]["military"]["active"] == 10000, "Destroyed personnel removed");
        result = applyMonthlyStatus(result, {}, adj);
        check(result.state.actors["A"]["status"]["military"]["active"] == 10000, "Destruction charged once");
        w = fixture(); w.entities["old"] = army("A", 10000); w.entities["old"].removeMember("personnel");
        auto prior = w.territories; seedMilitary(w, StatusSeed{});
        check(w.entities["old"]["personnel"] == 10000 && w.territories == prior, "Legacy formation backfill preserves demographics");
        auto encoded = encode(w); seedMilitary(w, StatusSeed{}); check(encode(w) == encoded, "Backfill idempotent");
        // Battle thresholds use exact fixtures, without GDP drift or diplomatic seed modifiers.
        w = fixture(15000); for (auto a : {"A", "D"}) { auto &m = w.actors[a]["status"]["military"]; m["ships"] = m["aircraft"] = 0; }
        w.actors["D"]["status"]["military"]["active"] = 10000;
        check(force::assaultCapacity(w, adj, "A", "D", "D") == 1, "Limited attack at ratio 0.8");
        w.actors["A"]["status"]["military"]["active"] = 28125;
        check(force::assaultCapacity(w, adj, "A", "D", "D") == 2, "Superior at exact 1.5 threshold");
        w.actors["A"]["status"]["military"]["active"] = 14062;
        check(force::assaultCapacity(w, adj, "A", "D", "D") == 0, "Below 0.75 is too weak");
        w.entities["one"] = army("A", 5000);
        check(force::uncommittedPersonnel(w, "A") == 9062, "No double-counting field armies");
        // Pinned national seed: reference values and actual front armies, not copied arithmetic.
        Store db(std::string(SOURCE_DIR) + "/data/world.sqlite", true); StatusSeed seed; WorldState real;
        for (auto &r : db.query("SELECT * FROM country_stats")) { auto a = r["code"].asString(); seed.countries[a] = r; real.actors[a]["name"] = a; }
        for (auto &r : db.query("SELECT * FROM military_stats")) seed.military[r["code"].asString()] = r;
        for (auto &r : db.query("SELECT * FROM region_stats")) seed.regions[r["id"].asString()] = r;
        for (auto &r : db.query("SELECT id,name,affiliation,kind FROM regions")) { auto &t = real.territories[r["id"].asString()]; t = r; t["owner"] = t["controller"] = t["initial_affiliation"] = r["affiliation"]; }
        Adjacency realAdj; for (auto &r : db.query("SELECT a,b FROM region_neighbors")) realAdj[r["a"].asString()].insert(r["b"].asString());
        seed.world = parse(db.query("SELECT document FROM world_seed")[0]["document"].asString()); seedStatus(real, seed, true);
        check(hasMilitary(real) && real.entities.size() == 4, "Every actor seeded and four front armies created");
        auto region = [&](const std::string &a, const std::string &name = "") { for (const auto &id : real.territories.getMemberNames()) if (real.territories[id]["owner"] == a && real.territories[id]["kind"] != "residual" && (name.empty() || real.territories[id]["name"] == name)) return id; throw std::runtime_error("missing region"); };
        auto capacity = [&](const char *a, const char *b, int expected, const std::string &name = "") {
            auto r = region(b, name); int n = force::assaultCapacity(real, realAdj, a, b, r);
            std::cout << a << " -> " << b << ": " << n << " regions; " << force::reachDescription(real, realAdj, a, r) << "\n";
            check(n == expected, std::string(a) + " -> " + b + " expected " + std::to_string(expected));
        };
        capacity("ES", "PT", 2); capacity("PT", "ES", 0); capacity("IN", "PK", 1); capacity("PK", "IN", 0); capacity("KM", "MG", 0);
        capacity("RU", "UA", 1, "Dnipropetrovsk Oblast"); capacity("UA", "RU", 0, "Belgorod Oblast");
        check(force::militaryLabel(real, "US") == "major power" && force::militaryLabel(real, "CN") == "major power" && force::militaryLabel(real, "IN") == "regional power" && force::militaryLabel(real, "KM") == "negligible", "Reference strength labels");
        auto month = applyMonthlyStatus({real, {}}, {}, realAdj);
        const auto &m = month.state.actors["UA"]["status"]["military"];
        check(m["reserve"].asInt() == 1176000 && m["active"].asInt() > 900000 && m["active"].asInt() < 924000, "Ukraine mobilises 2% reserves and loses active personnel");
        check(encode(month.state) == encode(applyMonthlyStatus({real, {}}, {}, realAdj).state), "Monthly military state deterministic");
        std::cout << "Military inventory, reservations, combat, migration and attrition passed\n";
    } catch (const std::exception &e) { std::cerr << e.what() << "\n"; return 1; }
}
