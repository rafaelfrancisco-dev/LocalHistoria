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
        for (auto a : {"PT", "ES", "FR"})
            w.countries[a] = {};
        auto r = resolve(
            w,
            {{"PT",
              {{"economy", "", ""}, {"army", "", ""}, {"navy", "", ""}, {"improve", "ES", ""}}}});
        check(r.state.countries["PT"].treasury == 55, "Costs and income");
        check(r.state.countries["PT"].income == 12, "Investment");
        check(r.state.countries["ES"].treasury == 110, "All actors administration");
        check(r.state.countries["PT"].army == 13 && r.state.countries["PT"].navy == 13,
              "Military investment");
        check(dump(encode(resolve(w, {{"PT",
                                       {{"navy", "", ""},
                                        {"improve", "ES", ""},
                                        {"army", "", ""},
                                        {"economy", "", ""}}}})
                              .state)) == dump(encode(r.state)),
              "Stable resolution");
        check(dump(encode(decode(encode(r.state)))) == dump(encode(r.state)), "Serialization");
        rejects([&] { validate(w, "PT", {{"improve", "ZZ", ""}}); });
        rejects([&] { validate(w, "PT", {{"improve", "PT", ""}}); });
        rejects([&] { validate(w, "PT", {{"accept", "ES", ""}}); });
        rejects([&] { validate(w, "PT", {{"posture", "", "hostile"}}); });
        rejects([&] { parseOrders(parse(R"([{"action":"army","treasury":999}])")); });
        rejects([&] { parseOrders(parse(R"({"orders":[]})")); });
        rejects([&] { parseOrders(parse(R"([{"action":9}])")); });
        auto poor = w;
        poor.countries["PT"].treasury = 19;
        rejects([&] { resolve(poor, {{"PT", {{"economy", "", ""}}}}); });
        auto offered = resolve(w, {{"PT", {{"offer", "ES", ""}, {"improve", "ES", ""}}}});
        check(offered.state.offers.contains("PT:ES"), "Offer persisted");
        auto accepted = resolve(offered.state, {{"ES", {{"accept", "PT", ""}}}});
        check(accepted.state.alliances.contains("ES:PT") && accepted.state.offers.empty(),
              "Acceptance lifecycle");
        auto noRelation = resolve(w, {{"PT", {{"offer", "FR", ""}}}});
        auto rejected = resolve(noRelation.state, {{"FR", {{"reject", "PT", ""}}}});
        check(rejected.state.offers.empty() && rejected.state.alliances.empty(),
              "Rejection lifecycle");
        rejects([&] { validate(offered.state, "PT", {{"offer", "ES", ""}}); });
        auto crossed = resolve(w, {{"PT", {{"offer", "ES", ""}}}, {"ES", {{"offer", "PT", ""}}}});
        auto crossedAccepted = resolve(crossed.state, {{"ES", {{"accept", "PT", ""}}}});
        check(crossedAccepted.state.offers.empty() &&
                  crossedAccepted.state.alliances.contains("ES:PT"),
              "Reciprocal offers clear on acceptance");
        auto prioritized = resolve(w, {{"PT", {{"priority", "", "prosperity"}}}});
        check(prioritized.state.countries.at("PT").treasury == 110,
              "New priority waits until next administration");
        check(resolve(prioritized.state, {}).state.countries.at("PT").treasury == 121,
              "Prosperity administration");
        auto c = context(offered.state, "ES", {}, {});
        LocalProvider malformed("http://127.0.0.1:8081", .1, [](auto, auto, auto, double) {
            return parse(
                R"({"choices":[{"message":{"content":"{\"orders\":[{\"action\":\"invade\"}]}"}}]})");
        });
        rejects([&] { malformed.decide(offered.state, c); });
        LocalProvider timeoutProvider("http://127.0.0.1:8082", .1,
                                      [](auto, auto, auto, double) -> Json::Value {
                                          throw std::runtime_error("timeout");
                                      });
        rejects([&] { timeoutProvider.decide(offered.state, c); });
        LocalProvider valid("http://127.0.0.1:8083", .1, [](auto, auto, auto b, double) {
            check(b["response_format"]["type"] == "json_schema" &&
                      !b["chat_template_kwargs"]["enable_thinking"].asBool(),
                  "Constrained request");
            return parse(
                R"({"choices":[{"message":{"content":"{\"orders\":[{\"action\":\"accept\",\"target\":\"PT\"}]}"}}]})");
        });
        auto validDecision = valid.decide(offered.state, c);
        check(validDecision.orders.size() == 1 && validDecision.orders[0].action == "accept" &&
                  validDecision.orders[0].target == "PT",
              "Valid model adapter");
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
            rejects([&] { timeoutProvider.narrate(r.events); });
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
        check(recorded["history_excerpt"].asString().find("History") != std::string::npos,
              "Separate history extraction");
        std::cout << checks << " checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
