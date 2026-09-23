#include "localhistoria/conflicts.h"
#include "localhistoria/politics.h"
#include "status/status_internal.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
namespace lh::detail {
Int integer(const Json::Value &v, const char *k) {
    return v[k].isNumeric() ? Int(v[k].asInt64()) : 0;
}
double number(const Json::Value &v, const char *k, double fallback) {
    return v[k].isNumeric() ? v[k].asDouble() : fallback;
}
int level(const std::string &intensity) {
    return intensity == "high" ? 2 : intensity == "medium" ? 1 : intensity == "low" ? 0 : -1;
}
double round1(double v) { return std::round(v * 10) / 10; }
std::string grouped(Int n) {
    auto s = std::to_string(n < 0 ? -n : n);
    for (int i = int(s.size()) - 3; i > 0; i -= 3)
        s.insert(size_t(i), ",");
    return (n < 0 ? "-" : "") + s;
}
std::string scaled(double v) {
    char b[64];
    double a = std::fabs(v);
    if (a >= 1e12)
        snprintf(b, sizeof b, "%.2f trillion", v / 1e12);
    else if (a >= 1e9)
        snprintf(b, sizeof b, "%.1f billion", v / 1e9);
    else if (a >= 1e6)
        snprintf(b, sizeof b, "%.1f million", v / 1e6);
    else
        return grouped(Int(std::llround(v)));
    return b;
}
std::string people(Int n) { return n >= 1000000 ? scaled(double(n)) : grouped(n); }
// The bands the journal, the inspector and the map legend all name unrest by.
std::string unrestLabel(double u) {
    return u < rate::unrestNotice      ? "calm"
           : u < 55                    ? "restive"
           : u < rate::revoltThreshold ? "unrest"
           : u < rate::revoltGround    ? "in revolt"
                                       : "in open rebellion";
}
std::string stabilityLabel(double s) {
    return s < 20   ? "collapsing"
           : s < 35 ? "unstable"
           : s < 50 ? "fragile"
           : s < 70 ? "stable"
           : s < 85 ? "very stable"
                    : "highly stable";
}
// Largest-remainder split of an amount across weighted targets, in stable ID order.
std::map<std::string, Int> distribute(Int amount, const std::map<std::string, double> &weights) {
    std::map<std::string, Int> out;
    if (amount <= 0 || weights.empty())
        return out;
    double total = 0;
    for (auto &[k, w] : weights)
        total += w;
    std::vector<std::pair<double, std::string>> remainders;
    Int given = 0;
    for (auto &[k, w] : weights) {
        double share = total > 0 ? amount * w / total : double(amount) / double(weights.size());
        out[k] = Int(share);
        given += out[k];
        remainders.emplace_back(share - double(out[k]), k);
    }
    std::stable_sort(remainders.begin(), remainders.end(),
                     [](auto &a, auto &b) { return a.first > b.first; });
    for (size_t i = 0; given < amount && i < remainders.size(); ++i, ++given)
        out[remainders[i].second]++;
    return out;
}
std::map<std::string, Totals> totals(const WorldState &w) {
    std::map<std::string, Totals> out;
    for (auto &id : w.territories.getMemberNames()) {
        auto &t = w.territories[id];
        auto owner = t["owner"].asString(), controller = t["controller"].asString();
        auto &o = out[owner];
        o.population += integer(t, "population");
        o.gdp += integer(t, "gdp_usd");
        o.displaced += integer(t, "displaced");
        if (owner != controller) {
            o.occupied++;
            out[controller].occupying++;
        }
        auto &c = out[controller];
        if (number(t, "unrest") > c.unrest) {
            c.unrest = number(t, "unrest");
            c.unrestRegion = t["name"].asString();
        }
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
        s["gdp_per_capita"] =
            t.population > 0
                ? "US$" + grouped(Int(std::llround(double(t.gdp) / double(t.population))))
                : "n/a";
        auto score = number(status, "stability", 50);
        char b[32];
        snprintf(b, sizeof b, "%.0f", score);
        s["stability"] = stabilityLabel(score) + " (" + b + "/100)";
        s["stability_score"] = round1(score);
        auto change = number(status, "last_gdp_change");
        snprintf(b, sizeof b, "%+.2f%%", change);
        s["economic_trend"] = std::string(
            status.isMember("last_gdp_change")
                ? (change < -1      ? "contracting sharply"
                   : change < -0.05 ? "contracting"
                   : change <= 0.05 ? "stagnant"
                   : change <= 0.4  ? "growing"
                                    : "growing fast") +
                      std::string(" (") + b + " last month)"
                : (number(status, "gdp_growth") < 0 ? "contracting" : "growing") +
                      std::string(" (trend ") +
                      [&] {
                          char t[16];
                          snprintf(t, sizeof t, "%+.1f%%", number(status, "gdp_growth"));
                          return std::string(t);
                      }() +
                      " a year)");
        auto government = status.get("government", "").asString();
        s["government_id"] = government;
        s["government"] = governmentLabel(government);
        if (integer(status, "government_since") > 0)
            s["government_since"] = integer(status, "government_since");
        // Only when something is actually stirring: a calm country says nothing about unrest, to
        // the player or to the model.
        if (t.unrest >= rate::unrestNotice) {
            snprintf(b, sizeof b, "%.0f", t.unrest);
            s["unrest"] = unrestLabel(t.unrest) + " in " + t.unrestRegion + " (" + b + "/100)";
            s["unrest_score"] = round1(t.unrest);
            s["unrest_region"] = t.unrestRegion;
        }
        s["currency"] = status["currency"];
        s["estimated"] = status.get("estimated", false).asBool();
    } else
        s["available"] = false;
    if (status["military"].isObject()) {
        s["military"] = status["military"];
        // Engine accounting stays out of the inventory; exhaustion is described below.
        for (auto k : {"establishment", "peace_months", "exhaustion"})
            s["military"].removeMember(k);
        s["military"]["uncommitted_personnel"] = force::uncommittedPersonnel(w, a);
        s["military"]["uncommitted_ships"] = force::uncommittedShips(w, a);
        s["military"]["label"] = force::militaryLabel(w, a);
        s["military_text"] = force::militarySummary(w, a);
        s["military_spend_usd"] = status["military_spend_usd"];
        // Realism: how worn its forces are, in words, once it matters.
        double worn = number(status["military"], "exhaustion");
        if (worn >= 0.25)
            s["forces"] = std::string(worn >= 0.75  ? "exhausted"
                                      : worn >= 0.5 ? "worn down"
                                                    : "strained") +
                          " by sustained fighting";
    }
    // Realism: what this month's implementation allowance could pay for, in one line.
    if (status["implementation"].isObject()) {
        const auto &i = status["implementation"];
        auto money = [](double usd) {
            char b[48];
            if (usd >= 1e9)
                snprintf(b, sizeof b, "US$%.1f billion", usd / 1e9);
            else
                snprintf(b, sizeof b, "US$%.0f million", usd / 1e6);
            return std::string(b);
        };
        std::string text = money(number(i, "allowance_usd")) + " a month for projects, " +
                           money(number(i, "committed_usd")) + " committed";
        if (integer(i, "stalled") > 0)
            text += "; " + std::to_string(integer(i, "stalled")) + " underfunded";
        for (const auto &k : status["efforts"].getMemberNames())
            text += "; " + k + " under way";
        if (status["procurement"].isObject() && !status["procurement"].empty())
            text += "; replacing losses";
        s["implementation"] = text;
    }
    s["wars"] = Json::arrayValue;
    for (auto &id : w.conflicts.getMemberNames()) {
        auto &c = w.conflicts[id];
        if (!isOpen(c) || !isParty(c, a))
            continue;
        Json::Value war;
        war["id"] = id;
        war["name"] = displayName(w, "conflict", id);
        war["intensity"] = c.get("intensity", "unknown");
        if (c["started_turn"].isInt())
            war["since"] = campaignDate(c["started_turn"].asInt());
        war["casualties"] = integer(c, "casualties");
        war["against"] = Json::arrayValue;
        if (!realismRules(w)) {
            for (auto &p : c["actors"])
                if (p != a && war["against"].size() < 4)
                    war["against"].append(w.actors[p.asString()]["name"]);
        } else {
            // Only the parties this nation is actually fighting: a co-belligerent is not an enemy,
            // and a covered ceasefire pauses its pair while the rest of the war goes on.
            for (const auto &pair : hostilePairs(w, c)) {
                if (!isParty(pair, a))
                    continue;
                auto other = pair["actors"][0] == a ? pair["actors"][1] : pair["actors"][0];
                const char *key = fightingPair(w, c, a, other.asString()) ? "against"
                                  : pair["status"] == "active"            ? "ceasefire_with"
                                                                          : nullptr;
                if (key && war[key].size() < 4)
                    war[key].append(w.actors[other.asString()]["name"]);
            }
        }
        s["wars"].append(war);
    }
    s["treaties"] = Json::arrayValue;
    for (auto &id : w.commitments.getMemberNames()) {
        auto &c = w.commitments[id];
        if (!isOpen(c) || !isParty(c, a))
            continue;
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
std::string fallbackName(const WorldState &w, const std::string &kind, const std::string &type,
                         const std::vector<std::string> &parties) {
    auto pair = names(w, parties);
    auto first = parties.empty() ? std::string("Unknown")
                                 : w.actors[parties[0]].get("name", parties[0]).asString();
    auto last = parties.size() < 2
                    ? first
                    : w.actors[parties.back()].get("name", parties.back()).asString();
    if (kind == "conflict") {
        if (type == "civil_war")
            return "Civil War in " + first;
        if (type == "insurgency")
            return last + " Insurgency";
        if (type == "border_conflict")
            return pair + " Border Conflict";
        return pair + " War";
    }
    if (type == "peace_treaty") {
        // Treaties are named after where they are signed: the acting party's most populous city
        // region.
        std::string city;
        Int best = -1;
        for (auto &id : w.entities.getMemberNames()) {
            auto &e = w.entities[id];
            if (parties.empty() || e["actor"] != parties[0] || e["kind"] != "city" ||
                e["status"] != "active")
                continue;
            auto population = integer(w.territories[e["region"].asString()], "population");
            if (population > best) {
                best = population;
                city = e["name"].asString();
            }
        }
        return "Treaty of " + (city.empty() ? first : city);
    }
    static const std::map<std::string, std::string> suffix{
        {"alliance", "Alliance"},
        {"defence_pact", "Defence Pact"},
        {"non_aggression_pact", "Non-Aggression Pact"},
        {"trade_agreement", "Trade Agreement"},
        {"ceasefire", "Ceasefire"},
        {"union", "Union"}};
    return pair + " " + (suffix.contains(type) ? suffix.at(type) : "Accord");
}
std::vector<std::string> parties(const Json::Value &v) {
    std::vector<std::string> out;
    for (auto &a : v["actors"])
        out.push_back(a.asString());
    return out;
}
double jitter(const WorldState &w, const std::string &key) {
    return 0.7 +
           0.6 *
               double(stableHash(w.campaignId + ":" + std::to_string(w.turn) + ":" + key) % 10000) /
               10000.0;
}
} // namespace lh::detail
