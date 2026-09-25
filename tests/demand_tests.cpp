#include "localhistoria/demands.h"
#include "localhistoria/domestic.h"
#include "localhistoria/lifecycle.h"
#include "localhistoria/store.h"
#include "localhistoria/world.h"
#include <fstream>
#include <iostream>
using namespace lh;
static void check(bool ok, const std::string &text) {
    if (!ok)
        throw std::runtime_error(text);
}
template <class F> static void rejects(F f, const std::string &text) {
    try {
        f();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Expected rejection: " + text);
}
// Two small nations of three regions each; AA is the player.
static WorldState world() {
    WorldState w;
    w.campaignId = "demand-test";
    w.player = "AA";
    w.actors = parse(
        R"({"AA":{"name":"Aland","color":"#123456"},"BB":{"name":"Bland","color":"#654321"}})");
    for (const auto &a : w.actors.getMemberNames()) {
        w.actors[a]["status"] = parse(
            R"({"baseline_stability":60,"stability":60,"seeded_stability":60,"gdp_growth":0,"pop_growth":0,"government":"parliamentary_republic"})");
        for (int n = 1; n <= 3; ++n) {
            auto id = a + std::to_string(n);
            auto &t = w.territories[id];
            t["owner"] = t["controller"] = t["initial_affiliation"] = a;
            t["name"] = "Province " + id;
            t["kind"] = "ADM1";
            t["population"] = 1000000;
            t["gdp_usd"] = Json::Int64(100000000000LL);
            t["longitude"] = t["latitude"] = 0;
        }
    }
    return w;
}
static Json::Value geography(const WorldState &w) {
    Json::Value g;
    for (const auto &id : w.territories.getMemberNames())
        g[id] =
            parse(R"({"type":"Polygon","coordinates":[[[-1,-1],[1,-1],[1,1],[-1,1],[-1,-1]]]})");
    return g;
}
struct Step {
    Resolution result;
    WorldDelta delta;
    Json::Value refs;
    const WorldState &state() const { return result.state; }
};
static Step step(const WorldState &w, const Json::Value &events = Json::Value(Json::arrayValue),
                 const Json::Value &responses = Json::Value(Json::arrayValue),
                 Json::Value refs = Json::nullValue) {
    if (refs.isNull())
        refs = lifecycleReference(w);
    auto base = beginTurn(w, refs);
    Json::Value response;
    response["events"] = events.isNull() ? Json::Value(Json::arrayValue) : events;
    auto d = compileOperations(base.state, response, {},
                               responses.isNull() ? Json::Value(Json::arrayValue) : responses);
    refs["delta"] = encode(d);
    return {finishTurn(base, d, {}, geography(w), refs), d, refs};
}
// A regional initiative from `region`, attributed to whoever controls it.
static Json::Value initiative(const WorldState &w, const std::string &region,
                              const std::string &kind, const std::string &benefit = "none",
                              const std::string &text = "",
                              const std::string &relations = "neutral") {
    auto e = parse(
        R"({"blocked_by":"none","background":false,"causes":[],"initiative_type":"independent_regional"})");
    e["actor"] = w.territories[region]["controller"];
    e["regional_actor"] = region;
    e["description"] = "The province presses the capital " + region + " " + kind;
    Json::Value op;
    op["action"] = "regional_initiative";
    op["region"] = region;
    op["agenda"] = "Home rule";
    op["relations"] = relations;
    op["demand"] = text;
    op["demand_kind"] = kind;
    op["project_benefit"] = benefit;
    e["operations"].append(op);
    return e;
}
static Json::Value one(const Json::Value &e) {
    Json::Value a(Json::arrayValue);
    a.append(e);
    return a;
}
static Json::Value demand(const WorldState &w, const std::string &region, int n = 0) {
    return w.regionalPolitics[region]["demands"][n];
}
static Json::Value answer(const WorldState &w, const std::string &region, bool accept, int n = 0) {
    auto d = demand(w, region, n);
    Json::Value a;
    a["demand"] = d["id"];
    a["region"] = region;
    a["accept"] = accept;
    if (d.isMember("revision"))
        a["revision"] = d["revision"];
    return one(a);
}
static bool mentions(const WorldDelta &d, const std::string &text) {
    for (const auto &e : d.events)
        if (e["text"].asString().find(text) != std::string::npos)
            return true;
    return false;
}
int main() {
    try {
        auto w = world();
        // Version selection: demand terms need project records, and unknown versions are refused.
        {
            auto refs = lifecycleReference(w);
            check(refs["demand_version"] == demandVersion, "New turns select demand version 1");
            auto orphan = refs;
            orphan.removeMember("project_version");
            rejects([&] { beginTurn(w, orphan); }, "demand terms without projects");
            auto unknown = refs;
            unknown["demand_version"] = 2;
            rejects([&] { beginTurn(w, unknown); }, "unknown demand version");
        }
        // Filing: terms are derived in C++ from the kind, bounded and immutable.
        auto filed = step(w, one(initiative(w, "AA1", "regional_project", "infrastructure",
                                            "Build a railway to the coast")));
        auto terms = demand(filed.state(), "AA1")["terms"];
        check(terms["kind"] == "regional_project" && terms["category"] == "investment" &&
                  terms["objective"] == "Build a railway to the coast" && terms["months"] == 12 &&
                  terms["estimated_cost_usd"].asInt64() == 600000000 &&
                  terms["required_consent"][0] == "AA" &&
                  demand(filed.state(), "AA1")["revision"] == 1 &&
                  demand(filed.state(), "AA1")["status"] == "pending",
              "A funded demand records its project, cost and consent");
        auto blank = step(w, one(initiative(w, "AA2", "independence")));
        check(demand(blank.state(), "AA2")["text"] == "Independence for Province AA2" &&
                  demand(blank.state(), "AA2")["terms"]["new_state"] == "Province AA2" &&
                  demand(blank.state(), "AA2")["terms"]["regions"][0] == "AA2",
              "A typed demand without text is still filed with bounded terms");
        auto vague = step(w, one(initiative(w, "AA3", "regional_project", "none", "More money")));
        check(demand(vague.state(), "AA3")["terms"] == parse(R"({"kind":"political"})"),
              "An incomplete funded request is only a political one");
        auto quiet = step(w, one(initiative(w, "AA3", "political")));
        check(quiet.state().regionalPolitics["AA3"]["demands"].empty(),
              "A political kind without text files nothing");
        // Seen live: Qwen3.5-4B wrote demand "none" with kind political to mean no demand.
        auto spelled = step(w, one(initiative(w, "AA3", "political", "administration", "None")));
        check(spelled.state().regionalPolitics["AA3"]["demands"].empty(),
              "A demand spelled \"none\" files nothing");
        // Answers need the demand's exact revision, once.
        const auto pending = filed.state();
        auto missing = answer(pending, "AA1", true);
        missing[0].removeMember("revision");
        rejects([&] { step(pending, {}, missing); }, "typed answer without revision");
        auto stale = answer(pending, "AA1", true);
        stale[0]["revision"] = 2;
        rejects([&] { step(pending, {}, stale); }, "stale revision");
        auto twice = answer(pending, "AA1", true);
        twice.append(twice[0]);
        rejects([&] { step(pending, {}, twice); }, "duplicate answer");
        // Declining and ignoring behave as before.
        auto declined = step(pending, {}, answer(pending, "AA1", false));
        check(demand(declined.state(), "AA1")["status"] == "rejected" &&
                  !demand(declined.state(), "AA1").isMember("fulfillment") &&
                  declined.state().actors["AA"]["status"]["projects"].empty() &&
                  declined.state().regionalPolitics["AA1"]["relations"] == "strained",
              "A declined demand starts nothing and sours relations");
        auto ignored = pending;
        for (int n = 0; n < 3; ++n)
            ignored = step(ignored).state();
        check(demand(ignored, "AA1")["status"] == "expired" &&
                  demand(ignored, "AA1")["lapse_reason"] == "unanswered",
              "An unanswered typed demand lapses at its deadline");
        // Accepting a funded demand is not fulfilling it: the project starts in the region.
        auto accepted = step(pending, {}, answer(pending, "AA1", true));
        const auto &acceptedState = accepted.state();
        auto f = demand(acceptedState, "AA1")["fulfillment"];
        auto projectId = domesticProjectId("AA", "investment", "AA1", "infrastructure");
        auto project = acceptedState.actors["AA"]["status"]["projects"][projectId];
        check(demand(acceptedState, "AA1")["status"] == "accepted" && f["state"] == "in_progress" &&
                  f["project"] == projectId && f["deadline_turn"] == f["turn"].asInt() + 24 &&
                  f["turn"] == pending.turn + 1 && project["target"] == "AA1" &&
                  project["paid"].asDouble() == 1 &&
                  acceptedState.regionalPolitics["AA1"]["relations"] == "cooperative",
              "An accepted funded demand starts its project and eases relations once");
        check(mentions(accepted.delta, "accepted Province AA1's demand") &&
                  mentions(accepted.delta, "fulfilled only when it completes") &&
                  !mentions(accepted.delta, "granted"),
              "Acceptance text never claims delivery");
        rejects([&] { step(acceptedState, {}, answer(pending, "AA1", true)); },
                "answering a settled demand again");
        // Tampering with terms or outcomes in a delta is refused.
        {
            auto tamper = accepted.delta;
            for (auto &c : tamper.changes)
                if (c["kind"] == "regional")
                    c["after"]["demands"][0]["terms"]["objective"] = "Something else";
            auto refs = accepted.refs;
            refs["delta"] = encode(tamper);
            rejects([&] { finishTurn(beginTurn(pending, refs), tamper, {}, geography(w), refs); },
                    "altered terms");
        }
        // The project runs on the common allowance and completes once; the demand is then
        // fulfilled exactly once, as the following month opens.
        auto progress = acceptedState;
        progress.regionalPolitics["AA1"]["relations"] = "neutral";
        WorldDelta fulfilledDelta;
        int fulfilledAt = -1;
        for (int n = 0; n < 14 && fulfilledAt < 0; ++n) {
            auto next = step(progress);
            if (mentions(next.delta, "fulfilled Province AA1's demand")) {
                fulfilledDelta = next.delta;
                fulfilledAt = next.state().turn;
            }
            progress = next.state();
        }
        check(fulfilledAt > 0 && demand(progress, "AA1")["fulfillment"]["state"] == "fulfilled" &&
                  progress.actors["AA"]["status"]["projects"][projectId]["state"] == "completed" &&
                  progress.regionalPolitics["AA1"]["relations"] == "cooperative",
              "Completed work fulfils the demand and earns its delivery step");
        auto after = step(progress);
        check(!mentions(after.delta, "fulfilled") &&
                  after.state().regionalPolitics["AA1"] == progress.regionalPolitics["AA1"],
              "Fulfilment is recorded once");
        // Underfunded work stalls; past the deadline, a ruler holding the region broke its promise.
        auto poor = acceptedState;
        poor.actors["AA"]["status"]["military_spend_usd"] = Json::Int64(100000000000LL);
        poor.actors["AA"]["status"]["projects"][projectId]["monthly_usd"] =
            Json::Int64(100000000000LL);
        auto stalled = step(poor).state();
        check(stalled.actors["AA"]["status"]["projects"][projectId]["delay"] == "funding" &&
                  demand(stalled, "AA1")["fulfillment"]["state"] == "in_progress",
              "Underfunded demand work stalls within the shared allowance");
        stalled.regionalPolitics["AA1"]["demands"][0]["fulfillment"]["deadline_turn"] =
            stalled.turn - 1;
        auto broken = step(stalled);
        check(demand(broken.state(), "AA1")["fulfillment"]["state"] == "failed" &&
                  broken.state().regionalPolitics["AA1"]["relations"] == "neutral" &&
                  mentions(broken.delta, "broke its promise"),
              "A missed deadline breaks the promise and costs a relations step");
        {
            auto forged = broken.delta;
            for (auto &c : forged.changes)
                if (c["kind"] == "regional")
                    c["after"]["demands"][0]["fulfillment"]["state"] = "political";
            auto refs = broken.refs;
            refs["delta"] = encode(forged);
            rejects([&] { finishTurn(beginTurn(stalled, refs), forged, {}, geography(w), refs); },
                    "forged fulfilment outcome");
        }
        // Cancelling the work breaks the promise too.
        auto cancel = parse(
            R"({"actor":"AA","blocked_by":"none","background":false,"causes":[],"operations":[],"outcome_kind":"investment","effects":[{"actor":"AA","economy":"none","stability":"none"}]})");
        cancel["description"] = "Aland cancels the coastal railway";
        cancel["project"]["id"] = projectId;
        cancel["project"]["action"] = "cancel";
        auto cancelled = step(acceptedState, one(cancel)).state();
        check(cancelled.actors["AA"]["status"]["projects"][projectId]["state"] == "cancelled",
              "The project can be cancelled");
        auto afterCancel = step(cancelled);
        check(demand(afterCancel.state(), "AA1")["fulfillment"]["state"] == "failed" &&
                  demand(afterCancel.state(), "AA1")["fulfillment"]["reason"] ==
                      "the funded work was cancelled",
              "Cancelled demand work is a broken promise");
        // Occupation at the deadline lapses without blame; a change of owner lapses at once.
        auto occupied = acceptedState;
        occupied.territories["AA1"]["controller"] = "BB";
        occupied.regionalPolitics["AA1"]["demands"][0]["fulfillment"]["deadline_turn"] =
            occupied.turn - 1;
        auto lapsed = step(occupied).state();
        check(demand(lapsed, "AA1")["fulfillment"]["state"] == "lapsed" &&
                  lapsed.regionalPolitics["AA1"]["relations"] == "cooperative",
              "Occupation at the deadline lapses the promise without penalty");
        auto ceded = acceptedState;
        ceded.territories["AA1"]["owner"] = ceded.territories["AA1"]["controller"] = "BB";
        check(demand(step(ceded).state(), "AA1")["fulfillment"]["state"] == "lapsed",
              "A region passed to another owner lapses its accepted demand");
        // A regional initiative the same month cannot undo a settlement's relations.
        auto contrary =
            step(pending, one(initiative(pending, "AA1", "none", "none", "", "strained")),
                 answer(pending, "AA1", true));
        check(contrary.state().regionalPolitics["AA1"]["relations"] == "cooperative",
              "A later initiative cannot erase a settlement's relations step");
        // Independence: only the owner holding the region can grant it, and it moves that region
        // alone to the new state in the acceptance month.
        const auto asking = blank.state();
        auto free = step(asking, {}, answer(asking, "AA2", true));
        const auto &freeState = free.state();
        auto newState = demand(freeState, "AA2")["fulfillment"]["actor"].asString();
        check(!newState.empty() && freeState.hasActor(newState) &&
                  freeState.actors[newState]["name"] == "Province AA2" &&
                  freeState.territories["AA2"]["owner"] == newState &&
                  freeState.territories["AA2"]["controller"] == newState &&
                  freeState.territories["AA1"]["owner"] == "AA" &&
                  freeState.territories["AA3"]["owner"] == "AA" &&
                  demand(freeState, "AA2")["fulfillment"]["state"] == "fulfilled" &&
                  mentions(free.delta, "Independence took effect"),
              "A valid independence settlement creates the state and transfers only its region");
        auto foreign = w;
        foreign.territories["BB1"]["controller"] = "AA";
        auto foreignAsk = step(foreign, one(initiative(foreign, "BB1", "independence"))).state();
        check(demand(foreignAsk, "BB1")["terms"]["required_consent"][0] == "BB",
              "Independence names the owner's consent");
        auto refused = step(foreignAsk, {}, answer(foreignAsk, "BB1", true));
        check(demand(refused.state(), "BB1")["fulfillment"]["state"] == "blocked" &&
                  refused.state().actors.size() == 2 &&
                  refused.state().territories["BB1"]["owner"] == "BB" &&
                  refused.state().regionalPolitics["BB1"]["relations"] == "neutral" &&
                  mentions(refused.delta, "holding it is not enough") &&
                  !mentions(refused.delta, "took effect"),
              "Occupation cannot grant another country's sovereignty");
        auto last = w;
        for (auto id : {"AA2", "AA3"})
            last.territories[id]["owner"] = last.territories[id]["controller"] = "BB";
        auto lastAsk = step(last, one(initiative(last, "AA1", "independence"))).state();
        check(demand(step(lastAsk, {}, answer(lastAsk, "AA1", true)).state(),
                     "AA1")["fulfillment"]["state"] == "blocked",
              "A nation cannot grant away its last region");
        // Two independence grants in one month: the second is blocked and rolled back cleanly.
        auto both = step(asking, one(initiative(asking, "AA3", "independence"))).state();
        auto pair = answer(both, "AA2", true);
        pair.append(answer(both, "AA3", true)[0]);
        auto one_ = step(both, {}, pair);
        int created = 0;
        for (const auto &id : one_.state().actors.getMemberNames())
            created += id != "AA" && id != "BB";
        check(created == 1 && demand(one_.state(), "AA3")["fulfillment"]["state"] == "blocked" &&
                  one_.state().territories["AA3"]["owner"] == "AA" &&
                  demand(one_.state(), "AA2")["fulfillment"]["state"] == "fulfilled",
              "A failed second founding rolls back and is recorded as blocked");
        // A legacy free-text demand is never permission to change land or claim it happened.
        auto legacy = w;
        legacy.engine["lifecycle_version"] = 1;
        legacy.regionalPolitics["AA1"] = parse(
            R"({"agenda":"Home rule","relations":"neutral","supporting_events":[],"demands":[{"id":"t0-sovereign","text":"sovereign_recognition","status":"pending","authority_at_creation":"AA","answer_by_turn":3}]})");
        auto withRevision = answer(legacy, "AA1", true);
        withRevision[0]["revision"] = 1;
        rejects([&] { step(legacy, {}, withRevision); }, "legacy answer with revision");
        auto recognised = step(legacy, {}, answer(legacy, "AA1", true));
        check(demand(recognised.state(), "AA1")["status"] == "accepted" &&
                  demand(recognised.state(), "AA1")["fulfillment"]["state"] == "political" &&
                  recognised.state().actors.size() == 2 &&
                  recognised.state().territories["AA1"]["owner"] == "AA" &&
                  mentions(recognised.delta, "Accepted for negotiation only") &&
                  !mentions(recognised.delta, "granted"),
              "A legacy sovereign_recognition demand is accepted for negotiation only");
        // An AI ruler answering through respond_regional starts the same funded work.
        auto theirs = step(w, one(initiative(w, "BB1", "regional_project", "public_services",
                                             "Open district clinics")))
                          .state();
        auto respond = parse(
            R"({"actor":"BB","blocked_by":"none","background":false,"causes":[],"initiative_type":"reactive","operations":[{"action":"respond_regional","region":"BB1","response":"accepted"}]})");
        respond["description"] = "Bland agrees to fund clinics";
        respond["operations"][0]["demand_id"] = demand(theirs, "BB1")["id"];
        auto answered = step(theirs, one(respond));
        auto theirId = domesticProjectId("BB", "reform", "BB1", "public_services");
        check(answered.state().actors["BB"]["status"]["projects"][theirId]["target"] == "BB1" &&
                  demand(answered.state(), "BB1")["fulfillment"]["state"] == "in_progress" &&
                  mentions(answered.delta, "fulfilled only when it completes"),
              "An AI acceptance starts the demand's project");
        // Save/reopen keeps terms, links and progress.
        check(encode(step(decode(parse(dump(encode(acceptedState))))).state()) ==
                  encode(step(acceptedState).state()),
              "Save/reopen preserves demand terms and fulfilment");
        // Export an old-rules month followed by a funded demand's filing, acceptance and delivery.
        Json::Value exportDoc;
        exportDoc["initial_state"] = encode(w);
        exportDoc["geography"] = geography(w);
        auto append = [&](const Step &s) {
            Json::Value t;
            t["reference"] = s.refs;
            t["state"] = encode(s.state());
            t["events"] = encode(s.result.events);
            t["intentions"] = Json::objectValue;
            exportDoc["turns"].append(t);
        };
        auto oldRefs = lifecycleReference(w);
        oldRefs.removeMember("demand_version");
        auto current =
            step(w, one(initiative(w, "AA2", "political", "none", "Local assembly")), {}, oldRefs);
        check(!demand(current.state(), "AA2").isMember("terms"), "Old rules file untyped demands");
        append(current);
        current = step(current.state(), one(initiative(current.state(), "AA1", "regional_project",
                                                       "administration", "Modern tax office")));
        append(current);
        current = step(current.state(), {}, answer(current.state(), "AA1", true));
        append(current);
        for (int n = 0; n < 20; ++n) {
            current = step(current.state());
            append(current);
        }
        check(demand(current.state(), "AA1")["fulfillment"]["state"] == "fulfilled",
              "The exported campaign reaches fulfilment");
        exportDoc["campaign"] = encode(current.state());
        std::ofstream("demand-replay.json") << dump(exportDoc);
        std::cout << "Typed demand, consent, fulfilment, rollback and replay checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
