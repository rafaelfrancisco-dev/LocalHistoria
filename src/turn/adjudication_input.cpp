#include "localhistoria/politics.h"
#include "localhistoria/status.h"
#include "localhistoria/turn.h"
#include <algorithm>
#include <chrono>
namespace lh {
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
            if (isOpen(w.conflicts[id]))
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
        if (isOpen(c) && involved)
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
        for (auto k : {"longitude", "latitude", "initial_affiliation", "kind", "gdp_usd",
                       "occupation_gdp", "control", "displacement"})
            t.removeMember(k);
        for (auto &a : acting)
            if (reaches(w, adjacency, a, id))
                t["within_reach_of"].append(force::reachDescription(w, adjacency, a, id));
        input["facts"]["territories"][id] = t;
    }
    // What each acting nation could actually take this month, read off the map rather than off the
    // wording of its order. Nothing here depends on how an action is phrased.
    for (auto &a : acting) {
        if (!input["facts"]["actors"].isMember(a))
            continue;
        Json::Value regions(Json::arrayValue);
        // Without an inventory there is no strength to weigh, and take() lets the occupation
        // through on reach alone; the facts must say the same.
        bool weighed =
            static_cast<const Json::Value &>(w.actors)[a]["status"]["military"].isObject();
        int most = 0;
        for (auto &id : input["facts"]["territories"].getMemberNames()) {
            auto controller = w.territories[id]["controller"].asString();
            if (controller.empty())
                controller = w.territories[id]["owner"].asString();
            if (controller == a || !reaches(w, adjacency, a, id))
                continue;
            int capacity = weighed ? force::assaultCapacity(w, adjacency, a, controller, id) : 2;
            if (capacity < 1)
                continue;
            regions.append(id);
            most = std::max(most, capacity);
        }
        if (!regions.empty()) {
            input["facts"]["actors"][a]["offensive"]["regions"] = regions;
            input["facts"]["actors"][a]["offensive"]["can_take_this_month"] = most;
        }
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
        for (auto k : {"longitude", "latitude", "origin_event", "arrears", "unfunded"})
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
            item["action"] = action.text + (targets.empty() ? "" : " (targets: " + targets + ")");
            input["intentions"].append(item);
        }
    }
    return input;
}
} // namespace lh
