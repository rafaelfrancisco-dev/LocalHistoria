#include "world_tests.h"
void WorldTests::events() {
    // Event sites appear at once and fade after a few months unless a later event involves them
    // or cites their incident.
    auto site =
        compile(R"({"events":[{"description":"Flood strikes West","actor":"old-state",)"
                R"("blocked_by":"none","background":false,"causes":[],)"
                R"("operations":[{"action":"build","kind":"event_site","name":"West flood",)"
                R"("actor":"old-state","region":"west"}]}]})");
    auto siteWorld = applyDelta(opsBase, site, geo);
    auto siteId = site.changes[0]["id"].asString();
    check(siteWorld.state.entities[siteId]["status"] == "active" &&
              siteWorld.state.entities[siteId]["expires_turn"] == 1 + eventMapMonths,
          "Event sites start active with an expiry");
    auto noExpiry = site;
    noExpiry.changes[0]["after"].removeMember("expires_turn");
    rejects([&] { applyDelta(opsBase, noExpiry, geo); });
    auto portExpiry = site;
    portExpiry.changes[0]["after"]["kind"] = "port";
    rejects([&] { applyDelta(opsBase, portExpiry, geo); });
    auto aged = siteWorld.state;
    for (int n = 0; n < eventMapMonths - 1; ++n)
        aged = resolve(aged).state;
    check(aged.entities[siteId]["status"] == "active", "Event site lasts its months");
    auto faded = resolve(aged);
    check(faded.state.entities[siteId]["status"] == "inactive" &&
              !faded.state.entities[siteId].isMember("expires_turn") && faded.events.empty(),
          "Event site fades quietly");
    auto cited = compileOperations(
        aged,
        parse(std::string(R"({"events":[{"description":"Flood relief","actor":"neighbor",)"
                          R"("blocked_by":"none","background":false,"causes":[")") +
              site.events[0]["id"].asString() + R"("],"operations":[]}]})"),
        adjacency);
    check(cited.changes.size() == 1 &&
              cited.changes[0]["after"]["expires_turn"] == aged.turn + eventMapMonths &&
              cited.events[0]["entities"][0] == siteId,
          "Citing an incident renews its site");
    auto touched = compileOperations(
        aged,
        parse(std::string(R"({"events":[{"description":"Memorial closed","actor":"old-state",)"
                          R"("blocked_by":"none","background":false,"causes":[],)"
                          R"("operations":[{"action":"set_entity_status","entity":")") +
              siteId + R"(","status":"inactive"}]}]})"),
        adjacency);
    check(touched.changes.size() == 1 && touched.changes[0]["after"]["status"] == "inactive" &&
              !touched.changes[0]["after"].isMember("expires_turn"),
          "An inactive site loses its expiry");
    // Significance: map and war changes are major whatever the model says; a severe or strong
    // effect declares a headline, capped at three a month; world events without an effect are
    // dropped.
    check(split2.events.size() == 2, "World developments without an effect are dropped");
    auto headlines = parse(R"({"events":[]})");
    for (auto text : {"Talks", "Summit", "Protest", "Visit", "Election"})
        headlines["events"].append(
            parse(std::string(R"({"description":")") + text +
                  R"(","actor":"neighbor","blocked_by":"none","background":false,"causes":[],)"
                  R"("operations":[],"effects":[{"actor":"neighbor","economy":"strong_growth",)"
                  R"("stability":"none"}]})"));
    headlines["events"][3]["effects"] =
        parse(R"([{"actor":"neighbor","economy":"decline","stability":"none"}])");
    headlines["events"][4]["effects"] =
        parse(R"([{"actor":"neighbor","economy":"severe_decline","stability":"severe_decline"}])");
    auto capped = compileOperations(opsBase.state, headlines, adjacency);
    int majors = 0;
    for (auto &e : capped.events)
        majors += e["major"].asBool();
    check(majors == 3 && capped.events[4]["major"].asBool() && capped.events[0]["major"].asBool() &&
              !capped.events[2]["major"].asBool() && !capped.events[3]["major"].asBool(),
          "Strong effects declare headlines, capped weightiest first; mild ones stay routine");
    check(named.events[0]["major"].asBool() && !reform.events[0]["major"].asBool() &&
              site.events[0]["major"].asBool() && !orphan.events[0]["major"].asBool(),
          "Map and war changes are major");
    // Events link the wars and treaties they shape.
    check(named.events[0]["conflicts"] == parse(R"(["c1-1"])") &&
              named.events[0]["commitments"] == parse(R"(["k1-2"])") &&
              ended.events[0]["conflicts"] == parse(R"(["c1-1"])") &&
              reform.events[0]["conflicts"].empty(),
          "Events record their wars and treaties");
    {
        // The event log: categories, places, scope and what is still on the map.
        Store log(":memory:");
        auto start = opsBase.state;
        start.turn = 0;
        log.create(start, false);
        auto world =
            parse(R"({"events":[{"description":"Flood strikes West","actor":"old-state",)"
                  R"("blocked_by":"none","background":false,"causes":[],)"
                  R"("operations":[{"action":"build","kind":"event_site","name":"West flood",)"
                  R"("actor":"old-state","region":"west"}]},{"description":"Island reform",)"
                  R"("actor":"far","blocked_by":"none","background":false,"causes":[],)"
                  R"("operations":[]},{"description":"Orphan","actor":"old-state",)"
                  R"("blocked_by":"none","background":false,"causes":[],)"
                  R"("operations":[{"action":"create_actor","actor_id":"z","name":"Zed",)"
                  R"("color":"#00aa00"}]}],"world_events":[{"actor":"neighbor","causes":[],)"
                  R"("description":"Harvest festival","effects":[{"actor":"neighbor",)"
                  R"("economy":"severe_decline","stability":"none"}]}]})");
        auto first = resolve(start);
        auto delta = compileOperations(first.state, world, adjacency);
        auto month = applyDelta(first, delta, geo);
        Json::Value r;
        r["delta"] = encode(delta);
        log.commit(log.submit(0, "log-1", "{}")["id"].asString(), month, {}, r);
        auto current = month.state;
        auto all = log.eventLog(current, parse(R"({"scope":"all"})"))["events"];
        check(all.size() == 3 && all[0]["category"] == "player" && all[1]["category"] == "nation" &&
                  all[2]["category"] == "world" && all[0]["on_map"].asBool() &&
                  all[0]["location"].size() == 1,
              ("Event log: " + dump(all)).c_str());
        check(!all[1]["location"].empty(), "Events without places sit in the actor's heartland");
        check(!all[1]["major"].asBool() && !all[1]["on_map"].asBool() && all[2]["major"].asBool() &&
                  log.eventLog(current, parse(R"({"major":true})"))["events"].size() == 2 &&
                  log.eventLog(current, parse(R"({"on_map":true})"))["events"].size() == 2,
              "Only major events reach the map");
        check(log.eventLog(current, parse(R"({"scope":"actor","actor":"far"})"))["events"].size() ==
                      1 &&
                  log.eventLog(current, parse(R"({"categories":["world"]})"))["events"].size() ==
                      1 &&
                  log.eventLog(current, parse(R"({"q":"harvest"})"))["events"].size() == 1,
              "Event log filters");
        for (int n = 0; n < eventMapMonths; ++n) {
            auto next = resolve(current);
            log.commit(
                log.submit(current.turn, "log-quiet-" + std::to_string(n), "{}")["id"].asString(),
                next, {}, {});
            current = next.state;
        }
        check(log.eventLog(current, parse(R"({"on_map":true})"))["events"].empty() &&
                  log.eventLog(current, parse(R"({})"))["events"].size() == 3,
              "Old events leave the map but stay in the log");
        check(log.campaignContext(current, "old-state").facts["related_history"].size() == 1,
              "Rolled-back events stay out of history");
        {
            // One query builds the journal; it matches reading each month's events alone.
            auto journal = log.journal();
            for (auto &month : journal) {
                Json::Value expected(Json::arrayValue);
                for (auto &row : log.query("SELECT document FROM events WHERE turn=? ORDER BY "
                                           "rowid",
                                           {month["turn"].asString()}))
                    expected.append(parse(row["document"].asString()));
                check(dump(month["structured_events"]) == dump(expected),
                      "Journal groups events by month");
            }
            check(journal.size() == Json::ArrayIndex(eventMapMonths + 1) &&
                      !journal[eventMapMonths]["structured_events"].empty(),
                  "Journal covers every month");
            // The event log is cached per month; filters still apply to the cached items.
            auto once = log.eventLog(current, parse(R"({})"));
            check(dump(log.eventLog(current, parse(R"({})"))) == dump(once) &&
                      log.eventLog(current, parse(R"({"q":"harvest"})"))["events"].size() == 1 &&
                      dump(log.eventLog(current, parse(R"({})"))) == dump(once),
                  "Cached event log");
        }
        auto paged = log.eventLog(current, parse(R"({"limit":1,"before_turn":1})"));
        check(paged["events"].empty() && !paged["more"].asBool(),
              "Paging before the first month is empty");
    }
    {
        // Partial events: flagged in the log, and prompts read what actually happened while the
        // journal keeps the model's text.
        Store log(":memory:");
        auto start = opsBase.state;
        start.turn = 0;
        log.create(start, false);
        auto first = resolve(start);
        auto partial = compileOperations(
            first.state,
            parse(R"({"events":[{"description":"Deep strike","actor":"old-state",)"
                  R"("blocked_by":"none","background":false,"causes":[],)"
                  R"("operations":[{"action":"occupy","region":"march","by":"old-state"},)"
                  R"({"action":"occupy","region":"island","by":"old-state"}]}]})"),
            adjacency);
        auto month = applyDelta(first, partial, geo);
        Json::Value r;
        r["delta"] = encode(partial);
        log.commit(log.submit(0, "partial-1", "{}")["id"].asString(), month, {}, r);
        auto current = month.state;
        auto truth = "Deep strike (In fact: not every region changed hands.)";
        check(log.campaignContext(current, "old-state").facts["related_history"][0]["text"] ==
                  truth,
              "Related history reads what happened");
        auto journal = log.journal();
        check(journal[0]["events"][0]["text"] == "Deep strike" &&
                  journal[0]["events"][0]["prompt_text"] == truth,
              ("Journal keeps the model's text: " + dump(journal[0]["events"])).c_str());
        check(context(current, "old-state", journal, {}).facts["recent_events"][0] == truth,
              "Recent events read what happened");
        check(log.chronicleEvents("old-state", 0)[0]["description"] == truth,
              "Chronicle inputs read what happened");
        auto logged = log.eventLog(current, parse(R"({"scope":"all"})"))["events"];
        check(logged[0]["partial"] == true && !logged[0].isMember("failed"),
              "The event log exposes partial");
    }
}
