#include "localhistoria/conflicts.h"
#include <algorithm>
#include <deque>
#include <map>
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
bool mayEnter(const WorldState &w, const std::string &a, const std::string &region) {
    const auto &t = w.territories[region];
    auto holder = t["controller"].asString();
    if (holder.empty() || holder == a)
        return true;
    for (const auto &k : w.commitments)
        if (isOpen(k) && isParty(k, a) && isParty(k, holder))
            for (const auto &term : k["terms"])
                if (term["action"] == "grant_access" && term["to"] == a &&
                    (!term.isMember("region") || term["region"] == region))
                    return true;
    return false;
}
std::string heartland(const WorldState &w, const std::string &a) {
    std::string best;
    Json::Int64 people = -1;
    for (const auto &id : w.territories.getMemberNames()) {
        const auto &t = w.territories[id];
        if (t["owner"] != a || t["controller"] != a || t["kind"] == "residual")
            continue;
        auto n = t.get("population", 0).asInt64();
        if (n > people) {
            best = id;
            people = n;
        }
    }
    return best;
}
bool supplied(const WorldState &w, const Adjacency &adj, const std::string &a,
              const std::string &region) {
    if (!w.territories.isMember(region) || !mayEnter(w, a, region))
        return false;
    auto home = heartland(w, a);
    if (home.empty())
        return true;
    // mayEnter, with the treaties read once: a basin can open well over a thousand coasts.
    std::set<std::string> hosts;
    std::map<std::string, std::set<std::string>> hostsHere;
    for (const auto &k : w.commitments)
        if (isOpen(k) && isParty(k, a))
            for (const auto &term : k["terms"])
                if (term["action"] == "grant_access" && term["to"] == a)
                    for (const auto &p : k["actors"])
                        (term.isMember("region") ? hostsHere[term["region"].asString()] : hosts)
                            .insert(p.asString());
    auto enterable = [&](const Json::Value &t, const std::string &id) {
        auto holder = t["controller"].asString();
        auto here = hostsHere.find(id);
        return holder.empty() || holder == a || hosts.contains(holder) ||
               (here != hostsHere.end() && here->second.contains(holder));
    };
    std::set<std::string> seen{region};
    std::set<int> crossed;
    std::deque<std::string> queue{region};
    auto visit = [&](const std::string &n) {
        if (!w.territories.isMember(n))
            return;
        const auto &t = w.territories[n];
        if (t["kind"] != "residual" && enterable(t, n) && seen.insert(n).second)
            queue.push_back(n);
    };
    while (!queue.empty()) {
        auto id = queue.front();
        queue.pop_front();
        if (id == home)
            return true;
        if (auto at = adj.find(id); at != adj.end())
            for (const auto &n : at->second)
                visit(n);
        if (auto at = adj.sea.find(id); at != adj.sea.end())
            for (const auto &[n, km] : at->second)
                visit(n);
        // A coast opens every other coast on its waters it may use, once per body of water.
        if (auto at = adj.basins.find(id); at != adj.basins.end())
            for (int b : at->second)
                if (crossed.insert(b).second)
                    for (const auto &n : adj.basinRegions.at(b))
                        visit(n);
    }
    return false;
}
bool fighting(const WorldState &w, const std::string &a, const std::string &b) {
    for (const auto &c : w.conflicts)
        if (fightingPair(w, c, a, b))
            return true;
    return false;
}
} // namespace lh
