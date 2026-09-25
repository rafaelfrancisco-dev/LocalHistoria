#include "localhistoria/domestic.h"
#include "localhistoria/lifecycle.h"
#include "localhistoria/store.h"
#include <chrono>
#include <fstream>
#include <iostream>
using namespace lh;
static void check(bool ok, const std::string &text) {
    if (!ok)
        throw std::runtime_error(text);
}
template <class F> static void rejects(F f) {
    try {
        f();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Expected rejection");
}
static WorldState world() {
    WorldState w;
    w.campaignId = "domestic-test";
    w.player = "AA";
    w.actors = parse(
        R"({"AA":{"name":"Aland","color":"#123456"},"BB":{"name":"Bland","color":"#123456"}})");
    for (const auto &a : w.actors.getMemberNames()) {
        w.actors[a]["status"] = parse(
            R"({"baseline_stability":60,"stability":60,"seeded_stability":60,"gdp_growth":0,"pop_growth":0,"government":"parliamentary_republic"})");
        for (int n = 1; n <= 2; ++n) {
            auto id = a + std::to_string(n);
            auto &t = w.territories[id];
            t["owner"] = t["controller"] = t["initial_affiliation"] = a;
            t["name"] = id;
            t["kind"] = "ADM1";
            t["population"] = 1000000;
            t["gdp_usd"] = Json::Int64(100000000000LL);
            t["longitude"] = t["latitude"] = 0;
        }
    }
    return w;
}
static Json::Value event(const std::string &text, const std::string &kind = "policy",
                         const std::string &actor = "AA") {
    auto e = parse(
        R"({"blocked_by":"none","background":false,"causes":[],"operations":[],"effects":[]})");
    e["actor"] = actor;
    e["description"] = text;
    e["outcome_kind"] = kind;
    Json::Value x;
    x["actor"] = actor;
    x["economy"] = "none";
    x["stability"] = "growth";
    e["effects"].append(x);
    return e;
}
static Json::Value project(const std::string &target, const std::string &category = "reform",
                           const std::string &benefit = "administration") {
    auto e = event("Improve services in " + target, category);
    e["project"]["id"] = "";
    e["project"]["target"] = target;
    e["project"]["objective"] = "Improve services in " + target;
    e["project"]["benefit"] = benefit;
    return e;
}
struct Step {
    Resolution result;
    WorldDelta delta;
    Json::Value refs;
};
static Step step(const WorldState &w, const Json::Value &events = Json::Value(Json::arrayValue)) {
    auto refs = lifecycleReference(w);
    auto base = beginTurn(w, refs);
    Json::Value response;
    response["events"] = events;
    auto d = compileOperations(base.state, response, {});
    refs["delta"] = encode(d);
    Json::Value geography;
    for (const auto &id : w.territories.getMemberNames())
        geography[id] = parse(R"({"type":"Polygon","coordinates":[[[-1,-1],[1,-1],[1,1],[-1,1],[-1,-1]]]})");
    return {finishTurn(base, d, {}, geography, refs), d, refs};
}
static Json::Value list(const Json::Value &e, int n = 1) {
    Json::Value es(Json::arrayValue);
    for (int i = 0; i < n; ++i)
        es.append(e);
    return es;
}
static double weight(const WorldDelta &d) {
    double sum = 0;
    for (const auto &e : d.events)
        for (const auto &x : e["effects"])
            sum += x.get("weight", 1).asDouble();
    return sum;
}
static Json::Value projects(const WorldState &w) { return w.actors["AA"]["status"]["projects"]; }
int main(int argc, char **) {
    try {
        auto w = world();
        if (argc > 1) {
            // Same fixed 16-nation, 32-region fixture and orders under old/new selected rules.
            for (int n = 0; n < 14; ++n) {
                auto a = "N" + std::to_string(n);
                w.actors[a] = w.actors["AA"];
                for (int r = 1; r <= 2; ++r) {
                    auto t = w.territories["AA1"];
                    t["owner"] = t["controller"] = a;
                    w.territories[a + std::to_string(r)] = t;
                }
            }
            for (int rules = 0; rules < 2; ++rules) {
                auto refs = lifecycleReference(w);
                if (!rules) {
                    refs.removeMember("consequence_version");
                    refs.removeMember("project_version");
                    refs.removeMember("demand_version");
                }
                auto base = beginTurn(w, refs);
                Json::Value response;
                for (int n = 0; n < 8; ++n)
                    response["events"].append(event("Policy number " + std::to_string(n)));
                auto start = std::chrono::steady_clock::now();
                size_t bytes = 0;
                for (int i = 0; i < 2000; ++i) {
                    auto d = compileOperations(base.state, response, {});
                    auto result = finishTurn(base, d, {}, {}, refs);
                    if (i == 1999)
                        bytes = dump(result.state.actors).size();
                }
                std::cout << "rules=" << rules << " milliseconds="
                          << std::chrono::duration<double, std::milli>(
                                 std::chrono::steady_clock::now() - start)
                                 .count()
                          << " actor_bytes=" << bytes << '\n';
            }
            return 0;
        }
        auto one = step(w, list(event("Maintain policy"))),
             eight = step(w, list(event("Maintain policy"), 8));
        check(weight(one.delta) == weight(eight.delta) && eight.delta.events.size() == 8,
              "Exact repeats keep events, not rewards");
        check(encode(one.result.state) == encode(eight.result.state),
              "One/eight same aggregate state");
        auto paraphrases = Json::Value(Json::arrayValue);
        for (int i = 0; i < 8; ++i)
            paraphrases.append(event("Paraphrase " + std::to_string(i)));
        check(weight(step(w, paraphrases).delta) == 1, "Paraphrases share envelope");
        for (auto &e : paraphrases)
            e["effects"][0]["stability"] = "decline";
        check(weight(step(w, paraphrases).delta) == 1, "Negative duplicates share envelope");
        for (const auto &actor : {"AA", "BB"}) {
            auto start = w;
            double total = 0;
            for (int m = 0; m < 36; ++m) {
                auto next = step(
                    start, list(event("Reworded policy " + std::to_string(m), "policy", actor)));
                total += weight(next.delta);
                start = next.result.state;
            }
            check(total == 1.75,
                  "Continuous paraphrases have a finite cumulative envelope for every actor");
        }
        Json::Value ledger;
        for (int i = 0; i < 200; ++i)
            recordConsequence(ledger, 1, "key" + std::to_string(i), "policy", true);
        check(ledger.size() <= consequenceCapacity, "Ledger capacity");
        expireConsequences(ledger, 25);
        check(ledger.empty(), "Ledger expiry");
        auto bad = event("Failed seizure labelled reform", "reform");
        bad["operations"] = parse(R"([{"action":"occupy","region":"missing","by":"AA"}])");
        bad["project"] = project("AA1")["project"];
        auto failed = step(w, list(bad));
        check(weight(failed.delta) == 0 && !failed.delta.events[0].isMember("project"),
              "Failure cannot reclassify as reward");
        auto p1 = project("AA1"), p2 = project("AA2");
        auto orders = list(p1);
        orders.append(p2);
        orders.append(p1);
        auto started = step(w, orders);
        auto ps = projects(started.result.state);
        check(ps.size() == 2 && weight(started.delta) == 0,
              "Distinct targets create distinct projects without startup rewards");
        for (const auto &p : ps)
            check(p["paid"].asDouble() == 1, "Repeated project receives one monthly allocation");
        auto withSite = p1;
        withSite["operations"] = parse(R"([{"action":"build","actor":"AA","kind":"event_site","name":"Reform launch","region":"AA1"}])");
        auto marked = step(w, list(withSite));
        check(projects(marked.result.state).size() == 1 && weight(marked.delta) == 0,
              "A decorative event site does not replace or immediately reward a funded project");
        auto invalid = p1;
        invalid["project"]["target"] = "BB1";
        check(projects(step(w, list(invalid)).result.state).empty(), "Foreign target rejected");
        invalid = p1;
        invalid["project"]["benefit"] = "magic";
        check(projects(step(w, list(invalid)).result.state).empty(),
              "Unsupported benefit rejected");
        auto firstId = ps.getMemberNames().front();
        invalid = p1;
        invalid["actor"] = "BB";
        invalid["project"]["id"] = firstId;
        check(!step(started.result.state, list(invalid)).delta.events[0].isMember("project"),
              "Foreign continuation rejected");
        auto renamed = p1;
        renamed["description"] = "Restart differently";
        renamed["project"]["objective"] = "Renamed";
        auto continued = step(started.result.state, list(renamed));
        check(projects(continued.result.state).size() == 2 &&
                  projects(continued.result.state)[firstId]["started"] == 1,
              "Rename continues identity");
        auto done = continued.result.state;
        for (int i = 0; i < 20; ++i)
            done = step(done).result.state;
        for (const auto &p : projects(done))
            check(p["state"] == "completed", "Projects complete independently");
        auto restart = step(done, list(renamed));
        check(projects(restart.result.state) == projects(done),
              "Completed project cannot restart or repay");
        auto cancel = p1;
        cancel["project"]["id"] = firstId;
        cancel["project"]["action"] = "cancel";
        auto cancelled = step(started.result.state, list(cancel));
        check(projects(cancelled.result.state)[firstId]["state"] == "cancelled" &&
                  projects(cancelled.result.state)[firstId]["paid"] == ps[firstId]["paid"],
              "Cancellation keeps paid work without progress or refund");
        auto resumed = cancel;
        resumed["project"]["action"] = "continue";
        auto afterResume = step(cancelled.result.state, list(resumed));
        check(projects(afterResume.result.state)[firstId]["paid"].asDouble() == 2 &&
                  projects(afterResume.result.state)[firstId]["started"] == 1,
              "Explicit resume keeps project age and previous expenditure");
        auto distinct = p1;
        distinct["project"]["benefit"] = "public_services";
        check(projects(step(done, list(distinct)).result.state).size() == 3,
              "Different supported purpose has its own identity");
        auto sector = p1;
        sector["project"].removeMember("benefit");
        sector["project"]["sector"] = "public_services";
        auto viaSector = projects(step(w, list(sector)).result.state);
        check(viaSector.size() == 1 && viaSector[viaSector.getMemberNames().front()]["benefit"] == "public_services" &&
                  !viaSector[viaSector.getMemberNames().front()].isMember("sector"),
              "Schema sector is stored as the project's benefit");
        sector["project"]["benefit"] = "administration";
        check(projects(step(w, list(sector)).result.state).empty(), "Sector with a second benefit rejected");
        auto poor = started.result.state;
        poor.actors["AA"]["status"]["military_spend_usd"] = Json::Int64(100000000000LL);
        for (auto &p : poor.actors["AA"]["status"]["projects"])
            p["monthly_usd"] = Json::Int64(100000000000LL);
        auto delayed = step(poor).result.state;
        bool stalled = false;
        for (const auto &p : projects(delayed))
            stalled = stalled || p["delay"] == "funding";
        check(stalled, "Projects share limited implementation allowance");
        auto occupied = started.result.state;
        occupied.territories["AA1"]["controller"] = "BB";
        auto paused = step(occupied).result.state;
        check(projects(paused)[firstId]["paid"] == ps[firstId]["paid"],
              "Loss of control pauses paid work");
        auto full = w;
        full.engine["project_version"] = 1;
        for (unsigned i = 0; i < projectCapacity; ++i)
            full.actors["AA"]["status"]["projects"]["completed" + std::to_string(i)]["state"] =
                "completed";
        check(projects(step(full, list(p1)).result.state).size() == projectCapacity,
              "Completed records count toward bounded capacity");
        auto legacy = w;
        legacy.actors["AA"]["status"]["efforts"]["investment"] =
            parse(R"({"started":-5,"months":12,"paid":11,"monthly_usd":100})");
        auto saved = encode(legacy);
        auto ref = lifecycleReference(legacy);
        auto begun = beginTurn(legacy, ref);
        auto migrated = projects(begun.state);
        check(migrated.size() == 1 && migrated.begin()->operator[]("paid") == 11,
              "Migration keeps paid work");
        check(encode(legacy) == saved, "Preparing migration cannot publish it");
        auto legacyDone = step(legacy);
        check(projects(legacyDone.result.state).begin()->operator[]("state") == "completed",
              "Legacy benefit completes on its original schedule");
        auto retry = step(decode(saved));
        check(encode(retry.result.state) == encode(legacyDone.result.state) &&
                  encode(retry.result.events) == encode(legacyDone.result.events),
              "Migration retry preserves state and events");
        auto unknown = ref;
        unknown["project_version"] = 2;
        rejects([&] { beginTurn(legacy, unknown); });
        // Commit failure rolls back migrations, project records and consequence ledgers together.
        Store store(":memory:");
        store.create(legacy, false);
        auto job = store.submit(0, "failed", "{}")["id"].asString();
        rejects([&] { store.commit(job, legacyDone.result, {}, legacyDone.refs, true); });
        check(store.campaign() == saved, "Failed commit leaves source save unchanged");
        store.commit(job, legacyDone.result, {}, legacyDone.refs);
        check(store.campaign() == encode(legacyDone.result.state), "Committed migration persists");
        check(encode(step(decode(parse(dump(encode(started.result.state))))).result.state) ==
                  encode(step(started.result.state).result.state),
              "Save/reopen preserves funding and ledgers");
        auto tamper = started.delta;
        tamper.events[0]["project"]["paid"] = 100;
        rejects([&] {
            finishTurn(beginTurn(w, lifecycleReference(w)), tamper, {}, {}, lifecycleReference(w));
        });
        // Export an old lifecycle month followed by projects, repeated consequences and completion.
        Json::Value exportDoc;
        exportDoc["initial_state"] = encode(w);
        exportDoc["geography"] = Json::objectValue;
        auto oldRefs = lifecycleReference(w);
        oldRefs.removeMember("project_version");
        oldRefs.removeMember("demand_version");
        oldRefs.removeMember("consequence_version");
        auto old = finishTurn(beginTurn(w, oldRefs), {}, {}, {}, oldRefs);
        auto append = [&](const Resolution &r, const Json::Value &refs) {
            Json::Value t;
            t["reference"] = refs;
            t["state"] = encode(r.state);
            t["events"] = encode(r.events);
            t["intentions"] = Json::objectValue;
            exportDoc["turns"].append(t);
        };
        append(old, oldRefs);
        w = old.state;
        for (int i = 0; i < 22; ++i) {
            auto es = i == 0 ? orders : list(event("Policy " + std::to_string(i)));
            auto next = step(w, es);
            append(next.result, next.refs);
            w = next.result.state;
        }
        exportDoc["campaign"] = encode(w);
        std::ofstream("domestic-replay.json") << dump(exportDoc);
        std::cout
            << "Domestic consequence, project, migration, rollback and replay checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
