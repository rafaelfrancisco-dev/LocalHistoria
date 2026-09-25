#include "localhistoria/lifecycle.h"
#include "localhistoria/politics.h"
#include "localhistoria/status.h"
#include "localhistoria/turn.h"
#include <algorithm>
#include <chrono>
#include <trantor/utils/Logger.h>
namespace lh {
void startSubmitted(Store &store, const std::string &id, const std::function<void()> &prepareAndQueue) {
    try {
        prepareAndQueue();
    } catch (const std::exception &e) {
        try {
            store.execute("UPDATE jobs SET status='failed',error=? WHERE id=? AND status='running'",
                          {e.what(), id});
        } catch (const std::exception &cleanup) {
            // Store::recover() marks the job interrupted at the next start.
            LOG_ERROR << "Could not fail job " << id << ": " << cleanup.what();
        }
        throw;
    }
}
// Only a few chronicles per month, to bound turn time.
const unsigned maxChroniclesPerTurn = 3;
void TurnRunner::run(const TurnPlan &p, const std::string &id) {
    auto &[w, orders, actors, reactiveCounts, chronicleNations, allocations, refs, contexts,
           independentContexts, narrate, submitted, preparationMs] = p;
    auto &store = d.store;
    auto &provider = d.provider;
    auto &references = d.references;
    auto &mutex = d.mutex;
    auto &geography = d.geography;
    auto &adjacency = d.adjacency;
    bool committed = false;
    InferenceRequest request;
    request.experiment = d.experiment;
    request.deadline = submitted + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(d.budgetSeconds));
    const auto runStarted = std::chrono::steady_clock::now();
    auto ms = [](auto from) { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - from).count(); };
    Json::Value timing;
    timing["kind"] = "turn_timing";
    timing["context_preparation_ms"] = preparationMs;
    timing["queue_ms"] = std::max(0.0, std::chrono::duration<double, std::milli>(runStarted - submitted).count() - preparationMs);
    auto checkpoint = runStarted;
    auto stage = [&](const char *name) { timing[name] = ms(checkpoint); checkpoint = std::chrono::steady_clock::now(); };
    {
        std::lock_guard l(mutex);
        if (cancelRequests.erase(id)) {
            store.execute("UPDATE jobs SET status='failed',error=? WHERE id=?",
                          {"Cancelled by the player", id});
            return;
        }
        current = id;
        cancellation = request.cancelled;
    }
    struct Finish {
        TurnRunner &t;
        const std::string &id;
        ~Finish() {
            std::lock_guard l(t.d.mutex);
            t.current.clear();
            t.cancelRequests.erase(id);
            t.cancellation.reset();
        }
    } finish{*this, id};
    try {
        auto turnContexts = contexts;
        auto turnIndependent = independentContexts;
        auto turnRefs = refs;
        if (request.experiment == "compact") turnRefs["event_text_version"] = 3;
        // Engaged nations ground their choices in a chronicle, written when
        // missing, a year old or source-less.
        std::set<std::string> chronicled;
        unsigned written = 0;
        const auto chronicleEnd = runStarted + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(std::min(60.0, .15 * d.budgetSeconds)));
        const auto reserveEnd = request.deadline - std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(d.mandatoryReserveSeconds));
        for (auto &a : chronicleNations) {
            if (!chronicled.insert(a).second)
                continue;
            auto previous = refs["chronicle_snapshots"][a];
            Json::Value input;
            std::string refresh = "cached";
            if (refs["chronicle_inputs"][a].isObject()) {
                refresh = d.mandatoryReserveSeconds > 0 ? "skipped_allowance" : "skipped_no_reserve";
                if (written >= maxChroniclesPerTurn) refresh = "skipped_limit";
                else if (d.mandatoryReserveSeconds > 0 &&
                         std::chrono::steady_clock::now() < std::min(chronicleEnd, reserveEnd))
                    input = refs["chronicle_inputs"][a];
            }
            if (!input.isNull()) {
                ++written;
                InferenceRequest optional;
                optional.cancelled = request.cancelled;
                optional.deadline = std::min(chronicleEnd, reserveEnd);
                try {
                    auto summary = provider.chronicle(input, a, &optional);
                    refresh = "refreshed";
                    Json::Value document;
                    document["summary"] = summary;
                    document["revision"] = previous.get("revision", 0).asInt() + 1;
                    document["through_turn"] = w.turn;
                    document["with_source"] = optional.audit[optional.audit.size() - 1]["input"]["reference_only"].isMember("history_source") ||
                                              previous.get("with_source", false).asBool();
                    document["events_covered"] = optional.audit[optional.audit.size() - 1]["input"]["campaign_events"].size();
                    std::lock_guard l(mutex);
                    document["source_revision"] = input["reference_only"]["revision"];
                    store.saveChronicle(a, document);
                    store.recordInference(id, optional.takeAudit());
                    previous = document;
                } catch (const std::exception &) {
                    refresh = "failed";
                    // Optional like narration: the nation keeps its earlier
                    // chronicle or the raw excerpt.
                    std::lock_guard l(mutex);
                    store.recordInference(id, optional.takeAudit());
                }
            }
            Json::Value provenance;
            provenance["refresh"] = refresh;
            provenance["revision"] = previous["revision"];
            provenance["through_turn"] = previous["through_turn"];
            turnRefs["chronicle_usage"][a] = provenance;
            turnContexts.at(a).facts["chronicle_used"] = provenance;
            for (Json::ArrayIndex n = 0; n < allocations.size(); ++n)
                if (allocations[n]["actor"] == a && allocations[n]["region"] == "")
                    turnIndependent[n].facts["chronicle_used"] = provenance;
            if (!previous["summary"].isString())
                continue;
            turnContexts.at(a).reference["chronicle"] = previous["summary"];
            turnRefs[a]["chronicle"] = previous["summary"];
            for (Json::ArrayIndex n = 0; n < allocations.size(); ++n)
                if (allocations[n]["actor"] == a && allocations[n]["region"] == "")
                    turnIndependent[n].reference["chronicle"] = previous["summary"];
        }
        stage("chronicles_ms");
        std::map<std::string, TurnOrders> intentions{{w.player, orders}};
        for (auto &a : actors) {
            auto c = turnContexts.at(a);
            c.facts["initiative_type"] = "reactive";
            c.facts["action_count"] = reactiveCounts.at(a);
            c.facts["player_actions"] = encode(orders);
            auto decision = provider.decide(w, c, request);
            intentions[a] = decision.orders;
            for (const auto &answer : decision.responses)
                turnRefs["proposal_responses"].append(answer);
            // Flushed as it lands so the client can show it before the month is
            // adjudicated.
            {
                std::lock_guard l(mutex);
                store.recordInference(id, request.takeAudit());
            }
        }
        Json::Value independent(Json::arrayValue);
        for (Json::ArrayIndex n = 0; n < allocations.size(); ++n) {
            auto slot = allocations[n];
            auto a = slot["actor"].asString(), r = slot["region"].asString();
            auto decision = provider.decide(w, turnIndependent[n], request);
            for (const auto &answer : decision.responses)
                turnRefs["proposal_responses"].append(answer);
            {
                std::lock_guard l(mutex);
                store.recordInference(id, request.takeAudit());
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
        stage("decisions_ms");
        auto result = beginTurn(w, turnRefs);
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
        stage("adjudication_preparation_ms");
        auto delta = provider.adjudicate(result.state, adjudication, adjacency,
                                         turnRefs.get("proposal_responses", Json::arrayValue), &request);
        stage("adjudication_ms");
        {
            std::lock_guard l(mutex);
            store.recordInference(id, request.takeAudit());
        }
        result = finishTurn(result, delta, adjacency, geography, turnRefs);
        auto frozen = turnRefs;
        frozen["delta"] = encode(delta);
        frozen["contexts"] = adjudication;
        Json::Value recorded;
        for (auto &[a, o] : intentions)
            recorded[a] = encode(o);
        stage("simulation_ms");
        {
            std::lock_guard l(mutex);
            store.recordInference(id, request.takeAudit());
            // Checked under the lock the cancel route takes, so an accepted cancel never commits.
            if (cancelRequests.contains(id))
                throw ValidationError("Cancelled by the player");
            store.commit(id, result, recorded, frozen, false);
            committed = true;
            stage("commit_ms");
            timing["submission_to_commit_ms"] = ms(submitted);
            references.prepare(result.state);
        }
        if (narrate)
            try {
                CountryContext narrationContext;
                {
                    std::lock_guard l(mutex);
                    narrationContext = store.campaignContext(result.state, result.state.player);
                }
                auto text = provider.narrate(result.events, narrationContext, &request);
                std::lock_guard l(mutex);
                store.narrate(id, text, false);
                store.recordInference(id, request.takeAudit());
            } catch (...) {
                std::lock_guard l(mutex);
                store.recordInference(id, request.takeAudit());
            }
    } catch (const std::exception &e) {
        if (!committed) {
            std::lock_guard l(mutex);
            store.recordInference(id, request.takeAudit());
            if (stopping)
                store.execute("UPDATE jobs SET status='interrupted',error=? WHERE id=?",
                              {"Interrupted before commit; submit again with a new key", id});
            else
                store.execute("UPDATE jobs SET status='failed',error=? WHERE id=?", {e.what(), id});
        }
    }
    stage("post_commit_or_failure_ms");
    timing["submission_to_finished_ms"] = ms(submitted);
    timing["status"] = committed ? "complete" : "failed";
    Json::Value records(Json::arrayValue);
    records.append(timing);
    std::lock_guard l(mutex);
    store.recordInference(id, records);
}
void TurnRunner::cancel(const std::string &id) {
    cancelRequests.insert(id);
    if (id == current)
        if (cancellation) cancellation->cancel();
}
void TurnRunner::interrupt() {
    stopping = true;
    if (!current.empty())
        cancel(current);
}
} // namespace lh
