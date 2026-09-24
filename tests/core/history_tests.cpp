#include "core_tests.h"
void historyTests() {
    auto history = fetchHistory("PT", [](auto host, auto path, auto, double) {
        if (host == "https://query.wikidata.org")
            return parse(
                R"({"results":{"bindings":[{"item":{"value":"http://www.wikidata.org/entity/)"
                R"(Q45"}}]}})");
        if (host == "https://www.wikidata.org")
            return parse(R"({"entities":{"Q45":{"sitelinks":{"enwiki":{"title":"Portugal"}}}}})");
        return parse(R"({"latest":{"id":12345},)"
                     R"("html":"<p>Portugal is a country.</p><h2>History</h2><p>Historical )"
                     R"(reference.</p>"})");
    });
    check(history["available"].asBool() && history["country_wikidata"] == "Q45" &&
              history["revision"] == 12345,
          "History mapping and revision");
    check(history["attribution"].asString().find("CC BY-SA") != std::string::npos,
          "Source attribution");
    check(!fetchHistory("PT",
                        [](auto, auto, auto, double) {
                            return parse(R"({"results":{"bindings":[]}})");
                        })["available"]
               .asBool(),
          "Unavailable mapping");
    check(!fetchHistory("PT",
                        [](auto, auto, auto, double) {
                            return parse(R"({"results":{"bindings":[{},{}]}})");
                        })["available"]
               .asBool(),
          "Ambiguous mapping");
    std::ifstream fixture(std::string(FIXTURE_DIR) + "/portugal-page.json");
    std::string fixtureText((std::istreambuf_iterator<char>(fixture)), {});
    auto recorded = fetchHistory("PT", [&](auto host, auto, auto, double) {
        if (host == "https://query.wikidata.org")
            return parse(
                R"({"results":{"bindings":[{"item":{"value":"http://www.wikidata.org/entity/)"
                R"(Q45"}}]}})");
        if (host == "https://www.wikidata.org")
            return parse(R"({"entities":{"Q45":{"sitelinks":{"enwiki":{"title":"Portugal"}}}}})");
        return parse(fixtureText);
    });
    check(recorded["available"].asBool() && recorded["revision"] == 1375265884,
          "Recorded Wikipedia response");
    check(recorded["excerpt"].asString().size() <= 3000 &&
              recorded["history_excerpt"].asString().size() <= 3000,
          "Bounded reference excerpts");
    auto recent = fetchHistory("PT", [](auto host, auto, auto, double) {
        if (host == "https://query.wikidata.org")
            return parse(
                R"({"results":{"bindings":[{"item":{"value":"http://www.wikidata.org/entity/)"
                R"(Q45"}}]}})");
        if (host == "https://www.wikidata.org")
            return parse(R"({"entities":{"Q45":{"sitelinks":{"enwiki":{"title":"Portugal"}}}}})");
        Json::Value page;
        page["latest"]["id"] = 1;
        std::string html = "<p>Portugal is a country on the Iberian Peninsula in south-western "
                           "Europe.</p><h2 id=\"History\">History</h2>";
        for (int n = 0; n < 400; ++n)
            html += "<p>Founding era paragraph " + std::to_string(n) + ".</p>";
        html += "<p>In 2024 a new government took office.<sup class=\"mw-ref\">[ 12 "
                "]</sup></p><h2 id=\"Geography\">Geography</h2><p>Mountains.</p>";
        page["html"] = html;
        return page;
    });
    auto recentText = recent["history_excerpt"].asString();
    auto sourceText = recent["history_source"].asString();
    check(sourceText.size() <= 12000 + 3 && sourceText.size() > recentText.size() &&
              sourceText.substr(sourceText.size() - 40) ==
                  recentText.substr(recentText.size() - 40),
          "Chronicle source is a longer tail of the same history");
    check(recent["history_scope"] == "recent_source" && recentText.size() <= 3000 + 3 &&
              recentText.rfind("…", 0) == 0 &&
              recentText.find("In 2024 a new government took office.") != std::string::npos &&
              recentText.find("[") == std::string::npos &&
              recentText.find("Mountains") == std::string::npos &&
              recentText.find("paragraph 0.") == std::string::npos,
          "History excerpt keeps the most recent part of the section");
    auto brief = briefReference(
        parse(R"({"available":true,"title":"T","excerpt":"Uma frase curta sobre a nação",)"
              R"("history_excerpt":"Founded long ago, then reformed in 2024"})"),
        12, 0);
    check(brief["excerpt"] == "Uma frase…" && !brief.isMember("history_excerpt") &&
              brief["title"] == "T" &&
              briefReference(brief, 3, 3)["excerpt"].asString().size() <= 6,
          "Brief references cut at words");
    check(briefReference(parse(R"({"history_excerpt":"Founded long ago, then reformed in 2024"})"),
                         10, 20)["history_excerpt"] == "…reformed in 2024",
          "Brief history keeps its most recent end");
    auto chronicled = briefReference(
        parse(R"({"excerpt":"Intro","history_excerpt":"Raw tail","history_source":"Long tail",)"
              R"("chronicle":"Old. A very long background sentence is omitted here. Reform won. Peace came."})"),
        12, 40);
    check(!chronicled.isMember("history_excerpt") && !chronicled.isMember("history_source") &&
              chronicled["chronicle"] == "Old. Reform won. Peace came." &&
              !briefReference(chronicled, 12, 0).isMember("chronicle"),
          "Chronicle reserves background and keeps newest complete sentences");
    {
        Store chronicles(":memory:");
        WorldState cw;
        cw.campaignId = "c";
        cw.player = "PT";
        cw.actors["PT"]["name"] = "Portugal";
        chronicles.create(cw, false);
        chronicles.saveChronicle("PT", parse(R"({"summary":"S","through_turn":0})"));
        check(chronicles.chronicle("PT")["summary"] == "S" && chronicles.chronicle("ES").isNull(),
              "Chronicle save and load");
        for (int n = 1; n <= 3; ++n) {
            std::string id = "e" + std::to_string(n), actor = n == 2 ? "ES" : "PT";
            chronicles.execute("INSERT INTO events VALUES(?,?,?)",
                               {id, std::to_string(n),
                                R"({"actor":")" + actor + R"(","text":"D)" + std::to_string(n) +
                                    R"(","effects":[],"operations":[1]})"});
            chronicles.execute("INSERT INTO event_links VALUES(?,'actors',?)", {id, actor});
        }
        // A rolled-back event never happened, so it never reaches a chronicle.
        chronicles.execute("INSERT INTO events VALUES('e4',4,?)",
                           {R"({"actor":"PT","text":"Rolled back","blocked_by":"infeasible"})"});
        chronicles.execute("INSERT INTO event_links VALUES('e4','actors','PT')");
        auto since = chronicles.chronicleEvents("PT", 1);
        check(since.size() == 1 && since[0]["description"] == "D3" && since[0]["turn"] == 3 &&
                  !since[0].isMember("operations"),
              "Chronicle events filter by actor and turn");
        check(chronicles.chronicleEvents("PT", 0).size() == 2 &&
                  chronicles.chronicleEvents("PT", 0)[0]["description"] == "D1",
              "Chronicle events are oldest first");
        // A busy year cannot push an unresolved milestone out with routine later events.
        auto state = chronicles.campaign();
        state["proposals"]["pending"]["status"] = "pending";
        state["proposals"]["pending"]["event"] = "e1";
        chronicles.execute("UPDATE campaign SET state=?", {dump(state)});
        for (int n = 5; n < 65; ++n) {
            auto id = "e" + std::to_string(n);
            chronicles.execute("INSERT INTO events VALUES(?,?,?)", {id, std::to_string(n), R"({"actor":"PT","text":"Routine event"})"});
            chronicles.execute("INSERT INTO event_links VALUES(?,'actors','PT')", {id});
        }
        auto busy = chronicles.chronicleEvents("PT", 0);
        check(busy.size() == 40 && busy[0]["id"] == "e1" && busy[39]["id"] == "e64", "Busy-year selection preserves unresolved events and chronological order");
        chronicles.create(cw, true);
        check(chronicles.chronicle("PT").isNull(), "New campaign clears chronicles");
    }
    check(recorded["history_excerpt"].asString().find("History") != std::string::npos,
          "Separate history extraction");
}
