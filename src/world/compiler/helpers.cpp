#include "world/compiler/helpers.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <numbers>
namespace lh {
namespace detail {
std::string slug(const std::string &text, size_t limit) {
    std::string out;
    for (unsigned char ch : text) {
        if (std::isalnum(ch))
            out += char(std::tolower(ch));
        else if (!out.empty() && out.back() != '-')
            out += '-';
        if (out.size() >= limit)
            break;
    }
    while (!out.empty() && out.back() == '-')
        out.pop_back();
    return out.empty() ? "event" : out;
}
double kilometres(const Json::Value &a, const Json::Value &b) {
    const double rad = std::numbers::pi / 180;
    double la1 = a["latitude"].asDouble() * rad, la2 = b["latitude"].asDouble() * rad,
           dl = (b["longitude"].asDouble() - a["longitude"].asDouble()) * rad;
    return 6371 * std::acos(std::clamp(std::sin(la1) * std::sin(la2) +
                                           std::cos(la1) * std::cos(la2) * std::cos(dl),
                                       -1.0, 1.0));
}
// Effects ordered from worst to best, so the compiler can force a ceiling or floor on what the
// model wrote.
int rank(const std::string &effect) {
    return effect == "severe_decline" ? 0
           : effect == "decline"      ? 1
           : effect == "none"         ? 2
           : effect == "growth"       ? 3
                                      : 4;
}
std::string weaker(const std::string &a, const std::string &b) {
    return rank(a) <= rank(b) ? a : b;
}
std::string stronger(const std::string &a, const std::string &b) {
    return rank(a) >= rank(b) ? a : b;
}
// Treaty terms a small model may attach: normalized to known clause fields, at most four.
Json::Value treatyTerms(const Json::Value &terms) {
    Json::Value out(Json::arrayValue);
    if (!terms.isArray())
        return out;
    for (auto &t : terms) {
        if (out.size() >= 4 || !t.isObject() || !t["action"].isString())
            continue;
        Json::Value kept;
        for (auto k : {"action", "conflict", "region", "to", "by"})
            if (t[k].isString())
                kept[k] = t[k];
        out.append(kept);
    }
    return out;
}
bool securityTreaty(const std::string &kind) {
    return kind == "ceasefire" || kind == "peace_treaty" || kind == "non_aggression_pact" ||
           kind == "alliance" || kind == "defence_pact" || kind == "union";
}
int effectWeight(const Json::Value &effects) {
    int score = 0;
    if (!effects.isArray())
        return 0;
    for (auto &x : effects)
        for (auto k : {"economy", "stability"}) {
            auto v = x[k].isString() ? x[k].asString() : "";
            score += v == "severe_decline" || v == "strong_growth" ? 2
                     : v == "decline" || v == "growth"             ? 1
                                                                   : 0;
        }
    return score;
}
std::string alphanumeric(const std::string &v) {
    std::string o;
    for (unsigned char ch : v)
        if (std::isalnum(ch))
            o += char(std::tolower(ch));
    return o;
}
// A treaty keeps its eight latest terms.
void appendTerms(Json::Value &treaty, const Json::Value &done) {
    for (auto &t : done) {
        treaty["terms"].append(t);
        if (treaty["terms"].size() > 8)
            treaty["terms"].removeIndex(0, nullptr);
    }
}
} // namespace detail
std::string droppedClause(const Json::Value &event) {
    bool failed = event.get("failed", false).asBool();
    if (!failed && !event.get("partial", false).asBool())
        return "";
    std::vector<std::string> phrases;
    for (auto &d : event["dropped_operations"]) {
        auto a = d["action"].asString();
        std::string p;
        if (a == "occupy" || a == "transfer_territory" || a == "restore_control")
            p = failed ? "no territory changed hands" : "not every region changed hands";
        else if (a == "move_entity")
            p = "no forces moved";
        else if (a == "build")
            p = "no construction began";
        else if (a == "open_conflict")
            p = "no war began";
        else if (a == "set_conflict_intensity")
            p = "the war did not change course";
        else if (a == "add_commitment" || a == "amend_commitment")
            p = "no agreement was concluded";
        else if (a.rfind("resolve_", 0) == 0)
            p = "nothing was ended";
        else if (a == "create_actor")
            p = "no new state formed";
        else if (a == "set_entity_status")
            p = "no unit was affected";
        else if (a == "change_identity")
            p = "no identity change took effect";
        else if (a == "regional_initiative" || a == "respond_regional")
            p = "no regional change was recorded";
        else if (a.rfind("term ", 0) == 0)
            p = "not every treaty term took effect";
        else
            p = failed ? "none of it happened" : "part of it did not happen";
        if (std::find(phrases.begin(), phrases.end(), p) == phrases.end())
            phrases.push_back(p);
    }
    if (phrases.empty())
        phrases.push_back(failed ? "none of it happened" : "part of it did not happen");
    std::string out = " (In fact: ";
    for (size_t n = 0; n < phrases.size(); ++n)
        out += (n ? "; " : "") + phrases[n];
    return out + ".)";
}
// How much an event's effects weigh: 2 per severe or strong one, 1 per mild one, ignoring malformed
// entries.
} // namespace lh
