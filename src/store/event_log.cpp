#include "localhistoria/conflicts.h"
#include "localhistoria/store.h"
#include "localhistoria/world.h"
#include <algorithm>
#include <memory>
#include <tuple>
namespace lh {
Json::Value Store::chronicleEvents(const std::string &actor, int afterTurn) {
    Json::Value out(Json::arrayValue);
    const auto state = campaign();
    std::set<std::string> unresolved;
    auto links = [&](const Json::Value &v) {
        for (auto k : {"event", "origin_event"}) if (v[k].isString()) unresolved.insert(v[k].asString());
        for (const auto &e : v["supporting_events"]) if (e.isString()) unresolved.insert(e.asString());
    };
    for (auto kind : {"conflicts", "commitments", "proposals"})
        for (const auto &v : state[kind])
            if (isOpen(v) || v["status"] == "pending") links(v);
    for (const auto &region : state["regional_politics"])
        for (const auto &demand : region["demands"])
            if (demand["status"] == "pending" || demand["fulfillment"]["state"] == "in_progress")
                links(demand);
    struct Candidate { Json::Value event; int turn, priority; Json::Int64 row; };
    std::vector<Candidate> candidates;
    // EXISTS avoids duplicate event links; select priority before the 40-event cap.
    for (auto &row : query("SELECT e.id,e.turn,e.rowid AS row,document FROM events e WHERE "
            "EXISTS (SELECT 1 FROM event_links l WHERE l.event=e.id AND l.kind='actors' AND l.target=?) "
            "AND e.turn>? AND " + happened, {actor, std::to_string(afterTurn)})) {
        auto document = parse(row["document"].asString());
        auto id = row["id"].asString();
        bool structural = document.get("major", false).asBool() || !document["identity_changes"].empty() ||
            !document["conflicts"].empty() || !document["commitments"].empty();
        Json::Value event;
        event["id"] = id;
        event["turn"] = row["turn"];
        event["actor"] = document["actor"];
        event["description"] = (document.isMember("text") ? document["text"] : document["description"]).asString() + droppedClause(document);
        for (auto key : {"effects", "consequences", "project", "project_reason", "project_action"})
            if (document.isMember(key)) event[key] = document[key];
        candidates.push_back({event, row["turn"].asInt(), unresolved.contains(id) ? 2 : structural ? 1 : 0, row["row"].asInt64()});
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto &a, const auto &b) {
        return std::tie(a.priority, a.turn, a.row) > std::tie(b.priority, b.turn, b.row);
    });
    if (candidates.size() > 40) candidates.resize(40);
    std::sort(candidates.begin(), candidates.end(), [](const auto &a, const auto &b) {
        return std::tie(a.turn, a.row) < std::tie(b.turn, b.row);
    });
    for (const auto &candidate : candidates) out.append(candidate.event);
    return out;
}
namespace {
bool domestic(const std::string &action) {
    return action == "unrest" || action == "revolt" || action == "secession" ||
           action == "regime_change" || action == "integrated";
}
bool construction(const std::string &action) {
    return action == "completion" || action == "construction_paused" ||
           action == "construction_resumed" || action == "construction_transferred" ||
           action == "construction_disbanded" || action == "construction_stalled";
}
Json::Value strings(const std::set<std::string> &values) {
    Json::Value out(Json::arrayValue);
    for (const auto &v : values)
        out.append(v);
    return out;
}
} // namespace
void Store::projectMonth(int turn, const WorldState &w) {
    // What happened is fixed when the month commits: text, category, significance and the places,
    // actors and wars it names. Who fights a war, where an entity stands and what is still on the
    // map follow today's state, so reads add them.
    const auto month = std::to_string(turn);
    execute("DELETE FROM history_items WHERE turn=?", {month});
    execute("DELETE FROM history_links WHERE turn=?", {month});
    std::map<std::string, Json::Value> documents;
    for (auto &d : query("SELECT id,document FROM events WHERE turn=?", {month}))
        documents[d["id"].asString()] = parse(d["document"].asString());
    auto saved = query("SELECT events FROM turns WHERE turn=?", {month});
    int seq = 0;
    for (auto &x :
         saved.empty() ? Json::Value(Json::arrayValue) : parse(saved[0]["events"].asString())) {
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
        // Links a read may match against today's state: an actor scope checks them before
        // building each candidate.
        std::set<std::pair<std::string, std::string>> links;
        if (documents.contains(target)) {
            auto &d = documents[target];
            for (auto k : {"actors", "regions", "entities", "conflicts", "commitments",
                           "causes", "effects", "consequences", "project", "project_reason", "project_action", "breaches", "blocked_by", "partial", "failed",
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
            action.clear();
        } else if (action == "front_advance") {
            // Unlike the monthly toll, ground changing hands is news: it belongs in the
            // journal and on the map even though no one ordered it.
            item["major"] = true;
            item["category"] = "war";
            item["id"] = item["id"].asString() + "-" + x["region"].asString();
            item["conflicts"].append(target);
            item["regions"].append(x["region"]);
        } else if (action == "war_toll") {
            // Fronts already show on the Wars & treaties map; the monthly toll is routine.
            item["major"] = false;
            item["category"] = "war";
            item["conflicts"].append(target);
        } else if (domestic(action)) {
            // What a country's own people do to it. A revolt, a secession, a government
            // falling and a conquest finally absorbed are news; a province growing restive is
            // a running note.
            item["category"] = "domestic";
            item["major"] = action != "unrest";
            if (!x["region"].asString().empty())
                item["regions"].append(x["region"]);
            links.insert({"conflicts", target});
            links.insert({"actors", target});
        } else if (construction(action)) {
            item["category"] = "construction";
            // A finished city is years of work; units, bases and ports are routine, and so is
            // work halting or resuming as the front moves. An entity destroyed since is looked
            // up in the month's own saved state.
            auto kind = w.entities[target]["kind"];
            if (!w.entities.isMember(target))
                kind = query("SELECT json_extract(state,'$.entities.'||json_quote(?)||'.kind') AS "
                             "kind FROM turns WHERE turn=?",
                             {target, month})[0]["kind"];
            item["major"] = action == "completion" && kind == "city";
            item["entities"].append(target);
        } else {
            item["major"] = false;
            item["category"] = x["actor"] == w.player ? "player" : "nation";
        }
        for (auto k : {"actors", "regions", "entities", "conflicts"})
            for (auto &v : item[k])
                if (v.isString())
                    links.insert({k, v.asString()});
        execute("INSERT INTO history_items VALUES(?,?,?,?,?,?,?,?,?,?)",
                {month, std::to_string(seq), item["id"].asString(), item["category"].asString(),
                 item["major"].asBool() ? "1" : "0", item["text"].asString(), action, target,
                 x["region"].asString(), dump(item)});
        for (auto &[kind, value] : links)
            execute("INSERT INTO history_links VALUES(?,?,?,?)",
                    {month, std::to_string(seq), kind, value});
        seq++;
    }
    execute("INSERT OR REPLACE INTO history_months VALUES(?)", {month});
}
void Store::backfillHistory() {
    // Saves from before the projection gain it here, a few months per transaction, so an
    // interrupted upgrade resumes where it stopped. Authoritative turns and events are only read.
    std::optional<WorldState> w;
    for (;;) {
        auto pending = query("SELECT turn FROM turns WHERE turn NOT IN (SELECT turn FROM "
                             "history_months) ORDER BY turn LIMIT 24");
        if (pending.empty())
            break;
        if (!w)
            w = decode(campaign());
        sql("BEGIN IMMEDIATE");
        try {
            for (auto &row : pending)
                projectMonth(row["turn"].asInt(), *w);
            sql("COMMIT");
        } catch (...) {
            sql("ROLLBACK");
            throw;
        }
    }
    historyReady = true;
}
Json::Value Store::eventLog(const WorldState &w, const Json::Value &filter) {
    if (!historyReady)
        backfillHistory();
    std::set<std::string> categories;
    for (auto &c : filter["categories"])
        categories.insert(c.asString());
    auto who = filter["scope"] == "actor" ? filter["actor"].asString() : std::string();
    auto needle = lowercase(filter.get("q", "").asString());
    bool onMapOnly = filter.get("on_map", false).asBool(),
         majorOnly = filter.get("major", false).asBool();
    unsigned limit = std::clamp(filter.get("limit", 50).asUInt(), 1u, 500u);
    // A later event citing one as its cause renews its time on the map; only citations from the
    // last few months can still matter.
    const int mapCutoff = w.turn - eventMapMonths;
    std::map<std::string, int> cited;
    for (auto &row : query("SELECT l.target,MAX(e.turn) AS turn FROM events e JOIN event_links l "
                           "ON l.event=e.id AND +l.kind='causes' WHERE e.turn>? AND " +
                               happened + " GROUP BY l.target",
                           {std::to_string(mapCutoff)}))
        cited[row["target"].asString()] = row["turn"].asInt();
    std::set<std::string> open;
    for (const auto &id : w.conflicts.getMemberNames())
        if (isOpen(w.conflicts[id]))
            open.insert(id);
    // The filters SQL can apply exactly; an actor scope and the map also pass a superset, which
    // each candidate is checked against once today's state is added.
    std::string where = " AND turn<?";
    std::vector<std::string> args;
    if (majorOnly || onMapOnly)
        where += " AND major=1";
    if (!categories.empty()) {
        where += " AND category IN (SELECT value FROM json_each(?))";
        args.push_back(dump(strings(categories)));
    }
    if (!needle.empty()) {
        // SQLite's lower() is ASCII-only, like lowercase().
        where += " AND instr(lower(text),?)>0";
        args.push_back(needle);
    }
    if (!who.empty()) {
        std::set<std::string> regions, conflicts, entities;
        for (const auto &id : w.territories.getMemberNames())
            if (w.territories[id]["owner"] == who || w.territories[id]["controller"] == who)
                regions.insert(id);
        for (const auto &id : w.conflicts.getMemberNames()) {
            const auto &c = w.conflicts[id];
            bool touches = isParty(c, who);
            for (const auto &r : c["affected_regions"])
                touches = touches || (r.isString() && regions.contains(r.asString()));
            if (touches)
                conflicts.insert(id);
        }
        for (const auto &id : w.entities.getMemberNames())
            if (regions.contains(w.entities[id]["region"].asString()))
                entities.insert(id);
        where +=
            " AND EXISTS(SELECT 1 FROM history_links l WHERE l.turn=h.turn AND l.seq=h.seq AND "
            "((l.kind='actors' AND l.target=?) OR (l.kind='regions' AND l.target IN (SELECT "
            "value FROM json_each(?))) OR (l.kind='conflicts' AND l.target IN (SELECT value "
            "FROM json_each(?))) OR (l.kind='entities' AND l.target IN (SELECT value FROM "
            "json_each(?)))))";
        args.insert(args.end(), {who, dump(strings(regions)), dump(strings(conflicts)),
                                 dump(strings(entities))});
    }
    if (onMapOnly) {
        std::set<std::string> renewed;
        for (auto &[id, turn] : cited)
            renewed.insert(id);
        where += " AND (turn>? OR id IN (SELECT value FROM json_each(?)) OR EXISTS(SELECT 1 FROM "
                 "history_links l WHERE l.turn=h.turn AND l.seq=h.seq AND l.kind='conflicts' AND "
                 "l.target IN (SELECT value FROM json_each(?))))";
        args.insert(args.end(),
                    {std::to_string(mapCutoff), dump(strings(renewed)), dump(strings(open))});
    }
    // Where an event without places happened: the acting nation's heartland, looked up once.
    std::map<std::string, std::string> heartlands;
    auto home = [&](const std::string &actor) -> const std::string & {
        auto at = heartlands.find(actor);
        if (at == heartlands.end())
            at = heartlands.emplace(actor, lh::heartland(w, actor)).first;
        return at->second;
    };
    auto finish = [&](const Json::Value &row) {
        auto item = parse(row["item"].asString());
        auto action = row["action"].asString(), target = row["target"].asString();
        const auto &conflict = w.conflicts[target];
        bool war = w.conflicts.isMember(target);
        if (action == "front_advance" && war) {
            item["actors"] = conflict["actors"];
        } else if (action == "war_toll" && war) {
            item["actors"] = conflict["actors"];
            if (conflict["affected_regions"].isArray())
                item["regions"] = conflict["affected_regions"];
        } else if (domestic(action)) {
            if (war) {
                item["conflicts"].append(target);
                item["actors"] = conflict["actors"];
            } else if (w.hasActor(target) && target != item["actor"].asString())
                item["actors"].append(target);
        } else if (construction(action) && w.entities.isMember(target)) {
            item["regions"].append(w.entities[target]["region"]);
        }
        int turn = item["turn"].asInt();
        auto renewal = cited.find(item["id"].asString());
        int active = std::max(turn, renewal == cited.end() ? turn : renewal->second);
        // Only major events reach the map: for a few months, and while a war they shaped is
        // open.
        bool onMap = w.turn - active < eventMapMonths;
        for (auto &c : item["conflicts"])
            onMap = onMap || open.contains(c.asString());
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
        if (item["location"].empty() && !home(item["actor"].asString()).empty())
            place(w.territories[home(item["actor"].asString())]);
        return item;
    };
    auto matches = [&](const Json::Value &item) {
        if (onMapOnly && !item["on_map"].asBool())
            return false;
        if (who.empty())
            return true;
        for (auto &a : item["actors"])
            if (a == who)
                return true;
        for (auto &r : item["regions"])
            if (w.territories[r.asString()]["owner"] == who ||
                w.territories[r.asString()]["controller"] == who)
                return true;
        return false;
    };
    Json::Value out;
    out["events"] = Json::arrayValue;
    out["more"] = false;
    // Newest month first, in batches. Pages hold whole months, so paging by turn never skips an
    // event; a page has more only if an older month has a match.
    const unsigned batch = limit + 64;
    const std::string select = "SELECT turn,seq,action,target,item FROM history_items h WHERE 1";
    int cursor = filter.get("before_turn", w.turn + 1).asInt(), last = -1;
    for (bool done = false; !done;) {
        auto page = args;
        page.insert(page.begin(), std::to_string(cursor));
        auto rows =
            query(select + where + " ORDER BY turn DESC,seq LIMIT " + std::to_string(batch), page);
        done = rows.size() < batch;
        if (!done) {
            // Leave a month cut by the batch for the next one; a month larger than a whole batch
            // is read on its own.
            int cut = rows[rows.size() - 1]["turn"].asInt();
            if (rows[0]["turn"].asInt() == cut) {
                page.push_back(std::to_string(cut));
                rows = query(select + where + " AND turn=? ORDER BY seq", page);
                cursor = cut;
            } else {
                Json::Value whole(Json::arrayValue);
                for (auto &row : rows)
                    if (row["turn"].asInt() != cut)
                        whole.append(row);
                rows = whole;
                cursor = cut + 1;
            }
        }
        for (auto &row : rows) {
            auto item = finish(row);
            if (!matches(item))
                continue;
            int turn = item["turn"].asInt();
            if (turn != last && out["events"].size() >= limit) {
                out["more"] = true;
                return out;
            }
            out["events"].append(item);
            out["next_before_turn"] = last = turn;
        }
    }
    return out;
}
} // namespace lh
