#pragma once
#include "localhistoria/conflicts.h"
#include "localhistoria/politics.h"
#include "localhistoria/status.h"
#include "world/compiler/helpers.h"
#include "world/compiler/pending_store.h"
#include <map>
#include <set>
namespace lh::detail {
// Turns the model's operations into a validated delta against the start-of-month world.
class OperationCompiler {
  public:
    OperationCompiler(const WorldState &w, const Adjacency &adjacency,
                      const std::set<std::string> &priorEvents = {})
        : w(w), adjacency(adjacency) {
        // Event IDs end in a slug of their description, so a slug is an exact text-duplicate test.
        for (const auto &id : priorEvents) {
            auto dash = id.find('-');
            if (dash != std::string::npos)
                priorSlugs.insert(id.substr(dash + 1));
        }
    }
    WorldDelta compile(const Json::Value &response, const Json::Value &responses);

  private:
    bool realism() const { return w.engine.get("lifecycle_version", 0).asInt() == 1; }
    void initializeProposal(Json::Value &) const;
    int settleExplicitProposals(Json::Value &, const Json::Value &) const;
    struct Touched {
        std::set<std::string> actors, regions, entities, createdHere, conflicts, commitments;
        // What this month makes obvious: parties to a war opened or escalated here (and whoever
        // took land by force), to a trade treaty, and owners whose land was taken.
        std::set<std::string> warParties, tradeParties, seizedOwners;
        // Parties to a trade treaty or union that ended here: their economies lose what it brought.
        std::set<std::string> tradeLoss;
    };
    struct EventScope : Touched {
        explicit EventScope(const Json::Value &e) : e(e) {}
        const Json::Value &e;
        std::string id, ev, regional, text, blocked, breaches;
        bool trusted = false;
        // Whether an operation changed the map or a war: such events are major whatever the model
        // calls them.
        bool structural = false;
        std::vector<Json::Value> seenOps, successfulOps;
        int failedOps = 0;
        // Why operations were dropped: kept for the audit, never in the event text players and
        // later prompts read.
        Json::Value dropped{Json::arrayValue};
    };
    struct OpScope {
        explicit OpScope(const Json::Value &o) : o(o) {}
        const Json::Value &o;
        std::string name, why;
        // A treaty offered to the player is only proposed: it changes nothing until the player
        // answers next month.
        bool proposal = false, shaped = false;
        // The operation finished early (a proposal or an identity correction): it neither shapes
        // the event nor breaches a treaty.
        bool done = false;
        // Whoever this operation acts against: a treaty of peace or alliance with them is breached.
        std::set<std::string> hostile;
    };
    struct Snapshot {
        int created, newActors;
        std::map<std::string, std::set<std::string>> taken;
        std::map<std::string, std::string> aliases;
        std::set<std::string> names;
    };
    Snapshot snapshot() const { return {created, newActors, taken, aliases, names}; }
    void restore(Snapshot s) {
        created = s.created;
        newActors = s.newActors;
        taken = std::move(s.taken);
        aliases = std::move(s.aliases);
        names = std::move(s.names);
    }

    int settleProposals(Json::Value &all, const Json::Value &responses) const;
    void collectNames();
    void collectConsent(const Json::Value &response);
    void compileEvent(const Json::Value &e, bool trusted);
    void runOperations(EventScope &s);
    void runOperation(EventScope &s, OpScope &op);
    void apply(EventScope &s, OpScope &op);
    void markStructural(EventScope &s, const OpScope &op) const;
    void breachTreaties(EventScope &s, OpScope &op);

    void opRegional(EventScope &s, OpScope &op);
    void opChangeIdentity(EventScope &s, OpScope &op);
    void opCreateActor(EventScope &s, OpScope &op);
    void opOpen(EventScope &s, OpScope &op);
    std::vector<std::string> treatyParties(EventScope &s, const OpScope &op, Json::Value &v) const;
    std::string treatyName(const OpScope &op, const std::string &kind, const std::string &type,
                           const std::vector<std::string> &members, const Json::Value &v);
    void propose(EventScope &s, OpScope &op, Json::Value v, const std::string &cid, int months);
    void opResolve(EventScope &s, OpScope &op);
    void opAmend(EventScope &s, OpScope &op);
    void proposeAmendment(EventScope &s, OpScope &op, const Json::Value &treaty,
                          const std::string &cid, int months, const std::string &revised);
    void opSettle(EventScope &s, OpScope &op);
    void acceptProposal(EventScope &s, OpScope &op, const std::string &pid, const Json::Value &p);
    void opIntensity(EventScope &s, OpScope &op);
    void opBuild(EventScope &s, OpScope &op);
    void allocateForce(const std::string &builder, const std::string &kind, Json::Value &v) const;
    void opEntity(EventScope &s, OpScope &op);
    // Applies handOverProject to every project in a region whose control passes by agreement.
    void handOver(EventScope &s, const std::string &region, const std::string &from,
                  const std::string &to, const std::string &why);
    void checkPassage(const std::string &eid, const std::string &region) const;
    void moveEntity(EventScope &s, const OpScope &op, const std::string &eid, Json::Value &v,
                    const std::string &owner);
    void setEntityStatus(EventScope &s, OpScope &op, const std::string &eid, Json::Value &v,
                         const std::string &owner);
    bool atWar(const std::string &a, const std::string &b) const;
    void opTerritory(EventScope &s, OpScope &op);
    void transfer(EventScope &s, const OpScope &op, const std::string &r, Json::Value &t,
                  const std::string &owner, const std::string &controller);
    void occupy(EventScope &s, OpScope &op, const std::string &r, Json::Value &t,
                const std::string &owner, const std::string &controller);

    Json::Value applyTerms(EventScope &s, const Json::Value &terms,
                           const std::vector<std::string> &members, const std::string &why);
    Json::Value applyTerm(EventScope &s, const Json::Value &t,
                          const std::vector<std::string> &members, const std::string &why);
    void consent(const EventScope &s, const std::vector<std::string> &members, bool &proposal);
    void awaiting(EventScope &s) const;
    void ending(EventScope &s, const Json::Value &treaty, const std::string &why);

    std::string str(const OpScope &op, const char *k) const;
    std::string region(const OpScope &op) const;
    std::string party(const EventScope &s, const OpScope &op, const char *k) const;
    std::string actorRef(const std::string &v) const;
    bool known(const std::string &a) const;
    bool adjacent(const std::string &from, const std::string &to) const;
    bool reaches(const std::string &a, const std::string &r) const;
    void take(const std::string &a, const std::string &r);
    WorldState allocatedWorld() const;
    WorldState engagementWorld() const;

    void renewEventSites(EventScope &s);
    void finishRegionalHistory(EventScope &s);
    void buildEvent(EventScope &s);
    void settleEffects(const EventScope &s, Json::Value &event) const;
    WorldDelta delta() const;

    const WorldState &w;
    const Adjacency &adjacency;
    PendingStore pending;
    std::map<std::string, std::set<std::string>> taken;
    // New actors get IDs derived from their names; later references may use the requested ID or the
    // name.
    std::map<std::string, std::string> aliases;
    // Names already used by any war or treaty, past or present, so repeats are numbered.
    std::set<std::string> names;
    // Who agreed with whom this month: an outcome of a nation's own action that names another
    // nation. A treaty needs it from every party.
    std::map<std::string, std::set<std::string>> consenting;
    std::set<std::string> eventIds;
    // Description slugs of events already in campaign history: a background development that
    // repeats one is the model re-reporting the past, not a new development.
    std::set<std::string> priorSlugs;
    // Efforts started by this month's events, as "actor:category": a second request the same month
    // continues the first.
    mutable std::set<std::string> efforts;
    int created = 0, newActors = 0, background = 0;
    Json::Value events{Json::arrayValue};
    std::vector<Json::ArrayIndex> declared;
};
} // namespace lh::detail
