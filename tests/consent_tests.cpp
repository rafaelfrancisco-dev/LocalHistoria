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
        bad["outcome_kind"] = "protest";
        d = compile(w, bad);
        check(d.events[0]["partial"] == true && d.events[0]["effects"][0]["economy"] == "none" &&
                  d.events[0]["effects"][0]["stability"] == "decline",
              "Supported independent result survives failed operation");
        // Plan 26 H: trade is an ongoing relationship the month step values, not a reward for
        // signing or a penalty for ending.
        auto rewarded = [](const WorldDelta &x) {
            for (const auto &e : x.events)
                for (const auto &f : e.get("effects", Json::arrayValue))
                    if (f["economy"] != "none")
                        return true;
            return false;
        };
        check(!rewarded(settled), "Signing a trade agreement earns no one-off growth");
        auto quit = event("AA", "Aland leaves the accord.", "[]");
        Json::Value leave;
        leave["action"] = "resolve_commitment";
        leave["commitment"] = id;
        quit["operations"].append(leave);
        check(!rewarded(compile(done, quit)), "Leaving one costs no one-off decline");
        // A reform or investment starts a priced effort; its benefit waits for completion.
        auto reform = event("AA", "Aland reforms its civil service.", "[]");
        reform["outcome_kind"] = "reform";
        reform["effects"] =
            parse(R"([{"actor":"AA","economy":"strong_growth","stability":"growth"}])");
        d = compile(w, reform);
        check(d.events[0]["effort"] == "reform" && d.events[0]["effects"].size() == 1 &&
                  d.events[0]["effects"][0]["economy"] == "none" &&
                  d.events[0]["effects"][0]["stability"] == "growth",
              "A reform starts an effort, with a month's confidence but no immediate growth");
        Json::Value twice;
        twice["events"].append(reform);
        auto again = reform;
        again["description"] = "Aland reforms its civil service again.";
        twice["events"].append(again);
        d = compileOperations(w, twice, {});
        check(d.events[0]["effort"] == "reform" && !d.events[1].isMember("effort") &&
                  !d.events[1].isMember("effects"),
              "Asking twice in a month continues the same effort and earns nothing more");
        auto underway = w;
        underway.actors["AA"]["status"]["efforts"]["reform"]["paid"] = 3;
        d = compile(underway, reform);
        check(!d.events[0].isMember("effort") && !d.events[0].isMember("effects"),
              "A reform already under way is continued, not rewarded again");
        // Re-audit N3: a small model never volunteers an optional field, so the realism schema
        // requires the outcome category; a background development keeps its consequence.
        auto schema = operationSchema(w, encode(w), {{"AA", "Reform the civil service"}});
        bool required = false;
        for (const auto &k : schema["properties"]["events"]["prefixItems"][0]["required"])
            required = required || k == "outcome_kind";
        check(required, "The realism schema requires an outcome category");
        Json::Value world;
        world["events"] = Json::arrayValue;
        world["world_events"].append(parse(
            R"({"actor":"BB","description":"Floods hit Bland.","causes":[],"effects":[{"actor":"BB","economy":"decline","stability":"none"}]})"));
        d = compileOperations(w, world, {});
        check(d.events[0]["text"] == "Floods hit Bland." &&
                  d.events[0]["effects"][0]["economy"] == "decline",
              "A world event keeps its text and consequence");
        auto plain = event("AA", "Aland reforms its courts.", "[]");
        plain["outcome_kind"] = "reform";
        check(compile(w, plain).events[0]["effort"] == "reform",
              "A named reform starts its effort without a rated effect");
        plain["effects"] = parse(R"([{"actor":"AA","economy":"decline","stability":"none"}])");
        check(!compile(w, plain).events[0].isMember("effort"),
              "A reform the model judged a setback starts nothing");
        // Re-audit N8: the same offer is not opened twice while it awaits answers.
        Json::Value both;
        both["events"].append(offer("BB"));
        both["events"].append(offer("AA"));
        d = compileOperations(w, both, {});
        check(apply(w, d).proposals.size() == 1 && d.events[1]["failed"] == true,
              "An identical offer awaiting answers is not made again");
        // Intent version 1: access terms alone never make a ceasefire or peace treaty.
        auto accessOffer = offer("AA", R"([{"action":"grant_access","to":"AA"}])");
        accessOffer["operations"][0]["kind"] = "peace_treaty";
        auto intents = w;
        intents.engine["intent_version"] = 1;
        auto accessOnly = compile(intents, accessOffer);
        auto relabelledState = apply(intents, accessOnly);
        check(
            relabelledState.proposals.size() == 1 &&
                relabelledState.proposals[relabelledState.proposals.getMemberNames()[0]]["kind"] ==
                    "other" &&
                accessOnly.events[0]["text"].asString().find("only military access") !=
                    std::string::npos,
            "An access-only peace treaty is recorded as an agreement and says so");
        auto textIntents = intents;
        textIntents.engine["event_text_version"] = 2;
        check(compile(textIntents, accessOffer)
                      .events[0]["text"]
                      .asString()
                      .find("only military access") != std::string::npos,
              "The relabel survives factual proposal text");
        auto withPeace = offer(
            "AA", R"([{"action":"grant_access","to":"AA"},{"action":"withdraw","region":"BB"}])");
        withPeace["operations"][0]["kind"] = "peace_treaty";
        auto kept = apply(intents, compile(intents, withPeace));
        check(kept.proposals[kept.proposals.getMemberNames()[0]]["kind"] == "peace_treaty",
              "A peace treaty with other terms keeps its kind");
        auto oldKind = apply(w, compile(w, accessOffer));
        check(oldKind.proposals[oldKind.proposals.getMemberNames()[0]]["kind"] == "peace_treaty",
              "Before intent version 1 the drafted kind is kept");
        // Re-audit N7: land passes to another nation only by a treaty it signs.
        auto gift = event("BB", "Bland hands its land to Aland.",
                          R"([{"action":"transfer_territory","region":"BB","to":"AA"}])");
        d = compile(w, gift);
        check(d.changes.empty() && d.events[0]["failed"] == true,
              "A cession to another nation needs its signature");
        auto held = w;
        held.territories["BB"]["controller"] = "AA";
        d = compile(held, gift);
        check(apply(held, d).territories["BB"]["owner"] == "AA",
              "An owner may recognise the nation already holding the region");
        auto legacy = w;
        legacy.engine = Json::objectValue;
        check(apply(legacy, compile(legacy, gift)).territories["BB"]["owner"] == "AA",
              "Legacy campaigns keep unilateral cession");
        auto textVersion = fixture();
        textVersion.engine["event_text_version"] = 1;
        auto concrete = compile(textVersion, offer("AA"));
        auto concreteState = apply(textVersion, concrete);
        const auto pid = concreteState.proposals.getMemberNames()[0];
        check(concrete.events[0]["text"].asString().find("proposal " + pid + ", revision 1") != std::string::npos && concrete.events[0]["text"].asString().find("awaiting required signatures") != std::string::npos, "Compiler describes an offer with its actual ID and revision");
        concreteState.turn++;
        auto signatures = answer(pid, "AA", true); signatures.append(answer(pid, "BB", true)[0]);
        auto signedDelta = compile(concreteState, event("AA", "Quiet", "[]"), signatures);
        check(signedDelta.events[0]["text"].asString().find("agreement " + pid + " took effect") != std::string::npos, "Compiler reports agreement only after exact signatures");
        auto talks = event("AA", "Everyone signed a peace treaty and ended the war.", "[]");
        talks["outcome_kind"] = "negotiation";
        check(compile(textVersion, talks).events[0]["text"] == "Preliminary diplomatic talks took place; no treaty offer or agreement was recorded.", "Unsupported diplomatic completion is not authoritative");
        auto oldText = textVersion; oldText.engine.removeMember("event_text_version");
        check(compile(oldText, talks).events[0]["text"] == talks["description"], "Absent event text version preserves old compiler output");
        // Plan 28 A: every outcome keeps its description and carries a consequence.
        auto v2 = fixture();
        v2.engine["event_text_version"] = 2;
        auto police = event("AA", "Aland deploys 200 border police to the frontier.", "[]");
        police["outcome_kind"] = "security";
        police["effects"] = parse(R"([{"actor":"AA","economy":"none","stability":"none"}])");
        auto guarded = compile(v2, police).events[0];
        check(guarded["text"] == police["description"] && guarded["effect_source"] == "default" &&
                  guarded["effects"][0]["economy"] == "slight_decline" &&
                  guarded["effects"][0]["stability"] == "slight_growth",
              "An act the model rated nothing keeps its text and its kind's default consequence");
        police["effects"] =
            parse(R"([{"actor":"AA","economy":"severe_decline","stability":"strong_growth"}])");
        guarded = compile(v2, police).events[0];
        check(!guarded.isMember("effect_source") &&
                  guarded["effects"][0]["economy"] == "slight_decline" &&
                  guarded["effects"][0]["stability"] == "slight_growth",
              "Without a map change a security act moves each axis by at most a step, felt slightly");
        auto spoken = compile(v2, talks).events[0];
        check(spoken["text"] == talks["description"].asString() + " No agreement was recorded." &&
                  spoken["effects"][0]["economy"] == "none" &&
                  spoken["effects"][0]["stability"] == "slight_growth",
              "Talks keep their description, say no agreement was recorded, and read at home");
        talks["effects"] =
            parse(R"([{"actor":"AA","economy":"strong_growth","stability":"severe_decline"}])");
        spoken = compile(v2, talks).events[0];
        check(spoken["effects"][0]["economy"] == "none" &&
                  spoken["effects"][0]["stability"] == "slight_decline",
              "Talks move stability only, by at most a step");
        auto claimed = event("BB", "Bland and Aland sign the Lisbon Defence Pact.", "[]");
        claimed["outcome_kind"] = "policy";
        check(compile(v2, claimed).events[0]["text"] ==
                  "Bland and Aland sign the Lisbon Defence Pact. No agreement was recorded.",
              "A signing narrated without a treaty operation is marked as unrecorded");
        Json::Value rumour;
        rumour["events"] = Json::arrayValue;
        rumour["world_events"].append(parse(
            R"({"actor":"BB","description":"The Lisbon Defence Pact is signed.","causes":[],"effects":[{"actor":"BB","economy":"none","stability":"growth"}]})"));
        check(compileOperations(v2, rumour, {}).events[0]["text"] ==
                  "The Lisbon Defence Pact is signed. No agreement was recorded.",
              "So is one a world event narrates");
        auto relabelled = event("AA", "Aland overhauls its curriculum.", "[]");
        relabelled["outcome_kind"] = "policy";
        relabelled["effects"] =
            parse(R"([{"actor":"AA","economy":"strong_growth","stability":"growth"}])");
        auto decree = compile(v2, relabelled).events[0];
        check(decree["effects"][0]["economy"] == "none" && decree["effects"][0]["stability"] == "slight_growth" &&
                  !decree.isMember("effort"),
              "A policy cannot grow the economy at once: that is what an effort pays for");
        {
            auto slightDelta = compile(v2, police);
            auto applied = applyDelta({v2, {}}, slightDelta, Json::Value(Json::objectValue));
            check(applied.state.turn == v2.turn, "A slight effect is a valid recorded effect");
        }
        auto unlabelled = event("AA", "Aland renames a ministry.", "[]");
        check(compile(v2, unlabelled).events[0]["effects"][0]["stability"] == "slight_growth",
              "An unlabelled outcome is a policy");
        check(compile(v2, reform).events[0]["effort"] == "reform",
              "A reform still starts its effort");
        auto impossibleV2 = bad;
        impossibleV2["outcome_kind"] = "security";
        impossibleV2.removeMember("effects");
        check(compile(v2, impossibleV2).events[0]["failed"] == true,
              "A failed attempt earns no default consequence");
        impossibleV2["effects"] =
            parse(R"([{"actor":"AA","economy":"none","stability":"growth"}])");
        auto attempted = compile(v2, impossibleV2).events[0];
        check(attempted["failed"] == true && !attempted.isMember("effects") &&
                  attempted["text"] == "The proposed action could not be carried out.",
              "Nor a rated one: a failed security act's consequence is the failure");
        auto v2schema = operationSchema(v2, encode(v2), {{"AA", "Deploy border police"}});
        const auto &item = v2schema["properties"]["events"]["prefixItems"][0];
        bool effectsRequired = false, hasNone = false;
        for (const auto &k : item["required"])
            effectsRequired = effectsRequired || k == "outcome_effects";
        for (const auto &k : item["properties"]["outcome_kind"]["enum"])
            hasNone = hasNone || k == "none";
        check(effectsRequired && !hasNone && item["properties"]["outcome_effects"]["minItems"] == 1 &&
                  !item["properties"].isMember("effects"),
              "Version 2 requires a rated consequence, written after the operations, and has no 'none' outcome");
        check(v2schema["properties"]["world_events"]["items"]["properties"]["effects"]["minItems"] == 1,
              "World events keep the plain name");
        const auto &rated = v2schema["$defs"]["rated_effect"]["anyOf"];
        check(item["properties"]["outcome_effects"]["items"]["$ref"] == "#/$defs/rated_effect" &&
                  rated.size() == 2 && rated[1]["properties"]["economy"]["const"] == "none" &&
                  rated[1]["properties"]["stability"]["enum"].size() == 4,
              "A rated consequence moves at least one axis");
        auto cut = event("AA", "Aland opens talks with Bland on fishing rights in the", "[]");
        cut["outcome_kind"] = "negotiation";
        check(compile(v2, cut).events[0]["text"] ==
                  "Aland opens talks with Bland on fishing rights in the… No agreement was recorded.",
              "A description cut at the length cap ends in an ellipsis before the verdict");
        auto trailing = event("AA", "Aland publishes a white paper on coastal defence in the", "[]");
        trailing["outcome_kind"] = "policy";
        check(compile(v2, trailing).events[0]["text"] ==
                  "Aland publishes a white paper on coastal defence in the…",
              "Any description cut at the cap ends in an ellipsis");
        auto ellipsis = event("AA", "Aland opens talks with Bland on fishing rights in the…", "[]");
        ellipsis["outcome_kind"] = "negotiation";
        check(compile(v2, ellipsis).events[0]["text"] ==
                  "Aland opens talks with Bland on fishing rights in the… No agreement was recorded.",
              "A model's own ellipsis is not doubled");
        std::cout << checks << " explicit consent and accepted outcome checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
