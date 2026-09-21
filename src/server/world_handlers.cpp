#include "server/server_internal.h"
#include <algorithm>
#include <cmath>
namespace lh::detail {
Json::Value changeSummaries(const Json::Value &changes, const WorldState &w) {
    Json::Value out(Json::arrayValue);
    auto actor = [&](const Json::Value &code) {
        auto name = w.actors[code.asString()]["name"];
        return name.isString() ? name.asString() : code.asString();
    };
    auto region = [&](const Json::Value &id) {
        auto name = w.territories[id.asString()]["name"];
        return name.isString() ? name.asString() : id.asString();
    };
    for (auto &c : changes) {
        auto kind = c["kind"].asString();
        auto &before = c["before"];
        auto &after = c["after"];
        auto name = after.get("name", c["id"]).asString();
        std::vector<std::string> parts;
        if (kind == "territory") {
            // A transfer moves ownership and control together: one arrow.
            bool transfer = before["owner"] != after["owner"] &&
                            before["owner"] == before["controller"] &&
                            after["owner"] == after["controller"];
            if (transfer)
                parts.push_back(actor(before["owner"]) + " → " + actor(after["owner"]));
            else if (before["owner"] != after["owner"])
                parts.push_back("owner " + actor(before["owner"]) + " → " + actor(after["owner"]));
            if (!transfer && before["controller"] != after["controller"])
                parts.push_back("control " + actor(before["controller"]) + " → " +
                                actor(after["controller"]));
            if (!parts.empty())
                parts.front() = region(c["id"]) + ": " + parts.front();
        } else if (kind == "entity") {
            auto label = name + " (" + after["kind"].asString() + ")";
            if (before.isNull())
                parts.push_back(label + (after["status"] == "under_construction"
                                             ? " construction started"
                                             : " formed"));
            else if (before["status"] != after["status"] && after["status"] == "destroyed")
                parts.push_back(label + " destroyed");
            else if (before["status"] == "under_construction" && after["status"] == "active")
                parts.push_back(label + " completed");
            else if (before["region"] != after["region"])
                parts.push_back(label + " moved to " + region(after["region"]));
        } else if (kind == "conflict") {
            if (before.isNull())
                parts.push_back(name + " opened");
            else if (before["status"] != after["status"] && !isOpen(after))
                parts.push_back(name + " ended");
            else if (before["intensity"] != after["intensity"]) {
                static const std::map<std::string, int> level{
                    {"none", 0}, {"low", 1}, {"medium", 2}, {"high", 3}};
                auto from = before["intensity"].asString(), to = after["intensity"].asString();
                bool up =
                    level.contains(from) && level.contains(to) && level.at(to) > level.at(from);
                parts.push_back(name + (up ? " escalated to " : " de-escalated to ") + to);
            }
        } else if (kind == "commitment") {
            if (before.isNull())
                parts.push_back(name + " signed");
            else if (before["status"] != after["status"] && !isOpen(after))
                parts.push_back(name + " ended");
        } else if (kind == "actor") {
            if (!before.isNull() && before["name"] != after["name"])
                parts.push_back(before["name"].asString() + " renamed " + after["name"].asString());
        }
        if (parts.empty())
            continue;
        std::string summary;
        for (auto &p : parts)
            summary += (summary.empty() ? "" : ", ") + p;
        Json::Value item;
        item["kind"] = kind;
        item["id"] = c["id"];
        item["summary"] = summary;
        out.append(item);
    }
    return out;
}
void history(Server &s, const HttpRequestPtr &req, Json::Value b, const CB &cb) {
    std::lock_guard l(s.mutex);
    auto code = b.isNull() ? req->getParameter("code") : b.get("code", "").asString();
    if (s.reference.query("SELECT code FROM countries WHERE code=?", {code}).empty())
        throw ValidationError("Unknown country");
    if (b.isNull()) {
        reply(cb, s.store.background(code));
        return;
    }
    reply(cb, s.references.refresh(code), 202);
}
void inference(Server &s, const HttpRequestPtr &req, Json::Value, const CB &cb) {
    std::lock_guard l(s.mutex);
    // "since" (a previously seen row id) lets a live log poll for only what is new.
    auto since = req->getParameter("since");
    if (since.empty())
        since = "0";
    auto rows = s.store.query("SELECT id,record FROM inference WHERE job=? AND id>? ORDER BY id",
                              {req->getParameter("job"), since});
    Json::Value records(Json::arrayValue);
    for (auto &row : rows) {
        auto record = parse(row["record"].asString());
        record["id"] = row["id"];
        records.append(record);
    }
    reply(cb, records);
}
void map(Server &s, const HttpRequestPtr &, Json::Value, const CB &cb) {
    std::lock_guard l(s.mutex);
    auto w = decode(s.store.campaign());
    Json::Value j;
    j["geometry_url"] = "/regions.topojson?v=" + s.geometryVersion;
    j["territories"] = w.territories;
    j["turn"] = w.turn;
    reply(cb, j);
}
void entities(Server &s, const HttpRequestPtr &req, Json::Value, const CB &cb) {
    std::lock_guard l(s.mutex);
    auto w = decode(s.store.campaign());
    Json::Value j(Json::arrayValue);
    auto number = [&](const char *key, double fallback) {
        auto p = req->getParameter(key);
        if (p.empty())
            return fallback;
        size_t end = 0;
        double x = std::stod(p, &end);
        if (end != p.size() || !std::isfinite(x))
            throw ValidationError("Invalid viewport");
        return x;
    };
    double west = number("west", -180), east = number("east", 180), south = number("south", -90),
           north = number("north", 90), zoom = number("zoom", 2);
    if (west > east || south > north)
        throw ValidationError("Invalid viewport bounds");
    auto wanted = req->getParameter("id");
    // Destroyed and inactive entities, such as faded event sites, leave the map
    // unless asked for.
    bool inactive = req->getParameter("include_inactive") == "1";
    for (auto &id : w.entities.getMemberNames()) {
        auto e = w.entities[id];
        double x = e["longitude"].asDouble(), y = e["latitude"].asDouble();
        if (wanted.empty() && !inactive &&
            (e["status"] == "destroyed" || e["status"] == "inactive"))
            continue;
        if ((!wanted.empty() && id == wanted) ||
            (wanted.empty() && x >= west && x <= east && y >= south && y <= north &&
             (zoom >= 4 || e["kind"] != "city"))) {
            e["id"] = id;
            j.append(e);
            if (j.size() >= 200)
                break;
        }
    }
    reply(cb, j);
}
void events(Server &s, const HttpRequestPtr &req, Json::Value, const CB &cb) {
    std::lock_guard l(s.mutex);
    auto w = decode(s.store.campaign());
    Json::Value filter;
    auto scope = req->getParameter("scope");
    if (scope.empty())
        scope = "all";
    if (scope != "all" && scope != "actor")
        throw ValidationError("scope must be all or actor");
    filter["scope"] = scope;
    if (scope == "actor") {
        auto a = req->getParameter("actor");
        if (!w.hasActor(a))
            throw ValidationError("Unknown actor");
        filter["actor"] = a;
    }
    static const std::set<std::string> known{"player", "nation", "regional",
                                             "world",  "war",    "construction"};
    filter["categories"] = Json::arrayValue;
    for (auto categories = req->getParameter("category"); !categories.empty();) {
        auto comma = categories.find(',');
        auto c = categories.substr(0, comma);
        if (!known.contains(c))
            throw ValidationError("Unknown event category: " + c);
        filter["categories"].append(c);
        categories = comma == std::string::npos ? "" : categories.substr(comma + 1);
    }
    auto q = req->getParameter("q");
    if (q.size() > 200)
        throw ValidationError("Search is too long");
    filter["q"] = q;
    filter["on_map"] = req->getParameter("on_map") == "1";
    filter["major"] = req->getParameter("major") == "1";
    auto integer = [&](const char *key, int fallback, int low, int high) {
        auto p = req->getParameter(key);
        if (p.empty())
            return fallback;
        if (!isSmallNumber(p))
            throw ValidationError(std::string("Invalid ") + key);
        return std::clamp(std::stoi(p), low, high);
    };
    filter["before_turn"] = integer("before_turn", w.turn + 1, 0, w.turn + 1);
    filter["limit"] = integer("limit", filter["on_map"].asBool() ? 500 : 50, 1,
                              filter["on_map"].asBool() ? 500 : 100);
    auto j = w.campaignId.empty() ? Json::Value() : s.store.eventLog(w, filter);
    if (j.isNull()) {
        j["events"] = Json::arrayValue;
        j["more"] = false;
    }
    j["turn"] = w.turn;
    reply(cb, j);
}
} // namespace lh::detail
