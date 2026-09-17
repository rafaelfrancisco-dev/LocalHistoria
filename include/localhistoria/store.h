#pragma once
#include "simulation.h"
#include <sqlite3.h>
namespace lh {
class Store {
    sqlite3 *db = nullptr;

  public:
    // Events rolled back as infeasible stay in the audit but never reach history as if they happened.
    static inline const std::string happened = "json_extract(document,'$.blocked_by') IS NOT 'infeasible'";
    explicit Store(const std::string &path, bool readOnly = false);
    ~Store();
    Store(const Store &) = delete;
    void sql(const std::string &);
    Json::Value query(const std::string &, const std::vector<std::string> &args = {});
    void execute(const std::string &, const std::vector<std::string> &args = {});
    Json::Value campaign();
    void create(const WorldState &, bool replace);
    void recover();
    Json::Value submit(int turn, const std::string &key, const std::string &payload);
    void commit(const std::string &job, const Resolution &, const Json::Value &intentions,
                const Json::Value &refs, bool fail = false);
    void narrate(const std::string &job, const std::string &text, bool fallback);
    Json::Value job(const std::string &);
    Json::Value journal();
    CountryContext campaignContext(const WorldState &, const std::string &);
    void recordInference(const std::string &, const Json::Value &);
    Json::Value background(const std::string &);
    void cache(const std::string &, const Json::Value &);
    Json::Value chronicle(const std::string &actor);
    void saveChronicle(const std::string &actor, const Json::Value &);
    // Events an actor took part in after a turn, oldest first, reduced to what a chronicle needs.
    Json::Value chronicleEvents(const std::string &actor, int afterTurn);
    // Every event that happened, newest month first, with its category, map location and whether it is still on the map.
    // filter: scope (all|actor), actor, categories (array), q, on_map, major, before_turn, limit.
    Json::Value eventLog(const WorldState &, const Json::Value &filter);
    std::string setting(const std::string &key, const std::string &fallbackDefault);
    void setSetting(const std::string &key, const std::string &value);
};
} // namespace lh
