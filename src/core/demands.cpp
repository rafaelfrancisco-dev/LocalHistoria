#include "localhistoria/demands.h"
#include "localhistoria/domestic.h"
#include "status/status_internal.h"
#include <cmath>
namespace lh {
bool typedDemands(const WorldState &w) {
    return w.engine.get("demand_version", 0) == demandVersion;
}
std::string demandCategory(const std::string &benefit) {
    for (auto category : {"investment", "reform"})
        if (supportedProject(category, benefit))
            return category;
    return "";
}
int fulfillmentMonths(const Json::Value &terms) {
    // Twice the work's own schedule: room for a lean month or two, never an open-ended promise.
    return 2 * (terms["category"] == "investment" ? rate::investmentMonths : rate::reformMonths);
}
Json::Value demandTerms(const WorldState &w, const std::string &r, const std::string &kind,
                        const std::string &benefit, const std::string &text) {
    const auto &t = w.territories[r];
    Json::Value terms;
    terms["kind"] = "political";
    auto category = demandCategory(benefit);
    if (kind == "regional_project" && !category.empty()) {
        terms["kind"] = kind;
        terms["category"] = category;
        terms["benefit"] = benefit;
        terms["objective"] = text.substr(0, 120);
        terms["months"] = category == "investment" ? rate::investmentMonths : rate::reformMonths;
        // What the work would cost at today's output of the region; the monthly allowance pays it.
        double gdp = t["gdp_usd"].isNumeric() ? t["gdp_usd"].asDouble() : 0;
        terms["estimated_cost_usd"] = Json::Int64(std::llround(
            (category == "investment" ? rate::investmentCost : rate::reformCost) * gdp));
        terms["required_consent"].append(t["owner"]);
    } else if (kind == "independence" && t["owner"].isString() && !t["owner"].asString().empty()) {
        terms["kind"] = kind;
        terms["new_state"] = t.get("name", r).asString().substr(0, 120);
        terms["regions"].append(r);
        terms["required_consent"].append(t["owner"]);
    }
    return terms;
}
bool validDemandTerms(const Json::Value &t) {
    if (!t.isObject() || !t["kind"].isString())
        return false;
    auto names = t.getMemberNames();
    auto kind = t["kind"].asString();
    auto consent = [&] {
        return t["required_consent"].isArray() && t["required_consent"].size() == 1 &&
               t["required_consent"][0].isString();
    };
    if (kind == "political")
        return names == std::vector<std::string>{"kind"};
    if (kind == "regional_project")
        return names ==
                   std::vector<std::string>{"benefit", "category",  "estimated_cost_usd", "kind",
                                            "months",  "objective", "required_consent"} &&
               t["benefit"].isString() &&
               demandCategory(t["benefit"].asString()) == t["category"].asString() &&
               t["objective"].isString() && !t["objective"].asString().empty() &&
               t["objective"].asString().size() <= 120 && t["months"].isInt() &&
               t["months"].asInt() > 0 && t["months"].asInt() <= 36 &&
               t["estimated_cost_usd"].isIntegral() && t["estimated_cost_usd"].asInt64() >= 0 &&
               consent();
    if (kind == "independence")
        return names ==
                   std::vector<std::string>{"kind", "new_state", "regions", "required_consent"} &&
               t["new_state"].isString() && !t["new_state"].asString().empty() &&
               t["new_state"].asString().size() <= 120 && t["regions"].isArray() &&
               t["regions"].size() == 1 && t["regions"][0].isString() && consent();
    return false;
}
} // namespace lh
