#include "localhistoria/lifecycle.h"
#include "status_tests.h"
namespace {
// Aland's capital a1 (coast), its border province a2 (next to Bland's b1 and Cland's c1) and a3,
// a quiet interior next only to a1. Bland holds b1, b2 and bz, a coast behind b2 with no sea route
// to anywhere near the fighting.
// Cland holds c1 and c2.
Resolution world() {
    WorldState w;
    w.player = "AA";
    w.campaignId = "forces";
    w.turn = 20;
    w.engine["lifecycle_version"] = realismLifecycleVersion;
    w.actors = parse(R"({"AA":{"name":"Aland","color":"#111111"},)"
                     R"("BB":{"name":"Bland","color":"#222222"},)"
                     R"("CC":{"name":"Cland","color":"#333333"}})");
    for (auto [id, owner, coastal] :
         std::initializer_list<std::tuple<const char *, const char *, bool>>{{"a1", "AA", true},
                                                                             {"a2", "AA", false},
                                                                             {"a3", "AA", false},
                                                                             {"b1", "BB", false},
                                                                             {"b2", "BB", false},
                                                                             {"bz", "BB", true},
                                                                             {"c1", "CC", false},
                                                                             {"c2", "CC", false}}) {
        auto &t = w.territories[id];
        t["name"] = id;
        t["owner"] = t["controller"] = t["initial_affiliation"] = owner;
        t["kind"] = "ADM1";
        t["coastal"] = coastal;
    }
    StatusSeed seed;
    seed.countries = parse(R"({
      "AA":{"population":30000000,"gdp_usd":300000000000,"gdp_growth":0,"pop_growth":0,
            "stability":60,"currency":"USD","symbol":"US$","usd_rate":1,"estimated":0},
      "BB":{"population":15000000,"gdp_usd":150000000000,"gdp_growth":0,"pop_growth":0,
            "stability":60,"currency":"USD","symbol":"US$","usd_rate":1,"estimated":0},
      "CC":{"population":10000000,"gdp_usd":100000000000,"gdp_growth":0,"pop_growth":0,
            "stability":60,"currency":"USD","symbol":"US$","usd_rate":1,"estimated":0}})");
    seed.regions = parse(R"({"a1":{"population":10000000,"gdp_usd":100000000000},
      "a2":{"population":10000000,"gdp_usd":100000000000},
      "a3":{"population":10000000,"gdp_usd":100000000000},
      "b1":{"population":5000000,"gdp_usd":50000000000},
      "b2":{"population":5000000,"gdp_usd":50000000000},
      "bz":{"population":5000000,"gdp_usd":50000000000},
      "c1":{"population":5000000,"gdp_usd":50000000000},
      "c2":{"population":5000000,"gdp_usd":50000000000}})");
    seed.military = parse(R"({"AA":{"active":200000,"reserve":0,"ships":20,"aircraft":300},
      "BB":{"active":100000,"reserve":0,"ships":20,"aircraft":30},
      "CC":{"active":100000,"reserve":0,"ships":0,"aircraft":30}})");
    seedStatus(w, seed, false);
    seedMilitary(w, seed);
    auto unit = [&](const char *id, const char *actor, const char *kind, const char *region,
                    const char *field, int n, const char *status = "active") {
        auto &e = w.entities[id];
        e["name"] = id;
        e["actor"] = actor;
        e["kind"] = kind;
        e["region"] = region;
        e["status"] = status;
        e["origin_event"] = "seed";
        e[field] = n;
        if (std::string(status) == "under_construction")
            e["completes_turn"] = 200;
    };
    unit("front", "AA", "army", "a2", "personnel", 40000);
    unit("home", "AA", "army", "a3", "personnel", 30000);
    unit("recruits", "AA", "army", "a2", "personnel", 20000, "under_construction");
    unit("far-fleet", "BB", "fleet", "bz", "ships", 12);
    unit("home-fleet", "AA", "fleet", "a1", "ships", 10);
    return Resolution{w, {}, realismLifecycleVersion};
}
Adjacency adjacency() {
    Adjacency adj{{"a1", {"a2", "a3"}}, {"a2", {"a1", "b1", "c1"}},
                  {"a3", {"a1"}},       {"b1", {"a2", "b2"}},
                  {"b2", {"b1", "bz"}}, {"bz", {"b2"}},
                  {"c1", {"a2", "c2"}}, {"c2", {"c1"}}};
    adj.seaRevision = "forces-1";
    return adj;
}
Json::Value war(const char *enemy, const char *intensity = "high") {
    auto c = parse(R"({"kind":"war","status":"open","text":"War","started_turn":0,)"
                   R"("theatre":[],"affected_regions":[]})");
    c["name"] = std::string("War with ") + enemy;
    c["actors"].append("AA");
    c["actors"].append(enemy);
    c["set_intensity"] = c["intensity"] = intensity;
    Json::Value pair;
    pair["actors"].append("AA");
    pair["actors"].append(enemy);
    pair["status"] = "active";
    c["hostile_pairs"].append(pair);
    c["postures"]["AA"] = c["postures"][enemy] = intensity;
    return c;
}
Json::Value refs() {
    Json::Value r;
    r["lifecycle_version"] = realismLifecycleVersion;
    r["status_version"] = statusVersion;
    r["sea_route_revision"] = "forces-1";
    return r;
}
Resolution months(Resolution r, int n, const Adjacency &adj) {
    for (int m = 0; m < n; ++m)
        r = finishTurn(beginTurn(r.state, refs()), {}, adj, Json::Value(Json::objectValue), refs());
    return r;
}
Json::Int64 of(const Resolution &r, const char *actor, const char *field) {
    return std::as_const(r.state.actors)[actor]["status"]["military"][field].asInt64();
}
Json::Int64 at(const Resolution &r, const char *entity, const char *field) {
    return std::as_const(r.state.entities)[entity][field].asInt64();
}
} // namespace

void forceTests() {
    const auto adj = adjacency();
    // One allocation, shared: an army facing two enemies serves each at half strength, and the
    // pool is divided across every engagement, so the parts never add up to more than exists.
    {
        auto two = world();
        two.state.conflicts["c1-b"] = war("BB");
        two.state.conflicts["c1-c"] = war("CC");
        std::map<std::string, double> againstB, againstC;
        double b = force::engaged(two.state, adj, "AA", {"b1"}, "BB", &againstB);
        double c = force::engaged(two.state, adj, "AA", {"c1"}, "CC", &againstC);
        check(againstB["front"] == 20000 && againstC["front"] == 20000,
              "An army facing two enemies is split between them: " +
                  std::to_string(againstB["front"]));
        check(!againstB.contains("recruits") && !againstB.contains("home"),
              "Only formations in the field and next to the front are allocated");
        check(b + c <= double(of(two, "AA", "active")),
              "Engagements never add up to more than the active force");
        auto one = world();
        one.state.conflicts["c1-b"] = war("BB");
        auto alone = force::assault(one.state, adj, "AA", "BB", "b1")["attack"].asDouble();
        auto split = force::assault(two.state, adj, "AA", "BB", "b1")["attack"].asDouble();
        check(split < alone * 0.8, "A second front costs offensive strength: " +
                                       std::to_string(split) + " vs " + std::to_string(alone));
    }

    // Losses fall on what was exposed. Two years of high-intensity war with Bland: the army on the
    // border bleeds, the army at home and the one still assembling do not.
    {
        auto fighting = world();
        fighting.state.conflicts["c1-b"] = war("BB");
        auto before = people(fighting.state);
        auto year = months(fighting, 24, adj);
        check(at(year, "front", "personnel") < 40000,
              "The exposed army takes losses: " + std::to_string(at(year, "front", "personnel")));
        check(at(year, "home", "personnel") == 30000 && at(year, "recruits", "personnel") == 20000,
              "An army at home and one still assembling take no front-line losses");
        Json::Int64 assigned = 0;
        for (const auto &e : std::as_const(year.state.entities))
            if (e["actor"] == "AA" && e["kind"] == "army" &&
                (e["status"] == "active" || e["status"] == "under_construction"))
                assigned += e["personnel"].asInt64();
        check(assigned <= of(year, "AA", "active"), "Formations never hold more than exists");
        // Deaths leave the population exactly once: nothing grows here, so the only change is
        // who died, civilians and soldiers alike.
        Json::Int64 died = 0;
        for (const auto &c : std::as_const(year.state.conflicts))
            died += c["casualties"].asInt64();
        check(before - people(year.state) == died,
              "Population falls by exactly the recorded deaths: " +
                  std::to_string(before - people(year.state)) + " vs " + std::to_string(died));
        // A small air arm now wears down: under rounding per conflict and month, 30 aircraft never
        // lost one.
        check(of(year, "BB", "aircraft") < 30,
              "A small air force is worn down by sustained exposure: " +
                  std::to_string(of(year, "BB", "aircraft")));
        const auto &left = std::as_const(year.state.actors)["BB"]["status"]["attrition"];
        check(left["aircraft"].asDouble() >= 0 && left["aircraft"].asDouble() < 1 &&
                  left["personnel"].asDouble() < 1,
              "Fractions below one are carried, not lost: " + dump(left));
        // A fleet on a coast no front can reach loses nothing, whatever the enemy owns.
        check(of(year, "BB", "ships") == 20 && at(year, "far-fleet", "ships") == 12,
              "No ship is lost where no front can reach it");
        // Fractions survive a save and reload: the month after a round trip matches the month
        // after an uninterrupted run.
        auto reloaded = year;
        reloaded.state = decode(parse(dump(encode(year.state))));
        check(encode(months(reloaded, 3, adj).state) == encode(months(year, 3, adj).state),
              "Carried attrition survives save and reload");
        check(encode(months(fighting, 24, adj).state) == encode(year.state),
              "Force allocation and attrition are deterministic");
    }

    // A navy fights where it can reach. With Bland's coast on the front and Aland's fleet in range,
    // both lose ships over time; Bland's far fleet still loses none.
    {
        auto coast = world();
        coast.state.conflicts["c1-b"] = war("BB");
        coast.state.territories["b1"]["coastal"] = true;
        auto sea = adj;
        sea.sea["a1"]["b1"] = sea.sea["b1"]["a1"] = 200;
        auto years = months(coast, 36, sea);
        const auto &bland = std::as_const(years.state.actors)["BB"]["status"];
        check(at(years, "home-fleet", "ships") < 10 && at(years, "far-fleet", "ships") == 12 &&
                  (of(years, "BB", "ships") < 20 || bland["attrition"]["ships"].asDouble() > 0),
              "Ships in reach of a front are worn down, a fleet out of reach is not: " +
                  std::to_string(at(years, "home-fleet", "ships")) + ", " + dump(bland));
        // Aland's ten uncommitted ships were never near the front, so the national count is those
        // ten plus what is left of the fleet that was: each sinking charged once.
        check(of(years, "AA", "ships") == 10 + at(years, "home-fleet", "ships"),
              "Ship losses are charged once to the fleet and the national count");
    }

    // The old rules are untouched: the same war under the legacy lifecycle still spreads losses
    // across every army.
    {
        auto legacy = world();
        legacy.state.engine.clear();
        legacy.lifecycleVersion = legacyLifecycleVersion;
        legacy.state.conflicts["c1-b"] = war("BB");
        for (int m = 0; m < 12; ++m) {
            legacy.state.turn++;
            legacy = applyMonthlyStatus(legacy, {}, adj);
        }
        check(at(legacy, "home", "personnel") < 30000 &&
                  !std::as_const(legacy.state.actors)["BB"]["status"].isMember("attrition"),
              "Legacy turns keep their original allocation and rounding");
    }
}
