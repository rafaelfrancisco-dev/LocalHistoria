#include "campaign_factory.h"
#include <cstdio>
#include <drogon/utils/Utilities.h>
namespace lh {
using namespace drogon;
Json::Value loadGeography(Store &reference) {
    Json::Value geography(Json::objectValue);
    for (auto &r : reference.query("SELECT id,geometry FROM regions"))
        geography[r["id"].asString()] = parse(r["geometry"].asString());
    return geography;
}
Adjacency loadAdjacency(Store &reference) {
    Adjacency adjacency;
    for (auto &r : reference.query("SELECT a,b FROM region_neighbors"))
        adjacency[r["a"].asString()].insert(r["b"].asString());
    return adjacency;
}
StatusSeed loadStatusSeed(Store &reference) {
    StatusSeed statusSeed;
    for (auto &r : reference.query("SELECT * FROM country_stats")) {
        auto code = r["code"].asString();
        r.removeMember("code");
        statusSeed.countries[code] = r;
    }
    for (auto &r : reference.query("SELECT * FROM region_stats")) {
        auto id = r["id"].asString();
        r.removeMember("id");
        statusSeed.regions[id] = r;
    }
    auto seedRows = reference.query("SELECT document FROM world_seed WHERE id=1");
    if (statusSeed.countries.empty() || seedRows.empty())
        throw std::runtime_error("Reference database lacks national statistics; run make data");
    statusSeed.world = parse(seedRows[0]["document"].asString());
    for (auto &r : reference.query("SELECT * FROM military_stats"))
        statusSeed.military[r["code"].asString()] = r;
    return statusSeed;
}
WorldState newCampaign(Store &reference, const StatusSeed &statusSeed, const std::string &country) {
    WorldState w;
    for (auto &c : reference.query("SELECT code,name FROM countries ORDER BY code")) {
        auto id = c["code"].asString();
        w.actors[id]["name"] = c["name"];
        w.actors[id]["static_flag"] = id;
        unsigned hash = 0;
        for (unsigned char ch : id)
            hash = hash * 31 + ch;
        char color[8];
        snprintf(color, sizeof(color), "#%06x", 0x405050 + (hash * 7919) % 0x909090);
        w.actors[id]["color"] = color;
    }
    for (auto &r : reference.query("SELECT id,name,affiliation,kind FROM regions")) {
        auto &t = w.territories[r["id"].asString()];
        t["name"] = r["name"];
        t["initial_affiliation"] = r["affiliation"];
        t["owner"] = r["affiliation"];
        t["controller"] = r["affiliation"];
        t["kind"] = r["kind"];
    }
    for (auto &r : reference.query("SELECT * FROM region_interiors")) {
        auto &t = w.territories[r["id"].asString()];
        t["longitude"] = r["longitude"];
        t["latitude"] = r["latitude"];
    }
    for (auto &c :
         reference.query("SELECT * FROM (SELECT c.*,r.affiliation,ROW_NUMBER() "
                         "OVER(PARTITION BY r.affiliation ORDER BY population DESC,c.id) AS "
                         "rank FROM region_cities c JOIN regions r ON r.id=c.region WHERE "
                         "r.affiliation!='') WHERE rank<=3")) {
        auto &e = w.entities[c["id"].asString()];
        for (auto k : {"name", "region", "latitude", "longitude"})
            e[k] = c[k];
        e["actor"] = c["affiliation"];
        e["kind"] = "city";
        e["status"] = "active";
        e["origin_event"] = "initial";
    }
    w.player = country;
    if (!w.hasActor(w.player))
        throw ValidationError("Country is not playable");
    seedStatus(w, statusSeed, true);
    w.campaignId = utils::getUuid();
    return w;
}
} // namespace lh
