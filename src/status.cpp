#include "localhistoria/status.h"
#include "localhistoria/politics.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
namespace lh {
// Monthly rates by effective intensity {low, medium, high}. Calibrated so a high-intensity war against a
// large state costs on the order of Russia–Ukraine 2023–25 (roughly 10k military deaths a month for Russia,
// a few thousand for Ukraine, low thousands of civilians) and front regions shed people like eastern Ukraine
// and Darfur did, while entrenched war economies adapt (Ukraine and Russia kept growing slowly through 2023–25).
// All values are fractions of the relevant population or output per month.
namespace rate {
constexpr double military[] = {0.000001, 0.00002, 0.00008};  // of a party's national population
constexpr double civilian[] = {0.000003, 0.000015, 0.00005}; // of each affected region
constexpr double displacement[] = {0.0003, 0.0015, 0.004};   // outflow from each affected region
constexpr double seizedDisplacement = 0.08;                  // one-off when a region changes hands
constexpr double refugeeShare = 0.35;                        // of the displaced who cross a border
constexpr double returnShare = 0.03;                         // of the displaced who return once it is calm
constexpr double frontGdpLoss[] = {0.001, 0.004, 0.01};
constexpr double partyGdpDrag[] = {0.00005, 0.0002, 0.0005};
constexpr double displacedOutput = 0.5; // share of per-capita output that leaves with displaced people
constexpr double occupationGdpLoss = 0.02, occupationGdpFloor = 0.4;
constexpr double stabilityWar[] = {3, 8, 15}; // lowers the level stability drifts toward
constexpr double stabilityReversion = 0.08, occupiedStability = 25, contractionStability = 5;
constexpr double maxStabilityStep = 10, maxEffectGdp = 1.5, maxEffectStability = 8;
constexpr size_t maxAffected = 12;
} // namespace rate
namespace {
using Int = Json::Int64;
Int integer(const Json::Value &v, const char *k) { return v[k].isNumeric() ? Int(v[k].asInt64()) : 0; }
double number(const Json::Value &v, const char *k, double fallback = 0) { return v[k].isNumeric() ? v[k].asDouble() : fallback; }
bool involves(const Json::Value &v, const std::string &a) { return std::find(v["actors"].begin(), v["actors"].end(), Json::Value(a)) != v["actors"].end(); }
int level(const std::string &intensity) { return intensity == "high" ? 2 : intensity == "medium" ? 1 : intensity == "low" ? 0 : -1; }
double round1(double v) { return std::round(v * 10) / 10; }
std::string lower(std::string s) { for (auto &c : s) c = char(std::tolower(static_cast<unsigned char>(c))); return s; }
std::string grouped(Int n) {
    auto s = std::to_string(n < 0 ? -n : n);
    for (int i = int(s.size()) - 3; i > 0; i -= 3) s.insert(size_t(i), ",");
    return (n < 0 ? "-" : "") + s;
}
std::string scaled(double v) {
    char b[64];
    double a = std::fabs(v);
    if (a >= 1e12) snprintf(b, sizeof b, "%.2f trillion", v / 1e12);
    else if (a >= 1e9) snprintf(b, sizeof b, "%.1f billion", v / 1e9);
    else if (a >= 1e6) snprintf(b, sizeof b, "%.1f million", v / 1e6);
    else return grouped(Int(std::llround(v)));
    return b;
}
std::string people(Int n) { return n >= 1000000 ? scaled(double(n)) : grouped(n); }
std::string stabilityLabel(double s) {
    return s < 20 ? "collapsing" : s < 35 ? "unstable" : s < 50 ? "fragile" : s < 70 ? "stable" : s < 85 ? "very stable" : "highly stable";
}
// Largest-remainder split of an amount across weighted targets, in stable ID order.
std::map<std::string, Int> distribute(Int amount, const std::map<std::string, double> &weights) {
    std::map<std::string, Int> out;
    if (amount <= 0 || weights.empty()) return out;
    double total = 0;
    for (auto &[k, w] : weights) total += w;
    std::vector<std::pair<double, std::string>> remainders;
    Int given = 0;
    for (auto &[k, w] : weights) {
        double share = total > 0 ? amount * w / total : double(amount) / double(weights.size());
        out[k] = Int(share);
        given += out[k];
        remainders.emplace_back(share - double(out[k]), k);
    }
    std::stable_sort(remainders.begin(), remainders.end(), [](auto &a, auto &b) { return a.first > b.first; });
    for (size_t i = 0; given < amount && i < remainders.size(); ++i, ++given) out[remainders[i].second]++;
    return out;
}
struct Totals {
    Int population = 0, gdp = 0, displaced = 0;
    int occupied = 0, occupying = 0;
};
std::map<std::string, Totals> totals(const WorldState &w) {
    std::map<std::string, Totals> out;
    for (auto &id : w.territories.getMemberNames()) {
        auto &t = w.territories[id];
        auto owner = t["owner"].asString(), controller = t["controller"].asString();
        auto &o = out[owner];
        o.population += integer(t, "population");
        o.gdp += integer(t, "gdp_usd");
        o.displaced += integer(t, "displaced");
        if (owner != controller) { o.occupied++; out[controller].occupying++; }
    }
    return out;
}
Json::Value buildStatus(const WorldState &w, const std::string &a, const Totals &t) {
    Json::Value s;
    auto &status = w.actors[a]["status"];
    s["population"] = t.population;
    s["population_text"] = people(t.population);
    s["gdp_usd"] = t.gdp;
    s["occupied_regions"] = t.occupied;
    s["occupying_regions"] = t.occupying;
    s["displaced"] = t.displaced;
    if (status.isObject()) {
        s["gdp"] = formatMoney(double(t.gdp), status);
        s["gdp_per_capita"] = t.population > 0 ? "US$" + grouped(Int(std::llround(double(t.gdp) / double(t.population)))) : "n/a";
        auto score = number(status, "stability", 50);
        char b[32];
        snprintf(b, sizeof b, "%.0f", score);
        s["stability"] = stabilityLabel(score) + " (" + b + "/100)";
        s["stability_score"] = round1(score);
        auto change = number(status, "last_gdp_change");
        snprintf(b, sizeof b, "%+.2f%%", change);
        s["economic_trend"] = std::string(status.isMember("last_gdp_change") ? (change < -1 ? "contracting sharply" : change < -0.05 ? "contracting" : change <= 0.05 ? "stagnant" : change <= 0.4 ? "growing" : "growing fast") + std::string(" (") + b + " last month)"
                                                                            : (number(status, "gdp_growth") < 0 ? "contracting" : "growing") + std::string(" (trend ") + [&] { char t[16]; snprintf(t, sizeof t, "%+.1f%%", number(status, "gdp_growth")); return std::string(t); }() + " a year)");
        s["currency"] = status["currency"];
        s["estimated"] = status.get("estimated", false).asBool();
    } else
        s["available"] = false;
    s["wars"] = Json::arrayValue;
    for (auto &id : w.conflicts.getMemberNames()) {
        auto &c = w.conflicts[id];
        if (c["status"] != "open" || !involves(c, a)) continue;
        Json::Value war;
        war["id"] = id;
        war["name"] = displayName(w, "conflict", id);
        war["intensity"] = c.get("intensity", "unknown");
        if (c["started_turn"].isInt()) war["since"] = campaignDate(c["started_turn"].asInt());
        war["casualties"] = integer(c, "casualties");
        war["against"] = Json::arrayValue;
        for (auto &p : c["actors"]) if (p != a && war["against"].size() < 4) war["against"].append(w.actors[p.asString()]["name"]);
        s["wars"].append(war);
    }
    s["treaties"] = Json::arrayValue;
    for (auto &id : w.commitments.getMemberNames()) {
        auto &c = w.commitments[id];
        if (c["status"] != "open" || !involves(c, a)) continue;
        Json::Value treaty;
        treaty["id"] = id;
        treaty["name"] = displayName(w, "commitment", id);
        treaty["kind"] = c.get("kind", "other");
        s["treaties"].append(treaty);
    }
    return s;
}
std::string names(const WorldState &w, const std::vector<std::string> &parties) {
    std::string out;
    for (size_t i = 0; i < parties.size() && i < 3; ++i) {
        auto n = w.actors[parties[i]]["name"].asString();
        out += (i ? "–" : "") + (n.empty() ? parties[i] : n);
    }
    return out;
}
std::string fallbackName(const WorldState &w, const std::string &kind, const std::string &type, const std::vector<std::string> &parties) {
    auto pair = names(w, parties);
    auto first = parties.empty() ? std::string("Unknown") : w.actors[parties[0]].get("name", parties[0]).asString();
    auto last = parties.size() < 2 ? first : w.actors[parties.back()].get("name", parties.back()).asString();
    if (kind == "conflict") {
        if (type == "civil_war") return "Civil War in " + first;
        if (type == "insurgency") return last + " Insurgency";
        if (type == "border_conflict") return pair + " Border Conflict";
        return pair + " War";
    }
    if (type == "peace_treaty") {
        // Treaties are named after where they are signed: the acting party's most populous city region.
        std::string city;
        Int best = -1;
        for (auto &id : w.entities.getMemberNames()) {
            auto &e = w.entities[id];
            if (parties.empty() || e["actor"] != parties[0] || e["kind"] != "city" || e["status"] != "active") continue;
            auto population = integer(w.territories[e["region"].asString()], "population");
            if (population > best) { best = population; city = e["name"].asString(); }
        }
        return "Treaty of " + (city.empty() ? first : city);
    }
    static const std::map<std::string, std::string> suffix{{"alliance", "Alliance"}, {"defence_pact", "Defence Pact"}, {"non_aggression_pact", "Non-Aggression Pact"}, {"trade_agreement", "Trade Agreement"}, {"ceasefire", "Ceasefire"}, {"union", "Union"}};
    return pair + " " + (suffix.contains(type) ? suffix.at(type) : "Accord");
}
std::vector<std::string> parties(const Json::Value &v) {
    std::vector<std::string> out;
    for (auto &a : v["actors"]) out.push_back(a.asString());
    return out;
}
double jitter(const WorldState &w, const std::string &key) {
    return 0.7 + 0.6 * double(stableHash(w.campaignId + ":" + std::to_string(w.turn) + ":" + key) % 10000) / 10000.0;
}
} // namespace

bool validConflictKind(const std::string &k) { return k == "war" || k == "border_conflict" || k == "insurgency" || k == "civil_war"; }
bool validCommitmentKind(const std::string &k) {
    return k == "alliance" || k == "defence_pact" || k == "non_aggression_pact" || k == "trade_agreement" || k == "ceasefire" || k == "peace_treaty" || k == "union" || k == "other";
}
bool validIntensity(const std::string &k) { return k == "none" || k == "low" || k == "medium" || k == "high"; }
bool validEffect(const std::string &k) { return k == "severe_decline" || k == "decline" || k == "none" || k == "growth" || k == "strong_growth"; }

Json::Value encode(const StatusSeed &s) {
    Json::Value j;
    j["countries"] = s.countries;
    j["regions"] = s.regions;
    j["world"] = s.world;
    return j;
}
StatusSeed decodeSeed(const Json::Value &j) {
    if (!j.isObject() || !j["countries"].isObject() || !j["regions"].isObject()) throw ValidationError("Invalid status seed");
    return {j["countries"], j["regions"], j["world"]};
}
bool hasStatus(const WorldState &w) {
    for (auto &id : w.actors.getMemberNames())
        if (w.actors[id].isMember("status")) return true;
    return false;
}
void seedStatus(WorldState &w, const StatusSeed &seed, bool worldEvents) {
    auto statusFrom = [](const Json::Value &c) {
        Json::Value s;
        s["stability"] = round1(number(c, "stability", 50));
        s["baseline_stability"] = s["stability"];
        s["gdp_growth"] = std::round(number(c, "gdp_growth") * 100) / 100;
        s["pop_growth"] = std::round(number(c, "pop_growth") * 100) / 100;
        s["currency"] = c.get("currency", "USD");
        s["symbol"] = c.get("symbol", "US$");
        s["usd_rate"] = number(c, "usd_rate", 1);
        s["estimated"] = number(c, "estimated") != 0;
        return s;
    };
    for (auto &a : w.actors.getMemberNames())
        if (seed.countries.isMember(a)) w.actors[a]["status"] = statusFrom(seed.countries[a]);
    for (auto &id : w.territories.getMemberNames()) {
        auto &t = w.territories[id];
        t["population"] = seed.regions.isMember(id) ? integer(seed.regions[id], "population") : Int(0);
        t["gdp_usd"] = seed.regions.isMember(id) ? integer(seed.regions[id], "gdp_usd") : Int(0);
        t["displaced"] = Int(0);
    }
    if (!worldEvents || !seed.world.isObject()) return;
    for (auto &a : seed.world["actors"].getMemberNames()) {
        auto &v = seed.world["actors"][a];
        auto base = v["base"].asString();
        w.actors[a]["name"] = v["name"];
        w.actors[a]["color"] = v["color"];
        if (seed.countries.isMember(base)) {
            auto s = statusFrom(seed.countries[base]);
            s["stability"] = s["baseline_stability"] = 25.0;
            s["gdp_growth"] = s["pop_growth"] = 0.0;
            w.actors[a]["status"] = s;
        }
    }
    int n = 0;
    for (auto &c : seed.world["conflicts"]) {
        Json::Value v;
        for (auto k : {"name", "kind", "actors", "text", "started_turn", "affected_regions"}) v[k] = c[k];
        v["status"] = "open";
        v["set_intensity"] = v["intensity"] = c["intensity"];
        v["theatre"] = c["affected_regions"];
        v["casualties"] = v["displaced"] = Int(0);
        for (auto &occupier : c["occupied"].getMemberNames())
            for (auto &r : c["occupied"][occupier])
                if (w.territories.isMember(r.asString())) {
                    // Long-standing occupations already did their economic damage before 2026.
                    auto &t = w.territories[r.asString()];
                    t["controller"] = occupier;
                    t["occupation_gdp"] = Int(std::llround(double(integer(t, "gdp_usd")) / rate::occupationGdpFloor));
                }
        w.conflicts["c0-" + std::to_string(++n)] = v;
    }
    n = 0;
    for (auto &c : seed.world["commitments"]) {
        Json::Value v;
        for (auto k : {"name", "kind", "actors", "text", "started_turn"}) v[k] = c[k];
        v["status"] = "open";
        w.commitments["k0-" + std::to_string(++n)] = v;
    }
}
std::string formatMoney(double usd, const Json::Value &status) {
    auto code = status.get("currency", "USD").asString();
    auto dollars = "US$" + scaled(usd);
    if (code == "USD") return dollars;
    auto symbol = status.get("symbol", code + " ").asString();
    return symbol + scaled(usd * number(status, "usd_rate", 1)) + " (" + dollars + ")";
}
Json::Value nationalStatus(const WorldState &w, const std::string &a) {
    auto t = totals(w);
    return buildStatus(w, a, t[a]);
}
Json::Value allNationalStatus(const WorldState &w) {
    auto t = totals(w);
    Json::Value out(Json::objectValue);
    for (auto &a : w.actors.getMemberNames()) out[a] = buildStatus(w, a, t[a]);
    return out;
}
std::string campaignDate(int turn) {
    static const char *months[] = {"January", "February", "March", "April", "May", "June",
                                   "July", "August", "September", "October", "November", "December"};
    return std::string(months[((turn % 12) + 12) % 12]) + " " + std::to_string(2026 + int(std::floor(turn / 12.0)));
}
bool chronicleDue(const Json::Value &current, int turn, bool sourceAvailable) {
    if (!current.isObject() || !current.isMember("summary") || !current["summary"].isString()) return true;
    if (turn - current.get("through_turn", 0).asInt() >= chronicleInterval) return true;
    return sourceAvailable && !current.get("with_source", false).asBool();
}
std::string displayName(const WorldState &w, const std::string &kind, const std::string &id) {
    auto &v = kind == "conflict" ? w.conflicts[id] : w.commitments[id];
    if (v["name"].isString() && !v["name"].asString().empty()) return v["name"].asString();
    return fallbackName(w, kind, v.get("kind", kind == "conflict" ? "war" : "other").asString(), parties(v));
}
Json::Value describeAgreement(const WorldState &w, const std::string &kind, const std::string &id) {
    auto &v = kind == "conflict" ? w.conflicts[id] : w.commitments[id];
    Json::Value d;
    d["name"] = displayName(w, kind, id);
    d["kind"] = v.get("kind", kind == "conflict" ? "war" : "other");
    // Large blocs list only their size: 32 member names would crowd out current facts.
    if (v["actors"].size() > 6) d["member_count"] = v["actors"].size();
    else {
        d["actors"] = v["actors"];
        d["party_names"] = Json::arrayValue;
        for (auto &a : v["actors"]) d["party_names"].append(w.actors[a.asString()].get("name", a));
    }
    d["status"] = v["status"];
    d["text"] = v["text"];
    // A start date rather than a month count, which models turn into "active for nearly 295 months".
    if (v["started_turn"].isInt()) d["since"] = campaignDate(v["started_turn"].asInt());
    if (v["expires_turn"].isInt() && v["status"] == "open") d["months_left"] = v["expires_turn"].asInt() - w.turn;
    // Terms already carried out, in words: a treaty's obligations shape what its parties may do next.
    auto name = [&](const Json::Value &a) { return w.actors[a.asString()].get("name", a).asString(); };
    for (auto &t : v["terms"]) {
        auto region = w.territories[t["region"].asString()].get("name", t["region"]).asString();
        if (t["action"] == "end_conflict") d["terms"].append("ended " + displayName(w, "conflict", t["conflict"].asString()));
        else if (t["action"] == "cede") d["terms"].append(name(t["from"]) + " ceded " + region + " to " + name(t["to"]));
        else if (t["action"] == "occupy") d["terms"].append(name(t["by"]) + " holds " + region + " while in force");
        else if (t["action"] == "withdraw") d["terms"].append(name(t["by"]) + " withdrew from " + region);
    }
    if (kind == "conflict") {
        d["intensity"] = v.get("intensity", "unknown");
        d["casualties"] = integer(v, "casualties");
        d["displaced"] = integer(v, "displaced");
        d["affected_regions"] = Json::arrayValue;
        for (auto &r : v["affected_regions"]) d["affected_regions"].append(w.territories[r.asString()]["name"]);
    }
    return d;
}
std::string realisticName(const WorldState &w, const std::string &kind, const std::string &type, const std::string &proposed,
                          const std::vector<std::string> &parties, const std::string &text, const std::set<std::string> &taken) {
    auto name = proposed;
    auto trim = [](std::string s) {
        auto b = s.find_first_not_of(" \t\r\n\"'"), e = s.find_last_not_of(" \t\r\n\"'.");
        return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
    };
    name = trim(name);
    // A name that says nothing beyond "war" or "treaty" is not how anyone would refer to it.
    static const std::set<std::string> generic{"the", "of", "a", "an", "and", "on", "in", "for", "between", "with", "war", "wars", "conflict", "treaty", "pact", "alliance", "agreement", "accord", "accords", "dispute", "crisis", "clash", "clashes", "border", "new", "first", "peace", "defence", "defense", "trade", "military", "mutual", "non", "aggression", "ceasefire", "truce", "security", "partnership", "cooperation", "union", "civil", "insurgency", "rebellion", "invasion", "operation", "campaign", "joint", "bilateral", "strategic", "comprehensive", "friendship"};
    bool specific = false;
    std::string word;
    for (size_t i = 0; i <= name.size(); ++i) {
        unsigned char ch = i < name.size() ? static_cast<unsigned char>(name[i]) : ' ';
        if (std::isalnum(ch) || ch >= 0x80) word += char(std::tolower(ch));
        else { if (!word.empty() && !generic.contains(word)) specific = true; word.clear(); }
    }
    if (name.size() < 4 || name.size() > 120 || !specific || lower(name) == lower(trim(text)))
        name = fallbackName(w, kind, type, parties);
    static const char *ordinals[] = {"Second", "Third", "Fourth", "Fifth", "Sixth", "Seventh", "Eighth", "Ninth", "Tenth"};
    auto unique = name;
    for (int n = 0; taken.contains(lower(unique)); ++n)
        unique = n < 9 ? std::string(ordinals[n]) + " " + name : name + " (" + std::to_string(n + 2) + ")";
    return unique.substr(0, 160);
}

Resolution applyMonthlyStatus(const Resolution &base, const WorldDelta &delta, const Adjacency &adjacency) {
    Resolution r = base;
    auto &w = r.state;
    auto &T = w.territories;
    // Reads go through const views so missing members are never inserted as nulls.
    const Json::Value &cT = w.territories, &cActors = w.actors;
    auto regionIds = T.getMemberNames();
    auto ownerStatus = [&](const Json::Value &t) -> const Json::Value & { return cActors[t["owner"].asString()]["status"]; };

    // New states inherit the economic character of the land they were carved from.
    for (auto &a : w.actors.getMemberNames()) {
        if (w.actors[a].isMember("status")) continue;
        std::string largest;
        Int best = -1;
        for (auto &id : regionIds)
            if (cT[id]["owner"] == a && integer(cT[id], "population") > best) { best = integer(cT[id], "population"); largest = id; }
        if (largest.empty()) continue;
        auto parent = cT[largest]["initial_affiliation"].asString();
        if (!cActors[parent]["status"].isObject()) continue;
        auto s = cActors[parent]["status"];
        s["stability"] = s["baseline_stability"] = std::min(number(s, "stability", 50), 45.0);
        s.removeMember("last_gdp_change");
        s.removeMember("last_population_change");
        w.actors[a]["status"] = s;
    }
    auto before = totals(w);

    // 1. Baseline demographic and economic trend.
    for (auto &id : regionIds) {
        auto &t = T[id];
        if (!cT[id].isMember("population")) continue;
        auto &s = ownerStatus(cT[id]);
        if (!s.isObject()) continue;
        t["population"] = Int(std::llround(double(integer(t, "population")) * (1 + std::clamp(number(s, "pop_growth"), -5.0, 5.0) / 1200)));
        t["gdp_usd"] = Int(std::llround(double(integer(t, "gdp_usd")) * (1 + std::clamp(number(s, "gdp_growth"), -10.0, 10.0) / 1200)));
    }

    // 2. Qualitative model effects, bounded per actor per month.
    std::map<std::string, double> effectGdp, effectStability;
    for (const auto &e : delta.events)
        for (const auto &x : e["effects"]) {
            static const std::map<std::string, std::pair<double, double>> scale{{"severe_decline", {-1.0, -6}}, {"decline", {-0.3, -2}}, {"none", {0, 0}}, {"growth", {0.2, 1}}, {"strong_growth", {0.5, 3}}};
            auto a = x["actor"].asString();
            if (scale.contains(x["economy"].asString())) effectGdp[a] += scale.at(x["economy"].asString()).first;
            if (scale.contains(x["stability"].asString())) effectStability[a] += scale.at(x["stability"].asString()).second;
        }
    for (auto &[a, pct] : effectGdp) {
        pct = std::clamp(pct, -rate::maxEffectGdp, rate::maxEffectGdp);
        for (auto &id : regionIds)
            if (cT[id]["owner"] == a) T[id]["gdp_usd"] = Int(std::llround(double(integer(cT[id], "gdp_usd")) * (1 + pct / 100)));
    }
    for (auto &[a, s] : effectStability) s = std::clamp(s, -rate::maxEffectStability, rate::maxEffectStability);

    // 3. Occupation drains the output of occupied land, down to a floor.
    for (auto &id : regionIds) {
        auto &t = T[id];
        if (!cT[id].isMember("population")) continue;
        if (cT[id]["owner"] != cT[id]["controller"] && cT[id]["owner"] != "") {
            if (!t.isMember("occupation_gdp")) t["occupation_gdp"] = integer(t, "gdp_usd");
            auto floor = double(integer(t, "occupation_gdp")) * rate::occupationGdpFloor;
            t["gdp_usd"] = Int(std::llround(std::max(double(integer(t, "gdp_usd")) * (1 - rate::occupationGdpLoss), std::min(floor, double(integer(t, "gdp_usd"))))));
        } else
            t.removeMember("occupation_gdp");
    }

    // 4. War fronts: where each open conflict is actually fought this month.
    std::set<std::string> seized;
    for (const auto &c : delta.changes) {
        if (c["kind"] == "territory" && !c["before"].isNull() && c["before"]["controller"] != c["after"]["controller"]) seized.insert(c["id"].asString());
    }
    struct Front {
        std::string id;
        std::set<std::string> parties;
        std::vector<std::string> regions;
        int intensity;
        bool fighting;
    };
    std::vector<Front> fronts;
    std::set<std::string> allAffected;
    for (auto &id : w.conflicts.getMemberNames()) {
        const Json::Value &c = w.conflicts[id];
        if (c["status"] != "open") continue;
        Front f{id, {}, {}, -1, false};
        for (auto &a : c["actors"]) f.parties.insert(a.asString());
        auto add = [&](const std::string &region) {
            if (cT.isMember(region) && cT[region]["kind"] != "residual" && std::find(f.regions.begin(), f.regions.end(), region) == f.regions.end()) f.regions.push_back(region);
        };
        auto held = [&](const std::string &region) { return f.parties.contains(cT[region]["controller"].asString()) || f.parties.contains(cT[region]["owner"].asString()); };
        for (auto &region : seized) {
            // Fighting: control changed between parties of this conflict.
            for (const auto &ch : delta.changes)
                if (ch["kind"] == "territory" && ch["id"] == region && f.parties.contains(ch["after"]["controller"].asString()) && (f.parties.contains(ch["before"]["controller"].asString()) || f.parties.contains(ch["before"]["owner"].asString()))) { f.fighting = true; add(region); }
        }
        for (const auto &ch : delta.changes)
            if (ch["kind"] == "entity" && ch["after"]["status"] == "destroyed" && ch["before"]["status"] != "destroyed" && f.parties.contains(ch["after"]["actor"].asString()) && held(ch["after"]["region"].asString())) { f.fighting = true; add(ch["after"]["region"].asString()); }
        for (auto &region : c["theatre"])
            if (cT.isMember(region.asString()) && held(region.asString())) add(region.asString());
        std::vector<std::string> border;
        for (auto &region : regionIds) {
            auto p = cT[region]["controller"].asString();
            if (!f.parties.contains(p) || cT[region]["kind"] == "residual" || !adjacency.contains(region)) continue;
            for (auto &n : adjacency.at(region)) {
                auto q = cT[n]["controller"].asString();
                if (cT.isMember(n) && q != p && f.parties.contains(q) && cT[n]["kind"] != "residual") { border.push_back(region); break; }
            }
        }
        for (auto &region : border) add(region);
        if (f.regions.size() > rate::maxAffected) f.regions.resize(rate::maxAffected);
        bool ceasefire = false;
        for (const auto &k : static_cast<const Json::Value &>(w.commitments)) {
            auto kind = k["kind"].asString();
            if (k["status"] != "open" || (kind != "ceasefire" && kind != "peace_treaty")) continue;
            int shared = 0;
            for (auto &a : k["actors"]) shared += f.parties.contains(a.asString());
            ceasefire = ceasefire || shared >= 2;
        }
        auto declared = level(c.get("set_intensity", "medium").asString());
        if (ceasefire) f.intensity = -1;
        else if (f.fighting) f.intensity = 2;
        else if (f.regions.empty()) f.intensity = std::min(declared, 0);
        else f.intensity = std::max(declared, 0);
        if (f.intensity < 0) f.regions.clear();
        allAffected.insert(f.regions.begin(), f.regions.end());
        fronts.push_back(f);
    }

    // 5. Casualties, displacement and war damage.
    std::map<std::string, Int> actorDeaths;
    auto move = [&](const std::string &from, const std::string &to, Int count) {
        auto &a = T[from];
        auto &b = T[to];
        count = std::min(count, integer(a, "population"));
        if (count <= 0) return Int(0);
        auto output = Int(std::llround(double(integer(a, "gdp_usd")) * double(count) / double(std::max<Int>(1, integer(a, "population"))) * rate::displacedOutput));
        a["population"] = integer(a, "population") - count;
        b["population"] = integer(b, "population") + count;
        a["gdp_usd"] = integer(a, "gdp_usd") - output;
        b["gdp_usd"] = integer(b, "gdp_usd") + output;
        return count;
    };
    auto kill = [&](const std::string &region, Int count) {
        count = std::min(count, integer(cT[region], "population") / 100);
        if (count <= 0) return Int(0);
        auto per = double(integer(cT[region], "gdp_usd")) / double(std::max<Int>(1, integer(cT[region], "population")));
        T[region]["population"] = integer(cT[region], "population") - count;
        T[region]["gdp_usd"] = std::max<Int>(0, integer(cT[region], "gdp_usd") - Int(std::llround(per * double(count))));
        actorDeaths[cT[region]["owner"].asString()] += count;
        return count;
    };
    auto current = totals(w);
    for (auto &f : fronts) {
        auto &c = w.conflicts[f.id];
        c["intensity"] = f.intensity < 0 ? "none" : f.intensity == 0 ? "low" : f.intensity == 1 ? "medium" : "high";
        c["affected_regions"] = Json::arrayValue;
        for (auto &region : f.regions) c["affected_regions"].append(region);
        if (f.intensity < 0 || f.regions.empty()) {
            c["last_month_casualties"] = c["last_month_displaced"] = Int(0);
            continue;
        }
        auto kind = c.get("kind", "war").asString();
        double multiplier = kind == "border_conflict" ? 0.4 : kind == "insurgency" ? 0.3 : 1.0;
        Int deaths = 0, displaced = 0;
        Int affectedPopulation = 0;
        for (auto &region : f.regions) affectedPopulation += integer(cT[region], "population");
        // Military losses come out of the population each party recruits from: its whole country, or for a
        // landless movement the front regions it fights in.
        for (auto &p : f.parties) {
            auto pool = std::max(double(current[p].population), 0.25 * double(affectedPopulation));
            auto losses = Int(std::llround(pool * rate::military[f.intensity] * multiplier * jitter(w, f.id + ":military:" + p)));
            std::map<std::string, double> weights;
            for (auto &region : regionIds) if (cT[region]["owner"] == p && cT[region]["controller"] == p) weights[region] = double(integer(cT[region], "population"));
            if (weights.empty()) for (auto &region : f.regions) if (cT[region]["controller"] == p) weights[region] = double(integer(cT[region], "population"));
            if (weights.empty()) for (auto &region : f.regions) weights[region] = double(integer(cT[region], "population"));
            for (auto &[region, n] : distribute(losses, weights)) deaths += kill(region, n);
        }
        for (auto &region : f.regions) {
            auto &t = T[region];
            auto population = double(integer(t, "population"));
            deaths += kill(region, Int(std::llround(population * rate::civilian[f.intensity] * multiplier * jitter(w, f.id + ":civilian:" + region))));
            auto outflow = Int(std::llround(double(integer(t, "population")) * (rate::displacement[f.intensity] * multiplier * jitter(w, f.id + ":displacement:" + region) + (seized.contains(region) ? rate::seizedDisplacement : 0))));
            if (outflow <= 0) continue;
            auto owner = cT[region]["owner"].asString();
            std::map<std::string, double> internal, abroad;
            for (auto &id : regionIds)
                if (id != region && cT[id]["owner"] == owner && cT[id]["controller"] == owner && cT[id]["kind"] != "residual" && !allAffected.contains(id)) internal[id] = double(integer(cT[id], "population")) + 1;
            if (adjacency.contains(region))
                for (auto &n : adjacency.at(region))
                    if (cT.isMember(n) && cT[n]["kind"] != "residual" && cT[n]["owner"] != owner && !f.parties.contains(cT[n]["controller"].asString()) && !allAffected.contains(n) && cT[n].isMember("population")) abroad[n] = double(integer(cT[n], "population")) + 1;
            // With nowhere safe, people still flee to the rest of their country, front or not.
            if (internal.empty() && abroad.empty())
                for (auto &id : regionIds)
                    if (id != region && cT[id]["owner"] == owner && cT[id]["controller"] == owner && cT[id]["kind"] != "residual") internal[id] = double(integer(cT[id], "population")) + 1;
            if (internal.empty() && abroad.empty()) continue;
            auto refugees = internal.empty() ? outflow : abroad.empty() ? Int(0) : Int(std::llround(double(outflow) * rate::refugeeShare));
            Int moved = 0;
            for (auto &[to, n] : distribute(refugees, abroad)) moved += move(region, to, n);
            for (auto &[to, n] : distribute(outflow - refugees, internal)) moved += move(region, to, n);
            t["displaced"] = integer(t, "displaced") + moved;
            displaced += moved;
        }
        for (auto &region : f.regions) T[region]["gdp_usd"] = Int(std::llround(double(integer(cT[region], "gdp_usd")) * (1 - rate::frontGdpLoss[f.intensity] * multiplier)));
        for (auto &id : regionIds)
            if (f.parties.contains(cT[id]["owner"].asString())) T[id]["gdp_usd"] = Int(std::llround(double(integer(cT[id], "gdp_usd")) * (1 - rate::partyGdpDrag[f.intensity] * multiplier)));
        c["casualties"] = integer(c, "casualties") + deaths;
        c["displaced"] = integer(c, "displaced") + displaced;
        c["last_month_casualties"] = deaths;
        c["last_month_displaced"] = displaced;
        std::string where;
        for (size_t i = 0; i < f.regions.size() && i < 4; ++i) where += (i ? ", " : "") + cT[f.regions[i]]["name"].asString();
        if (f.regions.size() > 4) where += " and " + std::to_string(f.regions.size() - 4) + " more regions";
        r.events.push_back({*f.parties.begin(), "war_toll", f.id,
                            displayName(w, "conflict", f.id) + " (" + c["intensity"].asString() + " intensity): about " + grouped(deaths) + " killed and " + grouped(displaced) + " displaced this month in " + where + "."});
    }

    // 6. Displaced people return home once their region is calm and back under its owner's control.
    for (auto &id : regionIds) {
        auto &t = T[id];
        auto waiting = integer(cT[id], "displaced");
        if (waiting <= 0 || allAffected.contains(id) || cT[id]["owner"] != cT[id]["controller"]) continue;
        auto count = std::min(waiting, std::max<Int>(1, Int(std::llround(double(waiting) * rate::returnShare))));
        std::map<std::string, double> sources;
        for (auto &other : regionIds)
            if (other != id && cT[other]["owner"] == cT[id]["owner"] && cT[other]["controller"] == cT[id]["owner"] && integer(cT[other], "population") > 0) sources[other] = double(integer(cT[other], "population"));
        Int back = 0;
        for (auto &[from, n] : distribute(count, sources)) back += move(from, id, n);
        t["displaced"] = waiting - (sources.empty() ? count : back);
    }

    // 7. Stability drifts toward what the nation's situation supports; model effects leave a lasting trace.
    auto after = totals(w);
    for (auto &a : w.actors.getMemberNames()) {
        if (!cActors[a]["status"].isObject()) continue;
        auto &s = w.actors[a]["status"];
        auto baseline = std::clamp(number(s, "baseline_stability", 50) + 0.25 * effectStability[a], 0.0, 100.0);
        double war = 0;
        for (auto &f : fronts)
            if (f.parties.contains(a) && f.intensity >= 0) war = std::max(war, rate::stabilityWar[f.intensity]);
        Int occupiedPopulation = 0;
        for (auto &id : regionIds)
            if (cT[id]["owner"] == a && cT[id]["controller"] != a) occupiedPopulation += integer(cT[id], "population");
        auto &b = before[a];
        auto &n = after[a];
        double gdpChange = b.gdp > 0 ? (double(n.gdp) - double(b.gdp)) / double(b.gdp) * 100 : 0;
        double target = baseline - war - (n.population > 0 ? rate::occupiedStability * double(occupiedPopulation) / double(n.population) : 0) - (gdpChange < -1 ? rate::contractionStability : 0);
        auto stability = number(s, "stability", 50);
        double shock = n.population > 0 && double(actorDeaths[a]) / double(n.population) > 0.0001 ? 1 : 0;
        auto step = std::clamp(rate::stabilityReversion * (target - stability) + effectStability[a] - shock, -rate::maxStabilityStep, rate::maxStabilityStep);
        s["stability"] = round1(std::clamp(stability + step, 0.0, 100.0));
        s["baseline_stability"] = round1(baseline);
        s["last_gdp_change"] = std::round(gdpChange * 100) / 100;
        s["last_population_change"] = n.population - b.population;
    }
    return r;
}
} // namespace lh
