#include "localhistoria/lifecycle.h"
#include "localhistoria/conflicts.h"
#include <algorithm>
namespace lh {
Json::Value handOverProject(const Json::Value &e, const std::string &from, const std::string &to,
                            int turn) {
    if (e["status"] != "under_construction" || e["actor"] != from || from == to || to.empty())
        return Json::nullValue;
    auto next = e;
    auto kind = e["kind"].asString();
    if (kind == "army" || kind == "fleet") {
        next["status"] = "inactive";
        next.removeMember("completes_turn");
        next.removeMember("paused");
        return next;
    }
    if (kind != "port" && kind != "base" && kind != "city")
        return Json::nullValue;
    next["actor"] = to;
    if (e["paused"].isObject()) {
        next["completes_turn"] = turn + e["paused"]["remaining_months"].asInt();
        next.removeMember("paused");
    }
    return next;
}
void reconcileProjects(WorldState &w, std::vector<ResolvedEvent> &events) {
    for (auto &id : w.entities.getMemberNames()) {
        auto &e = w.entities[id];
        if (e["status"] != "under_construction" || e["kind"] == "event_site")
            continue;
        auto builder = e["actor"].asString(), region = e["region"].asString();
        const auto &t = std::as_const(w.territories)[region];
        auto holder = t["controller"].asString();
        auto label =
            e["name"].asString() + " (" + e["kind"].asString() + ") in " + t["name"].asString();
        if (!e.isMember("paused") && holder != builder &&
            std::as_const(e)["completes_turn"].isInt()) {
            Json::Value paused;
            paused["since"] = w.turn;
            paused["remaining_months"] = std::max(1, e["completes_turn"].asInt() - w.turn);
            e["paused"] = paused;
            e.removeMember("completes_turn");
            auto by = holder.empty() ? std::string("no one")
                                     : std::as_const(w.actors)[holder]["name"].asString();
            events.push_back({builder, "construction_paused", id,
                              "Work on " + label + " halted: " + by + " now holds the region.",
                              region});
        } else if (std::as_const(e)["paused"].isObject() && holder == builder) {
            e["completes_turn"] = w.turn + e["paused"]["remaining_months"].asInt();
            e.removeMember("paused");
            events.push_back(
                {builder, "construction_resumed", id, "Work on " + label + " resumed.", region});
        }
    }
}
LifecycleRules lifecycleRules(const Json::Value &reference) {
    LifecycleRules rules;
    if (reference.isMember("lifecycle_version")) {
        if (!reference["lifecycle_version"].isInt())
            throw ValidationError("Invalid lifecycle version");
        rules.lifecycle = reference["lifecycle_version"].asInt();
    }
    switch (rules.lifecycle) {
    case legacyLifecycleVersion:
    case realismLifecycleVersion:
        break;
    default:
        throw ValidationError("Unsupported lifecycle version");
    }
    if (reference.isMember("status_version")) {
        if (!reference["status_version"].isInt())
            throw ValidationError("Invalid status version");
        rules.monthly = reference["status_version"].asInt();
        if (rules.monthly < 1 || rules.monthly > statusVersion)
            throw ValidationError("Unsupported status version");
    }
    return rules;
}
Json::Value lifecycleReference(const WorldState &w) {
    Json::Value ref(Json::objectValue);
    ref["lifecycle_version"] = realismLifecycleVersion;
    ref["status_version"] = statusVersion;
    const int prior = w.engine.get("lifecycle_version", legacyLifecycleVersion).asInt();
    if (prior != realismLifecycleVersion) {
        if (prior != legacyLifecycleVersion)
            throw ValidationError("Unsupported campaign lifecycle");
        ref["lifecycle_migration"]["from"] = prior;
        ref["lifecycle_migration"]["to"] = realismLifecycleVersion;
    }
    return ref;
}
Resolution beginTurn(const WorldState &source, const Json::Value &reference) {
    const auto rules = lifecycleRules(reference);
    auto w = source;
    // Preserve the order used by historical replay and the HTTP route.
    if (reference.isMember("status_seed"))
        seedStatus(w, decodeSeed(reference["status_seed"]), false);
    if (reference.isMember("military_seed"))
        seedMilitary(w, decodeSeed(reference["military_seed"]));
    if (reference.isMember("government_seed"))
        seedGovernment(w, decodeSeed(reference["government_seed"]));
    if (reference.isMember("lifecycle_migration")) {
        const auto &m = reference["lifecycle_migration"];
        if (rules.lifecycle != realismLifecycleVersion || !m["from"].isInt() || !m["to"].isInt() ||
            m["from"] != legacyLifecycleVersion || m["to"] != realismLifecycleVersion ||
            m.getMemberNames() != std::vector<std::string>{"from", "to"})
            throw ValidationError("Invalid lifecycle migration");
        const int prior = w.engine.get("lifecycle_version", legacyLifecycleVersion).asInt();
        if (prior != legacyLifecycleVersion && prior != realismLifecycleVersion)
            throw ValidationError("Lifecycle migration precondition failed");
        w.engine["lifecycle_version"] = realismLifecycleVersion;
    }
    if (rules.lifecycle == realismLifecycleVersion &&
        w.engine.get("lifecycle_version", legacyLifecycleVersion) != realismLifecycleVersion)
        throw ValidationError("Missing lifecycle migration");
    if (rules.lifecycle == realismLifecycleVersion)
        initializeConflictPairs(w);
    return resolve(w, rules.lifecycle);
}
Resolution finishTurn(const Resolution &base, const WorldDelta &delta, const Adjacency &adjacency,
                      const Json::Value &geography, const Json::Value &reference) {
    const auto rules = lifecycleRules(reference);
    if (reference.isMember("sea_route_revision") &&
        reference["sea_route_revision"].asString() != adjacency.seaRevision)
        throw ValidationError("Sea route graph differs from the one this turn used");
    // Old exports without a delta never ran delta validation. Preserve that path too.
    auto result = base;
    if (rules.lifecycle != legacyLifecycleVersion || reference.isMember("delta") ||
        !delta.changes.empty() || !delta.events.empty())
        result = applyDelta(base, delta, geography);
    if (rules.monthly)
        result = applyMonthlyStatus(result, delta, adjacency, rules.monthly);
    if (rules.lifecycle == realismLifecycleVersion)
        reconcileProjects(result.state, result.events);
    return result;
}
} // namespace lh
