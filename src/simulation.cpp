#include "localhistoria/simulation.h"
#include <algorithm>
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
std::string pairKey(std::string a, std::string b) { return a < b ? a + ":" + b : b + ":" + a; }
Json::Value encode(const WorldState &w) {
    Json::Value j;
    j["campaign_id"] = w.campaignId;
    j["actors"] = w.actors; j["territories"] = w.territories; j["entities"] = w.entities; j["conflicts"] = w.conflicts; j["commitments"] = w.commitments;
    j["turn"] = w.turn;
    j["player"] = w.player;
    j["countries"] = Json::objectValue;
    for (auto &[k, c] : w.countries) {
        auto &v = j["countries"][k];
        v["treasury"] = c.treasury;
        v["income"] = c.income;
        v["stability"] = c.stability;
        v["army"] = c.army;
        v["navy"] = c.navy;
        v["posture"] = c.posture;
        v["priority"] = c.priority;
    }
    j["relations"] = Json::objectValue;
    for (auto &[k, v] : w.relations)
        j["relations"][k] = v;
    for (auto key : {"alliances", "offers"})
        j[key] = Json::arrayValue;
    for (auto &v : w.alliances)
        j["alliances"].append(v);
    for (auto &v : w.offers)
        j["offers"].append(v);
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
    w.turn = j["turn"].asInt();
    w.player = j["player"].asString();
    for (auto &k : j["countries"].getMemberNames()) {
        auto &v = j["countries"][k];
        w.countries[k] = {v["treasury"].asInt(),   v["income"].asInt(), v["stability"].asInt(),
                          v["army"].asInt(),       v["navy"].asInt(),   v["posture"].asString(),
                          v["priority"].asString()};
    }
    for (auto &k : j["relations"].getMemberNames())
        w.relations[k] = j["relations"][k].asInt();
    for (auto &v : j["alliances"])
        w.alliances.insert(v.asString());
    for (auto &v : j["offers"])
        w.offers.insert(v.asString());
    return w;
}
Json::Value encode(const TurnOrders &o) {
    Json::Value j(Json::arrayValue);
    for (auto &v : o) {
        Json::Value x;
        x["action"] = v.action;
        if (!v.target.empty())
            x["target"] = v.target;
        if (!v.value.empty())
            x["value"] = v.value;
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
    if (!j.isArray() || j.size() > 8)
        throw ValidationError("orders must be an array of at most 8 actions");
    TurnOrders o;
    for (auto &x : j) {
        if (!x.isObject() || !x["action"].isString())
            throw ValidationError("Each order requires an action string");
        for (auto &k : x.getMemberNames())
            if ((k != "action" && k != "target" && k != "value") || !x[k].isString())
                throw ValidationError("Unknown field or non-string order field");
        o.push_back({x["action"].asString(), x.get("target", "").asString(),
                     x.get("value", "").asString()});
    }
    return o;
}
static int cost(const std::string &a) {
    if (a == "economy")
        return 20;
    if (a == "army" || a == "navy")
        return 15;
    if (a == "improve")
        return 5;
    return 0;
}
void validate(const WorldState &w, const std::string &a, const TurnOrders &o) {
    if (!w.countries.contains(a) || o.size() > 8)
        throw ValidationError("Invalid actor or order limit");
    int total = 0;
    std::set<std::string> seen;
    for (auto &x : o) {
        if (x.action == "construction" || x.action == "movement" || x.action == "conflict" || x.action == "territorial_diplomacy") {
            if (x.value.empty() || x.value.size()>500 || (!w.territories.isMember(x.target) && !w.entities.isMember(x.target) && !w.countries.contains(x.target)))
                throw ValidationError("World intention requires an existing target and 1-500 byte description");
            if (!seen.insert(x.action+":"+x.target).second) throw ValidationError("Duplicate intention");
            continue;
        }
        bool diplomatic = x.action == "improve" || x.action == "offer" || x.action == "accept" ||
                          x.action == "reject";
        if (!diplomatic && x.action != "economy" && x.action != "army" && x.action != "navy" &&
            x.action != "posture" && x.action != "priority")
            throw ValidationError("Unknown action");
        if (diplomatic) {
            if (x.target == a || !w.countries.contains(x.target) || !x.value.empty())
                throw ValidationError("Invalid diplomatic target or value");
        } else if (!x.target.empty())
            throw ValidationError("Domestic actions cannot have targets");
        if (!seen.insert((x.action == "accept" || x.action == "reject" ? "respond" : x.action) +
                         ":" + x.target)
                 .second)
            throw ValidationError("Duplicate action");
        if (x.action == "posture") {
            if (x.value != "defensive" && x.value != "neutral" && x.value != "assertive")
                throw ValidationError("Invalid posture");
        } else if (x.action == "priority") {
            if (x.value != "balanced" && x.value != "prosperity" && x.value != "welfare")
                throw ValidationError("Invalid priority");
        } else if (!x.value.empty())
            throw ValidationError("Unexpected value");
        auto key = pairKey(a, x.target);
        if (x.action == "offer" &&
            (w.alliances.contains(key) || w.offers.contains(a + ":" + x.target) ||
             w.offers.contains(x.target + ":" + a)))
            throw ValidationError("Alliance or offer already exists");
        if ((x.action == "accept" || x.action == "reject") &&
            !w.offers.contains(x.target + ":" + a))
            throw ValidationError("No incoming offer");
        total += cost(x.action);
    }
    if (total > w.countries.at(a).treasury)
        throw ValidationError("Orders exceed starting treasury");
}
Resolution resolve(const WorldState &w, const std::map<std::string, TurnOrders> &intentions) {
    if (w.turn >= 12000)
        throw ValidationError("Campaign turn limit reached");
    for (auto &[a, o] : intentions)
        validate(w, a, o);
    Resolution r{w, {}};
    for (auto &[a, c] : r.state.countries) {
        c.treasury =
            std::min(1000000, c.treasury + c.income + (c.priority == "prosperity" ? 1 : 0));
        if (c.priority == "welfare")
            c.stability = std::min(100, c.stability + 1);
    }
    for (auto &[a, o] : intentions) {
        auto sorted = o;
        std::sort(sorted.begin(), sorted.end(), [](auto &x, auto &y) {
            return std::tie(x.action, x.target, x.value) < std::tie(y.action, y.target, y.value);
        });
        for (auto &x : sorted) {
            if (x.action == "construction" || x.action == "movement" || x.action == "conflict" || x.action == "territorial_diplomacy") continue;
            auto &c = r.state.countries.at(a);
            c.treasury -= cost(x.action);
            auto key = pairKey(a, x.target);
            if (x.action == "economy")
                c.income = std::min(10000, c.income + 2);
            if (x.action == "army")
                c.army = std::min(10000, c.army + 3);
            if (x.action == "navy")
                c.navy = std::min(10000, c.navy + 3);
            if (x.action == "improve")
                r.state.relations[key] = std::min(100, r.state.relations[key] + 10);
            if (x.action == "posture")
                c.posture = x.value;
            if (x.action == "priority")
                c.priority = x.value;
            if (x.action == "offer")
                r.state.offers.insert(a + ":" + x.target);
            if (x.action == "accept" || x.action == "reject") {
                r.state.offers.erase(x.target + ":" + a);
                if (x.action == "accept") {
                    r.state.alliances.insert(key);
                    r.state.offers.erase(a + ":" + x.target);
                }
            }
            r.events.push_back({a, x.action, x.target,
                                a + " completed " + x.action +
                                    (x.target.empty() ? "" : " toward " + x.target) +
                                    (x.value.empty() ? "" : " (" + x.value + ")") + "."});
        }
    }
    r.state.turn++;
    r.events.push_back({"world", "administration", "",
                        "Monthly income collected by all actors; welfare priorities improve "
                        "stability and prosperity adds one treasury."});
    return r;
}
CountryContext context(const WorldState &w, const std::string &a, const Json::Value &recent,
                       const Json::Value &reference) {
    Json::Value f;
    f["actor"] = a;
    f["turn"] = w.turn;
    f["state"] = encode(w)["countries"][a];
    f["relations"] = Json::objectValue;
    for (auto &[k, v] : w.relations)
        if (k.substr(0, k.find(':')) == a || k.substr(k.find(':') + 1) == a)
            f["relations"][k] = v;
    for (auto &k : w.offers)
        if (k.substr(0, k.find(':')) == a || k.substr(k.find(':') + 1) == a)
            f["offers"].append(k);
    for (auto &k : w.alliances)
        if (k.substr(0, k.find(':')) == a || k.substr(k.find(':') + 1) == a)
            f["alliances"].append(k);
    f["recent_events"] = Json::arrayValue;
    for (const auto &month : recent) {
        for (const auto &event : month["events"]) {
            if ((event["actor"] == a || event["target"] == a) && f["recent_events"].size() < 6)
                f["recent_events"].append(event);
        }
    }
    f["available_targets"] = Json::arrayValue;
    for (const auto &[code, country] : w.countries)
        if (code != a)
            f["available_targets"].append(code);
    f["objective"] = "Maintain stability and peaceful prosperity";
    f["available_actions"] = "economy 20, army/navy 15, improve 5, offer/accept/reject 0, posture "
                             "defensive/neutral/assertive, priority balanced/prosperity/welfare";
    f["commitments"] = w.commitments;
    f["territories"] = Json::objectValue;
    for (auto &id : w.territories.getMemberNames())
        if (w.territories[id]["owner"] == a || w.territories[id]["controller"] == a)
            f["territories"][id] = w.territories[id];
    f["entities"] = Json::objectValue;
    for (auto &id : w.entities.getMemberNames())
        if (w.entities[id]["actor"] == a) f["entities"][id] = w.entities[id];
    f["conflicts"] = w.conflicts;
    Json::Value ref = reference;
    if (ref.isNull()) { ref["available"]=false; ref["reason"]="Not prepared"; }
    return {a, f, ref};
}
} // namespace lh
