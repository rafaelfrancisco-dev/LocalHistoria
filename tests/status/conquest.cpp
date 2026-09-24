#include "localhistoria/lifecycle.h"
#include "status_tests.h"
namespace {
// Aland (a1 capital, a2 and a3 provinces) and Bland (b1, b2), under the realism lifecycle. a2
// borders b1.
Resolution world(const char *aland = "presidential_republic", double alandStability = 60,
                 const char *bland = "presidential_republic") {
    WorldState w;
    w.player = "AA";
    w.campaignId = "conquest";
    w.engine["lifecycle_version"] = realismLifecycleVersion;
    w.actors = parse(R"({"AA":{"name":"Aland","color":"#111111"},)"
                     R"("BB":{"name":"Bland","color":"#222222"}})");
    w.territories = parse(R"({
      "a1":{"name":"Capital","owner":"AA","controller":"AA","initial_affiliation":"AA","kind":"ADM1"},
      "a2":{"name":"Northmark","owner":"AA","controller":"AA","initial_affiliation":"AA","kind":"ADM1"},
      "a3":{"name":"Southmark","owner":"AA","controller":"AA","initial_affiliation":"AA","kind":"ADM1"},
      "b1":{"name":"Bland Capital","owner":"BB","controller":"BB","initial_affiliation":"BB","kind":"ADM1"},
      "b2":{"name":"Bland West","owner":"BB","controller":"BB","initial_affiliation":"BB","kind":"ADM1"}})");
    StatusSeed seed;
    seed.countries = parse(R"({
      "AA":{"population":20000000,"gdp_usd":200000000000,"gdp_growth":0,"pop_growth":0,
            "stability":60,"currency":"USD","symbol":"US$","usd_rate":1,"estimated":0},
      "BB":{"population":10000000,"gdp_usd":100000000000,"gdp_growth":0,"pop_growth":0,
            "stability":60,"currency":"USD","symbol":"US$","usd_rate":1,"estimated":0}})");
    seed.countries["AA"]["government"] = aland;
    seed.countries["AA"]["stability"] = alandStability;
    seed.countries["BB"]["government"] = bland;
    seed.regions = parse(R"({"a1":{"population":10000000,"gdp_usd":100000000000},
      "a2":{"population":5000000,"gdp_usd":50000000000},
      "a3":{"population":5000000,"gdp_usd":50000000000},
      "b1":{"population":5000000,"gdp_usd":50000000000},
      "b2":{"population":5000000,"gdp_usd":50000000000}})");
    seed.military = parse(R"({"AA":{"active":80000,"reserve":40000,"ships":0,"aircraft":0},
      "BB":{"active":20000,"reserve":10000,"ships":0,"aircraft":0}})");
    seedStatus(w, seed, false);
    seedMilitary(w, seed);
    return Resolution{w, {}, realismLifecycleVersion};
}
const Adjacency adjacency{{"a1", {"a2", "a3"}},
                          {"a2", {"a1", "a3", "b1"}},
                          {"a3", {"a1", "a2"}},
                          {"b1", {"a2", "b2"}},
                          {"b2", {"b1"}}};
Json::Value refs() {
    Json::Value r;
    r["lifecycle_version"] = realismLifecycleVersion;
    r["status_version"] = statusVersion;
    return r;
}
// Everything the months below reported, in order: each month starts a fresh event list.
std::vector<ResolvedEvent> journal;
// A month of play with no orders, through the same lifecycle live play and replay use.
Resolution month(const Resolution &r) {
    auto next = finishTurn(beginTurn(r.state, refs()), {}, adjacency,
                           Json::Value(Json::objectValue), refs());
    journal.insert(journal.end(), next.events.begin(), next.events.end());
    return next;
}
Resolution months(Resolution r, int n) {
    for (int m = 0; m < n; ++m)
        r = month(r);
    return r;
}
// Bland takes Northmark by force this month: the monthly step, not the migration, records it.
Resolution seize(Resolution r) {
    r.state.territories["a2"]["controller"] = "BB";
    r.state.turn++;
    return applyMonthlyStatus(r, {}, adjacency);
}
double unrestOf(const Resolution &r, const char *region) {
    return r.state.territories[region]["unrest"].asDouble();
}
int count(size_t from, const char *action) {
    int n = 0;
    for (size_t i = from; i < journal.size(); ++i)
        n += journal[i].action == action;
    return n;
}
std::string num(double v) { return std::to_string(v); }
} // namespace

void conquestTests() {
    // What a seizure records, and how the migration differs from a seizure it witnesses.
    {
        auto taken = seize(world());
        const auto &record = taken.state.territories["a2"]["control"];
        check(record["claimant"] == "AA" && record["holder"] == "BB" &&
                  record["cause"] == "hostile" && record["since"] == taken.state.turn &&
                  record["integration"] == 0.0,
              "A seizure opens a control record: " + dump(record));
        auto found = world();
        found.state.territories["a2"]["controller"] = "BB";
        auto migrated = beginTurn(found.state, refs()).state.territories["a2"]["control"];
        check(migrated["cause"] == "recorded" && !migrated.isMember("since"),
              "An occupation predating the history is recorded without an invented age");
        auto lent = world();
        lent.state.territories["a2"]["controller"] = "BB";
        lent.state.commitments["k0-1"] =
            parse(R"({"name":"Lease","kind":"other","status":"open","text":"Lease",)"
                  R"("actors":["AA","BB"],"terms":[{"action":"occupy","region":"a2","by":"BB"}]})");
        auto leased = months(lent, 6);
        check(!leased.state.territories["a2"].isMember("control") &&
                  !leased.state.territories["a2"].isMember("occupation_gdp") &&
                  unrestOf(leased, "a2") == 0,
              "Land lent by treaty is neither occupied, drained nor resentful");
        check(outputShare(leased.state, "a2", "AA") == 1 &&
                  outputShare(leased.state, "a2", "BB") == 0,
              "Land lent by treaty still produces for its owner");
        auto rising = world();
        rising.state.actors["BB"]["name"] = "Northmark Front";
        rising.state.conflicts["c0-1"] =
            parse(R"({"name":"Northmark Insurgency","kind":"insurgency","status":"open",)"
                  R"("text":"Revolt","actors":["AA","BB"],"started_turn":0,)"
                  R"("set_intensity":"low","intensity":"low","theatre":[],"affected_regions":[]})");
        rising.state.territories["a2"]["controller"] = "BB";
        check(!beginTurn(rising.state, refs()).state.territories["a2"].isMember("control"),
              "An insurgency holding its own province is not a foreign occupation");
    }

    // A stable occupier still faces resistance. Under the old rules the same occupation accrued
    // nothing, because resistance waited for the occupier's own decline.
    Resolution held;
    {
        auto reported = journal.size();
        held = months(seize(world()), 24);
        double stability = held.state.actors["BB"]["status"]["stability"].asDouble();
        check(stability > 52, "Bland stays stable: " + num(stability));
        check(unrestOf(held, "a2") >= 35,
              "A stable occupier faces resistance: " + num(unrestOf(held, "a2")));
        check(count(reported, "unrest") >= 1, "The resistance is reported");
        check(held.state.territories["a2"].isMember("occupation_gdp") &&
                  outputShare(held.state, "a2", "AA") == 0 &&
                  outputShare(held.state, "a2", "BB") == 0.3,
              "Occupied output is lost to its owner and only partly usable by the occupier");
        auto legacy = world();
        legacy.state.engine.clear();
        legacy.state.territories["a2"]["controller"] = "BB";
        legacy.lifecycleVersion = legacyLifecycleVersion;
        for (int m = 0; m < 24; ++m) {
            legacy.state.turn++;
            legacy = applyMonthlyStatus(legacy, {}, adjacency);
        }
        check(unrestOf(legacy, "a2") == 0,
              "Legacy rules keep their old answer: " + num(unrestOf(legacy, "a2")));
        // A garrison in strength damps it but does not end it.
        auto garrisoned = seize(world());
        garrisoned.state.actors["BB"]["status"]["military"]["active"] = 60000;
        garrisoned.state.entities["garrison"] =
            parse(R"({"name":"Garrison","actor":"BB","kind":"army","region":"a2",)"
                  R"("status":"active","personnel":40000,"origin_event":"seed"})");
        garrisoned = months(garrisoned, 24);
        std::cout << "Two years under a stable occupier: unrest " << unrestOf(held, "a2")
                  << ", garrisoned " << unrestOf(garrisoned, "a2") << "\n";
        check(unrestOf(garrisoned, "a2") >= 35 && unrestOf(garrisoned, "a2") < unrestOf(held, "a2"),
              "A strong garrison damps resistance without ending it: " +
                  num(unrestOf(garrisoned, "a2")));
    }

    // Annexation changes the claim but not the conquest.
    {
        auto annexed = held;
        annexed.state.territories["a2"]["owner"] = "BB";
        auto later = months(annexed, 12);
        const auto &t = later.state.territories["a2"];
        check(t["control"]["claimant"] == "AA" && t["control"]["holder"] == "BB",
              "Annexation keeps the conquest's history: " + dump(t));
        check(t.isMember("occupation_gdp") && unrestOf(later, "a2") >= 35,
              "Annexation does not end the drain or the resistance");
        double share = outputShare(later.state, "a2", "BB");
        check(share > 0.3 && share < 0.5, "Annexed output is still mostly unusable: " + num(share));
        auto [aland, people] = effectiveOutput(later.state, "AA");
        check(aland < 1.6e11, "The claimant's usable output excludes its lost province");
        // Aland still feels the province it lost, as it did before the annexation.
        auto annexedAland = later.state.actors["AA"]["status"]["stability"].asDouble();
        auto occupiedAland = months(held, 12).state.actors["AA"]["status"]["stability"].asDouble();
        check(std::abs(annexedAland - occupiedAland) < 1,
              "The claimant's loss persists through annexation: " + num(annexedAland) + " vs " +
                  num(occupiedAland));
        // Integration: cooperative, at peace and short of revolt, a conquest is absorbed in about
        // five years, and resentment falls as it goes. Fighting there sets it back.
        auto absorbed = annexed;
        absorbed.state.regionalPolitics["a2"] = parse(
            R"({"agenda":"Order","relations":"cooperative","supporting_events":[],"demands":[]})");
        auto opened = journal.size();
        int integratedAt = 0;
        double halfway = 0;
        for (int m = 0; m < 70 && !integratedAt; ++m) {
            absorbed = month(absorbed);
            if (m == 30)
                halfway = unrestOf(absorbed, "a2");
            if (count(opened, "integrated"))
                integratedAt = m + 1;
        }
        std::cout << "Cooperative annexed province integrated after " << integratedAt
                  << " months; unrest " << unrestOf(annexed, "a2") << " at annexation, " << halfway
                  << " after 31 months\n";
        check(integratedAt >= 50 && integratedAt <= 65,
              "A cooperative annexed province integrates in about five years: " +
                  std::to_string(integratedAt));
        check(!absorbed.state.territories["a2"].isMember("control") &&
                  outputShare(absorbed.state, "a2", "BB") == 1,
              "An integrated province is ordinary territory");
        check(halfway < unrestOf(annexed, "a2"), "Integration lowers resentment: " + num(halfway) +
                                                     " vs " + num(unrestOf(annexed, "a2")));
        auto contested = annexed;
        contested.state.conflicts["c0-1"] =
            parse(R"({"name":"Northmark War","kind":"war","status":"open","text":"War",)"
                  R"("actors":["AA","BB"],"started_turn":0,"set_intensity":"medium",)"
                  R"("intensity":"medium","theatre":["a2"],"affected_regions":["a2"],)"
                  R"("hostile_pairs":[{"actors":["AA","BB"],"status":"active"}],)"
                  R"("postures":{"AA":"medium","BB":"medium"}})");
        contested.state.territories["a2"]["control"]["integration"] = 40.0;
        auto fought = month(contested);
        check(fought.state.territories["a2"]["control"]["integration"].asDouble() <= 30,
              "Fighting sets integration back: " + dump(fought.state.territories["a2"]["control"]));
        // Liberation ends the occupation, not the memory: unrest falls away over months.
        auto freed = held;
        freed.state.territories["a2"]["controller"] = "AA";
        auto after = month(freed);
        check(!after.state.territories["a2"].isMember("control") && unrestOf(after, "a2") > 0 &&
                  unrestOf(after, "a2") < unrestOf(held, "a2"),
              "Liberation lifts occupation pressure without erasing grievance at once: " +
                  num(unrestOf(after, "a2")));
    }

    // An occupied war theatre can rise, once, and the rising fights for the nation it was taken
    // from. The old guard refused any region an open war passed through.
    {
        auto theatre = seize(world("presidential_republic", 60, "presidential_republic"));
        theatre.state.conflicts["c0-1"] =
            parse(R"({"name":"Northmark War","kind":"war","status":"open","text":"War",)"
                  R"("actors":["AA","BB"],"started_turn":0,"set_intensity":"low",)"
                  R"("intensity":"low","theatre":["a2"],"affected_regions":["a2"],)"
                  R"("hostile_pairs":[{"actors":["AA","BB"],"status":"active"}],)"
                  R"("postures":{"AA":"low","BB":"low"}})");
        theatre.state.territories["a2"]["displaced"] = 250000;
        theatre.state.regionalPolitics["a2"] = parse(
            R"({"agenda":"Liberation","relations":"strained","supporting_events":[],"demands":[]})");
        auto r = theatre;
        auto opened = journal.size();
        int rose = 0;
        Json::Int64 armyBefore = 0, armyAfter = 0;
        for (int m = 0; m < 60 && !rose; ++m) {
            armyBefore = r.state.actors["BB"]["status"]["military"]["active"].asInt64();
            r = month(r);
            armyAfter = r.state.actors["BB"]["status"]["military"]["active"].asInt64();
            if (count(opened, "revolt"))
                rose = m + 1;
        }
        std::cout << "Occupied war theatre rose after " << rose << " months\n";
        check(rose > 0, "An occupied war theatre develops an insurgency within five years: " +
                            num(unrestOf(r, "a2")));
        // Read through const views: a non-const lookup would add null members to the state.
        const auto &actors = std::as_const(r.state.actors);
        std::string rebel;
        for (const auto &a : actors.getMemberNames())
            if (actors[a]["rebellion"].isObject())
                rebel = a;
        const auto &rebellion = actors[rebel]["rebellion"];
        const auto &conflict = std::as_const(r.state.conflicts)[rebellion["conflict"].asString()];
        check(rebellion["against"] == "BB" && rebellion["resisting_for"] == "AA" &&
                  conflict["kind"] == "insurgency" &&
                  conflict["text"].asString().find("occupation") != std::string::npos,
              "The rising resists the occupier on the claimant's behalf: " + dump(rebellion));
        auto rebels = actors[rebel]["status"]["military"]["active"].asInt64();
        check(rebels >= 1500 && armyBefore - armyAfter < rebels,
              "Resistance is raised locally, not by defection from the occupier");
        auto settled = journal.size();
        auto on = months(r, 18);
        check(count(settled, "revolt") == 0 && count(settled, "secession") == 0,
              "One rising per occupation, and a resistance declares no state of its own");
        check(encode(months(theatre, rose).state) == encode(r.state),
              "Occupation resistance is deterministic");
    }

    // Status version 5 (plan 28 D): occupation resentment climbs at one rate whoever occupies,
    // occupations older than the campaign start where they have long been, and land an armed
    // movement holds is restive too.
    {
        auto after = [](Resolution r, int n, int version) {
            auto v = refs();
            v["status_version"] = version;
            for (int m = 0; m < n; ++m)
                r = finishTurn(beginTurn(r.state, v), {}, adjacency, Json::Value(Json::objectValue), v);
            return r;
        };
        auto taken = [](const char *occupier, int version) {
            auto r = world("presidential_republic", 60, occupier);
            r.state.territories["a2"]["controller"] = "BB";
            r.state.turn++;
            return applyMonthlyStatus(r, {}, adjacency, version);
        };
        auto republic = after(taken("presidential_republic", 5), 3, 5);
        auto junta = after(taken("military_junta", 5), 3, 5);
        std::cout << "Four months occupied: unrest " << unrestOf(republic, "a2") << " under a republic, "
                  << unrestOf(junta, "a2") << " under a junta\n";
        check(unrestOf(republic, "a2") >= 35 &&
                  std::abs(unrestOf(republic, "a2") - unrestOf(junta, "a2")) < 1,
              "An occupation is restive within four months whoever occupies: " +
                  num(unrestOf(republic, "a2")) + " vs " + num(unrestOf(junta, "a2")));
        auto old = after(taken("presidential_republic", 4), 3, 4);
        check(unrestOf(old, "a2") < unrestOf(republic, "a2"),
              "Turns recorded at status version 4 keep the occupier-dependent climb: " +
                  num(unrestOf(old, "a2")));
        auto found = world();
        found.state.territories["a2"]["controller"] = "BB";
        auto seeded = after(found, 1, 5);
        check(unrestOf(seeded, "a2") >= 35 &&
                  seeded.state.territories["a2"]["control"]["seeded"] == true,
              "An occupation older than the campaign starts where it has long been: " +
                  num(unrestOf(seeded, "a2")));
        auto settled = after(seeded, 1, 5);
        check(std::abs(unrestOf(settled, "a2") - unrestOf(seeded, "a2")) < 10,
              "The seed is applied once: " + num(unrestOf(settled, "a2")));
        auto movement = world();
        movement.state.actors["MM"] = parse(R"({"name":"Northmark Movement","color":"#333333"})");
        // Like the seeded RSF and M23: a status and forces of its own, but no land it owns.
        movement.state.actors["MM"]["status"] = movement.state.actors["BB"]["status"];
        movement.state.conflicts["c0-9"] =
            parse(R"({"name":"Northmark Civil War","kind":"civil_war","status":"open","text":"War",)"
                  R"("actors":["AA","MM"],"started_turn":0,"set_intensity":"low","intensity":"low",)"
                  R"("theatre":["a2"],"affected_regions":["a2"],)"
                  R"("hostile_pairs":[{"actors":["AA","MM"],"status":"active"}],)"
                  R"("postures":{"AA":"low","MM":"low"}})");
        movement.state.territories["a2"]["controller"] = "MM";
        auto ruled = after(movement, 12, 5);
        bool rising = false;
        for (const auto &a : std::as_const(ruled.state.actors))
            rising = rising || a["rebellion"]["against"] == "MM";
        // 70 is the revolt threshold.
        check(unrestOf(ruled, "a2") >= 35 && unrestOf(ruled, "a2") < 70 && !rising,
              "Land an armed movement holds is restive, and raises no rising against it: " +
                  num(unrestOf(ruled, "a2")));
        check(unrestOf(after(movement, 3, 4), "a2") == 0,
              "Status version 4 left such land calm");
    }

    // Quiet countries stay quiet. A fragile but steady nation has no reason to fragment, and even
    // demands it ignores for years make a province restive but never rebellious on their own.
    {
        auto fragile = world("military_junta", 20);
        auto quietFrom = journal.size();
        auto idle = months(fragile, 60);
        for (const auto &id : idle.state.territories.getMemberNames())
            check(unrestOf(idle, id.c_str()) == 0,
                  "A chronically fragile but steady country stays quiet: " + id);
        check(count(quietFrom, "unrest") == 0 && count(quietFrom, "revolt") == 0,
              "Nothing is reported in a quiet fragile country");
        auto ignored = fragile;
        ignored.state.regionalPolitics["a2"] =
            parse(R"({"agenda":"Home rule","relations":"strained","supporting_events":[],)"
                  R"("demands":[{"id":"t0-a","text":"Devolve the budget","status":"pending"},)"
                  R"({"id":"t0-b","text":"Recognise the language","status":"pending"}]})");
        auto ignoredFrom = journal.size();
        auto restive = months(ignored, 60);
        int past = 0;
        for (const auto &id : restive.state.territories.getMemberNames())
            past += restive.state.territories[id]["unrest_months"].asInt();
        std::cout << "Five years of ignored demands in a steady country: unrest "
                  << unrestOf(restive, "a2") << "\n";
        check(unrestOf(restive, "a2") >= 35 && unrestOf(restive, "a2") <= 40 && past == 0 &&
                  count(ignoredFrom, "revolt") == 0,
              "Ignored demands make a province restive, never rebellious by themselves: " +
                  num(unrestOf(restive, "a2")));
        // A front does not turn a province against the nation defending it.
        auto defended = world();
        defended.state.conflicts["c0-1"] =
            parse(R"({"name":"Border War","kind":"war","status":"open","text":"War",)"
                  R"("actors":["AA","BB"],"started_turn":0,"set_intensity":"high",)"
                  R"("intensity":"high","theatre":["a2"],"affected_regions":["a2"],)"
                  R"("hostile_pairs":[{"actors":["AA","BB"],"status":"active"}],)"
                  R"("postures":{"AA":"high","BB":"high"}})");
        defended.state.territories["a2"]["displaced"] = 500000;
        auto fought = months(defended, 24);
        check(fought.state.territories["a2"]["controller"] == "AA" && unrestOf(fought, "a2") == 0,
              "War damage to a defended province is no separatism: " + num(unrestOf(fought, "a2")));
    }
}
