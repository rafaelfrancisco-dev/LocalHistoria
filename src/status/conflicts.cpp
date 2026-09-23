#include "localhistoria/conflicts.h"
#include <algorithm>
namespace lh {
bool realismRules(const WorldState &w) { return w.engine.get("lifecycle_version", 0).asInt() == 1; }
std::string hostilePairKey(const std::string &a, const std::string &b) {
    return a < b ? a + ":" + b : b + ":" + a;
}
Json::Value hostilePairs(const WorldState &w, const Json::Value &c) {
    if (c["hostile_pairs"].isArray())
        return c["hostile_pairs"];
    Json::Value pairs(Json::arrayValue);
    std::set<std::string> actors;
    for (const auto &a : c["actors"])
        actors.insert(a.asString());
    for (const auto &a : actors)
        for (const auto &b : actors) {
            if (a >= b)
                continue;
            bool allies = false;
            // An ambiguous old coalition never turns known allies into enemies.
            for (const auto &k : w.commitments)
                if (isOpen(k) && isParty(k, a) && isParty(k, b) &&
                    (k["kind"] == "alliance" || k["kind"] == "defence_pact" ||
                     k["kind"] == "union"))
                    allies = true;
            if (allies && actors.size() > 2)
                continue;
            Json::Value p;
            p["actors"].append(a);
            p["actors"].append(b);
            p["status"] = "active";
            pairs.append(p);
        }
    return pairs;
}
void initializeConflictPairs(WorldState &w) {
    for (auto &c : w.conflicts) {
        if (!c["hostile_pairs"].isArray()) {
            c["hostile_pairs"] = hostilePairs(w, c);
            c["pair_provenance"] =
                c["actors"].size() == 2 ? "legacy_two_party" : "legacy_alliance_filtered_fallback";
        }
        if (!c["postures"].isObject()) {
            c["postures"] = Json::objectValue;
            for (const auto &a : c["actors"])
                c["postures"][a.asString()] = c.get("set_intensity", "medium");
        }
    }
}
int pairPosture(const Json::Value &c, const std::string &a, const std::string &b) {
    auto rank = [](const Json::Value &v) {
        return v == "high" ? 2 : v == "medium" ? 1 : v == "low" ? 0 : -1;
    };
    auto fallback = c.get("set_intensity", "medium");
    return std::max(rank(c["postures"].get(a, fallback)), rank(c["postures"].get(b, fallback)));
}
bool fightingPair(const WorldState &w, const Json::Value &c, const std::string &a,
                  const std::string &b) {
    if (!isOpen(c) || a == b)
        return false;
    bool hostile = false;
    for (const auto &p : hostilePairs(w, c))
        if (isParty(p, a) && isParty(p, b) && p["status"] == "active")
            hostile = true;
    if (!hostile)
        return false;
    for (const auto &k : w.commitments)
        if (isOpen(k) && isParty(k, a) && isParty(k, b) &&
            (k["kind"] == "ceasefire" || k["kind"] == "peace_treaty" ||
             k["kind"] == "non_aggression_pact"))
            return false;
    return true;
}
bool fighting(const WorldState &w, const std::string &a, const std::string &b) {
    for (const auto &c : w.conflicts)
        if (fightingPair(w, c, a, b))
            return true;
    return false;
}
} // namespace lh
