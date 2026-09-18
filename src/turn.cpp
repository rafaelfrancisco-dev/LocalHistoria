#include "localhistoria/turn.h"
#include "localhistoria/politics.h"
#include "localhistoria/status.h"
#include <algorithm>
namespace lh {
const size_t maxAiActors = 4;
// Only a few chronicles per month, to bound turn time.
const unsigned maxChroniclesPerTurn = 3;
Mentions mentioned(const WorldState &w, const std::string &actor, const TurnOrders &orders) {
    Mentions m;
    std::string text;
    for (auto &o : orders) {
        text += lowercase(o.text) + "\n";
        for (auto &t : o.targets) {
            if (w.hasActor(t))
                m.actors.insert(t);
            else if (w.territories.isMember(t))
                m.regions.insert(t);
            else if (w.entities.isMember(t))
                m.regions.insert(w.entities[t]["region"].asString());
        }
    }
    for (auto &id : w.actors.getMemberNames()) {
        auto name = lowercase(w.actors[id]["name"].asString());
        if (name.size() >= 4 && containsWord(text, name))
            m.actors.insert(id);
    }
    std::map<std::string, std::vector<std::string>> byName;
    for (auto &id : w.territories.getMemberNames()) {
        auto name = lowercase(w.territories[id]["name"].asString());
        if (name.size() >= 4 && w.territories[id]["kind"] != "residual")
            byName[name].push_back(id);
    }
    for (auto &[name, ids] : byName) {
        if (!containsWord(text, name))
            continue;
        std::vector<std::string> preferred;
        for (auto &id : ids) {
            auto owner = w.territories[id]["owner"].asString(),
                 controller = w.territories[id]["controller"].asString();
            if (owner == actor || controller == actor || m.actors.contains(owner) ||
                m.actors.contains(controller))
                preferred.push_back(id);
        }
        auto &chosen = preferred.empty() ? ids : preferred;
        if (chosen.size() <= 3)
            m.regions.insert(chosen.begin(), chosen.end());
    }
    for (auto &id : m.regions)
        if (w.territories.isMember(id)) {
            auto owner = w.territories[id]["owner"].asString();
            if (w.hasActor(owner))
                m.actors.insert(owner);
        }
    m.actors.erase(actor);
    return m;
}
Json::Value adjudicationInput(const WorldState &w,
                              const std::map<std::string, CountryContext> &contexts,
                              const std::map<std::string, TurnOrders> &intentions,
                              const Adjacency &adjacency) {
    Json::Value input;
    input["facts"]["turn"] = w.turn;
    input["facts"]["actors"] = Json::objectValue;
    std::set<std::string> acting, relevant, grounded, targeted, fronts;
    for (auto &[a, o] : intentions) {
        acting.insert(a);
        auto m = mentioned(w, a, o);
        // The acting nation and the nations it names get recent history when the action is about
        // war, land or treaties.
        for (auto &action : o)
            if (consequential(action.text)) {
                grounded.insert(a);
                grounded.insert(m.actors.begin(), m.actors.end());
                break;
            }
        relevant.insert(m.regions.begin(), m.regions.end());
        targeted.insert(m.regions.begin(), m.regions.end());
        // Nations an actor addresses without naming a region: the parts of them it borders, where
        // an invasion or handover would begin.
        for (auto &target : m.actors)
            for (auto &id : w.territories.getMemberNames())
                if (w.territories[id]["controller"] == target &&
                    w.territories[id]["kind"] != "residual" && relevant.size() < 24 &&
                    reaches(w, adjacency, a, id))
                    relevant.insert(id);
    }
    for (auto &[a, c] : contexts) {
        bool fighting = false;
        for (auto &id : w.conflicts.getMemberNames())
            if (w.conflicts[id]["status"] == "open")
                for (auto &party : w.conflicts[id]["actors"])
                    fighting = fighting || party == a;
        // The player, nations at war and nations in a war, land or treaty action this month need
        // depth; the rest are context.
        bool central = a == w.player || fighting || grounded.contains(a);
        auto facts = c.facts;
        facts.removeMember("territories");
        facts["past_events"] = facts["recent_events"];
        facts.removeMember("recent_events");
        if (!central)
            for (auto k : {"entities", "regional_politics"})
                facts.removeMember(k);
        // Both lists are newest first. Decisions read all of related_history; adjudication only
        // needs the latest.
        for (auto [k, limit] : {std::pair{"related_history", central ? 8u : 4u},
                                std::pair{"past_events", central ? 4u : 2u}})
            if (facts[k].isArray() && facts[k].size() > limit)
                facts[k].resize(limit);
        input["facts"]["actors"][a] = facts;
        // Each decision already read its reference; adjudication mostly needs the facts, and a
        // shorter prompt is a faster month.
        if (central)
            input["reference_only"][a] =
                briefReference(c.reference, 600, c.reference["chronicle"].isString() ? 1200 : 600);
        for (auto k : {"occupied_by_others", "occupying"})
            for (auto &id : c.facts[k].getMemberNames())
                relevant.insert(id);
    }
    // Fronts of wars this month's actors fight; distant wars would crowd the prompt with irrelevant
    // regions.
    for (auto &id : w.conflicts.getMemberNames()) {
        auto &c = w.conflicts[id];
        bool involved = false;
        for (auto &a : c["actors"])
            involved = involved || acting.contains(a.asString());
        if (c["status"] == "open" && involved)
            for (auto &r : c["affected_regions"])
                if (r.isString() && relevant.size() < 36) {
                    relevant.insert(r.asString());
                    fronts.insert(r.asString());
                }
    }
    // Where the acting nations' armies and fleets stand, so movement has somewhere to go.
    for (auto &id : w.entities.getMemberNames()) {
        auto &e = w.entities[id];
        if (acting.contains(e["actor"].asString()) &&
            (e["kind"] == "army" || e["kind"] == "fleet") && e["status"] == "active")
            relevant.insert(e["region"].asString());
    }
    input["facts"]["territories"] = Json::objectValue;
    input["facts"]["entities"] = Json::objectValue;
    for (auto &id : relevant) {
        if (!w.territories.isMember(id))
            continue;
        auto t = w.territories[id];
        for (auto k :
             {"longitude", "latitude", "initial_affiliation", "kind", "gdp_usd", "occupation_gdp"})
            t.removeMember(k);
        for (auto &a : acting)
            if (reaches(w, adjacency, a, id))
                t["within_reach_of"].append(force::reachDescription(w, adjacency, a, id));
        input["facts"]["territories"][id] = t;
    }
    for (auto &id : w.entities.getMemberNames()) {
        auto region = w.entities[id]["region"].asString();
        if (!relevant.contains(region))
            continue;
        // Cities matter where an action or a front is; elsewhere they only lengthen the prompt.
        if (w.entities[id]["kind"] == "city" && !targeted.contains(region) &&
            !fronts.contains(region))
            continue;
        auto e = w.entities[id];
        for (auto k : {"longitude", "latitude", "origin_event"})
            e.removeMember(k);
        input["facts"]["entities"][id] = e;
    }
    // The player's actions come first so the model's bounded reasoning reaches them.
    input["intentions"] = Json::arrayValue;
    std::vector<std::string> order{w.player};
    for (auto &[a, o] : intentions)
        if (a != w.player)
            order.push_back(a);
    for (auto &a : order) {
        if (!intentions.contains(a))
            continue;
        for (auto &action : intentions.at(a)) {
            std::string targets;
            for (auto &t : action.targets) {
                auto name = w.territories.isMember(t)
                                ? w.territories[t]["name"].asString() + " (region " + t + ")"
                            : w.entities.isMember(t)
                                ? w.entities[t]["name"].asString() + " (entity " + t + ")"
                                : w.actors[t]["name"].asString() + " (actor " + t + ")";
                targets += (targets.empty() ? "" : ", ") + name;
            }
            Json::Value item;
            item["actor"] = a;
            item["action"] = action.text + (targets.empty() ? "" : " (targets: " + targets + ")") +
                             attackGuidance(a, action.text, input["facts"]["territories"]);
            input["intentions"].append(item);
        }
    }
    return input;
}
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
    auto engaged = [&](const Json::Value &v) {
        return v["status"] == "open" && std::find(v["actors"].begin(), v["actors"].end(),
                                                  Json::Value(w.player)) != v["actors"].end();
    };
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
                        if (k["status"] != "open" ||
                            (kind != "alliance" && kind != "defence_pact") ||
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
                auto d = parse(row["document"].asString());
                d["text"] = d["text"].asString() + droppedClause(d);
                c.facts["related_history"].append(d);
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
void TurnRunner::run(const TurnPlan &p, const std::string &id) {
    auto &[w, orders, actors, reactiveCounts, chronicleNations, allocations, refs, contexts,
           independentContexts] = p;
    auto &store = d.store;
    auto &provider = d.provider;
    auto &references = d.references;
    auto &mutex = d.mutex;
    auto &geography = d.geography;
    auto &adjacency = d.adjacency;
    bool committed = false;
    try {
        auto turnContexts = contexts;
        auto turnIndependent = independentContexts;
        auto turnRefs = refs;
        // Engaged nations ground their choices in a chronicle, written when
        // missing, a year old or source-less.
        std::set<std::string> chronicled;
        unsigned written = 0;
        for (auto &a : chronicleNations) {
            if (!chronicled.insert(a).second)
                continue;
            Json::Value current, input;
            {
                std::lock_guard l(mutex);
                current = store.chronicle(a);
                auto background = store.background(a);
                bool sourceAvailable = background["history_source"].isString();
                if (chronicleDue(current, w.turn, sourceAvailable) &&
                    written < maxChroniclesPerTurn) {
                    auto &facts = turnContexts.at(a).facts;
                    for (auto k : {"actor_name", "status", "conflicts", "commitments"})
                        input["facts"][k] = facts[k];
                    input["facts"]["date"] = campaignDate(w.turn);
                    // A chronicle written before the source was cached is
                    // rebuilt from it, not built upon.
                    bool first = !current["summary"].isString() ||
                                 (sourceAvailable && !current.get("with_source", false).asBool());
                    if (!first)
                        input["previous_chronicle"] = current["summary"];
                    input["campaign_events"] =
                        store.chronicleEvents(a, first ? 0 : current["through_turn"].asInt());
                    // Later rewrites build on the previous chronicle instead of
                    // the source.
                    if (first && sourceAvailable)
                        input["reference_only"]["history_source"] = background["history_source"];
                    input["reference_only"]["title"] = background["title"];
                }
            }
            if (!input.isNull()) {
                ++written;
                try {
                    auto summary = provider.chronicle(input, a);
                    Json::Value document;
                    document["summary"] = summary;
                    document["through_turn"] = w.turn;
                    document["with_source"] = input["reference_only"].isMember("history_source") ||
                                              current.get("with_source", false).asBool();
                    document["events_covered"] = input["campaign_events"].size();
                    std::lock_guard l(mutex);
                    document["source_revision"] = store.background(a)["revision"];
                    store.saveChronicle(a, document);
                    store.recordInference(id, provider.takeAudit());
                    current = document;
                } catch (const std::exception &) {
                    // Optional like narration: the nation keeps its earlier
                    // chronicle or the raw excerpt.
                    std::lock_guard l(mutex);
                    store.recordInference(id, provider.takeAudit());
                }
            }
            if (!current["summary"].isString())
                continue;
            turnContexts.at(a).reference["chronicle"] = current["summary"];
            turnRefs[a]["chronicle"] = current["summary"];
            for (Json::ArrayIndex n = 0; n < allocations.size(); ++n)
                if (allocations[n]["actor"] == a && allocations[n]["region"] == "")
                    turnIndependent[n].reference["chronicle"] = current["summary"];
        }
        std::map<std::string, TurnOrders> intentions{{w.player, orders}};
        for (auto &a : actors) {
            auto c = turnContexts.at(a);
            c.facts["initiative_type"] = "reactive";
            c.facts["action_count"] = reactiveCounts.at(a);
            c.facts["player_actions"] = encode(orders);
            intentions[a] = provider.decide(w, c).orders;
            // Flushed as it lands so the client can show it before the month is
            // adjudicated.
            {
                std::lock_guard l(mutex);
                store.recordInference(id, provider.takeAudit());
            }
        }
        Json::Value independent(Json::arrayValue);
        for (Json::ArrayIndex n = 0; n < allocations.size(); ++n) {
            auto slot = allocations[n];
            auto a = slot["actor"].asString(), r = slot["region"].asString();
            auto decision = provider.decide(w, turnIndependent[n]);
            {
                std::lock_guard l(mutex);
                store.recordInference(id, provider.takeAudit());
            }
            for (auto &o : decision.orders) {
                slot["action"] = o.text;
                if (!r.empty()) {
                    o.targets.push_back(r);
                    slot["regional_actor"] = r;
                }
                slot["actor_action_index"] = Json::UInt(intentions[a].size());
                intentions[a].push_back(o);
                independent.append(slot);
            }
        }
        auto result = resolve(w);
        auto adjudication = adjudicationInput(result.state, turnContexts, intentions, adjacency);
        // Attach trusted slot attribution by matching each appended action
        // once.
        for (auto &slot : independent) {
            unsigned index = 0;
            for (auto &i : adjudication["intentions"])
                if (i["actor"] == slot["actor"] && index++ == slot["actor_action_index"].asUInt()) {
                    i["initiative_type"] = slot["initiative_type"];
                    if (slot.isMember("regional_actor"))
                        i["regional_actor"] = slot["regional_actor"];
                    break;
                }
        }
        adjudication["facts"]["regional_politics"] = Json::objectValue;
        for (auto &slot : allocations)
            if (slot["region"] != "") {
                auto r = slot["region"].asString();
                adjudication["facts"]["regional_politics"][r] = regionalState(w, r);
            }
        auto delta = provider.adjudicate(result.state, adjudication, adjacency,
                                         turnRefs.get("proposal_responses", Json::arrayValue));
        {
            std::lock_guard l(mutex);
            store.recordInference(id, provider.takeAudit());
        }
        result = applyDelta(result, delta, geography);
        result = applyMonthlyStatus(result, delta, adjacency);
        auto frozen = turnRefs;
        frozen["delta"] = encode(delta);
        frozen["contexts"] = adjudication;
        Json::Value recorded;
        for (auto &[a, o] : intentions)
            recorded[a] = encode(o);
        {
            std::lock_guard l(mutex);
            store.recordInference(id, provider.takeAudit());
            store.commit(id, result, recorded, frozen, false);
            committed = true;
            references.prepare(result.state);
        }
        try {
            CountryContext narrationContext;
            {
                std::lock_guard l(mutex);
                narrationContext = store.campaignContext(result.state, result.state.player);
            }
            auto text = provider.narrate(result.events, narrationContext);
            std::lock_guard l(mutex);
            store.narrate(id, text, false);
            store.recordInference(id, provider.takeAudit());
        } catch (...) {
            std::lock_guard l(mutex);
            store.recordInference(id, provider.takeAudit());
        }
    } catch (const std::exception &e) {
        if (!committed) {
            std::lock_guard l(mutex);
            store.recordInference(id, provider.takeAudit());
            store.execute("UPDATE jobs SET status='failed',error=? WHERE id=?", {e.what(), id});
        }
    }
}
} // namespace lh
