#include "localhistoria/lifecycle.h"
#include "world/compiler/operation_compiler.h"
#include <algorithm>
namespace lh::detail {
// At war since before this month, or declared earlier in this month's outcomes.
bool OperationCompiler::atWar(const std::string &a, const std::string &b) const {
    if (realism())
        return lh::fighting(allocatedWorld(), a, b);
    auto fighting = [&](const Json::Value &c) {
        return isOpen(c) && isParty(c, a) && isParty(c, b);
    };
    bool war = false;
    for (auto &c : w.conflicts)
        war = war || fighting(c);
    for (auto &c : pending)
        war = war || (c.kind == "conflict" && fighting(c.after));
    return war;
}
void OperationCompiler::opTerritory(EventScope &s, OpScope &op) {
    auto r = region(op);
    auto owner = w.territories[r]["owner"].asString(),
         controller = w.territories[r]["controller"].asString();
    auto &t = pending.touch("territory", r, w.territories[r], s.id, op.why);
    if (op.name == "transfer_territory")
        transfer(s, op, r, t, owner, controller);
    else if (op.name == "occupy")
        occupy(s, op, r, t, owner, controller);
    else {
        if (s.ev == owner && controller != owner)
            take(s.ev, r);
        else if (s.ev != controller)
            throw Infeasible("only the owner or the occupier can change control");
        t["controller"] = t["owner"];
    }
    s.regions.insert(r);
    for (auto k : {"owner", "controller"})
        if (!t[k].asString().empty())
            s.actors.insert(t[k].asString());
}
void OperationCompiler::transfer(EventScope &s, const OpScope &op, const std::string &r,
                                 Json::Value &t, const std::string &owner,
                                 const std::string &controller) {
    auto &ev = s.ev;
    auto to = party(s, op, "to");
    // Owners may cede; others must already have held the region before this month to annex it.
    bool cession = ev == owner,
         annexation = controller == ev && (to == ev || s.createdHere.contains(to)),
         returned = controller == ev && to == owner;
    if (s.createdHere.contains(to) && !cession) {
        bool established = false;
        for (auto &conflict : w.conflicts)
            if (isOpen(conflict)) {
                auto &parties = conflict["actors"];
                bool acting =
                    std::find(parties.begin(), parties.end(), Json::Value(ev)) != parties.end();
                bool governing =
                    std::find(parties.begin(), parties.end(), Json::Value(owner)) != parties.end();
                established = established || (acting && governing);
            }
        if (!established)
            throw Infeasible("independence needs national agreement or an established relevant "
                             "conflict");
    }
    if (!cession && !annexation && !returned)
        throw Infeasible(controller == ev ? "cannot hand over land it does not own"
                                          : "must occupy " + w.territories[r]["name"].asString() +
                                                " for at least a month before annexing it");
    if (!cession && to != owner && !owner.empty()) {
        s.seizedOwners.insert(owner);
        s.warParties.insert(ev);
    }
    t["owner"] = to;
    t["controller"] = to;
    // Ceded or handed-back land takes its unfinished civilian works with it.
    if (realism() && (cession || returned))
        handOver(s, r, controller, to, op.why);
}
void OperationCompiler::handOver(EventScope &s, const std::string &region, const std::string &from,
                                 const std::string &to, const std::string &why) {
    std::set<std::string> ids;
    for (const auto &id : w.entities.getMemberNames())
        ids.insert(id);
    for (const auto &p : pending)
        if (p.kind == "entity")
            ids.insert(p.id);
    for (const auto &id : ids) {
        auto e = pending.current("entity", w.entities, id);
        if (e["region"] != region)
            continue;
        auto next = handOverProject(e, from, to, w.turn);
        if (next.isNull())
            continue;
        pending.touch("entity", id, w.entities[id], s.id, why) = next;
        s.entities.insert(id);
        s.actors.insert(from);
        s.actors.insert(to);
    }
}
void OperationCompiler::occupy(EventScope &s, OpScope &op, const std::string &r, Json::Value &t,
                               const std::string &owner, const std::string &controller) {
    auto by = party(s, op, "by");
    if (by != s.ev && !s.createdHere.contains(by))
        throw Infeasible("an occupation must be carried out by the acting nation");
    if (controller != by) {
        take(s.ev, r);
        if (!owner.empty() && owner != by) {
            s.seizedOwners.insert(owner);
            s.warParties.insert(s.ev);
        }
        op.hostile.insert(owner);
        op.hostile.insert(controller);
    }
    t["controller"] = by;
}
std::string OperationCompiler::str(const OpScope &op, const char *k) const {
    if (!op.o[k].isString() || op.o[k].asString().empty())
        throw Infeasible("incomplete " + op.name);
    return op.o[k].asString();
}
std::string OperationCompiler::region(const OpScope &op) const {
    auto r = str(op, "region");
    if (!w.territories.isMember(r))
        throw Infeasible("unknown region");
    return r;
}
std::string OperationCompiler::party(const EventScope &s, const OpScope &op, const char *k) const {
    auto a = actorRef(str(op, k));
    // A small model often names the new state after its region; the state this event created is
    // the only sensible recipient.
    if (!known(a) && s.createdHere.size() == 1)
        a = *s.createdHere.begin();
    if (!known(a))
        throw Infeasible("unknown actor " + a);
    return a;
}
std::string OperationCompiler::actorRef(const std::string &v) const {
    if (w.hasActor(v))
        return v;
    if (aliases.contains(v))
        return aliases.at(v);
    if (aliases.contains("~" + alphanumeric(v)))
        return aliases.at("~" + alphanumeric(v));
    return v;
}
bool OperationCompiler::known(const std::string &a) const {
    return w.hasActor(a) || pending.contains("actor", a);
}
bool OperationCompiler::adjacent(const std::string &from, const std::string &to) const {
    return lh::adjacent(adjacency, from, to);
}
bool OperationCompiler::reaches(const std::string &a, const std::string &r) const {
    return lh::reaches(w, adjacency, a, r);
}
} // namespace lh::detail
