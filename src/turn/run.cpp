#include "localhistoria/politics.h"
#include "localhistoria/status.h"
#include "localhistoria/turn.h"
#include <algorithm>
#include <chrono>
namespace lh {
// Only a few chronicles per month, to bound turn time.
const unsigned maxChroniclesPerTurn = 3;
void TurnRunner::run(const TurnPlan &p, const std::string &id) {
    auto &[w, orders, actors, reactiveCounts, chronicleNations, allocations, refs, contexts,
           independentContexts, narrate] = p;
    auto &store = d.store;
    auto &provider = d.provider;
    auto &references = d.references;
    auto &mutex = d.mutex;
    auto &geography = d.geography;
    auto &adjacency = d.adjacency;
    bool committed = false;
    {
        std::lock_guard l(mutex);
        if (cancelRequests.erase(id)) {
            store.execute("UPDATE jobs SET status='failed',error=? WHERE id=?",
                          {"Cancelled by the player", id});
            return;
        }
        current = id;
        provider.clearCancel();
        provider.setDeadline(std::chrono::steady_clock::now() +
                             std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                 std::chrono::duration<double>(d.budgetSeconds)));
    }
    struct Finish {
        TurnRunner &t;
        const std::string &id;
        ~Finish() {
            std::lock_guard l(t.d.mutex);
            t.current.clear();
            t.cancelRequests.erase(id);
            t.d.provider.clearCancel();
            t.d.provider.clearDeadline();
        }
    } finish{*this, id};
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
            Json::Value previous, input;
            {
                std::lock_guard l(mutex);
                previous = store.chronicle(a);
                auto background = store.background(a);
                bool sourceAvailable = background["history_source"].isString();
                if (chronicleDue(previous, w.turn, sourceAvailable) &&
                    written < maxChroniclesPerTurn) {
                    auto &facts = turnContexts.at(a).facts;
                    for (auto k : {"actor_name", "status", "conflicts", "commitments"})
                        input["facts"][k] = facts[k];
                    input["facts"]["date"] = campaignDate(w.turn);
                    // A chronicle written before the source was cached is
                    // rebuilt from it, not built upon.
                    bool first = !previous["summary"].isString() ||
                                 (sourceAvailable && !previous.get("with_source", false).asBool());
                    if (!first)
                        input["previous_chronicle"] = previous["summary"];
                    input["campaign_events"] =
                        store.chronicleEvents(a, first ? 0 : previous["through_turn"].asInt());
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
                                              previous.get("with_source", false).asBool();
                    document["events_covered"] = input["campaign_events"].size();
                    std::lock_guard l(mutex);
                    document["source_revision"] = store.background(a)["revision"];
                    store.saveChronicle(a, document);
                    store.recordInference(id, provider.takeAudit());
                    previous = document;
                } catch (const std::exception &) {
                    // Optional like narration: the nation keeps its earlier
                    // chronicle or the raw excerpt.
                    std::lock_guard l(mutex);
                    store.recordInference(id, provider.takeAudit());
                }
            }
            if (!previous["summary"].isString())
                continue;
            turnContexts.at(a).reference["chronicle"] = previous["summary"];
            turnRefs[a]["chronicle"] = previous["summary"];
            for (Json::ArrayIndex n = 0; n < allocations.size(); ++n)
                if (allocations[n]["actor"] == a && allocations[n]["region"] == "")
                    turnIndependent[n].reference["chronicle"] = previous["summary"];
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
            // Checked under the lock the cancel route takes, so an accepted cancel never commits.
            if (cancelRequests.contains(id))
                throw ValidationError("Cancelled by the player");
            store.commit(id, result, recorded, frozen, false);
            committed = true;
            references.prepare(result.state);
        }
        if (narrate)
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
void TurnRunner::cancel(const std::string &id) {
    cancelRequests.insert(id);
    if (id == current)
        d.provider.requestCancel();
}
} // namespace lh
