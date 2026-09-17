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
    mutable std::mutex inference;
    Json::Value completion(const Json::Value &, const Json::Value &, const std::string &);

  public:
    explicit LocalProvider(std::string endpoint, double timeout = 20,
                           Transport transport = httpJson);
    Decision decide(const WorldState &, const CountryContext &) override;
    std::string narrate(const std::vector<ResolvedEvent> &) override;
    Decision propose(const WorldState &, const CountryContext &, const std::string &);
    void setEndpoint(const std::string &);
    std::string endpointValue() const;
};
Json::Value orderSchema();
} // namespace lh
