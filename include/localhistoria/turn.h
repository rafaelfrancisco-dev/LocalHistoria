#pragma once
#include "providers.h"
#include "references.h"
#include "store.h"
#include "world.h"
namespace lh {
struct Mentions {
    std::set<std::string> actors, regions;
};
// Free text names nations and regions; resolve those names to IDs so the model sees the relevant
// map.
Mentions mentioned(const WorldState &, const std::string &actor, const TurnOrders &);
// What the adjudication prompt reads: facts of every actor with a context, the regions and entities
// this month's actions concern, and the intentions, player first.
Json::Value adjudicationInput(const WorldState &,
                              const std::map<std::string, CountryContext> &contexts,
                              const std::map<std::string, TurnOrders> &intentions,
                              const Adjacency &);
// A month's inputs, snapshotted under the mutex when the turn is submitted.
struct TurnPlan {
    WorldState world;
    TurnOrders orders;
    // AI nations reacting to the player, with their action counts.
    std::set<std::string> actors;
    std::map<std::string, int> reactiveCounts;
    // Nations whose chronicle may be written before their decision.
    std::vector<std::string> chronicleNations;
    Json::Value allocations, refs;
    std::map<std::string, CountryContext> contexts;
    std::vector<CountryContext> independentContexts;
    // The player can skip the optional model narration; the factual summary stays.
    bool narrate = true;
    std::chrono::steady_clock::time_point submitted = std::chrono::steady_clock::now();
    double preparationMs = 0;
};
struct TurnDeps {
    Store &store;
    Store &reference;
    LocalProvider &provider;
    ReferencePreparer &references;
    const Json::Value &geography;
    const Adjacency &adjacency;
    std::mutex &mutex;
    // Total time a month's model calls may take, from the start of run().
    double budgetSeconds = 480;
    // Measured baseline p90 mandatory work, zero disables chronicle refresh.
    double mandatoryReserveSeconds = 0;
    std::string experiment;
};
class TurnRunner {
    TurnDeps d;
    // Guarded by the mutex: the job in run(), and jobs the player cancelled.
    std::string current;
    std::shared_ptr<std::atomic<bool>> cancellation;
    std::set<std::string> cancelRequests;

  public:
    explicit TurnRunner(TurnDeps deps) : d(deps) {}
    // Called under mutex: picks the month's AI nations and independent slots and snapshots their
    // contexts and references.
    TurnPlan plan(const WorldState &, const TurnOrders &);
    // Chronicles, decisions, adjudication, commit and narration; takes the mutex only around store
    // access, so inference never holds it.
    void run(const TurnPlan &, const std::string &jobId);
    // Called under mutex for a running job: it fails before commit with "Cancelled by the player";
    // after commit only narration is skipped.
    void cancel(const std::string &jobId);
};
} // namespace lh
