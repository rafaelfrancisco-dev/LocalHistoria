#include "localhistoria/lifecycle.h"
#include <fstream>
#include <iostream>
using namespace lh;
static void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> static void rejects(F action) {
    try {
        action();
    } catch (const ValidationError &) {
        return;
    }
    throw std::runtime_error("Expected validation rejection");
}
int main() {
    try {
        std::ifstream file(std::string(FIXTURE_DIR) + "/legacy-lifecycle-v4.json");
        auto fixture = parse(std::string(std::istreambuf_iterator<char>(file), {}));
        auto w = decode(fixture["initial_state"]);
        const auto initial = encode(w);
        auto refs = lifecycleReference(w);
        auto first = beginTurn(w, refs), retry = beginTurn(decode(initial), refs);
        check(encode(w) == initial, "Preparing a cancelled turn cannot change its source");
        check(encode(first.state) == encode(retry.state) &&
                  encode(first.events) == encode(retry.events),
              "Retry repeats migration and lifecycle exactly");
        check(first.state.engine["lifecycle_version"] == realismLifecycleVersion,
              "Migration marks only the working copy");
        auto migrated = w;
        migrated.engine = first.state.engine;
        auto repeated = beginTurn(migrated, refs);
        check(encode(repeated.state) == encode(first.state), "Migration is idempotent");
        check(!lifecycleReference(first.state).isMember("lifecycle_migration"),
              "Subsequent month does not migrate again");
        auto missing = refs;
        missing.removeMember("lifecycle_migration");
        rejects([&] { beginTurn(w, missing); });
        auto unknown = refs;
        unknown["lifecycle_version"] = 900;
        rejects([&] { beginTurn(w, unknown); });
        unknown = refs;
        unknown["status_version"] = 900;
        rejects([&] { beginTurn(w, unknown); });
        unknown = refs;
        unknown["lifecycle_migration"]["population"] = 100;
        rejects([&] { beginTurn(w, unknown); });
        auto legacy = beginTurn(w, Json::Value(Json::objectValue));
        check(legacy.state.engine.empty(), "Legacy resolution does not migrate");
        check(!encode(decode(initial)).isMember("engine"), "Absent accounting stays absent");
        check(encode(decode(encode(first.state))) == encode(first.state), "Accounting round-trip");
        // Engine-owned accounting has no delta collection that a model can address.
        WorldDelta tampered;
        tampered.events = parse(R"([{"id":"t1-tamper","actor":"AA","actors":["AA"],
          "regions":[],"entities":[],"causes":[],"text":"Tamper","background":false}])");
        tampered.changes = parse(R"([{"kind":"engine","id":"accounting","event":"t1-tamper",
          "before":null,"after":{"budget":100},"rationale":"Model injection"}])");
        rejects([&] { finishTurn(first, tampered, {}, fixture["geography"], refs); });
        // Compact compiler text comes only from accepted changes; legacy text stays unchanged.
        {
            auto v2 = first.state, v3 = first.state;
            v2.engine["event_text_version"] = 2;
            v3.engine["event_text_version"] = 3;
            auto response = parse(R"({"events":[{"actor":"AA","blocked_by":"none","causes":[],
              "description":"Misleading duplicated description","operations":[{"action":"build","actor":"AA",
              "kind":"base","name":"Test Base","region":"AA"}]}]})");
            auto oldText = compileOperations(v2, response, {});
            auto compact = compileOperations(v3, response, {});
            check(oldText.events[0]["text"].asString().find("Misleading duplicated") != std::string::npos,
                  "Version 2 preserves historical mechanical prose");
            check(compact.events[0]["text"].asString().find("Test Base") != std::string::npos &&
                  compact.events[0]["text"].asString().find("construction is underway") != std::string::npos &&
                  compact.events[0]["text"].asString().find("Misleading") == std::string::npos,
                  "Version 3 renders accepted construction and its actual status");
            response["events"][0]["operations"][0]["region"] = "BB";
            auto rejected = compileOperations(v3, response, {});
            check(rejected.events[0]["text"].asString().find("construction is underway") == std::string::npos &&
                  rejected.events[0]["text"].asString().find("Rejected build") != std::string::npos,
                  "Rejected construction cannot be narrated as accepted");
        }
        // A real mixed-version export is also checked by the replay executable in CTest.
        Adjacency adj;
        for (auto &id : fixture["adjacency"].getMemberNames())
            for (auto &to : fixture["adjacency"][id])
                adj[id].insert(to.asString());
        w = decode(fixture["campaign"]);
        for (int month = 0; month < 3; ++month) {
            refs = lifecycleReference(w);
            if (month == 1) refs["event_text_version"] = 3;
            auto result = finishTurn(beginTurn(w, refs), {}, adj, fixture["geography"], refs);
            auto replay = finishTurn(beginTurn(decode(encode(w)), parse(dump(refs))), {}, adj,
                                     fixture["geography"], refs);
            check(encode(result.state) == encode(replay.state) &&
                      encode(result.events) == encode(replay.events),
                  "Upgrade boundary replays state and events");
            Json::Value t;
            t["reference"] = refs;
            t["state"] = encode(result.state);
            t["events"] = encode(result.events);
            t["intentions"] = Json::objectValue;
            fixture["turns"].append(t);
            w = result.state;
        }
        fixture["campaign"] = encode(w);
        std::ofstream("mixed-lifecycle-export.json") << dump(fixture);
        std::cout << "Lifecycle migration, retry, round-trip and mixed replay checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
