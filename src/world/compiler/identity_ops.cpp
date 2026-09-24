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
} // namespace
void OperationCompiler::stepRelations(const std::string &r, Json::Value &p, int direction) const {
    static const std::vector<std::string> steps{"strained", "neutral", "cooperative"};
    // One step a month, whatever else happened to the region this month.
    if (!direction || p["relations"] != regionalState(w, r)["relations"])
        return;
    auto at = std::find(steps.begin(), steps.end(), p["relations"].asString());
    if (at == steps.end())
        return;
    auto next = std::clamp<long>(at - steps.begin() + direction, 0, long(steps.size()) - 1);
    p["relations"] = steps[size_t(next)];
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
        for (const auto &d : std::as_const(w.regionalPolitics)[r]["demands"])
            open = open || (d["id"] == id && d["status"] == "pending");
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
            e["description"] =
                answer == "accepted"   ? ruler + " granted " + place + "'s demand: " + text + "."
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
            found = true;
        }
    if (!found)
        throw Infeasible("no pending demand to settle");
    // Ignoring a demand is never better than refusing it; one addressed to a vanished authority
    // costs no one.
    stepRelations(r, p,
                  answer == "accepted"                                 ? 1
                  : answer == "rejected" || op.o["lapse"] == "unanswered" ? -1
                                                                        : 0);
    p["supporting_events"].append(s.id);
    if (p["supporting_events"].size() > 12)
        p["supporting_events"].removeIndex(0, nullptr);
    pending.touch("regional", r, w.regionalPolitics[r], s.id, op.why) = p;
    s.regions.insert(r);
    op.done = true;
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
            if (consequential())
                demand["answer_by_turn"] = answerBy(w, r, demand);
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
                    stepRelations(r, p, response == "accepted" ? 1 : -1);
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
