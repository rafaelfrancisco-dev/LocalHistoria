#include "localhistoria/status.h"
#include "world_tests.h"
void WorldTests::treaties() {
    // Treaty terms take effect on signing; treaties expire, can be amended, and are broken by
    // acting against a partner.
    auto atPeace = resolve(namedWorld.state);
    atPeace.state.conflicts["c1-1"]["static_months"] = 8;
    auto peaceJson =
        R"({"events":[{"description":"Old makes peace","actor":"old-state",)"
        R"("blocked_by":"none","background":false,"causes":[],)"
        R"("operations":[{"action":"add_commitment","actors":["old-state","neighbor"],)"
        R"("kind":"peace_treaty","months":12,"name":"Treaty of Heart",)"
        R"("text":"Peace; Hills to Old; Old garrisons March",)"
        R"("terms":[{"action":"end_conflict","conflict":"c1-1"},{"action":"cede",)"
        R"("region":"hills","to":"old-state"},{"action":"occupy","region":"march",)"
        R"("by":"old-state"},{"action":"cede","region":"island","to":"old-state"}]}]},
  {"description":"Neighbor signs with Old","actor":"neighbor","blocked_by":"none",)"
        R"("background":false,"causes":[],"operations":[]}]})";
    auto peace = compileOperations(atPeace.state, parse(peaceJson), adjacency);
    auto peaceWorld2 = applyDelta(atPeace, peace, geo);
    auto &treatyNow = peaceWorld2.state.commitments["k2-1"];
    check(peaceWorld2.state.conflicts["c1-1"]["status"] == "resolved" &&
              peaceWorld2.state.territories["hills"]["owner"] == "old-state" &&
              peaceWorld2.state.territories["march"]["controller"] == "old-state" &&
              peaceWorld2.state.territories["march"]["owner"] == "neighbor" &&
              treatyNow["terms"].size() == 3 && treatyNow["expires_turn"] == 14 &&
              peace.events[0]["dropped_operations"][0]["action"] == "term cede" &&
              peace.events[0]["major"].asBool(),
          ("Treaty terms take effect on signing: " + dump(peace.events[0]) + dump(treatyNow))
              .c_str());
    check(context(peaceWorld2.state, "old-state", {}, {})
                      .facts["commitments"]["k2-1"]["months_left"] == 12 &&
              context(peaceWorld2.state, "old-state", {}, {})
                      .facts["commitments"]["k2-1"]["terms"]
                      .size() == 3,
          "Decisions see a treaty's terms and remaining months");
    check(context(namedWorld.state, "far", {}, {}).facts["allied_wars"].isMember("c1-1"),
          "Allies see their partners' wars");
    auto ending = peaceWorld2.state;
    ending.commitments["k2-1"]["expires_turn"] = 3;
    auto expired = resolve(ending);
    check(expired.state.commitments["k2-1"]["status"] == "resolved" &&
              expired.state.territories["march"]["controller"] == "neighbor" &&
              expired.events.size() == 1 &&
              expired.events[0].text.find("handed March back") != std::string::npos,
          "Treaties expire and their occupations end");
    auto amend = [&](const char *reply) {
        return compileOperations(
            peaceWorld2.state,
            parse((std::string(R"({"events":[{"description":"Neighbor renews the treaty",)"
                               R"("actor":"neighbor","blocked_by":"none","background":false,)"
                               R"("causes":[],"operations":[{"action":"amend_commitment",)"
                               R"("commitment":"k2-1","months":24,"text":"Peace renewed",)"
                               R"("terms":[{"action":"withdraw","region":"march"}]}]},)"
                               R"({"description":")") +
                   reply +
                   R"(","actor":"old-state","blocked_by":"none","background":false,"causes":[],)"
                   R"("operations":[]}]})")
                      .c_str()),
            adjacency);
    };
    auto renewed = applyDelta(peaceWorld2, amend("Old agrees with Neighbor"), geo);
    check(renewed.state.commitments["k2-1"]["expires_turn"] == 26 &&
              renewed.state.commitments["k2-1"]["text"] == "Peace renewed" &&
              renewed.state.territories["march"]["controller"] == "neighbor" &&
              renewed.state.commitments["k2-1"]["terms"].size() == 4,
          "Agreed amendments renew and revise a treaty");
    auto amendOffer = applyDelta(peaceWorld2, amend("Old holds elections"), geo);
    check(amendOffer.state.proposals["k2-1-r2"]["amends"] == "k2-1" &&
              amendOffer.state.commitments["k2-1"]["text"] == treatyNow["text"],
          "An amendment offered to the player waits for an answer");
    auto amendNext = resolve(amendOffer.state);
    auto amended = applyDelta(amendNext,
                              compileOperations(amendNext.state, none, adjacency,
                                                parse(R"([{"proposal":"k2-1-r2","accept":true}])")),
                              geo);
    check(amended.state.commitments["k2-1"]["text"] == "Peace renewed" &&
              amended.state.commitments["k2-1"]["expires_turn"] == 27 &&
              amended.state.territories["march"]["controller"] == "neighbor" &&
              amended.state.proposals["k2-1-r2"]["status"] == "accepted",
          "Accepted amendment takes effect next month");
    auto betrayal = compileOperations(
        peaceWorld2.state,
        parse(R"({"events":[{"description":"Neighbor strikes East","actor":"neighbor",)"
              R"("blocked_by":"none","background":false,"causes":[],)"
              R"("operations":[{"action":"occupy","region":"east","by":"neighbor"}]}]})"),
        adjacency);
    auto betrayed = applyDelta(peaceWorld2, betrayal, geo);
    check(
        betrayal.events[0]["breaches"] == "k2-1" &&
            betrayed.state.commitments["k2-1"]["status"] == "resolved" &&
            betrayed.state.territories["east"]["controller"] == "neighbor" &&
            betrayed.state.territories["march"]["controller"] == "neighbor",
        ("Acting against a treaty partner breaks the treaty: " + dump(betrayal.events[0])).c_str());
    auto traded = applyDelta(opsBase, trade, geo);
    auto tradeNext = resolve(traded.state);
    auto tariffs = compileOperations(
        tradeNext.state,
        parse(R"({"events":[{"description":"Neighbor quits the accord","actor":"neighbor",)"
              R"("blocked_by":"none","background":false,"causes":[],)"
              R"("operations":[{"action":"resolve_commitment","commitment":"k1-1"}]}]})"),
        adjacency);
    check(effectOf(tariffs, "neighbor")["economy"] == "decline" &&
              effectOf(tariffs, "far")["economy"] == "decline",
          "Ending a trade treaty costs its parties");
    auto peaceOffer = compileOperations(
        atPeace.state,
        parse(R"({"events":[{"description":"Neighbor sues for peace","actor":"neighbor",)"
              R"("blocked_by":"none","background":false,"causes":[],)"
              R"("operations":[{"action":"add_commitment","actors":["neighbor","old-state"],)"
              R"("kind":"peace_treaty","months":0,"name":"Peace of March","text":"War ends",)"
              R"("terms":[{"action":"end_conflict","conflict":"c1-1"}]}]}]})"),
        adjacency);
    auto offeredPeace = applyDelta(atPeace, peaceOffer, geo);
    check(offeredPeace.state.conflicts["c1-1"]["status"] == "open" &&
              offeredPeace.state.proposals["k2-1"]["terms"].size() == 1,
          "Offered terms wait for the answer");
    auto peaceNext = resolve(offeredPeace.state);
    auto accepted2 = applyDelta(peaceNext,
                                compileOperations(peaceNext.state, none, adjacency,
                                                  parse(R"([{"proposal":"k2-1","accept":true}])")),
                                geo);
    check(accepted2.state.conflicts["c1-1"]["status"] == "resolved" &&
              accepted2.state.commitments["k2-1"]["terms"][0]["action"] == "end_conflict" &&
              !accepted2.state.commitments["k2-1"].isMember("expires_turn"),
          "Accepted terms take effect");
    check(offeredPeace.state.proposals["k2-1"]["status"] == "pending" &&
              peaceOffer.events[0]["text"].asString().find("awaits") != std::string::npos,
          "Peace from a stalled war awaits the player's answer");
    auto quiet = applyMonthlyStatus(accepted2, {}, adjacency);
    for (const auto &event : quiet.events)
        check(event.action != "war_toll" || event.target != "c1-1",
              "Accepted peace silences the stalled war's toll");
    auto treatySchema =
        operationSchema(treaty.state, encode(treaty.state), {{"old-state", "Renew"}});
    check(operation(treatySchema, "amend_commitment").isObject() &&
              operation(treatySchema, "add_commitment")["properties"].isMember("terms") &&
              operation(treatySchema, "add_commitment")["properties"]["months"]["maximum"] == 120,
          "Treaty terms and amendments in the schema");
    auto stranger = offer;
    for (auto &c : stranger.changes)
        if (c["kind"] == "proposal")
            c["after"]["actors"] = parse(R"(["neighbor","far"])");
    rejects([&] { applyDelta(opsBase, stranger, geo); });
    ended = compileOperations(
        namedWorld.state,
        parse(R"({"events":[{"description":"Peace","actor":"old-state","blocked_by":"none",)"
              R"("background":false,"causes":[],"operations":[{"action":"resolve_conflict",)"
              R"("conflict":"c1-1"}]}]})"),
        adjacency);
    auto peaceWorld = applyDelta(namedWorld, ended, geo);
    check(peaceWorld.state.conflicts["c1-1"]["ended_turn"] == 1 &&
              peaceWorld.state.conflicts["c1-1"]["intensity"] == "none",
          "Ended wars record when");
    auto unnamed = d;
    unnamed.changes = Json::arrayValue;
    Json::Value bare;
    bare["kind"] = "conflict";
    bare["id"] = "c9";
    bare["before"] = Json::nullValue;
    bare["after"] = parse(R"({"actors":["old-state","neighbor"],"status":"open","text":"War"})");
    bare["event"] = "t1-change";
    bare["rationale"] = "x";
    unnamed.changes.append(bare);
    rejects([&] { applyDelta(base, unnamed, geo); });
    auto legacy = base;
    legacy.state.conflicts["c9"] = bare["after"];
    applyDelta(legacy, WorldDelta{}, geo); // Entries from before names stay valid.
}
