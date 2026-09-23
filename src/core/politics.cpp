#include "localhistoria/politics.h"
#include "localhistoria/conflicts.h"
#include <algorithm>
#include <cstdio>
namespace lh {
bool identityCorrection(const std::string &a, const std::string &b) {
    auto norm = [](auto &s) {
        std::string n;
        for (unsigned char c : s)
            if (std::isalnum(c))
                n += char(std::tolower(c));
        return n;
    };
    auto x = norm(a), y = norm(b);
    std::vector<unsigned> cost(y.size() + 1);
    for (unsigned j = 0; j <= y.size(); ++j)
        cost[j] = j;
    for (unsigned i = 1; i <= x.size(); ++i) {
        unsigned diagonal = cost[0];
        cost[0] = i;
        for (unsigned j = 1; j <= y.size(); ++j) {
            auto prior = cost[j];
            cost[j] = std::min({cost[j] + 1, cost[j - 1] + 1, diagonal + (x[i - 1] != y[j - 1])});
            diagonal = prior;
        }
    }
    return cost.back() <= 1;
}
uint64_t stableHash(const std::string &s) {
    uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    // Avalanche the short actor-ID suffix; adjacent ISO codes must not cluster in rankings.
    h ^= h >> 30;
    h *= 0xbf58476d1ce4e5b9ULL;
    h ^= h >> 27;
    h *= 0x94d049bb133111ebULL;
    return h ^ (h >> 31);
}
// What a region has at stake this month. A slot spent on a quiet district is a decision call and an
// adjudicated outcome that change nothing, so fronts, occupied land and unanswered demands come
// first. Only what is happening inside the region counts: who governs it is the governor ranking's
// business, and counting it twice would undo the country balance there. Derived from the state
// alone, so the ranking stays deterministic.
static int regionStake(const WorldState &w, const std::string &r) {
    const auto &t = w.territories[r];
    int stake = 0;
    auto raise = [&](int n) { stake = std::max(stake, n); };
    if (t[key::owner] != t[key::controller])
        raise(3);
    // A region the engine has marked as restive outranks a quiet one: whatever is boiling is
    // exactly what the month's one regional voice should be spent on.
    if (t["unrest"].isNumeric() && t["unrest"].asDouble() >= 35)
        raise(t["unrest"].asDouble() >= 55 ? 3 : 2);
    for (const auto &cid : w.conflicts.getMemberNames()) {
        const auto &c = w.conflicts[cid];
        if (!isOpen(c))
            continue;
        for (const auto &front : c["affected_regions"])
            if (front == r)
                raise(3);
    }
    if (w.regionalPolitics.isMember(r)) {
        const auto &p = w.regionalPolitics[r];
        for (const auto &d : p["demands"])
            if (d[key::status] == "pending")
                raise(2);
        if (p["relations"] == "strained")
            raise(2);
        if (!p["supporting_events"].empty())
            raise(1);
    }
    return stake;
}
Json::Value scheduleInitiatives(const WorldState &w, const std::set<std::string> &reactive,
                                const std::map<std::string, int> &closeness,
                                const std::set<std::string> &powers) {
    auto seed = w.campaignId + ":" + std::to_string(w.turn + 1);
    auto recent = [&](const std::string &key) {
        return w.activity[key].isInt() && w.turn + 1 - w.activity[key].asInt() <= 2;
    };
    auto tier = [&](const std::string &id) {
        auto it = closeness.find(id);
        return it == closeness.end() ? 0 : it->second;
    };
    // Higher `priority` ranks first, after cooldown and reactive preferences.
    auto rank = [&](std::vector<std::string> &v, const std::string &scope, auto priority) {
        std::sort(v.begin(), v.end(), [&](auto &a, auto &b) {
            auto score = [&](auto &id) {
                return std::make_tuple(recent(id), reactive.contains(id), -priority(id),
                                       stableHash(seed + scope + id), id);
            };
            return score(a) < score(b);
        });
    };
    std::vector<std::string> nations;
    std::map<std::string, std::vector<std::string>> regions;
    for (auto &a : w.actors.getMemberNames())
        if (a != w.player)
            nations.push_back(a);
    std::map<std::string, int> stakes, bestStake;
    for (auto &r : w.territories.getMemberNames()) {
        auto &t = w.territories[r];
        auto a = t["controller"].asString();
        // Residual geometry has no politics; a single-territory country's politics are national.
        if (t["kind"] != "residual" && t["kind"] != "single" && w.hasActor(a)) {
            regions[a].push_back(r);
            stakes[r] = regionStake(w, r);
            bestStake[a] = std::max(bestStake[a], stakes[r]);
        }
    }
    rank(nations, "national", tier);
    // The first slot is the player's neighbourhood; the second the world stage, a great power,
    // except every third month when a distant nation keeps the wider world alive.
    if ((!closeness.empty() || !powers.empty()) && nations.size() > 2) {
        std::vector<std::string> rest(nations.begin() + 1, nations.end());
        if ((w.turn + 1) % 3 == 0)
            rank(rest, "distant",
                 [&](auto &id) { return tier(id) > 0 || powers.contains(id) ? -1 : 0; });
        else
            rank(rest, "power", [&](auto &id) { return powers.contains(id) ? 1 : 0; });
        std::copy(rest.begin(), rest.end(), nations.begin() + 1);
    }
    Json::Value slots(Json::arrayValue);
    std::set<std::string> selectedRegions;
    auto add = [&](const std::string &a, const std::string &r) {
        Json::Value s;
        s["actor"] = a;
        s["region"] = r;
        s["initiative_type"] = r.empty() ? "independent_national" : "independent_regional";
        s["action_count"] = 1;
        slots.append(s);
    };
    for (size_t i = 0; i < std::min<size_t>(2, nations.size()); ++i)
        add(nations[i], "");
    // Choose authority before region: large countries receive no extra tickets. A governor is
    // preferred for the stake of its weightiest region, then for being the player or a close
    // partner; great powers get no regional advantage.
    std::vector<std::string> governors;
    for (auto &[a, rs] : regions)
        governors.push_back(a);
    auto stake = [&](const std::map<std::string, int> &m, const std::string &id) {
        auto it = m.find(id);
        return it == m.end() ? 0 : it->second;
    };
    // Stake leads, closeness breaks its ties: a war front anywhere is worth more than a quiet
    // province of the player's, and the player's regions still win when nothing is at stake.
    rank(governors, "governor",
         [&](auto &id) { return stake(bestStake, id) * 4 + (tier(id) >= 2 ? tier(id) : 0); });
    // Among a governor's regions, stake outranks the cooldown: a live front's month keeps changing,
    // so returning to it is the point. Equal stakes still rotate on `recent`.
    auto rankRegions = [&](std::vector<std::string> &v, const std::string &scope) {
        std::sort(v.begin(), v.end(), [&](auto &a, auto &b) {
            auto score = [&](auto &id) {
                return std::make_tuple(-stake(stakes, id), recent(id),
                                       stableHash(seed + scope + id), id);
            };
            return score(a) < score(b);
        });
    };
    for (size_t round = 0; slots.size() < 3 && !governors.empty(); ++round) {
        bool found = false;
        for (auto &a : governors) {
            auto rs = regions[a];
            rankRegions(rs, "region" + std::to_string(round));
            for (auto &r : rs)
                if (selectedRegions.insert(r).second) {
                    add(a, r);
                    found = true;
                    break;
                }
            if (slots.size() == 3)
                break;
        }
        if (!found)
            break;
    }
    for (size_t i = 2; slots.size() < 3 && i < nations.size(); ++i)
        add(nations[i], "");
    return slots;
}
Json::Value flagDesignSchema() {
    return parse(
        R"({"type":"object","additionalProperties":false,"required":["layout","palette","emblem",)"
        R"("explanation"],"properties":{"layout":{"type":"string","enum":["horizontal",)"
        R"("vertical","cross","diagonal"]},"palette":{"type":"array","minItems":3,)"
        R"("maxItems":3,"items":{"type":"string","pattern":"^#[0-9a-fA-F]{6}$"}},)"
        R"("emblem":{"type":"string","enum":["none","circle","diamond"]},)"
        R"("explanation":{"type":"string","minLength":1,"maxLength":300}}})");
}
std::string renderFlag(const Json::Value &d) {
    if (!d.isObject() || !d["layout"].isString() || !d["emblem"].isString() ||
        !d["explanation"].isString() || d["explanation"].asString().empty() ||
        d["explanation"].asString().size() > 300 || !d["palette"].isArray() ||
        d["palette"].size() != 3)
        throw ValidationError("Invalid flag design");
    for (auto &k : d.getMemberNames())
        if (k != "layout" && k != "emblem" && k != "palette" && k != "explanation")
            throw ValidationError("Unknown flag property");
    for (auto &c : d["palette"])
        if (!c.isString() || !isHexColor(c.asString()))
            throw ValidationError("Invalid flag color");
    auto a = d["palette"][0].asString(), b = d["palette"][1].asString(),
         c = d["palette"][2].asString();
    std::string s = "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 300 200\"><rect "
                    "width=\"300\" height=\"200\" fill=\"" +
                    a + "\"/>";
    auto rect = [&](int x, int y, int width, int height, const std::string &color) {
        s += "<rect x=\"" + std::to_string(x) + "\" y=\"" + std::to_string(y) + "\" width=\"" +
             std::to_string(width) + "\" height=\"" + std::to_string(height) + "\" fill=\"" +
             color + "\"/>";
    };
    auto layout = d["layout"].asString();
    if (layout == "horizontal") {
        rect(0, 67, 300, 66, b);
        rect(0, 133, 300, 67, c);
    } else if (layout == "vertical") {
        rect(100, 0, 100, 200, b);
        rect(200, 0, 100, 200, c);
    } else if (layout == "cross") {
        rect(0, 75, 300, 50, b);
        rect(85, 0, 50, 200, b);
        rect(0, 90, 300, 20, c);
        rect(100, 0, 20, 200, c);
    } else if (layout == "diagonal")
        s += "<path d=\"M0 200 L300 0 L300 200 Z\" fill=\"" + b +
             "\"/><path d=\"M0 180 L270 0 L300 0 L300 20 L30 200 L0 200 Z\" fill=\"" + c + "\"/>";
    else
        throw ValidationError("Invalid flag layout");
    if (d["emblem"] == "circle")
        s += "<circle cx=\"150\" cy=\"100\" r=\"30\" fill=\"" + c + "\" stroke=\"" + a +
             "\" stroke-width=\"5\"/>";
    else if (d["emblem"] == "diamond")
        s += "<path d=\"M150 60 L190 100 L150 140 L110 100 Z\" fill=\"" + c + "\" stroke=\"" + a +
             "\" stroke-width=\"5\"/>";
    else if (d["emblem"] != "none")
        throw ValidationError("Invalid flag emblem");
    return s + "</svg>";
}
Json::Value makeFlag(const Json::Value &proposal, const std::string &seed, const std::string &event,
                     const std::string &previous) {
    Json::Value d = proposal;
    std::string svg;
    try {
        svg = renderFlag(d);
    } catch (const ValidationError &) {
    }
    for (unsigned n = 0; svg.empty() || svg == previous; ++n) {
        auto h = stableHash(seed + ":" + std::to_string(n));
        d = Json::objectValue;
        d["layout"] = "vertical";
        d["emblem"] = "diamond";
        d["explanation"] =
            "Campaign-specific geometric identity; no historical symbolism asserted.";
        for (int i = 0; i < 3; ++i) {
            char color[8];
            snprintf(color, 8, "#%06x", unsigned((h >> (i * 16)) & 0xffffff));
            d["palette"].append(color);
        }
        svg = renderFlag(d);
    }
    Json::Value r;
    r["specification"] = d;
    r["svg"] = svg;
    r["renderer_version"] = 1;
    r["origin_event"] = event;
    return r;
}
void reviseIdentity(Json::Value &actor, const Json::Value &design, const std::string &seed,
                    const std::string &event) {
    if (!actor["identity_revisions"].isArray()) {
        actor["identity_revisions"] = Json::arrayValue;
        Json::Value initial;
        initial["name"] = actor["name"];
        initial["static_flag"] = actor["static_flag"];
        initial["origin_event"] = "initial";
        if (actor.get("static_flag", Json::nullValue).isString())
            actor["identity_revisions"].append(initial);
    }
    auto &revisions = actor["identity_revisions"];
    auto r = makeFlag(design, seed + std::to_string(revisions.size()), event,
                      revisions.empty() ? "" : revisions[revisions.size() - 1]["svg"].asString());
    r["name"] = actor["name"];
    revisions.append(r);
    actor["flag_revision"] = int(revisions.size() - 1);
}
Json::Value regionalState(const WorldState &w, const std::string &r) {
    if (w.regionalPolitics.isMember(r))
        return w.regionalPolitics[r];
    Json::Value p;
    p["agenda"] = "No standing agenda recorded";
    p["demands"] = Json::arrayValue;
    p["relations"] = "neutral";
    p["supporting_events"] = Json::arrayValue;
    return p;
}
std::vector<ReactiveCandidate> reactiveCandidates(const WorldState &w,
                                                  const std::vector<std::string> &linked,
                                                  const std::set<std::string> &mentioned) {
    std::map<std::string, ReactiveCandidate> out;
    auto consider = [&](const std::string &a, int urgency, int age, int order) {
        if (a == w.player || !w.hasActor(a))
            return;
        auto [it, fresh] = out.try_emplace(a, ReactiveCandidate{a, urgency, age, order});
        auto &c = it->second;
        if (!fresh && (urgency < c.urgency || (urgency == c.urgency && age > c.age))) {
            c.urgency = urgency;
            c.age = age;
        }
        c.order = std::min(c.order, order);
    };
    const int late = int(linked.size());
    // Treaty answers: every nation party to a pending proposal that has not signed, and an author
    // reacting to the player's answer.
    for (const auto &p : w.proposals) {
        if (p["status"] != "pending")
            continue;
        int age = w.turn - p.get("proposed_turn", w.turn).asInt();
        const auto &answers = p["answers"];
        for (const auto &a : p["actors"])
            if (!answers.isObject() || !answers.isMember(a.asString()) || a == p["from"])
                consider(a.asString(), 0, age, late);
        consider(p["from"].asString(), 0, age, late);
    }
    for (size_t i = 0; i < linked.size(); ++i) {
        const auto &a = linked[i];
        if (!w.hasActor(a) || a == w.player)
            continue;
        const auto &status = w.actors[a]["status"];
        int urgency = mentioned.contains(a) ? 4 : 5;
        bool war = false, rising = false;
        for (const auto &c : w.conflicts) {
            if (!isOpen(c) || !isParty(c, a))
                continue;
            for (const auto &p : c["actors"])
                war = war || (p != a && fightingPair(w, c, a, p.asString()));
            rising =
                rising || ((c["kind"] == "civil_war" || c["kind"] == "insurgency") &&
                           c["started_turn"].isInt() && w.turn - c["started_turn"].asInt() <= 3);
        }
        if (fighting(w, w.player, a))
            urgency = 1;
        else if (status.get("collapse_months", 0).asInt() > 0 ||
                 status.get("stability", 50).asDouble() < 20 || rising)
            urgency = 2;
        else if (war)
            urgency = 3;
        consider(a, urgency, 0, int(i));
    }
    std::vector<ReactiveCandidate> list;
    for (auto &[a, c] : out)
        list.push_back(c);
    return list;
}
std::vector<std::string> selectReactive(const std::vector<ReactiveCandidate> &candidates,
                                        const std::map<std::string, int> &lastService, int turn,
                                        size_t cap) {
    auto served = [&](const std::string &a) {
        auto it = lastService.find(a);
        return it == lastService.end() ? -1 : std::min(it->second, turn);
    };
    auto ranked = candidates;
    std::stable_sort(ranked.begin(), ranked.end(), [&](const auto &x, const auto &y) {
        return std::make_tuple(x.urgency, -x.age, served(x.actor), x.order, x.actor) <
               std::make_tuple(y.urgency, -y.age, served(y.actor), y.order, y.actor);
    });
    std::vector<std::string> out;
    for (const auto &c : ranked)
        if (out.size() < cap)
            out.push_back(c.actor);
    return out;
}
} // namespace lh
