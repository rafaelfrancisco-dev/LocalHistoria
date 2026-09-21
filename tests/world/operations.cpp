#include "world_tests.h"
void WorldTests::operations() {
    // Model operations compile into validated deltas; physically impossible events are kept as
    // failures without changes.
    WorldState m;
    m.player = "old-state";
    m.campaignId = "ops";
    m.actors = w.actors;
    m.actors["far"] = parse(R"({"name":"Far","color":"#999999"})");
    m.territories = parse(
        R"({"west":{"name":"West","owner":"old-state","controller":"old-state","longitude":1,)"
        R"("latitude":1},"east":{"name":"East","owner":"old-state",)"
        R"("controller":"old-state","longitude":1,"latitude":1},
  "march":{"name":"March","owner":"neighbor","controller":"neighbor","longitude":1,"latitude":1},)"
        R"("heart":{"name":"Heart","owner":"neighbor","controller":"neighbor","longitude":1,)"
        R"("latitude":1},
  "hills":{"name":"Hills","owner":"neighbor","controller":"neighbor","longitude":1,"latitude":1},)"
        R"("island":{"name":"Island","owner":"far","controller":"far","longitude":1,)"
        R"("latitude":1}})");
    for (auto id : {"march", "heart", "hills", "island"})
        geo[id] = g;
    adjacency = Adjacency{{"west", {"east", "march", "hills"}},
                          {"east", {"west", "heart"}},
                          {"march", {"west", "heart"}},
                          {"heart", {"march", "east"}},
                          {"hills", {"west"}},
                          {"island", {}}};
    m.turn = 1;
    opsBase = resolve(m);
    opsBase.state.turn = 1;
    facts = encode(opsBase.state);
    schema = operationSchema(opsBase.state, facts,
                             {{"old-state", "Build a port"}, {"neighbor", "Wait"}});
    auto &outcomes = schema["properties"]["events"];
    check(outcomes["minItems"].asInt() == 2 && outcomes["maxItems"].asInt() == 2 &&
              !outcomes.isMember("items") &&
              outcomes["prefixItems"][0]["properties"]["action"]["const"] == "Build a port" &&
              actionOpening("Venezuela mobilizes the National Guard in Zulia and Lara") ==
                  "Venezuela mobilizes the National Guard in…" &&
              outcomes["prefixItems"][0]["properties"]["actor"]["const"] == "old-state" &&
              outcomes["prefixItems"][1]["properties"]["actor"]["const"] == "neighbor",
          "One outcome per action");
    check(outcomes["prefixItems"][0]["properties"]["operations"]["items"]["anyOf"].size() == 10,
          "Schema");
    // Operations are defined once in $defs and referenced from every outcome, so many actions
    // keep the schema small.
    std::vector<std::pair<std::string, std::string>> many;
    for (int i = 0; i < 13; ++i)
        many.emplace_back(i % 2 ? "neighbor" : "old-state", "Action number " + std::to_string(i));
    auto large = operationSchema(opsBase.state, facts, many);
    bool referenced = true;
    for (auto &item : large["properties"]["events"]["prefixItems"])
        for (auto &o : item["properties"]["operations"]["items"]["anyOf"]) {
            auto r = o["$ref"].asString();
            referenced = referenced && o.size() == 1 && r.rfind("#/$defs/op_", 0) == 0 &&
                         large["$defs"].isMember(r.substr(8));
        }
    check(referenced && large["properties"]["events"]["prefixItems"].size() == 13 &&
              dump(large).size() < 30000,
          "Operations referenced from $defs");
    // Only finished armies and fleets can be moved; every entity can change status.
    auto garrisoned = opsBase.state;
    garrisoned.entities["army-1"] =
        parse(R"({"name":"First Army","kind":"army","actor":"old-state","region":"west",)"
              R"("status":"active","origin_event":"initial","longitude":1,"latitude":1})");
    garrisoned.entities["city-1"] =
        parse(R"({"name":"Old Town","kind":"city","actor":"old-state","region":"west",)"
              R"("status":"active","origin_event":"initial","longitude":1,"latitude":1})");
    auto garrisonSchema = operationSchema(garrisoned, encode(garrisoned), {{"old-state", "March"}});
    check(
        operation(garrisonSchema, "move_entity")["properties"]["entity"]["enum"] ==
                parse(R"(["army-1"])") &&
            operation(garrisonSchema, "set_entity_status")["properties"]["entity"]["enum"].size() ==
                2,
        "Only armies and fleets are movable");
    garrisoned.entities["army-1"]["status"] = "under_construction";
    garrisoned.entities["army-1"]["completes_turn"] = 3;
    check(operation(garrisonSchema, "move_entity").isObject() &&
              operation(operationSchema(garrisoned, encode(garrisoned), {{"old-state", "March"}}),
                        "move_entity")
                  .isNull(),
          "No move_entity without a finished unit");
    auto reachFacts =
        parse(R"({"west":{"controller":"old-state","within_reach_of":["old-state"]},)"
              R"("march":{"controller":"neighbor","within_reach_of":["old-state"]}})");
    check(attackGuidance("old-state", "Invade Neighbor.", reachFacts).find("use occupy") !=
                  std::string::npos &&
              attackGuidance("old-state", "Build a port", reachFacts).empty(),
          "Attack orders get an occupy hint");
    reachFacts["march"]["within_reach_of"] = Json::arrayValue;
    check(attackGuidance("old-state", "Invade Neighbor.", reachFacts)
                  .find("no region within reach") != std::string::npos,
          "No hint to occupy when nothing is reachable");
    auto &developments = schema["properties"]["world_events"];
    check(developments["maxItems"].asInt() == 2 &&
              !developments["items"]["properties"].isMember("operations") &&
              developments["items"]["properties"]["actor"]["enum"] ==
                  parse(R"(["far","neighbor"])"),
          "World events are narrative and never the player's decisions");
    split2 = compileOperations(
        opsBase.state,
        parse(R"({"events":[{"action":"Build a port","actor":"old-state","blocked_by":"none",)"
              R"("causes":[],"description":"Port begun","operations":[]}],)"
              R"("world_events":[{"actor":"neighbor","blocked_by":"none","causes":[],)"
              R"("description":"Harvest festival","operations":[],)"
              R"("effects":[{"actor":"neighbor","economy":"growth","stability":"none"}]},)"
              R"({"actor":"far","causes":[],"description":"Markets steady"}]})"),
        {});
    auto invented = compileOperations(
        opsBase.state,
        parse(R"({"events":[],"world_events":[{"actor":"old-state","causes":[],)"
              R"("description":"Old builds a fortress"},{"actor":"neighbor","causes":[],)"
              R"("description":"Neighbor takes West","operations":[{"action":"occupy",)"
              R"("region":"west","by":"neighbor"}]}]})"),
        {});
    check(invented.events.empty() && invented.changes.empty(),
          "Invented player decisions and map changes are dropped");
    check(split2.events.size() == 2 && !split2.events[0]["background"].asBool() &&
              split2.events[1]["background"].asBool(),
          "Outcomes then world events");
    ops = parse(
        R"({"events":[{"description":"West Secedes!","actor":"old-state","blocked_by":"none",)"
        R"("background":false,"causes":[],"operations":[
  {"action":"create_actor","actor_id":"west-rep","name":"West Republic","color":"#00aa00"},)"
        R"({"action":"transfer_territory","region":"west","to":"west-rep"},
  {"action":"build","kind":"port","name":"Harbor","actor":"old-state","region":"east"},)"
        R"({"action":"build","kind":"port","name":"Harbor","actor":"old-state","region":"east"},
  {"action":"open_conflict","actors":["old-state","west-rep"],"text":"Dispute"}]},)"
        R"({"description":"West secedes","actor":"neighbor","blocked_by":"none",)"
        R"("background":true,)"
        R"("effects":[{"actor":"neighbor","economy":"none","stability":"decline"}],)"
        R"("causes":[],)"
        R"("operations":[{"action":"occupy","region":"west","by":"neighbor"}]}]})");
    compiled = compileOperations(opsBase.state, ops, adjacency);
    check(compiled.events[0]["id"] == "t1-west-secedes" &&
              compiled.events[1]["id"] == "t1-west-secedes-2",
          "Event IDs");
    auto seceded = applyDelta(opsBase, compiled, geo);
    check(seceded.state.territories["west"]["owner"] == "west-republic" &&
              seceded.state.territories["west"]["controller"] == "neighbor",
          "Secession and bordering occupation");
    check(seceded.state.entities["e1-harbor"]["origin_event"] == "t1-west-secedes" &&
              seceded.state.entities.size() == 1 && seceded.state.conflicts.size() == 1,
          "Deduplicated construction");
    check(seceded.state.entities["e1-harbor"]["status"] == "under_construction" &&
              seceded.state.entities["e1-harbor"]["completes_turn"] ==
                  1 + constructionMonths("port"),
          "Construction takes months");
    auto later = resolve(seceded.state);
    for (int n = 1; n < constructionMonths("port"); ++n)
        later = resolve(later.state);
    check(later.state.entities["e1-harbor"]["status"] == "active" && later.events.size() == 1,
          "Construction completes");
    // An impossible operation is dropped with its reason kept for the audit; the event text
    // never mentions it.
    // Out of reach: Heart borders no land the aggressor controls.
    auto far = compile(
        R"({"events":[{"description":"Deep strike","actor":"old-state","blocked_by":"none",)"
        R"("background":false,"causes":[],"operations":[{"action":"occupy",)"
        R"("region":"march","by":"old-state"},{"action":"occupy","region":"island",)"
        R"("by":"old-state"}]}]})");
    check(infeasible(far) && far.events[0]["text"] == "Deep strike" &&
              !far.events[0].isMember("blocked_by") && changed(far, "territory", "march") &&
              !changed(far, "territory", "island"),
          "Unreachable region dropped, the rest of its event holds");
    check(far.events[0]["partial"] == true && !far.events[0].isMember("failed") &&
              droppedClause(far.events[0]) == " (In fact: not every region changed hands.)",
          ("A partly dropped event is partial: " + dump(far.events[0])).c_str());
    auto blitz = compile(
        R"({"events":[{"description":"Blitz","actor":"old-state","blocked_by":"none",)"
        R"("background":false,"causes":[],"operations":[{"action":"occupy",)"
        R"("region":"march","by":"old-state"},{"action":"open_conflict",)"
        R"("actors":["old-state","neighbor"],"text":"War"}]},
  {"description":"Second front","actor":"old-state","blocked_by":"none","background":false,)"
        R"("causes":[],"operations":[{"action":"occupy","region":"hills","by":"old-state"}]},
  {"description":"Third front","actor":"old-state","blocked_by":"none","background":false,)"
        R"("causes":[],"operations":[{"action":"occupy","region":"heart",)"
        R"("by":"old-state"}]}]})");
    check(!infeasible(blitz, 0) && !infeasible(blitz, 1) && infeasible(blitz, 2),
          "At most two regions taken per month");
    war = applyDelta(opsBase, blitz, geo);
    check(war.state.territories["march"]["controller"] == "old-state" &&
              war.state.territories["march"]["owner"] == "neighbor" &&
              war.state.conflicts.size() == 1,
          "Occupation keeps ownership");
    auto annexNow =
        compile(R"({"events":[{"description":"Annex","actor":"old-state","blocked_by":"none",)"
                R"("background":false,"causes":[],"operations":[{"action":"transfer_territory",)"
                R"("region":"march","to":"old-state"}]}]})");
    check(infeasible(annexNow) && annexNow.changes.empty(), "Annexation needs prior occupation");
    check(annexNow.events[0]["failed"] == true && !annexNow.events[0].isMember("partial"),
          "An event with every operation dropped failed");
    auto nextMonth = resolve(war.state);
    auto annexLater = compileOperations(
        nextMonth.state,
        parse(R"({"events":[{"description":"Annex","actor":"old-state","blocked_by":"none",)"
              R"("background":false,"causes":[],)"
              R"("operations":[{"action":"transfer_territory","region":"march",)"
              R"("to":"old-state"},{"action":"occupy","region":"heart",)"
              R"("by":"old-state"}]}]})"),
        adjacency);
    auto annexed = applyDelta(nextMonth, annexLater, geo);
    check(annexed.state.territories["march"]["owner"] == "old-state" &&
              annexed.state.territories["heart"]["controller"] == "old-state",
          "Annexation after a month of occupation, then further advance");
    auto grab =
        compile(R"({"events":[{"description":"Grab","actor":"neighbor","blocked_by":"none",)"
                R"("background":false,"causes":[],"operations":[{"action":"transfer_territory",)"
                R"("region":"west","to":"neighbor"}]}]})");
    check(infeasible(grab), "Cannot take land by declaration");
    // A dropped operation leaves an earlier operation's change to the same region untouched.
    auto overreach =
        compile(R"({"events":[{"description":"Overreach","actor":"neighbor","blocked_by":"none",)"
                R"("background":false,"causes":[],"operations":[{"action":"occupy",)"
                R"("region":"west","by":"neighbor"},{"action":"transfer_territory",)"
                R"("region":"west","to":"neighbor"}]}]})");
    check(overreach.changes.size() == 1 && overreach.changes[0]["id"] == "west" &&
              overreach.changes[0]["after"]["controller"] == "neighbor" &&
              overreach.changes[0]["after"]["owner"] == "old-state" &&
              overreach.changes[0]["rationale"].asString().find("transfer_territory") ==
                  std::string::npos &&
              overreach.events[0]["partial"] == true,
          "Rollback restores a change touched earlier in the event");
}
