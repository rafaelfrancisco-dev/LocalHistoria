#include "world/compiler/operation_compiler.h"
#include <algorithm>
#include <cmath>
namespace lh::detail {
void OperationCompiler::opBuild(EventScope &s, OpScope &op) {
    auto r = region(op);
    auto builder = party(s, op, "actor");
    auto kind = str(op, "kind");
    if (builder != s.ev && !s.createdHere.contains(builder))
        throw Infeasible("can only build for itself");
    auto t = pending.current("territory", w.territories, r);
    if (kind != "event_site" && w.territories[r]["controller"] != builder)
        throw Infeasible("can only build in regions it controls, not " +
                         w.territories[r]["name"].asString());
    require(t["longitude"].isNumeric() && t["latitude"].isNumeric(),
            "Region has no interior location");
    if (realism() && (kind == "fleet" || kind == "port") &&
        !w.territories[r].get("coastal", false).asBool())
        throw Infeasible("a " + kind + " needs a coast, which " +
                         w.territories[r]["name"].asString() + " lacks");
    // A civilian project halted when its builder lost the region is finished by the new
    // controller only by taking it over, and it still costs a construction slot to complete.
    if (realism() && (kind == "port" || kind == "base" || kind == "city"))
        for (const auto &id : w.entities.getMemberNames()) {
            auto e = pending.current("entity", w.entities, id);
            if (e["kind"] != kind || e["region"] != r || e["actor"] == builder ||
                !e["paused"].isObject())
                continue;
            if (w.actors[builder]["status"]["military"].isObject())
                allocateForce(builder, kind, e);
            auto &v = pending.touch("entity", id, w.entities[id], s.id, op.why);
            v["actor"] = builder;
            v["completes_turn"] = w.turn + e["paused"]["remaining_months"].asInt();
            v.removeMember("paused");
            s.actors.insert(builder);
            s.regions.insert(r);
            s.entities.insert(id);
            return;
        }
    auto eid = "e" + std::to_string(w.turn) + "-" + slug(str(op, "name"), 30);
    for (int n = 2; w.entities.isMember(eid) || pending.contains("entity", eid); ++n)
        eid = "e" + std::to_string(w.turn) + "-" + slug(str(op, "name"), 30) + "-" +
              std::to_string(n);
    Json::Value v;
    v["name"] = str(op, "name");
    v["kind"] = kind;
    v["actor"] = builder;
    v["region"] = r;
    v["longitude"] = t["longitude"];
    v["latitude"] = t["latitude"];
    v["origin_event"] = s.id;
    // Construction takes months; the finished entity appears when resolve() reaches
    // completes_turn.
    if (int months = constructionMonths(kind)) {
        v["status"] = "under_construction";
        v["completes_turn"] = w.turn + months;
    } else
        v["status"] = "active";
    if (kind == "event_site")
        v["expires_turn"] = w.turn + eventMapMonths;
    if (w.actors[builder]["status"]["military"].isObject() && kind != "event_site")
        allocateForce(builder, kind, v);
    pending.touch("entity", eid, Json::nullValue, s.id, op.why) = v;
    s.actors.insert(builder);
    s.regions.insert(r);
    s.entities.insert(eid);
}
// A nation with military figures builds within its construction capacity and forms armies and
// fleets from what is not yet committed.
void OperationCompiler::allocateForce(const std::string &builder, const std::string &kind,
                                      Json::Value &v) const {
    auto allocated = allocatedWorld();
    int projects = 0;
    for (const auto &e : allocated.entities)
        if (e["actor"] == builder && e["status"] == "under_construction" &&
            e["kind"] != "event_site")
            ++projects;
    if (projects >= force::constructionCapacity(w, builder))
        throw Infeasible("construction capacity reached");
    auto &military = w.actors[builder]["status"]["military"];
    if (kind == "army") {
        int available = force::uncommittedPersonnel(allocated, builder);
        if (available < 5000)
            throw Infeasible("no troops left to form an army");
        v["personnel"] = std::min(
            available, std::clamp(int(std::round(military["active"].asDouble() / 10000)) * 1000,
                                  5000, 150000));
    } else if (kind == "fleet") {
        int available = force::uncommittedShips(allocated, builder);
        if (!available)
            throw Infeasible("no major warships available");
        v["ships"] = std::min(
            available, std::clamp(int(std::round(military["ships"].asDouble() * 0.25)), 1, 30));
    }
}
void OperationCompiler::opEntity(EventScope &s, OpScope &op) {
    auto eid = str(op, "entity");
    if (!w.entities.isMember(eid))
        throw Infeasible("unknown or unfinished entity");
    auto &v = pending.touch("entity", eid, w.entities[eid], s.id, op.why);
    auto owner = w.entities[eid]["actor"].asString();
    if (op.name == "move_entity")
        moveEntity(s, op, eid, v, owner);
    else
        setEntityStatus(s, op, eid, v, owner);
    s.entities.insert(eid);
    s.actors.insert(owner);
}
void OperationCompiler::moveEntity(EventScope &s, const OpScope &op, const std::string &eid,
                                   Json::Value &v, const std::string &owner) {
    auto &entity = w.entities[eid];
    auto r = region(op);
    auto from = entity["region"].asString();
    auto kind = entity["kind"].asString();
    if (s.ev != owner)
        throw Infeasible("only its owner can move " + entity["name"].asString());
    if (entity["status"] != "active")
        throw Infeasible(entity["name"].asString() + " is not ready");
    if (realism())
        checkPassage(eid, r);
    else if (kind == "army") {
        if (!adjacent(from, r))
            throw Infeasible("an army advances at most one region per month");
    } else if (kind == "fleet") {
        if (kilometres(w.territories[from], w.territories[r]) > 1500)
            throw Infeasible("a fleet sails at most 1,500 km per month");
    } else
        throw Infeasible("a " + kind + " cannot be relocated");
    auto t = pending.current("territory", w.territories, r);
    v["region"] = r;
    v["longitude"] = t["longitude"];
    v["latitude"] = t["latitude"];
    s.regions.insert(r);
}
// Where a formation may go this month. Armies march one region, or sail with a fleet that shares
// their region and can lift them; fleets sail their routes to a coast. Both need the destination's
// controller's leave: their own ground, access by treaty, or (for an army) ground an occupation
// took earlier in this month's events. A fleet may also lie off an enemy coast. Anything else is
// transit through a neutral, or hostile entry that has not been fought for.
void OperationCompiler::checkPassage(const std::string &eid, const std::string &r) const {
    const auto &e = w.entities[eid];
    auto a = e["actor"].asString(), from = e["region"].asString(), kind = e["kind"].asString();
    auto place = w.territories[r]["name"].asString();
    auto now = allocatedWorld();
    // Ground taken, treaties signed and wars declared earlier this month count.
    for (const auto &p : pending)
        if (p.kind == "territory")
            now.territories[p.id] = p.after;
        else if (p.kind == "commitment")
            now.commitments[p.id] = p.after;
        else if (p.kind == "conflict")
            now.conflicts[p.id] = p.after;
    auto holder = now.territories[r]["controller"].asString();
    auto route = seaKm(adjacency, from, r);
    auto inRange = route >= 0 && route <= fleetRangeKm;
    if (kind == "fleet") {
        if (!w.territories[r].get("coastal", false).asBool())
            throw Infeasible("a fleet cannot sail inland to " + place);
        // A fleet saved before the route graph existed may sit inland; it can still put to sea on
        // a bordering coast.
        if (!inRange && !(adjacency.sea.find(from) == adjacency.sea.end() && adjacent(from, r)))
            throw Infeasible("no sea route within 1,500 km reaches " + place);
        if (!mayEnter(now, a, r) && !lh::fighting(now, a, holder))
            throw Infeasible("no port access at " + place);
        return;
    }
    if (kind != "army")
        throw Infeasible("a " + kind + " cannot be relocated");
    if (!mayEnter(now, a, r))
        throw Infeasible(lh::fighting(now, a, holder)
                             ? place + " must be taken by an occupation before troops move in"
                             : "no military access to " + place);
    if (adjacent(from, r))
        return;
    int lift = 0;
    for (const auto &f : now.entities)
        if (f["actor"] == a && f["kind"] == "fleet" && f["status"] == "active" &&
            f["region"] == from)
            lift += f.get("ships", 3).asInt() * troopsPerShip;
    if (!lift || !inRange || !w.territories[r].get("coastal", false).asBool())
        throw Infeasible(
            "an army advances one region a month, or sails with a fleet in its region");
    if (lift < e.get("personnel", 10000).asInt())
        throw Infeasible("the fleet here can lift only " + std::to_string(lift) + " troops");
}
void OperationCompiler::setEntityStatus(EventScope &s, OpScope &op, const std::string &eid,
                                        Json::Value &v, const std::string &owner) {
    auto &entity = w.entities[eid];
    auto &ev = s.ev;
    auto status = str(op, "status");
    if (entity["status"] == "under_construction" && status == "active")
        throw Infeasible("construction is still under way");
    if (entity["status"] == "destroyed" && status != "destroyed")
        throw Infeasible("a destroyed entity cannot be restored");
    if (ev != owner && (status == "active" || !reaches(ev, entity["region"].asString())))
        throw Infeasible("cannot reach " + entity["name"].asString());
    if (ev != owner && status == "destroyed") {
        if (!atWar(ev, owner))
            throw Infeasible("not at war with " + w.actors[owner]["name"].asString());
        op.hostile.insert(owner);
    }
    if (ev != owner && status != "active" && w.actors[ev]["status"]["military"].isObject() &&
        force::assaultCapacity(w, adjacency, ev, owner, entity["region"].asString()) < 1)
        throw Infeasible("too weak to defeat this formation");
    if (status == "active" && v["status"] == "inactive" &&
        w.actors[owner]["status"]["military"].isObject()) {
        auto allocated = allocatedWorld();
        if (v["kind"] == "army" &&
            force::uncommittedPersonnel(allocated, owner) < v.get("personnel", 10000).asInt())
            throw Infeasible("no troops left to reactivate army");
        if (v["kind"] == "fleet" &&
            force::uncommittedShips(allocated, owner) < v.get("ships", 3).asInt())
            throw Infeasible("no major warships available");
    }
    v["status"] = status;
    if (status != "under_construction") {
        v.removeMember("completes_turn");
        // Abandoning or destroying a halted project ends its pause too.
        v.removeMember("paused");
    }
    if (status != "active")
        v.removeMember("expires_turn");
}
void OperationCompiler::take(const std::string &a, const std::string &r) {
    if (!reaches(a, r))
        throw Infeasible("no border, army or fleet within reach of " +
                         w.territories[r]["name"].asString());
    auto defender = w.territories[r]["controller"].asString();
    if (defender.empty())
        defender = w.territories[r]["owner"].asString();
    if (w.actors[a]["status"]["military"].isObject() && defender != a) {
        int cap = force::assaultCapacity(w, adjacency, a, defender, r);
        if (!cap)
            throw Infeasible("too weak to occupy " + w.territories[r]["name"].asString());
        int used = 0;
        for (const auto &held : taken[a]) {
            auto d = w.territories[held]["controller"].asString();
            if (d.empty())
                d = w.territories[held]["owner"].asString();
            if (d == defender && held != r)
                ++used;
        }
        if (used >= cap)
            throw Infeasible("military strength limits gains against this defender this month");
    }
    taken[a].insert(r);
    if (taken[a].size() > 2)
        throw Infeasible("at most two regions can change hands by force in one month");
}
// The start-of-month world with this month's entities, less the forces of formations destroyed
// here.
WorldState OperationCompiler::allocatedWorld() const {
    auto now = w;
    for (const auto &p : pending)
        if (p.kind == "entity") {
            now.entities[p.id] = p.after;
            if (p.after["status"] == "destroyed" && p.before["status"] != "destroyed") {
                auto a = p.after["actor"].asString();
                if (!static_cast<const WorldState &>(now)
                         .actors[a]["status"]["military"]
                         .isObject())
                    continue;
                const char *field = p.after["kind"] == "army" ? "active" : "ships";
                const char *quantity = p.after["kind"] == "army" ? "personnel" : "ships";
                if (p.after["kind"] == "army" || p.after["kind"] == "fleet") {
                    auto &m = now.actors[a]["status"]["military"];
                    m[field] = std::max(
                        0,
                        m[field].asInt() -
                            p.after.get(quantity, p.after["kind"] == "army" ? 10000 : 3).asInt());
                }
            }
        }
    return now;
}
} // namespace lh::detail
