#include "localhistoria/providers.h"
#include "localhistoria/status.h"
#include "localhistoria/turn.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <tuple>
using namespace lh;
// Real-model scenarios on a four-region map: West and North (PT), East (ES, borders West) and
// Interior (ES, borders only East).
int main(int argc, char **argv) {
    if (argc < 2 || argc > 4) {
        std::cerr << "Usage: world_probe output.json [scenario|all] [reasoning|schema|compact]\n";
        return 1;
    }
    Json::Value report(Json::arrayValue);
    LocalProvider provider("http://127.0.0.1:8081", 600);
    Adjacency adjacency{{"region-west", {"region-east", "region-north"}},
                        {"region-north", {"region-west"}},
                        {"region-east", {"region-west", "region-interior"}},
                        {"region-interior", {"region-east"}}};
    // Coasts: West, North and East share a sea; Madeira (naval scenarios only) lies 900 km out.
    for (auto [a, b, km] : std::initializer_list<std::tuple<const char *, const char *, int>>{
             {"region-west", "region-north", 600},
             {"region-west", "region-east", 800},
             {"region-north", "region-east", 1000},
             {"region-west", "region-isle", 900},
             {"region-north", "region-isle", 1200}}) {
        adjacency.sea[a][b] = km;
        adjacency.sea[b][a] = km;
    }
    const std::vector<std::pair<std::string, std::pair<std::string, std::string>>> scenarios{
        {"construction", {"PT", "Build a port in West."}},
        {"conflict", {"ES", "Send the army across the border to occupy West."}},
        {"secession", {"PT", "Grant West independence as a new state named West Republic."}},
        {"long_history", {"PT", "Construct a military base in West."}},
        {"reform", {"PT", "Launch a national education reform."}},
        {"overreach", {"PT", "Conquer all of Spain this month."}},
        {"weak_raid", {"PT", "Send the army to occupy Galicia."}},
        {"capital", {"PT", "Build a new city in North to serve as the future capital."}},
        {"invasion", {"PT", "Invade Spain."}},
        {"finish_invasion", {"PT", "Finish the invasion of Spain to avoid the war stalling."}},
        {"capital_push", {"PT", "Drive straight through to Spain's capital to topple the regime."}},
        {"treaty_consent", {"ES", "Sign a defence pact with Portugal."}},
        {"access_treaty",
         {"PT", "Offer Spain a treaty granting our forces military access to Spanish territory."}},
        {"sea_move", {"PT", "Sail the Portuguese Fleet to Madeira."}},
        {"overseas_landing", {"PT", "Ship the Portuguese Army to Madeira with the fleet."}}};
    for (auto &[name, intention] : scenarios) {
        if (argc >= 3 && std::string(argv[2]) != "all" && name != argv[2])
            continue;
        WorldState w;
        w.engine["lifecycle_version"] = 1;
        w.turn = 80;
        w.player = "PT";
        w.actors = parse(R"({"PT":{"name":"Portugal","color":"#338855"},"ES":{"name":"Spain",)"
                         R"("color":"#ddcc44"}})");
        w.territories =
            parse(R"({"region-west":{"name":"West","owner":"PT","controller":"PT",)"
                  R"("initial_affiliation":"PT","longitude":1,"latitude":1},)"
                  R"("region-north":{"name":"North","owner":"PT","controller":"PT",)"
                  R"("initial_affiliation":"PT","longitude":1,"latitude":11},)"
                  R"("region-east":{"name":"East","owner":"ES","controller":"ES",)"
                  R"("initial_affiliation":"ES","longitude":11,"latitude":1},)"
                  R"("region-interior":{"name":"Interior","owner":"ES","controller":"ES",)"
                  R"("initial_affiliation":"ES","longitude":21,"latitude":1}})");
        for (auto a : {"PT", "ES"}) {
            auto &status = w.actors[a]["status"];
            status["stability"] = status["baseline_stability"] = 70;
            status["military"] =
                a == std::string("PT")
                    ? parse(R"({"active":28000,"reserve":210000,"ships":7,"aircraft":27,)"
                            R"("estimated":1})")
                    : parse(R"({"active":133000,"reserve":15000,"ships":14,"aircraft":140,)"
                            R"("estimated":1})");
            for (auto &t : w.territories)
                if (t["owner"] == a) {
                    t["population"] = 1000000;
                    t["gdp_usd"] =
                        Json::Int64(a == std::string("PT") ? 32100000000LL : 38600000000LL);
                }
        }
        const bool strongInvasion =
            name == "invasion" || name == "finish_invasion" || name == "capital_push";
        // Synthetic superiority isolates order comprehension from the real Portuguese force's
        // inability to invade Spain. weak_raid retains the real inventories as a negative control.
        // These regions lie about 1,100 km apart, so the plan 26 capture effort is about 2 and a
        // seizure needs an effective 3:1; the forces below clear it with a margin.
        if (strongInvasion) {
            w.actors["PT"]["status"]["military"]["active"] = 2500000;
            w.actors["PT"]["status"]["military"]["aircraft"] = 500;
        }
        if (name == "conflict")
            w.actors["ES"]["status"]["military"]["active"] = 450000;
        if (name == "weak_raid")
            w.territories["region-east"]["name"] = "Galicia";
        const bool naval = name == "sea_move" || name == "overseas_landing";
        for (auto r : {"region-west", "region-north", "region-east"})
            w.territories[r]["coastal"] = true;
        if (naval) {
            w.territories["region-isle"] =
                parse(R"({"name":"Madeira","owner":"PT","controller":"PT","coastal":true,)"
                      R"("initial_affiliation":"PT","longitude":-16,"latitude":2,)"
                      R"("population":250000,"gdp_usd":6000000000})");
            w.entities = parse(
                R"({"pt-fleet":{"name":"Portuguese Fleet","kind":"fleet","actor":"PT",)"
                R"("region":"region-west","status":"active","ships":7,"longitude":1,"latitude":1,)"
                R"("origin_event":"t0-seed"},"pt-army":{"name":"Portuguese Army","kind":"army",)"
                R"("actor":"PT","region":"region-west","status":"active","personnel":3000,)"
                R"("longitude":1,"latitude":1,"origin_event":"t0-seed"}})");
        }
        Json::Value geo;
        geo["region-west"] =
            parse(R"({"type":"Polygon","coordinates":[[[0,0],[5,0],[5,5],[0,5],[0,0]]]})");
        geo["region-north"] =
            parse(R"({"type":"Polygon","coordinates":[[[0,10],[5,10],[5,15],[0,15],[0,10]]]})");
        geo["region-east"] =
            parse(R"({"type":"Polygon","coordinates":[[[10,0],[15,0],[15,5],[10,5],[10,0]]]})");
        geo["region-interior"] =
            parse(R"({"type":"Polygon","coordinates":[[[20,0],[25,0],[25,5],[20,5],[20,0]]]})");
        geo["region-isle"] = parse(
            R"({"type":"Polygon","coordinates":[[[-18,0],[-14,0],[-14,4],[-18,4],[-18,0]]]})");
        if (name == "long_history")
            w.commitments = parse(
                R"({"treaty-1":{"actors":["PT","ES"],"status":"open",)"
                R"("text":"Treaty signed in turn 1 forbids construction of military bases in )"
                R"(West until turn 120."}})");
        auto base = resolve(w);
        std::map<std::string, CountryContext> contexts;
        for (auto a : {"PT", "ES"})
            contexts[a] = context(base.state, a, {}, {});
        auto input = adjudicationInput(base.state, contexts,
                                       {{intention.first, {{intention.second, {}}}}}, adjacency);
        Json::Value row;
        row["scenario"] = name;
        auto started = std::chrono::steady_clock::now();
        InferenceRequest request;
        if (argc == 4) request.experiment = argv[3];
        try {
            auto delta = provider.adjudicate(base.state, input, adjacency, Json::arrayValue, &request);
            auto result = applyDelta(base, delta, geo);
            auto &s = result.state;
            row["valid"] = true;
            row["delta"] = encode(delta);
            row["state"] = encode(s);
            // Validity only proves the output is legal; these check the outcome matches the
            // intention and stays realistic.
            bool expected = false;
            auto has = [&](const char *kind, const char *region, const char *status) {
                for (auto &e : s.entities)
                    if (e["kind"] == kind && e["region"] == region && e["status"] == status)
                        return true;
                return false;
            };
            if (name == "construction")
                expected = has("port", "region-west", "under_construction");
            if (name == "conflict")
                expected = s.territories["region-west"]["controller"] == "ES" &&
                           s.territories["region-west"]["owner"] == "PT";
            if (name == "secession")
                expected = s.territories["region-west"]["owner"] != "PT" && s.actors.size() == 3;
            if (name == "long_history") {
                expected = true;
                for (auto &e : s.entities)
                    expected = expected && e["kind"] != "base";
            }
            // A reform changes no map and starts an effort the month step prices and pays for.
            if (name == "reform") {
                expected = delta.changes.empty() && !delta.events.empty() &&
                           std::any_of(delta.events.begin(), delta.events.end(),
                                       [](const Json::Value &e) { return e.isMember("effort"); });
            }
            if (name == "overreach" || name == "weak_raid")
                expected = s.territories["region-interior"]["controller"] == "ES" &&
                           s.territories["region-east"]["controller"] == "ES" &&
                           !delta.events.empty();
            if (strongInvasion)
                expected = s.territories["region-east"]["controller"] == "PT" &&
                           s.territories["region-east"]["owner"] == "ES" &&
                           s.territories["region-interior"]["controller"] == "ES";
            if (name == "capital")
                expected = has("city", "region-north", "under_construction");

            // Portugal (the player) never mentioned Spain, so Spain's pact is only offered: a
            // proposal awaiting Portugal's answer, no treaty.
            if (name == "treaty_consent")
                expected = s.commitments.empty() && s.proposals.size() == 1;
            if (name == "access_treaty") {
                for (auto &p : s.proposals)
                    for (auto &t : p["terms"])
                        expected = expected || (t["action"] == "grant_access" && t["to"] == "PT");
            }
            if (name == "sea_move")
                expected = s.entities["pt-fleet"]["region"] == "region-isle";
            if (name == "overseas_landing")
                expected = s.entities["pt-army"]["region"] == "region-isle";
            row["expected_outcome"] = expected;
        } catch (const std::exception &e) {
            row["valid"] = false;
            row["expected_outcome"] = false;
            row["error"] = e.what();
        }
        row["seconds"] =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        auto records = request.takeAudit();
        for (auto &r : records) {
            r.removeMember("chat_template");
            r["schema_bytes"] = Json::UInt64(dump(r["schema"]).size());
            r.removeMember("schema");
        }
        row["inference"] = records;
        report.append(row);
        std::ofstream(argv[1]) << dump(report);
        std::string events;
        for (auto &e : row["delta"]["events"])
            events += "\n    - " + e["text"].asString();
        std::cout << name << ": valid=" << row["valid"].asBool()
                  << " expected=" << row["expected_outcome"].asBool()
                  << " seconds=" << int(row["seconds"].asDouble()) << " "
                  << row.get("error", "").asString() << events << std::endl;
    }
    return 0;
}
