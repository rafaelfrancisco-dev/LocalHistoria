#include "localhistoria/turn.h"
#include <iostream>
using namespace lh;
static void check(bool ok, const std::string &what) {
    if (!ok)
        throw std::runtime_error(what);
}
// Answers like tests/integration.py's mock server: every action succeeds without operations.
static Json::Value model(const std::string &, const std::string &path, const Json::Value &body,
                         double) {
    if (path == "/props")
        return parse(R"({"default_generation_settings":{"n_ctx":8192}})");
    if (path == "/apply-template")
        return parse(R"({"prompt":"fixture"})");
    if (path == "/tokenize")
        return parse(R"({"tokens":[1,2,3]})");
    auto input = parse(body["messages"][1]["content"].asString());
    Json::Value result;
    if (input.isMember("intentions")) {
        result["events"] = Json::arrayValue;
        for (auto &i : input["intentions"]) {
            Json::Value e;
            e["actor"] = i["actor"];
            e["blocked_by"] = "none";
            e["description"] = i["action"];
            e["causes"] = Json::arrayValue;
            e["operations"] = Json::arrayValue;
            if (i["regional_actor"].isString())
                e["operations"].append(
                    parse(R"({"action":"regional_initiative","region":")" +
                          i["regional_actor"].asString() +
                          R"(","agenda":"Local civic representation","relations":"cooperative",)"
                          R"("demand":"Request a regional assembly"})"));
            result["events"].append(e);
        }
    } else if (input.isMember("authoritative_events"))
        result["text"] = "A quiet month of fictional administration.";
    else if (input.isMember("campaign_events"))
        result["summary"] = "Fixture chronicle";
    else
        result["actions"].append(parse(R"({"text":"Discuss a civic initiative"})"));
    Json::Value response;
    response["choices"][0]["message"]["content"] = dump(result);
    return response;
}
int main() {
    try {
        WorldState w;
        w.player = "PT";
        w.campaignId = "turn-test";
        w.actors = parse(R"({"PT":{"name":"Portugal","color":"#123456"},)"
                         R"("ES":{"name":"Spain","color":"#654321"}})");
        w.territories =
            parse(R"({"r-PT":{"name":"Lusitania","owner":"PT","controller":"PT",)"
                  R"("initial_affiliation":"PT","kind":"adm1","longitude":-8,"latitude":40},)"
                  R"("r-ES":{"name":"Castile","owner":"ES","controller":"ES",)"
                  R"("initial_affiliation":"ES","kind":"adm1","longitude":-4,"latitude":40}})");
        Adjacency adjacency{{"r-PT", {"r-ES"}}, {"r-ES", {"r-PT"}}};
        Json::Value geography(Json::objectValue);
        Store store(":memory:"), reference(":memory:");
        reference.sql(
            "CREATE TABLE neighbors(a TEXT,b TEXT); CREATE TABLE countries(code TEXT,name "
            "TEXT); INSERT INTO neighbors VALUES('PT','ES'),('ES','PT'); INSERT INTO "
            "countries VALUES('PT','Portugal'),('ES','Spain');");
        store.create(w, false);
        std::mutex mutex;
        LocalProvider provider("http://127.0.0.1:8081", 1, model);
        ReferencePreparer references(store, reference, mutex,
                                     [](auto &, auto &, auto &, double) -> Json::Value {
                                         throw std::runtime_error("Reference network disabled");
                                     });
        TurnRunner runner({store, reference, provider, references, geography, adjacency, mutex});
        TurnOrders orders{{"Negotiate a trade agreement with Spain", {"ES"}}};
        std::string id;
        TurnPlan plan;
        {
            std::lock_guard lock(mutex);
            id = store.submit(0, "turn-test-key", "{}")["id"].asString();
            plan = runner.plan(decode(store.campaign()), orders);
        }
        check(plan.actors == std::set<std::string>{"ES"}, "Spain, named by the player, reacts");
        runner.run(plan, id);
        // Queued reference fetches take the mutex; stop them first.
        references.finish();
        std::lock_guard lock(mutex);
        auto job = store.job(id);
        check(job["status"] == "complete", "Job completes: " + dump(job));
        check(decode(store.campaign()).turn == 1, "The month advances");
        std::set<std::string> kinds;
        for (auto &row : store.query("SELECT record FROM inference WHERE job=?", {id}))
            kinds.insert(parse(row["record"].asString())["kind"].asString());
        for (auto kind : {"chronicle", "decide", "adjudicate", "narrate"})
            check(kinds.contains(kind), std::string("Inference recorded: ") + kind);
        std::cout << "Turn tests passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
