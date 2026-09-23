#include "localhistoria/conflicts.h"
#include "localhistoria/lifecycle.h"
#include <iostream>
using namespace lh;
static int checks;
static void check(bool ok, const char *message) {
    ++checks;
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> static void rejects(F action, const char *message) {
    ++checks;
    try {
        action();
    } catch (const ValidationError &) {
        return;
    }
    throw std::runtime_error(message);
}
// AA's home A1 (coast) and A2 (inland); BB, its enemy, holds B1 (coast) and B2, and AA holds A3,
// a pocket of BB's land surrounded by BB. DD is a neutral neighbour (N1 coast, N2 inland). EE holds
// an island I1 800 km off A1, and F1 is a coast 2,000 km away by sea. AA also owns A4, an enclave
// inside BB; A5, a distant island on the same ocean; and A6, a coast on a separate inland sea.
struct Region {
    const char *id, *actor;
    bool coastal;
    double longitude;
};
static const Region regions[]{
    {"A1", "AA", true, 0},   {"A2", "AA", false, 2}, {"A3", "BB", false, 4},
    {"B1", "BB", true, 6},   {"B2", "BB", false, 8}, {"N1", "DD", true, 10},
    {"N2", "DD", false, 12}, {"I1", "EE", true, 14}, {"F1", "EE", true, 16},
    {"A4", "AA", false, 18}, {"A5", "AA", true, 20}, {"A6", "AA", true, 22}};
static Json::Value geography() {
    Json::Value g;
    for (const auto &r : regions) {
        Json::Value ring(Json::arrayValue);
        for (auto [x, y] :
             {std::pair{-0.5, -0.5}, {0.5, -0.5}, {0.5, 0.5}, {-0.5, 0.5}, {-0.5, -0.5}}) {
            Json::Value p;
            p.append(r.longitude + x);
            p.append(y);
            ring.append(p);
        }
        g[r.id]["type"] = "Polygon";
        g[r.id]["coordinates"].append(ring);
    }
    return g;
}
static Adjacency adjacency() {
    Adjacency adj{
        {"A1", {"A2", "B1", "N1"}}, {"A2", {"A1"}},       {"A3", {"B2"}}, {"B1", {"A1", "B2"}},
        {"B2", {"B1", "A3", "A4"}}, {"N1", {"A1", "N2"}}, {"N2", {"N1"}}, {"A4", {"B2"}}};
    auto route = [&](const char *a, const char *b, int km) {
        adj.sea[a][b] = km;
        adj.sea[b][a] = km;
    };
    route("A1", "B1", 100);
    route("A1", "N1", 200);
    route("A1", "I1", 800);
    route("B1", "I1", 900);
    route("A1", "F1", 2000);
    for (auto r : {"A1", "B1", "N1", "I1", "F1", "A5"}) {
        adj.basins[r].insert(0);
        adj.basinRegions[0].push_back(r);
    }
    adj.basins["A6"].insert(1);
    adj.basinRegions[1].push_back("A6");
    adj.seaRevision = "fixture-1";
    return adj;
}
static WorldState world() {
    WorldState w;
    w.turn = 5;
    w.player = "AA";
    w.engine["lifecycle_version"] = realismLifecycleVersion;
    for (auto a : {"AA", "BB", "DD", "EE"}) {
        w.actors[a]["name"] = std::string("Land ") + a;
        auto &m = w.actors[a]["status"]["military"];
        // AA is decisively stronger, so its occupations succeed where reach allows them.
        m["active"] = std::string(a) == "AA" ? 600000 : 100000;
        m["reserve"] = 0;
        m["ships"] = 30;
        m["aircraft"] = 100;
        w.actors[a]["status"]["stability"] = 60;
    }
    for (const auto &r : regions) {
        auto &t = w.territories[r.id];
        t["name"] = r.id;
        t["owner"] = t["controller"] = t["initial_affiliation"] = r.actor;
        t["kind"] = "ADM1";
        t["coastal"] = r.coastal;
        // Interiors a degree apart: provinces about the size of real ones, so capture effort
        // (Plan 26 I) reads them as typical.
        t["longitude"] = r.longitude / 2;
        t["latitude"] = 0;
        t["population"] = 1000000;
        t["gdp_usd"] = Json::Int64(20000000000);
    }
    w.conflicts["war"] = parse(R"({"name":"War","kind":"war","status":"open","text":"War",
        "actors":["AA","BB"],"set_intensity":"medium","started_turn":0,
        "hostile_pairs":[{"actors":["AA","BB"],"status":"active"}],
        "postures":{"AA":"medium","BB":"medium"}})");
    auto unit = [&](const char *id, const char *kind, const char *region, const char *field,
                    int n) {
        auto &e = w.entities[id];
        e["name"] = id;
        e["actor"] = "AA";
        e["kind"] = kind;
        e["region"] = region;
        e["status"] = "active";
        e["longitude"] = w.territories[region]["longitude"];
        e["latitude"] = 0;
        e["origin_event"] = "t0-seed";
        e[field] = n;
    };
    w.territories["A3"]["controller"] = "AA";
    unit("army1", "army", "A1", "personnel", 20000);
    unit("fleet1", "fleet", "A1", "ships", 10);
    return w;
}
static WorldDelta ops(const WorldState &w, const std::string &actor, const std::string &json) {
    Json::Value response;
    auto e = parse(
        R"({"description":"Fixture order","blocked_by":"none","causes":[],"background":false})");
    e["actor"] = actor;
    e["operations"] = parse(json);
    response["events"].append(e);
    return compileOperations(w, response, adjacency());
}
static bool done(const WorldDelta &d) {
    return !d.changes.empty() && !d.events[0]["failed"].asBool();
}
static std::string move(const char *entity, const char *region) {
    return std::string(R"([{"action":"move_entity","entity":")") + entity + R"(","region":")" +
           region + R"("}])";
}
static void access(WorldState &w, const char *grantor, const char *kind = "defence_pact") {
    auto &k = w.commitments[std::string("access-") + grantor];
    k = parse(R"({"name":"Access","text":"Access","status":"open"})");
    k["kind"] = kind;
    k["actors"].append("AA");
    k["actors"].append(grantor);
    k["terms"] = parse(R"([{"action":"grant_access","to":"AA"}])");
}
// A treaty offered by `from` and signed the next month by every party: the state it was offered
// in and the delta that settles it (empty when the offer was refused outright).
static std::pair<WorldState, WorldDelta>
settlement(WorldState w, const char *from, const std::string &actors, const std::string &terms) {
    auto offer = ops(w, from,
                     R"([{"action":"add_commitment","kind":"other","name":"Accord","text":"Terms",)"
                     R"("months":12,"actors":)" +
                         actors + R"(,"terms":)" + terms + "}]");
    auto offered = applyDelta({w, {}}, offer, geography()).state;
    if (offered.proposals.empty())
        return {offered, {}};
    auto id = offered.proposals.getMemberNames().back();
    offered.turn++;
    Json::Value answers(Json::arrayValue);
    for (const auto &a : parse(actors)) {
        // An AI proposer signed its own offer; the player never signs in advance.
        if (a == from && a != w.player)
            continue;
        Json::Value yes;
        yes["proposal"] = id;
        yes["actor"] = a;
        yes["revision"] = 1;
        yes["accept"] = true;
        answers.append(yes);
    }
    Json::Value response;
    response["events"].append(parse(R"({"actor":"DD","description":"Quiet month",)"
                                    R"("blocked_by":"none","causes":[],"background":false,)"
                                    R"("operations":[]})"));
    return {offered, compileOperations(offered, response, adjacency(), answers)};
}
static WorldState signTreaty(WorldState w, const char *from, const std::string &actors,
                             const std::string &terms) {
    auto [offered, settled] = settlement(w, from, actors, terms);
    return applyDelta({offered, {}}, settled, geography()).state;
}
static Json::Value refs() {
    Json::Value r;
    r["lifecycle_version"] = realismLifecycleVersion;
    r["sea_route_revision"] = "fixture-1";
    return r;
}
int main() {
    try {
        const auto adj = adjacency();
        auto w = world();

        // Fleets: coasts only, along sea routes within range, into waters they may use.
        check(!done(ops(w, "AA", move("fleet1", "A2"))), "No inland fleet");
        check(!done(ops(w, "AA", move("fleet1", "F1"))), "Route beyond a month's sailing");
        check(done(ops(w, "AA", move("fleet1", "B1"))), "A fleet may lie off an enemy coast");
        check(!done(ops(w, "AA", move("fleet1", "I1"))), "No port call without access");
        auto granted = w;
        access(granted, "EE");
        check(done(ops(granted, "AA", move("fleet1", "I1"))), "Access opens a neutral port");
        auto inland = w;
        inland.entities["fleet1"]["region"] = "A2";
        check(!done(ops(inland, "AA", move("fleet1", "B1"))) &&
                  !done(ops(inland, "AA", move("fleet1", "I1"))),
              "An inland fleet has no sea route beyond a bordering coast");

        // Armies: no neutral transit without access, no hostile entry without an occupation.
        check(!done(ops(w, "AA", move("army1", "N1"))), "No neutral transit without access");
        auto allied = w;
        allied.commitments["alliance"] = parse(R"({"name":"Alliance","text":"Allies",
            "status":"open","kind":"alliance","actors":["AA","DD"],"terms":[]})");
        check(!done(ops(allied, "AA", move("army1", "N1"))), "An alliance alone grants no transit");
        access(allied, "DD", "alliance");
        check(done(ops(allied, "AA", move("army1", "N1"))), "Access by treaty permits transit");
        check(!done(ops(w, "AA", move("army1", "B1"))), "No hostile entry by movement");
        auto taken = ops(w, "AA", R"([{"action":"occupy","region":"B1","by":"AA"},
            {"action":"move_entity","entity":"army1","region":"B1"}])");
        check(done(taken) && !taken.events[0].isMember("dropped_operations"),
              "Troops follow an occupation fought for this month");

        // Sealift: an army sails only with a fleet in its region that can lift it.
        check(!done(ops(granted, "AA", move("army1", "I1"))), "Sealift respects lift capacity");
        auto small = granted;
        small.entities["army1"]["personnel"] = 5000;
        check(done(ops(small, "AA", move("army1", "I1"))), "A fleet carries what it can lift");
        small.entities["fleet1"]["region"] = "A2";
        check(!done(ops(small, "AA", move("army1", "I1"))), "No sealift without a fleet");

        // Overseas strength is what is embarked, never the national pool.
        auto overseas = w;
        overseas.conflicts["island"] = parse(R"({"name":"Island War","kind":"war",
            "status":"open","text":"War","actors":["AA","EE"],"started_turn":0,
            "hostile_pairs":[{"actors":["AA","EE"],"status":"active"}]})");
        overseas.entities["army1"]["region"] = "A2";
        auto bare = force::committedForce(overseas, adj, "AA", "I1", false);
        check(bare["land"].asDouble() == 0 && bare["air"].asDouble() == 0 &&
                  bare["sea"].asDouble() > 0,
              "An empty fleet marker carries no land force");
        check(force::assaultCapacity(overseas, adj, "AA", "EE", "I1") == 0,
              "An empty fleet cannot seize a coast");
        overseas.entities["army1"]["region"] = "A1";
        auto embarked = force::committedForce(overseas, adj, "AA", "I1", false);
        check(embarked["land"].asDouble() == 10 * troopsPerShip,
              "Embarked strength is capped by lift");
        auto legacy = overseas;
        legacy.engine.clear();
        check(force::committedForce(legacy, adj, "AA", "I1", false)["land"].asDouble() >
                  embarked["land"].asDouble(),
              "The legacy rule is unchanged for old campaigns");
        check(reaches(overseas, adj, "AA", "I1") && !reaches(overseas, adj, "AA", "F1"),
              "Fleet reach follows sea routes");

        // Isolation: an army in an occupied pocket fights at reduced support, without national
        // reserves. Land a nation owns is its own supply base, so only foreign ground can be cut
        // off.
        auto enclave = w;
        enclave.entities["army1"]["region"] = "A3";
        check(!supplied(enclave, adj, "AA", "A3") && supplied(enclave, adj, "AA", "A1"),
              "An occupied pocket is cut off; home is supplied");
        auto cut = force::committedForce(enclave, adj, "AA", "B2", false);
        check(cut["land"].asDouble() == 20000 * 0.5, "Isolated army at half, no pool");
        check(!supplied(w, adj, "AA", "A4"), "An owned enclave with no link home is cut off");
        check(supplied(w, adj, "AA", "A5") && !supplied(w, adj, "AA", "A6"),
              "An island on the home ocean is supplied at any distance; another sea is not");
        check(heartland(w, "AA") == "A1", "The heartland is the most populous home region");
        check(supplied(allied, adj, "AA", "N1") && !supplied(w, adj, "AA", "N1"),
              "Access land is supplied; neutral land is not");
        // Revoked access strands troops but does not delete them.
        auto stranded = allied;
        stranded.entities["army1"]["region"] = "N1";
        stranded.commitments["access-DD"]["status"] = "resolved";
        check(!done(ops(stranded, "AA", move("army1", "N2"))) &&
                  stranded.entities.isMember("army1") && !supplied(stranded, adj, "AA", "N1"),
              "Revoked access stops transit and leaves troops isolated");

        // Captured construction pauses, keeps its reservation, and resumes on recapture.
        auto project = w;
        project.entities["recruit"] = parse(R"({"name":"Recruits","actor":"AA","kind":"army",
            "region":"A2","status":"under_construction","completes_turn":6,"personnel":8000,
            "longitude":2,"latitude":0,"origin_event":"t4-build"})");
        project.entities["dock"] = parse(R"({"name":"Dock","actor":"AA","kind":"port",
            "region":"B1","status":"under_construction","completes_turn":9,
            "longitude":6,"latitude":0,"origin_event":"t4-build"})");
        project.territories["A2"]["controller"] = "BB";
        project.territories["B1"]["controller"] = "BB";
        const auto reserved = force::uncommittedPersonnel(project, "AA");
        auto month = beginTurn(project, refs());
        month = finishTurn(month, {}, adj, geography(), refs());
        auto &recruit = month.state.entities["recruit"];
        check(recruit["status"] == "under_construction" &&
                  recruit["paused"]["remaining_months"] == 1 && !recruit.isMember("completes_turn"),
              "A due army recruited behind enemy lines does not complete");
        check(force::uncommittedPersonnel(month.state, "AA") == reserved,
              "A paused project keeps its reserved troops");
        check(month.state.entities["dock"]["paused"]["remaining_months"] == 3,
              "A captured civilian project keeps its progress");
        auto encoded = encode(month.state);
        check(encode(decode(encoded)) == encoded, "Pauses round-trip");
        auto later = beginTurn(month.state, refs());
        later = finishTurn(later, {}, adj, geography(), refs());
        check(later.state.entities["recruit"]["paused"] == recruit["paused"],
              "A pause does not advance the schedule");
        auto held = later.state;
        WorldDelta activate;
        activate.events = parse(R"([{"id":"t8-x","actor":"AA","actors":["AA"],"regions":[],
            "entities":["recruit"],"causes":[],"text":"Activate","background":false}])");
        Json::Value change;
        change["kind"] = "entity";
        change["id"] = "recruit";
        change["event"] = "t8-x";
        change["rationale"] = "Model injection";
        change["before"] = held.entities["recruit"];
        change["after"] = held.entities["recruit"];
        change["after"]["status"] = "active";
        change["after"].removeMember("paused");
        activate.changes.append(change);
        auto next = beginTurn(held, refs());
        rejects([&] { finishTurn(next, activate, adj, geography(), refs()); },
                "A delta cannot finish paused construction");
        change["after"] = held.entities["recruit"];
        change["after"]["paused"]["remaining_months"] = 0;
        activate.changes[0] = change;
        rejects([&] { finishTurn(next, activate, adj, geography(), refs()); },
                "A delta cannot rewrite a pause");
        // BB takes over the captured dock rather than duplicating it.
        auto takeover =
            ops(next.state, "BB",
                R"([{"action":"build","kind":"port","name":"Dock","actor":"BB","region":"B1"}])");
        auto afterTakeover = finishTurn(next, takeover, adj, geography(), refs()).state;
        check(afterTakeover.entities["dock"]["actor"] == "BB" &&
                  afterTakeover.entities["dock"]["completes_turn"] == next.state.turn + 3 &&
                  afterTakeover.entities.size() == held.entities.size(),
              "The new controller completes a captured project only by taking it over");
        // Agreed transfers hand civilian works over and disband unfinished formations.
        auto ceding = w;
        ceding.entities["harbour"] = parse(R"({"name":"Harbour","actor":"AA","kind":"port",
            "region":"A1","status":"under_construction","completes_turn":9,
            "longitude":0,"latitude":0,"origin_event":"t4-build"})");
        ceding.entities["levy"] = parse(R"({"name":"Levy","actor":"AA","kind":"army",
            "region":"A1","status":"under_construction","completes_turn":8,"personnel":8000,
            "longitude":0,"latitude":0,"origin_event":"t4-build"})");
        const auto before = force::uncommittedPersonnel(ceding, "AA");
        // Under the realism rules land passes to another nation only by a treaty it signs.
        check(!done(ops(ceding, "AA",
                        R"([{"action":"transfer_territory","region":"A1","to":"DD"}])")),
              "A cession to another nation needs its signature");
        auto [signing, cession] = settlement(ceding, "AA", R"(["AA","DD"])",
                                             R"([{"action":"cede","region":"A1","to":"DD"}])");
        auto ceded = applyDelta({signing, {}}, cession, geography()).state;
        check(ceded.entities["harbour"]["actor"] == "DD" &&
                  ceded.entities["harbour"]["completes_turn"] == 9,
              "A ceded region's port passes to its new holder on schedule");
        check(ceded.entities["levy"]["status"] == "inactive" &&
                  force::uncommittedPersonnel(ceded, "AA") == before + 8000,
              "A ceded region's recruits are disbanded back to the pool");
        check(!done(ops(ceded, "AA",
                        R"([{"action":"set_entity_status","entity":"levy","status":"active"}])")),
              "A disbanded formation cannot muster on foreign soil");
        auto stolen = cession;
        for (auto &c : stolen.changes)
            if (c["id"] == "harbour")
                c["after"]["actor"] = "EE";
        rejects([&] { applyDelta({signing, {}}, stolen, geography()); },
                "A project passes only to its region's controller");
        auto oldRules = ceding;
        oldRules.engine.clear();
        auto oldCeded =
            applyDelta(
                {oldRules, {}},
                ops(oldRules, "AA", R"([{"action":"transfer_territory","region":"A1","to":"DD"}])"),
                geography())
                .state;
        check(oldCeded.entities["harbour"]["actor"] == "AA" &&
                  oldCeded.entities["levy"]["status"] == "under_construction",
              "Legacy cession leaves projects alone");
        // When an agreed occupation expires, the occupier's works stay and its recruits disband.
        auto leased = w;
        leased.territories["A2"]["controller"] = "DD";
        leased.commitments["lease"] = parse(R"({"name":"Lease","text":"Lease","status":"open",
            "kind":"other","actors":["AA","DD"],"expires_turn":6,
            "terms":[{"action":"occupy","region":"A2","by":"DD"}]})");
        leased.entities["depot"] = parse(R"({"name":"Depot","actor":"DD","kind":"base",
            "region":"A2","status":"under_construction","completes_turn":8,
            "longitude":2,"latitude":0,"origin_event":"t4-build"})");
        leased.entities["garrison"] = parse(R"({"name":"Garrison","actor":"DD","kind":"army",
            "region":"A2","status":"under_construction","completes_turn":7,"personnel":5000,
            "longitude":2,"latitude":0,"origin_event":"t4-build"})");
        auto expired = beginTurn(leased, refs());
        check(expired.state.territories["A2"]["controller"] == "AA" &&
                  expired.state.entities["depot"]["actor"] == "AA" &&
                  expired.state.entities["depot"]["completes_turn"] == 8 &&
                  expired.state.entities["garrison"]["status"] == "inactive",
              "An expired agreed occupation hands works back and disbands recruits");
        int handed = 0;
        for (const auto &e : expired.events)
            handed +=
                e.action == "construction_transferred" || e.action == "construction_disbanded";
        check(handed == 2, "Handovers are reported");
        // Recapture resumes with the same remaining months.
        auto freed = held;
        freed.territories["A2"]["controller"] = "AA";
        auto resumed = beginTurn(freed, refs());
        check(resumed.state.entities["recruit"]["completes_turn"] == resumed.state.turn + 1 &&
                  !resumed.state.entities["recruit"].isMember("paused"),
              "Recapture resumes the remaining work");
        resumed = finishTurn(resumed, {}, adj, geography(), refs());
        auto finished = beginTurn(resumed.state, refs());
        check(finished.state.entities["recruit"]["status"] == "active",
              "Resumed recruitment completes on its new schedule");
        // Replay reproduces the same pauses, and refuses a different sea graph.
        auto again = finishTurn(beginTurn(project, refs()), {}, adj, geography(), refs());
        check(encode(again.state) == encode(month.state) &&
                  encode(again.events) == encode(month.events),
              "Pauses replay identically");
        auto other = adj;
        other.seaRevision = "fixture-2";
        rejects([&] { finishTurn(beginTurn(project, refs()), {}, other, geography(), refs()); },
                "Replay needs the recorded sea graph");
        // Signing away a claim does not move the forces of a third party that holds the land.
        // A3 is BB's, held by AA.
        auto claim = w;
        claim.conflicts.clear();
        auto treatySold = signTreaty(claim, "BB", R"(["BB","DD"])",
                                     R"([{"action":"cede","region":"A3","to":"DD"}])");
        check(treatySold.territories["A3"]["owner"] == "DD" &&
                  treatySold.territories["A3"]["controller"] == "AA",
              "A treaty cession between others leaves the occupier in place");
        auto agreed = signTreaty(claim, "BB", R"(["AA","BB","DD"])",
                                 R"([{"action":"cede","region":"A3","to":"DD"}])");
        check(agreed.territories["A3"]["owner"] == "DD" &&
                  agreed.territories["A3"]["controller"] == "DD",
              "An occupier that signs the cession hands the region over");
        auto evicting = signTreaty(claim, "BB", R"(["BB","DD"])",
                                   R"([{"action":"occupy","region":"A3","by":"DD"}])");
        check(evicting.territories["A3"]["controller"] == "AA" && evicting.commitments.empty(),
              "An agreed occupation cannot evict a controller that did not agree");
        auto legacyClaim = claim;
        legacyClaim.engine.clear();
        check(applyDelta({legacyClaim, {}},
                         ops(legacyClaim, "BB",
                             R"([{"action":"transfer_territory","region":"A3","to":"DD"}])"),
                         geography())
                      .state.territories["A3"]["controller"] == "DD",
              "Legacy cession still moves control");
        // An accepted access treaty is a valid treaty in force.
        auto opened =
            signTreaty(claim, "AA", R"(["AA","DD"])", R"([{"action":"grant_access","to":"AA"}])");
        check(opened.commitments.size() == 1 && mayEnter(opened, "AA", "N2"),
              "An accepted access treaty takes effect");

        // A war declared earlier in the month draws on the same pool as the assault that follows:
        // alone Aland can take B1, but not after opening a second front against DD first. The
        // strength clears the shared seizure threshold (Plan 26 I) on one front only.
        auto stretched = w;
        stretched.actors["AA"]["status"]["military"]["active"] = 500000;
        check(done(ops(stretched, "AA", R"([{"action":"occupy","region":"B1","by":"AA"}])")),
              "One front: strong enough to take B1");
        auto overreach = ops(stretched, "AA",
                             R"([{"action":"open_conflict","actors":["AA","DD"],"kind":"war",)"
                             R"("intensity":"medium","name":"Northern War","text":"War on DD"},)"
                             R"({"action":"occupy","region":"B1","by":"AA"}])");
        bool declared = false, took = false;
        for (const auto &c : overreach.changes) {
            declared = declared || c["kind"] == "conflict";
            took = took || (c["kind"] == "territory" && c["id"] == "B1");
        }
        check(declared && !took, "A second front declared this month leaves too little to take B1");

        std::cout << checks << " access, movement and construction checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
