#include "localhistoria/domestic.h"
#include "world/compiler/operation_compiler.h"
#include <algorithm>
namespace lh::detail {
void OperationCompiler::prepareProject(const EventScope &s, Json::Value &event) {
    auto category = s.e.get("outcome_kind", "").asString();
    if (category != "reform" && category != "investment")
        return;
    for (const auto &x : event.get("effects", Json::Value(Json::arrayValue)))
        if (x["actor"] == s.ev &&
            (rank(x["economy"].asString()) < 2 || rank(x["stability"].asString()) < 2)) {
            event["project_reason"] = "setback; no project started";
            event["text"] = event["text"].asString() + " The setback did not start funded work.";
            return;
        }
    // Starting or restating funded work grants no immediate statistical reward.
    event.removeMember("effort");
    event.removeMember("effects");
    auto request = s.e["project"];
    auto reject = [&](const std::string &why) {
        event["project_reason"] = why;
        event["text"] = event["text"].asString() + " No funded project was recorded: " + why + ".";
    };
    if (!s.regional.empty() || !request.isObject()) {
        reject("unsupported or unspecified project");
        return;
    }
    // The schema names the benefit "sector" so it generates after the objective.
    if (request.isMember("sector")) {
        if (request.isMember("benefit")) {
            reject("unknown project field");
            return;
        }
        request["benefit"] = request["sector"];
        request.removeMember("sector");
    }
    // Each simulated benefit belongs to one category; the 4B sometimes labels an education
    // reform an investment. The chosen sector decides.
    for (const auto *kind : {"reform", "investment"})
        if (supportedProject(kind, request.get("benefit", "").asString()))
            category = kind;
    if (!domesticProjects.isMember(s.ev))
        domesticProjects[s.ev] =
            std::as_const(w.actors)[s.ev]["status"].get("projects", Json::Value(Json::objectValue));
    auto &projects = domesticProjects[s.ev];
    for (const auto &key : request.getMemberNames())
        if (key != "id" && key != "objective" && key != "target" && key != "benefit" &&
            key != "action") {
            reject("unknown project field");
            return;
        }
    if (!request["id"].isString()) {
        reject("invalid project reference");
        return;
    }
    auto id = request["id"].asString();
    auto action = request.get("action", id.empty() ? "start" : "continue").asString();
    if (action != "start" && action != "continue" && action != "cancel") {
        reject("invalid project action");
        return;
    }
    // The 4B invents ids (event ids, made-up slugs). Identity is derived from the terms, so an
    // id unknown to this actor is resolved by them; cancellation still needs the exact record.
    if (!projects.isMember(id)) {
        if (action == "cancel") {
            reject(id.empty() ? "invalid project action" : "project reference does not belong to this actor and category");
            return;
        }
        id.clear();
    }
    Json::Value p;
    if (!id.empty()) {
        if (!projects.isMember(id) || projects[id]["category"] != category) {
            reject("project reference does not belong to this actor and category");
            return;
        }
        p = projects[id];
        for (auto key : {"target", "benefit", "objective"})
            if (request.isMember(key) && request[key] != p[key]) {
                reject("a continuation cannot change its terms");
                return;
            }
    } else {
        for (auto key : {"objective", "target", "benefit"})
            if (!request[key].isString() || request[key].asString().empty()) {
                reject("missing project terms");
                return;
            }
        auto target = request["target"].asString(), benefit = request["benefit"].asString();
        const auto &t = std::as_const(w.territories)[target];
        if (request["objective"].asString().size() > 120 || !supportedProject(category, benefit) ||
            (target != s.ev && (t["owner"] != s.ev || t["controller"] != s.ev))) {
            reject("unsupported benefit or target not owned and controlled");
            return;
        }
        id = domesticProjectId(s.ev, category, target, benefit);
        if (projects.isMember(id))
            p = projects[id];
        else {
            if (projects.size() >= projectCapacity) {
                reject("project record capacity reached");
                return;
            }
            p = request;
            p.removeMember("action");
            p["id"] = id;
            p["category"] = category;
            p["origin_event"] = s.id;
            p["started"] = w.turn;
            p["state"] = "active";
            p["completion_condition"] = "paid_months";
            projects[id] = p;
            action = "start";
        }
    }
    event["project"] = p;
    event["project_action"] = action;
    if (p["state"] != "completed") {
        if (action == "cancel")
            projects[id]["state"] = "cancelled";
        else if (action == "continue")
            projects[id]["state"] = "active";
    }
    const bool fresh = p["origin_event"] == s.id;
    event["project_reason"] = p["state"] == "completed" ? "already completed"
                              : fresh                   ? "started"
                                                        : "continuation";
    if (p["state"] != "completed" && projects[id]["state"] == "cancelled")
        event["project_reason"] = "cancelled; paid work retained without refund";
    else if (p["state"] == "cancelled")
        event["project_reason"] = "resumed with prior paid work";
    event["text"] = s.text + " Project “" + p["objective"].asString() +
                    "”: " + event["project_reason"].asString() +
                    (p["state"] == "completed" ? "; no further completion benefit."
                                               : "; benefits require funded completion.");
}
void OperationCompiler::boundConsequences(const EventScope &s, Json::Value &event) {
    auto kind = s.e.get("outcome_kind", "policy").asString();
    if (s.e.get("background", false).asBool())
        kind = "world";
    if (kind != "reform" && kind != "investment" && kind != "negotiation" && kind != "security" &&
        kind != "protest" && kind != "world")
        kind = "policy";
    for (auto &x : event["effects"]) {
        auto actor = x["actor"].asString();
        if (!consequenceLedgers.isMember(actor))
            consequenceLedgers[actor] = std::as_const(w.actors)[actor]["status"].get(
                "recent_consequences", Json::Value(Json::objectValue));
        bool operation = x.get("operation", false).asBool();
        x.removeMember("operation");
        std::string identity;
        if (operation) {
            // Only actual accepted changes can ground an operation effect. Ignore free-text names.
            std::set<std::string> changes;
            for (const auto &c : pending)
                if (c.event == s.id && c.before != c.after) {
                    std::string key = c.kind + ":";
                    if (c.kind == "conflict" || c.kind == "commitment") {
                        key += c.after["kind"].asString() + ":";
                        std::set<std::string> parties;
                        for (const auto &a : c.after["actors"])
                            parties.insert(a.asString());
                        for (const auto &a : parties)
                            key += a + ":";
                        key += c.after["status"].asString() + ":" +
                               std::to_string(c.after.get("started_turn", w.turn).asInt());
                    } else {
                        key += c.id;
                        if (c.kind == "territory")
                            key += ":" + c.before["controller"].asString() + ":" +
                                   c.after["controller"].asString();
                    }
                    changes.insert(key);
                }
            for (const auto &change : changes)
                identity += change + "|";
            if (identity.empty()) {
                x["weight"] = 0;
                continue;
            }
        } else {
            identity = consequenceText(s.text);
            for (const auto &target : s.regions)
                identity += ":" + target;
        }
        std::string key = "event:" + s.ev + ":" + (operation ? "operation" : kind) + ":" + identity;
        auto record = recordConsequence(consequenceLedgers[actor], w.turn, key,
                                        operation ? "operation" : kind, !operation);
        record["actor"] = actor;
        x["weight"] = record["weight"];
        event["consequences"].append(record);
    }
    // Merely inspecting absent effects must not introduce a null delta field.
    if (event["effects"].isNull())
        event.removeMember("effects");
}
} // namespace lh::detail
