#include "status_tests.h"
void realWorldTests() {
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
    {
        // Bordering countries never share a map colour.
        auto coloured = world;
        std::vector<std::pair<std::string, std::string>> borders;
        for (auto &n : reference.query("SELECT a,b FROM neighbors"))
            borders.emplace_back(n["a"].asString(), n["b"].asString());
        assignActorColors(coloured, borders);
        std::set<std::string> used;
        for (auto &[a, b] : borders) {
            if (!coloured.hasActor(a) || !coloured.hasActor(b))
                continue;
            check(coloured.actors[a]["color"] != coloured.actors[b]["color"],
                  "Neighbours " + a + " and " + b + " share a colour");
        }
        for (auto &id : coloured.actors.getMemberNames()) {
            check(isHexColor(coloured.actors[id]["color"].asString()), "Palette colour");
            used.insert(coloured.actors[id]["color"].asString());
        }
        check(used.size() >= 20, "Actors spread across the palette");
        auto again = world;
        assignActorColors(again, borders);
        check(again.actors == coloured.actors, "Colours are deterministic");
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
    {
        // Supply under the plan 26 rules, on the real map: by land or across one ocean to the
        // heartland, however far, but never out of an enclave with no friendly way home.
        auto supplyWorld = world;
        supplyWorld.engine["lifecycle_version"] = 1;
        auto sea = realAdjacency;
        for (auto &r : reference.query("SELECT a,b,km FROM sea_routes"))
            sea.sea[r["a"].asString()][r["b"].asString()] = r["km"].asInt();
        for (auto &r : reference.query("SELECT region,basin FROM sea_basins ORDER BY region")) {
            sea.basins[r["region"].asString()].insert(r["basin"].asInt());
            sea.basinRegions[r["basin"].asInt()].push_back(r["region"].asString());
        }
        auto id = [&](const char *name, const char *code) {
            for (auto &t : supplyWorld.territories.getMemberNames())
                if (supplyWorld.territories[t]["name"] == name &&
                    supplyWorld.territories[t]["initial_affiliation"] == code)
                    return t;
            throw std::runtime_error(std::string("No region ") + name);
        };
        for (auto [name, code, actor] :
             std::initializer_list<std::tuple<const char *, const char *, const char *>>{
                 {"Kaliningrad", "RU", "RU"},
                 {"Hawaii", "US", "US"},
                 {"Alaska", "US", "US"},
                 {"Autonomous Republic of Crimea", "UA", "RU"},
                 {"Gilan", "IR", "IR"}})
            check(supplied(supplyWorld, sea, actor, id(name, code)),
                  std::string(name) + " is supplied");
        check(!supplied(supplyWorld, sea, "AZ", id("Nakhchivan Autonomous Republic", "AZ")),
              "Nakhchivan has no friendly way home");
    }
    // Every political system classifies, and the seeded values are the real ones.
    check(nationalStatus(world, "PT")["government"] == "Semi-presidential republic" &&
              nationalStatus(world, "KP")["government"] == "One-party state" &&
              nationalStatus(world, "SA")["government"] == "Absolute monarchy" &&
              world.actors["rapid-support-forces"]["status"]["government"] ==
                  "transitional_government",
          "Political systems are seeded, including for non-state actors");
    for (const auto &a : world.actors.getMemberNames())
        check(validGovernment(world.actors[a]["status"]["government"].asString()),
              "Every actor has a political system: " + a);

    auto actorCount = world.actors.size();
    auto conflictCount = world.conflicts.size();
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
    // Unrest must be earned. The World Bank score this world is seeded from is a percentile rank
    // of the absence of violence, not of regime fragility -- North Korea reads 71.4 and Yemen 18.9
    // -- so an absolute threshold would set the Sahel, Yemen and Somalia alight before the player
    // had done anything. Nothing here has fallen from the stability it started with, and no region
    // carries a grievance this campaign produced, so two idle years change nothing internally.
    auto idle = year;
    for (int m = 0; m < 12; ++m) {
        idle.state.turn++;
        idle = applyMonthlyStatus(idle, {}, realAdjacency);
    }
    check(idle.state.actors.size() == actorCount, "No breakaway state forms in an idle real world");
    check(idle.state.conflicts.size() == conflictCount,
          "No new conflict opens in an idle real world");
    // The one thing that does stir is occupied Ukraine: Russia's own stability slides while it
    // fights, which opens the gate a crack for the land it holds. That is the mechanic working --
    // an occupier bogged down in a long war is exactly who an occupied population turns on -- and
    // it stays far below the level anything is reported or acted on at.
    std::string worst;
    double peak = 0;
    for (const auto &id : idle.state.territories.getMemberNames())
        if (idle.state.territories[id]["unrest"].asDouble() > peak) {
            peak = idle.state.territories[id]["unrest"].asDouble();
            worst = idle.state.territories[id]["name"].asString();
        }
    std::cout << "Most restive region after two idle years: " << (worst.empty() ? "none" : worst)
              << " at " << peak << "/100\n";
    // Below rate::unrestNotice, which is 35 and is the line an unrest event is reported at. The
    // requirement is not that a world at war is numb -- occupied Kordofan and occupied Ukraine are
    // genuinely restive, and reading so is the mechanic working -- but that an idle campaign
    // surfaces nothing and acts on nothing, which the checks below pin down exactly.
    check(peak < 35, "Nothing approaches unrest in an idle real world: " + worst + " at " +
                         std::to_string(peak));
    // The calibration the whole design turns on: a country that is chronically fragile but steady
    // is at its equilibrium and stays there. These five sit between 18.9 and 28.2 on the seeded
    // scale -- below Sudan, below Myanmar -- and an absolute threshold would have set every one of
    // them alight in the first year. None of them has fallen, so none of them stirs at all.
    for (const auto *code : {"YE", "ML", "SY", "SO", "AF"})
        for (const auto &id : idle.state.territories.getMemberNames())
            if (idle.state.territories[id]["controller"] == code)
                check(idle.state.territories[id]["unrest"].asDouble() == 0,
                      std::string("A chronically fragile but steady country stays quiet: ") + code +
                          " " + id);
    for (const auto &id : idle.state.territories.getMemberNames())
        check(idle.state.territories[id]["unrest_months"].asInt() == 0,
              "No region spends a month past the revolt line: " + id);
    for (const auto &a : idle.state.actors.getMemberNames())
        check(idle.state.actors[a]["status"]["government"] ==
                  world.actors[a]["status"]["government"],
              "No government falls in an idle real world: " + a);
    for (const auto &e : idle.events)
        check(e.action != "revolt" && e.action != "secession" && e.action != "regime_change" &&
                  e.action != "unrest",
              "An idle real world reports nothing domestic: " + e.text);
    // And the war calibration plan 19 tuned is untouched by any of it.
    check(idle.state.conflicts["c0-1"]["intensity"] == "high", "The seeded wars run as before");

    // Chronicles: missing, a year old, or source-less once the source exists.
    auto chronicle = parse(R"({"summary":"Old","through_turn":2,"with_source":true})");
    check(chronicleDue(Json::Value(), 3, false) && chronicleDue(Json::objectValue, 3, true),
          "Missing chronicle is due");
    check(!chronicleDue(chronicle, 5, true) && chronicleDue(chronicle, 14, false),
          "Sourced chronicle is due after a year");
    chronicle["with_source"] = false;
    check(chronicleDue(chronicle, 3, true) && !chronicleDue(chronicle, 3, false),
          "Source-less chronicle is due once the source exists");
}
