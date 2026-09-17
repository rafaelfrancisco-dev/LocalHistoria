#include "localhistoria/simulation.h"
#include "localhistoria/status.h"
#include <sstream>
namespace lh {
std::string dump(const Json::Value &j) {
    Json::StreamWriterBuilder b;
    b["indentation"] = "";
    return Json::writeString(b, j);
}
Json::Value parse(const std::string &s) {
    Json::CharReaderBuilder b;
    b["rejectDupKeys"] = true;
    b["failIfExtra"] = true;
    Json::Value j;
    std::string e;
    std::istringstream in(s);
    if (!Json::parseFromStream(b, in, &j, &e))
        throw ValidationError("Invalid JSON");
    return j;
}
Json::Value briefReference(const Json::Value &reference, size_t excerpt, size_t history) {
    auto brief = reference;
    if (!brief.isObject()) return brief;
    auto cut = [&](const char *key, size_t limit) {
        if (!brief[key].isString()) return;
        auto text = brief[key].asString();
        if (text.size() <= limit) return;
        auto end = text.rfind(' ', limit);
        // Without a space, step back to a UTF-8 lead byte so no character is split.
        if (end == std::string::npos || end == 0) for (end = limit; end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80; --end) {}
        brief[key] = text.substr(0, end) + "…";
    };
    cut("excerpt", excerpt);
    // The long source only feeds chronicle summaries.
    brief.removeMember("history_source");
    // A chronicle already covers the history and the campaign so far, oldest first.
    if (brief["chronicle"].isString()) {
        brief.removeMember("history_excerpt");
        if (history == 0) brief.removeMember("chronicle");
        else cut("chronicle", history);
        return brief;
    }
    // The history excerpt ends with the most recent period, so keep its end.
    if (history == 0) brief.removeMember("history_excerpt");
    else if (brief["history_excerpt"].isString() && brief["history_excerpt"].asString().size() > history) {
        auto text = brief["history_excerpt"].asString();
        auto start = text.find(' ', text.size() - history);
        brief["history_excerpt"] = "…" + (start == std::string::npos ? text.substr(text.size() - history) : text.substr(start + 1));
    }
    return brief;
}
Json::Value encode(const WorldState &w) {
    Json::Value j;
    j["campaign_id"] = w.campaignId;
    j["actors"] = w.actors; j["territories"] = w.territories; j["entities"] = w.entities; j["conflicts"] = w.conflicts; j["commitments"] = w.commitments;
    j["regional_politics"] = w.regionalPolitics; j["activity"] = w.activity;
    j["turn"] = w.turn;
    j["player"] = w.player;
    return j;
}
WorldState decode(const Json::Value &j) {
    WorldState w;
    w.campaignId = j["campaign_id"].asString();
    for (auto key : {"actors", "territories", "entities", "conflicts", "commitments"})
        if (!j[key].isNull() && !j[key].isObject()) throw ValidationError("Invalid world collection");
    if (j["actors"].isObject()) w.actors=j["actors"];
    if (j["territories"].isObject()) w.territories=j["territories"];
    if (j["entities"].isObject()) w.entities=j["entities"];
    if (j["conflicts"].isObject()) w.conflicts=j["conflicts"];
    if (j["commitments"].isObject()) w.commitments=j["commitments"];
    for(auto key:{"regional_politics","activity"})if(!j[key].isNull()&&!j[key].isObject())throw ValidationError("Invalid political collection");
    if(j["regional_politics"].isObject())w.regionalPolitics=j["regional_politics"];
    if(j["activity"].isObject())w.activity=j["activity"];
    w.turn = j["turn"].asInt();
    w.player = j["player"].asString();
    return w;
}
Json::Value encode(const TurnOrders &o) {
    Json::Value j(Json::arrayValue);
    for (auto &v : o) {
        Json::Value x;
        x["text"] = v.text;
        if (!v.targets.empty())
            for (auto &t : v.targets) x["targets"].append(t);
        j.append(x);
    }
    return j;
}
Json::Value encode(const std::vector<ResolvedEvent> &e) {
    Json::Value j(Json::arrayValue);
    for (auto &v : e) {
        Json::Value x;
        x["actor"] = v.actor;
        x["action"] = v.action;
        x["target"] = v.target;
        x["text"] = v.text;
        j.append(x);
    }
    return j;
}
TurnOrders parseOrders(const Json::Value &j) {
    if (!j.isArray() || j.size() > maxActions)
        throw ValidationError("Actions must be an array of at most 8 entries");
    TurnOrders o;
    for (auto &x : j) {
        if (!x.isObject() || !x["text"].isString())
            throw ValidationError("Each action requires text");
        for (auto &k : x.getMemberNames())
            if (k != "text" && k != "targets")
                throw ValidationError("Unknown action field: " + k);
        Order order{x["text"].asString(), {}};
        if (x.isMember("targets")) {
            if (!x["targets"].isArray() || x["targets"].size() > maxTargets)
                throw ValidationError("targets must be an array of at most 4 IDs");
            for (auto &t : x["targets"]) {
                if (!t.isString()) throw ValidationError("Target IDs must be strings");
                order.targets.push_back(t.asString());
            }
        }
        o.push_back(order);
    }
    return o;
}
void validate(const WorldState &w, const std::string &a, const TurnOrders &o) {
    if (!w.hasActor(a) || o.size() > maxActions)
        throw ValidationError("Invalid actor or action limit");
    std::set<std::string> seen;
    for (auto &x : o) {
        auto blank = x.text.find_first_not_of(" \t\r\n") == std::string::npos;
        if (blank || x.text.size() > maxActionBytes)
            throw ValidationError("Each action needs 1-500 bytes of text");
        if (!seen.insert(x.text).second)
            throw ValidationError("Duplicate action");
        for (auto &t : x.targets)
            if (!w.territories.isMember(t) && !w.entities.isMember(t) && !w.hasActor(t))
                throw ValidationError("Unknown action target: " + t);
    }
}
Resolution resolve(const WorldState &w) {
    if (w.turn >= 12000)
        throw ValidationError("Campaign turn limit reached");
    Resolution r{w, {}};
    r.state.turn++;
    for (auto &id : r.state.entities.getMemberNames()) {
        auto &e = r.state.entities[id];
        // An event site no later event renewed leaves the map without a journal line.
        if (e["status"] == "active" && e.isMember("expires_turn") && e["expires_turn"].isInt() && e["expires_turn"].asInt() <= r.state.turn) {
            e["status"] = "inactive";
            e.removeMember("expires_turn");
            continue;
        }
        if (e["status"] != "under_construction" || !e["completes_turn"].isInt() || e["completes_turn"].asInt() > r.state.turn)
            continue;
        e["status"] = "active";
        e.removeMember("completes_turn");
        auto region = w.territories[e["region"].asString()]["name"].asString();
        r.events.push_back({e["actor"].asString(), "completion", id,
                            e["name"].asString() + " (" + e["kind"].asString() + ") was completed" +
                                (region.empty() ? "" : " in " + region) + "."});
    }
    return r;
}
static bool involves(const Json::Value &v, const std::string &a) {
    for (auto &x : v["actors"])
        if (x == a) return true;
    return false;
}
CountryContext context(const WorldState &w, const std::string &a, const Json::Value &recent,
                       const Json::Value &reference) {
    Json::Value f;
    f["actor"] = a;
    f["actor_name"] = w.actors[a]["name"];
    f["turn"] = w.turn;
    // Compact territory facts: large countries have dozens of regions.
    f["territories"] = Json::objectValue;
    f["occupied_by_others"] = Json::objectValue;
    f["occupying"] = Json::objectValue;
    for (auto &id : w.territories.getMemberNames()) {
        auto &t = w.territories[id];
        if (t["owner"] == a && t["controller"] == a) f["territories"][id] = t["name"];
        else if (t["owner"] == a) { f["occupied_by_others"][id]["name"] = t["name"]; f["occupied_by_others"][id]["controller"] = t["controller"]; }
        else if (t["controller"] == a) { f["occupying"][id]["name"] = t["name"]; f["occupying"][id]["owner"] = t["owner"]; }
    }
    f["entities"] = Json::objectValue;
    for (auto &id : w.entities.getMemberNames())
        if (w.entities[id]["actor"] == a && w.entities[id]["status"] != "destroyed") {
            auto e = w.entities[id];
            for (auto k : {"longitude", "latitude", "actor", "origin_event"}) e.removeMember(k);
            f["entities"][id] = e;
        }
    // The nation's size, wealth, stability and wars ground every decision, adjudication and narration.
    f["status"] = nationalStatus(w, a);
    for (auto kind : {"conflicts", "commitments"}) {
        bool conflict = std::string(kind) == "conflicts";
        auto &collection = conflict ? w.conflicts : w.commitments;
        f[kind] = Json::objectValue;
        for (auto &id : collection.getMemberNames())
            if (collection[id]["status"] == "open" && involves(collection[id], a)) f[kind][id] = describeAgreement(w, conflict ? "conflict" : "commitment", id);
    }
    // Wars and treaties already appear in full above.
    f["status"].removeMember("wars");
    f["status"].removeMember("treaties");
    f["recent_events"] = Json::arrayValue;
    for (const auto &month : recent)
        for (const auto &event : month["events"])
            if ((event["actor"] == a || event["target"] == a) && f["recent_events"].size() < 6)
                f["recent_events"].append(event["text"]);
    Json::Value ref = reference;
    if (ref.isNull()) { ref["available"]=false; ref["reason"]="Not prepared"; }
    return {a, f, ref};
}
} // namespace lh
