#pragma once
#include "simulation.h"
#include <functional>
#include <mutex>
namespace lh {
using Transport = std::function<Json::Value(const std::string &, const std::string &,
                                            const Json::Value &, double)>;
Json::Value httpJson(const std::string &, const std::string &, const Json::Value &, double);
struct Decision {
    TurnOrders orders;
    bool fallback = false;
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
class LocalProvider : public DecisionProvider, public NarrationProvider {
    std::string endpoint;
    double timeout;
    Transport transport;
    std::mutex inference;
    Json::Value completion(const Json::Value &, const Json::Value &, const std::string &);

  public:
    LocalProvider(std::string endpoint = "", double timeout = 20, Transport transport = httpJson)
        : endpoint(std::move(endpoint)), timeout(timeout), transport(std::move(transport)) {}
    Decision decide(const WorldState &, const CountryContext &) override;
    std::string narrate(const std::vector<ResolvedEvent> &) override;
    Decision propose(const WorldState &, const CountryContext &, const std::string &);
    bool enabled() const { return !endpoint.empty(); }
};
Json::Value orderSchema();
} // namespace lh
