#include "localhistoria/politics.h"
#include "status/status_internal.h"
#include <algorithm>
namespace lh {
using namespace detail;
bool validConflictKind(const std::string &k) {
    return k == "war" || k == "border_conflict" || k == "insurgency" || k == "civil_war";
}
bool validCommitmentKind(const std::string &k) {
    return k == "alliance" || k == "defence_pact" || k == "non_aggression_pact" ||
           k == "trade_agreement" || k == "ceasefire" || k == "peace_treaty" || k == "union" ||
           k == "other";
}
bool validIntensity(const std::string &k) {
    return k == "none" || k == "low" || k == "medium" || k == "high";
}
bool validEffect(const std::string &k) {
    return k == "severe_decline" || k == "decline" || k == "none" || k == "growth" ||
           k == "strong_growth";
}

std::string displayName(const WorldState &w, const std::string &kind, const std::string &id) {
    auto &v = kind == "conflict" ? w.conflicts[id] : w.commitments[id];
    if (v["name"].isString() && !v["name"].asString().empty())
        return v["name"].asString();
    return fallbackName(w, kind, v.get("kind", kind == "conflict" ? "war" : "other").asString(),
                        parties(v));
}
Json::Value describeAgreement(const WorldState &w, const std::string &kind, const std::string &id) {
    auto &v = kind == "conflict" ? w.conflicts[id] : w.commitments[id];
    Json::Value d;
    d["name"] = displayName(w, kind, id);
    d["kind"] = v.get("kind", kind == "conflict" ? "war" : "other");
    // Large blocs list only their size: 32 member names would crowd out current facts.
    if (v["actors"].size() > 6)
        d["member_count"] = v["actors"].size();
    else {
        d["actors"] = v["actors"];
        d["party_names"] = Json::arrayValue;
        for (auto &a : v["actors"])
            d["party_names"].append(w.actors[a.asString()].get("name", a));
    }
    d["status"] = v["status"];
    d["text"] = v["text"];
    // A start date rather than a month count, which models turn into "active for nearly 295
    // months".
    if (v["started_turn"].isInt())
        d["since"] = campaignDate(v["started_turn"].asInt());
    if (v["expires_turn"].isInt() && isOpen(v))
        d["months_left"] = v["expires_turn"].asInt() - w.turn;
    // Terms already carried out, in words: a treaty's obligations shape what its parties may do
    // next.
    auto name = [&](const Json::Value &a) {
        return w.actors[a.asString()].get("name", a).asString();
    };
    for (auto &t : v["terms"]) {
        auto region = w.territories[t["region"].asString()].get("name", t["region"]).asString();
        if (t["action"] == "end_conflict")
            d["terms"].append("ended " + displayName(w, "conflict", t["conflict"].asString()));
        else if (t["action"] == "cede")
            d["terms"].append(name(t["from"]) + " ceded " + region + " to " + name(t["to"]));
        else if (t["action"] == "occupy")
            d["terms"].append(name(t["by"]) + " holds " + region + " while in force");
        else if (t["action"] == "withdraw")
            d["terms"].append(name(t["by"]) + " withdrew from " + region);
    }
    if (kind == "conflict") {
        d["intensity"] = v.get("intensity", "unknown");
        d["casualties"] = integer(v, "casualties");
        d["displaced"] = integer(v, "displaced");
        d["affected_regions"] = Json::arrayValue;
        for (auto &r : v["affected_regions"])
            d["affected_regions"].append(w.territories[r.asString()]["name"]);
    }
    return d;
}
std::string realisticName(const WorldState &w, const std::string &kind, const std::string &type,
                          const std::string &proposed, const std::vector<std::string> &parties,
                          const std::string &text, const std::set<std::string> &taken) {
    auto name = proposed;
    auto trim = [](std::string s) {
        auto b = s.find_first_not_of(" \t\r\n\"'"), e = s.find_last_not_of(" \t\r\n\"'.");
        return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
    };
    name = trim(name);
    // A name that says nothing beyond "war" or "treaty" is not how anyone would refer to it.
    static const std::set<std::string> generic{
        "the",       "of",         "a",           "an",          "and",
        "on",        "in",         "for",         "between",     "with",
        "war",       "wars",       "conflict",    "treaty",      "pact",
        "alliance",  "agreement",  "accord",      "accords",     "dispute",
        "crisis",    "clash",      "clashes",     "border",      "new",
        "first",     "peace",      "defence",     "defense",     "trade",
        "military",  "mutual",     "non",         "aggression",  "ceasefire",
        "truce",     "security",   "partnership", "cooperation", "union",
        "civil",     "insurgency", "rebellion",   "invasion",    "operation",
        "campaign",  "joint",      "bilateral",   "strategic",   "comprehensive",
        "friendship"};
    bool specific = false;
    std::string word;
    for (size_t i = 0; i <= name.size(); ++i) {
        unsigned char ch = i < name.size() ? static_cast<unsigned char>(name[i]) : ' ';
        if (std::isalnum(ch) || ch >= 0x80)
            word += char(std::tolower(ch));
        else {
            if (!word.empty() && !generic.contains(word))
                specific = true;
            word.clear();
        }
    }
    if (name.size() < 4 || name.size() > 120 || !specific ||
        lowercase(name) == lowercase(trim(text)))
        name = fallbackName(w, kind, type, parties);
    static const char *ordinals[] = {"Second",  "Third",  "Fourth", "Fifth", "Sixth",
                                     "Seventh", "Eighth", "Ninth",  "Tenth"};
    auto unique = name;
    for (int n = 0; taken.contains(lowercase(unique)); ++n)
        unique = n < 9 ? std::string(ordinals[n]) + " " + name
                       : name + " (" + std::to_string(n + 2) + ")";
    return unique.substr(0, 160);
}

} // namespace lh
