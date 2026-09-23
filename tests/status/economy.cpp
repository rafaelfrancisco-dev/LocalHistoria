#include "localhistoria/lifecycle.h"
#include "status_tests.h"
namespace {
// Rich Aland (US$3 trillion) and poor Pland (US$20 billion), each with two provinces, and Bland
// and Tiny as trading partners. Nothing grows on its own, so every change in output is a rule.
Resolution world() {
    WorldState w;
    w.player = "AA";
    w.campaignId = "economy";
    w.turn = 20;
    w.engine["lifecycle_version"] = realismLifecycleVersion;
    w.actors = parse(R"({"AA":{"name":"Aland","color":"#111111"},)"
                     R"("PP":{"name":"Pland","color":"#222222"},)"
                     R"("BB":{"name":"Bland","color":"#333333"},)"
                     R"("TT":{"name":"Tiny","color":"#444444"}})");
    for (auto [id, owner] : std::initializer_list<std::pair<const char *, const char *>>{
             {"a1", "AA"}, {"a2", "AA"}, {"p1", "PP"}, {"p2", "PP"}, {"b1", "BB"}, {"t1", "TT"}}) {
        auto &t = w.territories[id];
        t["name"] = id;
        t["owner"] = t["controller"] = t["initial_affiliation"] = owner;
        t["kind"] = "ADM1";
        t["coastal"] = true;
        t["longitude"] = t["latitude"] = 0;
    }
    StatusSeed seed;
    auto country = [](const char *pop, const char *gdp) {
        return std::string(R"({"population":)") + pop + R"(,"gdp_usd":)" + gdp +
               R"(,"gdp_growth":0,"pop_growth":0,"stability":60,)"
               R"("currency":"USD","symbol":"US$","usd_rate":1,"estimated":0})";
    };
    seed.countries = parse(R"({"AA":)" + country("30000000", "3000000000000") + R"(,"PP":)" +
                           country("10000000", "20000000000") + R"(,"BB":)" +
                           country("20000000", "2000000000000") + R"(,"TT":)" +
                           country("100000", "2000000000") + "}");
    seed.regions = parse(R"({"a1":{"population":15000000,"gdp_usd":1500000000000},
      "a2":{"population":15000000,"gdp_usd":1500000000000},
      "p1":{"population":5000000,"gdp_usd":10000000000},
      "p2":{"population":5000000,"gdp_usd":10000000000},
      "b1":{"population":20000000,"gdp_usd":2000000000000},
      "t1":{"population":100000,"gdp_usd":2000000000}})");
    seed.military = parse(R"({"AA":{"active":200000,"reserve":100000,"ships":20,"aircraft":300},
      "PP":{"active":20000,"reserve":0,"ships":0,"aircraft":10},
      "BB":{"active":100000,"reserve":0,"ships":10,"aircraft":100},
      "TT":{"active":100,"reserve":0,"ships":0,"aircraft":0}})");
    seedStatus(w, seed, false);
    seedMilitary(w, seed);
    return Resolution{w, {}, realismLifecycleVersion};
}
Adjacency adjacency() {
    return Adjacency{{"a1", {"a2", "b1"}}, {"a2", {"a1"}}, {"p1", {"p2"}},
                     {"p2", {"p1"}},       {"b1", {"a1"}}, {"t1", {}}};
}
Json::Value refs() {
    Json::Value r;
    r["lifecycle_version"] = realismLifecycleVersion;
    r["status_version"] = statusVersion;
    return r;
}
Resolution month(const Resolution &r, const Adjacency &adj, const WorldDelta &d = {}) {
    auto begun = beginTurn(r.state, refs());
    return applyMonthlyStatus(begun, d, adj, statusVersion);
}
Resolution months(Resolution r, int n, const Adjacency &adj) {
    for (int m = 0; m < n; ++m)
        r = month(r, adj);
    return r;
}
const Json::Value &status(const Resolution &r, const char *a) {
    return std::as_const(r.state.actors)[a]["status"];
}
Json::Int64 gdp(const Resolution &r, const char *a) {
    Json::Int64 n = 0;
    for (const auto &t : std::as_const(r.state.territories))
        if (t["owner"] == a)
            n += t["gdp_usd"].asInt64();
    return n;
}
void project(Resolution &r, const char *id, const char *actor, const char *region, const char *kind,
             int months) {
    auto &e = r.state.entities[id];
    e["name"] = id;
    e["actor"] = actor;
    e["kind"] = kind;
    e["region"] = region;
    e["status"] = "under_construction";
    e["completes_turn"] = r.state.turn + months;
    e["origin_event"] = "seed";
}
WorldDelta effort(const Resolution &r, const char *actor, const char *category) {
    Json::Value e;
    e["id"] = "t" + std::to_string(r.state.turn + 1) + "-effort";
    e["actor"] = actor;
    e["effort"] = category;
    WorldDelta d;
    d.events.append(e);
    return d;
}
Json::Value treaty(const char *name, const char *a, const char *b, const char *kind) {
    Json::Value k;
    k["name"] = name;
    k["kind"] = kind;
    k["actors"].append(a);
    k["actors"].append(b);
    k["status"] = "open";
    k["started_turn"] = 21;
    return k;
}
bool happened(const Resolution &r, const char *action) {
    for (const auto &e : r.events)
        if (e.action == action)
            return true;
    return false;
}
} // namespace

void economyTests() {
    const auto adj = adjacency();
    // Each nation's allowance comes from its own output, and rich and poor both pay for what they
    // build: two ports cost Aland money without delaying either, while Pland's allowance covers
    // less than one even at its lower prices, so its older port goes first and the newer one
    // waits.
    {
        auto r = world();
        project(r, "e20-port-a", "AA", "a1", "port", 6);
        project(r, "e20-port-b", "AA", "a2", "port", 6);
        project(r, "e19-old-port", "PP", "p1", "port", 6);
        project(r, "e20-new-port", "PP", "p2", "port", 6);
        auto next = month(r, adj);
        const auto &rich = status(next, "AA")["implementation"];
        const auto &poor = status(next, "PP")["implementation"];
        check(rich["allowance_usd"].asInt64() > 100 * poor["allowance_usd"].asInt64(),
              "Allowance scales with effective output: " + dump(rich) + " vs " + dump(poor));
        check(rich["committed_usd"].asInt64() >= 600000000 && rich["stalled"] == 0,
              "A rich nation pays for its work too: " + dump(rich));
        const auto &ents = std::as_const(next.state.entities);
        check(ents["e20-port-a"]["completes_turn"] == 26 &&
                  ents["e19-old-port"]["completes_turn"] == 26 &&
                  ents["e19-old-port"]["arrears"].asDouble() > 0.2 &&
                  ents["e20-new-port"]["completes_turn"] == 27 &&
                  ents["e20-new-port"]["unfunded"] == true,
              "Projects compete for the allowance, oldest first: " + dump(ents));
        check(happened(next, "construction_stalled"), "A shortfall is reported");
        auto done = months(next, 40, adj);
        check(std::as_const(done.state.entities)["e20-new-port"]["status"] == "active" &&
                  std::as_const(done.state.entities)["e19-old-port"]["status"] == "active",
              "Underfunded work finishes later, but finishes");
    }

    // An effort pays out once, on paid completion; asking again while it runs continues it.
    {
        auto r = world();
        auto before = gdp(r, "AA");
        auto started = month(r, adj, effort(r, "AA", "investment"));
        check(status(started, "AA")["efforts"]["investment"]["paid"].asDouble() == 1 &&
                  gdp(started, "AA") == before,
              "An investment under way earns nothing yet");
        auto again = month(started, adj, effort(started, "AA", "investment"));
        check(status(again, "AA")["efforts"]["investment"]["started"] ==
                  status(started, "AA")["efforts"]["investment"]["started"],
              "An equivalent request continues the effort under way");
        auto finished = months(again, 10, adj);
        check(!status(finished, "AA").isMember("efforts") &&
                  std::abs(double(gdp(finished, "AA")) / double(before) - 1.01) < 1e-6,
              "The benefit arrives once, on completion: " +
                  std::to_string(double(gdp(finished, "AA")) / double(before)));
        auto later = months(finished, 12, adj);
        check(gdp(later, "AA") == gdp(finished, "AA"), "A completed effort pays out only once");
        // Reforms: the second within five years is worth half the first.
        auto reforms = month(r, adj, effort(r, "PP", "reform"));
        reforms = months(reforms, 17, adj);
        auto first = status(reforms, "PP")["baseline_stability"].asDouble() -
                     status(r, "PP")["baseline_stability"].asDouble();
        auto base = status(reforms, "PP")["baseline_stability"].asDouble();
        reforms = month(reforms, adj, effort(reforms, "PP", "reform"));
        reforms = months(reforms, 17, adj);
        auto second = status(reforms, "PP")["baseline_stability"].asDouble() - base;
        check(first > 2.5 && std::abs(second - first / 2) < 0.2,
              "Repeated reforms have diminishing durable effect: " + std::to_string(first) +
                  " then " + std::to_string(second));
    }

    // Trade: an ongoing benefit counted once per partner and weighted by its market, ended alike
    // by expiry and withdrawal. Relationships from before the first realism month are the
    // baseline.
    {
        auto r = month(world(), adj);
        auto withB = r;
        withB.state.commitments["k1"] = treaty("Accord", "AA", "BB", "trade_agreement");
        auto withTiny = r;
        withTiny.state.commitments["k1"] = treaty("Accord", "AA", "TT", "trade_agreement");
        auto twice = withB;
        twice.state.commitments["k2"] = treaty("Accord renamed", "AA", "BB", "trade_agreement");
        twice.state.commitments["k3"] = treaty("Union", "AA", "BB", "union");
        auto big = month(withB, adj), small = month(withTiny, adj), copies = month(twice, adj);
        check(gdp(big, "AA") > gdp(r, "AA") && gdp(copies, "AA") == gdp(big, "AA"),
              "A partner counts once however many treaties link them");
        check(gdp(small, "AA") - gdp(r, "AA") < (gdp(big, "AA") - gdp(r, "AA")) / 100,
              "A tiny partner is not a major market");
        auto expired = big;
        expired.state.commitments["k1"]["expires_turn"] = expired.state.turn + 1;
        auto withdrawn = big;
        withdrawn.state.commitments["k1"]["status"] = "resolved";
        auto a = month(expired, adj), b = month(withdrawn, adj);
        check(gdp(a, "AA") == gdp(b, "AA") && gdp(a, "AA") == gdp(big, "AA"),
              "Expiry and withdrawal end the same future benefit and keep what was earned");
    }

    // Replacement: losses below the establishment are ordered, paid for and delivered only on
    // completion; recruits change classification, not population.
    {
        auto r = month(world(), adj);
        r.state.actors["PP"]["status"]["military"]["active"] = Json::Int64(15000);
        r.state.actors["PP"]["status"]["military"]["aircraft"] = Json::Int64(8);
        auto people0 = people(r.state);
        auto ordered = month(r, adj);
        const auto &orders = status(ordered, "PP")["procurement"];
        check(orders["personnel"]["count"] == 1000 && orders["aircraft"]["count"] == 1,
              "Losses open replacement orders in batches: " + dump(orders));
        auto waiting = months(ordered, 4, adj);
        check(status(waiting, "PP")["military"]["active"] == 15000,
              "Nothing arrives before the order is paid in full");
        auto arrived = months(waiting, 2, adj);
        check(status(arrived, "PP")["military"]["active"] == 16000 &&
                  people(arrived.state) == people0,
              "Recruits arrive on completion without creating people");
        auto full = months(arrived, 40, adj);
        check(status(full, "PP")["military"]["active"] == 20000,
              "Replacement stops at the establishment");
        // A slot-bound order cannot start while construction fills every slot.
        auto busy = r;
        project(busy, "e20-base", "PP", "p1", "base", 3);
        auto blocked = month(busy, adj);
        check(!status(blocked, "PP")["procurement"].isMember("aircraft") &&
                  status(blocked, "PP")["procurement"].isMember("personnel"),
              "Aircraft wait for a free construction slot; recruitment does not");
    }

    // Demobilisation: after a year without fighting, excess active personnel return to the
    // reserve gradually, never below what formations hold, and never while any war is fought.
    {
        auto r = month(world(), adj);
        auto &m = r.state.actors["AA"]["status"]["military"];
        m["active"] = Json::Int64(260000);
        m["reserve"] = Json::Int64(40000);
        auto &army = r.state.entities["big-army"];
        army["name"] = "Big army";
        army["actor"] = "AA";
        army["kind"] = "army";
        army["region"] = "a2";
        army["status"] = "active";
        army["personnel"] = Json::Int64(230000);
        auto year = months(r, 10, adj);
        check(status(year, "AA")["military"]["active"] == 260000,
              "No demobilisation within a year");
        auto later = months(year, 12, adj);
        auto active = status(later, "AA")["military"]["active"].asInt64();
        check(active == 230000 && status(later, "AA")["military"]["reserve"] == 70000,
              "Excess returns to the reserve, but not below the formations: " +
                  std::to_string(active));
        auto war = r;
        auto c = parse(R"({"kind":"war","status":"open","text":"War","started_turn":0,)"
                       R"("theatre":[],"affected_regions":[],"name":"War","set_intensity":"low",)"
                       R"("intensity":"low","actors":["AA","BB"],)"
                       R"("hostile_pairs":[{"actors":["AA","BB"],"status":"active"}],)"
                       R"("postures":{"AA":"low","BB":"low"}})");
        war.state.conflicts["war"] = c;
        auto fighting = months(war, 24, adj);
        check(status(fighting, "AA")["military"]["reserve"] == 40000 &&
                  status(fighting, "AA")["military"]["peace_months"] == 0,
              "No demobilisation while a war is fought: " +
                  dump(status(fighting, "AA")["military"]));
    }

    // The allowance is deterministic and survives a save and reload.
    {
        auto r = world();
        project(r, "e19-old-port", "PP", "p1", "port", 6);
        auto a = months(r, 3, adj);
        auto reloaded = a;
        reloaded.state = decode(parse(dump(encode(a.state))));
        check(encode(months(reloaded, 3, adj).state) == encode(months(a, 3, adj).state),
              "Implementation accounting survives save and reload");
    }

    // The old rules have none of it.
    {
        auto legacy = world();
        legacy.state.engine.clear();
        legacy.lifecycleVersion = legacyLifecycleVersion;
        project(legacy, "e20-port", "PP", "p1", "port", 6);
        legacy.state.turn++;
        auto next = applyMonthlyStatus(legacy, {}, adj);
        check(!status(next, "PP").isMember("implementation") &&
                  std::as_const(next.state.entities)["e20-port"]["completes_turn"] == 26,
              "Legacy turns keep free construction");
    }
}
