#include "world_tests.h"
#include <filesystem>
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
        // The event log: categories, places, scope and what is still on the map. On disk, so an
        // older save can be reopened below.
        auto path =
            (std::filesystem::temp_directory_path() / "localhistoria-event-log.sqlite").string();
        for (auto suffix : {"", "-wal", "-shm"})
            std::filesystem::remove(path + suffix);
        Store log(path);
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
            auto once = log.eventLog(current, parse(R"({})"));
            check(dump(log.eventLog(current, parse(R"({})"))) == dump(once) &&
                      log.eventLog(current, parse(R"({"q":"harvest"})"))["events"].size() == 1 &&
                      dump(log.eventLog(current, parse(R"({})"))) == dump(once),
                  "Repeated event-log reads agree");
        }
        auto paged = log.eventLog(current, parse(R"({"limit":1,"before_turn":1})"));
        check(paged["events"].empty() && !paged["more"].asBool(),
              "Paging before the first month is empty");
        // A finished city stays news after it is destroyed; where it stood follows today's map.
        auto built = resolve(current);
        built.state.entities["new-town"] =
            parse(R"({"kind":"city","name":"New Town","region":"west","owner":"old-state"})");
        built.events.push_back({"old-state", "completion", "new-town", "New Town is finished"});
        log.commit(log.submit(current.turn, "log-city", "{}")["id"].asString(), built, {}, {});
        current = built.state;
        auto city = log.eventLog(current, parse(R"({"categories":["construction"],"limit":1})"));
        check(city["events"].size() == 1 && city["events"][0]["major"].asBool() &&
                  city["events"][0]["regions"] == parse(R"(["west"])") && !city["more"].asBool(),
              "A page has more only when an older month matches its filters");
        auto razed = resolve(current);
        razed.state.entities.removeMember("new-town");
        log.commit(log.submit(current.turn, "log-razed", "{}")["id"].asString(), razed, {}, {});
        current = razed.state;
        auto ruin = log.eventLog(current, parse(R"({"categories":["construction"]})"))["events"];
        check(ruin.size() == 1 && ruin[0]["major"].asBool() && ruin[0]["regions"].empty(),
              "A destroyed city's completion stays major");
        auto whole = log.eventLog(current, parse(R"({"limit":1,"before_turn":2})"));
        check(whole["events"].size() == 3 && whole["next_before_turn"] == 1 &&
                  !whole["more"].asBool(),
              "Pages hold whole months");
        // A commit that fails after its month's items were written leaves none behind.
        auto doomed = resolve(current);
        doomed.events.push_back({"far", "unrest", "far", "Grumbling on the island"});
        auto doomedJob = log.submit(current.turn, "log-doomed", "{}")["id"].asString();
        log.execute("INSERT INTO narration VALUES(?,'taken',0)", {doomedJob});
        bool failed = false;
        try {
            log.commit(doomedJob, doomed, {}, {});
        } catch (const std::runtime_error &) {
            failed = true;
        }
        check(failed, "The doomed commit fails");
        log.execute("UPDATE jobs SET status='failed' WHERE id=?", {doomedJob});
        auto doomedTurn = std::to_string(doomed.state.turn);
        check(log.query("SELECT turn FROM history_items WHERE turn=? UNION SELECT turn FROM "
                        "history_months WHERE turn=?",
                        {doomedTurn, doomedTurn})
                  .empty(),
              "A failed commit leaves no event-log items");
        // An older save gains its items when it opens, a few months at a time, and reads exactly
        // like the log written month by month; an interrupted backfill resumes and replaces
        // what it half wrote.
        Json::Value written(Json::arrayValue);
        auto filters = parse(R"([{}, {"major":true}, {"on_map":true}, {"q":"harvest"},
            {"scope":"actor","actor":"far"}, {"scope":"actor","actor":"old-state"},
            {"categories":["construction","world"]}, {"limit":1}])");
        for (auto &f : filters)
            written.append(log.eventLog(current, f));
        log.sql("DROP TABLE history_items; DROP TABLE history_links; DROP TABLE history_months; "
                "PRAGMA user_version = 1");
        auto reads = [&](Store &store) {
            Json::Value out(Json::arrayValue);
            for (auto &f : filters)
                out.append(store.eventLog(current, f));
            return dump(out);
        };
        {
            Store older(path);
            check(reads(older) == dump(written), "A backfilled log reads like the written one");
            older.sql("UPDATE history_items SET text='stale' WHERE turn=1; DELETE FROM "
                      "history_months WHERE turn=1");
        }
        Store resumed(path);
        check(reads(resumed) == dump(written) &&
                  resumed.query("SELECT turn FROM history_items WHERE text='stale'").empty(),
              "An interrupted backfill resumes");
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
    {
        // A background development that re-reports one already in history is the past, not a
        // development. Only background events are dropped: an intention's outcome stays 1:1.
        const char *repeat =
            R"({"events":[],"world_events":[{"actor":"neighbor","causes":[],)"
            R"("description":"A localized humanitarian crisis emerges in West",)"
            R"("effects":[{"actor":"neighbor","economy":"decline","stability":"none"}]}]})";
        auto fresh = compileOperations(opsBase.state, parse(repeat), adjacency);
        check(fresh.events.size() == 1, "A new background development is kept");
        auto known = std::set<std::string>{fresh.events[0]["id"].asString()};
        check(compileOperations(opsBase.state, parse(repeat), adjacency, Json::arrayValue, known)
                  .events.empty(),
              "A background development repeating history is dropped");
        auto owned =
            parse(R"({"events":[{"actor":"old-state","blocked_by":"none","background":false,)"
                  R"("causes":[],"operations":[],"description":)"
                  R"("A localized humanitarian crisis emerges in West"}]})");
        check(compileOperations(opsBase.state, owned, adjacency, Json::arrayValue, known)
                      .events.size() == 1,
              "An intention's own outcome is never dropped as a repeat");
    }
}
