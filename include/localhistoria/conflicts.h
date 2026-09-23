#pragma once
#include "status.h"
#include "world.h"
namespace lh {
bool realismRules(const WorldState &);
// Canonical unordered pairs; missing historical sides use a documented conservative fallback.
Json::Value hostilePairs(const WorldState &, const Json::Value &conflict);
void initializeConflictPairs(WorldState &);
bool fightingPair(const WorldState &, const Json::Value &, const std::string &,
                  const std::string &);
bool fighting(const WorldState &, const std::string &, const std::string &);
int pairPosture(const Json::Value &, const std::string &, const std::string &);
std::string hostilePairKey(const std::string &, const std::string &);
// Peaceful entry: land the actor controls, land nobody holds, or land whose controller granted it
// military access in an open treaty. An alliance by itself grants no basing or transit.
bool mayEnter(const WorldState &, const std::string &actor, const std::string &region);
// An actor's most populous region that it both owns and controls (residual slivers excepted, ties
// by ID), or "" if it has none.
std::string heartland(const WorldState &, const std::string &actor);
// Whether a region links back to the actor's heartland through land it may enter, sea routes, or an
// ocean basin shared by coasts it may enter. An actor with no heartland is never cut off.
bool supplied(const WorldState &, const Adjacency &, const std::string &actor,
              const std::string &region);

// Control history (realism lifecycle). A region held by force against the actor it was taken from
// carries an engine-owned `control` record: {claimant, holder, since, cause, integration}. It
// survives annexation, so a label change is not integration; it ends when the claimant holds the
// region again, or when an annexed region is fully integrated. `since` is absent and `cause` is
// "recorded" for occupations that predate the campaign's history; otherwise `cause` is "hostile".
//
// A movement that rose from the land it holds: a rebel actor that has not yet become a state, or a
// side in a civil war or insurgency with the region's owner. Its control is not foreign occupation.
bool localRule(const WorldState &, const std::string &controller, const std::string &owner);
// Held by `by` under an open treaty's occupation term that the owner signed.
bool agreedOccupation(const WorldState &, const std::string &region, const std::string &by);
// The region's control record when its current controller is the one holding it by force.
const Json::Value &hostileControl(const WorldState &, const std::string &region);
// The share of a region's output an actor can actually use: all of it for an undisputed owner,
// none for an owner who has lost control, and a conqueror's share that grows with integration.
double outputShare(const WorldState &, const std::string &region, const std::string &actor);
// Output an actor can use across the map, and the population it comes from, weighted alike.
std::pair<double, double> effectiveOutput(const WorldState &, const std::string &actor);
// Brings every region's record in line with who holds it now: opens one when a region falls to a
// foreign controller, moves it when a holder loses it to someone other than the claimant, and ends
// it when the claimant or a local movement holds it. `migrating` records what it finds without
// inventing an age, for occupations present before the campaign's control history began.
void recordControl(WorldState &, bool migrating);
// Displacement from before the origin-host ledger existed: an outstanding count with no recorded
// host is kept as a balance of unknown whereabouts, never assigned a host or returned. Idempotent.
void recordDisplacement(WorldState &);
// Exhaustion for nations that have none recorded: those already fighting a war a year old or more
// start where the old anniversary multiplier left them, everyone else fresh. Idempotent.
void recordExhaustion(WorldState &);
} // namespace lh
