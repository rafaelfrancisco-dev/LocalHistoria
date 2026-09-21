#include "localhistoria/politics.h"
#include "localhistoria/status.h"
#include "localhistoria/turn.h"
#include <algorithm>
#include <chrono>
namespace lh {
const size_t maxAiActors = 4;
TurnPlan TurnRunner::plan(const WorldState &w, const TurnOrders &orders) {
    auto &store = d.store;
    auto &reference = d.reference;
    auto &references = d.references;
    Json::Value refs(Json::objectValue);
    auto mentions = mentioned(w, w.player, orders);
    // AI nations that act this month: those the player addresses first, then ongoing
    // disputes and one rotating neighbor.
    std::vector<std::string> candidates(mentions.actors.begin(), mentions.actors.end());
    // A nation whose treaty offer is answered this month reacts to the answer.
    for (auto &p : w.proposals)
        if (p["status"] == "pending")
            candidates.insert(candidates.begin(), p["from"].asString());
    // Large blocs (NATO, the EU) would otherwise fill every month with the same
    // alphabetical members.
    auto engaged = [&](const Json::Value &v) { return isOpen(v) && isParty(v, w.player); };
    for (auto &v : w.conflicts)
        if (engaged(v))
            for (auto &a : v["actors"])
                candidates.push_back(a.asString());
    // An opponent's allies weigh in: every partner in a small pact, the largest economy
    // of a bloc.
    for (auto &v : w.conflicts)
        if (engaged(v))
            for (auto &enemy : v["actors"])
                if (enemy != w.player)
                    for (auto &k : w.commitments) {
                        auto kind = k["kind"].asString();
                        auto &members = k["actors"];
                        if (!isOpen(k) || (kind != "alliance" && kind != "defence_pact") ||
                            std::find(members.begin(), members.end(), enemy) == members.end() ||
                            std::find(members.begin(), members.end(), Json::Value(w.player)) !=
                                members.end())
                            continue;
                        std::string largest;
                        double best = -1;
                        for (auto &a : members)
                            if (a != enemy && w.hasActor(a.asString())) {
                                if (members.size() <= 3) {
                                    candidates.push_back(a.asString());
                                    continue;
                                }
                                double gdp = nationalStatus(w, a.asString())["gdp_usd"].asDouble();
                                if (gdp > best) {
                                    best = gdp;
                                    largest = a.asString();
                                }
                            }
                        if (!largest.empty())
                            candidates.push_back(largest);
                    }
    for (auto &v : w.commitments)
        if (engaged(v) && v["actors"].size() <= 3)
            for (auto &a : v["actors"])
                candidates.push_back(a.asString());
    auto neighbors = reference.query("SELECT b FROM neighbors WHERE a=? ORDER BY b", {w.player});
    if (!neighbors.empty())
        candidates.push_back(neighbors[w.turn % neighbors.size()]["b"].asString());
    if (neighbors.empty() && w.actors.size() > 1) {
        auto names = w.actors.getMemberNames();
        candidates.push_back(names[w.turn % names.size()]);
    }
    std::set<std::string> actors;
    for (auto &a : candidates)
        if (a != w.player && w.hasActor(a) && actors.size() < maxAiActors)
            actors.insert(a);
    actors.erase(w.player);
    // One action each; two for nations the player named or is fighting, the only ones
    // whose month usually matters.
    std::map<std::string, int> reactiveCounts;
    for (auto &a : actors) {
        int count = 1;
        if (mentions.actors.contains(a))
            count = 2;
        for (auto &v : w.conflicts)
            if (engaged(v) && std::find(v["actors"].begin(), v["actors"].end(), Json::Value(a)) !=
                                  v["actors"].end())
                count = 2;
        reactiveCounts[a] = count;
    }
    // Chronicles only for nations whose history shapes the player's month: those at war
    // or in a small pact with the player. Named or independent actors keep the excerpt,
    // or an earlier chronicle.
    std::vector<std::string> chronicleNations{w.player};
    for (auto &a : actors) {
        bool bound = false;
        for (auto collection : {&w.conflicts, &w.commitments})
            for (auto &v : *collection)
                if (engaged(v) && (collection == &w.conflicts || v["actors"].size() <= 3) &&
                    std::find(v["actors"].begin(), v["actors"].end(), Json::Value(a)) !=
                        v["actors"].end())
                    bound = true;
        if (bound)
            chronicleNations.push_back(a);
    }
    auto [closeness, powers] = references.relevance(w);
    auto allocations = scheduleInitiatives(w, actors, closeness, powers);
    auto prior = store.query("SELECT slots FROM monthly_allocations WHERE campaign=? AND month=?",
                             {w.campaignId, std::to_string(w.turn + 1)});
    if (!prior.empty())
        allocations = parse(prior[0]["slots"].asString());
    else
        store.execute("INSERT INTO monthly_allocations VALUES(?,?,?)",
                      {w.campaignId, std::to_string(w.turn + 1), dump(allocations)});
    refs["allocations"] = allocations;
    refs[w.player] = store.background(w.player);
    for (auto &a : actors)
        refs[a] = store.background(a);
    std::map<std::string, CountryContext> contexts;
    contexts[w.player] = store.campaignContext(w, w.player);
    for (auto &a : actors)
        contexts[a] = store.campaignContext(w, a);
    std::vector<CountryContext> independentContexts;
    for (auto &slot : allocations) {
        auto a = slot["actor"].asString(), r = slot["region"].asString();
        auto c = store.campaignContext(w, a);
        contexts[a] = c;
        c.facts["initiative_type"] = slot["initiative_type"];
        c.facts["action_count"] = 1;
        if (!r.empty()) {
            // No national military inventory or player orders in a regional decision.
            c.facts = Json::objectValue;
            c.facts["initiative_type"] = slot["initiative_type"];
            c.facts["action_count"] = 1;
            c.facts["turn"] = w.turn;
            c.facts["regional_identity"] = w.territories[r];
            c.facts["regional_identity"]["id"] = r;
            for (auto k : {"gdp_usd", "occupation_gdp"})
                c.facts["regional_identity"].removeMember(k);
            c.facts["governing_authority"] = w.actors[a];
            c.facts["governing_authority"].removeMember("identity_revisions");
            c.facts["governing_authority"]["status"] = nationalStatus(w, a);
            c.facts["politics"] = regionalState(w, r);
            c.facts["related_history"] = Json::arrayValue;
            for (auto &row :
                 store.query("SELECT document FROM events WHERE id IN (SELECT event FROM "
                             "event_links WHERE kind='regions' AND target=?) AND " +
                                 Store::happened + " ORDER BY turn DESC LIMIT 12",
                             {r})) {
                auto event = parse(row["document"].asString());
                event["text"] = event["text"].asString() + droppedClause(event);
                c.facts["related_history"].append(event);
            }
            c.reference = store.background(r);
            refs[r] = c.reference;
        }
        refs[a] = store.background(a);
        independentContexts.push_back(c);
    }
    // The long history source feeds chronicles only; frozen turns keep the bounded
    // excerpts.
    for (auto &k : refs.getMemberNames())
        if (refs[k].isObject())
            refs[k].removeMember("history_source");
    return {w,           orders, actors,   reactiveCounts,     chronicleNations,
            allocations, refs,   contexts, independentContexts};
}
} // namespace lh
