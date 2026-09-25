#include "localhistoria/domestic.h"
#include <algorithm>
#include <cctype>
namespace lh {
bool groundedConsequences(const WorldState &w) {
    return w.engine.get("consequence_version", 0) == consequenceVersion;
}
bool distinctProjects(const WorldState &w) {
    return w.engine.get("project_version", 0) == projectVersion;
}
std::string consequenceText(const std::string &text) {
    std::string out;
    bool space = false;
    for (unsigned char c : text) {
        if (c < 128 && !std::isalnum(c)) {
            space = !out.empty();
            continue;
        }
        if (space)
            out += ' ';
        space = false;
        out += char(c < 128 ? std::tolower(c) : c);
    }
    return out.substr(0, 320);
}
void expireConsequences(Json::Value &ledger, int turn) {
    if (!ledger.isObject())
        ledger = Json::objectValue;
    for (const auto &id : ledger.getMemberNames())
        if (ledger[id]["last"].asInt() <= turn - consequenceExpiry)
            ledger.removeMember(id);
}
Json::Value recordConsequence(Json::Value &ledger, int turn, const std::string &key,
                              const std::string &kind, bool envelope) {
    expireConsequences(ledger, turn);
    const std::string bucket = "kind:" + kind;
    const auto prior = ledger.get(key, Json::nullValue);
    const auto group = ledger.get(bucket, Json::nullValue);
    double weight = 1;
    std::string reason = "distinct";
    if (!prior.isNull()) {
        weight = 0;
        reason = "duplicate";
    } else if (envelope && group.get("last", -1) == turn) {
        weight = 0;
        reason = "monthly envelope";
    } else if (envelope) {
        int count = group.get("months", 0).asInt();
        weight = count == 0 ? 1 : count == 1 ? 0.5 : count == 2 ? 0.25 : 0;
        if (count)
            reason = "diminished consequence";
    }
    // Even a repeat refreshes the envelope: continuous restatements never reset its budget.
    if (envelope) {
        auto &g = ledger[bucket];
        if (g.get("last", -1) != turn && prior.isNull())
            g["months"] = std::min(3, g.get("months", 0).asInt() + 1);
        g["last"] = turn;
    }
    ledger[key]["last"] = turn;
    while (ledger.size() > consequenceCapacity) {
        std::string oldest;
        for (const auto &id : ledger.getMemberNames())
            if (!id.starts_with("kind:") &&
                (oldest.empty() || ledger[id]["last"].asInt() < ledger[oldest]["last"].asInt()))
                oldest = id;
        ledger.removeMember(oldest);
    }
    Json::Value result;
    result["key"] = key;
    result["kind"] = kind;
    result["envelope"] = envelope;
    result["weight"] = weight;
    result["reason"] = reason;
    return result;
}
std::string domesticProjectId(const std::string &actor, const std::string &category,
                              const std::string &target, const std::string &benefit) {
    // Length prefixes avoid delimiter collisions; identity never depends on prose or the turn.
    auto part = [](const std::string &s) { return std::to_string(s.size()) + ":" + s; };
    return "p:" + part(actor) + part(category) + part(target) + part(benefit);
}
bool supportedProject(const std::string &category, const std::string &benefit) {
    return (category == "reform" &&
            (benefit == "administration" || benefit == "public_services")) ||
           (category == "investment" && (benefit == "infrastructure" || benefit == "production"));
}
void migrateDomesticProjects(WorldState &w) {
    for (const auto &a : w.actors.getMemberNames()) {
        if (!w.actors[a].isMember("status"))
            continue;
        auto &s = w.actors[a]["status"];
        if (!s.isObject())
            continue;
        const auto old = s.get("efforts", Json::Value(Json::objectValue));
        for (const auto &category : old.getMemberNames()) {
            if (category != "reform" && category != "investment")
                continue;
            auto p = old[category];
            auto id = domesticProjectId(a, category, a, "legacy");
            p["id"] = id;
            p["category"] = category;
            p["target"] = a;
            p["objective"] = "Legacy " + category + " (objective not recorded)";
            p["benefit"] = "legacy";
            p["origin_event"] = "";
            p["completion_condition"] = "paid_months";
            p["state"] = "active";
            s["projects"][id] = p;
        }
        s.removeMember("efforts");
    }
}
} // namespace lh
