#include "localhistoria/conflicts.h"
#include "localhistoria/lifecycle.h"
#include <fstream>
#include <iostream>
using namespace lh;
static int checks;
static void check(bool ok, const char *message) {
    ++checks;
    if (!ok)
        throw std::runtime_error(message);
}
static WorldState fixture() {
    std::ifstream f(std::string(FIXTURE_DIR) + "/legacy-lifecycle-v4.json");
    auto w = decode(parse(std::string(std::istreambuf_iterator<char>(f), {}))["initial_state"]);
    w.turn = 4;
    w.entities = w.commitments = Json::objectValue;
    w.territories["CC"]["controller"] = "CC";
    w.engine["lifecycle_version"] = 1;
    w.conflicts["war"]["actors"].append("CC");
    w.conflicts["war"]["hostile_pairs"] = parse(
        R"([{"actors":["AA","BB"],"status":"active"},{"actors":["BB","CC"],"status":"active"}])");
    initializeConflictPairs(w);
    return w;
}
static Resolution month(const WorldState &w) {
    Adjacency adj{{"AA", {"BB"}}, {"BB", {"AA", "CC"}}, {"CC", {"BB"}}};
    return applyMonthlyStatus(resolve(w, 1), {}, adj);
}
static WorldDelta operation(const WorldState &w, const std::string &actor, const std::string &ops) {
    Json::Value response;
    auto e = parse(
        R"({"description":"Fixture decision","blocked_by":"none","causes":[],"background":false})");
    e["actor"] = actor;
    e["operations"] = parse(ops);
    response["events"].append(e);
    return compileOperations(w, response, {});
}
int main() {
    try {
        auto w = fixture();
        check(fighting(w, "AA", "BB") && !fighting(w, "AA", "CC"),
              "Participants on same side are not enemies");
        w.commitments["truce"] = parse(
            R"({"name":"Truce","text":"Partial truce","actors":["AA","BB"],"kind":"ceasefire","status":"open"})");
        check(!fighting(w, "AA", "BB") && fighting(w, "BB", "CC"),
              "Ceasefire pauses only covered pair");
        auto r = month(w);
        check(r.state.conflicts["war"]["last_month_casualties"].asInt64() > 0,
              "Other pair still incurs fighting toll");
        check(r.state.actors["AA"]["status"]["military"]["active"] ==
                  w.actors["AA"]["status"]["military"]["active"],
              "Ceasefire party incurs no unrelated combat losses");
        auto d = operation(
            w, "BB", R"([{"action":"set_conflict_intensity","conflict":"war","intensity":"low"}])");
        auto changed = applyDelta({w, {}}, d, Json::Value(Json::objectValue)).state;
        check(changed.conflicts["war"]["postures"]["CC"] == "high" &&
                  pairPosture(changed.conflicts["war"], "BB", "CC") == 2,
              "One party cannot lower another's posture");
        d = operation(w, "BB", R"([{"action":"resolve_conflict","conflict":"war"}])");
        check(d.changes.empty() && d.events[0]["failed"] == true,
              "Unilateral war termination rejected");
        auto proposed = operation(
            w, "BB",
            R"([{"action":"add_commitment","actors":["AA","BB"],"name":"Separate peace","kind":"peace_treaty","text":"Settle A-B only","terms":[{"action":"end_conflict","conflict":"war"}]}])");
        auto pending = applyDelta({w, {}}, proposed, Json::Value(Json::objectValue)).state;
        auto pid = pending.proposals.getMemberNames()[0];
        pending.turn++;
        Json::Value responses, answer, response;
        answer["proposal"] = pid;
        answer["actor"] = "AA";
        answer["accept"] = true;
        answer["revision"] = 1;
        responses.append(answer);
        response["events"] = Json::arrayValue;
        auto settlement = compileOperations(pending, response, {}, responses);
        auto peace = applyDelta({pending, {}}, settlement, Json::Value(Json::objectValue)).state;
        check(isOpen(peace.conflicts["war"]) && !fighting(peace, "AA", "BB") &&
                  fighting(peace, "BB", "CC"),
              "Accepted separate settlement leaves the third party's hostility intact");
        auto duplicate = w;
        duplicate.conflicts["a-duplicate"] = duplicate.conflicts["war"];
        auto repeated = month(duplicate);
        check(repeated.state.actors == r.state.actors &&
                  repeated.state.territories == r.state.territories,
              "Duplicate declaration cannot multiply exposure or losses");
        auto legacy = w;
        legacy.conflicts["war"].removeMember("hostile_pairs");
        legacy.conflicts["war"].removeMember("postures");
        legacy.commitments["allies"] =
            parse(R"({"actors":["AA","CC"],"kind":"alliance","status":"open"})");
        initializeConflictPairs(legacy);
        check(legacy.conflicts["war"]["pair_provenance"] == "legacy_alliance_filtered_fallback" &&
                  !fighting(legacy, "AA", "CC"),
              "Ambiguous legacy coalition labels its fallback and excludes known allies");
        check(encode(decode(encode(repeated.state))) == encode(repeated.state),
              "Pair accounting round-trips");
        std::cout << checks << " hostile-pair checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
