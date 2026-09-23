#include "world/compiler/operation_compiler.h"
#include <algorithm>
namespace lh::detail {
void OperationCompiler::opOpen(EventScope &s, OpScope &op) {
    auto &o = op.o;
    if (!o["actors"].isArray() || o["actors"].size() < 2)
        throw Infeasible("needs two parties");
    auto kind = op.name == "open_conflict" ? std::string("conflict") : std::string("commitment");
    auto cid = std::string(kind == "conflict" ? "c" : "k") + std::to_string(w.turn) + "-" +
               std::to_string(++created);
    Json::Value v;
    v["actors"] = Json::arrayValue;
    v["status"] = "open";
    v["text"] = str(op, "text");
    auto members = treatyParties(s, op, v);
    if (kind == "commitment")
        consent(s, members, op.proposal);
    else
        for (auto &m : members)
            if (m != s.ev)
                op.hostile.insert(m);
    auto type = o["kind"].asString();
    if (kind == "conflict" ? !validConflictKind(type) : !validCommitmentKind(type))
        type = kind == "conflict" ? "war" : "other";
    v["name"] = treatyName(op, kind, type, members, v);
    v["kind"] = type;
    v["started_turn"] = w.turn;
    if (kind == "conflict") {
        auto level = o["intensity"].asString();
        v["set_intensity"] = v["intensity"] =
            (level == "low" || level == "high") ? level : "medium";
        if (realism()) {
            v["hostile_pairs"] = Json::arrayValue;
            v["postures"] = Json::objectValue;
            for (const auto &m : members) {
                v["postures"][m] = v["set_intensity"];
                if (m == s.ev)
                    continue;
                Json::Value pair;
                pair["actors"].append(std::min(m, s.ev));
                pair["actors"].append(std::max(m, s.ev));
                pair["status"] = "active";
                v["hostile_pairs"].append(pair);
            }
            v["pair_provenance"] = "explicit_declaration";
        }
        v["affected_regions"] = Json::arrayValue;
        v["casualties"] = v["displaced"] = Json::Int64(0);
    }
    // How long it lasts (0 = open-ended) and what signing changes at once, both drafted by the
    // model.
    int months =
        kind == "commitment" && o["months"].isInt() ? std::clamp(o["months"].asInt(), 0, 120) : 0;
    if (op.proposal)
        return propose(s, op, v, cid, months);
    if (kind == "commitment") {
        auto done = applyTerms(s, o["terms"], members, op.why);
        if (!done.empty()) {
            v["terms"] = done;
            op.shaped = true;
        }
        if (months)
            v["expires_turn"] = w.turn + months;
    }
    pending.touch(kind, cid, Json::nullValue, s.id, op.why) = v;
    (kind == "conflict" ? s.conflicts : s.commitments).insert(cid);
    if (kind == "conflict" ? (type == "war" || type == "civil_war" || v["set_intensity"] == "high")
                           : (type == "trade_agreement" || type == "union"))
        for (auto &m : members)
            (kind == "conflict" ? s.warParties : s.tradeParties).insert(m);
}
std::vector<std::string> OperationCompiler::treatyParties(EventScope &s, const OpScope &op,
                                                          Json::Value &v) const {
    std::vector<std::string> members;
    for (auto &a : op.o["actors"]) {
        if (!a.isString())
            throw Infeasible("unknown party");
        auto ref = actorRef(a.asString());
        if (!known(ref))
            throw Infeasible("unknown party " + ref);
        if (std::find(members.begin(), members.end(), ref) != members.end())
            continue;
        members.push_back(ref);
        v["actors"].append(ref);
        s.actors.insert(ref);
    }
    if (members.size() < 2)
        throw Infeasible("needs two parties");
    if (std::find(members.begin(), members.end(), s.ev) == members.end())
        throw Infeasible("only a party can open or sign this");
    return members;
}
std::string OperationCompiler::treatyName(const OpScope &op, const std::string &kind,
                                          const std::string &type,
                                          const std::vector<std::string> &members,
                                          const Json::Value &v) {
    // Pending creations are not in w yet; name them against what they will be.
    WorldState named = w;
    for (auto &m : members)
        if (!named.hasActor(m))
            named.actors[m] = pending.current("actor", w.actors, m);
    auto name =
        realisticName(named, kind, type, op.o["name"].isString() ? op.o["name"].asString() : "",
                      members, v["text"].asString(), names);
    names.insert(lowercase(name));
    return name;
}
// Keyed by the commitment ID it will take, so acceptance never clashes with a later treaty.
void OperationCompiler::propose(EventScope &s, OpScope &op, Json::Value v, const std::string &cid,
                                int months) {
    v.removeMember("status");
    v.removeMember("started_turn");
    v["from"] = s.ev;
    v["status"] = "pending";
    v["proposed_turn"] = w.turn;
    v["event"] = s.id;
    v["months"] = months;
    v["terms"] = treatyTerms(op.o["terms"]);
    if (realism())
        initializeProposal(v);
    pending.touch("proposal", cid, Json::nullValue, s.id, op.why) = v;
    awaiting(s);
    op.done = true;
}
void OperationCompiler::opResolve(EventScope &s, OpScope &op) {
    auto kind = op.name == "resolve_conflict" ? std::string("conflict") : std::string("commitment");
    auto cid = str(op, kind.c_str());
    auto &collection = kind == "conflict" ? w.conflicts : w.commitments;
    if (!collection.isMember(cid))
        throw Infeasible("unknown " + kind);
    if (std::find(collection[cid]["actors"].begin(), collection[cid]["actors"].end(),
                  Json::Value(s.ev)) == collection[cid]["actors"].end())
        throw Infeasible("only a party can end " + displayName(w, kind, cid));
    auto treaty = pending.current(kind, collection, cid);
    if (realism() && kind == "conflict" && isOpen(treaty))
        throw Infeasible("ending hostility requires an accepted settlement for its hostile pairs");
    {
        auto &v = pending.touch(kind, cid, collection[cid], s.id, op.why);
        (kind == "conflict" ? s.conflicts : s.commitments).insert(cid);
        v["status"] = "resolved";
        v["ended_turn"] = w.turn;
        if (kind == "conflict")
            v["intensity"] = "none";
    }
    if (kind == "commitment" && isOpen(treaty))
        ending(s, treaty, op.why);
}
void OperationCompiler::opAmend(EventScope &s, OpScope &op) {
    auto &o = op.o;
    auto cid = str(op, "commitment");
    auto treaty = pending.current("commitment", w.commitments, cid);
    if (!treaty.isObject() || !isOpen(treaty))
        throw Infeasible("no such treaty in force");
    std::vector<std::string> members;
    for (auto &a : treaty["actors"])
        members.push_back(a.asString());
    if (std::find(members.begin(), members.end(), s.ev) == members.end())
        throw Infeasible("only a party can amend " + displayName(w, "commitment", cid));
    consent(s, members, op.proposal);
    // Renewal: months from now; 0 keeps the current end date.
    int months = o["months"].isInt() ? std::clamp(o["months"].asInt(), 0, 120) : 0;
    auto revised = o["text"].isString() ? o["text"].asString().substr(0, 1000) : "";
    for (auto &m : members)
        s.actors.insert(m);
    if (op.proposal)
        return proposeAmendment(s, op, treaty, cid, months, revised);
    auto done = applyTerms(s, o["terms"], members, op.why);
    auto &v = pending.touch("commitment", cid, w.commitments[cid], s.id, op.why);
    s.commitments.insert(cid);
    if (!revised.empty())
        v["text"] = revised;
    if (months)
        v["expires_turn"] = w.turn + months;
    appendTerms(v, done);
    op.shaped = !done.empty();
}
void OperationCompiler::proposeAmendment(EventScope &s, OpScope &op, const Json::Value &treaty,
                                         const std::string &cid, int months,
                                         const std::string &revised) {
    auto pid = cid + "-r" + std::to_string(w.turn);
    for (int n = 2; w.proposals.isMember(pid) || pending.contains("proposal", pid); ++n)
        pid = cid + "-r" + std::to_string(w.turn) + "-" + std::to_string(n);
    Json::Value v;
    v["from"] = s.ev;
    v["actors"] = treaty["actors"];
    v["kind"] = validCommitmentKind(treaty.get("kind", "").asString()) ? treaty["kind"]
                                                                       : Json::Value("other");
    v["name"] = displayName(w, "commitment", cid);
    v["text"] = revised.empty() ? treaty["text"] : Json::Value(revised);
    v["months"] = months;
    v["terms"] = treatyTerms(op.o["terms"]);
    v["amends"] = cid;
    v["status"] = "pending";
    v["proposed_turn"] = w.turn;
    v["event"] = s.id;
    if (realism()) {
        initializeProposal(v);
        v["revision"] = treaty.get("revision", 1).asInt() + 1;
        v["amends_snapshot"] = treaty;
        for (auto &answer : v["answers"])
            answer["revision"] = v["revision"];
    }
    pending.touch("proposal", pid, Json::nullValue, s.id, op.why) = v;
    awaiting(s);
    op.done = true;
}
void OperationCompiler::opSettle(EventScope &s, OpScope &op) {
    if (!s.trusted)
        throw Infeasible("unknown operation " + op.name);
    auto pid = str(op, "proposal"), answer = str(op, "answer");
    auto &p = w.proposals[pid];
    auto after = p;
    after["status"] = answer;
    if (answer != "pending")
        after["answered_turn"] = w.turn;
    if (realism() && op.o["answers"].isObject())
        after["answers"] = op.o["answers"];
    pending.touch("proposal", pid, p, s.id, op.why) = after;
    for (auto &a : p["actors"])
        s.actors.insert(a.asString());
    if (answer == "accepted") {
        if (!realism()) {
            acceptProposal(s, op, pid, p);
            return;
        }
        auto before = snapshot();
        Touched touched = s;
        pending.open();
        try {
            for (const auto &region : p["term_preconditions"].getMemberNames()) {
                auto now = pending.current("territory", w.territories, region);
                for (auto k : {"owner", "controller"})
                    if (now[k] != p["term_preconditions"][region][k])
                        throw Infeasible("territorial terms changed while awaiting signatures");
            }
            if (p["amends"].isString() &&
                pending.current("commitment", w.commitments, p["amends"].asString()) !=
                    p["amends_snapshot"])
                throw Infeasible("the treaty changed while signatures were pending");
            acceptProposal(s, op, pid, p);
            pending.commit();
        } catch (const Infeasible &) {
            pending.rollback();
            restore(std::move(before));
            static_cast<Touched &>(s) = std::move(touched);
            after["status"] = "expired";
            after["lapse_reason"] =
                "Material terms are no longer feasible; revised terms need new signatures.";
            pending.touch("proposal", pid, p, s.id, op.why) = after;
            s.text =
                "The treaty proposal lapsed because its material terms could not be fulfilled.";
        }
    }
}
void OperationCompiler::acceptProposal(EventScope &s, OpScope &op, const std::string &pid,
                                       const Json::Value &p) {
    std::vector<std::string> members;
    for (auto &a : p["actors"])
        members.push_back(a.asString());
    auto done = applyTerms(s, p["terms"], members, op.why);
    int months = p.get("months", 0).asInt();
    auto kind = p["kind"].asString();
    if (p["amends"].isString()) {
        auto cid = p["amends"].asString();
        auto &v = pending.touch("commitment", cid, w.commitments[cid], s.id, op.why);
        v["text"] = p["text"];
        if (realism())
            v["revision"] = p["revision"];
        if (months)
            v["expires_turn"] = w.turn + months;
        appendTerms(v, done);
        s.commitments.insert(cid);
        op.shaped = !done.empty();
        return;
    }
    Json::Value v;
    for (auto k : {"actors", "kind", "name", "text"})
        v[k] = p[k];
    v["status"] = "open";
    v["started_turn"] = w.turn;
    if (realism())
        v["revision"] = p["revision"];
    if (!done.empty())
        v["terms"] = done;
    if (months)
        v["expires_turn"] = w.turn + months;
    pending.touch("commitment", pid, Json::nullValue, s.id, op.why) = v;
    s.commitments.insert(pid);
    if (kind == "trade_agreement" || kind == "union")
        for (auto &m : members)
            s.tradeParties.insert(m);
    op.shaped = !done.empty() || (kind != "trade_agreement" && kind != "other");
}
void OperationCompiler::opIntensity(EventScope &s, OpScope &op) {
    auto cid = str(op, "conflict");
    auto level = str(op, "intensity");
    if (!w.conflicts.isMember(cid) || !isOpen(w.conflicts[cid]))
        throw Infeasible("no such open conflict");
    if (level != "low" && level != "medium" && level != "high")
        throw Infeasible("invalid intensity");
    auto &parties = w.conflicts[cid]["actors"];
    if (std::find(parties.begin(), parties.end(), Json::Value(s.ev)) == parties.end())
        throw Infeasible("only a party can escalate or scale down " +
                         displayName(w, "conflict", cid));
    if (realism())
        pending.touch("conflict", cid, w.conflicts[cid], s.id, op.why)["postures"][s.ev] = level;
    else
        pending.touch("conflict", cid, w.conflicts[cid], s.id, op.why)["set_intensity"] = level;
    s.conflicts.insert(cid);
    if (realism()) {
        s.actors.insert(s.ev);
        for (const auto &pair : hostilePairs(w, w.conflicts[cid])) {
            if (!isParty(pair, s.ev) || pair["status"] == "settled")
                continue;
            for (const auto &actor : pair["actors"]) {
                auto other = actor.asString();
                s.actors.insert(other);
                if (level != "low" && other != s.ev)
                    op.hostile.insert(other);
                if (level == "high")
                    s.warParties.insert(other);
            }
        }
        return;
    }
    if (level != "low")
        for (auto &a : parties)
            op.hostile.insert(a.asString());
    for (auto &a : parties) {
        s.actors.insert(a.asString());
        if (level == "high")
            s.warParties.insert(a.asString());
    }
}
} // namespace lh::detail
