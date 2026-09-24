#pragma once
#include "simulation.h"
#include <optional>
#include <sqlite3.h>
namespace lh {
class Store {
    sqlite3 *db = nullptr;
    // Every event-log item of one campaign month, unfiltered and newest month first:
    // [{turn, items}]. Saved rows change only by committing a month or replacing the campaign.
    std::optional<std::pair<std::pair<std::string, int>, Json::Value>> eventLogCache;
    // Brings an older save's schema up to date; PRAGMA user_version records how far it got.
    void migrate();
    Json::Value eventLogMonths(const WorldState &);

  public:
    // Events rolled back as infeasible stay in the audit but never reach history as if they
    // happened.
    static inline const std::string happened =
        "json_extract(document,'$.blocked_by') IS NOT 'infeasible'";
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
    // The recorded delta changes of one month, without parsing the whole reference.
    Json::Value monthChanges(int turn);
    CountryContext campaignContext(const WorldState &, const std::string &);
    CountryContext campaignContext(const WorldState &, const std::string &, const Json::Value &journalSnapshot);
    void recordInference(const std::string &, const Json::Value &);
    Json::Value background(const std::string &);
    void cache(const std::string &, const Json::Value &);
    Json::Value chronicle(const std::string &actor);
    void saveChronicle(const std::string &actor, const Json::Value &);
    // Events an actor took part in after a turn, oldest first, reduced to what a chronicle needs.
    Json::Value chronicleEvents(const std::string &actor, int afterTurn);
    // Every event that happened, newest month first, with its category, map location and whether it
    // is still on the map. filter: scope (all|actor), actor, categories (array), q, on_map, major,
    // before_turn, limit.
    Json::Value eventLog(const WorldState &, const Json::Value &filter);
    std::string setting(const std::string &key, const std::string &fallbackDefault);
    void setSetting(const std::string &key, const std::string &value);
};
} // namespace lh
