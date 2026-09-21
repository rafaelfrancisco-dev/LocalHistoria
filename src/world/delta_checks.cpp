#include "localhistoria/politics.h"
#include "localhistoria/status.h"
#include "world/delta_internal.h"
#include <utility>
namespace lh::detail {
void checkEvents(const WorldDelta &d, const WorldState &w, std::set<std::string> &eventIds) {
    int background = 0;
    for (auto &e : d.events) {
        require(e.isObject() && e["id"].isString() && e["text"].isString() &&
                    e["actor"].isString() && e["causes"].isArray() && e["regions"].isArray() &&
                    e["entities"].isArray() && e["actors"].isArray() && e["background"].isBool(),
                "Invalid structured event");
        require(e["id"].asString().rfind("t" + std::to_string(w.turn) + "-", 0) == 0 &&
                    eventIds.insert(e["id"].asString()).second &&
                    e["text"].asString().size() <= 2000,
                "Invalid event ID or text");
        if (e["background"].asBool())
            ++background;
        require(!e.isMember("major") || e["major"].isBool(), "Invalid event significance");
        if (e.isMember("effects")) {
            require(e["effects"].isArray() && e["effects"].size() <= 3, "Invalid event effects");
            for (auto &x : e["effects"])
                require(x.isObject() && x["actor"].isString() && x["economy"].isString() &&
                            validEffect(x["economy"].asString()) && x["stability"].isString() &&
                            validEffect(x["stability"].asString()),
                        "Invalid event effect");
        }
    }
    require(background <= 3, "At most three background developments");
}
// Identity changes keep permanent affiliation and add exactly one flag revision.
void checkActorChange(const Json::Value &c) {
    if (!c["before"].isNull()) {
        auto expected = c["before"];
        for (auto k : {"name", "identity_revisions", "flag_revision"}) {
            if (c["after"].isMember(k))
                expected[k] = c["after"][k];
            else
                expected.removeMember(k);
        }
        require(expected == c["after"], "Identity change cannot alter permanent affiliation");
        auto &old = c["before"]["identity_revisions"];
        auto &revs = c["after"]["identity_revisions"];
        bool correction =
            old == revs && c["before"]["flag_revision"] == c["after"]["flag_revision"] &&
            identityCorrection(c["before"]["name"].asString(), c["after"]["name"].asString());
        unsigned prior = old.isArray() ? old.size() : 1;
        require(correction || (revs.isArray() && revs.size() == prior + 1),
                "Identity revision required");
        if (old.isArray())
            for (unsigned i = 0; i < old.size(); ++i)
                require(old[i] == revs[i], "Historical revision is immutable");
    }
    auto &revs = c["after"]["identity_revisions"];
    if (revs.isArray() && !revs.empty() && revs != c["before"]["identity_revisions"]) {
        auto &last = revs[revs.size() - 1];
        require(c["after"]["flag_revision"].isUInt() &&
                    c["after"]["flag_revision"].asUInt() == revs.size() - 1 &&
                    last["origin_event"] == c["event"] && last["name"] == c["after"]["name"] &&
                    last["renderer_version"] == 1 &&
                    last["svg"] == renderFlag(last["specification"]),
                "Invalid identity flag revision");
        if (revs.size() > 1 && revs[revs.size() - 2]["svg"].isString())
            require(last["svg"] != revs[revs.size() - 2]["svg"], "Replacement flag must differ");
    }
    require(c["after"]["name"].isString() && !c["after"]["name"].asString().empty() &&
                c["after"]["name"].asString().size() <= 120 && c["after"]["color"].isString() &&
                isHexColor(c["after"]["color"].asString()),
            "Invalid actor identity");
}
void checkRegionalChange(const Json::Value &c, const std::string &id, WorldState &w) {
    auto &p = c["after"];
    require(w.territories.isMember(id) && w.territories[id]["kind"] != "residual" &&
                p["agenda"].isString() && p["agenda"].asString().size() <= 500 &&
                p["relations"].isString() &&
                (p["relations"] == "neutral" || p["relations"] == "cooperative" ||
                 p["relations"] == "strained") &&
                p["demands"].isArray() && p["demands"].size() <= 8 &&
                p["supporting_events"].isArray() && p["supporting_events"].size() <= 12,
            "Invalid regional politics");
    for (auto &demand : p["demands"])
        require(demand["id"].isString() && demand["text"].isString() &&
                    demand["text"].asString().size() <= 500 &&
                    (demand["status"] == "pending" || demand["status"] == "accepted" ||
                     demand["status"] == "rejected"),
                "Invalid regional demand");
}
void checkProposalChange(const Json::Value &c, WorldState &w) {
    auto &p = c["after"];
    auto status = p["status"].asString();
    require(p["from"].isString() && p["from"] != w.player && p["actors"].isArray() &&
                p["actors"].size() >= 2 && p["kind"].isString() &&
                validCommitmentKind(p["kind"].asString()) && p["name"].isString() &&
                !p["name"].asString().empty() && p["name"].asString().size() <= 160 &&
                p["text"].isString() && p["text"].asString().size() <= 1000 &&
                p["proposed_turn"].isInt() && p["event"].isString(),
            "Invalid treaty proposal");
    bool player = false, from = false;
    for (auto &a : p["actors"]) {
        require(a.isString(), "Invalid treaty proposal");
        player = player || a == w.player;
        from = from || a == p["from"];
    }
    require((!p.isMember("months") ||
             (p["months"].isInt() && p["months"].asInt() >= 0 && p["months"].asInt() <= 120)) &&
                (!p.isMember("terms") || (p["terms"].isArray() && p["terms"].size() <= 4)) &&
                (!p.isMember("amends") || p["amends"].isString()),
            "Invalid treaty proposal terms");
    for (auto &t : p["terms"])
        require(t.isObject() && t["action"].isString(), "Invalid treaty proposal terms");
    require(player && from, "A proposal is offered by a party to the player");
    if (c["before"].isNull())
        require(status == "pending" && p["proposed_turn"] == w.turn && p["event"] == c["event"] &&
                    (!p.isMember("amends") ||
                     isOpen(std::as_const(w.commitments)[p["amends"].asString()])),
                "A new proposal is pending");
    else {
        // Answered once, in a later month; nothing else about it changes.
        auto expected = c["before"];
        expected["status"] = p["status"];
        expected["answered_turn"] = w.turn;
        require(c["before"]["status"] == "pending" &&
                    (status == "accepted" || status == "declined" || status == "expired") &&
                    w.turn > c["before"]["proposed_turn"].asInt() && expected == p,
                "Invalid proposal answer");
    }
}
void checkTerritoryChange(const Json::Value &c) {
    require(!c["before"].isNull(), "Territory geometry is fixed");
    auto expected = c["before"];
    expected["owner"] = c["after"]["owner"];
    expected["controller"] = c["after"]["controller"];
    require(expected == c["after"], "Only ownership and control can change");
}
void checkTerritories(WorldState &w) {
    for (auto &id : w.territories.getMemberNames()) {
        auto &t = w.territories[id];
        for (auto k : {"owner", "controller"})
            require(t[k].isString() && (t[k] == "" || w.hasActor(t[k].asString())),
                    "Unknown territory actor");
    }
}
void checkEntities(const Resolution &base, WorldState &w, const Json::Value &geo,
                   const std::set<std::string> &eventIds) {
    for (auto &id : w.entities.getMemberNames()) {
        auto &e = w.entities[id];
        if (e == base.state.entities[id])
            continue;
        require(e["actor"].isString() && w.hasActor(e["actor"].asString()) &&
                    e["region"].isString() && w.territories.isMember(e["region"].asString()),
                "Invalid entity affiliation");
        require(e["name"].isString() && e["name"].asString().size() <= 120 &&
                    e["status"].isString() &&
                    (e["status"] == "active" || e["status"] == "destroyed" ||
                     e["status"] == "inactive" || e["status"] == "under_construction"),
                "Invalid entity description");
        require(e["status"] == "under_construction"
                    ? e["completes_turn"].isInt() && e["completes_turn"].asInt() > w.turn
                    : !e.isMember("completes_turn"),
                "Invalid construction schedule");
        // Event sites fade after a few months; sites saved before expiry existed keep none until an
        // event touches them.
        require(!e.isMember("expires_turn") ||
                    (e["kind"] == "event_site" && e["status"] == "active" &&
                     e["expires_turn"].isInt() && e["expires_turn"].asInt() > w.turn &&
                     e["expires_turn"].asInt() <= w.turn + eventMapMonths),
                "Invalid event site expiry");
        require(!(e["kind"] == "event_site" && base.state.entities[id].isNull()) ||
                    e.isMember("expires_turn"),
                "New event sites need an expiry");
        require(e["kind"] == "city" || e["kind"] == "port" || e["kind"] == "base" ||
                    e["kind"] == "army" || e["kind"] == "fleet" || e["kind"] == "event_site",
                "Invalid entity kind");
        for (auto field : {"personnel", "ships"})
            require(!e.isMember(field) || (e[field].isInt() && e[field].asInt() >= 0),
                    "Invalid formation quantity");
        if (static_cast<const WorldState &>(w)
                .actors[e["actor"].asString()]["status"]["military"]
                .isObject()) {
            require(e["kind"] != "army" || e["personnel"].isInt(), "Army needs personnel");
            require(e["kind"] != "fleet" || e["ships"].isInt(), "Fleet needs ships");
        }
        require(e["longitude"].isNumeric() && e["latitude"].isNumeric() &&
                    containsPoint(geo[e["region"].asString()], e["longitude"].asDouble(),
                                  e["latitude"].asDouble()),
                "Entity location outside region");
        require(e["origin_event"].isString() &&
                    (eventIds.contains(e["origin_event"].asString()) ||
                     base.state.entities[id]["origin_event"] == e["origin_event"]),
                "Invalid originating event");
    }
}
void checkProposals(const Resolution &base, WorldState &w) {
    for (auto &id : w.proposals.getMemberNames()) {
        auto &p = w.proposals[id];
        if (p == base.state.proposals[id])
            continue;
        for (auto &a : p["actors"])
            require(w.hasActor(a.asString()), "Unknown proposal party");
        // An accepted proposal becomes the treaty under its reserved ID, in the same month.
        if (p["status"] == "accepted" && p["amends"].isString()) {
            auto cid = p["amends"].asString();
            require(std::as_const(w.commitments)[cid] != base.state.commitments[cid] &&
                        std::as_const(w.commitments)[cid]["text"] == p["text"],
                    "An accepted amendment changes its treaty");
        } else if (p["status"] == "accepted")
            require(base.state.commitments[id].isNull() &&
                        std::as_const(w.commitments)[id]["actors"] == p["actors"] &&
                        std::as_const(w.commitments)[id]["name"] == p["name"],
                    "An accepted proposal needs its treaty");
    }
}
void checkNewActors(const Resolution &base, WorldState &w) {
    for (auto &id : w.actors.getMemberNames())
        if (!base.state.actors.isMember(id)) {
            bool land = false;
            for (auto &t : w.territories)
                land = land || t["owner"] == id;
            require(land, "New actor requires territory allocation");
        }
}
void checkAgreements(const Resolution &base, WorldState &w) {
    for (auto kind : {"conflicts", "commitments"}) {
        auto collection = kind == std::string("conflicts") ? w.conflicts : w.commitments;
        for (auto &id : collection.getMemberNames()) {
            const Json::Value &v = collection[id];
            require(v["actors"].isArray() && v["actors"].size() >= 2 && v["status"].isString() &&
                        (isOpen(v) || v["status"] == "resolved") && v["text"].isString() &&
                        v["text"].asString().size() <= 1000,
                    "Invalid conflict or commitment");
            for (auto &a : v["actors"])
                require(a.isString() && w.hasActor(a.asString()), "Unknown commitment actor");
            bool conflict = kind == std::string("conflicts");
            // Names and kinds are optional only for entries that predate them.
            bool created = (conflict ? base.state.conflicts : base.state.commitments)[id].isNull();
            require(!created || (v["name"].isString() && v["kind"].isString()),
                    "New conflicts and commitments need a name and kind");
            require(!v.isMember("name") || (v["name"].isString() && !v["name"].asString().empty() &&
                                            v["name"].asString().size() <= 160),
                    "Invalid conflict or commitment name");
            require(!v.isMember("kind") || (v["kind"].isString() &&
                                            (conflict ? validConflictKind(v["kind"].asString())
                                                      : validCommitmentKind(v["kind"].asString()))),
                    "Invalid conflict or commitment kind");
            for (auto k : {"started_turn", "ended_turn"})
                require(!v.isMember(k) || v[k].isInt(), "Invalid conflict or commitment dates");
            // A treaty in force ends after its term; resolve() closes it when that month arrives.
            require(!v.isMember("expires_turn") ||
                        (v["expires_turn"].isInt() &&
                         (!isOpen(v) || v["expires_turn"].asInt() > w.turn)),
                    "Invalid treaty expiry");
            if (v.isMember("terms")) {
                require(v["terms"].isArray() && v["terms"].size() <= 8, "Invalid treaty terms");
                for (auto &t : v["terms"])
                    require(t.isObject() &&
                                (t["action"] == "end_conflict" || t["action"] == "cede" ||
                                 t["action"] == "occupy" || t["action"] == "withdraw"),
                            "Invalid treaty term");
            }
            for (auto k : {"intensity", "set_intensity"})
                require(!v.isMember(k) || (v[k].isString() && validIntensity(v[k].asString())),
                        "Invalid conflict intensity");
            for (auto k : {"casualties", "displaced"})
                require(!v.isMember(k) || (v[k].isIntegral() && v[k].asInt64() >= 0),
                        "Invalid conflict totals");
            for (auto k : {"affected_regions", "theatre"})
                if (v.isMember(k)) {
                    require(v[k].isArray(), "Invalid conflict regions");
                    for (auto &region : v[k])
                        require(region.isString() && w.territories.isMember(region.asString()),
                                "Unknown conflict region");
                }
        }
    }
}
} // namespace lh::detail
