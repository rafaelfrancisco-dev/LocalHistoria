#include "localhistoria/status.h"
#include "localhistoria/store.h"
#include <iostream>
using namespace lh;
static void check(bool b, const std::string &what) {
    if (!b)
        throw std::runtime_error(what);
}
static Json::Int64 people(const WorldState &w) {
    Json::Int64 n = 0;
    for (auto &t : w.territories)
        n += t["population"].asInt64();
    return n;
}
static WorldDelta events(const char *json) {
    WorldDelta d;
    d.events = parse(json);
    return d;
}
int main() {
    try {
        // A small world: two neighbors at war across a border, a bystander beyond.
        WorldState w;
        w.player = "AA";
        w.campaignId = "status";
        w.actors = parse(
            R"({"AA":{"name":"Aland","color":"#111111"},"BB":{"name":"Bland","color":"#222222"},)"
            R"("CC":{"name":"Cland","color":"#333333"}})");
        w.territories = parse(R"({
          "a1":{"name":"A Capital","owner":"AA","controller":"AA","initial_affiliation":"AA",)"
                              R"("kind":"ADM1"},
          "a2":{"name":"A Border","owner":"AA","controller":"AA","initial_affiliation":"AA",)"
                              R"("kind":"ADM1"},
          "b1":{"name":"B Border","owner":"BB","controller":"BB","initial_affiliation":"BB",)"
                              R"("kind":"ADM1"},
          "b2":{"name":"B Capital","owner":"BB","controller":"BB","initial_affiliation":"BB",)"
                              R"("kind":"ADM1"},
          "c1":{"name":"C Hills","owner":"CC","controller":"CC","initial_affiliation":"CC",)"
                              R"("kind":"ADM1"}})");
        Adjacency adjacency{{"a1", {"a2"}},
                            {"a2", {"a1", "b1", "c1"}},
                            {"b1", {"a2", "b2"}},
                            {"b2", {"b1"}},
                            {"c1", {"a2"}}};
        StatusSeed seed;
        seed.countries =
            parse(R"({
          "AA":{"population":10000000,"gdp_usd":200000000000,"gdp_growth":2.4,"pop_growth":1.2,)"
                  R"("stability":70,"currency":"EUR","symbol":"€","usd_rate":0.9,"estimated":0},
          "BB":{"population":40000000,"gdp_usd":400000000000,"gdp_growth":1.2,"pop_growth":0,)"
                  R"("stability":50,"currency":"USD","symbol":"US$","usd_rate":1,"estimated":0},
          "CC":{"population":1000000,"gdp_usd":10000000000,"gdp_growth":0,"pop_growth":0,)"
                  R"("stability":80,"currency":"USD","symbol":"US$","usd_rate":1,"estimated":1}})");
        seed.regions = parse(
            R"({"a1":{"population":6000000,"gdp_usd":150000000000},"a2":{"population":4000000,)"
            R"("gdp_usd":50000000000},
          "b1":{"population":10000000,"gdp_usd":100000000000},"b2":{"population":30000000,)"
            R"("gdp_usd":300000000000},"c1":{"population":1000000,"gdp_usd":10000000000}})");
        seedStatus(w, seed, false);
        check(hasStatus(w), "Seeded status");
        auto status = nationalStatus(w, "AA");
        check(status["population"].asInt64() == 10000000 &&
                  status["gdp_usd"].asInt64() == 200000000000,
              "National totals sum regions");
        check(status["gdp"] == "€180.0 billion (US$200.0 billion)",
              "Local currency first: " + status["gdp"].asString());
        check(status["stability"] == "very stable (70/100)",
              "Stability label: " + status["stability"].asString());
        check(nationalStatus(w, "BB")["gdp"] == "US$400.0 billion",
              "Dollar economies show one amount");

        // Baseline: twelve peaceful months follow the annual rates.
        auto peace = Resolution{w, {}};
        for (int m = 0; m < 12; ++m) {
            peace.state.turn++;
            peace = applyMonthlyStatus(peace, {}, adjacency);
        }
        auto grown = nationalStatus(peace.state, "AA");
        check(std::abs(double(grown["population"].asInt64()) / 10000000 - 1.012) < 0.0005,
              "Annual population growth");
        check(std::abs(double(grown["gdp_usd"].asInt64()) / 200000000000 - 1.024) < 0.001,
              "Annual GDP growth");
        check(peace.events.empty() &&
                  std::abs(peace.state.actors["AA"]["status"]["stability"].asDouble() - 70) < 0.05,
              "Peace is uneventful");

        // Names: specific names survive, generic ones get a historian's fallback, repeats are
        // numbered.
        std::set<std::string> taken;
        check(realisticName(w, "conflict", "war", "Hill War", {"AA", "BB"}, "text", taken) ==
                  "Hill War",
              "Specific name kept");
        check(realisticName(w, "conflict", "war", "The War", {"AA", "BB"}, "text", taken) ==
                  "Aland–Bland War",
              "Generic war renamed");
        check(realisticName(w, "conflict", "civil_war", "", {"AA", "BB"}, "text", taken) ==
                  "Civil War in Aland",
              "Civil war fallback");
        check(realisticName(w, "commitment", "defence_pact", "Defence Pact", {"AA", "CC"}, "text",
                            taken) == "Aland–Cland Defence Pact",
              "Generic pact renamed");
        taken.insert("aland–bland war");
        taken.insert("second aland–bland war");
        check(realisticName(w, "conflict", "war", "treaty", {"AA", "BB"}, "text", taken) ==
                  "Third Aland–Bland War",
              "Repeats numbered");
        w.conflicts["legacy"] =
            parse(R"({"actors":["AA","CC"],"status":"resolved","text":"An old quarrel"})");
        check(displayName(w, "conflict", "legacy") == "Aland–Cland War",
              "Legacy entries get a display name");
        w.conflicts.removeMember("legacy");

        // War: a medium-intensity war with a shared border.
        w.conflicts["c1-1"] =
            parse(R"({"name":"Border War of the Hills","kind":"war","actors":["AA","BB"],)"
                  R"("status":"open","text":"War","started_turn":0,"set_intensity":"medium",)"
                  R"("intensity":"medium","affected_regions":[],"casualties":0,"displaced":0})");
        auto start = Resolution{w, {}};
        start.state.turn = 1;
        auto described = describeAgreement(start.state, "conflict", "c1-1");
        check(described["since"] == "January 2026" && !described.isMember("months_in_force"),
              "Agreements give a start date: " + dump(described));
        check(nationalStatus(start.state, "AA")["wars"][0]["since"] == "January 2026",
              "Wars give a start date");
        // No demographic trend, so only the war changes population.
        start.state.actors["AA"]["status"]["pop_growth"] = 0.0;
        auto total = people(start.state);
        auto month = applyMonthlyStatus(start, {}, adjacency);
        auto &war = month.state.conflicts["c1-1"];
        check(war["intensity"] == "medium" && war["affected_regions"] == parse(R"(["a2","b1"])"),
              "Front is the shared border: " + dump(war));
        auto deaths = war["casualties"].asInt64(), displaced = war["displaced"].asInt64();
        check(deaths > 0 && displaced > 0 && month.events.size() == 1 &&
                  month.events[0].action == "war_toll",
              "War has a toll");
        check(people(month.state) == total - deaths, "Population is conserved except for deaths");
        check(month.state.territories["a1"]["population"].asInt64() >
                  start.state.territories["a1"]["population"].asInt64(),
              "Internal displacement reaches the interior");
        check(month.state.territories["c1"]["population"].asInt64() >
                  start.state.territories["c1"]["population"].asInt64(),
              "Refugees cross into the bystander");
        check(month.state.territories["a2"]["displaced"].asInt64() > 0,
              "Displacement is tracked at its origin");
        // Military deaths: about 400 per party per front region a month at medium intensity
        // (±30%), capped by population.
        check(deaths > 100 && deaths < 2500,
              "Plausible medium-intensity deaths: " + std::to_string(deaths));
        check(month.state.actors["AA"]["status"]["stability"].asDouble() < 70,
              "War lowers stability");
        check(encode(applyMonthlyStatus(start, {}, adjacency).state) == encode(month.state),
              "Deterministic");
        check(encode(decode(parse(dump(encode(month.state))))) == encode(month.state),
              "Round-trips through saved JSON");

        // Fighting this month (control changes between the parties) raises intensity to high,
        // whatever was declared.
        WorldDelta assault;
        auto seizedAfter = start.state.territories["b1"];
        seizedAfter["controller"] = "AA";
        Json::Value change;
        change["kind"] = "territory";
        change["id"] = "b1";
        change["before"] = start.state.territories["b1"];
        change["after"] = seizedAfter;
        change["event"] = "t1-x";
        change["rationale"] = "x";
        assault.changes.append(change);
        auto seized = start;
        seized.state.territories["b1"]["controller"] = "AA";
        auto fought = applyMonthlyStatus(seized, assault, adjacency);
        check(fought.state.conflicts["c1-1"]["intensity"] == "high", "Fighting is high intensity");
        check(fought.state.conflicts["c1-1"]["casualties"].asInt64() > deaths,
              "High intensity kills more");
        check(fought.state.territories["b1"]["displaced"].asInt64() >
                  Json::Int64(0.08 * 10000000 * 0.9),
              "Seized regions empty out");
        check(fought.state.territories["b1"].isMember("occupation_gdp"), "Occupation is tracked");

        // Kind matters: a border conflict is far less deadly than a war.
        auto skirmish = start;
        skirmish.state.conflicts["c1-1"]["kind"] = "border_conflict";
        check(applyMonthlyStatus(skirmish, {}, adjacency)
                      .state.conflicts["c1-1"]["casualties"]
                      .asInt64() < deaths,
              "Border conflicts are smaller");

        // Deaths follow the front, not the country's size: a static border clash with a 1.4 bn
        // state stays small, and a declared-high conflict only a month old stays medium until
        // ground changes hands.
        auto giant = skirmish;
        giant.state.conflicts["c1-1"]["set_intensity"] = "high";
        giant.state.territories["a1"]["population"] = Json::Int64(1400000000);
        auto standoff = applyMonthlyStatus(giant, {}, adjacency).state.conflicts["c1-1"];
        check(standoff["intensity"] == "medium" &&
                  standoff["last_month_casualties"].asInt64() < 5000,
              "Large states' border clashes stay small: " + dump(standoff));
        auto giantSeized = giant;
        giantSeized.state.territories["b1"]["controller"] = "AA";
        auto clash = applyMonthlyStatus(giantSeized, assault, adjacency).state.conflicts["c1-1"];
        check(clash["intensity"] == "high" && clash["last_month_casualties"].asInt64() >
                                                  standoff["last_month_casualties"].asInt64(),
              "Fighting escalates a new border conflict: " + dump(clash));

        // A ceasefire stops the fighting; people then drift home.
        auto truce = month;
        truce.state.commitments["k2-1"] = parse(
            R"({"name":"Treaty of Hills","kind":"ceasefire","actors":["AA","BB"],"status":"open",)"
            R"("text":"Ceasefire"})");
        truce.state.turn++;
        auto waiting = truce.state.territories["a2"]["displaced"].asInt64();
        auto calm = applyMonthlyStatus(truce, {}, adjacency);
        check(calm.state.conflicts["c1-1"]["intensity"] == "none" &&
                  calm.state.conflicts["c1-1"]["affected_regions"].empty() &&
                  calm.events.size() == truce.events.size(),
              "Ceasefire halts the war");
        check(calm.state.territories["a2"]["displaced"].asInt64() < waiting,
              "Displaced people return");

        // A distant war with no front costs nothing but morale.
        auto distant = start;
        distant.state.conflicts["c1-1"]["actors"] = parse(R"(["BB","CC"])");
        auto quiet = applyMonthlyStatus(distant, {}, adjacency);
        check(quiet.state.conflicts["c1-1"]["intensity"] == "low" &&
                  quiet.state.conflicts["c1-1"]["casualties"] == 0,
              "No front, no casualties");

        // Model effects are qualitative and capped.
        auto boom = start;
        boom.state.conflicts = Json::objectValue;
        auto effects =
            events(R"([{"id":"t1-a","effects":[{"actor":"AA","economy":"strong_growth",)"
                   R"("stability":"strong_growth"},{"actor":"AA","economy":"strong_growth",)"
                   R"("stability":"strong_growth"},{"actor":"AA","economy":"strong_growth",)"
                   R"("stability":"strong_growth"},{"actor":"AA","economy":"strong_growth",)"
                   R"("stability":"strong_growth"}]}])");
        auto boomed = applyMonthlyStatus(boom, effects, adjacency);
        auto change2 = boomed.state.actors["AA"]["status"]["last_gdp_change"].asDouble();
        check(change2 > 1.5 && change2 < 1.8, "GDP effects are capped: " + std::to_string(change2));
        check(boomed.state.actors["AA"]["status"]["stability"].asDouble() <= 78.5,
              "Stability effects are capped");
        check(boomed.state.actors["AA"]["status"]["baseline_stability"].asDouble() > 70,
              "Effects leave a lasting trace");

        // Occupation drains output to a floor, never below.
        auto occupied = start;
        occupied.state.conflicts = Json::objectValue;
        occupied.state.territories["a2"]["controller"] = "BB";
        for (int m = 0; m < 120; ++m) {
            occupied.state.turn++;
            occupied = applyMonthlyStatus(occupied, {}, adjacency);
        }
        auto &drained = occupied.state.territories["a2"];
        check(drained["gdp_usd"].asInt64() < 0.5 * 50000000000 &&
                  drained["gdp_usd"].asInt64() >= 0.39 * 50000000000,
              "Occupation floor: " + drained["gdp_usd"].asString());
        check(occupied.state.actors["AA"]["status"]["stability"].asDouble() < 70,
              "Occupation weighs on stability");

        // New states inherit their parent's character; landless actors are left alone.
        auto split = start;
        split.state.conflicts = Json::objectValue;
        split.state.actors["new-state"] = parse(R"({"name":"New State","color":"#444444"})");
        split.state.actors["rebels"] = parse(R"({"name":"Rebels","color":"#555555"})");
        split.state.territories["a2"]["owner"] = split.state.territories["a2"]["controller"] =
            "new-state";
        auto founded = applyMonthlyStatus(split, {}, adjacency);
        check(founded.state.actors["new-state"]["status"]["currency"] == "EUR" &&
                  founded.state.actors["new-state"]["status"]["stability"].asDouble() <= 45,
              "New state status");
        check(!founded.state.actors["rebels"].isMember("status") &&
                  !founded.state.actors.isMember(""),
              "No phantom actors or statuses");

        // The real January 2026 world, from the imported reference database.
        Store reference(std::string(SOURCE_DIR) + "/data/world.sqlite", true);
        StatusSeed real;
        for (auto &r : reference.query("SELECT * FROM country_stats"))
            real.countries[r["code"].asString()] = r;
        for (auto &r : reference.query("SELECT * FROM region_stats"))
            real.regions[r["id"].asString()] = r;
        for (auto &r : reference.query("SELECT * FROM military_stats"))
            real.military[r["code"].asString()] = r;
        real.world =
            parse(reference.query("SELECT document FROM world_seed")[0]["document"].asString());
        WorldState world;
        world.player = "PT";
        world.campaignId = "real";
        for (auto &c : reference.query("SELECT code,name FROM countries")) {
            world.actors[c["code"].asString()]["name"] = c["name"];
            world.actors[c["code"].asString()]["color"] = "#777777";
        }
        for (auto &r : reference.query("SELECT id,name,affiliation,kind FROM regions")) {
            auto &t = world.territories[r["id"].asString()];
            t["name"] = r["name"];
            t["initial_affiliation"] = t["owner"] = t["controller"] = r["affiliation"];
            t["kind"] = r["kind"];
        }
        Adjacency realAdjacency;
        for (auto &r : reference.query("SELECT a,b FROM region_neighbors"))
            realAdjacency[r["a"].asString()].insert(r["b"].asString());
        seedStatus(world, real, true);
        auto pt = nationalStatus(world, "PT");
        check(pt["population"].asInt64() > 10000000 && pt["population"].asInt64() < 11500000 &&
                  pt["gdp"].asString().rfind("€", 0) == 0,
              "Portugal: " + dump(pt));
        check(world.conflicts.size() == 4 && world.commitments.size() >= 10 &&
                  world.actors.isMember("rapid-support-forces"),
              "Seeded wars and treaties");
        auto ua = nationalStatus(world, "UA");
        check(ua["occupied_regions"].asInt() == 3 && ua["wars"][0]["name"] == "Russo-Ukrainian War",
              "Ukraine: " + dump(ua));
        auto year = Resolution{world, {}};
        Json::Int64 killed = 0;
        for (int m = 0; m < 12; ++m) {
            year.state.turn++;
            year = applyMonthlyStatus(year, {}, realAdjacency);
            auto &ru = year.state.conflicts["c0-1"];
            killed += ru["last_month_casualties"].asInt64();
            if (m == 0)
                std::cout << "Month 1: " << year.events[0].text << "\n";
        }
        auto &ruWar = year.state.conflicts["c0-1"];
        std::cout << "Russo-Ukrainian War after 12 months: " << ruWar["casualties"].asInt64()
                  << " killed, " << ruWar["displaced"].asInt64() << " displaced; Ukraine "
                  << dump(nationalStatus(year.state, "UA")) << "\nRussia "
                  << dump(nationalStatus(year.state, "RU")) << "\nSudan "
                  << dump(nationalStatus(year.state, "SD")) << "\n";
        // Order of magnitude of 2023–25: roughly 100k–250k deaths a year, displacement in the
        // hundreds of thousands to millions.
        check(ruWar["intensity"] == "high" && killed > 80000 && killed < 300000,
              "Russo-Ukrainian War toll");
        check(ruWar["displaced"].asInt64() > 100000 && ruWar["displaced"].asInt64() < 5000000,
              "Russo-Ukrainian War displacement");
        // Chronicles: missing, a year old, or source-less once the source exists.
        auto chronicle = parse(R"({"summary":"Old","through_turn":2,"with_source":true})");
        check(chronicleDue(Json::Value(), 3, false) && chronicleDue(Json::objectValue, 3, true),
              "Missing chronicle is due");
        check(!chronicleDue(chronicle, 5, true) && chronicleDue(chronicle, 14, false),
              "Sourced chronicle is due after a year");
        chronicle["with_source"] = false;
        check(chronicleDue(chronicle, 3, true) && !chronicleDue(chronicle, 3, false),
              "Source-less chronicle is due once the source exists");
        std::cout
            << "National status, consequences, naming, effects and the January 2026 world passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
