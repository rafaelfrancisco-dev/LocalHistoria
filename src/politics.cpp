#include "localhistoria/politics.h"
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
    for (auto &r : w.territories.getMemberNames()) {
        auto &t = w.territories[r];
        auto a = t["controller"].asString();
        // Residual geometry has no politics; a single-territory country's politics are national.
        if (t["kind"] != "residual" && t["kind"] != "single" && w.hasActor(a))
            regions[a].push_back(r);
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
    // Choose authority before region: large countries receive no extra tickets. Only the player and
    // close partners are preferred; great powers get no regional advantage.
    std::vector<std::string> governors;
    for (auto &[a, rs] : regions)
        governors.push_back(a);
    rank(governors, "governor", [&](auto &id) { return tier(id) >= 2 ? tier(id) : 0; });
    auto none = [](auto &) { return 0; };
    for (size_t round = 0; slots.size() < 3 && !governors.empty(); ++round) {
        bool found = false;
        for (auto &a : governors) {
            auto rs = regions[a];
            rank(rs, "region" + std::to_string(round), none);
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
    p["agenda"] = "Local civic wellbeing and representation";
    p["demands"] = Json::arrayValue;
    p["relations"] = "neutral";
    p["supporting_events"] = Json::arrayValue;
    return p;
}
} // namespace lh
