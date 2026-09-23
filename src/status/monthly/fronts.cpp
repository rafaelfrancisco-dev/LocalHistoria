#include "status/monthly/monthly_status.h"
#include <algorithm>
namespace lh::detail {
void MonthlyStatus::collectSeized() {
    for (const auto &c : delta.changes) {
        if (c["kind"] == "territory" && !c["before"].isNull() &&
            c["before"]["controller"] != c["after"]["controller"]) {
            seized.insert(c["id"].asString());
            seizedBy[c["id"].asString()] = {c["after"]["controller"].asString(),
                                            c["before"]["controller"].asString()};
        }
    }
}
// The one-off flight on seizure reaches the people the fighting reaches: with inventories, at
// most residentsPerCombatant per soldier both sides committed there, not a whole province.
double MonthlyStatus::seizedShare(const std::string &region, double population) {
    if (!seized.contains(region))
        return 0.0;
    if (population <= 0)
        return rate::seizedDisplacement;
    auto exposed = population;
    const auto &[taker, loser] = seizedBy[region];
    if (static_cast<const WorldState &>(forceStart)
            .actors[taker]["status"]["military"]
            .isObject()) {
        double committed = force::committedPersonnel(forceStart, adjacency, taker, {region});
        if (static_cast<const WorldState &>(forceStart)
                .actors[loser]["status"]["military"]
                .isObject())
            committed += force::committedPersonnel(forceStart, adjacency, loser, {region});
        exposed = std::min(population, rate::residentsPerCombatant * committed);
    }
    return exposed == population ? rate::seizedDisplacement
                                 : rate::seizedDisplacement * exposed / population;
}
// Two parties of the same conflict who also share an open truce or pact are not fighting each
// other: a conflict records parties, not sides, so co-belligerents appear opposed without this.
bool MonthlyStatus::atPeace(const std::string &a, const std::string &b) const {
    if (realism())
        return !fighting(w, a, b);
    static const std::set<std::string> peaceful{"ceasefire",    "peace_treaty",        "alliance",
                                                "defence_pact", "non_aggression_pact", "union"};
    for (const auto &k : static_cast<const Json::Value &>(w.commitments)) {
        if (!isOpen(k) || !peaceful.contains(k["kind"].asString()))
            continue;
        bool first = false, second = false;
        for (auto &party : k["actors"]) {
            first = first || party == a;
            second = second || party == b;
        }
        if (first && second)
            return true;
    }
    return false;
}
// A war moves ground on its own where one side is decisively superior; a contested front costs
// lives and gains nothing. Everything here reads the start-of-month world, the delta and adjacency,
// so replay recomputes it exactly.
void MonthlyStatus::advanceFronts() {
    if (version < frontMomentumVersion || !hasMilitary(forceStart))
        return;
    std::set<std::string> seen;
    for (auto &id : w.conflicts.getMemberNames()) {
        if (!realism()) {
            advanceConflict(id);
            continue;
        }
        const auto &c = w.conflicts[id];
        for (const auto &pair : hostilePairs(w, c)) {
            auto a = pair["actors"][0].asString(), b = pair["actors"][1].asString();
            if (!fightingPair(w, c, a, b) || !seen.insert(hostilePairKey(a, b)).second)
                continue;
            auto scoped = c;
            scoped["actors"] = pair["actors"];
            const char *levels[]{"low", "medium", "high"};
            int posture = pairPosture(c, a, b);
            scoped["set_intensity"] = posture < 0 ? "none" : levels[posture];
            advanceConflict(id, &scoped);
        }
    }
}
void MonthlyStatus::advanceConflict(const std::string &id, const Json::Value *pair) {
    const Json::Value &c = pair ? *pair : w.conflicts[id];
    if (!isOpen(c))
        return;
    // Only conventional war moves on a balance of forces. An insurgency or a civil war turns on
    // who the population and the defectors side with, which no inventory ratio captures: measured
    // conventionally, Congo's army walks into M23's Kivu, when in reality it is losing ground
    // there. Those fronts move when the adjudicator says they do.
    auto kind = c.get("kind", "war").asString();
    if (kind != "war" && kind != "border_conflict")
        return;
    // The declared level is what the parties have chosen to fight at; the effective intensity is
    // not known until addFront runs, and deriving one from the other would be circular.
    auto declared = level(c.get("set_intensity", "medium").asString());
    if (declared <= 0)
        return;
    std::set<std::string> parties;
    for (auto &a : c["actors"])
        if (a.isString() && w.hasActor(a.asString()))
            parties.insert(a.asString());
    // One pressure gain per attacker per month, even in a conflict with several opponents.
    // Choose the strongest reachable axis with stable actor/region ordering for ties.
    for (const auto &attacker : parties) {
        std::string opponent;
        double best = -1;
        for (const auto &defender : parties) {
            if (attacker == defender || atPeace(attacker, defender))
                continue;
            auto axis = bestAxis(attacker, defender);
            if (!axis.region.empty() && axis.ratio > best) {
                best = axis.ratio;
                opponent = defender;
            }
        }
        double held = opponent.empty()
                          ? std::max(0.0, c["pressure"].get(attacker, 0.0).asDouble() -
                                              rate::pressureGain[declared] * rate::pressureDecay)
                          : accrue(id, attacker, opponent, declared);
        if (!opponent.empty())
            while (takeGround(id, attacker, opponent, held))
                ;
        auto &pressure = w.conflicts[id]["pressure"];
        if (held >= 0.005)
            pressure[attacker] = std::round(held * 100) / 100;
        else
            pressure.removeMember(attacker);
        if (pressure.empty())
            w.conflicts[id].removeMember("pressure");
    }
}
// The region this attacker would take next from this defender, and what the odds there are.
MonthlyStatus::Axis MonthlyStatus::bestAxis(const std::string &attacker,
                                            const std::string &defender) const {
    Axis best;
    if (!forceStart.actors[attacker]["status"]["military"].isObject() ||
        !forceStart.actors[defender]["status"]["military"].isObject())
        return best;
    for (const auto &region : regionIds) {
        if (cT[region]["controller"].asString() != defender || cT[region]["kind"] == "residual" ||
            seized.contains(region) || !reaches(forceStart, adjacency, attacker, region))
            continue;
        auto f = force::assault(forceStart, adjacency, attacker, defender, region);
        double attack = f["attack"].asDouble(), defend = f["defend"].asDouble();
        double ratio = defend > 0 ? attack / defend : (attack > 0 ? rate::pressureRatioCap : 0);
        if (best.region.empty() || ratio > best.ratio)
            best = {region, ratio, f["capacity"].asInt()};
    }
    return best;
}
// This month's pressure for one attacker against one defender, added to what it had banked.
double MonthlyStatus::accrue(const std::string &id, const std::string &attacker,
                             const std::string &defender, int declared) const {
    double held = w.conflicts[id]["pressure"].get(attacker, 0.0).asDouble();
    auto axis = bestAxis(attacker, defender);
    // Below decisive superiority a front does not move at all, and a repelled attacker bleeds off
    // what it banked rather than hoarding it until the day it finally wins a skirmish.
    if (axis.region.empty() || axis.capacity < 2)
        return std::max(0.0, held - rate::pressureGain[declared] * rate::pressureDecay);
    return held +
           rate::pressureGain[declared] *
               std::log2(std::clamp(axis.ratio, rate::pressureFloor, rate::pressureRatioCap) /
                         rate::pressureFloor) *
               jitter(forceStart, (realism() ? hostilePairKey(attacker, defender) : id) +
                                      ":pressure:" + attacker);
}
// Spends banked pressure on one region. Returns true when ground changed hands, so the caller can
// look for a second; `held` is debited in place.
bool MonthlyStatus::takeGround(const std::string &id, const std::string &attacker,
                               const std::string &defender, double &held) {
    if (held < rate::pressureTake)
        return false;
    // The compiler's own budget, applied to the same start-of-month world: at most two regions by
    // force a month, and never more than this attacker's capacity against this defender.
    int total = 0, against = 0;
    for (const auto &[region, sides] : seizedBy)
        if (sides.first == attacker) {
            ++total;
            against += sides.second == defender;
        }
    auto axis = bestAxis(attacker, defender);
    if (total >= 2 || axis.region.empty() || axis.capacity < 2 || against >= axis.capacity)
        return false;
    held -= rate::pressureTake;
    // Control only: taking ground never transfers ownership, so conquest still needs the
    // deliberate second step of annexation or a treaty.
    T[axis.region]["controller"] = attacker;
    seized.insert(axis.region);
    seizedBy[axis.region] = {attacker, defender};
    r.events.push_back({attacker, "front_advance", id,
                        cActors[attacker]["name"].asString() + " forces took " +
                            cT[axis.region]["name"].asString() + " from " +
                            cActors[defender]["name"].asString() + " in the " +
                            displayName(w, "conflict", id) + ".",
                        axis.region});
    return true;
}
void MonthlyStatus::addFront(const std::string &id, const Json::Value *pair) {
    const Json::Value &c = pair ? *pair : w.conflicts[id];
    if (!isOpen(c))
        return;
    Front f{id, {}, {}, -1, false};
    for (auto &a : c["actors"])
        f.parties.insert(a.asString());
    auto add = [&](const std::string &region) {
        if (cT.isMember(region) && cT[region]["kind"] != "residual" &&
            std::find(f.regions.begin(), f.regions.end(), region) == f.regions.end())
            f.regions.push_back(region);
    };
    auto held = [&](const std::string &region) {
        return f.parties.contains(cT[region]["controller"].asString()) ||
               f.parties.contains(cT[region]["owner"].asString());
    };
    // Fighting: control changed between parties of this conflict, whether an operation moved it or
    // the balance of forces did.
    for (auto &region : seized) {
        // Preserve the original before-owner check for historical replay, including a cession
        // that changed ownership and control together.
        if (version < frontMomentumVersion) {
            for (const auto &ch : delta.changes)
                if (ch["kind"] == "territory" && ch["id"] == region &&
                    f.parties.contains(ch["after"]["controller"].asString()) &&
                    (f.parties.contains(ch["before"]["controller"].asString()) ||
                     f.parties.contains(ch["before"]["owner"].asString()))) {
                    f.fighting = true;
                    add(region);
                }
            continue;
        }
        auto at = seizedBy.find(region);
        if (at == seizedBy.end())
            continue;
        const auto &[taker, loser] = at->second;
        if (f.parties.contains(taker) &&
            (f.parties.contains(loser) || f.parties.contains(cT[region]["owner"].asString()))) {
            f.fighting = true;
            add(region);
        }
    }
    const bool groundChanged = f.fighting;
    for (const auto &ch : delta.changes)
        if (ch["kind"] == "entity" && ch["after"]["status"] == "destroyed" &&
            ch["before"]["status"] != "destroyed" &&
            f.parties.contains(ch["after"]["actor"].asString()) &&
            held(ch["after"]["region"].asString())) {
            f.fighting = true;
            add(ch["after"]["region"].asString());
        }
    for (auto &region : c["theatre"])
        if (cT.isMember(region.asString()) && held(region.asString()))
            add(region.asString());
    std::vector<std::string> border;
    for (auto &region : regionIds) {
        auto p = cT[region]["controller"].asString();
        if (!f.parties.contains(p) || cT[region]["kind"] == "residual" ||
            !adjacency.contains(region))
            continue;
        for (auto &n : adjacency.at(region)) {
            auto q = cT[n]["controller"].asString();
            if (cT.isMember(n) && q != p && f.parties.contains(q) && cT[n]["kind"] != "residual") {
                border.push_back(region);
                break;
            }
        }
    }
    for (auto &region : border)
        add(region);
    if (f.regions.size() > rate::maxAffected)
        f.regions.resize(rate::maxAffected);
    bool ceasefire = false;
    for (const auto &k : static_cast<const Json::Value &>(w.commitments)) {
        auto kind = k["kind"].asString();
        if (!isOpen(k) || (kind != "ceasefire" && kind != "peace_treaty"))
            continue;
        int shared = 0;
        for (auto &a : k["actors"])
            shared += f.parties.contains(a.asString());
        ceasefire = ceasefire || shared >= 2;
    }
    auto declared = level(c.get("set_intensity", "medium").asString());
    // Months since ground last changed hands between these parties. A war neither side can move is
    // the normal state of most real wars, and it is what gives them a reason to talk.
    int stalled = groundChanged ? 0 : integer(c, "static_months") + 1;
    if (realism())
        ceasefire = false; // The pair was filtered by the shared relationship helper.
    if (ceasefire)
        f.intensity = -1;
    else if (f.fighting)
        f.intensity = 2;
    else if (f.regions.empty())
        f.intensity = std::min(declared, 0);
    else
        f.intensity = std::max(declared, 0);
    // A newly declared conflict with no ground changing hands has not yet escalated to full
    // war.
    if (!f.fighting && f.intensity > 1 && c["started_turn"].isInt() &&
        w.turn - c["started_turn"].asInt() < 3)
        f.intensity = 1;
    // A static front is not a quiet one: the Russo-Ukrainian line has barely moved for years and
    // still kills six figures a year, as Verdun did. static_months therefore only records the
    // stalemate for the parties to reason about; it never scales the toll down.
    if (f.intensity < 0)
        f.regions.clear();
    if (version >= frontMomentumVersion)
        w.conflicts[id]["static_months"] = stalled;
    allAffected.insert(f.regions.begin(), f.regions.end());
    fronts.push_back(f);
}
} // namespace lh::detail
