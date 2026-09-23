#include "world/compiler/operation_compiler.h"
namespace lh::detail {
void OperationCompiler::initializeProposal(Json::Value &p) const {
    p["term_preconditions"] = Json::objectValue;
    for (const auto &t : p["terms"]) {
        if (t["region"].isString()) {
            auto id = t["region"].asString();
            for (auto k : {"owner", "controller"})
                p["term_preconditions"][id][k] = w.territories[id][k];
        }
    }
    p["revision"] = 1;
    p["required_signatories"] = p["actors"];
    p["answers"] = Json::objectValue;
    // The player must see and answer the actual terms, even when their order started talks.
    auto from = p["from"].asString();
    if (from != w.player) {
        p["answers"][from]["accept"] = true;
        p["answers"][from]["revision"] = 1;
        p["answers"][from]["turn"] = w.turn;
    }
    // Player offers keep the visible one-month answer window. AI-only offers allow rotation.
    p["expires_turn"] = w.turn + 12;
    if (isParty(p, w.player))
        p["player_answer_turn"] = w.turn + 1;
}
int OperationCompiler::settleExplicitProposals(Json::Value &all,
                                               const Json::Value &responses) const {
    int count = 0;
    for (auto &id : w.proposals.getMemberNames()) {
        const auto &original = w.proposals[id];
        if (original["status"] != "pending" || original["proposed_turn"].asInt() >= w.turn)
            continue;
        auto p = original;
        // Old pending offers have known terms but no revision. Require a fresh explicit answer.
        const int revision = p.get("revision", 1).asInt();
        auto answers = p.get("answers", Json::Value(Json::objectValue));
        if (!p.isMember("revision") && p["from"] != w.player) {
            auto from = p["from"].asString();
            answers[from]["accept"] = true;
            answers[from]["revision"] = revision;
            answers[from]["turn"] = p["proposed_turn"];
        }
        std::set<std::string> seen;
        for (auto &a : responses) {
            if (a["proposal"] != id)
                continue;
            auto actor = a.get("actor", w.player).asString();
            if (!a["revision"].isInt() || a["revision"] != revision || !a["accept"].isBool() ||
                !isParty(p, actor) || !seen.insert(actor).second || answers.isMember(actor))
                throw ValidationError("Invalid or stale proposal signature");
            answers[actor]["accept"] = a["accept"];
            answers[actor]["revision"] = revision;
            answers[actor]["turn"] = w.turn;
        }
        bool signedAll = true, declined = false;
        for (auto &a : p["actors"]) {
            const auto &answer = std::as_const(answers)[a.asString()];
            signedAll = signedAll && answer["revision"] == revision && answer["accept"] == true;
            declined = declined || answer["accept"] == false;
        }
        int expiry = p.get("expires_turn", p["proposed_turn"].asInt() + 1).asInt();
        if (isParty(p, w.player) && !answers.isMember(w.player))
            expiry = p.get("player_answer_turn", p["proposed_turn"].asInt() + 1).asInt();
        std::string status = declined           ? "declined"
                             : signedAll        ? "accepted"
                             : w.turn >= expiry ? "expired"
                                                : "pending";
        if (status == "pending" && answers == original["answers"])
            continue;
        Json::Value e;
        e["actor"] = p["from"];
        e["description"] = "Treaty proposal " + p["name"].asString() + ": " + status + ".";
        e["blocked_by"] = "none";
        e["background"] = false;
        e["causes"] = Json::arrayValue;
        e["causes"].append(p["event"]);
        Json::Value op;
        op["action"] = "settle_proposal";
        op["proposal"] = id;
        op["answer"] = status;
        op["answers"] = answers;
        e["operations"].append(op);
        all.append(e);
        ++count;
    }
    return count;
}
} // namespace lh::detail
