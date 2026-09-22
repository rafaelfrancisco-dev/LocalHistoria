#pragma once
#include "status/status_internal.h"
#include <map>
#include <set>
namespace lh::detail {
// Where an open conflict is fought this month.
struct Front {
    std::string id;
    std::set<std::string> parties;
    std::vector<std::string> regions;
    int intensity;
    bool fighting;
};
// One month of applyMonthlyStatus: each numbered step is a method over the shared month state.
class MonthlyStatus {
  public:
    MonthlyStatus(const Resolution &base, const WorldDelta &delta, const Adjacency &adjacency,
                  int version);
    Resolution run();

  private:
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
    void advanceConflict(const std::string &id);
    Axis bestAxis(const std::string &attacker, const std::string &defender) const;
    double accrue(const std::string &id, const std::string &attacker, const std::string &defender,
                  int declared) const;
    bool takeGround(const std::string &id, const std::string &attacker, const std::string &defender,
                    double &held);
    void addFront(const std::string &id);
    // 5
    Int move(const std::string &from, const std::string &to, Int count);
    Int kill(const std::string &region, Int count);
    void warToll(const Front &f, std::map<std::string, Totals> &current);
    Int partyLosses(const Front &f, const std::string &p, std::map<std::string, Totals> &current,
                    Int affectedPopulation, double multiplier, const std::string &kind);
    void displace(const Front &f, const std::string &region, double multiplier, Int &deaths,
                  Int &displaced);
    // 5b
    void applyInventory(const std::string &a);
    // 6–7
    void returnDisplaced();
    void driftStability();
    // 4c and 8: unrest and what it eventually does. Ground moves with the conventional advances so
    // the toll, displacement and front drawing treat a rebel seizure exactly like any other; the
    // rest runs last, because it reads the stability this month finished with.
    void advanceRebellions();
    void unrestStep();
    double nationalStress(const std::string &actor) const;
    double regionalGrievance(const std::string &region) const;
    void accrueUnrest();
    void formRebellions();
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
    std::set<std::string> mobilising;
    std::map<std::string, double> effectGdp, effectStability;
    std::set<std::string> seized;
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
