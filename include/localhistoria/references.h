#pragma once
#include "providers.h"
#include "store.h"
#include <ctime>
#include <memory>
namespace lh {
class Worker;
// Fetches and caches the reference histories a campaign's decisions will read, on its own bounded
// queue. Every method is called under the shared mutex; queued work takes it itself.
class ReferencePreparer {
    Store &store, &reference;
    std::mutex &mutex;
    Transport transport;
    std::string greatPowersCampaign;
    std::set<std::string> greatPowers;
    std::set<std::string> preparing;
    std::map<std::string, std::time_t> retryAfter;
    // Refresh and preparation jobs, polled through /api/v1/jobs.
    std::map<std::string, Json::Value> auxiliary;
    unsigned auxiliaryId = 0;
    std::unique_ptr<Worker> worker;

  public:
    ReferencePreparer(Store &store, Store &reference, std::mutex &mutex, Transport transport);
    ~ReferencePreparer();
    // Closeness to the player for the first independent slot: neighbours and the player itself (for
    // regional slots) 3, war and small-pact partners 2, fellow members of large blocs 1. Great
    // powers, the twelve largest economies, fill the second slot.
    std::pair<std::map<std::string, int>, std::set<std::string>> relevance(const WorldState &);
    // One bounded queue, deduplicated, with failures backed off.
    void prepare(const WorldState &);
    // Queues a fresh fetch of one country's history; returns the running job.
    Json::Value refresh(const std::string &code);
    // The player, its neighbours and its small-pact and war partners.
    std::set<std::string> engagedActors(const WorldState &);
    Json::Value coverage(const std::set<std::string> &actors);
    // Queues a fetch of every actor's history; returns the running job.
    Json::Value prepareAll(const std::set<std::string> &actors);
    // A refresh or preparation job, null when the id is not one.
    Json::Value job(const std::string &id) const;
    // Stops the queue, dropping work not yet started.
    void finish();
};
} // namespace lh
