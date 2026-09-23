#include "localhistoria/politics.h"
#include "status/monthly/monthly_status.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
namespace lh::detail {
namespace {
// Demand IDs are event IDs, which always begin "t<turn>-"; that is where a demand's age comes from
// without storing a second copy of it.
int demandTurn(const std::string &id) {
    if (id.size() < 2 || id[0] != 't')
        return 0;
    size_t end = id.find('-');
    return end == std::string::npos ? 0 : std::atoi(id.substr(1, end - 1).c_str());
}
const Json::Value &view(const Json::Value &collection, const std::string &key) {
    return collection[key];
}
} // namespace

int MonthlyStatus::campaignYear() const { return 2026 + int(std::floor(w.turn / 12.0)); }

std::map<std::string, std::string> MonthlyStatus::rebellions() const {
    std::map<std::string, std::string> out;
    for (const auto &id : w.actors.getMemberNames()) {
        const auto &rebellion = cActors[id]["rebellion"];
        if (rebellion.isObject() && rebellion["conflict"].isString())
            out[id] = rebellion["conflict"].asString();
    }
    return out;
}
// A state the adjudicator founded this month already spent the month's allowance.
bool MonthlyStatus::newStateThisMonth() const {
    for (const auto &c : delta.changes)
        if (c["kind"] == "actor" && c["before"].isNull())
            return true;
    return false;
}

// How far a nation has fallen from the stability it began the campaign with, 0 to 1. Measured
// against the frozen January figure rather than baseline_stability, which ratchets with every model
// effect, and gated so a country that is chronically low but steady contributes nothing.
double MonthlyStatus::nationalStress(const std::string &a) const {
    const auto &s = cActors[a]["status"];
    if (!s.isObject() || !regime(s.get("government", "").asString()).inhabited)
        return 0;
    double seeded = number(s, "seeded_stability", number(s, "baseline_stability", 50));
    double fall = seeded - number(s, "stability", 50);
    if (fall <= rate::unrestDrop)
        return 0;
    return std::min(1.0, (fall - rate::unrestDrop) / (rate::unrestStressCap - rate::unrestDrop));
}

// What this region has to be aggrieved about, charged against whoever controls it. A front in a war
// its own controller is fighting is deliberately not a grievance: invaded populations rally, and
// only occupied ones resist. Without that rule Kharkiv revolts against Kyiv.
double MonthlyStatus::regionalGrievance(const std::string &id) const {
    const auto &t = cT[id];
    if (t["kind"] == "residual" || integer(t, "population") <= 0)
        return 0;
    auto owner = t["owner"].asString(), controller = t["controller"].asString();
    double g = 0;
    double population = double(integer(t, "population"));
    // Under the realism rules a foreign occupation is its own path, occupationPressure, independent
    // of the occupier's decline; this one keeps only rule by a movement from the region itself.
    bool occupied = !owner.empty() && !controller.empty() && owner != controller;
    if (realism() && occupied)
        occupied = !hostileControl(w, id).isObject() && !agreedOccupation(w, id, controller);
    if (occupied) {
        g += rate::grievanceOccupied;
        // Devastation compounds occupation but is never a grievance on its own: almost all
        // displacement is war damage, and a population does not turn on the government fighting to
        // defend it because that fighting hurt.
        if (population > 0 &&
            double(integer(t, "displaced")) / population >= rate::displacedGrievanceShare)
            g += rate::grievanceDisplaced;
    }
    const auto &p = view(static_cast<const Json::Value &>(w.regionalPolitics), id);
    if (p["relations"] == "strained")
        g += rate::grievanceStrained;
    else if (p["relations"] == "cooperative")
        g += rate::grievanceCooperative;
    for (const auto &d : p["demands"])
        if (d["status"] == "pending") {
            double age = w.turn - demandTurn(d["id"].asString());
            g += rate::grievanceDemand * std::clamp(age / rate::demandAgeMonths, 0.0, 1.0);
        }
    return std::max(0.0, g);
}

// What a foreign occupation justifies on its own. The occupier's stress, devastation and the
// region's relations with its new rulers raise it; a strong garrison and integration lower it.
double MonthlyStatus::occupationPressure(const std::string &id) const {
    const auto &record = hostileControl(w, id);
    if (!record.isObject())
        return 0;
    const auto &t = cT[id];
    auto controller = t["controller"].asString();
    double p = rate::occupationPressure * (1 - number(record, "integration") / 100) *
               (1 + rate::occupationStress * nationalStress(controller));
    double population = double(integer(t, "population"));
    if (population > 0 &&
        double(integer(t, "displaced")) / population >= rate::displacedGrievanceShare)
        p += rate::occupationDevastation;
    const auto &politics = view(static_cast<const Json::Value &>(w.regionalPolitics), id);
    if (politics["relations"] == "strained")
        p += rate::occupationStrained;
    else if (politics["relations"] == "cooperative")
        p += rate::occupationCooperative;
    if (force::committedPersonnel(forceStart, adjacency, controller, {id}) >=
        population * rate::garrisonPerResident)
        p *= rate::garrisonedPressure;
    return std::clamp(p, 0.0, 100.0);
}
// Demands left unanswered make a region restive without any national decline, up to a bound.
double MonthlyStatus::demandPressure(const std::string &id) const {
    double p = 0;
    for (const auto &d : view(static_cast<const Json::Value &>(w.regionalPolitics), id)["demands"])
        if (d["status"] == "pending")
            p += rate::demandPressure *
                 std::clamp((w.turn - demandTurn(d["id"].asString())) / rate::demandAgeMonths, 0.0,
                            1.0);
    return std::min(p, rate::demandPressureCap);
}

void MonthlyStatus::accrueUnrest() {
    for (const auto &id : regionIds) {
        auto controller = cT[id]["controller"].asString();
        if (controller.empty() || !cActors[controller]["status"].isObject())
            continue;
        double grievance = std::min(regionalGrievance(id), rate::unrestGrievanceCap);
        double target = 100 * nationalStress(controller) * grievance / rate::unrestGrievanceCap;
        if (realism() && cT[id]["kind"] != "residual" && integer(cT[id], "population") > 0)
            target = std::max({target, occupationPressure(id), demandPressure(id)});
        double unrest = number(cT[id], "unrest");
        const auto &g = regime(cActors[controller]["status"].get("government", "").asString());
        double share = target > unrest ? rate::unrestRise * (1 - rate::ventRise * g.vent)
                                       : rate::unrestFall * (1 + g.vent);
        double step =
            std::clamp((target - unrest) * share, -rate::maxUnrestStep, rate::maxUnrestStep);
        double next = std::clamp(unrest + step, 0.0, 100.0);
        // A region that never stirs keeps no field at all, so a quiet campaign saves exactly what
        // it saved before this rule existed.
        if (next <= 0 && !cT[id].isMember("unrest"))
            continue;
        auto band = unrestLabel(next);
        if (band != unrestLabel(unrest) &&
            (next >= rate::unrestNotice || unrest >= rate::unrestNotice))
            r.events.push_back({controller, "unrest", id,
                                cT[id]["name"].asString() + " is " +
                                    (realism() && band == "unrest" ? "in unrest" : band) +
                                    " under " + cActors[controller]["name"].asString() + ".",
                                id});
        T[id]["unrest"] = round1(next);
        double armed = rate::revoltThreshold + g.repression * rate::repressionHold;
        T[id]["unrest_months"] = next >= armed ? integer(cT[id], "unrest_months") + 1 : Int(0);
    }
}

void MonthlyStatus::formRebellions() {
    // One a month, campaign-wide, and never a second one against a state that is already fighting
    // this region's rebellion.
    std::set<std::string> spokenFor;
    if (!realism())
        for (const auto &c : static_cast<const Json::Value &>(w.conflicts))
            if (isOpen(c))
                for (const auto *key : {"affected_regions", "theatre"})
                    for (const auto &region : c[key])
                        spokenFor.insert(region.asString());
    std::string best;
    for (const auto &id : regionIds) {
        if (integer(cT[id], "unrest_months") < rate::revoltMonths || spokenFor.contains(id))
            continue;
        auto controller = cT[id]["controller"].asString();
        if (realism() && represented(id, controller))
            continue;
        if (controller.empty() || !cActors[controller]["status"]["military"].isObject())
            continue;
        if (best.empty() || number(cT[id], "unrest") > number(cT[best], "unrest"))
            best = id;
    }
    if (!best.empty())
        raiseRebellion(best);
}

// Under the realism rules a region already has its rising only when a civil war or insurgency its
// controller is fighting reaches it, or a rebellion against that controller holds ground next door
// and would carry the revolt there itself. Another war passing through is no substitute for one.
bool MonthlyStatus::represented(const std::string &id, const std::string &controller) const {
    for (const auto &c : static_cast<const Json::Value &>(w.conflicts))
        if (isOpen(c) && (c["kind"] == "civil_war" || c["kind"] == "insurgency") &&
            isParty(c, controller))
            for (const auto *key : {"affected_regions", "theatre"})
                for (const auto &region : c[key])
                    if (region == id)
                        return true;
    if (auto at = adjacency.find(id); at != adjacency.end())
        for (const auto &n : at->second) {
            const auto &rebellion = cActors[cT[n]["controller"].asString()]["rebellion"];
            if (rebellion.isObject() && rebellion["against"] == controller &&
                !rebellion.get("state", false).asBool())
                return true;
        }
    return false;
}

// Control history: opened, moved or ended by whatever changed hands this month, and integration
// for annexed conquests at peace with their claimant and short of revolt. Resentment alone does
// not stop it, since the occupation itself keeps a conquest restive; fighting there sets it back.
void MonthlyStatus::controlStep() {
    recordControl(w, false);
    for (const auto &id : regionIds) {
        const auto &record = hostileControl(w, id);
        if (!record.isObject())
            continue;
        auto holder = record["holder"].asString(), claimant = record["claimant"].asString();
        double integration = number(record, "integration"), was = integration;
        const auto &politics = view(static_cast<const Json::Value &>(w.regionalPolitics), id);
        if (allAffected.contains(id))
            integration = std::max(0.0, integration - rate::integrationSetback);
        else if (cT[id]["owner"] == holder && !fighting(w, holder, claimant) &&
                 number(cT[id], "unrest") < rate::revoltThreshold &&
                 politics["relations"] != "strained")
            integration +=
                100 / rate::integrationMonths * (politics["relations"] == "cooperative" ? 2 : 1);
        if (integration >= 100) {
            T[id].removeMember("control");
            r.events.push_back({holder, "integrated", id,
                                cT[id]["name"].asString() + " is now fully integrated into " +
                                    cActors[holder]["name"].asString() + ".",
                                id});
            continue;
        }
        if (integration != was)
            T[id]["control"]["integration"] = std::round(integration * 100) / 100;
    }
}

void MonthlyStatus::unrestStep() {
    if (version < unrestVersion)
        return;
    secededThisMonth = newStateThisMonth();
    accrueUnrest();
    declareSecessions();
    if (!rebelledThisMonth)
        formRebellions();
    regimeCollapse();
}
} // namespace lh::detail
