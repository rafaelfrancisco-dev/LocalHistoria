#include "localhistoria/conflicts.h"
#include "localhistoria/store.h"
#include "localhistoria/world.h"
#include <algorithm>
#include <memory>
namespace lh {
Json::Value Store::chronicleEvents(const std::string &actor, int afterTurn) {
    Json::Value out(Json::arrayValue);
    // The latest 40, returned oldest first.
    for (auto &row :
         query("SELECT turn,document FROM (SELECT e.turn,e.document,e.rowid AS r FROM events e "
               "JOIN event_links l ON l.event=e.id "
               "WHERE l.kind='actors' AND l.target=? AND e.turn>? AND " +
                   happened + " ORDER BY e.turn DESC,e.rowid DESC LIMIT 40) ORDER BY turn,r",
               {actor, std::to_string(afterTurn)})) {
        auto document = parse(row["document"].asString());
        Json::Value event;
        event["turn"] = row["turn"];
        event["actor"] = document["actor"];
        // Saved events hold their outcome as text; the chronicle prompt calls it a description.
        event["description"] =
            (document.isMember("text") ? document["text"] : document["description"]).asString() +
            droppedClause(document);
        if (document.isMember("effects"))
            event["effects"] = document["effects"];
        out.append(event);
    }
    return out;
}
Json::Value Store::eventLog(const WorldState &w, const Json::Value &filter) {
    auto key = std::pair{w.campaignId, w.turn};
    if (!eventLogCache || eventLogCache->first != key)
        eventLogCache.emplace(key, eventLogMonths(w));
    std::set<std::string> categories;
    for (auto &c : filter["categories"])
        categories.insert(c.asString());
    auto who = filter["scope"] == "actor" ? filter["actor"].asString() : std::string();
    auto needle = lowercase(filter.get("q", "").asString());
    bool onMapOnly = filter.get("on_map", false).asBool(),
         majorOnly = filter.get("major", false).asBool();
    unsigned limit = std::clamp(filter.get("limit", 50).asUInt(), 1u, 500u);
    Json::Value out;
    out["events"] = Json::arrayValue;
    out["more"] = false;
    int before = filter.get("before_turn", w.turn + 1).asInt();
    for (const auto &month : eventLogCache->second) {
        int turn = month["turn"].asInt();
        if (turn >= before)
            continue;
        // Pages hold whole months, so paging by turn never skips an event.
        if (out["events"].size() >= limit) {
            out["more"] = true;
            break;
        }
        for (const auto &item : month["items"]) {
            if (majorOnly && !item["major"].asBool())
                continue;
            if (onMapOnly && !item["on_map"].asBool())
                continue;
            if (!categories.empty() && !categories.contains(item["category"].asString()))
                continue;
            if (!needle.empty() &&
                lowercase(item["text"].asString()).find(needle) == std::string::npos)
                continue;
            if (!who.empty()) {
                bool related = false;
                for (auto &a : item["actors"])
                    related = related || a == who;
                for (auto &r : item["regions"])
                    related = related || w.territories[r.asString()]["owner"] == who ||
                              w.territories[r.asString()]["controller"] == who;
                if (!related)
                    continue;
            }
            out["events"].append(item);
        }
        out["next_before_turn"] = turn;
    }
    return out;
}
Json::Value Store::eventLogMonths(const WorldState &w) {
    // A later event citing one as its cause renews its time on the map.
    std::map<std::string, int> cited;
    for (auto &row : query("SELECT l.target,MAX(e.turn) AS turn FROM event_links l JOIN events e "
                           "ON e.id=l.event WHERE l.kind='causes' AND " +
                           happened + " GROUP BY l.target"))
        cited[row["target"].asString()] = row["turn"].asInt();
    // Where an event without places happened: the acting nation's heartland, looked up once.
    std::map<std::string, std::string> heartlands;
    auto home = [&](const std::string &actor) -> const std::string & {
        auto at = heartlands.find(actor);
        if (at == heartlands.end())
            at = heartlands.emplace(actor, lh::heartland(w, actor)).first;
        return at->second;
    };
    Json::Value months(Json::arrayValue);
    for (auto &row : query("SELECT turn,events FROM turns ORDER BY turn DESC")) {
        int turn = row["turn"].asInt();
        // Only feasible events reach a month's events, and structured documents add their places
        // and links.
        std::map<std::string, Json::Value> documents;
        for (auto &d : query("SELECT id,document FROM events WHERE turn=?", {std::to_string(turn)}))
            documents[d["id"].asString()] = parse(d["document"].asString());
        Json::Value month;
        month["turn"] = turn;
        month["items"] = Json::arrayValue;
        for (auto &x : parse(row["events"].asString())) {
            auto action = x["action"].asString(), target = x["target"].asString();
            Json::Value item;
            item["turn"] = turn;
            item["text"] = x["text"];
            item["actor"] = x["actor"];
            item["id"] = documents.contains(target)
                             ? target
                             : "t" + std::to_string(turn) + "-" + action + "-" + target;
            for (auto k : {"actors", "regions", "entities", "conflicts", "commitments", "causes"})
                item[k] = Json::arrayValue;
            item["actors"].append(x["actor"]);
            if (documents.contains(target)) {
                auto &d = documents[target];
                for (auto k : {"actors", "regions", "entities", "conflicts", "commitments",
                               "causes", "effects", "breaches", "blocked_by", "partial", "failed",
                               "regional_actor", "initiative_type", "identity_changes"})
                    if (d.isMember(k))
                        item[k] = d[k];
                // Events saved before significance existed count as major only for a breach, a new
                // flag or a severe effect.
                bool major = d.get("major", false).asBool();
                if (!d.isMember("major")) {
                    major = d.isMember("breaches") ||
                            (d["identity_changes"].isArray() && !d["identity_changes"].empty());
                    for (auto &e : d["effects"])
                        for (auto k : {"economy", "stability"})
                            major = major || e[k] == "severe_decline" || e[k] == "strong_growth";
                }
                item["major"] = major;
                item["category"] = d["regional_actor"].isString() ? "regional"
                                   : d["background"].asBool()     ? "world"
                                   : x["actor"] == w.player       ? "player"
                                                                  : "nation";
            } else if (action == "front_advance") {
                // Unlike the monthly toll, ground changing hands is news: it belongs in the
                // journal and on the map even though no one ordered it.
                item["major"] = true;
                item["category"] = "war";
                item["id"] = item["id"].asString() + "-" + x["region"].asString();
                item["conflicts"].append(target);
                item["regions"].append(x["region"]);
                if (w.conflicts.isMember(target)) {
                    item["actors"] = w.conflicts[target]["actors"];
                }
            } else if (action == "war_toll") {
                // Fronts already show on the Wars & treaties map; the monthly toll is routine.
                item["major"] = false;
                item["category"] = "war";
                item["conflicts"].append(target);
                if (w.conflicts.isMember(target)) {
                    item["actors"] = w.conflicts[target]["actors"];
                    if (w.conflicts[target]["affected_regions"].isArray())
                        item["regions"] = w.conflicts[target]["affected_regions"];
                }
            } else if (action == "unrest" || action == "revolt" || action == "secession" ||
                       action == "regime_change") {
                // What a country's own people do to it. A revolt, a secession and a government
                // falling are news; a province growing restive is a running note.
                item["category"] = "domestic";
                item["major"] = action != "unrest";
                if (!x["region"].asString().empty())
                    item["regions"].append(x["region"]);
                if (w.conflicts.isMember(target)) {
                    item["conflicts"].append(target);
                    item["actors"] = w.conflicts[target]["actors"];
                } else if (w.hasActor(target) && target != x["actor"].asString())
                    item["actors"].append(target);
            } else if (action == "completion" || action == "construction_paused" ||
                       action == "construction_resumed" || action == "construction_transferred" ||
                       action == "construction_disbanded") {
                item["category"] = "construction";
                // A finished city is years of work; units, bases and ports are routine, and so is
                // work halting or resuming as the front moves.
                item["major"] = action == "completion" && w.entities[target]["kind"] == "city";
                item["entities"].append(target);
                if (w.entities.isMember(target))
                    item["regions"].append(w.entities[target]["region"]);
            } else {
                item["major"] = false;
                item["category"] = x["actor"] == w.player ? "player" : "nation";
            }
            int active = std::max(
                turn, cited.contains(item["id"].asString()) ? cited[item["id"].asString()] : turn);
            // Only major events reach the map: for a few months, and while a war they shaped is
            // open.
            bool onMap = w.turn - active < eventMapMonths;
            for (auto &c : item["conflicts"])
                onMap = onMap || isOpen(w.conflicts[c.asString()]);
            onMap = onMap && item["major"].asBool();
            item["on_map"] = onMap;
            item["location"] = Json::arrayValue;
            std::set<std::pair<double, double>> seen;
            auto place = [&](const Json::Value &v) {
                if (!v["latitude"].isNumeric() || !v["longitude"].isNumeric() ||
                    item["location"].size() >= 4)
                    return;
                if (!seen.insert({v["latitude"].asDouble(), v["longitude"].asDouble()}).second)
                    return;
                Json::Value p;
                p["lat"] = v["latitude"];
                p["lon"] = v["longitude"];
                item["location"].append(p);
            };
            for (auto &r : item["regions"])
                place(w.territories[r.asString()]);
            for (auto &e : item["entities"])
                place(w.entities[e.asString()]);
            if (item["location"].empty() && !home(x["actor"].asString()).empty())
                place(w.territories[home(x["actor"].asString())]);
            month["items"].append(item);
        }
        months.append(month);
    }
    return months;
}
} // namespace lh
