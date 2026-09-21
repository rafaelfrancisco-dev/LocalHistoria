#include "world/delta_internal.h"
namespace lh {
using namespace detail;
WorldDelta parseDelta(const Json::Value &j) {
    require(j.isObject() && j.getMemberNames() == std::vector<std::string>({"changes", "events"}),
            "Invalid delta shape");
    require(j["changes"].isArray() && j["changes"].size() <= 120 && j["events"].isArray() &&
                j["events"].size() <= 40,
            "Delta exceeds bounds");
    return {j["changes"], j["events"]};
}
Json::Value encode(const WorldDelta &d) {
    Json::Value j;
    j["changes"] = d.changes;
    j["events"] = d.events;
    return j;
}
Resolution applyDelta(const Resolution &base, const WorldDelta &d, const Json::Value &geo) {
    parseDelta(encode(d));
    Resolution r = base;
    auto &w = r.state;
    std::set<std::string> seen, eventIds;
    checkEvents(d, w, eventIds);
    for (auto &c : d.changes) {
        require(c.isObject() &&
                    c.getMemberNames() == std::vector<std::string>({"after", "before", "event",
                                                                    "id", "kind", "rationale"}),
                "Invalid change shape");
        require(c["kind"].isString() && c["id"].isString() && c["event"].isString() &&
                    c["rationale"].isString() && !c["rationale"].asString().empty() &&
                    c["rationale"].asString().size() <= 1000,
                "Invalid change metadata");
        auto id = c["id"].asString(), kind = c["kind"].asString();
        require(isPermanentId(id), "Invalid permanent ID");
        require(seen.insert(kind + ":" + id).second && eventIds.contains(c["event"].asString()),
                "Conflicting change or unknown event");
        Json::Value *collection = nullptr;
        if (kind == "actor")
            collection = &w.actors;
        if (kind == "territory")
            collection = &w.territories;
        if (kind == "entity")
            collection = &w.entities;
        if (kind == "conflict")
            collection = &w.conflicts;
        if (kind == "commitment")
            collection = &w.commitments;
        if (kind == "regional")
            collection = &w.regionalPolitics;
        if (kind == "proposal")
            collection = &w.proposals;
        require(collection, "Unknown change kind");
        require((*collection)[id] == c["before"], "Change precondition failed");
        require(c["after"].isObject(), "Deletion is not allowed; update status instead");
        if (kind == "actor")
            checkActorChange(c);
        if (kind == "regional")
            checkRegionalChange(c, id, w);
        if (kind == "proposal")
            checkProposalChange(c, w);
        if (kind == "territory")
            checkTerritoryChange(c);
        (*collection)[id] = c["after"];
    }
    checkTerritories(w);
    checkEntities(base, w, geo, eventIds);
    checkProposals(base, w);
    checkNewActors(base, w);
    for (auto &e : d.events) {
        require(w.hasActor(e["actor"].asString()), "Unknown event actor");
        for (auto &id : e["actors"])
            require(id.isString() && w.hasActor(id.asString()), "Unknown event related actor");
        for (auto &id : e["regions"])
            require(id.isString() && w.territories.isMember(id.asString()), "Unknown event region");
        for (auto &id : e["entities"])
            require(id.isString() && w.entities.isMember(id.asString()), "Unknown event entity");
        for (auto &x : e["effects"])
            require(w.hasActor(x["actor"].asString()), "Unknown effect actor");
        for (auto k : {"conflicts", "commitments"})
            if (e.isMember(k)) {
                require(e[k].isArray(), "Invalid event agreements");
                for (auto &id : e[k])
                    require(id.isString() &&
                                (k == std::string("conflicts") ? w.conflicts : w.commitments)
                                    .isMember(id.asString()),
                            "Unknown event agreement");
            }
        if (e["initiative_type"].isString() &&
            e["initiative_type"].asString().rfind("independent_", 0) == 0) {
            auto key = e.get("regional_actor", "").asString();
            if (key.empty())
                key = e["actor"].asString();
            require(!e.isMember("regional_actor") || w.territories.isMember(key),
                    "Unknown regional attribution");
            w.activity[key] = w.turn;
            w.activity[e["actor"].asString()] = w.turn;
        }
        // A rolled-back event stays in the audit but never reaches the journal, narration or later
        // prompts as if it happened.
        if (e["blocked_by"] != "infeasible")
            r.events.push_back({e["actor"].asString(),
                                e.get("initiative_type", "development").asString(),
                                e["id"].asString(), e["text"].asString()});
    }
    checkAgreements(base, w);
    return r;
}
} // namespace lh
