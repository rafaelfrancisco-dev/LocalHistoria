#include "world_tests.h"
void WorldTests::movement() {
    // Armies advance one region per month; unfinished units cannot move.
    auto army = parse(R"({"name":"First Army","kind":"army","actor":"old-state","region":"west",)"
                      R"("status":"active","origin_event":"initial","longitude":1,"latitude":1})");
    auto armed = opsBase;
    armed.state.entities["army-1"] = army;
    auto march = [&](const char *region) {
        return compileOperations(
            armed.state,
            parse(std::string(R"({"events":[{"description":"March","actor":"old-state",)"
                              R"("blocked_by":"none","background":false,"causes":[],)"
                              R"("operations":[{"action":"move_entity","entity":"army-1",)"
                              R"("region":")") +
                  region + R"("}]}]})"),
            adjacency);
    };
    check(!infeasible(march("march")) && infeasible(march("heart")),
          "Army moves one region per month");
    auto seized = compileOperations(
        armed.state,
        parse(R"({"events":[{"description":"Neighbor mobilizes","actor":"neighbor",)"
              R"("blocked_by":"none","background":false,"causes":[],)"
              R"("effects":[{"actor":"neighbor","economy":"decline","stability":"decline"}],)"
              R"("operations":[{"action":"open_conflict","actors":["neighbor","old-state"],)"
              R"("kind":"war","name":"West War","text":"War"},{"action":"move_entity",)"
              R"("entity":"army-1","region":"march"}]}]})"),
        adjacency);
    check(infeasible(seized) && seized.events[0]["text"] == "Neighbor mobilizes" &&
              seized.events[0]["dropped_operations"][0]["action"] == "move_entity" &&
              changed(seized, "conflict", "c1-1") && !changed(seized, "entity", "army-1") &&
              seized.events[0]["effects"].size() == 2,
          "Valid operations survive a foreign unit order");
    check(effectOf(seized, "old-state")["economy"] == "decline" &&
              effectOf(seized, "old-state")["stability"] == "decline",
          "The attacked side suffers even when the model left it out");
    auto destroy = [&](const WorldState &state, const char *first) {
        return compileOperations(
            state,
            parse(std::string(R"({"events":[{"description":"Strike","actor":"neighbor",)"
                              R"("blocked_by":"none","background":false,"causes":[],)"
                              R"("operations":[)") +
                  first +
                  R"({"action":"set_entity_status","entity":"army-1","status":"destroyed"}]}]})"),
            adjacency);
    };
    auto atWar = armed.state;
    atWar.conflicts["c9"] = parse(
        R"({"actors":["neighbor","old-state"],"status":"open","text":"War","name":"West War",)"
        R"("kind":"war"})");
    check(infeasible(destroy(armed.state, "")) && !infeasible(destroy(atWar, "")) &&
              changed(destroy(atWar, ""), "entity", "army-1") &&
              !infeasible(destroy(
                  armed.state,
                  R"({"action":"open_conflict","actors":["neighbor","old-state"],"kind":"war",)"
                  R"("name":"West War","text":"War"},)")),
          "Destroying a foreign unit needs a war, declared before or in the same event");
    armed.state.entities["army-1"]["status"] = "under_construction";
    armed.state.entities["army-1"]["completes_turn"] = 3;
    check(infeasible(march("march")), "Unfinished army cannot move");
    auto rush = compileOperations(
        armed.state,
        parse(R"({"events":[{"description":"Rush","actor":"old-state","blocked_by":"none",)"
              R"("background":false,"causes":[],"operations":[{"action":"set_entity_status",)"
              R"("entity":"army-1","status":"active"}]}]})"),
        adjacency);
    check(infeasible(rush), "Construction cannot be completed by decree");
    auto foreignBuild =
        compile(R"({"events":[{"description":"Base","actor":"old-state","blocked_by":"none",)"
                R"("background":false,"causes":[],"operations":[{"action":"build","kind":"base",)"
                R"("name":"Forward base","actor":"old-state","region":"march"}]}]})");
    check(infeasible(foreignBuild), "Building requires control");
    auto twoStates = compile(
        R"({"events":[{"description":"Split","actor":"old-state","blocked_by":"none",)"
        R"("background":false,"causes":[],"operations":[{"action":"create_actor",)"
        R"("actor_id":"a","name":"Alpha","color":"#00aa00"},)"
        R"({"action":"transfer_territory","region":"west","to":"Alpha"}]},
  {"description":"Split again","actor":"old-state","blocked_by":"none","background":false,)"
        R"("causes":[],"operations":[{"action":"create_actor","actor_id":"b","name":"Beta",)"
        R"("color":"#00aa00"},{"action":"transfer_territory","region":"east",)"
        R"("to":"Beta"}]}]})");
    check(!infeasible(twoStates, 0) && infeasible(twoStates, 1) &&
              applyDelta(opsBase, twoStates, geo).state.territories["west"]["owner"] == "alpha",
          "One new state per month, referenced by name");
    auto regionNamed =
        compile(R"({"events":[{"description":"Free West","actor":"old-state","blocked_by":"none",)"
                R"("background":false,"causes":[],"operations":[{"action":"create_actor",)"
                R"("actor_id":"wr","name":"West Republic","color":"#00aa00"},)"
                R"({"action":"transfer_territory","region":"west","to":"West"}]}]})");
    check(applyDelta(opsBase, regionNamed, geo).state.territories["west"]["owner"] ==
              "west-republic",
          "Recipient named after its region resolves to the new state");
    orphan =
        compile(R"({"events":[{"description":"Orphan","actor":"old-state","blocked_by":"none",)"
                R"("background":false,"causes":[],"operations":[{"action":"create_actor",)"
                R"("actor_id":"z","name":"Zed","color":"#00aa00"}]}]})");
    check(infeasible(orphan) && orphan.events[0]["blocked_by"] == "infeasible" &&
              orphan.changes.empty() && applyDelta(opsBase, orphan, geo).events.empty(),
          "New state needs land; the rolled-back event stays out of the journal");
    reform = compile(R"({"events":[{"description":"Education reform begins","actor":"old-state",)"
                     R"("blocked_by":"none","background":false,"causes":[],"operations":[]}]})");
    check(reform.changes.empty() && reform.events.size() == 1 &&
              !reform.events[0].isMember("blocked_by") &&
              applyDelta(opsBase, reform, geo).events.size() == 1,
          "Narrative-only event");
    check(!reform.events[0].isMember("partial") && !reform.events[0].isMember("failed") &&
              droppedClause(reform.events[0]).empty(),
          "A narrative event is neither partial nor failed");
    treaty = opsBase;
    treaty.state.commitments["treaty-9"] =
        parse(R"({"actors":["old-state","neighbor"],"status":"open","text":"No new states"})");
    auto refused = ops;
    refused["events"][0]["blocked_by"] = "treaty-9";
    refused["events"][0]["operations"] = Json::arrayValue;
    auto denied = compileOperations(treaty.state, refused, adjacency);
    check(denied.events[0]["blocked_by"] == "treaty-9" && denied.changes.size() == 1,
          "Commitment block");
    auto broken = ops;
    broken["events"][0]["blocked_by"] = "treaty-9";
    auto breach = compileOperations(treaty.state, broken, adjacency);
    check(!breach.events[0].isMember("blocked_by") && breach.events[0]["breaches"] == "treaty-9" &&
              breach.events[0]["text"].asString().find("No new states") != std::string::npos &&
              breach.changes.size() == compiled.changes.size(),
          "Treaty breach recorded");
    auto outsider = compileOperations(
        treaty.state,
        parse(R"({"events":[{"description":"Far builds","actor":"far","blocked_by":"treaty-9",)"
              R"("background":false,"causes":[],"operations":[{"action":"build",)"
              R"("kind":"port","name":"Far port","actor":"far","region":"island"}]}]})"),
        adjacency);
    check(!outsider.events[0].isMember("blocked_by") && !outsider.events[0].isMember("breaches") &&
              outsider.events[0]["text"] == "Far builds",
          "A treaty binds only its parties");
    auto futile =
        compileOperations(treaty.state,
                          parse(R"({"events":[{"description":"Futile grab","actor":"neighbor",)"
                                R"("blocked_by":"treaty-9","background":false,"causes":[],)"
                                R"("operations":[{"action":"transfer_territory","region":"west",)"
                                R"("to":"neighbor"}]}]})"),
                          adjacency);
    check(!futile.events[0].isMember("breaches") && futile.events[0]["text"] == "Futile grab",
          "No breach when every operation was dropped");
    auto binding = operationSchema(
        treaty.state, encode(treaty.state),
        {{"far", "Wait"}, {"neighbor", "Wait"}})["properties"]["events"]["prefixItems"];
    check(binding[0]["properties"]["blocked_by"]["enum"].size() == 1 &&
              binding[1]["properties"]["blocked_by"]["enum"][1] == "treaty-9",
          "Only an actor's own commitments can block it");
    rejects([&] {
        compile(R"({"events":[{"description":"Ghost","actor":"nobody","blocked_by":"none",)"
                R"("background":false,"causes":[],"operations":[]}]})");
    });
    auto noisy = parse(R"({"events":[]})");
    for (int n = 0; n < 5; ++n)
        noisy["events"].append(
            parse(R"({"description":"Rumor","actor":"neighbor","blocked_by":"none",)"
                  R"("background":true,"causes":[],"operations":[],)"
                  R"("effects":[{"actor":"neighbor","economy":"none",)"
                  R"("stability":"severe_decline"}]})"));
    check(compileOperations(opsBase.state, noisy, adjacency).events.size() == 3,
          "Background events capped");
}
