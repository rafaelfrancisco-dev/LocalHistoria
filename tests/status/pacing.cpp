#include "localhistoria/lifecycle.h"
#include "status_tests.h"
#include <cmath>
namespace {
// Aland and Bland face each other along a border of `width` region pairs: a<i> next to b<i>, each
// next to its neighbours along the line. Interiors are a degree apart, the size of real provinces;
// every other pair is populous. `prefix` renames every region without changing anything else.
struct Line {
    int width;
    std::string prefix;
    std::string a(int i) const { return prefix + "a" + std::to_string(i); }
    std::string b(int i) const { return prefix + "b" + std::to_string(i); }
};
Resolution world(const Line &line, Json::Int64 aland, Json::Int64 bland) {
    WorldState w;
    w.player = "AA";
    w.campaignId = "pacing";
    w.turn = 20;
    w.engine["lifecycle_version"] = realismLifecycleVersion;
    w.actors = parse(R"({"AA":{"name":"Aland","color":"#111111"},)"
                     R"("BB":{"name":"Bland","color":"#222222"},)"
                     R"("CC":{"name":"Cland","color":"#333333"}})");
    StatusSeed seed;
    Json::Value regions;
    Json::Int64 people[2] = {0, 0};
    for (int i = 0; i < line.width; ++i)
        for (int side = 0; side < 2; ++side) {
            auto id = side ? line.b(i) : line.a(i);
            auto &t = w.territories[id];
            t["name"] = id;
            t["owner"] = t["controller"] = t["initial_affiliation"] = side ? "BB" : "AA";
            t["kind"] = "ADM1";
            t["longitude"] = double(i);
            t["latitude"] = double(side);
            Json::Int64 n = i % 2 ? 200000 : 5000000;
            regions[id]["population"] = n;
            regions[id]["gdp_usd"] = n * 20000;
            people[side] += n;
        }
    // Cland, off Aland's edge: a small district among close neighbours, and a vast region whose
    // neighbours are hundreds of kilometres away, for capture effort.
    for (auto [id, lon] : std::initializer_list<std::pair<const char *, double>>{{"a-edge", 29.9},
                                                                                 {"c-small", 30.0},
                                                                                 {"c-town", 30.15},
                                                                                 {"c-huge", 37.0},
                                                                                 {"c-far", 44.0}}) {
        auto &t = w.territories[id];
        t["name"] = id;
        t["owner"] = t["controller"] = t["initial_affiliation"] = id[0] == 'a' ? "AA" : "CC";
        t["kind"] = "ADM1";
        t["longitude"] = lon;
        t["latitude"] = 10.0;
        regions[id]["population"] = 1000000;
        regions[id]["gdp_usd"] = Json::Int64(20000000000);
    }
    people[0] += 1000000;
    auto country = [](Json::Int64 pop) {
        return parse(R"({"gdp_growth":0,"pop_growth":0,"stability":60,"currency":"USD",)"
                     R"("symbol":"US$","usd_rate":1,"estimated":0,"population":)" +
                     std::to_string(pop) + R"(,"gdp_usd":)" + std::to_string(pop * 20000) + "}");
    };
    seed.countries["AA"] = country(people[0]);
    seed.countries["BB"] = country(people[1]);
    seed.countries["CC"] = country(3000000);
    seed.regions = regions;
    seed.military["AA"] = parse(R"({"reserve":0,"ships":0,"aircraft":100})");
    seed.military["AA"]["active"] = aland;
    seed.military["BB"] = parse(R"({"reserve":0,"ships":0,"aircraft":100})");
    seed.military["BB"]["active"] = bland;
    seed.military["CC"] = parse(R"({"active":20000,"reserve":0,"ships":0,"aircraft":0})");
    seedStatus(w, seed, false);
    seedMilitary(w, seed);
    return Resolution{w, {}, realismLifecycleVersion};
}
Adjacency adjacency(const Line &line) {
    Adjacency adj;
    auto link = [&](const std::string &x, const std::string &y) {
        adj[x].insert(y);
        adj[y].insert(x);
    };
    for (int i = 0; i < line.width; ++i) {
        link(line.a(i), line.b(i));
        if (i) {
            link(line.a(i - 1), line.a(i));
            link(line.b(i - 1), line.b(i));
        }
    }
    link("a-edge", "c-small");
    link("c-small", "c-town");
    link("c-town", "c-huge");
    link("c-huge", "c-far");
    link(line.a(0), "a-edge");
    return adj;
}
Json::Value war(const char *a, const char *b, int started, const char *intensity = "high") {
    auto c = parse(R"({"kind":"war","status":"open","text":"War","theatre":[],)"
                   R"("affected_regions":[],"casualties":0,"displaced":0})");
    c["name"] = std::string("War ") + a + b;
    c["started_turn"] = started;
    c["actors"].append(a);
    c["actors"].append(b);
    c["set_intensity"] = c["intensity"] = intensity;
    Json::Value pair;
    pair["actors"].append(a);
    pair["actors"].append(b);
    pair["status"] = "active";
    c["hostile_pairs"].append(pair);
    c["postures"][a] = c["postures"][b] = intensity;
    return c;
}
Json::Value refs() {
    Json::Value r;
    r["lifecycle_version"] = realismLifecycleVersion;
    r["status_version"] = statusVersion;
    return r;
}
Resolution month(const Resolution &r, const Adjacency &adj) {
    return finishTurn(beginTurn(r.state, refs()), {}, adj, Json::Value(Json::objectValue), refs());
}
double exhaustion(const Resolution &r, const char *a) {
    return std::as_const(r.state.actors)[a]["status"]["military"]["exhaustion"].asDouble();
}
int held(const Resolution &r, const char *a) {
    int n = 0;
    for (const auto &t : std::as_const(r.state.territories))
        n += t["controller"] == a && t["owner"] != a;
    return n;
}
Json::Int64 total(const Resolution &r, const char *key) {
    Json::Int64 n = 0;
    for (const auto &c : std::as_const(r.state.conflicts))
        n += c[key].asInt64();
    return n;
}
} // namespace

void pacingTests() {
    const Line line{20, ""};
    const auto adj = adjacency(line);

    // No anniversary: a new war's attacking strength declines smoothly as exhaustion builds, with
    // no step at month twelve, and recovers gradually in peace.
    {
        auto r = world(line, 400000, 400000);
        r.state.conflicts["war"] = war("AA", "BB", 20);
        std::vector<double> attack;
        for (int m = 0; m < 24; ++m) {
            r = month(r, adj);
            attack.push_back(
                force::assault(r.state, adj, "AA", "BB", line.b(4))["attack"].asDouble());
        }
        double worst = 0;
        for (size_t m = 1; m < attack.size(); ++m)
            worst = std::max(worst, std::abs(attack[m] / attack[m - 1] - 1));
        check(worst < 0.06 && attack[23] < attack[0] * 0.8,
              "Attacking strength wears down smoothly: largest monthly step " +
                  std::to_string(worst) + ", " + std::to_string(attack[0]) + " -> " +
                  std::to_string(attack[23]));
        auto tired = exhaustion(r, "AA");
        r.state.conflicts["war"]["status"] = "resolved";
        for (int m = 0; m < 7; ++m)
            r = month(r, adj);
        check(exhaustion(r, "AA") < tired * 0.55 && exhaustion(r, "AA") > tired * 0.4,
              "Peace restores forces gradually: " + std::to_string(tired) + " -> " +
                  std::to_string(exhaustion(r, "AA")));
        std::cout << "New high-intensity war: exhaustion " << tired << " after two years, "
                  << exhaustion(r, "AA") << " after seven months of peace\n";
        // A war already a year old when these rules begin starts where the anniversary left it.
        auto old = world(line, 400000, 400000);
        old.state.conflicts["war"] = war("AA", "BB", 0);
        check(exhaustion(month(old, adj), "AA") > 0.75, "Old wars start worn");
    }

    // Parity is a stalemate that still costs lives; decisive superiority moves the line, at most
    // two regions a month, and slows as the attacker tires.
    {
        auto parity = world(line, 400000, 400000);
        parity.state.conflicts["war"] = war("AA", "BB", 20);
        auto superior = world(line, 1600000, 300000);
        superior.state.conflicts["war"] = war("AA", "BB", 20);
        int firstYear = 0, secondYear = 0, maxMonth = 0;
        for (int m = 0; m < 36; ++m) {
            parity = month(parity, adj);
            int before = held(superior, "AA");
            superior = month(superior, adj);
            int gained = held(superior, "AA") - before;
            maxMonth = std::max(maxMonth, gained);
            if (m < 12)
                firstYear += gained;
            else if (m < 24)
                secondYear += gained;
        }
        check(held(parity, "AA") == 0 && held(parity, "BB") == 0 && total(parity, "casualties") > 0,
              "A war between equals takes no ground and still kills");
        check(held(superior, "AA") > 0 && maxMonth <= 2,
              "Superiority takes ground within the monthly budget: " +
                  std::to_string(held(superior, "AA")));
        check(secondYear <= firstYear,
              "Advances slow as the attacker tires: " + std::to_string(firstYear) + " then " +
                  std::to_string(secondYear));
        std::cout << "Three years at 1:1: no ground, " << total(parity, "casualties")
                  << " dead; at 5:1: " << held(superior, "AA") << " regions (" << firstYear
                  << " in year one, " << secondYear << " in year two)\n";
    }

    // A long front is fought over whole, each region by its exposure, so renaming every region
    // leaves the toll equivalent instead of changing which twelve are counted.
    {
        const Line renamed{20, "z"};
        const auto adj2 = adjacency(renamed);
        auto one = world(line, 400000, 400000), two = world(renamed, 400000, 400000);
        one.state.conflicts["war"] = war("AA", "BB", 20);
        two.state.conflicts["war"] = war("AA", "BB", 20);
        for (int m = 0; m < 24; ++m) {
            one = month(one, adj);
            two = month(two, adj2);
        }
        auto near = [](double x, double y) { return std::abs(x - y) <= 0.15 * std::max(x, y); };
        check(near(double(total(one, "displaced")), double(total(two, "displaced"))) &&
                  near(double(total(one, "casualties")), double(total(two, "casualties"))),
              "Renamed fronts bear equivalent losses: " + std::to_string(total(one, "displaced")) +
                  " vs " + std::to_string(total(two, "displaced")));
        Json::Int64 away = 0;
        for (int i = 0; i < line.width; ++i)
            for (const auto &id : {line.a(i), line.b(i)})
                away += std::as_const(one.state.territories)[id].get("displaced", 0).asInt64();
        check(away > 0 && std::as_const(one.state.territories)[line.a(19)].get("displaced", 0) > 0,
              "Every front region bears some of it, beyond the first twelve by ID");
        check(std::as_const(one.state.conflicts)["war"]["affected_regions"].size() <= size_t(12),
              "The reported front stays bounded");
    }

    // Capture effort: a small crowded district and a vast one are not the same objective, and an
    // order meets the same threshold momentum does.
    {
        auto r = world(line, 400000, 400000);
        double small = force::captureEffort(r.state, adj, "AA", "c-small");
        double huge = force::captureEffort(r.state, adj, "AA", "c-huge");
        check(huge > 2.5 * small, "A vast region takes more effort than a small one: " +
                                      std::to_string(small) + " vs " + std::to_string(huge));
        r.state.conflicts["war"] = war("AA", "CC", 20);
        auto response =
            parse(R"({"events":[{"actor":"AA","description":"Take it",)"
                  R"("blocked_by":"none","causes":[],"background":false,)"
                  R"("operations":[{"action":"occupy","region":"c-small","by":"AA"}]}]})");
        auto begun = beginTurn(r.state, refs());
        auto ordered = compileOperations(begun.state, response, adj);
        bool took = false;
        for (const auto &c : ordered.changes)
            took = took || c["id"] == "c-small";
        auto a = force::assault(begun.state, adj, "AA", "CC", "c-small");
        check(took == (a["capacity"].asInt() >= 1),
              "An order is judged by the same threshold as momentum: " + dump(a));
        // Momentum and an order share one budget: an ordered seizure spends banked progress.
        auto banked = r;
        banked.state.conflicts["war"]["pressure"]["AA"] = 1.6;
        auto start = beginTurn(banked.state, refs());
        auto delta = compileOperations(start.state, response, adj);
        auto quiet = finishTurn(start, {}, adj, Json::Value(Json::objectValue), refs());
        auto spent = finishTurn(start, delta, adj, Json::Value(Json::objectValue), refs());
        const auto &p1 = std::as_const(quiet.state.conflicts)["war"]["pressure"];
        const auto &p2 = std::as_const(spent.state.conflicts)["war"]["pressure"];
        if (took)
            check(p2.get("AA", 0.0).asDouble() < p1.get("AA", 0.0).asDouble(),
                  "An ordered seizure spends the progress momentum banked: " + dump(p1) + " vs " +
                      dump(p2));
        std::cout << "Capture effort: small district " << small << ", vast region " << huge
                  << "; order " << (took ? "took" : "could not take") << " the district at "
                  << a["ratio"].asDouble() << ":1 (threshold 1.5)\n";
    }
}
