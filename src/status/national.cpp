#include "status/status_internal.h"
#include <cmath>
#include <cstdio>
namespace lh {
using namespace detail;
std::string formatMoney(double usd, const Json::Value &status) {
    auto code = status.get("currency", "USD").asString();
    auto dollars = "US$" + scaled(usd);
    if (code == "USD")
        return dollars;
    auto symbol = status.get("symbol", code + " ").asString();
    return symbol + scaled(usd * number(status, "usd_rate", 1)) + " (" + dollars + ")";
}
Json::Value nationalStatus(const WorldState &w, const std::string &a) {
    auto t = totals(w);
    return buildStatus(w, a, t[a]);
}
Json::Value allNationalStatus(const WorldState &w) {
    auto t = totals(w);
    Json::Value out(Json::objectValue);
    for (auto &a : w.actors.getMemberNames())
        out[a] = buildStatus(w, a, t[a]);
    return out;
}
std::string campaignDate(int turn) {
    static const char *months[] = {"January",   "February", "March",    "April",
                                   "May",       "June",     "July",     "August",
                                   "September", "October",  "November", "December"};
    return std::string(months[((turn % 12) + 12) % 12]) + " " +
           std::to_string(2026 + int(std::floor(turn / 12.0)));
}
bool chronicleDue(const Json::Value &current, int turn, bool sourceAvailable) {
    if (!current.isObject() || !current.isMember("summary") || !current["summary"].isString())
        return true;
    if (turn - current.get("through_turn", 0).asInt() >= chronicleInterval)
        return true;
    return sourceAvailable && !current.get("with_source", false).asBool();
}
} // namespace lh
