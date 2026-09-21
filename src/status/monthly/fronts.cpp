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
void MonthlyStatus::addFront(const std::string &id) {
    const Json::Value &c = w.conflicts[id];
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
    for (auto &region : seized) {
        // Fighting: control changed between parties of this conflict.
        for (const auto &ch : delta.changes)
            if (ch["kind"] == "territory" && ch["id"] == region &&
                f.parties.contains(ch["after"]["controller"].asString()) &&
                (f.parties.contains(ch["before"]["controller"].asString()) ||
                 f.parties.contains(ch["before"]["owner"].asString()))) {
                f.fighting = true;
                add(region);
            }
    }
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
    if (f.intensity < 0)
        f.regions.clear();
    allAffected.insert(f.regions.begin(), f.regions.end());
    fronts.push_back(f);
}
} // namespace lh::detail
