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
// A front moves on its own only where one side is decisively superior: assaultCapacity 2, which
// is the same 1.5x threshold the compiler already uses to allow two seizures a month. A merely
// contested front (capacity 1) costs lives and gains nothing, which is what most real wars do.
// Pressure per month scales with how far past that threshold the ratio sits, and a region changes
// hands when it reaches pressureTake.
constexpr double pressureGain[] = {0.0, 0.25, 0.45}; // by declared intensity: low / medium / high
constexpr double pressureFloor = 1.5;                // the capacity-2 threshold, in ratio terms
constexpr double pressureRatioCap = 8.0;             // a walkover is still one front at a time
constexpr double pressureTake = 1.0;                 // pressure spent per region taken
constexpr double pressureDecay = 0.5; // share of a month's gain a stalled attacker loses
// A front that has not moved for this long is a stalemate: its belligerents are told so, which is
// what gives them a reason to talk. It never scales the toll down: a static front is not a quiet
// one.
constexpr int warLullMonths = 6;
// Reservists count toward defence only: a mobilisation base for holding home ground, never an
// expeditionary pool available next month.
constexpr double reserveDefence = 0.05;
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
// {attack, defend, capacity} for an assault on one region. assaultCapacity() returns only the
// band; the monthly front model needs the ratio behind it.
Json::Value assault(const WorldState &w, const Adjacency &adj, const std::string &a,
                    const std::string &d, const std::string &r);
} // namespace force
} // namespace lh
