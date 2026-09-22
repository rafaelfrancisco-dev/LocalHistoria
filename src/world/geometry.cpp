#include "localhistoria/status.h"
#include "localhistoria/world.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <map>
namespace lh {
static bool ring(const Json::Value &r, double x, double y) {
    bool inside = false;
    if (r.size() < 4)
        return false;
    for (Json::ArrayIndex i = 0, j = r.size() - 1; i < r.size(); j = i++) {
        double ax = r[i][0].asDouble(), ay = r[i][1].asDouble(), bx = r[j][0].asDouble(),
               by = r[j][1].asDouble();
        double cross = (x - ax) * (by - ay) - (y - ay) * (bx - ax);
        if (std::abs(cross) < 1e-10 && x >= std::min(ax, bx) && x <= std::max(ax, bx) &&
            y >= std::min(ay, by) && y <= std::max(ay, by))
            return true;
        if ((ay > y) != (by > y) && x < (bx - ax) * (y - ay) / (by - ay) + ax)
            inside = !inside;
    }
    return inside;
}
bool containsPoint(const Json::Value &g, double x, double y) {
    if (!std::isfinite(x) || !std::isfinite(y) || x < -180 || x > 180 || y < -90 || y > 90)
        return false;
    auto polygon = [&](const Json::Value &p) {
        if (p.empty() || !ring(p[0], x, y))
            return false;
        for (Json::ArrayIndex i = 1; i < p.size(); ++i)
            if (ring(p[i], x, y))
                return false;
        return true;
    };
    if (g["type"] == "Polygon")
        return polygon(g["coordinates"]);
    if (g["type"] == "MultiPolygon")
        for (auto &p : g["coordinates"])
            if (polygon(p))
                return true;
    return false;
}
bool containsWord(const std::string &text, const std::string &word) {
    for (auto at = text.find(word); at != std::string::npos; at = text.find(word, at + 1)) {
        auto boundary = [&](size_t i) {
            return i >= text.size() || !std::isalnum(static_cast<unsigned char>(text[i]));
        };
        if ((at == 0 || boundary(at - 1)) && boundary(at + word.size()))
            return true;
    }
    return false;
}
bool consequential(const std::string &raw) {
    static const std::vector<std::string> stems{
        "invad",      "invasion", "annex",     "occup",      "troop",     "militar",   "mobili",
        "armies",     "fleet",    "naval",     "border",     "offensive", "attack",    "bombard",
        "treaty",     "treaties", "alliance",  "allied",     "accord",    "agreement", "ceasefire",
        "cease-fire", "peace",    "armistice", "independen", "secession", "seced",     "autonom",
        "sanction",   "blockade", "embargo",   "territor",   "sovereign", "rebel",     "insurg"};
    static const std::vector<std::string> words{"war",  "wars", "pact", "army",
                                                "navy", "coup", "cede", "union"};
    auto text = lowercase(raw);
    for (auto &stem : stems)
        if (text.find(stem) != std::string::npos)
            return true;
    for (auto &word : words)
        if (containsWord(text, word))
            return true;
    return false;
}
bool ceremonial(const std::string &raw) {
    static const std::vector<std::string> gestures{
        "town hall",    "heritage",       "ceremon",         "festival", "commemorat", "awareness",
        "celebrat",     "symbolic",       "monument",        "mural",    "anniversar", "beautif",
        "public forum", "public hearing", "civic wellbeing", "outreach", "signage",    "proclaim",
        "commute",      "transit",        "tourism",         "recycl",   "clean-up"};
    // A gesture that also names something at stake is a real action wearing a civic hat.
    static const std::vector<std::string> substance{
        "budget",   "revenue", "tax",     "levy",   "demand",  "refugee",   "displac",
        "garrison", "evacuat", "blockad", "strike", "protest", "election",  "quota",
        "tariff",   "mining",  "oil",     "water",  "port",    "ultimatum", "conscript"};
    auto text = lowercase(raw);
    bool gesture = false;
    for (auto &stem : gestures)
        gesture = gesture || text.find(stem) != std::string::npos;
    if (!gesture || consequential(raw))
        return false;
    for (auto &stem : substance)
        if (text.find(stem) != std::string::npos)
            return false;
    return true;
}
std::string attackGuidance(const std::string &actor, const std::string &text,
                           const Json::Value &territories) {
    static const std::vector<std::string> stems{"invad",   "occup", "attack", "offensive",
                                                "conquer", "seize", "annex",  "bombard"};
    auto lowered = lowercase(text);
    bool attack = false;
    for (auto &stem : stems)
        attack = attack || lowered.find(stem) != std::string::npos;
    if (!attack)
        return "";
    if (territories.isObject())
        for (auto &id : territories.getMemberNames()) {
            auto &t = territories[id];
            if (t["controller"] == actor)
                continue;
            for (auto &a : t["within_reach_of"])
                if (a.isString() &&
                    (a.asString() == actor || a.asString().rfind(actor + " (", 0) == 0) &&
                    a.asString().find("too weak") == std::string::npos)
                    return " [attack: use occupy on one or two regions whose within_reach_of "
                           "lists " +
                           actor +
                           ", plus open_conflict unless already at war; never move_entity on "
                           "cities]";
        }
    return " [no region within reach: describe preparations, no occupy]";
}
int constructionMonths(const std::string &kind) {
    static const std::map<std::string, int> months{{"army", 2}, {"fleet", 4}, {"base", 3},
                                                   {"port", 6}, {"city", 24}, {"event_site", 0}};
    return months.contains(kind) ? months.at(kind) : 0;
}
// Qualitative, mid-lightness colours that stay apart on the dark map and from the gold selection.
static const std::array<const char *, 24> actorPalette{
    "#3b6fb6", "#c8553d", "#4f9d69", "#8e5ea2", "#d4a017", "#2a9d8f", "#b5485d", "#6b8e23",
    "#c97b2a", "#5b6bbf", "#8c6d46", "#3f8fb5", "#7fa7e0", "#e8998d", "#8fd19e", "#c3a1d6",
    "#7d8fa0", "#7fcdc3", "#e39bb0", "#b5c96a", "#f0b27a", "#a3acec", "#6f9a8d", "#9fd3e8"};
static size_t paletteIndex(const std::string &id) {
    unsigned hash = 0;
    for (unsigned char ch : id)
        hash = hash * 31 + ch;
    return hash % actorPalette.size();
}
std::string paletteColor(const std::string &id) { return actorPalette[paletteIndex(id)]; }
void assignActorColors(WorldState &w,
                       const std::vector<std::pair<std::string, std::string>> &neighbors) {
    std::map<std::string, std::set<std::string>> borders;
    for (auto &[a, b] : neighbors) {
        borders[a].insert(b);
        borders[b].insert(a);
    }
    // Greedy in ID order: each actor avoids the colours of the neighbours already coloured.
    std::set<std::string> done;
    for (auto &id : w.actors.getMemberNames()) {
        std::set<std::string> taken;
        for (auto &n : borders[id])
            if (done.contains(n))
                taken.insert(w.actors[n]["color"].asString());
        auto start = paletteIndex(id), pick = start;
        for (size_t i = 0; i < actorPalette.size(); ++i)
            if (!taken.contains(actorPalette[(start + i) % actorPalette.size()])) {
                pick = (start + i) % actorPalette.size();
                break;
            }
        w.actors[id]["color"] = actorPalette[pick];
        done.insert(id);
    }
}
bool adjacent(const Adjacency &adjacency, const std::string &from, const std::string &to) {
    return from == to || (adjacency.contains(from) && adjacency.at(from).contains(to));
}
bool reaches(const WorldState &w, const Adjacency &adjacency, const std::string &a,
             const std::string &r) {
    // Reach is judged from the start of the month: controlled land (residual slivers excepted) or
    // active armies and fleets.
    for (auto &id : w.territories.getMemberNames()) {
        auto &t = w.territories[id];
        if (t["controller"] == a && t["kind"] != "residual" && adjacent(adjacency, id, r))
            return true;
    }
    for (auto &id : w.entities.getMemberNames()) {
        auto &e = w.entities[id];
        if (e["actor"] == a && e["status"] == "active" &&
            (e["kind"] == "army" || e["kind"] == "fleet") &&
            adjacent(adjacency, e["region"].asString(), r))
            return true;
    }
    return false;
}
} // namespace lh
