#include "world/compiler/operation_compiler.h"
#include <algorithm>
namespace lh::detail {
void OperationCompiler::opRegional(EventScope &s, OpScope &op) {
    auto &o = op.o;
    auto r = region(op);
    if (w.territories[r]["kind"] == "residual")
        throw Infeasible("residual geometry has no political actor");
    auto p = pending.current("regional", w.regionalPolitics, r);
    if (p.isNull())
        p = regionalState(w, r);
    if (op.name == "regional_initiative") {
        if (s.regional != r)
            throw Infeasible("only the selected regional actor can set its agenda");
        p["agenda"] = str(op, "agenda");
        p["relations"] = str(op, "relations");
        if (o["demand"].isString() && !o["demand"].asString().empty()) {
            if (p["demands"].size() >= 8) {
                for (Json::ArrayIndex n = 0; n < p["demands"].size(); ++n)
                    if (p["demands"][n]["status"] != "pending") {
                        p["demands"].removeIndex(n, nullptr);
                        break;
                    }
            }
            if (p["demands"].size() >= 8)
                throw Infeasible("regional pending demand capacity reached");
            Json::Value demand;
            demand["id"] = s.id;
            demand["text"] = o["demand"];
            demand["status"] = "pending";
            demand["authority_at_creation"] = s.ev;
            p["demands"].append(demand);
        }
    } else {
        if (!s.regional.empty() || w.territories[r]["controller"] != s.ev)
            throw Infeasible("only current national authority can respond");
        bool found = false;
        for (auto &d : p["demands"])
            if (d["id"] == str(op, "demand_id") && d["status"] == "pending") {
                auto response = str(op, "response");
                if (response != "accepted" && response != "rejected")
                    throw Infeasible("invalid national response");
                d["status"] = response;
                d["response_event"] = s.id;
                d["responding_authority"] = s.ev;
                found = true;
            }
        if (!found)
            throw Infeasible("no pending demand to answer");
    }
    p["supporting_events"].append(s.id);
    if (p["supporting_events"].size() > 12)
        p["supporting_events"].removeIndex(0, nullptr);
    pending.touch("regional", r, w.regionalPolitics[r], s.id, op.why) = p;
    s.regions.insert(r);
}
void OperationCompiler::opChangeIdentity(EventScope &s, OpScope &op) {
    auto a = party(s, op, "actor");
    if (a != s.ev)
        throw Infeasible("only a national actor can change its own identity");
    auto v = pending.current("actor", w.actors, a);
    auto type = str(op, "change_type");
    auto newName = str(op, "name");
    auto reason = str(op, "reason");
    if (type != "rename" && type != "refounding" && type != "correction")
        throw Infeasible("unknown identity change");
    if (reason.size() > 500 || newName.size() > 120)
        throw Infeasible("identity exceeds bounds");
    if (type == "correction") {
        if (!identityCorrection(newName, v["name"].asString()))
            throw Infeasible("substantive rename requires a new identity");
    }
    if (type == "rename" && identityCorrection(newName, v["name"].asString()))
        type = "correction";
    // A constitutional refounding is the one thing the model may write into an actor's status,
    // and only into this field. A spelling fix is not a revolution.
    auto government = op.o.get("government", "none").asString();
    // Read through a const view: the non-const operator[] would insert a null status on an actor
    // that has none yet, and that null would then be saved.
    auto current = static_cast<const Json::Value &>(v)["status"]["government"].asString();
    if (government != "none" && government != current) {
        if (type != "refounding")
            throw Infeasible("only a refounding changes a nation's political system");
        if (!validGovernment(government))
            throw Infeasible("unknown political system " + government);
        if (!static_cast<const Json::Value &>(v)["status"].isObject())
            throw Infeasible("an actor without national status has no political system");
        v["status"]["government"] = government;
        v["status"]["government_since"] = 2026 + int(w.turn / 12);
    }
    if (pending.contains("actor", a))
        throw Infeasible("only one identity change per actor per month");
    if (type == "correction") {
        v["name"] = newName;
        pending.touch("actor", a, w.actors[a], s.id, reason) = v;
        s.actors.insert(a);
        op.done = true;
        return;
    }
    // Capture original static identity before updating its name.
    if (!v["identity_revisions"].isArray()) {
        Json::Value old;
        if (!isCountryCode(a))
            old = makeFlag(Json::nullValue, w.campaignId + a, "legacy");
        else {
            old["static_flag"] = v.get("static_flag", a);
            old["origin_event"] = "initial";
        }
        old["name"] = v["name"];
        v["identity_revisions"].append(old);
    }
    v["name"] = newName;
    reviseIdentity(v, op.o["flag_design"], w.campaignId + a + s.id, s.id);
    pending.touch("actor", a, w.actors[a], s.id, reason) = v;
    s.actors.insert(a);
}
void OperationCompiler::opCreateActor(EventScope &s, OpScope &op) {
    if (++newActors > 1)
        throw Infeasible("only one new state can emerge per month");
    auto requested = str(op, "actor_id"), a = slug(str(op, "name"), 30);
    for (int n = 2; w.hasActor(a) || pending.contains("actor", a); ++n)
        a = slug(str(op, "name"), 30) + "-" + std::to_string(n);
    if (!w.hasActor(requested))
        aliases[requested] = a;
    aliases["~" + alphanumeric(str(op, "name"))] = a;
    aliases["~" + alphanumeric(requested)] = a;
    Json::Value identity;
    identity["name"] = str(op, "name");
    auto color = op.o.get("color", "").asString();
    identity["color"] = isHexColor(color) ? color : paletteColor(a);
    reviseIdentity(identity, op.o["flag_design"], w.campaignId + a + s.id, s.id);
    pending.touch("actor", a, Json::nullValue, s.id, op.why) = identity;
    s.actors.insert(a);
    s.createdHere.insert(a);
}
} // namespace lh::detail
