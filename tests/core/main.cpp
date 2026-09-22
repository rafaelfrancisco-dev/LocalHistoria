#include "core_tests.h"
int checks = 0;
void check(bool b, const char *s) {
    checks++;
    if (!b)
        throw std::runtime_error(s);
}
int main(int argc, char **argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "--live-history") {
            auto j = fetchHistory(argv[2]);
            std::cout << dump(j) << "\n";
            return j["available"].asBool() ? 0 : 1;
        }
        check(campaignDate(0) == "January 2026" && campaignDate(-3) == "October 2025" &&
                  campaignDate(13) == "February 2027" && campaignDate(-12) == "January 2025",
              "Campaign dates, including months before January 2026");
        check(isCountryCode("GB") && !isCountryCode("gb") && !isCountryCode("GBR") &&
                  !isCountryCode("G") && !isCountryCode(""),
              "Country codes are two capital letters");
        check(isHexColor("#a1B2c3") && !isHexColor("#abc") && !isHexColor("a1b2c3f") &&
                  !isHexColor("#a1b2cg") && !isHexColor("#a1b2c3d"),
              "Colours are #rrggbb");
        check(isEntityId("Q42") && !isEntityId("Q") && !isEntityId("q42") && !isEntityId("Q4x"),
              "Wikidata entity IDs");
        check(isPermanentId("army-1_b") && !isPermanentId("") &&
                  !isPermanentId(std::string(101, 'a')) && isPermanentId(std::string(100, 'a')) &&
                  !isPermanentId("a b") && !isPermanentId("a/b"),
              "Permanent IDs");
        check(lowercase("São PAULO") == "são paulo", "Lowercase leaves non-ASCII bytes alone");
        // The six regional initiatives a real Brazil campaign spent its slots on.
        for (auto text :
             {"Marowijne council convenes a town hall to draft a local heritage resolution "
              "celebrating indigenous and Sranan Tongo culture.",
              "Bolivar holds a public town hall in San Cristobal to solicit local grievances "
              "regarding infrastructure and resource allocation.",
              "The Cojedes Regional Council convenes a public hearing to propose a local tourism "
              "initiative aimed at boosting regional employment.",
              "Lavalleja launches a localized 'Green Commute' initiative to incentivize public "
              "transit use.",
              "The council proclaims a month of cultural celebration.",
              "The province unveils a monument commemorating its founding."})
            check(ceremonial(text), "Ceremonial initiatives are recognised");
        for (auto text :
             {"The Santander Council convenes a public forum to address displacement concerns "
              "and proposes a civic wellbeing plan.",
              "Paraguay presents a unified regional ultimatum to Bolivia and Argentina demanding "
              "the immediate withdrawal of Brazilian forces from AMAMBAY.",
              "Corrientes provincial authorities draft a regional taxation autonomy proposal.",
              "The district demands a larger share of mining revenue from the capital."})
            check(!ceremonial(text), "A gesture naming something at stake is a real action");
        {
            auto war = parse(R"({"status":"open","actors":["PT","ES"]})"),
                 over = parse(R"({"status":"resolved","actors":["PT"]})");
            check(isOpen(war) && !isOpen(over) && isParty(war, "ES") && !isParty(war, "FR") &&
                      !isParty(Json::Value(), "PT") &&
                      controllerOf(parse(R"({"controller":"PT"})")) == "PT",
                  "Conflict helpers");
            Adjacency adjacency{{"a", {"b"}}};
            check(adjacent(adjacency, "a", "b") && adjacent(adjacency, "c", "c") &&
                      !adjacent(adjacency, "b", "a") && !adjacent(adjacency, "a", "c"),
                  "Adjacency");
        }
        WorldState w;
        w.player = "PT";
        w.campaignId = "test";
        w.actors = parse(R"({"PT":{"name":"Portugal","color":"#123456"},"ES":{"name":"Spain",)"
                         R"("color":"#654321"},"FR":{"name":"France","color":"#abcdef"}})");
        w.territories = parse(R"({"north":{"name":"North","owner":"PT","controller":"PT",)"
                              R"("initial_affiliation":"PT"}})");
        w.entities =
            parse(R"({"port-1":{"name":"Harbor","kind":"port","actor":"PT","region":"north",)"
                  R"("status":"under_construction","completes_turn":2,"longitude":1,"latitude":1,)"
                  R"("origin_event":"initial"}})");
        auto r = resolve(w);
        check(r.state.turn == 1 && r.events.empty() &&
                  r.state.entities["port-1"]["status"] == "under_construction",
              "Month advance");
        auto completed = resolve(r.state);
        check(completed.state.entities["port-1"]["status"] == "active" &&
                  !completed.state.entities["port-1"].isMember("completes_turn") &&
                  completed.events.size() == 1 &&
                  completed.events[0].text == "Harbor (port) was completed in North.",
              "Construction completes on schedule");
        check(dump(encode(decode(encode(r.state)))) == dump(encode(r.state)), "Serialization");
        auto orders = parseOrders(
            parse(R"([{"text":"Build a port","targets":["north"]},{"text":"Negotiate with Spain",)"
                  R"("targets":["ES"]}])"));
        check(orders.size() == 2 && orders[0].targets[0] == "north", "Free-text actions parse");
        check(dump(encode(orders)) ==
                  R"([{"targets":["north"],"text":"Build a port"},{"targets":["ES"],)"
                  R"("text":"Negotiate with Spain"}])",
              "Action encoding");
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
        rejects([&] {
            parseOrders(parse(
                R"([{"text":"1"},{"text":"2"},{"text":"3"},{"text":"4"},{"text":"5"},{"text":"6"},)"
                R"({"text":"7"},{"text":"8"},{"text":"9"}])"));
        });
        auto c = context(w, "ES", {}, {});
        check(c.facts["actor_name"] == "Spain" && c.facts["territories"].empty(), "Context");
        LocalProvider malformed("http://127.0.0.1:8081", .1, [](auto, auto path, auto, double) {
            if (path == "/props")
                return parse(R"({"default_generation_settings":{"n_ctx":8192}})");
            if (path == "/apply-template")
                return parse(R"({"prompt":"fixture"})");
            if (path == "/tokenize")
                return parse(R"({"tokens":[1,2,3]})");
            return parse(R"({"choices":[{"message":{"content":"{\"orders\":[{\"action\":)"
                         R"(\"invade\"}]}"}}]})");
        });
        rejects([&] { malformed.decide(w, c); });
        LocalProvider timeoutProvider(
            "http://127.0.0.1:8082", .1,
            [](auto, auto, auto, double) -> Json::Value { throw std::runtime_error("timeout"); });
        rejects([&] { timeoutProvider.decide(w, c); });
        LocalProvider valid("http://127.0.0.1:8083", .1, [](auto, auto path, auto b, double) {
            if (path == "/props")
                return parse(R"({"default_generation_settings":{"n_ctx":8192}})");
            if (path == "/apply-template")
                return parse(R"({"prompt":"fixture"})");
            if (path == "/tokenize")
                return parse(R"({"tokens":[1,2,3]})");
            check(b["response_format"]["type"] == "json_schema" &&
                      !b["chat_template_kwargs"]["enable_thinking"].asBool() &&
                      b["max_tokens"].asInt() == 768,
                  "Constrained request");
            return parse(
                R"({"choices":[{"message":{"content":"{\"actions\":[{\"text\":\"Open trade talks )"
                R"(with Portugal\"}]}"}}]})");
        });
        auto validDecision = valid.decide(w, c);
        check(validDecision.orders.size() == 1 &&
                  validDecision.orders[0].text == "Open trade talks with Portugal",
              "Valid model adapter");
        // A cancelled or overdue month stops before its next request; the deadline caps each
        // request's timeout.
        {
            double longest = 0;
            int requests = 0;
            bool fail = false;
            LocalProvider bounded(
                "http://127.0.0.1:8084", 10, [&](auto, auto path, auto, double t) -> Json::Value {
                    ++requests;
                    longest = std::max(longest, t);
                    if (fail) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(30));
                        throw std::runtime_error("timeout");
                    }
                    if (path == "/props")
                        return parse(R"({"default_generation_settings":)"
                                     R"({"n_ctx":8192}})");
                    return parse(R"({"choices":[{"message":{"content":)"
                                 R"("{\"actions\":[]}"}}]})");
                });
            auto message = [&] {
                try {
                    bounded.decide(w, c);
                } catch (const std::exception &e) {
                    return std::string(e.what());
                }
                return std::string();
            };
            bounded.requestCancel();
            check(message() == "Cancelled by the player" && requests == 0, "Cancel before request");
            bounded.clearCancel();
            bounded.setDeadline(std::chrono::steady_clock::now() + std::chrono::seconds(2));
            check(message().empty() && longest > 0 && longest <= 2, "Deadline caps timeout");
            bounded.setDeadline(std::chrono::steady_clock::now() - std::chrono::seconds(1));
            requests = 0;
            check(message() == "Month exceeded its time budget" && requests == 0, "Past deadline");
            // A request cut short by the deadline reports the budget, not the transport.
            bounded.setDeadline(std::chrono::steady_clock::now() + std::chrono::milliseconds(20));
            fail = true;
            check(message() == "Month exceeded its time budget", "Budget error after timeout");
            bounded.clearDeadline();
            check(message() == "timeout", "Transport error without deadline");
        }
        // Adjudication reasons only for actions about war, land or treaties, and only when the
        // context can hold the reasoning.
        for (int ctx : {8192, 32768})
            for (std::string action : {"Wait", "Invade Spain", "Drive straight to the capital"}) {
                Json::Value seen;
                LocalProvider judge(
                    "http://127.0.0.1:8084", .1, [&](auto, auto path, auto b, double) {
                        if (path == "/props") {
                            Json::Value j;
                            j["default_generation_settings"]["n_ctx"] = ctx;
                            return j;
                        }
                        if (path == "/apply-template")
                            return parse(R"({"prompt":"fixture"})");
                        if (path == "/tokenize")
                            return parse(R"({"tokens":[1,2,3]})");
                        seen = b;
                        return parse(
                            R"({"choices":[{"message":{"content":"{\"events\":[{\"actor\":\"PT\",)"
                            R"(\"blocked_by\":\"none\",\"causes\":[],\"description\":\"Waited\",)"
                            R"(\"operations\":[]}]}","reasoning_content":"Thinking"}}]})");
                    });
                Json::Value input;
                input["intentions"][0]["actor"] = "PT";
                input["intentions"][0]["action"] = action;
                if (action == "Drive straight to the capital")
                    input["facts"]["actors"]["PT"]["offensive"]["regions"].append("border");
                judge.adjudicate(w, input, {});
                auto audit = judge.takeAudit();
                bool thinks = ctx == 32768 && action != "Wait";
                check(seen["chat_template_kwargs"]["enable_thinking"].asBool() == thinks &&
                          seen["max_tokens"].asInt() == (thinks ? 1536 + 384 : 1536) &&
                          seen.isMember("thinking_budget_tokens") == thinks &&
                          audit[0]["context_limit"].asInt() == ctx,
                      "Thinking budget follows context size, actions and state-derived options");
            }
        rejects([&] { LocalProvider bad("", 1); });
        rejects([&] { LocalProvider bad("not-a-url", 1); });
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
        historyTests();
        std::cout << checks << " checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
