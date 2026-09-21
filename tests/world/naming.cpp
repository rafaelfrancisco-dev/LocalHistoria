#include "world_tests.h"
void WorldTests::naming() {
    auto &outcomes = schema["properties"]["events"];
    auto &developments = schema["properties"]["world_events"];
    // Wars and treaties get historians' names, kinds and intensity; effects are validated and
    // recorded on events.
    check(outcomes["prefixItems"][0]["required"].size() == 6 &&
              !outcomes["prefixItems"][0]["properties"].isMember("significance") &&
              developments["items"]["properties"]["effects"]["minItems"] == 1 &&
              outcomes["prefixItems"][0]["properties"]["effects"]["maxItems"] == 3,
          "Events carry effects");
    named =
        compile(R"({"events":[{"description":"War is declared","actor":"old-state",)"
                R"("blocked_by":"none","background":false,"causes":[],)"
                R"("effects":[{"actor":"old-state","economy":"decline","stability":"growth"},)"
                R"({"actor":"ghost","economy":"decline","stability":"decline"},)"
                R"({"actor":"neighbor","economy":"none","stability":"none"}],)"
                R"("operations":[{"action":"occupy","region":"march","by":"old-state"},)"
                R"({"action":"open_conflict","actors":["old-state","neighbor"],"kind":"war",)"
                R"("intensity":"high","name":"March War","text":"War over the March"},)"
                R"({"action":"add_commitment","actors":["old-state","far"],"kind":"defence_pact",)"
                R"("name":"Defence Pact","text":"Mutual defence"}]},
  {"description":"Another war","actor":"neighbor","blocked_by":"none","background":false,)"
                R"("causes":[],"operations":[{"action":"open_conflict","actors":["neighbor",)"
                R"("old-state"],)"
                R"("kind":"skirmish","name":"March War","text":"Again"}]},
  {"description":"Far pledges to defend Old","actor":"far","blocked_by":"none","background":false,)"
                R"("causes":[],"operations":[]}]})");
    namedWorld = applyDelta(opsBase, named, geo);
    check(namedWorld.state.conflicts["c1-1"]["name"] == "March War" &&
              namedWorld.state.conflicts["c1-1"]["set_intensity"] == "high" &&
              namedWorld.state.conflicts["c1-1"]["started_turn"] == 1,
          ("Named war: " + dump(namedWorld.state.conflicts)).c_str());
    check(namedWorld.state.commitments["k1-2"]["name"] == "Old–Far Defence Pact" &&
              namedWorld.state.commitments["k1-2"]["kind"] == "defence_pact",
          "Generic treaty name replaced");
    check(namedWorld.state.conflicts["c1-3"]["name"] == "Second March War" &&
              namedWorld.state.conflicts["c1-3"]["kind"] == "war",
          "Repeated name numbered, invalid kind defaulted");
    check(named.events[0]["effects"].size() == 2 && effectOf(named, "ghost").isNull() &&
              effectOf(named, "old-state")["economy"] == "decline" &&
              effectOf(named, "old-state")["stability"] == "growth" &&
              effectOf(named, "neighbor")["economy"] == "decline" &&
              effectOf(named, "neighbor")["stability"] == "decline",
          ("Only valid effects recorded; wars and seizures force the sign: " +
           dump(named.events[0]["effects"]))
              .c_str());
    check(named.events[1]["effects"].size() == 2 &&
              effectOf(named, "neighbor", 1)["economy"] == "decline" &&
              effectOf(named, "neighbor", 1)["stability"] == "none" &&
              effectOf(named, "old-state", 1)["stability"] == "decline",
          "Opening a war costs both sides even without model effects");
    trade = compile(
        R"({"events":[{"description":"Trade accord","actor":"neighbor","blocked_by":"none",)"
        R"("background":false,"causes":[],"effects":[{"actor":"neighbor",)"
        R"("economy":"decline","stability":"none"}],)"
        R"("operations":[{"action":"add_commitment","actors":["neighbor","far"],)"
        R"("kind":"trade_agreement","name":"Trade Accord","text":"Tariffs cut"}]},
  {"description":"Non-aggression","actor":"neighbor","blocked_by":"none","background":false,)"
        R"("causes":[],"operations":[{"action":"add_commitment","actors":["neighbor","far"],)"
        R"("kind":"non_aggression_pact","name":"Peace Pact","text":"No war"}]},
  {"description":"Far agrees terms with Neighbor","actor":"far","blocked_by":"none",)"
        R"("background":false,"causes":[],"operations":[]}]})");
    check(trade.events[0]["effects"].size() == 2 &&
              effectOf(trade, "neighbor")["economy"] == "growth" &&
              effectOf(trade, "far")["economy"] == "growth" &&
              effectOf(trade, "far")["stability"] == "none" && !trade.events[1].isMember("effects"),
          ("Trade agreements lift both economies; other treaties are left to the model: " +
           dump(trade.events[0]["effects"]))
              .c_str());
    auto crowded = compile(
        R"({"events":[{"description":"Crowded war","actor":"old-state","blocked_by":"none",)"
        R"("background":false,"causes":[],"effects":[{"actor":"far","economy":"growth",)"
        R"("stability":"none"},{"actor":"old-state","economy":"strong_growth",)"
        R"("stability":"growth"},{"actor":"neighbor","economy":"growth",)"
        R"("stability":"growth"}],"operations":[{"action":"open_conflict",)"
        R"("actors":["old-state","neighbor"],"kind":"war","intensity":"low",)"
        R"("name":"Crowded War","text":"War"}]}]})");
    check(
        crowded.events[0]["effects"].size() == 3 &&
            effectOf(crowded, "old-state")["economy"] == "decline" &&
            effectOf(crowded, "old-state")["stability"] == "growth" &&
            effectOf(crowded, "neighbor")["economy"] == "decline" &&
            effectOf(crowded, "neighbor")["stability"] == "decline" &&
            effectOf(crowded, "far")["economy"] == "growth",
        ("Forced effects cap the model's optimism: " + dump(crowded.events[0]["effects"])).c_str());
    auto escalated = compileOperations(
        namedWorld.state,
        parse(R"({"events":[{"description":"Offensive","actor":"neighbor","blocked_by":"none",)"
              R"("background":false,"causes":[],"effects":[{"actor":"neighbor",)"
              R"("economy":"growth","stability":"growth"}],)"
              R"("operations":[{"action":"set_conflict_intensity","conflict":"c1-1",)"
              R"("intensity":"high"}]}]})"),
        adjacency);
    auto skirmish =
        compile(R"({"events":[{"description":"Border raid","actor":"neighbor","blocked_by":"none",)"
                R"("background":false,"causes":[],"effects":[{"actor":"neighbor",)"
                R"("economy":"growth","stability":"none"}],"operations":[{"action":"occupy",)"
                R"("region":"west","by":"neighbor"},{"action":"open_conflict",)"
                R"("actors":["neighbor","old-state"],"kind":"border_conflict","intensity":"high",)"
                R"("name":"West Raid","text":"Raid"}]},
  {"description":"Quiet grab","actor":"neighbor","blocked_by":"none","background":false,)"
                R"("causes":[],"effects":[{"actor":"neighbor","economy":"strong_growth",)"
                R"("stability":"growth"}],"operations":[{"action":"occupy","region":"east",)"
                R"("by":"neighbor"}]}]})");
    check(effectOf(skirmish, "neighbor")["economy"] == "decline" &&
              effectOf(skirmish, "neighbor")["stability"] == "none" &&
              effectOf(skirmish, "old-state")["economy"] == "decline" &&
              effectOf(skirmish, "old-state")["stability"] == "decline" &&
              effectOf(skirmish, "neighbor", 1)["economy"] == "decline" &&
              effectOf(skirmish, "neighbor", 1)["stability"] == "growth" &&
              effectOf(skirmish, "old-state", 1)["stability"] == "decline",
          ("A high-intensity conflict of any kind, or taking land by force, is never a "
           "windfall: " +
           dump(skirmish.events))
              .c_str());
    check(escalated.events[0]["effects"].size() == 2 &&
              effectOf(escalated, "neighbor")["economy"] == "decline" &&
              effectOf(escalated, "neighbor")["stability"] == "growth" &&
              effectOf(escalated, "old-state")["stability"] == "decline",
          "Escalating to high costs every party");
    auto intensity = [&](const char *actor) {
        return compileOperations(
            namedWorld.state,
            parse(std::string(R"({"events":[{"description":"Offensive","actor":")") + actor +
                  R"(","blocked_by":"none","background":false,"causes":[],)"
                  R"("operations":[{"action":"set_conflict_intensity","conflict":"c1-1",)"
                  R"("intensity":"low"}]}]})"),
            adjacency);
    };
    check(!infeasible(intensity("neighbor")) && infeasible(intensity("far")),
          "Only parties change a war's intensity");
    auto meddle = compileOperations(
        namedWorld.state,
        parse(R"({"events":[{"description":"Far brokers peace","actor":"far",)"
              R"("blocked_by":"none","background":false,"causes":[],)"
              R"("operations":[{"action":"resolve_conflict","conflict":"c1-1"},)"
              R"({"action":"open_conflict","actors":["old-state","neighbor"],"kind":"war",)"
              R"("name":"Proxy War","text":"War"}]}]})"),
        adjacency);
    check(meddle.events[0]["dropped_operations"].size() == 2 && meddle.changes.empty(),
          "Only parties end a war or open one");
    auto pact = [&](const char *reply) {
        return compile(
            (std::string(R"({"events":[{"description":"Far courts Neighbor","actor":"far",)"
                         R"("blocked_by":"none","background":false,"causes":[],)"
                         R"("operations":[{"action":"add_commitment","actors":["far","neighbor"],)"
                         R"("kind":"alliance","name":"Pact","text":"Alliance"}]},)"
                         R"({"description":")") +
             reply +
             R"(","actor":"neighbor","blocked_by":"none","background":false,"causes":[],)"
             R"("operations":[]}]})")
                .c_str());
    };
    check(infeasible(pact("Neighbor holds a parade")) &&
              !infeasible(pact("Neighbor welcomes the Far envoy")) &&
              changed(pact("Neighbor welcomes the Far envoy"), "commitment", "k1-1") &&
              infeasible(pact("Neighbor studies farming")),
          "A treaty needs every party's own outcome to name the partner");
    auto proxyBuild = compile(
        R"({"events":[{"description":"Neighbor builds","actor":"neighbor","blocked_by":"none",)"
        R"("background":false,"causes":[],"operations":[{"action":"build","kind":"army",)"
        R"("name":"Proxy army","actor":"old-state","region":"west"}]}]})");
    check(infeasible(proxyBuild) && proxyBuild.changes.empty(),
          "Nations only build for themselves");
    // A treaty offered to the player waits for the player's answer, given the next month.
    auto offerJson =
        R"({"events":[{"description":"Neighbor offers an alliance","actor":"neighbor",)"
        R"("blocked_by":"none","background":false,"causes":[],)"
        R"("operations":[{"action":"add_commitment","actors":["neighbor","old-state"],)"
        R"("kind":"alliance","name":"Heart Alliance","text":"Mutual defence"}]},)"
        R"({"description":"Old reforms schools","actor":"old-state","blocked_by":"none",)"
        R"("background":false,"causes":[],"operations":[]}]})";
    offer = compile(offerJson);
    check(!infeasible(offer) && changed(offer, "proposal", "k1-1") &&
              !changed(offer, "commitment", "k1-1") && !offer.events[0]["major"].asBool() &&
              offer.events[0]["text"].asString().find("awaits Old's answer") != std::string::npos,
          ("Treaty offered to the player is a proposal: " + dump(offer.events[0])).c_str());
    auto agreed = parse(offerJson);
    agreed["events"][1]["description"] = "Old signs with Neighbor";
    check(changed(compileOperations(opsBase.state, agreed, adjacency), "commitment", "k1-1"),
          "The player's own outcome naming the partner signs at once");
    auto offered = applyDelta(opsBase, offer, geo);
    check(offered.state.proposals["k1-1"]["status"] == "pending" &&
              offered.state.commitments.empty(),
          "Proposal recorded");
    none = parse(R"({"events":[]})");
    auto answer = [&](const char *responses) {
        auto next = resolve(offered.state);
        auto d = compileOperations(next.state, none, adjacency, parse(responses));
        return std::make_pair(d, applyDelta(next, d, geo));
    };
    auto [yesDelta, yes] = answer(R"([{"proposal":"k1-1","accept":true}])");
    check(yes.state.commitments["k1-1"]["name"] == "Heart Alliance" &&
              yes.state.commitments["k1-1"]["status"] == "open" &&
              yes.state.proposals["k1-1"]["status"] == "accepted" && yesDelta.events.size() == 1 &&
              yesDelta.events[0]["actor"] == "old-state" &&
              yesDelta.events[0]["causes"][0] == offer.events[0]["id"],
          ("Accepted proposal becomes the treaty: " + dump(yesDelta.events)).c_str());
    auto [noDelta, no] = answer(R"([{"proposal":"k1-1","accept":false}])");
    check(no.state.commitments.empty() && no.state.proposals["k1-1"]["status"] == "declined" &&
              noDelta.events[0]["text"].asString().find("declines") != std::string::npos,
          "Declined proposal");
    auto [lapseDelta, lapse] = answer("[]");
    check(lapse.state.commitments.empty() && lapse.state.proposals["k1-1"]["status"] == "expired" &&
              lapseDelta.events[0]["actor"] == "neighbor",
          "Unanswered proposal lapses");
    check(compileOperations(offered.state, none, adjacency,
                            parse(R"([{"proposal":"k1-1","accept":true}])"))
              .changes.empty(),
          "No answer in the month of the offer");
    auto forged =
        compileOperations(resolve(offered.state).state,
                          parse(R"({"events":[{"description":"Old accepts","actor":"old-state",)"
                                R"("blocked_by":"none","background":false,"causes":[],)"
                                R"("operations":[{"action":"settle_proposal","proposal":"k1-1",)"
                                R"("answer":"accepted"}]}]})"),
                          adjacency);
    check(!changed(forged, "commitment", "k1-1") && changed(forged, "proposal", "k1-1") &&
              forged.events[0]["text"].asString().find("lapses") != std::string::npos,
          "Only the player's own answer settles an offer; a model cannot forge one");
    auto sameMonth = yesDelta;
    for (auto &e : sameMonth.events)
        e["id"] = "t1-early";
    for (auto &c : sameMonth.changes)
        c["event"] = "t1-early";
    rejects([&] { applyDelta(offered, sameMonth, geo); });
    auto undone = yesDelta;
    for (auto &c : undone.changes)
        if (c["kind"] == "proposal")
            c["after"]["status"] = "pending";
    rejects([&] { applyDelta(resolve(offered.state), undone, geo); });
    auto treatyless = yesDelta;
    for (Json::ArrayIndex n = 0; n < treatyless.changes.size(); ++n)
        if (treatyless.changes[n]["kind"] == "commitment") {
            treatyless.changes.removeIndex(n, nullptr);
            break;
        }
    for (auto &e : treatyless.events)
        e["commitments"] = Json::arrayValue;
    rejects([&] { applyDelta(resolve(offered.state), treatyless, geo); });
}
