#pragma once
#include "localhistoria/conflicts.h"
#include "status/status_internal.h"
#include <map>
#include <optional>
#include <set>
namespace lh::detail {
// Where an open conflict is fought this month.
struct Front {
    std::string id;
    std::set<std::string> parties;
    std::vector<std::string> regions;
    int intensity;
    bool fighting;
    // Realism: how much of the front's toll and flight each region bears (1 = fully).
    std::map<std::string, double> weight;
    double of(const std::string &region) const {
        auto at = weight.find(region);
        return at == weight.end() ? 1.0 : at->second;
    }
};
// One month of applyMonthlyStatus: each numbered step is a method over the shared month state.
class MonthlyStatus {
  public:
    MonthlyStatus(const Resolution &base, const WorldDelta &delta, const Adjacency &adjacency,
                  int version);
    Resolution run();

  private:
    bool realism() const { return base.lifecycleVersion == 1; }
    void freezeForceInputs();
    void accountDestroyed();
    const Json::Value &ownerStatus(const Json::Value &t) const;
    void inheritStatus();
    // 1–3
    void baselineTrend();
    void modelEffects();
    void occupationDrain();
    // 4
    void collectSeized();
    double seizedShare(const std::string &region, double population);
    // 4b
    // Where an attacker would push next against one defender, and the odds it faces there.
    struct Axis {
        std::string region;
        double ratio = 0;
        int capacity = 0;
    };
    bool atPeace(const std::string &a, const std::string &b) const;
    void advanceFronts();
    void advanceConflict(const std::string &id, const Json::Value *pair = nullptr);
    Axis bestAxis(const std::string &attacker, const std::string &defender) const;
    double accrue(const std::string &id, const std::string &attacker, const std::string &defender,
                  int declared) const;
    bool takeGround(const std::string &id, const std::string &attacker, const std::string &defender,
                    double &held);
    void addFront(const std::string &id, const Json::Value *pair = nullptr);
    void weighFront(Front &f) const;
    // 5d (realism): how worn down each nation's forces are.
    void exhaustionStep();
    std::set<std::string> engagedPairs;
    // 5
    Int move(const std::string &from, const std::string &to, Int count);
    // Realism: the displacement ledger. People displaced from each origin, by the region hosting
    // them now, loaded from the origin territories at the start of the month and written back once
    // every movement, death and return has been counted.
    void loadDisplacement();
    void saveDisplacement();
    // Who among `count` of a region's people leave or die: residents, and each origin's hosted
    // people in proportion to their share of `population`. Residents are keyed "".
    std::map<std::string, Int> cohorts(const std::string &region, Int count, Int population) const;
    void forget(const std::string &host, const std::map<std::string, Int> &gone);
    Int flee(const std::string &from, const std::string &to, Int count);
    std::map<std::string, std::map<std::string, Int>> hosted; // host -> origin -> people
    Int kill(const std::string &region, Int count);
    void warToll(const Front &f, std::map<std::string, Totals> &current);
    Int partyLosses(const Front &f, const std::string &p, std::map<std::string, Totals> &current,
                    Int affectedPopulation, double multiplier, const std::string &kind);
    void displace(const Front &f, const std::string &region, double multiplier, Int &deaths,
                  Int &displaced);
    // Realism: what one side of a pair's front exposes this month, and the losses it takes.
    struct Exposure {
        double air = 0, ships = 0;
        std::map<std::string, double> fleets; // fleet ID ("" the national pool) -> ships
    };
    Exposure exposure(const std::string &p, const std::string &q, const Front &f) const;
    Int realismLosses(const Front &f, const std::string &p, double multiplier,
                      const std::string &kind);
    // 1b, 3b and 5c (realism): trade, the implementation allowance and what it pays for.
    void tradeStep();
    std::map<std::string, double> outputs() const;
    std::set<std::string> tradePartners(const std::string &a) const;
    void allowances();
    void implementation();
    void fund(const std::string &a);
    void recover(const std::string &a);
    void complete(const std::string &a, const std::string &category);
    // Effective output after the occupation drain, the allowance it supports and what is left of
    // it.
    std::map<std::string, double> effective, budget, allowance;
    // 5b
    void applyInventory(const std::string &a);
    void applyRealismInventory(const std::string &a);
    // 6–7
    void returnDisplaced();
    void driftStability();
    // 7b
    void controlStep();
    // 4c and 8: unrest and what it eventually does. Ground moves with the conventional advances so
    // the toll, displacement and front drawing treat a rebel seizure exactly like any other; the
    // rest runs last, because it reads the stability this month finished with.
    void advanceRebellions();
    void unrestStep();
    double nationalStress(const std::string &actor) const;
    double regionalGrievance(const std::string &region) const;
    double occupationPressure(const std::string &region) const;
    bool armedMovementRule(const std::string &region) const;
    double demandPressure(const std::string &region) const;
    void accrueUnrest();
    void formRebellions();
    bool represented(const std::string &region, const std::string &controller) const;
    void raiseRebellion(const std::string &region);
    void declareSecessions();
    void regimeCollapse();
    // Rebel actors created by earlier months, as {rebel actor -> conflict ID}.
    std::map<std::string, std::string> rebellions() const;
    bool newStateThisMonth() const;
    int campaignYear() const;

    const Resolution &base;
    const WorldDelta &delta;
    const Adjacency &adjacency;
    // The status rules this month was played under; replay passes the turn's recorded value.
    const int version;
    Resolution r;
    WorldState &w;
    Json::Value &T;
    // Reads go through const views so missing members are never inserted as nulls.
    const Json::Value &cT, &cActors;
    WorldState forceStart;
    std::vector<std::string> regionIds;
    std::map<std::string, Int> militaryLosses, aircraftLosses, shipLosses, actorDeaths;
    // Realism: this month's losses before rounding, and where they fall, by actor. Personnel
    // weights are by army ID with "" for the national pool; ship weights by fleet ID likewise.
    std::map<std::string, double> personnelLoss, aircraftLoss, shipLoss;
    std::map<std::string, std::map<std::string, double>> personnelWeights, shipWeights;
    std::set<std::string> mobilising;
    std::map<std::string, double> effectGdp, effectStability;
    std::set<std::string> seized;
    // Actors that own at least one region, as the month began; filled on first use.
    mutable std::optional<std::set<std::string>> landowners;
    // Seizures this month's orders made, as opposed to momentum.
    std::set<std::string> ordered;
    std::map<std::string, std::pair<std::string, std::string>>
        seizedBy; // region -> (new controller, previous controller)
    std::vector<Front> fronts;
    std::set<std::string> allAffected;
    std::map<std::string, Totals> before;
    // Pacing, shared by the model's operations and this step: one rebellion and one new state a
    // month, campaign-wide.
    bool rebelledThisMonth = false, secededThisMonth = false;
};
} // namespace lh::detail
