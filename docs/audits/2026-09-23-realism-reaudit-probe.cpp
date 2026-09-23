// Plan 26 re-audit probe: the eight observations of 2026-09-22-realism-probe.cpp, rerun under the
// realism lifecycle, followed by adversarial cross-system sequences. Synthetic countries only; it
// reads and writes no save. Build instructions are in 2026-09-23-simulation-realism-reaudit.md.
#include "localhistoria/conflicts.h"
#include "localhistoria/lifecycle.h"
#include "localhistoria/status.h"
#include <iostream>
using namespace lh;
static WorldState fixture() {
    WorldState w;
    w.campaignId = "audit";
    w.player = "AA";
    w.turn = 4;
    w.engine["lifecycle_version"] = realismLifecycleVersion;
    for (auto a : {"AA", "BB", "CC"}) {
        std::string id = a;
        auto &v = w.actors[id];
        v["name"] = id == "AA" ? "Aland" : id == "BB" ? "Bland" : "Cland";
        v["color"] = "#123456";
        v["status"] = parse(
            R"({"stability":70,"baseline_stability":70,"seeded_stability":70,"government":"parliamentary_republic","gdp_growth":0,"pop_growth":0,"military":{"active":100000,"reserve":0,"aircraft":100,"ships":30}})");
        auto &t = w.territories[id];
        t["name"] = id;
        t["owner"] = id;
        t["controller"] = id;
        t["initial_affiliation"] = id;
        t["kind"] = "ADM1";
        t["population"] = 10000000;
        t["gdp_usd"] = Json::Int64(200000000000);
        t["longitude"] = id == "AA" ? 0 : id == "BB" ? 1 : 2;
        t["latitude"] = 0;
        t["coastal"] = false;
    }
    w.conflicts["war"] = parse(
        R"({"actors":["AA","BB"],"kind":"war","name":"Test War","text":"War","status":"open","started_turn":0,"set_intensity":"high","intensity":"high","theatre":["AA","BB"]})");
    return w;
}
static Json::Value event(const std::string &a, const std::string &text, const char *ops) {
    auto e = parse(R"({"blocked_by":"none","background":false,"causes":[]})");
    e["actor"] = a;
    e["description"] = text;
    e["operations"] = parse(ops);
    return e;
}
static const Adjacency line{{"AA", {"BB"}}, {"BB", {"AA", "CC"}}, {"CC", {"BB"}}};
static WorldDelta compile(const WorldState &w, const Json::Value &events,
                          const Json::Value &answers = Json::Value(Json::arrayValue)) {
    Json::Value response;
    response["events"] = events;
    return compileOperations(w, response, line, answers);
}
static Json::Value list(const Json::Value &e) {
    Json::Value x(Json::arrayValue);
    x.append(e);
    return x;
}
// One full month exactly as the turn runner performs it: migration, resolve, delta, monthly rules.
static Resolution month(const WorldState &w, const WorldDelta &d = {}) {
    auto ref = lifecycleReference(w);
    return finishTurn(beginTurn(w, ref), d, line, Json::Value(Json::objectValue), ref);
}
static Json::Int64 people(const WorldState &w) {
    Json::Int64 n = 0;
    for (auto &t : w.territories)
        n += t["population"].asInt64();
    return n;
}
int main() {
    // --- The original eight observations ---------------------------------------------------
    auto w = fixture();
    auto d = compile(w, list(event("AA", "Aland stops the war.",
                                   R"([{"action":"resolve_conflict","conflict":"war"}])")));
    std::cout << "1 unilateral_end changes=" << dump(d.changes) << "\n";

    auto es = list(event(
        "AA", "Aland offers trade with Bland.",
        R"([{"action":"add_commitment","actors":["AA","BB"],"kind":"trade_agreement","text":"Trade","name":"Test Accord"}])"));
    es.append(event("BB", "Bland rejects Aland's trade offer.", "[]"));
    d = compile(w, es);
    std::cout << "2 rejection_as_consent changes=" << dump(d.changes)
              << "\n";

    auto bad = event(
        "AA", "Aland builds a city in foreign territory.",
        R"([{"action":"build","actor":"AA","region":"BB","kind":"city","name":"Impossible City"}])");
    bad["effects"] =
        parse(R"([{"actor":"AA","economy":"strong_growth","stability":"strong_growth"}])");
    d = compile(w, list(bad));
    std::cout << "3 failed_effects events=" << dump(d.events) << "\n";

    w.entities["fleet"] = parse(
        R"({"name":"Fleet","actor":"AA","kind":"fleet","region":"AA","status":"active","ships":3,"longitude":0,"latitude":0})");
    d = compile(w, list(event("AA", "Move inland.",
                              R"([{"action":"move_entity","entity":"fleet","region":"CC"}])")));
    std::cout << "4 inland_fleet changes=" << dump(d.changes) << "\n";

    w = fixture();
    Resolution r{w, {}};
    for (int i = 0; i < 24; i++)
        r = month(r.state);
    std::cout << "5 after_24_war_months AA=" << dump(r.state.actors["AA"]["status"]["military"])
              << " attrition=" << dump(r.state.actors["AA"]["status"]["attrition"]) << "\n";

    w = fixture();
    w.conflicts["war"]["actors"].append("CC");
    w.conflicts["war"]["theatre"].append("CC");
    w.commitments["truce"] =
        parse(R"({"actors":["AA","BB"],"kind":"ceasefire","status":"open","name":"Truce","text":"Partial truce"})");
    r = month(w);
    std::cout << "6 partial_truce war=" << dump(r.state.conflicts["war"]) << "\n";

    w = fixture();
    w.conflicts = Json::objectValue;
    w.territories["BB"]["controller"] = "AA";
    r = {w, {}};
    for (int i = 0; i < 24; i++)
        r = month(r.state);
    std::cout << "7 stable_occupier_unrest_24m=" << dump(r.state.territories["BB"]["unrest"])
              << "\n";

    w = fixture();
    w.conflicts = Json::objectValue;
    w.territories["AA"]["displaced"] = 1000;
    auto before = w.territories["AA"]["population"].asInt64();
    r = month(w);
    std::cout << "8 refugees_no_return_route pop_delta="
              << r.state.territories["AA"]["population"].asInt64() - before
              << " territory.displaced=" << dump(r.state.territories["AA"]["displaced"])
              << " displacement=" << dump(r.state.territories["AA"]["displacement"]) << "\n";

    // --- Cross-system sequences --------------------------------------------------------------
    // Capture, then annex, then look for resistance and output after 18 months.
    w = fixture();
    w.territories["BB"]["controller"] = "AA";
    r = month(w);
    {
        auto ref = lifecycleReference(r.state);
        auto base = beginTurn(r.state, ref);
        d = compile(base.state, list(event("AA", "Aland annexes BB.",
                                           R"([{"action":"transfer_territory","region":"BB","to":"AA"}])")));
        std::cout << "X1 annex changes=" << dump(d.changes) << "\n";
        r = finishTurn(base, d, line, Json::Value(Json::objectValue), ref);
    }
    for (int i = 0; i < 18; i++)
        r = month(r.state);
    std::cout << "X1 after_annex_18m BB control=" << dump(r.state.territories["BB"]["control"])
              << " unrest=" << dump(r.state.territories["BB"]["unrest"]) << "\n";

    // A proposal outstanding while its terms change: an answer to revision 1 after an amendment.
    w = fixture();
    w.conflicts = Json::objectValue;
    d = compile(w, list(event(
                       "BB", "Bland offers Aland a trade pact.",
                       R"([{"action":"add_commitment","actors":["AA","BB"],"kind":"trade_agreement","text":"Trade","name":"Pact","months":6,"terms":[]}])")));
    std::cout << "X2 offer proposals=" << dump(d.changes) << "\n";

    // Two fronts drawing on one inventory: AA at war with BB and CC.
    w = fixture();
    w.conflicts["war2"] = parse(
        R"({"actors":["AA","CC"],"kind":"war","name":"Second War","text":"War","status":"open","started_turn":0,"set_intensity":"high","intensity":"high"})");
    auto p0 = people(w);
    r = {w, {}};
    for (int i = 0; i < 36; i++)
        r = month(r.state);
    std::cout << "X3 two_fronts_36m AA=" << dump(r.state.actors["AA"]["status"]["military"])
              << " BB=" << dump(r.state.actors["BB"]["status"]["military"])
              << " CC=" << dump(r.state.actors["CC"]["status"]["military"])
              << " people_delta=" << people(r.state) - p0
              << " war2=" << dump(r.state.conflicts["war2"]["affected_regions"])
              << " war2_casualties=" << dump(r.state.conflicts["war2"]["casualties"])
              << " war_casualties=" << dump(r.state.conflicts["war"]["casualties"]) << "\n";

    // Peace and recovery: the 24-month war ends by settlement, then 24 months of peace.
    w = fixture();
    r = {w, {}};
    for (int i = 0; i < 24; i++)
        r = month(r.state);
    r.state.conflicts["war"]["status"] = "resolved";
    for (int i = 0; i < 24; i++)
        r = month(r.state);
    std::cout << "X4 after_peace_24m AA=" << dump(r.state.actors["AA"]["status"]["military"])
              << " exhaustion=" << dump(r.state.actors["AA"]["status"]["exhaustion"])
              << " gdp_growth=" << dump(r.state.actors["AA"]["status"]["gdp_growth"]) << "\n";

    // An unreachable declared war still takes a share of the national pool. CC borders only BB.
    w = fixture();
    auto one = force::assault(w, line, "BB", "AA", "AA");
    w.conflicts["far"] = parse(
        R"({"actors":["AA","CC"],"kind":"war","name":"Far War","text":"War","status":"open","started_turn":0,"set_intensity":"high","intensity":"high"})");
    initializeConflictPairs(w);
    auto two = force::assault(w, line, "BB", "AA", "AA");
    std::cout << "X5 BB_on_AA one_war=" << dump(one) << " with_unreachable_war=" << dump(two)
              << "\n";

    // Shared status on a coalition war: Aland and Cland fight Bland together.
    w = fixture();
    w.conflicts["war"]["actors"].append("CC");
    w.conflicts["war"]["hostile_pairs"] =
        parse(R"([{"actors":["AA","BB"],"status":"active"},{"actors":["BB","CC"],"status":"active"}])");
    w.commitments["truce"] = parse(
        R"({"actors":["AA","BB"],"kind":"ceasefire","status":"open","name":"Truce","text":"Partial truce"})");
    std::cout << "X6 AA_wars=" << dump(nationalStatus(w, "AA")["wars"]) << "\n";
}
