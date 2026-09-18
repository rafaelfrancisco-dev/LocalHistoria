#pragma once
#include "world.h"
#include <chrono>
#include <functional>
#include <mutex>
namespace lh {
using Transport = std::function<Json::Value(const std::string &, const std::string &,
                                            const Json::Value &, double)>;
Json::Value httpJson(const std::string &, const std::string &, const Json::Value &, double);
struct Decision {
    TurnOrders orders;
    std::string status;
};
class DecisionProvider {
  public:
    virtual ~DecisionProvider() = default;
    virtual Decision decide(const WorldState &, const CountryContext &) = 0;
};
class NarrationProvider {
  public:
    virtual ~NarrationProvider() = default;
    virtual std::string narrate(const std::vector<ResolvedEvent> &) = 0;
};
std::string validateEndpoint(const std::string &);
class LocalProvider : public DecisionProvider, public NarrationProvider {
    std::string endpoint;
    double timeout;
    Transport transport;
    mutable std::mutex inference, settings;
    Json::Value audit{Json::arrayValue};
    // Share of the context below which a byte estimate replaces exact tokenization; 0 always
    // tokenizes.
    double estimateShare;
    // /props, reused for a minute per endpoint.
    Json::Value cachedProps;
    std::string propsEndpoint;
    std::chrono::steady_clock::time_point propsAt;
    Json::Value completion(const Json::Value &, const Json::Value &, const std::string &,
                           unsigned reasoningBudget, double temperature,
                           const std::string &kind = "", const std::string &actor = "");

  public:
    explicit LocalProvider(std::string endpoint, double timeout = 20,
                           Transport transport = httpJson, double estimateShare = 0.8);
    Decision decide(const WorldState &, const CountryContext &) override;
    std::string narrate(const std::vector<ResolvedEvent> &) override;
    std::string narrate(const std::vector<ResolvedEvent> &, const CountryContext &);
    // Condenses a nation's real and campaign history into a short chronicle; input holds facts,
    // previous_chronicle, campaign_events and reference_only.
    std::string chronicle(const Json::Value &input, const std::string &actor);
    // responses: the player's answers to last month's treaty offers (see compileOperations).
    WorldDelta adjudicate(const WorldState &, const Json::Value &, const Adjacency &,
                          const Json::Value &responses = Json::Value(Json::arrayValue));
    Json::Value takeAudit();
    void setEndpoint(const std::string &);
    std::string endpointValue() const;
};
} // namespace lh
