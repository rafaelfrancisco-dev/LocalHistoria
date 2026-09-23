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
    w.engine["lifecycle_version"] = realismLifecycleVersion;
    return w;
}
static Json::Value event(const std::string &actor, const std::string &text,
                         const std::string &ops) {
    auto e = parse(R"({"blocked_by":"none","causes":[],"background":false})");
    e["actor"] = actor;
    e["description"] = text;
    e["operations"] = parse(ops);
    return e;
}
static WorldDelta compile(const WorldState &w, const Json::Value &e,
                          const Json::Value &answers = Json::Value(Json::arrayValue)) {
    Json::Value response;
    response["events"].append(e);
    return compileOperations(w, response, {}, answers);
}
static Json::Value offer(const std::string &from, const std::string &terms = "[]") {
    auto e = event(
        from, "Aland and Bland agree on everything.",
        R"([{"action":"add_commitment","actors":["AA","BB"],"name":"Accord","text":"Trade terms","kind":"trade_agreement","months":6,"terms":[]}])");
    e["operations"][0]["terms"] = parse(terms);
    return e;
}
static WorldState apply(const WorldState &w, const WorldDelta &d) {
    return applyDelta({w, {}}, d, Json::Value(Json::objectValue)).state;
}
static Json::Value answer(const std::string &id, const std::string &actor, bool yes,
                          int revision = 1) {
    Json::Value a, out(Json::arrayValue);
    a["proposal"] = id;
    a["actor"] = actor;
    a["revision"] = revision;
    a["accept"] = yes;
    out.append(a);
    return out;
}
int main() {
    try {
        auto w = fixture();
        auto response = Json::Value(Json::objectValue);
        response["events"].append(offer("AA"));
        response["events"].append(event("BB", "Bland rejects Aland's offer.", "[]"));
        auto d = compileOperations(w, response, {});
        auto offered = apply(w, d);
        check(offered.commitments.empty() && offered.proposals.size() == 1,
              "Rejection is not consent");
        auto id = offered.proposals.getMemberNames()[0];
        check(offered.proposals[id]["answers"].empty(),
              "Generated player offer has no player signature");
        offered.turn++;
        auto accepted = answer(id, "AA", true);
        accepted.append(answer(id, "BB", true)[0]);
        auto settled = compile(offered, event("BB", "Quiet month", "[]"), accepted);
        auto done = apply(offered, settled);
        check(done.proposals[id]["status"] == "accepted" && done.commitments.isMember(id),
              "Exact signatures settle");
        check(encode(apply(offered, settled)) == encode(done),
              "Retry does not duplicate settlement");
        auto missing = apply(
            offered, compile(offered, event("BB", "Quiet month", "[]"), answer(id, "AA", true)));
        check(missing.commitments.empty() && missing.proposals[id]["status"] == "pending",
              "Missing AI signature waits within lifetime");
        bool stale = false;
        try {
            compile(offered, event("BB", "Quiet month", "[]"), answer(id, "AA", true, 2));
        } catch (const ValidationError &) {
            stale = true;
        }
        check(stale, "Stale revision rejected");
        auto ai = apply(w, compile(w, offer("BB")));
        auto aid = ai.proposals.getMemberNames()[0];
        ai.turn++;
        auto declined = apply(ai, compile(ai, event("AA", "Aland says Bland in prose", "[]"),
                                          answer(aid, "AA", false)));
        check(declined.commitments.empty() && declined.proposals[aid]["status"] == "declined",
              "Explicit refusal never signs");
        auto expired = apply(ai, compile(ai, event("AA", "I mention Bland", "[]")));
        check(expired.proposals[aid]["status"] == "expired", "Unanswered player proposal expires");
        // Both clauses are material: a first feasible cession must roll back with the second.
        auto impossible = apply(
            w,
            compile(
                w,
                offer(
                    "BB",
                    R"([{"action":"cede","region":"BB","to":"AA"},{"action":"cede","region":"missing","to":"AA"}])")));
        auto iid = impossible.proposals.getMemberNames()[0];
        impossible.turn++;
        auto failed = apply(impossible, compile(impossible, event("AA", "Quiet month", "[]"),
                                                answer(iid, "AA", true)));
        check(failed.territories == impossible.territories && failed.commitments.empty() &&
                  failed.proposals[iid]["status"] == "expired",
              "Impossible settlement rolls back all material clauses");
        auto amendment = event("BB", "Revise the accord.", "[]");
        Json::Value amend;
        amend["action"] = "amend_commitment";
        amend["commitment"] = id;
        amend["text"] = "New terms";
        amend["months"] = 8;
        amend["terms"] = Json::arrayValue;
        amendment["operations"].append(amend);
        done.turn++;
        auto revised = apply(done, compile(done, amendment));
        std::string rid;
        for (auto &pid : revised.proposals.getMemberNames())
            if (revised.proposals[pid]["status"] == "pending")
                rid = pid;
        check(!rid.empty() && revised.proposals[rid]["revision"] == 2 &&
                  !revised.proposals[rid]["answers"].isMember("AA"),
              "Amendment requires fresh consent to a new revision");
        revised.turn++;
        revised.commitments[id]["text"] = "An intervening change";
        auto staleAmendment = apply(
            revised, compile(revised, event("AA", "Quiet", "[]"), answer(rid, "AA", true, 2)));
        check(staleAmendment.proposals[rid]["status"] == "expired" &&
                  staleAmendment.commitments == revised.commitments,
              "Concurrent treaty change lapses amendment rather than overwriting it");
        auto unsupported = compile(w, event("AA", "Aland and Bland signed peace.", "[]"));
        check(unsupported.events[0]["text"] ==
                  "No validated change resulted from the proposed action.",
              "Narration without a treaty operation cannot claim a signature");
        auto bad = event(
            "AA", "Aland builds an impossible city.",
            R"([{"action":"build","actor":"AA","region":"BB","kind":"city","name":"Impossible"}])");
        bad["effects"] =
            parse(R"([{"actor":"AA","economy":"strong_growth","stability":"strong_growth"}])");
        d = compile(w, bad);
        check(d.events[0]["failed"] == true && d.events[0]["effects"].empty() &&
                  d.events[0]["major"] == false,
              "Failed operation grants no reward or headline");
        bad["independent_outcome"] = "protest";
        d = compile(w, bad);
        check(d.events[0]["partial"] == true && d.events[0]["effects"][0]["economy"] == "none" &&
                  d.events[0]["effects"][0]["stability"] == "decline",
              "Supported independent result survives failed operation");
        std::cout << checks << " explicit consent and accepted outcome checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
