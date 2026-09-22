#include "localhistoria/politics.h"
#include "status/monthly/monthly_status.h"
#include <algorithm>
#include <cctype>
#include <cmath>
namespace lh::detail {
namespace {
// A movement names itself after the province it rose in; the ID follows the name.
std::string slugId(const std::string &text) {
    std::string out;
    for (unsigned char ch : text) {
        if (std::isalnum(ch))
            out += char(std::tolower(ch));
        else if (!out.empty() && out.back() != '-')
            out += '-';
        if (out.size() >= 30)
            break;
    }
    while (!out.empty() && out.back() == '-')
        out.pop_back();
    return out.empty() ? "movement" : out;
}
} // namespace
void MonthlyStatus::raiseRebellion(const std::string &region) {
    auto state = cT[region]["controller"].asString();
    auto name = cT[region]["name"].asString();
    static const char *forms[] = {" Liberation Front", " Defence Forces", " National Council",
                                  " Freedom Movement"};
    auto rebelName = (name + forms[stableHash(w.campaignId + region) % 4]).substr(0, 120);
    std::string id = slugId(rebelName);
    for (int n = 2; w.hasActor(id); ++n)
        id = slugId(rebelName) + "-" + std::to_string(n);

    auto &army = w.actors[state]["status"]["military"];
    double unrest = number(cT[region], "unrest") / 100;
    const auto &g = regime(cActors[state]["status"].get("government", "").asString());
    double committed = force::committedPersonnel(forceStart, adjacency, state, {region});
    double raised = committed * rate::defectionShare * unrest * (1 + g.repression) +
                    double(integer(cT[region], "population")) * rate::militiaPerResident * unrest;
    Int active =
        std::clamp(Int(std::llround(raised)), Int(rate::minimumRebels), Int(rate::maximumRebels));
    active = std::min(active, integer(army, "active"));
    if (active < rate::minimumRebels)
        return; // A state with no army left to defect from is already finished.
    army["active"] = integer(army, "active") - active;

    Json::Value identity;
    identity["name"] = rebelName;
    identity["color"] = paletteColor(id);
    reviseIdentity(identity, Json::nullValue, w.campaignId + id, "revolt:" + region);
    auto s = cActors[state]["status"];
    s["stability"] = s["baseline_stability"] = s["seeded_stability"] = 25.0;
    s["gdp_growth"] = s["pop_growth"] = 0.0;
    s["government"] = "transitional_government";
    s["government_since"] = campaignYear();
    s.removeMember("last_gdp_change");
    s.removeMember("last_population_change");
    s.removeMember("collapse_months");
    s.removeMember("military_spend_usd");
    Json::Value military;
    military["active"] = active;
    military["reserve"] = military["ships"] = military["aircraft"] = Int(0);
    military["estimated"] = 1;
    s["military"] = military;
    identity["status"] = s;
    Json::Value rebellion;
    rebellion["against"] = state;
    rebellion["origin"] = region;
    rebellion["since_turn"] = w.turn;
    rebellion["held_since"] = -1;
    identity["rebellion"] = rebellion;
    w.actors[id] = identity;

    bool major = double(active) >= rate::civilWarShare * double(integer(army, "active") + active);
    std::string kind = major ? "civil_war" : "insurgency";
    std::set<std::string> taken;
    for (const auto &c : static_cast<const Json::Value &>(w.conflicts))
        taken.insert(lowercase(c["name"].asString()));
    Json::Value conflict;
    conflict["kind"] = kind;
    conflict["name"] = realisticName(w, "conflict", kind, "", {state, id}, "", taken);
    conflict["actors"] = Json::arrayValue;
    conflict["actors"].append(state);
    conflict["actors"].append(id);
    conflict["status"] = "open";
    conflict["started_turn"] = w.turn;
    conflict["set_intensity"] = conflict["intensity"] =
        number(cT[region], "unrest") >= 90 ? "high" : "medium";
    conflict["casualties"] = conflict["displaced"] = Int(0);
    conflict["affected_regions"] = conflict["theatre"] = Json::arrayValue;
    conflict["affected_regions"].append(region);
    conflict["theatre"].append(region);
    conflict["text"] = "Sustained unrest in " + cT[region]["name"].asString() +
                       " broke into armed revolt against " + cActors[state]["name"].asString() +
                       ".";
    auto conflictId = "c" + std::to_string(w.turn) + "-r" + id.substr(0, 20);
    w.conflicts[conflictId] = conflict;
    w.actors[id]["rebellion"]["conflict"] = conflictId;
    T[region]["unrest_months"] = Int(0);
    auto &stability = w.actors[state]["status"]["stability"];
    stability = round1(std::max(0.0, stability.asDouble() - rate::revoltShock));
    r.events.push_back({id, "revolt", conflictId,
                        rebelName + " took up arms in " + cT[region]["name"].asString() +
                            " against " + cActors[state]["name"].asString() + ", opening the " +
                            conflict["name"].asString() + ".",
                        region});
    rebelledThisMonth = true;
}

// Ground moves where the population is with the rebels and the state cannot garrison. Deliberately
// not a force ratio: advanceConflict refuses insurgencies for exactly that reason, and this rule
// leaves that refusal in place.
void MonthlyStatus::advanceRebellions() {
    if (version < unrestVersion || !hasMilitary(forceStart))
        return;
    for (const auto &[rebel, conflictId] : rebellions()) {
        if (!w.conflicts.isMember(conflictId) ||
            !isOpen(static_cast<const Json::Value &>(w.conflicts)[conflictId]))
            continue;
        auto state = cActors[rebel]["rebellion"]["against"].asString();
        auto origin = cActors[rebel]["rebellion"]["origin"].asString();
        if (!w.hasActor(state) || atPeace(rebel, state))
            continue;
        std::string best;
        for (const auto &id : regionIds) {
            if (cT[id]["controller"].asString() != state || cT[id]["kind"] == "residual" ||
                seized.contains(id) || number(cT[id], "unrest") < rate::revoltGround)
                continue;
            bool reachable = id == origin;
            if (!reachable && adjacency.contains(id))
                for (const auto &n : adjacency.at(id))
                    reachable = reachable || cT[n]["controller"].asString() == rebel;
            if (!reachable)
                continue;
            double garrison = force::committedPersonnel(forceStart, adjacency, state, {id});
            if (garrison >= double(integer(cT[id], "population")) * rate::garrisonPerResident)
                continue;
            if (best.empty() || number(cT[id], "unrest") > number(cT[best], "unrest"))
                best = id;
        }
        if (best.empty())
            continue;
        T[best]["controller"] = rebel;
        seized.insert(best);
        seizedBy[best] = {rebel, state};
        r.events.push_back({rebel, "front_advance", conflictId,
                            cActors[rebel]["name"].asString() + " took control of " +
                                cT[best]["name"].asString() + " from " +
                                cActors[state]["name"].asString() + ".",
                            best});
    }
}

void MonthlyStatus::declareSecessions() {
    for (const auto &[rebel, conflictId] : rebellions()) {
        auto &rebellion = w.actors[rebel]["rebellion"];
        auto state = rebellion["against"].asString();
        std::vector<std::string> held;
        for (const auto &id : regionIds)
            if (cT[id]["controller"].asString() == rebel && cT[id]["owner"].asString() == state &&
                cT[id]["kind"] != "residual")
                held.push_back(id);
        if (held.empty()) {
            rebellion["held_since"] = -1;
            continue;
        }
        if (integer(rebellion, "held_since") < 0) {
            rebellion["held_since"] = w.turn;
            continue;
        }
        if (secededThisMonth || rebellion.get("state", false).asBool() ||
            w.turn - integer(rebellion, "held_since") < rate::secessionMonths ||
            atPeace(rebel, state))
            continue;
        for (const auto &id : held)
            T[id]["owner"] = rebel;
        auto stateName = ("Republic of " + cT[held.front()]["name"].asString()).substr(0, 120);
        std::set<std::string> names;
        for (const auto &a : w.actors.getMemberNames())
            names.insert(lowercase(cActors[a]["name"].asString()));
        if (names.contains(lowercase(stateName)))
            stateName = ("Free " + cT[held.front()]["name"].asString()).substr(0, 120);
        auto identity = w.actors[rebel];
        identity["name"] = stateName;
        reviseIdentity(identity, Json::nullValue, w.campaignId + rebel + "secession",
                       "secession:" + held.front());
        identity["rebellion"]["state"] = true;
        identity["status"]["government_since"] = campaignYear();
        w.actors[rebel] = identity;
        auto &stability = w.actors[state]["status"]["stability"];
        stability = round1(std::max(0.0, stability.asDouble() - rate::secessionShock));
        r.events.push_back({rebel, "secession", conflictId,
                            stateName + " declared independence from " +
                                cActors[state]["name"].asString() + " over " +
                                std::to_string(held.size()) + " region(s) it holds.",
                            held.front()});
        secededThisMonth = true;
    }
}

// A state that cannot put down a civil war stops being the state it was.
void MonthlyStatus::regimeCollapse() {
    for (const auto &a : w.actors.getMemberNames()) {
        if (!cActors[a]["status"].isObject())
            continue;
        bool fighting = false;
        for (const auto &c : static_cast<const Json::Value &>(w.conflicts))
            fighting = fighting || (isOpen(c) && c["kind"] == "civil_war" && isParty(c, a));
        auto &s = w.actors[a]["status"];
        bool failing = fighting && number(s, "stability", 50) < rate::collapseFloor;
        Int months = failing ? integer(s, "collapse_months") + 1 : Int(0);
        if (!failing && !s.isMember("collapse_months"))
            continue;
        s["collapse_months"] = months;
        if (months < rate::collapseMonths || s["government"] == "transitional_government")
            continue;
        s["government"] = "transitional_government";
        s["government_since"] = campaignYear();
        s["collapse_months"] = Int(0);
        r.events.push_back({a, "regime_change", a,
                            cActors[a]["name"].asString() +
                                "'s government collapsed under the civil war; a transitional "
                                "authority took its place.",
                            ""});
    }
}
} // namespace lh::detail
