#include "localhistoria/history.h"
#include "localhistoria/providers.h"
#include "localhistoria/store.h"
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace lh;
int checks = 0;
void check(bool b, const char *s) {
    checks++;
    if (!b)
        throw std::runtime_error(s);
}
template <class F> void rejects(F f) {
    bool thrown = false;
    try {
        f();
    } catch (...) {
        thrown = true;
    }
    check(thrown, "Expected rejection");
}
int main(int argc, char **argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "--live-history") {
            auto j = fetchHistory(argv[2]);
            std::cout << dump(j) << "\n";
            return j["available"].asBool() ? 0 : 1;
        }
        WorldState w;
        w.player = "PT";
        w.campaignId = "test";
        w.actors = parse(R"({"PT":{"name":"Portugal","color":"#123456"},"ES":{"name":"Spain","color":"#654321"},"FR":{"name":"France","color":"#abcdef"}})");
        w.territories = parse(R"({"north":{"name":"North","owner":"PT","controller":"PT","initial_affiliation":"PT"}})");
        w.entities = parse(R"({"port-1":{"name":"Harbor","kind":"port","actor":"PT","region":"north","status":"under_construction","completes_turn":2,"longitude":1,"latitude":1,"origin_event":"initial"}})");
        auto r = resolve(w);
        check(r.state.turn == 1 && r.events.empty() && r.state.entities["port-1"]["status"] == "under_construction", "Month advance");
        auto completed = resolve(r.state);
        check(completed.state.entities["port-1"]["status"] == "active" && !completed.state.entities["port-1"].isMember("completes_turn") &&
                  completed.events.size() == 1 && completed.events[0].text == "Harbor (port) was completed in North.",
              "Construction completes on schedule");
        check(dump(encode(decode(encode(r.state)))) == dump(encode(r.state)), "Serialization");
        auto orders = parseOrders(parse(R"([{"text":"Build a port","targets":["north"]},{"text":"Negotiate with Spain","targets":["ES"]}])"));
        check(orders.size() == 2 && orders[0].targets[0] == "north", "Free-text actions parse");
        check(dump(encode(orders)) == R"([{"targets":["north"],"text":"Build a port"},{"targets":["ES"],"text":"Negotiate with Spain"}])", "Action encoding");
        validate(w, "PT", orders);
        validate(w, "PT", {});
        rejects([&] { validate(w, "PT", {{"   ", {}}}); });
        rejects([&] { validate(w, "PT", {{std::string(501, 'x'), {}}}); });
        rejects([&] { validate(w, "PT", {{"Invade", {"nowhere"}}}); });
        rejects([&] { validate(w, "PT", {{"Same", {}}, {"Same", {}}}); });
        rejects([&] { validate(w, "ZZ", {}); });
        rejects([&] { parseOrders(parse(R"([{"text":"x","action":"army"}])")); });
        rejects([&] { parseOrders(parse(R"({"orders":[]})")); });
        rejects([&] { parseOrders(parse(R"([{"text":9}])")); });
        rejects([&] { parseOrders(parse(R"([{"text":"x","targets":["a","b","c","d","e"]}])")); });
        rejects([&] { parseOrders(parse(R"([{"text":"1"},{"text":"2"},{"text":"3"},{"text":"4"},{"text":"5"},{"text":"6"},{"text":"7"},{"text":"8"},{"text":"9"}])")); });
        auto c = context(w, "ES", {}, {});
        check(c.facts["actor_name"] == "Spain" && c.facts["territories"].empty(), "Context");
        LocalProvider malformed("http://127.0.0.1:8081", .1, [](auto, auto path, auto, double) {
            if(path=="/props")return parse(R"({"default_generation_settings":{"n_ctx":8192}})");
            if(path=="/apply-template")return parse(R"({"prompt":"fixture"})");
            if(path=="/tokenize")return parse(R"({"tokens":[1,2,3]})");
            return parse(
                R"({"choices":[{"message":{"content":"{\"orders\":[{\"action\":\"invade\"}]}"}}]})");
        });
        rejects([&] { malformed.decide(w, c); });
        LocalProvider timeoutProvider("http://127.0.0.1:8082", .1,
                                      [](auto, auto, auto, double) -> Json::Value {
                                          throw std::runtime_error("timeout");
                                      });
        rejects([&] { timeoutProvider.decide(w, c); });
        LocalProvider valid("http://127.0.0.1:8083", .1, [](auto, auto path, auto b, double) {
            if(path=="/props")return parse(R"({"default_generation_settings":{"n_ctx":8192}})");
            if(path=="/apply-template")return parse(R"({"prompt":"fixture"})");
            if(path=="/tokenize")return parse(R"({"tokens":[1,2,3]})");
            check(b["response_format"]["type"] == "json_schema" &&
                      !b["chat_template_kwargs"]["enable_thinking"].asBool() && b["max_tokens"].asInt() == 768,
                  "Constrained request");
            return parse(
                R"({"choices":[{"message":{"content":"{\"actions\":[{\"text\":\"Open trade talks with Portugal\"}]}"}}]})");
        });
        auto validDecision = valid.decide(w, c);
        check(validDecision.orders.size() == 1 && validDecision.orders[0].text == "Open trade talks with Portugal",
              "Valid model adapter");
        // Adjudication reasons only when the context can hold the reasoning; otherwise it answers directly.
        for (int ctx : {8192, 32768}) {
            Json::Value seen;
            LocalProvider judge("http://127.0.0.1:8084", .1, [&](auto, auto path, auto b, double) {
                if(path=="/props"){Json::Value j;j["default_generation_settings"]["n_ctx"]=ctx;return j;}
                if(path=="/apply-template")return parse(R"({"prompt":"fixture"})");
                if(path=="/tokenize")return parse(R"({"tokens":[1,2,3]})");
                seen=b;
                return parse(R"({"choices":[{"message":{"content":"{\"events\":[{\"actor\":\"PT\",\"blocked_by\":\"none\",\"causes\":[],\"description\":\"Waited\",\"operations\":[]}]}","reasoning_content":"Thinking"}}]})");
            });
            Json::Value input;input["intentions"][0]["actor"]="PT";input["intentions"][0]["action"]="Wait";
            judge.adjudicate(w, input, {});
            auto audit = judge.takeAudit();
            check(seen["chat_template_kwargs"]["enable_thinking"].asBool() == (ctx == 32768) && seen["max_tokens"].asInt() == (ctx == 32768 ? 2048 + 384 : 2048) && seen.isMember("thinking_budget_tokens") == (ctx == 32768) &&
                      audit[0]["context_limit"].asInt() == ctx,
                  "Thinking budget follows context size");
        }
        rejects([&] { LocalProvider bad(""); });
        rejects([&] { LocalProvider bad("not-a-url"); });
        auto path = (std::filesystem::temp_directory_path() / "localhistoria-test.sqlite").string();
        std::filesystem::remove(path);
        {
            Store s(path);
            s.create(w, false);
            auto j = s.submit(0, "same-key", "payload");
            auto id = j["id"].asString();
            check(s.submit(0, "same-key", "payload")["id"] == j["id"], "Duplicate idempotency");
            rejects([&] { s.submit(0, "same-key", "different"); });
            rejects([&] { s.submit(0, "other-key", "payload"); });
            rejects([&] { s.create(w, true); });
            rejects([&] { s.commit(id, r, {}, {}, true); });
            check(s.campaign()["turn"] == 0, "Atomic rollback");
            s.commit(id, r, {}, {});
            auto before = dump(s.campaign());
            rejects([&] { timeoutProvider.narrate(completed.events); });
            check(dump(s.campaign()) == before, "Narration isolation");
            s.narrate(id, "Fictional narration", false);
            check(dump(s.campaign()) == before, "Narration never mutates state");
            rejects([&] { s.submit(0, "stale-key", "p"); });
            s.submit(1, "interrupted-key", "p");
        }
        {
            Store s(path);
            check(dump(s.campaign()) == dump(encode(r.state)), "Save reload equivalence");
            s.recover();
            check(s.query("SELECT * FROM jobs WHERE status='interrupted'").size() == 1,
                  "Interrupted recovery");
            s.submit(1, "retry-key", "p");
        }
        std::filesystem::remove(path);
        auto history = fetchHistory("PT", [](auto host, auto path, auto, double) {
            if (host == "https://query.wikidata.org")
                return parse(
                    R"({"results":{"bindings":[{"item":{"value":"http://www.wikidata.org/entity/Q45"}}]}})");
            if (host == "https://www.wikidata.org")
                return parse(
                    R"({"entities":{"Q45":{"sitelinks":{"enwiki":{"title":"Portugal"}}}}})");
            return parse(
                R"({"latest":{"id":12345},"html":"<p>Portugal is a country.</p><h2>History</h2><p>Historical reference.</p>"})");
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
                    R"({"results":{"bindings":[{"item":{"value":"http://www.wikidata.org/entity/Q45"}}]}})");
            if (host == "https://www.wikidata.org")
                return parse(
                    R"({"entities":{"Q45":{"sitelinks":{"enwiki":{"title":"Portugal"}}}}})");
            return parse(fixtureText);
        });
        check(recorded["available"].asBool() && recorded["revision"] == 1375265884,
              "Recorded Wikipedia response");
        check(recorded["excerpt"].asString().size() <= 3000 &&
                  recorded["history_excerpt"].asString().size() <= 3000,
              "Bounded reference excerpts");
        auto recent = fetchHistory("PT", [](auto host, auto, auto, double) {
            if (host == "https://query.wikidata.org")
                return parse(R"({"results":{"bindings":[{"item":{"value":"http://www.wikidata.org/entity/Q45"}}]}})");
            if (host == "https://www.wikidata.org")
                return parse(R"({"entities":{"Q45":{"sitelinks":{"enwiki":{"title":"Portugal"}}}}})");
            Json::Value page;page["latest"]["id"]=1;
            std::string html="<p>Portugal is a country on the Iberian Peninsula in south-western Europe.</p><h2 id=\"History\">History</h2>";
            for(int n=0;n<400;++n)html+="<p>Founding era paragraph "+std::to_string(n)+".</p>";
            html+="<p>In 2024 a new government took office.<sup class=\"mw-ref\">[ 12 ]</sup></p><h2 id=\"Geography\">Geography</h2><p>Mountains.</p>";
            page["html"]=html;return page;
        });
        auto recentText = recent["history_excerpt"].asString();
        auto sourceText = recent["history_source"].asString();
        check(sourceText.size() <= 12000 + 3 && sourceText.size() > recentText.size() &&
                  sourceText.substr(sourceText.size() - 40) == recentText.substr(recentText.size() - 40),
              "Chronicle source is a longer tail of the same history");
        check(recent["history_scope"] == "recent_source" && recentText.size() <= 3000 + 3 && recentText.rfind("…", 0) == 0 &&
                  recentText.find("In 2024 a new government took office.") != std::string::npos && recentText.find("[") == std::string::npos &&
                  recentText.find("Mountains") == std::string::npos && recentText.find("paragraph 0.") == std::string::npos,
              "History excerpt keeps the most recent part of the section");
        auto brief = briefReference(parse(R"({"available":true,"title":"T","excerpt":"Uma frase curta sobre a nação","history_excerpt":"Founded long ago, then reformed in 2024"})"), 12, 0);
        check(brief["excerpt"] == "Uma frase…" && !brief.isMember("history_excerpt") && brief["title"] == "T" &&
                  briefReference(brief, 3, 3)["excerpt"].asString().size() <= 6,
              "Brief references cut at words");
        check(briefReference(parse(R"({"history_excerpt":"Founded long ago, then reformed in 2024"})"), 10, 20)["history_excerpt"] == "…reformed in 2024",
              "Brief history keeps its most recent end");
        auto chronicled = briefReference(parse(R"({"excerpt":"Intro","history_excerpt":"Raw tail","history_source":"Long tail","chronicle":"Founded long ago, then reformed in 2024"})"), 12, 20);
        check(!chronicled.isMember("history_excerpt") && !chronicled.isMember("history_source") &&
                  chronicled["chronicle"] == "Founded long ago,…" &&
                  !briefReference(chronicled, 12, 0).isMember("chronicle"),
              "Chronicle replaces the history excerpt and keeps its start");
        {
            Store chronicles(":memory:");
            WorldState cw;cw.campaignId="c";cw.player="PT";cw.actors["PT"]["name"]="Portugal";
            chronicles.create(cw, false);
            chronicles.saveChronicle("PT", parse(R"({"summary":"S","through_turn":0})"));
            check(chronicles.chronicle("PT")["summary"] == "S" && chronicles.chronicle("ES").isNull(), "Chronicle save and load");
            for (int n = 1; n <= 3; ++n) {
                std::string id = "e" + std::to_string(n), actor = n == 2 ? "ES" : "PT";
                chronicles.execute("INSERT INTO events VALUES(?,?,?)", {id, std::to_string(n), R"({"actor":")" + actor + R"(","description":"D)" + std::to_string(n) + R"(","effects":[],"operations":[1]})"});
                chronicles.execute("INSERT INTO event_links VALUES(?,'actors',?)", {id, actor});
            }
            auto since = chronicles.chronicleEvents("PT", 1);
            check(since.size() == 1 && since[0]["description"] == "D3" && since[0]["turn"] == 3 && !since[0].isMember("operations"),
                  "Chronicle events filter by actor and turn");
            check(chronicles.chronicleEvents("PT", 0).size() == 2 && chronicles.chronicleEvents("PT", 0)[0]["description"] == "D1", "Chronicle events are oldest first");
            chronicles.create(cw, true);
            check(chronicles.chronicle("PT").isNull(), "New campaign clears chronicles");
        }
        check(recorded["history_excerpt"].asString().find("History") != std::string::npos,
              "Separate history extraction");
        std::cout << checks << " checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
