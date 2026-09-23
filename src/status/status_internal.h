#pragma once
#include "localhistoria/status.h"
#include <map>
#include <set>
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
// A formation or region cut off from home land by enemy or neutral ground, with no sea route
// back, fights on what it holds: half its usual effectiveness, and no national reinforcement.
constexpr double isolatedSupport = 0.5;

// Unrest, on the same 0-100 scale as stability but per region and charged against whoever
// controls it.
//
// Nothing accrues unless BOTH a nation has fallen from the stability it started the campaign with
// AND the region has a grievance the campaign produced. The World Bank score the game seeds is
// "political stability and absence of violence", a percentile rank on which North Korea reads 71.4
// and Yemen 18.9; an absolute threshold would therefore set the Sahel, Yemen and Somalia alight in
// the first year of every campaign while leaving closed states untouchable. A country that is
// chronically low but steady is at its equilibrium, and this leaves it there.
constexpr double unrestDrop = 8; // points below seeded stability before anything accrues
// A fall past this adds no further pressure. It is 22 rather than a rounder 30 because stability
// reverts toward its target by stabilityReversion a month and so never arrives: measured over two
// campaign years, a medium-intensity war produces a fall of 6.9 and never reaches unrestDrop at
// all, a high-intensity one 13.0, and a high-intensity war with a fifth of the population under
// occupation 21.6. A cap above that last figure puts the top of the scale beyond anything the
// month step can deliver, which leaves the whole chain unreachable outside a synthetic test.
constexpr double unrestStressCap = 22;
// Grievance units at which a region is as aggrieved as it can usefully be. Soured relations alone
// are 1.0, which tops out below the revolt threshold however far the nation falls: it takes
// grievance the capital has refused to answer as well. Reaching it needs strained relations plus
// two demands left pending for half a year, or occupation.
constexpr double unrestGrievanceCap = 2;
// Unrest drifts toward what the situation justifies, the way stability drifts toward its target.
// A regime that vents grievance closes the gap slowly and forgets quickly; a repressive one does
// the reverse, which is why its revolts arrive later and larger.
//
// unrestRise is 0.16 rather than a tenth because the target it chases is itself slow to open: a
// nation needs about six months of its worst sustainable war before it clears unrestDrop at all,
// so a tenth of a gap per month left two years of high-intensity war, a quarter of the population
// occupied and a province with two demands ignored throughout short of even being reported.
constexpr double unrestRise = 0.16, unrestFall = 0.20, ventRise = 0.8;
constexpr double maxUnrestStep = 10;
// Grievance weights. Occupation by a foreign power is the strongest separatist signal there is; a
// front in a war the region's own controller is fighting is deliberately absent, because invaded
// populations rally and only occupied ones resist.
constexpr double grievanceOccupied = 1.6, grievanceStrained = 1.0, grievanceDemand = 0.5,
                 grievanceCooperative = -1.5;
// Devastation compounds occupation rather than standing alone: almost all displacement is war
// damage, and counting it by itself would turn every front region against its own defender.
constexpr double grievanceDisplaced = 0.8;
constexpr double demandAgeMonths = 6;            // a demand reaches its full weight after this long
constexpr double displacedGrievanceShare = 0.02; // displaced share of a region that counts
constexpr double unrestNotice = 35;              // the journal starts reporting a region here
// A revolt needs sustained unrest, and a repressive state holds the lid on for longer.
constexpr double revoltThreshold = 70, repressionHold = 25;
constexpr int revoltMonths = 4;
// A rebellion takes ground where the population is with it and the state cannot garrison, never on
// an inventory ratio: measured conventionally an army always walks into its own insurgency.
constexpr double revoltGround = 80, garrisonPerResident = 0.004;
constexpr double defectionShare = 0.25;      // of the state's personnel committed in the region
constexpr double militiaPerResident = 0.002; // volunteers a fully unresting region raises
constexpr int minimumRebels = 1500, maximumRebels = 150000;
constexpr double civilWarShare = 0.25; // rebels this large relative to the army make it a civil war
constexpr double revoltShock = 6, secessionShock = 8; // stability the state loses on the day
constexpr int secessionMonths = 6; // months a rebellion must hold ground before it is a state
// A state that cannot put down a civil war stops being the state it was.
constexpr double collapseFloor = 15;
constexpr int collapseMonths = 6;

// Conquest and resistance, realism lifecycle only.
//
// Foreign occupation justifies local unrest by itself, whatever the occupier's own stability: a
// region taken by force sits at "restive", and devastation, soured relations, a stressed occupier
// or a thin garrison push it through "unrest" toward revolt. Measured on the seeded world, occupied
// Ukraine under a garrisoned, repressive Russia settles in the "unrest" band and never reaches
// Russia's armed line (70 + 0.55 x 25), which is what the partisan record supports.
constexpr double occupationPressure = 60;
constexpr double occupationStress = 0.5; // a fully stressed occupier multiplies it by 1.5
constexpr double occupationDevastation = 10, occupationStrained = 10, occupationCooperative = -15;
constexpr double garrisonedPressure = 0.8; // a region its occupier garrisons in strength
// Demands a government leaves unanswered make a region restive on their own, each one to
// demandPressure once fully aged, but never past demandPressureCap: that path reaches the journal,
// and only a nation's real decline takes it further.
constexpr double demandPressure = 20, demandPressureCap = 40;
// Integration: an annexed conquest becomes ordinary territory only after years of peaceful rule.
// Ten years without fighting, revolt or a war with the claimant; twice as fast where the region
// cooperates with its new capital, stalled where it is at odds with it. Fighting there sets it
// back.
constexpr double integrationMonths = 120, integrationSetback = 10;
// A conqueror uses this share of a conquered region's output before integration, the rest of the
// way as integration progresses; the legal owner of land it does not control uses none.
constexpr double conqueredOutput = 0.3;

// Implementation, trade and military recovery, realism lifecycle only (Plan 26 H). These are
// scenario estimates for gameplay, not fiscal accounting: GDP is output, never a treasury balance.
//
// Each nation has one monthly implementation allowance: implementationShare of its effective annual
// output, less militaryClaim of its recorded military spending (the running cost of the forces it
// already has), but never below allowanceFloor of the envelope. Measured on the seeded world:
// Portugal about US$1.8 billion a month, Sudan US$0.38 billion, Ukraine the floor, US$0.34 billion.
constexpr double implementationShare = 0.08, militaryClaim = 0.5, allowanceFloor = 0.25;
// What a month of construction costs, in US dollars at the builder's price level (the force quality
// factor, 0.6 to 1.4): a rich nation pays more for a port and still affords far more of them.
// Assembling an army or fleet only moves existing forces and costs their logistics.
constexpr double projectCost[] = {50e6, 100e6, 150e6, 300e6, 500e6}; // army fleet base port city
// Ongoing national efforts the model may start but not price. Cost is a share of effective annual
// output, spread over the months; the benefit arrives once, on paid completion.
constexpr int investmentMonths = 12, reformMonths = 18;
constexpr double investmentCost = 0.006, reformCost = 0.003; // of annual output, in total
constexpr double investmentGain = 1.0;                       // % of output, once
constexpr double reformGain = 3.0, reformConfidence = 2.0;   // baseline points, stability points
constexpr int reformMemoryMonths = 60; // earlier reforms in this window dilute the next one
// Trade: a continuing growth bonus while agreements are open, scaled by each partner's share of
// the pair's combined output and counted once per partner however many treaties link them. Only
// the change from the campaign's first realism month counts, since seeded growth already includes
// the trade of 2026.
constexpr double tradeGrowth = 0.25; // % a year per unit of partner weight
constexpr double tradeCap = 3.0;     // partner weight counted at most
// Replacement: losses below the establishment frozen at the first realism month are made good by
// paid orders, delivered only on completion. Recruits are drawn from civilians (population is
// unchanged); aircraft and ships also occupy a construction slot.
constexpr double recruitCost = 30e3, aircraftCost = 60e6, shipCost = 600e6; // US$ per unit
constexpr int recruitMonths = 6, aircraftMonths = 18, shipMonths = 36;
constexpr double replacementBatch = 0.05; // of establishment per order, at least one unit
// Mobilising a reservist is essential in a high-intensity war but not free.
constexpr double mobiliseCost = 5e3;
// After a year without fighting, excess active personnel return to the reserve, 2% of the
// establishment a month, never below what formations hold.
constexpr int demobiliseAfter = 12;
constexpr double demobiliseShare = 0.02;

// Pacing, realism lifecycle only (Plan 26 I).
//
// Capture effort: how much superiority a region takes, relative to a typical one. Geographic extent
// is the median distance from the region's interior to its neighbours' (the map has no areas; the
// seeded median is 136 km, Donetsk 187, Texas 691), population how many people must be controlled,
// and distance how far from the attacker's heartland the supply line runs. None of these is
// terrain.
constexpr double effortExtentKm = 150;
constexpr double effortSupplyKm = 1500, effortSupplyScaleKm = 10000;
// Exhaustion, 0 to 1, lowers attacking strength by up to exhaustionCap. Each month it rises with
// the intensity fought at, the share of active personnel lost and any army cut off from home, and
// recovers in proportion to itself: slowly while fighting, faster with fully funded replacement,
// fastest in peace (a half-life of about seven months). A long high-intensity war with Russia's
// losses settles near 0.8, the old anniversary multiplier of 0.6; a new war approaches it
// gradually.
constexpr double exhaustionCap = 0.5;
constexpr double exhaustionOps[] = {0.005, 0.012, 0.02}; // by intensity: low / medium / high
constexpr double exhaustionLosses = 5, exhaustionIsolated = 0.01;
constexpr double recoveryFighting = 0.03, recoveryFunded = 0.03, recoveryPeace = 0.10;
// Wars already a year old when a campaign moves to these rules start at the old multiplier.
constexpr double exhaustionSeeded = 0.8;
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
    // The most restive region this actor controls, charged to whoever holds it rather than to
    // whoever owns it: an occupier inherits the resentment along with the ground.
    double unrest = 0;
    std::string unrestRegion;
};
std::string unrestLabel(double u);
std::map<std::string, Totals> totals(const WorldState &w);
Json::Value buildStatus(const WorldState &w, const std::string &a, const Totals &t);
std::string names(const WorldState &w, const std::vector<std::string> &parties);
std::string fallbackName(const WorldState &w, const std::string &kind, const std::string &type,
                         const std::vector<std::string> &parties);
std::vector<std::string> parties(const Json::Value &v);
double jitter(const WorldState &w, const std::string &key);
} // namespace detail
namespace force {
// Personnel an actor commits around some regions. Under the realism rules this is `engaged`
// against `opponent` (a garrison when empty); legacy rules ignore the opponent.
double committedPersonnel(const WorldState &w, const Adjacency &adj, const std::string &a,
                          const std::vector<std::string> &regions,
                          const std::string &opponent = "");
// Everyone an actor is fighting or disputing ground with.
std::set<std::string> engagements(const WorldState &w, const Adjacency &adj, const std::string &a);
} // namespace force
} // namespace lh
