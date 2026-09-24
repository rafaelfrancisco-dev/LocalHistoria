#pragma once
#include "world.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <memory>
namespace lh {
using Transport = std::function<Json::Value(const std::string &, const std::string &,
                                            const Json::Value &, double)>;
Json::Value httpJson(const std::string &, const std::string &, const Json::Value &, double);
struct Decision {
    TurnOrders orders;
    std::string status;
    Json::Value responses{Json::arrayValue};
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
bool clearlyDomestic(const WorldState &, const std::string &actor, const std::string &text);
bool trimOptionalContext(Json::Value &, const std::string &kind);
std::string validateEndpoint(const std::string &);
// Owned by the caller. Optional requests get their own deadline and audit; cancellation can
// be shared by requests belonging to the same turn. Never shared between concurrent callers.
struct InferenceRequest {
    std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max();
    std::shared_ptr<std::atomic<bool>> cancelled = std::make_shared<std::atomic<bool>>(false);
    Json::Value audit{Json::arrayValue};
    std::string retryReason;
    // Independent opt-in experiments; empty keeps the conservative production behavior.
    std::string experiment;
    Json::Value takeAudit() { auto result = audit; audit = Json::arrayValue; return result; }
};
class LocalProvider : public DecisionProvider, public NarrationProvider {
    std::string endpoint;
    double timeout;
    Transport transport;
    mutable std::mutex inference, settings;
    // Share of the context below which a byte estimate replaces exact tokenization; 0 always
    // tokenizes.
    double estimateShare;
    // /props, reused for a minute per endpoint.
    Json::Value cachedProps;
    std::string propsEndpoint;
    std::chrono::steady_clock::time_point propsAt;
    Json::Value call(const std::string &url, const std::string &path, const Json::Value &body,
                     InferenceRequest &);
    Json::Value completion(const Json::Value &, const Json::Value &, const std::string &,
                           unsigned reasoningBudget, double temperature,
                           const std::string &kind, const std::string &actor, InferenceRequest &);

  public:
    explicit LocalProvider(std::string endpoint, double timeout, Transport transport = httpJson,
                           double estimateShare = 0.8);
    Decision decide(const WorldState &, const CountryContext &) override;
    Decision decide(const WorldState &, const CountryContext &, InferenceRequest &);
    std::string narrate(const std::vector<ResolvedEvent> &) override;
    std::string narrate(const std::vector<ResolvedEvent> &, const CountryContext &,
                        InferenceRequest * = nullptr);
    // Condenses a nation's real and campaign history into a short chronicle; input holds facts,
    // previous_chronicle, campaign_events and reference_only.
    std::string chronicle(const Json::Value &input, const std::string &actor, InferenceRequest * = nullptr);
    // responses: the player's answers to last month's treaty offers (see compileOperations).
    WorldDelta adjudicate(const WorldState &, const Json::Value &, const Adjacency &,
                          const Json::Value &responses = Json::Value(Json::arrayValue),
                          InferenceRequest * = nullptr);
    void setEndpoint(const std::string &);
    std::string endpointValue() const;
};
} // namespace lh
