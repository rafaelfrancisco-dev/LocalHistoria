#pragma once
#include "localhistoria/status.h"
#include <map>
#include <string>
#include <vector>
namespace lh {
// Population-based military rates are retained only for replay of pre-inventory turns.
// New campaigns use committed forces below; civilian and economic rates remain unchanged.
namespace rate {
constexpr double militaryPerFrontRegion[] = {40, 400, 1600}; // deaths per party per affected region
constexpr double militaryPopulationCap = 0.00008;            // of a party's recruiting population
constexpr double civilian[] = {0.000003, 0.000015, 0.00005}; // of each affected region
constexpr double displacement[] = {0.0003, 0.0015, 0.004};   // outflow from each affected region
constexpr double seizedDisplacement = 0.08;                  // one-off when a region changes hands
constexpr double residentsPerCombatant =
    20;                               // people caught in a seizure per soldier committed there
constexpr double refugeeShare = 0.35; // of the displaced who cross a border
constexpr double returnShare = 0.03;  // of the displaced who return once it is calm
constexpr double frontGdpLoss[] = {0.001, 0.004, 0.01};
constexpr double partyGdpDrag[] = {0.00005, 0.0002, 0.0005};
constexpr double displacedOutput =
    0.5; // share of per-capita output that leaves with displaced people
constexpr double occupationGdpLoss = 0.02, occupationGdpFloor = 0.4;
constexpr double stabilityWar[] = {3, 8, 15}; // lowers the level stability drifts toward
constexpr double stabilityReversion = 0.08, occupiedStability = 25, contractionStability = 5;
constexpr double maxStabilityStep = 10, maxEffectGdp = 1.5, maxEffectStability = 8;
constexpr size_t maxAffected = 12;
} // namespace rate
namespace detail {
using Int = Json::Int64;
Int integer(const Json::Value &v, const char *k);
double number(const Json::Value &v, const char *k, double fallback = 0);
int level(const std::string &intensity);
double round1(double v);
std::string grouped(Int n);
std::string scaled(double v);
std::string people(Int n);
std::string stabilityLabel(double s);
// Largest-remainder split of an amount across weighted targets, in stable ID order.
std::map<std::string, Int> distribute(Int amount, const std::map<std::string, double> &weights);
struct Totals {
    Int population = 0, gdp = 0, displaced = 0;
    int occupied = 0, occupying = 0;
};
std::map<std::string, Totals> totals(const WorldState &w);
Json::Value buildStatus(const WorldState &w, const std::string &a, const Totals &t);
std::string names(const WorldState &w, const std::vector<std::string> &parties);
std::string fallbackName(const WorldState &w, const std::string &kind, const std::string &type,
                         const std::vector<std::string> &parties);
std::vector<std::string> parties(const Json::Value &v);
double jitter(const WorldState &w, const std::string &key);
} // namespace detail
namespace force {
double committedPersonnel(const WorldState &w, const Adjacency &adj, const std::string &a,
                          const std::vector<std::string> &regions);
} // namespace force
} // namespace lh
