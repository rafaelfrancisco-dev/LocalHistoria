#include "localhistoria/lifecycle.h"
#include "status_tests.h"
namespace {
// Aland: a1 on the border with Bland's b1 and neutral Nland's n1, a2 next to Cland's c1, and a3,
// a quiet interior. Nothing grows, so every change in population is a death or a movement.
Resolution world() {
    WorldState w;
    w.player = "AA";
    w.campaignId = "displacement";
    w.turn = 20;
    w.engine["lifecycle_version"] = realismLifecycleVersion;
    w.actors = parse(R"({"AA":{"name":"Aland","color":"#111111"},)"
                     R"("BB":{"name":"Bland","color":"#222222"},)"
                     R"("CC":{"name":"Cland","color":"#333333"},)"
                     R"("NN":{"name":"Nland","color":"#444444"}})");
    for (auto [id, owner] : std::initializer_list<std::pair<const char *, const char *>>{
             {"a1", "AA"}, {"a2", "AA"}, {"a3", "AA"}, {"b1", "BB"}, {"c1", "CC"}, {"n1", "NN"}}) {
        auto &t = w.territories[id];
        t["name"] = id;
        t["owner"] = t["controller"] = t["initial_affiliation"] = owner;
        t["kind"] = "ADM1";
    }
    StatusSeed seed;
    auto country = [](const char *pop) {
        return std::string(R"({"population":)") + pop +
               R"(,"gdp_usd":100000000000,"gdp_growth":0,"pop_growth":0,"stability":60,)"
               R"("currency":"USD","symbol":"US$","usd_rate":1,"estimated":0})";
    };
    seed.countries =
        parse(R"({"AA":)" + country("3000000") + R"(,"BB":)" + country("1000000") + R"(,"CC":)" +
              country("1000000") + R"(,"NN":)" + country("1000000") + "}");
    seed.regions = parse(R"({"a1":{"population":1000000,"gdp_usd":30000000000},
      "a2":{"population":1000000,"gdp_usd":30000000000},
      "a3":{"population":1000000,"gdp_usd":30000000000},
      "b1":{"population":1000000,"gdp_usd":30000000000},
      "c1":{"population":1000000,"gdp_usd":30000000000},
      "n1":{"population":1000000,"gdp_usd":30000000000}})");
    seedStatus(w, seed, false);
    return Resolution{w, {}, realismLifecycleVersion};
}
Adjacency adjacency() {
    return Adjacency{{"a1", {"a2", "a3", "b1", "n1"}},
                     {"a2", {"a1", "a3", "c1"}},
                     {"a3", {"a1", "a2"}},
                     {"b1", {"a1"}},
                     {"c1", {"a2"}},
                     {"n1", {"a1"}}};
}
Json::Value war(const char *enemy) {
    auto c = parse(R"({"kind":"war","status":"open","text":"War","started_turn":0,)"
                   R"("theatre":[],"affected_regions":[],"casualties":0,"displaced":0})");
    c["name"] = std::string("War with ") + enemy;
    c["actors"].append("AA");
    c["actors"].append(enemy);
    c["set_intensity"] = c["intensity"] = "high";
    Json::Value pair;
    pair["actors"].append("AA");
    pair["actors"].append(enemy);
    pair["status"] = "active";
    c["hostile_pairs"].append(pair);
    c["postures"]["AA"] = c["postures"][enemy] = "high";
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
const Json::Value &territory(const Resolution &r, const char *id) {
    return std::as_const(r.state.territories)[id];
}
Json::Int64 pop(const Resolution &r, const char *id) {
    return territory(r, id)["population"].asInt64();
}
Json::Int64 away(const Resolution &r, const char *origin, const char *host) {
    return territory(r, origin)["displacement"]["hosts"].get(host, 0).asInt64();
}
Json::Int64 deaths(const Resolution &r) {
    Json::Int64 n = 0;
    for (const auto &c : std::as_const(r.state.conflicts))
        n += c["casualties"].asInt64();
    return n;
}
// Every origin's outstanding count is its ledger plus any unknown balance, and no host holds more
// displaced people than it has residents.
void consistent(const Resolution &r, const std::string &when) {
    std::map<std::string, Json::Int64> hostedIn;
    for (auto it = r.state.territories.begin(); it != r.state.territories.end(); ++it) {
        const auto &ledger = std::as_const(*it)["displacement"];
        Json::Int64 known = 0;
        for (auto h = ledger["hosts"].begin(); h != ledger["hosts"].end(); ++h) {
            check(h.name() != it.name() && h->asInt64() > 0,
                  when + ": a ledger row is positive and away from home");
            known += h->asInt64();
            hostedIn[h.name()] += h->asInt64();
        }
        check((*it).get("displaced", 0).asInt64() == known + ledger.get("unknown", 0).asInt64(),
              when + ": outstanding displacement is the ledger's sum in " + it.name());
    }
    for (const auto &[host, n] : hostedIn)
        check(n <= territory(r, host.c_str())["population"].asInt64(),
              when + ": " + host + " hosts no more people than live there");
}
} // namespace

void displacementTests() {
    const auto adj = adjacency();
    // Flight is recorded by origin and host, at home and abroad, and conserves people: the only
    // change in the world's population is the dead.
    auto start = world();
    start.state.conflicts["war-b"] = war("BB");
    auto total = people(start.state);
    auto r = start;
    for (int m = 0; m < 6; ++m) {
        r = month(r, adj);
        consistent(r, "war month " + std::to_string(m));
        check(total - people(r.state) == deaths(r), "Flight conserves people");
    }
    check(away(r, "a1", "a2") > 0 && away(r, "a1", "a3") > 0 && away(r, "a1", "n1") > 0,
          "People flee to safe regions at home and across the border: " +
              dump(territory(r, "a1")["displacement"]));
    check(std::as_const(r.state.conflicts)["war-b"]["displaced"].asInt64() >=
              territory(r, "a1")["displaced"].asInt64(),
          "The conflict keeps counting every movement separately");

    // Peace: people come home from exactly where they sheltered, foreign hosts included, and the
    // outstanding count falls by exactly the people who moved.
    auto peace = r;
    peace.state.conflicts["war-b"]["status"] = "resolved";
    auto was = territory(peace, "a1")["displaced"].asInt64();
    auto abroad = away(peace, "a1", "n1"), nland = pop(peace, "n1"), home = pop(peace, "a1");
    auto calm = peace;
    for (int m = 0; m < 12; ++m) {
        auto next = month(calm, adj);
        consistent(next, "peace month " + std::to_string(m));
        auto returned = calm.state.territories["a1"]["displaced"].asInt64() -
                        next.state.territories["a1"]["displaced"].asInt64();
        check(returned > 0 && pop(next, "a1") - pop(calm, "a1") == returned,
              "Each return is a movement home, never a written-off balance");
        calm = next;
    }
    check(territory(calm, "a1")["displaced"].asInt64() < was && away(calm, "a1", "n1") < abroad,
          "Refugees return from a foreign host");
    check(nland - pop(calm, "n1") == abroad - away(calm, "a1", "n1") &&
              pop(calm, "a1") - home == was - territory(calm, "a1")["displaced"].asInt64(),
          "Returns come out of the recorded host and nowhere else");
    std::cout << "Six months of war displaced " << was << " from a1 (" << abroad
              << " abroad); a year of peace brought "
              << was - territory(calm, "a1")["displaced"].asInt64() << " home\n";

    // No safe origin: while Bland holds a1 by force, nobody goes back and the balance stands.
    auto held = peace;
    held.state.territories["a1"]["controller"] = "BB";
    auto occupied = month(held, adj);
    for (int m = 0; m < 6; ++m)
        occupied = month(occupied, adj);
    check(occupied.state.territories["a1"]["displacement"]["hosts"] ==
              held.state.territories["a1"]["displacement"]["hosts"],
          "No one returns to an origin held by an enemy, and the balance does not decay");

    // A border change does not rewrite provenance: a2 passing to Cland leaves the people it
    // shelters recorded there, still displaced from a1.
    auto ceded = held;
    ceded.state.territories["a2"]["owner"] = ceded.state.territories["a2"]["controller"] = "CC";
    auto after = month(ceded, adj);
    check(away(after, "a1", "a2") == away(held, "a1", "a2"), "Ownership changes keep provenance");

    // Repeated flight: when the war reaches the host, people it shelters move on still displaced
    // from a1, and a2's own residents become displaced from a2. Deaths there fall on hosted people
    // in proportion, so no row ever outnumbers the living.
    auto second = held;
    second.state.conflicts["war-c"] = war("CC");
    auto hostedBefore = territory(second, "a1")["displaced"].asInt64();
    auto moved = second;
    for (int m = 0; m < 6; ++m) {
        moved = month(moved, adj);
        consistent(moved, "second front month " + std::to_string(m));
    }
    check(away(moved, "a1", "a2") < away(second, "a1", "a2") && away(moved, "a2", "a3") > 0,
          "Hosted people flee on, and the host's residents are displaced from it: " +
              dump(territory(moved, "a1")["displacement"]) + " / " +
              dump(territory(moved, "a2")["displacement"]));
    check(territory(moved, "a1")["displaced"].asInt64() <= hostedBefore,
          "Moving on is not a second displacement of the same person");

    // A balance from before the ledger keeps its size and is marked as of unknown whereabouts: the
    // audit's 1,000 people cannot become 970 without anyone moving.
    auto legacy = world();
    legacy.state.territories["a3"]["displaced"] = Json::Int64(1000);
    auto kept = legacy;
    for (int m = 0; m < 6; ++m)
        kept = month(kept, adj);
    check(territory(kept, "a3")["displaced"].asInt64() == 1000 &&
              territory(kept, "a3")["displacement"]["unknown"].asInt64() == 1000 &&
              people(kept.state) == people(legacy.state),
          "A legacy balance is kept as unknown provenance and never returned: " +
              dump(territory(kept, "a3")));

    // Deterministic, and the ledger survives a save and reload.
    auto reloaded = r;
    reloaded.state = decode(parse(dump(encode(r.state))));
    check(encode(month(reloaded, adj).state) == encode(month(r, adj).state),
          "The ledger survives save and reload");

    // The old rules still let the count fall without a movement.
    auto old = world();
    old.state.engine.clear();
    old.lifecycleVersion = legacyLifecycleVersion;
    old.state.territories["a3"]["displaced"] = Json::Int64(1000);
    old.state.territories["a1"]["population"] = Json::Int64(0);
    old.state.territories["a2"]["population"] = Json::Int64(0);
    old.state.turn++;
    auto legacyMonth = applyMonthlyStatus(old, {}, adj);
    check(territory(legacyMonth, "a3")["displaced"].asInt64() == 970 &&
              !territory(legacyMonth, "a3").isMember("displacement"),
          "Legacy turns keep their original return rule");
}
