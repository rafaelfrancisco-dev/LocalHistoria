#include "localhistoria/demands.h"
#include "localhistoria/domestic.h"
#include "world/compiler/operation_compiler.h"
#include <algorithm>
namespace lh::detail {
namespace {
// A regional demand's last month for an answer: three months when the player governs the region,
// six (a demand's full weight) otherwise. Demands raised before deadlines count from the month in
// their ID.
int answerBy(const WorldState &w, const std::string &r, const Json::Value &d) {
    if (d["answer_by_turn"].isInt())
        return d["answer_by_turn"].asInt();
    auto id = d["id"].asString();
    int raised = w.turn;
    if (id.size() > 1 && id[0] == 't' && std::isdigit(static_cast<unsigned char>(id[1])))
        raised = std::atoi(id.c_str() + 1);
    return raised + (w.territories[r]["controller"] == w.player ? 3 : 6);
}
std::string demandText(const Json::Value &d) {
    auto text = d["text"].asString();
    std::replace(text.begin(), text.end(), '_', ' ');
    return text.substr(0, 200);
}
const std::vector<std::string> relationSteps{"strained", "neutral", "cooperative"};
long relationStep(const Json::Value &v) {
    auto at = std::find(relationSteps.begin(), relationSteps.end(), v.asString());
    return at == relationSteps.end() ? -1 : at - relationSteps.begin();
}
} // namespace
void OperationCompiler::stepRelations(const std::string &r, Json::Value &p, int direction) const {
    // One step a month, whatever else happened to the region this month.
    if (!direction || p["relations"] != regionalState(w, r)["relations"])
        return;
    auto at = relationStep(p["relations"]);
    if (at < 0)
        return;
    auto next = std::clamp<long>(at + direction, 0, long(relationSteps.size()) - 1);
    p["relations"] = relationSteps[size_t(next)];
}
// Answers the player gave to their regions' demands are applied as the month opens; a demand
// past its deadline lapses as a refusal, and one whose region changed hands lapses with no
// penalty: the authority it addressed is gone.
int OperationCompiler::settleDemands(Json::Value &all, const Json::Value &responses) const {
    if (!consequential())
        return 0;
    std::set<std::string> answered;
    for (const auto &a : responses) {
        if (!a.isMember("demand"))
            continue;
        auto r = a["region"].asString(), id = a["demand"].asString();
        bool open = false;
        // A typed demand is answered at its exact revision; a legacy one has none.
        for (const auto &d : std::as_const(w.regionalPolitics)[r]["demands"])
            open = open || (d["id"] == id && d["status"] == "pending" &&
                            (d.isMember("revision") ? a["revision"] == d["revision"]
                                                    : !a.isMember("revision")));
        if (!a["accept"].isBool() || !open || w.territories[r]["controller"] != w.player ||
            !answered.insert(r + "/" + id).second)
            throw ValidationError("Invalid or stale demand answer");
    }
    int count = 0;
    for (const auto &r : w.regionalPolitics.getMemberNames()) {
        auto controller = w.territories[r]["controller"].asString();
        if (!w.hasActor(controller))
            continue;
        for (const auto &d : w.regionalPolitics[r]["demands"]) {
            if (d["status"] != "pending")
                continue;
            std::string answer, lapse;
            for (const auto &a : responses)
                if (a["demand"] == d["id"] && a["region"] == r)
                    answer = a["accept"].asBool() ? "accepted" : "rejected";
            if (answer.empty() && d["authority_at_creation"].isString() &&
                d["authority_at_creation"] != controller) {
                answer = "expired";
                lapse = "authority_changed";
            } else if (answer.empty() && w.turn >= answerBy(w, r, d)) {
                answer = "expired";
                lapse = "unanswered";
            }
            if (answer.empty())
                continue;
            auto place = w.territories[r]["name"].asString(),
                 ruler = w.actors[controller]["name"].asString(), text = demandText(d);
            Json::Value e;
            e["actor"] = controller;
            e["blocked_by"] = "none";
            e["background"] = false;
            e["causes"] = Json::arrayValue;
            e["causes"].append(d["id"]);
            // Under typed demands accepting is not granting: the settlement adds what was done.
            e["description"] =
                answer == "accepted" && typedDemands(w)
                    ? ruler + " accepted " + place + "'s demand: " + text + "."
                : answer == "accepted" ? ruler + " granted " + place + "'s demand: " + text + "."
                : answer == "rejected" ? ruler + " refused " + place + "'s demand: " + text + "."
                : lapse == "unanswered"
                    ? place + "'s demand lapsed unanswered: " + text + "."
                    : place + "'s demand lapsed with the change of authority: " + text + ".";
            Json::Value op;
            op["action"] = "settle_demand";
            op["region"] = r;
            op["demand"] = d["id"];
            op["answer"] = answer;
            if (!lapse.empty())
                op["lapse"] = lapse;
            e["operations"].append(op);
            all.append(e);
            ++count;
        }
    }
    if (!typedDemands(w))
        return count;
    // Accepted funded work is kept or broken as the month opens: fulfilled once its project has
    // completed; broken when the ruler cancelled it or let the deadline pass while holding the
    // region; lapsed without blame when the region passed to another owner, or was occupied at
    // the deadline.
    for (const auto &r : w.regionalPolitics.getMemberNames())
        for (const auto &d : w.regionalPolitics[r]["demands"]) {
            const auto &f = d["fulfillment"];
            if (d["status"] != "accepted" || f["state"] != "in_progress")
                continue;
            const auto &t = w.territories[r];
            auto authority = d["responding_authority"].asString();
            auto actor = w.hasActor(authority) ? authority : t["controller"].asString();
            if (!w.hasActor(actor))
                continue;
            const auto &project =
                w.actors[authority]["status"]["projects"][f["project"].asString()];
            std::string outcome, reason;
            if (t["owner"] != authority) {
                outcome = "lapsed";
                reason = "the region passed to another authority";
            } else if (project["state"] == "completed")
                outcome = "fulfilled";
            else if (project["state"] != "active") {
                outcome = "failed";
                reason = "the funded work was cancelled";
            } else if (w.turn > f["deadline_turn"].asInt()) {
                outcome = t["controller"] == authority ? "failed" : "lapsed";
                reason = t["controller"] == authority ? "the work was not completed in time"
                                                      : "the region was occupied at the deadline";
            }
            if (outcome.empty())
                continue;
            auto place = t["name"].asString(), ruler = w.actors[actor]["name"].asString(),
                 text = demandText(d);
            Json::Value e;
            e["actor"] = actor;
            e["blocked_by"] = "none";
            e["background"] = false;
            e["causes"] = Json::arrayValue;
            e["causes"].append(d["id"]);
            e["description"] =
                outcome == "fulfilled" ? ruler + " fulfilled " + place + "'s demand: " + text +
                                             "; the funded work is complete."
                : outcome == "failed"
                    ? ruler + " broke its promise to " + place + ": " + text + " (" + reason + ")."
                    : place + "'s accepted demand lapsed: " + text + " (" + reason + ").";
            Json::Value op;
            op["action"] = "fulfill_demand";
            op["region"] = r;
            op["demand"] = d["id"];
            op["outcome"] = outcome;
            if (!reason.empty())
                op["reason"] = reason;
            e["operations"].append(op);
            all.append(e);
            ++count;
        }
    return count;
}
void OperationCompiler::opSettleDemand(EventScope &s, OpScope &op) {
    if (!s.trusted)
        throw Infeasible("unknown operation " + op.name);
    auto r = region(op), id = str(op, "demand"), answer = str(op, "answer");
    auto p = pending.current("regional", w.regionalPolitics, r);
    if (p.isNull())
        p = regionalState(w, r);
    bool found = false;
    // Ignoring a demand is never better than refusing it; one addressed to a vanished authority
    // costs no one.
    int direction = answer == "accepted"                                    ? 1
                    : answer == "rejected" || op.o["lapse"] == "unanswered" ? -1
                                                                            : 0;
    for (auto &d : p["demands"])
        if (d["id"] == id && d["status"] == "pending") {
            d["status"] = answer;
            d["answered_turn"] = w.turn;
            if (answer == "expired")
                d["lapse_reason"] = op.o["lapse"];
            else {
                d["response_event"] = s.id;
                d["responding_authority"] = s.ev;
            }
            if (answer == "accepted" && typedDemands(w))
                direction = grantDemand(s, r, d);
            found = true;
        }
    if (!found)
        throw Infeasible("no pending demand to settle");
    stepRelations(r, p, direction);
    p["supporting_events"].append(s.id);
    if (p["supporting_events"].size() > 12)
        p["supporting_events"].removeIndex(0, nullptr);
    pending.touch("regional", r, w.regionalPolitics[r], s.id, op.why) = p;
    s.regions.insert(r);
    op.done = true;
}
void OperationCompiler::opFulfillDemand(EventScope &s, OpScope &op) {
    if (!s.trusted)
        throw Infeasible("unknown operation " + op.name);
    auto r = region(op), id = str(op, "demand"), outcome = str(op, "outcome");
    auto p = pending.current("regional", w.regionalPolitics, r);
    if (p.isNull())
        p = regionalState(w, r);
    bool found = false;
    for (auto &d : p["demands"])
        if (d["id"] == id && d["status"] == "accepted" &&
            std::as_const(d)["fulfillment"]["state"] == "in_progress") {
            auto &f = d["fulfillment"];
            f["state"] = outcome;
            f["settled_turn"] = w.turn;
            f["event"] = s.id;
            if (op.o["reason"].isString())
                f["reason"] = op.o["reason"];
            found = true;
        }
    if (!found)
        throw Infeasible("no accepted demand awaiting fulfillment");
    // Delivery earns what acceptance did not; a broken promise costs what acceptance earned.
    stepRelations(r, p, outcome == "fulfilled" ? 1 : outcome == "failed" ? -1 : 0);
    p["supporting_events"].append(s.id);
    if (p["supporting_events"].size() > 12)
        p["supporting_events"].removeIndex(0, nullptr);
    pending.touch("regional", r, w.regionalPolitics[r], s.id, op.why) = p;
    s.regions.insert(r);
    op.done = true;
}
int OperationCompiler::grantDemand(EventScope &s, const std::string &r, Json::Value &d) {
    // Read through a const view: a legacy demand has no terms, and naming them must not add any.
    const auto terms = std::as_const(d)["terms"];
    const auto kind = terms.get("kind", "political").asString();
    const auto &t = w.territories[r];
    const auto place = t["name"].asString();
    Json::Value f;
    f["turn"] = w.turn;
    auto blocked = [&](const std::string &why) {
        f["state"] = "blocked";
        f["reason"] = why;
        d["fulfillment"] = f;
        s.verdict = "It cannot be carried out: " + why + ".";
        return 0;
    };
    // A legacy free-text demand is never read as permission to change anything.
    if (kind == "political") {
        f["state"] = "political";
        d["fulfillment"] = f;
        s.verdict = "Accepted for negotiation only; no law, border or budget changed.";
        return 1;
    }
    if (t["owner"] != s.ev)
        return blocked("only " +
                       w.actors[t["owner"].asString()].get("name", "its owner").asString() +
                       ", which owns " + place + ", can agree to this; holding it is not enough");
    if (kind == "regional_project") {
        auto category = terms["category"].asString(), benefit = terms["benefit"].asString();
        auto id = domesticProjectId(s.ev, category, r, benefit);
        if (!domesticProjects.isMember(s.ev))
            domesticProjects[s.ev] = std::as_const(w.actors)[s.ev]["status"].get(
                "projects", Json::Value(Json::objectValue));
        auto &projects = domesticProjects[s.ev];
        if (projects.isMember(id)) {
            // Work already under way answers the demand; finished or abandoned work cannot.
            if (projects[id]["state"] != "active")
                return blocked("that work was already " + projects[id]["state"].asString());
        } else {
            if (projects.size() >= projectCapacity)
                return blocked("the government's project record is full");
            Json::Value p;
            p["benefit"] = benefit;
            p["category"] = category;
            p["completion_condition"] = "paid_months";
            p["id"] = id;
            p["objective"] = terms["objective"];
            p["origin_event"] = s.id;
            p["started"] = w.turn;
            p["state"] = "active";
            p["target"] = r;
            projects[id] = p;
            s.demandProject = p;
        }
        f["state"] = "in_progress";
        f["project"] = id;
        f["deadline_turn"] = w.turn + fulfillmentMonths(terms);
        d["fulfillment"] = f;
        s.verdict = "Funded " + category + " work in " + place +
                    " begins from the national allowance; the demand is fulfilled only when it "
                    "completes.";
        return 1;
    }
    // Independence: the owner, holding the region, founds the state and cedes this region alone.
    bool remaining = false;
    for (const auto &id : w.territories.getMemberNames())
        if (id != r && (pending.contains("territory", id)
                            ? pending.current("territory", w.territories, id)["owner"] == s.ev
                            : w.territories[id]["owner"] == s.ev)) {
            remaining = true;
            break;
        }
    if (!remaining)
        return blocked("it would leave " + w.actors[s.ev]["name"].asString() +
                       " without territory");
    auto name = terms["new_state"].asString();
    auto before = snapshot();
    Touched touched = s;
    pending.open();
    try {
        Json::Value create;
        create["actor_id"] = slug(name, 30);
        create["name"] = name;
        OpScope founding(create);
        founding.name = "create_actor";
        founding.why = "Independence granted to " + place;
        opCreateActor(s, founding);
        Json::Value cession;
        cession["region"] = r;
        cession["to"] = aliases.at("~" + alphanumeric(name));
        OpScope transfer(cession);
        transfer.name = "transfer_territory";
        transfer.why = founding.why;
        opTerritory(s, transfer);
        pending.commit();
        f["actor"] = cession["to"];
    } catch (const Infeasible &x) {
        pending.rollback();
        restore(std::move(before));
        static_cast<Touched &>(s) = std::move(touched);
        return blocked(x.what());
    }
    s.structural = true;
    f["state"] = "fulfilled";
    d["fulfillment"] = f;
    s.verdict = "Independence took effect: " + name + " now governs " + place + ".";
    return 0;
}
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
        auto kind = o.get("demand_kind", "none").asString();
        auto text = o.get("demand", "").asString();
        if (typedDemands(w)) {
            // A region moves its own stance one step a month, and cannot erase a settlement's.
            auto want = relationStep(o["relations"]), have = relationStep(p["relations"]);
            if (want < 0)
                throw Infeasible("unknown regional relations");
            stepRelations(r, p, want > have ? 1 : want < have ? -1 : 0);
            // A small model writes "none" for no demand, and often names the kind but leaves the
            // text empty.
            if (lowercase(text) == "none")
                text.clear();
            if (text.empty() && kind == "independence")
                text = "Independence for " + w.territories[r]["name"].asString();
            else if (text.empty() && kind == "regional_project")
                text = "Funded " + o.get("project_benefit", "public works").asString() + " for " +
                       w.territories[r]["name"].asString();
        } else
            p["relations"] = str(op, "relations");
        if (!text.empty()) {
            if (p["demands"].size() >= 8) {
                // Settled records make room; accepted work still under way never does.
                for (Json::ArrayIndex n = 0; n < p["demands"].size(); ++n)
                    if (p["demands"][n]["status"] != "pending" &&
                        std::as_const(p["demands"][n])["fulfillment"]["state"] != "in_progress") {
                        p["demands"].removeIndex(n, nullptr);
                        break;
                    }
            }
            if (p["demands"].size() >= 8)
                throw Infeasible("regional pending demand capacity reached");
            Json::Value demand;
            demand["id"] = s.id;
            demand["text"] = text;
            demand["status"] = "pending";
            demand["authority_at_creation"] = s.ev;
            if (consequential())
                demand["answer_by_turn"] = answerBy(w, r, demand);
            if (typedDemands(w)) {
                demand["revision"] = demandRevision;
                demand["terms"] =
                    demandTerms(w, r, kind, o.get("project_benefit", "none").asString(), text);
            }
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
                if (consequential())
                    d["answered_turn"] = w.turn;
                found = true;
                // Granting eases relations with the capital; refusing sours them.
                if (consequential())
                    stepRelations(r, p,
                                  response != "accepted" ? -1
                                  : typedDemands(w)      ? grantDemand(s, r, d)
                                                         : 1);
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
