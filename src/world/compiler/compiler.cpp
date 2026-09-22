#include "world/compiler/operation_compiler.h"
#include <algorithm>
namespace lh::detail {
WorldDelta OperationCompiler::compile(const Json::Value &response, const Json::Value &responses) {
    require(response.isObject() && response["events"].isArray() &&
                response["events"].size() <= 37 &&
                (!response.isMember("world_events") ||
                 (response["world_events"].isArray() && response["world_events"].size() <= 3)),
            "Invalid operation response");
    for (auto &k : response.getMemberNames())
        require(k == "events" || k == "world_events", "Invalid operation response");
    // Outcomes of this month's actions first, then background developments.
    Json::Value all(Json::arrayValue);
    int settlements = settleProposals(all, responses);
    for (auto e : response["events"]) {
        if (e.isObject()) {
            e.removeMember("action");
            if (!e.isMember("background"))
                e["background"] = false;
        }
        all.append(e);
    }
    for (auto e : response["world_events"]) {
        if (e.isObject()) {
            if (e["actor"] == w.player || !e["operations"].empty())
                continue;
            e["background"] = true;
            e["blocked_by"] = "none";
            e["operations"] = Json::arrayValue;
        }
        all.append(e);
    }
    collectNames();
    collectConsent(response);
    int position = -1;
    for (auto &e : all)
        // Only the settlements C++ wrote may carry settle_proposal; a model cannot answer for the
        // player.
        compileEvent(e, ++position < settlements);
    // Beyond what changed the map, a month keeps its three weightiest headlines.
    std::stable_sort(declared.begin(), declared.end(), [&](Json::ArrayIndex a, Json::ArrayIndex b) {
        return effectWeight(events[a].get("effects", Json::nullValue)) >
               effectWeight(events[b].get("effects", Json::nullValue));
    });
    for (size_t n = 0; n < declared.size() && n < 3; ++n)
        events[declared[n]]["major"] = true;
    return delta();
}
// Last month's treaty offers to the player are settled first, as the month opens: accepted,
// declined, or lapsed unanswered.
int OperationCompiler::settleProposals(Json::Value &all, const Json::Value &responses) const {
    int settlements = 0;
    for (auto &pid : w.proposals.getMemberNames()) {
        auto &p = w.proposals[pid];
        if (p["status"] != "pending" || p["proposed_turn"].asInt() >= w.turn)
            continue;
        std::string answer = "expired";
        for (auto &r : responses)
            if (r["proposal"] == pid && r["accept"].isBool())
                answer = r["accept"].asBool() ? "accepted" : "declined";
        bool standing = true;
        for (auto &a : p["actors"])
            standing = standing && w.hasActor(a.asString());
        if (p["amends"].isString())
            standing = standing && isOpen(w.commitments[p["amends"].asString()]);
        if (!standing)
            answer = "expired";
        auto from = p["from"].asString();
        auto player = w.actors[w.player]["name"].asString(),
             proposer = w.actors[from]["name"].asString();
        auto what = p["amends"].isString() ? "amendment to the " + p["name"].asString()
                                           : p["name"].asString();
        Json::Value e;
        e["actor"] = answer == "expired" ? from : w.player;
        e["blocked_by"] = "none";
        e["background"] = false;
        e["causes"] = Json::arrayValue;
        e["causes"].append(p["event"]);
        e["description"] = answer == "accepted"
                               ? player + " accepts the " + what + " proposed by " + proposer + "."
                           : answer == "declined"
                               ? player + " declines the " + what + " proposed by " + proposer + "."
                               : "The " + what + " offered by " + proposer + " lapses unanswered.";
        Json::Value o;
        o["action"] = "settle_proposal";
        o["proposal"] = pid;
        o["answer"] = answer;
        e["operations"].append(o);
        all.append(e);
        ++settlements;
    }
    return settlements;
}
void OperationCompiler::collectNames() {
    for (auto kind : {"conflict", "commitment"})
        for (auto &cid :
             (kind == std::string("conflict") ? w.conflicts : w.commitments).getMemberNames())
            names.insert(lowercase(displayName(w, kind, cid)));
}
void OperationCompiler::collectConsent(const Json::Value &response) {
    for (auto &e : response["events"])
        if (e.isObject() && e["actor"].isString() && e["description"].isString())
            for (auto &b : w.actors.getMemberNames())
                if (b != e["actor"].asString() && w.actors[b]["name"].isString() &&
                    containsWord(lowercase(e["description"].asString()),
                                 lowercase(w.actors[b]["name"].asString())))
                    consenting[e["actor"].asString()].insert(b);
}
void OperationCompiler::compileEvent(const Json::Value &e, bool trusted) {
    require(e.isObject() && e["description"].isString() && e["actor"].isString() &&
                e["blocked_by"].isString() && e["background"].isBool() && e["causes"].isArray() &&
                e["operations"].isArray() && e["operations"].size() <= 8,
            "Invalid operation event");
    EventScope s(e);
    s.trusted = trusted;
    s.ev = e["actor"].asString();
    require(w.hasActor(s.ev), "Unknown event actor");
    // Background developments exist only for what changes something; those without an effect, and
    // those repeating one already recorded, are noise. The instruction asks the model not to
    // re-report history; a small model does it anyway.
    if (e["background"].asBool() &&
        (effectWeight(e.get("effects", Json::nullValue)) == 0 || ++background > 3 ||
         priorSlugs.contains(slug(e["description"].asString(), 32))))
        return;
    s.regional = e.get("regional_actor", "").asString();
    require(s.regional.empty() || (w.territories.isMember(s.regional) &&
                                   w.territories[s.regional]["controller"] == s.ev &&
                                   w.territories[s.regional]["kind"] != "residual"),
            "Invalid regional authority");
    s.text = e["description"].asString();
    auto base = "t" + std::to_string(w.turn) + "-" + slug(s.text, 32);
    s.id = base;
    for (int n = 2; !eventIds.insert(s.id).second; ++n)
        s.id = base + "-" + std::to_string(n);
    s.blocked = e["blocked_by"].asString();
    // A commitment binds only its parties.
    auto &commitments = w.commitments;
    if (commitments.isMember(s.blocked) &&
        std::find(commitments[s.blocked]["actors"].begin(), commitments[s.blocked]["actors"].end(),
                  Json::Value(s.ev)) == commitments[s.blocked]["actors"].end())
        s.blocked = "none";
    // Treaties can be broken: a cited commitment only blocks an event whose outcome changes
    // nothing; otherwise the breach is recorded.
    if (s.blocked != "none" && !e["operations"].empty() && commitments.isMember(s.blocked)) {
        s.breaches = s.blocked;
        s.blocked = "none";
    }
    runOperations(s);
    if (s.blocked == "none")
        renewEventSites(s);
    if (!s.regional.empty())
        finishRegionalHistory(s);
    s.actors.insert(s.ev);
    buildEvent(s);
}
void OperationCompiler::runOperations(EventScope &s) {
    auto before = snapshot();
    pending.open();
    try {
        for (auto &o : s.e["operations"]) {
            if (s.blocked != "none")
                break;
            require(o.isObject() && o["action"].isString(), "Invalid operation");
            // Small models sometimes loop; an exact repeat within one event is degenerate, not a
            // second outcome.
            if (std::find(s.seenOps.begin(), s.seenOps.end(), o) != s.seenOps.end())
                continue;
            s.seenOps.push_back(o);
            OpScope op{o};
            op.name = o["action"].asString();
            op.why = op.name + ": " + s.text.substr(0, 300);
            runOperation(s, op);
        }
        // Nothing was breached if every operation was dropped.
        if (!s.breaches.empty() && s.failedOps == int(s.seenOps.size()))
            s.breaches.clear();
        for (auto &a : s.createdHere) {
            bool land = false;
            for (auto &c : pending)
                land = land || (c.kind == "territory" && c.after["owner"] == a);
            if (!land)
                throw Infeasible("a new state needs territory");
        }
        pending.commit();
    } catch (const Infeasible &x) {
        pending.rollback();
        restore(std::move(before));
        static_cast<Touched &>(s) = Touched();
        s.structural = false;
        s.blocked = "infeasible";
        Json::Value d;
        d["action"] = "event";
        d["reason"] = x.what();
        s.dropped.append(d);
    }
}
// An impossible operation is dropped on its own; the rest of the event still happens.
void OperationCompiler::runOperation(EventScope &s, OpScope &op) {
    auto before = snapshot();
    Touched touched = s;
    pending.open();
    try {
        apply(s, op);
        pending.commit();
    } catch (const Infeasible &x) {
        pending.rollback();
        restore(std::move(before));
        static_cast<Touched &>(s) = std::move(touched);
        Json::Value d;
        d["action"] = op.name;
        d["reason"] = x.what();
        s.dropped.append(d);
        ++s.failedOps;
    }
}
void OperationCompiler::apply(EventScope &s, OpScope &op) {
    auto &name = op.name;
    if (!s.regional.empty() && name != "regional_initiative")
        throw Infeasible("regional initiatives require an explicit national response for national "
                         "operations");
    if (name == "regional_initiative" || name == "respond_regional")
        opRegional(s, op);
    else if (name == "change_identity")
        opChangeIdentity(s, op);
    else if (name == "create_actor")
        opCreateActor(s, op);
    else if (name == "open_conflict" || name == "add_commitment")
        opOpen(s, op);
    else if (name == "resolve_conflict" || name == "resolve_commitment")
        opResolve(s, op);
    else if (name == "amend_commitment")
        opAmend(s, op);
    else if (name == "settle_proposal")
        opSettle(s, op);
    else if (name == "set_conflict_intensity")
        opIntensity(s, op);
    else if (name == "build")
        opBuild(s, op);
    else if (name == "move_entity" || name == "set_entity_status")
        opEntity(s, op);
    else if (name == "transfer_territory" || name == "occupy" || name == "restore_control")
        opTerritory(s, op);
    else
        throw Infeasible("unknown operation " + name);
    if (op.done)
        return;
    markStructural(s, op);
    breachTreaties(s, op);
}
void OperationCompiler::markStructural(EventScope &s, const OpScope &op) const {
    static const std::set<std::string> shaping{
        "occupy",        "transfer_territory", "restore_control",
        "open_conflict", "resolve_conflict",   "set_conflict_intensity",
        "create_actor",  "change_identity",    "resolve_commitment"};
    auto &name = op.name;
    auto kind = op.o["kind"].asString();
    if (!op.proposal)
        s.structural = s.structural || op.shaped || shaping.contains(name) ||
                       (name == "add_commitment" && kind != "trade_agreement" && kind != "other") ||
                       (name == "build" && kind == "event_site") ||
                       (name == "set_entity_status" && op.o["status"] == "destroyed");
}
// Acting against a partner breaches the treaties of peace or alliance between them; a two-party one
// is broken.
void OperationCompiler::breachTreaties(EventScope &s, OpScope &op) {
    op.hostile.erase(s.ev);
    op.hostile.erase("");
    for (auto &target : op.hostile)
        for (auto &kid : w.commitments.getMemberNames()) {
            auto treaty = pending.current("commitment", w.commitments, kid);
            auto &p = treaty["actors"];
            if (!isOpen(treaty) || !securityTreaty(treaty["kind"].asString()) ||
                std::find(p.begin(), p.end(), Json::Value(s.ev)) == p.end() ||
                std::find(p.begin(), p.end(), Json::Value(target)) == p.end())
                continue;
            if (s.breaches.empty())
                s.breaches = kid;
            if (p.size() == 2) {
                auto &v =
                    pending.touch("commitment", kid, w.commitments[kid], s.id, "Broken: " + op.why);
                v["status"] = "resolved";
                v["ended_turn"] = w.turn;
                s.commitments.insert(kid);
                ending(s, treaty, "Broken: " + op.why);
                s.structural = true;
            }
        }
}
} // namespace lh::detail
namespace lh {
WorldDelta compileOperations(const WorldState &w, const Json::Value &response,
                             const Adjacency &adjacency, const Json::Value &responses,
                             const std::set<std::string> &priorEvents) {
    return detail::OperationCompiler(w, adjacency, priorEvents).compile(response, responses);
}
} // namespace lh
