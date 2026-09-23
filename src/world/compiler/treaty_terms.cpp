#include "world/compiler/operation_compiler.h"
#include <algorithm>
namespace lh::detail {
// Executes a treaty's terms, which its parties' signatures authorize; an impossible clause is
// dropped on its own.
Json::Value OperationCompiler::applyTerms(EventScope &s, const Json::Value &terms,
                                          const std::vector<std::string> &members,
                                          const std::string &why) {
    Json::Value done(Json::arrayValue);
    if (realism()) {
        if (!terms.isArray() || treatyTerms(terms) != terms)
            throw Infeasible("invalid material treaty terms");
        for (const auto &t : terms)
            done.append(applyTerm(s, t, members, why));
        return done;
    }
    for (auto &t : treatyTerms(terms)) {
        try {
            done.append(applyTerm(s, t, members, why));
        } catch (const Infeasible &x) {
            Json::Value d;
            d["action"] = "term " + t["action"].asString();
            d["reason"] = x.what();
            s.dropped.append(d);
        }
    }
    return done;
}
Json::Value OperationCompiler::applyTerm(EventScope &s, const Json::Value &t,
                                         const std::vector<std::string> &members,
                                         const std::string &why) {
    auto clause = t["action"].asString();
    auto party = [&](const std::string &a) {
        return std::find(members.begin(), members.end(), a) != members.end();
    };
    auto field = [&](const char *k) {
        if (!t[k].isString() || t[k].asString().empty())
            throw Infeasible("incomplete " + clause);
        return t[k].asString();
    };
    Json::Value kept;
    kept["action"] = clause;
    if (clause == "end_conflict") {
        auto cid = field("conflict");
        auto c = pending.current("conflict", w.conflicts, cid);
        if (!c.isObject() || !isOpen(c))
            throw Infeasible("no such open conflict");
        if (realism()) {
            auto pairs = hostilePairs(w, c);
            bool covered = false, remaining = false;
            for (auto &pair : pairs) {
                if (party(pair["actors"][0].asString()) && party(pair["actors"][1].asString())) {
                    pair["status"] = "settled";
                    covered = true;
                }
                remaining = remaining || pair["status"] != "settled";
            }
            if (!covered)
                throw Infeasible("settlement covers no hostile pair");
            auto &v = pending.touch("conflict", cid, w.conflicts[cid], s.id, why);
            v["hostile_pairs"] = pairs;
            if (!remaining) {
                v["status"] = "resolved";
                v["ended_turn"] = w.turn;
                v["intensity"] = "none";
            }
            s.conflicts.insert(cid);
            kept["conflict"] = cid;
            return kept;
        }
        for (auto &a : c["actors"])
            if (!party(a.asString()))
                throw Infeasible("every side must sign to end " + displayName(w, "conflict", cid));
        auto &v = pending.touch("conflict", cid, w.conflicts[cid], s.id, why);
        v["status"] = "resolved";
        v["ended_turn"] = w.turn;
        v["intensity"] = "none";
        s.conflicts.insert(cid);
        kept["conflict"] = cid;
        return kept;
    }
    // Military access: the other signatories let one party base and move forces on the ground
    // they hold, for as long as the treaty stays open. Nothing moves when it is signed.
    if (clause == "grant_access") {
        auto to = actorRef(field("to"));
        if (!party(to) || members.size() < 2)
            throw Infeasible("access can only be granted to a party by the other parties");
        kept["to"] = to;
        s.actors.insert(to);
        return kept;
    }
    auto r = field("region");
    if (!w.territories.isMember(r))
        throw Infeasible("unknown region");
    auto now = pending.current("territory", w.territories, r);
    auto owner = now["owner"].asString(), controller = now["controller"].asString(),
         place = w.territories[r]["name"].asString();
    if (clause == "cede") {
        auto to = actorRef(field("to"));
        if (!party(to) || !party(owner) || owner == to)
            throw Infeasible("only its owner can cede " + place + " to another party");
        auto &v = pending.touch("territory", r, w.territories[r], s.id, why);
        v["owner"] = to;
        v["controller"] = to;
        if (realism())
            handOver(s, r, controller, to, why);
        kept["from"] = owner;
        kept["to"] = to;
        s.actors.insert(to);
    } else if (clause == "occupy") {
        auto by = actorRef(field("by"));
        if (!party(by) || !party(owner) || by == owner)
            throw Infeasible("an agreed occupation of " + place +
                             " needs its owner and the occupier as parties");
        pending.touch("territory", r, w.territories[r], s.id, why)["controller"] = by;
        if (realism())
            handOver(s, r, controller, by, why);
        kept["by"] = by;
        s.actors.insert(by);
    } else if (clause == "withdraw") {
        if (!party(controller) || !party(owner) || controller == owner)
            throw Infeasible("no party occupies " + place);
        pending.touch("territory", r, w.territories[r], s.id, why)["controller"] = owner;
        if (realism())
            handOver(s, r, controller, owner, why);
        kept["by"] = controller;
    } else
        throw Infeasible("unknown term " + clause);
    s.regions.insert(r);
    s.actors.insert(owner);
    kept["region"] = r;
    return kept;
}
// Parties to a treaty signed, amended or answered here, and whether anyone must still agree.
void OperationCompiler::consent(const EventScope &s, const std::vector<std::string> &members,
                                bool &proposal) {
    if (realism()) {
        proposal = true;
        return;
    }
    for (auto &m : members)
        if (m != s.ev && !s.createdHere.contains(m) && !consenting[m].contains(s.ev)) {
            if (m == w.player) {
                proposal = true;
                continue;
            }
            throw Infeasible("needs agreement from " +
                             pending.current("actor", w.actors, m)["name"].asString());
        }
}
void OperationCompiler::awaiting(EventScope &s) const {
    if (realism()) {
        s.text = "A treaty proposal was recorded; it awaits explicit signatures to its terms.";
        return;
    }
    auto awaits = " It awaits " + w.actors[w.player]["name"].asString() + "'s answer.";
    if (s.text.find(awaits) == std::string::npos)
        s.text += awaits;
}
// A treaty ended here: occupations it allowed end with it, and trade it brought is lost.
void OperationCompiler::ending(EventScope &s, const Json::Value &treaty, const std::string &why) {
    for (auto &t : treaty["terms"])
        if (t["action"] == "occupy" && t["region"].isString() &&
            w.territories.isMember(t["region"].asString())) {
            auto r = t["region"].asString();
            auto now = pending.current("territory", w.territories, r);
            if (now["controller"] == t["by"] && now["owner"] != t["by"]) {
                pending.touch("territory", r, w.territories[r], s.id, why)["controller"] =
                    now["owner"];
                if (realism())
                    handOver(s, r, t["by"].asString(), now["owner"].asString(), why);
                s.regions.insert(r);
            }
        }
    auto kind = treaty["kind"].asString();
    if (kind == "trade_agreement" || kind == "union")
        for (auto &a : treaty["actors"])
            s.tradeLoss.insert(a.asString());
}
} // namespace lh::detail
