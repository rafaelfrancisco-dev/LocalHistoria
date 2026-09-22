#include "localhistoria/status.h"
#include "localhistoria/store.h"
#include <cmath>
#include <iostream>
using namespace lh;
static void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
static Json::Value event(const std::string &actor, const Json::Value &ops) {
    Json::Value e;
    e["actor"] = actor;
    e["description"] = "Military test";
    e["blocked_by"] = "none";
    e["background"] = false;
    e["causes"] = Json::arrayValue;
    e["operations"] = ops;
    Json::Value response;
    response["events"].append(e);
    return response;
}
static WorldState fixture(int active = 15000, double gdp = 1e12) {
    WorldState w;
    w.campaignId = "military";
    w.player = "A";
    w.turn = 1;
    w.actors = parse(
        R"({"A":{"name":"Alpha","color":"#777777"},"D":{"name":"Defender","color":"#888888"}})");
    for (auto a : {"A", "D"}) {
        auto &s = w.actors[a]["status"];
        s["stability"] = s["baseline_stability"] = 50;
        s["military"] =
            parse(R"({"active":15000,"reserve":10000,"ships":4,"aircraft":100,"estimated":0})");
        auto &t = w.territories[a];
        t["name"] = a;
        t["owner"] = t["controller"] = t["initial_affiliation"] = a;
        t["population"] = 1000000;
        t["gdp_usd"] = Json::Int64(gdp);
        t["longitude"] = t["latitude"] = 1;
    }
    w.actors["A"]["status"]["military"]["active"] = active;
    return w;
}
static Json::Value army(const std::string &actor, int n, const char *status = "active") {
    Json::Value e;
    e["name"] = "Field Army";
    e["kind"] = "army";
    e["actor"] = actor;
    e["region"] = actor;
    e["status"] = status;
    e["personnel"] = n;
    e["longitude"] = e["latitude"] = 1;
    e["origin_event"] = "seed";
    return e;
}
int main() {
    try {
        Adjacency adj{{"A", {"D"}}, {"D", {"A"}}};
        auto w = fixture();
        w.entities["one"] = army("A", 5000);
        w.entities["two"] = army("A", 6000);
        auto build = parse(
            R"([{"action":"build","actor":"A","region":"A","kind":"army","name":"Third Army"}])");
        auto d = compileOperations(w, event("A", build), adj);
        check(d.changes.empty() && dump(d.events).find("no troops left") != std::string::npos,
              "Armies exhaust their national pool");
        w.entities["two"]["status"] = "inactive";
        d = compileOperations(w, event("A", build), adj);
        check(d.changes.size() == 1 && d.changes[0]["after"]["personnel"] == 5000,
              "Inactive armies release allocations");
        w = fixture(15000, 100e9);
        build.append(build[0]);
        build[1]["name"] = "Fourth Army";
        d = compileOperations(w, event("A", build), adj);
        check(d.changes.size() == 1 &&
                  dump(d.events).find("construction capacity") != std::string::npos,
              "Pending builds reserve capacity");
        auto site = build[0];
        site["kind"] = "event_site";
        site["name"] = "Memorial";
        build.insert(0, site);
        d = compileOperations(w, event("A", build), adj);
        check(d.changes.size() == 2, "Event sites do not consume capacity");
        w = fixture(11000);
        build.removeIndex(0, nullptr);
        d = compileOperations(w, event("A", build), adj);
        check(d.changes.size() == 2 && d.changes[0]["after"]["personnel"] == 5000 &&
                  d.changes[1]["after"]["personnel"] == 5000,
              "Pending armies reserve personnel");
        build.append(build[1]);
        build[2]["name"] = "Fifth Army";
        d = compileOperations(w, event("A", build), adj);
        check(d.changes.size() == 2 && dump(d.events).find("no troops left") != std::string::npos,
              "Third army cannot reuse reservations");
        w.actors["A"]["status"]["military"]["ships"] = 0;
        build.resize(1);
        build[0]["kind"] = "fleet";
        check(compileOperations(w, event("A", build), adj).changes.empty(),
              "No fleets without warships");
        w = fixture(1000);
        w.actors["A"]["status"]["military"]["aircraft"] = 0;
        auto invade = parse(R"([{"action":"occupy","by":"A","region":"D"}])");
        d = compileOperations(w, event("A", invade), adj);
        check(d.changes.empty() && dump(d.events).find("too weak") != std::string::npos,
              "Weak raids rejected");
        w = fixture();
        w.entities["one"] = army("A", 5000);
        auto destroyed = w.entities["one"];
        destroyed["status"] = "destroyed";
        WorldDelta death;
        Json::Value change;
        change["kind"] = "entity";
        change["id"] = "one";
        change["before"] = w.entities["one"];
        change["after"] = destroyed;
        death.changes.append(change);
        w.entities["one"] = destroyed;
        auto result = applyMonthlyStatus({w, {}}, death, adj);
        check(result.state.actors["A"]["status"]["military"]["active"] == 10000,
              "Destroyed personnel removed");
        result = applyMonthlyStatus(result, {}, adj);
        check(result.state.actors["A"]["status"]["military"]["active"] == 10000,
              "Destruction charged once");
        w = fixture();
        w.entities["old"] = army("A", 10000);
        w.entities["old"].removeMember("personnel");
        auto prior = w.territories;
        seedMilitary(w, StatusSeed{});
        check(w.entities["old"]["personnel"] == 10000 && w.territories == prior,
              "Legacy formation backfill preserves demographics");
        auto encoded = encode(w);
        seedMilitary(w, StatusSeed{});
        check(encode(w) == encoded, "Backfill idempotent");
        // Battle thresholds use exact fixtures, without GDP drift or diplomatic seed modifiers.
        // GDP per head of 20,000 makes quality exactly 1.0, so the ratios below test the
        // thresholds themselves rather than the rounding of a quality multiplier.
        w = fixture(15000, 2e10);
        for (auto a : {"A", "D"}) {
            auto &m = w.actors[a]["status"]["military"];
            // Reserves are zeroed with ships and aircraft so these cases measure the thresholds
            // against active strength alone; the reserve's own contribution is checked below.
            m["ships"] = m["aircraft"] = m["reserve"] = 0;
        }
        w.actors["D"]["status"]["military"]["active"] = 10000;
        check(force::assaultCapacity(w, adj, "A", "D", "D") == 1, "Limited attack at ratio 0.8");
        w.actors["A"]["status"]["military"]["active"] = 28125;
        check(force::assaultCapacity(w, adj, "A", "D", "D") == 2,
              "Superior at exact 1.5 threshold");
        w.actors["A"]["status"]["military"]["active"] = 14062;
        check(force::assaultCapacity(w, adj, "A", "D", "D") == 0, "Below 0.75 is too weak");
        w.entities["one"] = army("A", 5000);
        check(force::uncommittedPersonnel(w, "A") == 9062, "No double-counting field armies");
        // Reservists hold home ground: they stiffen a defence without ever mounting an attack.
        auto reserved = fixture(28125, 2e10);
        for (auto a : {"A", "D"}) {
            auto &m = reserved.actors[a]["status"]["military"];
            m["ships"] = m["aircraft"] = m["reserve"] = 0;
        }
        reserved.actors["D"]["status"]["military"]["active"] = 10000;
        check(force::assaultCapacity(reserved, adj, "A", "D", "D") == 2,
              "Superior against a defender with no reserve");
        reserved.actors["D"]["status"]["military"]["reserve"] = 25000;
        check(force::assaultCapacity(reserved, adj, "A", "D", "D") == 1,
              "A large reserve blunts the same attack");
        reserved.actors["A"]["status"]["military"]["reserve"] = 400000;
        check(force::assaultCapacity(reserved, adj, "A", "D", "D") == 1,
              "The attacker's own reserve does not march");
        // Pinned national seed: reference values and actual front armies, not copied arithmetic.
        Store db(std::string(SOURCE_DIR) + "/data/world.sqlite", true);
        StatusSeed seed;
        WorldState real;
        for (auto &r : db.query("SELECT * FROM country_stats")) {
            auto a = r["code"].asString();
            seed.countries[a] = r;
            real.actors[a]["name"] = a;
        }
        for (auto &r : db.query("SELECT * FROM military_stats"))
            seed.military[r["code"].asString()] = r;
        for (auto &r : db.query("SELECT * FROM region_stats"))
            seed.regions[r["id"].asString()] = r;
        for (auto &r : db.query("SELECT id,name,affiliation,kind,coastal FROM regions")) {
            auto &t = real.territories[r["id"].asString()];
            t = r;
            t["owner"] = t["controller"] = t["initial_affiliation"] = r["affiliation"];
        }
        Adjacency realAdj;
        for (auto &r : db.query("SELECT a,b FROM region_neighbors"))
            realAdj[r["a"].asString()].insert(r["b"].asString());
        seed.world = parse(db.query("SELECT document FROM world_seed")[0]["document"].asString());
        seedStatus(real, seed, true);
        check(hasMilitary(real) && real.entities.size() == 4,
              "Every actor seeded and four front armies created");
        auto region = [&](const std::string &a, const std::string &name = "") {
            for (const auto &id : real.territories.getMemberNames())
                if (real.territories[id]["owner"] == a &&
                    real.territories[id]["kind"] != "residual" &&
                    (name.empty() || real.territories[id]["name"] == name))
                    return id;
            throw std::runtime_error("missing region");
        };
        auto capacity = [&](const char *a, const char *b, int expected,
                            const std::string &name = "") {
            auto r = region(b, name);
            int n = force::assaultCapacity(real, realAdj, a, b, r);
            std::cout << a << " -> " << b << ": " << n << " regions; "
                      << force::reachDescription(real, realAdj, a, r) << "\n";
            check(n == expected,
                  std::string(a) + " -> " + b + " expected " + std::to_string(expected));
        };
        // Plan 24 made reserves count in defence, and Portugal's 211,000 reservists move Spain off
        // the exact 1.5 threshold it used to sit on. One region a month rather than two is the
        // better answer: two comparable European states grind, and under front momentum capacity 2
        // would have rolled this border unordered.
        capacity("ES", "PT", 1);
        capacity("PT", "ES", 0);
        capacity("IN", "PK", 1);
        capacity("PK", "IN", 0);
        capacity("KM", "MG", 0);
        capacity("RU", "UA", 1, "Dnipropetrovsk Oblast");
        capacity("UA", "RU", 0, "Belgorod Oblast");
        // Plan 21 re-based these bands on active-duty strength. The top band is no longer
        // unreachable, and the largest armies outrank smaller wealthy ones.
        check(force::militaryLabel(real, "US") == "superpower" &&
                  force::militaryLabel(real, "CN") == "superpower" &&
                  force::militaryLabel(real, "IN") == "major power" &&
                  force::militaryLabel(real, "KR") == "major power" &&
                  force::militaryLabel(real, "BR") == "regional power" &&
                  force::militaryLabel(real, "KM") == "negligible",
              "Reference strength labels");
        auto month = applyMonthlyStatus({real, {}}, {}, realAdj);
        const auto &m = month.state.actors["UA"]["status"]["military"];
        check(m["reserve"].asInt() == 1176000 && m["active"].asInt() > 900000 &&
                  m["active"].asInt() < 924000,
              "Ukraine mobilises 2% reserves and loses active personnel");
        check(encode(month.state) == encode(applyMonthlyStatus({real, {}}, {}, realAdj).state),
              "Monthly military state deterministic");
        // Seizing a region displaces the people the fighting reaches, not a whole province.
        auto seize = [&](const std::string &taker, const std::string &loser,
                         const std::string &name) {
            auto state = real;
            auto id = region(loser, name);
            if (!(state.conflicts.isMember("c0-1") && taker == "RU"))
                state.conflicts["c9-1"] =
                    parse(R"({"name":"Test War","kind":"war","status":"open","intensity":"high",)"
                          R"("started_turn":0,"casualties":0,"displaced":0})"),
                state.conflicts["c9-1"]["actors"].append(taker),
                state.conflicts["c9-1"]["actors"].append(loser);
            WorldDelta d;
            Json::Value ch;
            ch["kind"] = "territory";
            ch["id"] = id;
            ch["before"] = state.territories[id];
            state.territories[id]["controller"] = taker;
            ch["after"] = state.territories[id];
            d.changes.append(ch);
            auto population = state.territories[id]["population"].asDouble();
            auto out = applyMonthlyStatus({state, {}}, d, realAdj);
            auto share = (out.state.territories[id]["displaced"].asDouble() -
                          state.territories[id].get("displaced", 0).asDouble()) /
                         population;
            std::cout << taker << " seizes " << name << " (" << population / 1e6
                      << " M): " << share * 100 << "% displaced\n";
            return share;
        };
        auto punjab = seize("IN", "PK", "Punjab");
        check(punjab > 0.001 && punjab < 0.02,
              "A province-sized seizure displaces the combat zone, not 8% of the province");
        check(seize("RU", "UA", "Dnipropetrovsk Oblast") > 0.08,
              "Russia's committed force still reaches a whole oblast");

        // The seeded January 2026 wars are all contested, so a year of them must move no ground at
        // all. A model that quietly unfreezes the Russo-Ukrainian line is worse than one that
        // never moves it, and this is the assertion that keeps it honest.
        auto decade = Resolution{real, {}};
        for (int m = 0; m < 12; ++m) {
            decade.state.turn++;
            decade = applyMonthlyStatus(decade, {}, realAdj);
            for (const auto &id : real.territories.getMemberNames())
                check(decade.state.territories[id]["controller"] ==
                          real.territories[id]["controller"],
                      "Every seeded front stays fixed in every passive month");
        }
        int moved = 0;
        for (auto &id : real.territories.getMemberNames())
            if (decade.state.territories[id]["controller"] != real.territories[id]["controller"]) {
                std::cout << "unexpected advance: " << id << " "
                          << real.territories[id]["controller"].asString() << " -> "
                          << decade.state.territories[id]["controller"].asString() << "\n";
                ++moved;
            }
        check(!moved, "A year of the seeded wars moves no front: " + std::to_string(moved) +
                          " regions changed hands");
        check(decade.state.conflicts["c0-1"]["static_months"].asInt() == 12 &&
                  decade.state.conflicts["c0-1"]["intensity"] == "high",
              "The Russo-Ukrainian front is recorded as frozen, and still fought at full "
              "intensity: " +
                  dump(decade.state.conflicts["c0-1"]["static_months"]));

        auto brazil = Resolution{real, {}};
        brazil.state.conflicts["brazil-paraguay"] =
            parse(R"({"name":"Brazil-Paraguay War","kind":"war","status":"open","text":"War",)"
                  R"("actors":["BR","PY"],"started_turn":0,"set_intensity":"high"})");
        int unordered = 0;
        for (int m = 0; m < 6; ++m) {
            brazil = applyMonthlyStatus(resolve(brazil.state), {}, realAdj);
            int thisMonth = 0;
            for (const auto &event : brazil.events)
                if (event.action == "front_advance" && event.target == "brazil-paraguay") {
                    ++thisMonth;
                    check(event.actor == "BR" &&
                              brazil.state.territories[event.region]["owner"] == "PY",
                          "Brazil's autonomous advance preserves Paraguayan ownership");
                }
            check(thisMonth <= 2, "Real-world autonomous advances respect the monthly cap");
            unordered += thisMonth;
        }
        check(unordered >= 2, "Seeded Brazil advances against Paraguay without monthly orders");

        // Front momentum: a war moves ground on its own where one side is decisively superior,
        // and nowhere else.
        // `defenderAir` 0 is the Paraguay case: a neighbour with no air force at all. Left at 100
        // the two sides' aircraft cancel and the x1.5 defence bonus decides, which is what keeps
        // a comparable pair contested however much infantry the attacker has.
        auto war = [&](int attacker, const char *intensity = "high", int defenderAir = 0) {
            auto state = fixture(attacker);
            state.actors["D"]["status"]["military"]["aircraft"] = defenderAir;
            state.conflicts["c1-1"] =
                parse(R"({"name":"Test War","kind":"war","status":"open","started_turn":0,)"
                      R"("text":"Border war","casualties":0,"displaced":0,"actors":["A","D"]})");
            state.conflicts["c1-1"]["intensity"] = state.conflicts["c1-1"]["set_intensity"] =
                intensity;
            return Resolution{state, {}};
        };
        auto months = [&](Resolution start, int n) {
            for (int m = 0; m < n; ++m) {
                start.state.turn++;
                start = applyMonthlyStatus(start, {}, adj);
            }
            return start;
        };
        // 400,000 against 15,000 with no air defence is capacity 2 at about 14:1 — the Brazil
        // against Paraguay case. The front moves without anyone ordering it.
        auto overrun = war(400000);
        int fell = 0;
        std::string whenTaken;
        for (int m = 1; m <= 3 && !fell; ++m) {
            overrun.state.turn++;
            overrun = applyMonthlyStatus(overrun, {}, adj);
            for (auto &e : overrun.events)
                if (e.action == "front_advance") {
                    fell = m;
                    whenTaken = e.text;
                }
            if (fell)
                check(overrun.state.conflicts["c1-1"]["intensity"] == "high",
                      "Ground changing hands is full-intensity fighting");
        }
        check(fell && overrun.state.territories["D"]["controller"] == "A" &&
                  overrun.state.territories["D"]["owner"] == "D",
              "An overwhelming attacker takes ground unordered, and only its control: " +
                  dump(overrun.state.territories["D"]));
        std::cout << "Unordered advance in month " << fell << ": " << whenTaken << "\n";
        // Displacement needs somewhere to go and this fixture's defender owns one region, so the
        // toll is what proves the seizure was fought over rather than conjured.
        check(overrun.state.conflicts["c1-1"]["casualties"].asInt64() > 0 &&
                  overrun.state.conflicts["c1-1"]["affected_regions"][0] == "D",
              "A momentum seizure costs lives and draws a front like an ordered one: " +
                  dump(overrun.state.conflicts["c1-1"]));
        // Four times the infantry, but matched in the air and facing dug-in defenders, is
        // capacity 1: the Russo-Ukrainian case. It never moves, however long it runs, and never
        // banks pressure against the day it might.
        auto frozen = months(war(60000, "high", 100), 24);
        check(frozen.state.territories["D"]["controller"] == "D" &&
                  frozen.state.territories["A"]["controller"] == "A",
              "A contested front never moves on its own");
        check(!frozen.state.conflicts["c1-1"].isMember("pressure"),
              "A contested attacker banks no pressure: " + dump(frozen.state.conflicts["c1-1"]));
        check(frozen.state.conflicts["c1-1"]["static_months"].asInt() == 24 &&
                  frozen.state.conflicts["c1-1"]["intensity"] == "high",
              "A stalemate is recorded but still bleeds: " + dump(frozen.state.conflicts["c1-1"]));
        check(months(war(400000, "low"), 6).state.territories["D"]["controller"] == "D",
              "A declared lull moves no ground");
        auto truce = war(400000);
        truce.state.commitments["k1-1"] =
            parse(R"({"name":"Truce","kind":"ceasefire","actors":["A","D"],"status":"open",)"
                  R"("text":"Ceasefire"})");
        check(months(truce, 6).state.territories["D"]["controller"] == "D",
              "A ceasefire holds the line");
        // Symmetry: what an occupier took, a defender that gains the upper hand takes back. This
        // needs a defender with a second province — one that has lost all its land has no reach
        // left to counterattack from, which is the same rule the compiler applies to orders.
        Adjacency deep{{"A", {"D"}}, {"D", {"A", "D2"}}, {"D2", {"D"}}};
        auto counter = war(400000);
        counter.state.territories["D2"] = counter.state.territories["D"];
        counter.state.territories["D2"]["name"] = "D2";
        for (int m = 0; m < 3 && counter.state.territories["D"]["controller"] == "D"; ++m) {
            counter.state.turn++;
            counter = applyMonthlyStatus(counter, {}, deep);
        }
        check(counter.state.territories["D"]["controller"] == "A" &&
                  counter.state.territories["D2"]["controller"] == "D",
              "The stronger side advances province by province, not all at once: " +
                  dump(counter.state.territories["D"]));
        counter.state.actors["D"]["status"]["military"]["active"] = 800000;
        counter.state.actors["A"]["status"]["military"]["active"] = 15000;
        counter.state.actors["A"]["status"]["military"]["aircraft"] = 0;
        for (int m = 0; m < 6 && counter.state.territories["D"]["controller"] == "A"; ++m) {
            counter.state.turn++;
            counter = applyMonthlyStatus(counter, {}, deep);
        }
        check(counter.state.territories["D"]["controller"] == "D",
              "A defender that gains the upper hand retakes its own ground");
        // Both gates: a month recorded before this rule, and a world without inventories.
        auto old = war(400000);
        old.state.turn++;
        check(applyMonthlyStatus(old, {}, adj, statusVersion - 1)
                      .state.territories["D"]["controller"] == "D",
              "Turns recorded under earlier rules keep their front");
        auto bare = war(400000);
        for (auto a : {"A", "D"})
            bare.state.actors[a]["status"].removeMember("military");
        check(months(bare, 6).state.territories["D"]["controller"] == "D",
              "Without inventories nothing advances");
        check(encode(months(war(400000), fell).state) == encode(overrun.state),
              "Front momentum is deterministic");
        auto mixed = war(400000);
        mixed.state.actors["D"]["status"].removeMember("military");
        check(months(mixed, 6).state.territories["D"]["controller"] == "D",
              "A missing defender inventory is unknown strength, not zero defence");

        // Seizure displacement has somewhere to go, and two advances in one conflict retain
        // distinct IDs and exact locations even after the front moves on.
        auto sweep = war(400000);
        sweep.state.territories["D2"] = sweep.state.territories["D"];
        sweep.state.territories["D3"] = sweep.state.territories["D"];
        sweep.state.conflicts["c1-1"]["pressure"]["A"] = 2.0;
        Adjacency broad{
            {"A", {"D", "D2"}}, {"D", {"A", "D3"}}, {"D2", {"A", "D3"}}, {"D3", {"D", "D2"}}};
        Store frontLog(":memory:");
        frontLog.create(sweep.state, false);
        auto swept = applyMonthlyStatus(resolve(sweep.state), {}, broad);
        int advances = 0;
        for (const auto &event : swept.events)
            advances += event.action == "front_advance";
        check(advances == 2 && swept.state.territories["D3"]["controller"] == "D" &&
                  swept.state.territories["D"]["displaced"].asInt64() > 0,
              "Two-region cap and one-off displacement apply to autonomous advances");
        frontLog.commit(frontLog.submit(sweep.state.turn, "advance", "{}")["id"].asString(), swept,
                        {}, {});
        auto logged = frontLog.eventLog(swept.state, parse(R"({"major":true})"))["events"];
        check(logged.size() == 2 && logged[0]["id"] != logged[1]["id"] &&
                  logged[0]["regions"].size() == 1 && logged[1]["regions"].size() == 1 &&
                  logged[0]["regions"][0] != logged[1]["regions"][0] &&
                  logged[0]["conflicts"][0] == "c1-1",
              "Each autonomous advance has a unique map event at its captured region");

        auto alreadyTaken = sweep;
        auto ordered = compileOperations(
            alreadyTaken.state, event("A", parse(R"([{"action":"occupy","region":"D","by":"A"},
                                 {"action":"occupy","region":"D2","by":"A"}])")),
            broad);
        auto combined = applyMonthlyStatus(applyDelta(alreadyTaken, ordered, Json::objectValue),
                                           ordered, broad);
        for (const auto &event : combined.events)
            check(event.action != "front_advance", "Ordered seizures spend the same monthly cap");
        std::cout << "Military inventory, reservations, combat, migration and attrition passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
