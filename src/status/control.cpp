#include "localhistoria/conflicts.h"
#include "status/status_internal.h"
namespace lh {
using namespace detail;
bool localRule(const WorldState &w, const std::string &controller, const std::string &owner) {
    const auto &rebellion = w.actors[controller]["rebellion"];
    if (rebellion.isObject() && !rebellion.get("state", false).asBool())
        return true;
    for (const auto &c : w.conflicts)
        if (isOpen(c) && (c["kind"] == "civil_war" || c["kind"] == "insurgency") &&
            isParty(c, controller) && isParty(c, owner))
            return true;
    return false;
}
bool agreedOccupation(const WorldState &w, const std::string &region, const std::string &by) {
    auto owner = w.territories[region]["owner"].asString();
    for (const auto &k : w.commitments)
        if (isOpen(k) && isParty(k, owner) && isParty(k, by))
            for (const auto &t : k["terms"])
                if (t["action"] == "occupy" && t["region"] == region && t["by"] == by)
                    return true;
    return false;
}
const Json::Value &hostileControl(const WorldState &w, const std::string &region) {
    static const Json::Value none;
    const auto &t = w.territories[region];
    const auto &record = t["control"];
    if (record.isObject() && record["holder"] == t["controller"])
        return record;
    return none;
}
double outputShare(const WorldState &w, const std::string &region, const std::string &a) {
    const auto &t = w.territories[region];
    auto owner = t["owner"].asString(), controller = t["controller"].asString();
    if (controller.empty() || controller == owner) {
        if (controller != a && owner != a)
            return 0;
        const auto &record = hostileControl(w, region);
        return record.isObject() ? rate::conqueredOutput + (1 - rate::conqueredOutput) *
                                                               number(record, "integration") / 100
                                 : 1;
    }
    // Held by someone other than its owner: the owner has lost it, unless it lent it by treaty.
    if (agreedOccupation(w, region, controller))
        return owner == a ? 1 : 0;
    if (controller != a)
        return 0;
    const auto &record = hostileControl(w, region);
    return rate::conqueredOutput +
           (1 - rate::conqueredOutput) * number(record, "integration") / 100;
}
std::pair<double, double> effectiveOutput(const WorldState &w, const std::string &a) {
    double gdp = 0, population = 0;
    // Iterated without copying every region's ID: strength and capacity ask this per assault.
    for (auto it = w.territories.begin(); it != w.territories.end(); ++it) {
        const auto &t = *it;
        if (t["owner"] != a && t["controller"] != a)
            continue;
        double share = outputShare(w, it.name(), a);
        gdp += share * number(t, "gdp_usd");
        population += share * number(t, "population");
    }
    return {gdp, population};
}
void recordControl(WorldState &w, bool migrating) {
    for (const auto &id : w.territories.getMemberNames()) {
        const auto &t = std::as_const(w.territories)[id];
        if (t["kind"] == "residual")
            continue;
        auto owner = t["owner"].asString(), controller = t["controller"].asString();
        const auto &record = t["control"];
        auto claimant = record.isObject() ? record["claimant"].asString() : owner;
        bool ended = controller.empty() || controller == claimant ||
                     localRule(w, controller, claimant) ||
                     (!record.isObject() && agreedOccupation(w, id, controller));
        if (ended) {
            if (record.isObject())
                w.territories[id].removeMember("control");
            continue;
        }
        if (record.isObject() && record["holder"] == controller)
            continue;
        // Taken from its claimant, or taken again from whoever held it: the claim stays with the
        // actor it was first taken from, and the new holder starts from nothing.
        Json::Value next;
        next["claimant"] = claimant;
        next["holder"] = controller;
        if (migrating)
            next["cause"] = "recorded";
        else {
            next["cause"] = "hostile";
            next["since"] = w.turn;
        }
        next["integration"] = 0.0;
        w.territories[id]["control"] = next;
    }
}
void recordDisplacement(WorldState &w) {
    for (const auto &id : w.territories.getMemberNames()) {
        const auto &t = std::as_const(w.territories)[id];
        auto outstanding = t.get("displaced", 0).asInt64();
        if (outstanding > 0 && !t.isMember("displacement"))
            w.territories[id]["displacement"]["unknown"] = outstanding;
    }
}
void recordExhaustion(WorldState &w) {
    for (const auto &a : w.actors.getMemberNames()) {
        const auto &m = std::as_const(w.actors)[a]["status"]["military"];
        if (!m.isObject() || m.isMember("exhaustion"))
            continue;
        bool worn = false;
        for (const auto &c : std::as_const(w.conflicts))
            if (isOpen(c) && isParty(c, a) && c["started_turn"].isInt() &&
                w.turn - c["started_turn"].asInt() >= 12)
                for (const auto &p : c["actors"])
                    worn = worn || (p != a && fightingPair(w, c, a, p.asString()));
        w.actors[a]["status"]["military"]["exhaustion"] = worn ? rate::exhaustionSeeded : 0.0;
    }
}
} // namespace lh
