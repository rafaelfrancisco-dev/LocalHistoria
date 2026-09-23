#pragma once
#include "world.h"
#include <vector>
namespace lh {
// Reference inputs frozen into a campaign: national statistics keyed by actor code, regional
// population/GDP shares keyed by territory ID, and the January 2026 wars and treaties.
struct StatusSeed {
    Json::Value countries{Json::objectValue}, regions{Json::objectValue}, world,
        military{Json::objectValue};
};
Json::Value encode(const StatusSeed &);
StatusSeed decodeSeed(const Json::Value &);
// Adds national status and regional population/GDP; with worldEvents, also the seeded wars,
// treaties, occupations and non-state actors. Only for new campaigns or saves that predate national
// status.
void seedStatus(WorldState &, const StatusSeed &, bool worldEvents);
bool hasMilitary(const WorldState &);
void seedMilitary(WorldState &, const StatusSeed &);
// A save made before political systems existed has actors with a status but no government.
bool hasGovernment(const WorldState &);
void seedGovernment(WorldState &, const StatusSeed &);
namespace force {
double quality(const WorldState &, const std::string &);
// Strength an actor brings to one region, attacking or defending. `opponent`, the other side,
// sets which engagement the realism rules charge it to; legacy rules ignore it.
Json::Value committedForce(const WorldState &, const Adjacency &, const std::string &actor,
                           const std::string &region, bool defending,
                           const std::string &opponent = "");
int assaultCapacity(const WorldState &, const Adjacency &, const std::string &, const std::string &,
                    const std::string &);
// {attack, defend, capacity} for an assault on one region. assaultCapacity() returns only the
// band; the monthly front model needs the ratio behind it.
Json::Value assault(const WorldState &w, const Adjacency &adj, const std::string &a,
                    const std::string &d, const std::string &r);
// The realism allocation, one engagement at a time: the actor's national pool divided across
// every engagement (those it is already in, and this opponent), plus each active army in or next
// to the regions at the share it can spare for this enemy. Summed over all its enemies it never
// exceeds the actor's active strength. `parts`, when given, receives each army's contribution by
// entity ID and the pool's under "".
double engaged(const WorldState &w, const Adjacency &adj, const std::string &a,
               const std::vector<std::string> &regions, const std::string &opponent,
               std::map<std::string, double> *parts = nullptr);

int constructionCapacity(const WorldState &, const std::string &);
int uncommittedPersonnel(const WorldState &, const std::string &);
int uncommittedShips(const WorldState &, const std::string &);
std::string militaryLabel(const WorldState &, const std::string &);
std::string militarySummary(const WorldState &, const std::string &);
std::string reachDescription(const WorldState &, const Adjacency &, const std::string &,
                             const std::string &);
} // namespace force
bool hasStatus(const WorldState &);
// Compact, model-ready and UI-ready national situation derived from the regions an actor owns.
Json::Value nationalStatus(const WorldState &, const std::string &actor);
Json::Value allNationalStatus(const WorldState &);
std::string formatMoney(double usd, const Json::Value &status);
// A conflict or commitment as the model and UI should see it: name, kind, parties, intensity and
// totals.
Json::Value describeAgreement(const WorldState &, const std::string &kind, const std::string &id);
std::string displayName(const WorldState &, const std::string &kind, const std::string &id);
// "January 2026" for turn 0; negative turns are months before the campaign.
std::string campaignDate(int turn);
// Chronicles are rewritten at most once a year per nation.
constexpr int chronicleInterval = 12;
// Missing, a year old, or written before the nation's history source was cached and it now is.
bool chronicleDue(const Json::Value &current, int turn, bool sourceAvailable);
// Historians' naming: keeps a specific proposed name, replaces generic ones and numbers repeats.
std::string realisticName(const WorldState &, const std::string &kind, const std::string &type,
                          const std::string &proposed, const std::vector<std::string> &parties,
                          const std::string &text, const std::set<std::string> &taken);
bool validConflictKind(const std::string &);
bool validCommitmentKind(const std::string &);
bool validIntensity(const std::string &);
bool validEffect(const std::string &);
bool validGovernment(const std::string &);
// Every political system the game models, in the order the UI and the schema list them.
const std::vector<std::string> &governments();
// "Semi-presidential republic" for "semi_presidential_republic"; empty for an unknown system.
std::string governmentLabel(const std::string &system);
// How a political system handles discontent. `vent` is how much grievance a legitimate channel
// discharges: it slows unrest and speeds its decay. `repression` holds the lid on longer and
// stores the pressure, so the revolt that eventually comes is proportionally larger. A regime
// that answers dissent with force has no protest rung to stop at.
struct Regime {
    double vent = 0.5, repression = 0.3;
    bool armedFirst = false, inhabited = true;
};
const Regime &regime(const std::string &system);
// The band a region's unrest falls in: "calm", "restive", "unrest", "in revolt",
// "in open rebellion". The journal, the inspector, the map legend and the regional prompt all
// name it the same way.
std::string unrestDescription(double unrest);
// 3 added autonomous front movement and stalemate tracking; turns recorded at 2 keep neither.
// 4 added regional unrest and the rebellion, secession and regime-collapse chain it feeds.
constexpr int statusVersion = 4, frontMomentumVersion = 3, unrestVersion = 4;
// Deterministic monthly consequences after the accepted delta: growth, war fronts, casualties,
// displacement, GDP and stability. Replay runs it again, so it needs no inference. `version` is the
// turn's recorded status_version: a live month uses the current rules, while replay passes what the
// turn was actually played with, so rules added later never rewrite a month saved without them.
Resolution applyMonthlyStatus(const Resolution &, const WorldDelta &, const Adjacency &,
                              int version = statusVersion);
} // namespace lh
