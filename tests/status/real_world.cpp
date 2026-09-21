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
}
