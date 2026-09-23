#include "localhistoria/lifecycle.h"
#include "localhistoria/conflicts.h"
namespace lh {
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
    // Old exports without a delta never ran delta validation. Preserve that path too.
    auto result = base;
    if (rules.lifecycle != legacyLifecycleVersion || reference.isMember("delta") ||
        !delta.changes.empty() || !delta.events.empty())
        result = applyDelta(base, delta, geography);
    if (rules.monthly)
        result = applyMonthlyStatus(result, delta, adjacency, rules.monthly);
    return result;
}
} // namespace lh
