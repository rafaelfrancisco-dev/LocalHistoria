#include "localhistoria/references.h"
#include "localhistoria/history.h"
#include "localhistoria/politics.h"
#include "localhistoria/status.h"
#include "turn/worker.h"
#include <algorithm>
namespace lh {
ReferencePreparer::ReferencePreparer(Store &store, Store &reference, std::mutex &mutex,
                                     Transport transport)
    : store(store), reference(reference), mutex(mutex), transport(std::move(transport)),
      worker(std::make_unique<Worker>(false)) {}
ReferencePreparer::~ReferencePreparer() = default;
void ReferencePreparer::finish() { worker->finish(); }
std::pair<std::map<std::string, int>, std::set<std::string>>
ReferencePreparer::relevance(const WorldState &w) {
    if (greatPowersCampaign != w.campaignId) {
        std::vector<std::pair<double, std::string>> gdp;
        auto status = allNationalStatus(w);
        for (auto &a : status.getMemberNames())
            gdp.emplace_back(-status[a]["gdp_usd"].asDouble(), a);
        std::sort(gdp.begin(), gdp.end());
        greatPowers.clear();
        for (size_t i = 0; i < std::min<size_t>(12, gdp.size()); ++i)
            greatPowers.insert(gdp[i].second);
        greatPowersCampaign = w.campaignId;
    }
    std::map<std::string, int> closeness;
    auto raise = [&](const std::string &a, int tier) {
        if (w.hasActor(a))
            closeness[a] = std::max(closeness[a], tier);
    };
    auto party = [&](const Json::Value &v) { return isOpen(v) && isParty(v, w.player); };
    for (auto &v : w.conflicts)
        if (party(v))
            for (auto &a : v["actors"])
                raise(a.asString(), 2);
    for (auto &v : w.commitments)
        if (party(v))
            for (auto &a : v["actors"])
                raise(a.asString(), v["actors"].size() <= 6 ? 2 : 1);
    for (auto &n : reference.query("SELECT b FROM neighbors WHERE a=?", {w.player}))
        raise(n["b"].asString(), 3);
    raise(w.player, 3);
    return std::make_pair(closeness, greatPowers);
}
void ReferencePreparer::prepare(const WorldState &w) {
    std::vector<std::string> wanted;
    auto [closeness, powers] = relevance(w);
    for (auto &slot : scheduleInitiatives(w, {}, closeness, powers)) {
        wanted.push_back(slot["actor"].asString());
        if (slot["region"] != "")
            wanted.push_back(slot["region"].asString());
    }
    wanted.push_back(w.player);
    for (auto &n : reference.query("SELECT b FROM neighbors WHERE a=? ORDER BY b", {w.player}))
        wanted.push_back(n["b"].asString());
    for (auto &key : wanted) {
        if (preparing.size() >= 8)
            break;
        // References cached before they held recent history and a chronicle source are
        // fetched once more.
        auto ready = [&](const Json::Value &r) {
            return r.get("available", false).asBool() &&
                   r.get("history_scope", "") == "recent_source";
        };
        if (key.empty() || preparing.contains(key) || ready(store.background(key)) ||
            retryAfter[key] > std::time(nullptr))
            continue;
        auto cached = store.background(key);
        if (cached["retrieved_at"].isIntegral() &&
            cached["retrieved_at"].asInt64() + 3600 > std::time(nullptr))
            continue;
        bool regional = w.territories.isMember(key);
        if (!regional && reference.query("SELECT code FROM countries WHERE code=?", {key}).empty())
            continue;
        auto region = regional ? w.territories[key] : Json::Value();
        preparing.insert(key);
        try {
            worker->add([&, key, region, regional] {
                Json::Value result;
                try {
                    result = regional
                                 ? fetchRegionalHistory(region["name"].asString(),
                                                        region["initial_affiliation"].asString(),
                                                        transport)
                                 : fetchHistory(key, transport);
                } catch (const std::exception &e) {
                    result["available"] = false;
                    result["reason"] = e.what();
                }
                if (!result.get("available", false).asBool())
                    result["retrieved_at"] = Json::Int64(std::time(nullptr));
                std::lock_guard lock(mutex);
                if (result.get("available", false).asBool() ||
                    !store.background(key).get("available", false).asBool())
                    store.cache(key, result);
                if (!result.get("available", false).asBool())
                    retryAfter[key] = std::time(nullptr) + 3600;
                preparing.erase(key);
            });
        } catch (const ValidationError &) {
            preparing.erase(key);
            break;
        }
    }
}
Json::Value ReferencePreparer::refresh(const std::string &code) {
    if (auxiliary.size() >= 128) {
        for (auto it = auxiliary.begin(); it != auxiliary.end();) {
            if (it->second["status"] != "running")
                it = auxiliary.erase(it);
            else
                ++it;
        }
    }
    auto id = "history-" + std::to_string(++auxiliaryId);
    worker->add([&, code, id] {
        auto result = fetchHistory(code, transport);
        std::lock_guard l(mutex);
        if (result["available"].asBool() || store.background(code).isNull())
            store.cache(code, result);
        Json::Value j;
        j["id"] = id;
        j["status"] = "complete";
        j["result"] = result;
        auxiliary[id] = j;
    });
    Json::Value j;
    j["id"] = id;
    j["status"] = "running";
    auxiliary[id] = j;
    return j;
}
std::set<std::string> ReferencePreparer::engagedActors(const WorldState &w) {
    std::set<std::string> actors{w.player};
    for (auto &r : reference.query("SELECT b FROM neighbors WHERE a=?", {w.player}))
        actors.insert(r["b"].asString());
    for (auto collection : {w.conflicts, w.commitments})
        for (auto &v : collection)
            if (v["actors"].size() <= 3 && std::find(v["actors"].begin(), v["actors"].end(),
                                                     Json::Value(w.player)) != v["actors"].end())
                for (auto &a : v["actors"])
                    actors.insert(a.asString());
    return actors;
}
Json::Value ReferencePreparer::coverage(const std::set<std::string> &actors) {
    Json::Value coverage;
    for (auto &a : actors)
        coverage[a] = store.background(a);
    return coverage;
}
Json::Value ReferencePreparer::prepareAll(const std::set<std::string> &actors) {
    auto id = "prepare-" + std::to_string(++auxiliaryId);
    Json::Value j;
    j["id"] = id;
    j["status"] = "running";
    auxiliary[id] = j;
    worker->add([&, actors, id] {
        for (auto &a : actors) {
            if (!isCountryCode(a))
                continue;
            auto result = fetchHistory(a, transport);
            std::lock_guard l(mutex);
            if (result["available"].asBool() ||
                !store.background(a).get("available", false).asBool())
                store.cache(a, result);
            auxiliary[id]["coverage"][a] = result;
        }
        std::lock_guard l(mutex);
        auxiliary[id]["status"] = "complete";
    });
    return j;
}
Json::Value ReferencePreparer::job(const std::string &id) const {
    return auxiliary.contains(id) ? auxiliary.at(id) : Json::Value();
}
} // namespace lh
