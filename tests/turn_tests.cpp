#include "localhistoria/turn.h"
#include "turn/worker.h"
#include <iostream>
#include <thread>
#include <cmath>
#ifndef _WIN32
#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
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
    else if (body["messages"][0]["content"].asString().find("symbolic gesture") !=
             std::string::npos)
        // The re-ask an independent slot gets when its first answer was a ceremony.
        result["actions"].append(parse(R"({"text":"Demand a share of the mining royalties"})"));
    else if (input["facts"]["initiative_type"] == "independent_regional")
        result["actions"].append(parse(R"({"text":"Convene a town hall on local heritage"})"));
    else
        result["actions"].append(parse(R"({"text":"Discuss a civic initiative"})"));
    Json::Value response;
    response["choices"][0]["message"]["content"] = dump(result);
    return response;
}
#ifndef _WIN32
// llama-server over a real socket. While `block` is set a completion reports that it started, then
// holds its connection open until the client disconnects, as a long generation would.
struct BlockingModel {
    int listener = -1, port = 0;
    std::atomic<bool> stop{false};
    std::mutex m;
    std::condition_variable cv;
    bool block = true;
    int completions = 0, started = 0, disconnected = 0;
    std::thread acceptor;
    std::vector<std::thread> connections;
    BlockingModel() {
        listener = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        socklen_t n = sizeof a;
        check(::bind(listener, reinterpret_cast<sockaddr *>(&a), n) == 0 && ::listen(listener, 8) == 0 &&
                  ::getsockname(listener, reinterpret_cast<sockaddr *>(&a), &n) == 0,
              "Mock model listens");
        port = ntohs(a.sin_port);
        acceptor = std::thread([this] {
            while (!stop) {
                pollfd p{listener, POLLIN, 0};
                if (::poll(&p, 1, 50) <= 0)
                    continue;
                int fd = ::accept(listener, nullptr, nullptr);
                if (fd < 0)
                    continue;
                // Bounds a failing test instead of hanging it.
                timeval limit{5, 0};
                ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &limit, sizeof limit);
                std::lock_guard l(m);
                connections.emplace_back([this, fd] { serve(fd); });
            }
        });
    }
    ~BlockingModel() {
        stop = true;
        acceptor.join();
        for (auto &t : connections)
            t.join();
        ::close(listener);
    }
    template <class P> bool waitFor(P done, double seconds) {
        std::unique_lock l(m);
        return cv.wait_for(l, std::chrono::duration<double>(seconds), done);
    }
    void serve(int fd) {
        std::string in;
        char buffer[4096];
        size_t length = std::string::npos, head;
        while ((head = in.find("\r\n\r\n")) == std::string::npos || in.size() < head + 4 + length) {
            auto got = ::recv(fd, buffer, sizeof buffer, 0);
            if (got <= 0) {
                ::close(fd);
                return;
            }
            in.append(buffer, got);
            if ((head = in.find("\r\n\r\n")) != std::string::npos && length == std::string::npos) {
                std::string headers = in.substr(0, head);
                for (auto &ch : headers)
                    ch = std::tolower(static_cast<unsigned char>(ch));
                auto at = headers.find("content-length:");
                length = at == std::string::npos ? 0 : std::stoul(headers.substr(at + 15));
            }
        }
        auto path = in.substr(in.find(' ') + 1, in.find(' ', in.find(' ') + 1) - in.find(' ') - 1);
        auto body = in.substr(head + 4, length);
        Json::Value answer;
        if (path == "/props")
            answer = parse(R"({"default_generation_settings":{"n_ctx":8192}})");
        else {
            bool hold = false;
            {
                std::lock_guard l(m);
                if (path == "/v1/chat/completions") {
                    ++completions;
                    hold = block;
                    started += hold;
                }
                cv.notify_all();
            }
            if (hold) {
                // Returns 0 once the client shuts its side; a timeout means it never did.
                auto got = ::recv(fd, buffer, 1, 0);
                std::lock_guard l(m);
                disconnected += got == 0 || (got < 0 && errno != EAGAIN && errno != EWOULDBLOCK);
                cv.notify_all();
                ::close(fd);
                return;
            }
            answer = model("", path, parse(body), 0);
        }
        auto text = dump(answer);
        auto out = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n"
                   "Content-Length: " + std::to_string(text.size()) + "\r\n\r\n" + text;
        ::send(fd, out.data(), out.size(), 0);
        ::close(fd);
    }
};
#endif
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
        TurnRunner runner({store, reference, provider, references, geography, adjacency, mutex, 480, 100});
        TurnOrders orders{{"Negotiate a trade agreement with Spain", {"ES"}}};
        std::string id;
        TurnPlan plan;
        {
            std::lock_guard lock(mutex);
            id = store.submit(0, "turn-test-key", "{}")["id"].asString();
            plan = runner.plan(decode(store.campaign()), orders);
        }
        check(plan.actors == std::set<std::string>{"ES"}, "Spain, named by the player, reacts");
        {
            std::lock_guard lock(mutex);
            auto retried = runner.plan(decode(store.campaign()), orders);
            check(retried.actors == plan.actors &&
                      store.query("SELECT actors FROM monthly_reactions").size() == 1,
                  "A retried month asks the same nations");
        }
        bool situated = false;
        for (auto &c : plan.independentContexts)
            if (c.facts["initiative_type"] == "independent_regional")
                situated = c.facts["region_situation"].isObject() &&
                           c.facts["region_situation"]["on_front"].isBool() &&
                           c.facts["region_situation"]["occupied"].isBool();
        check(situated, "A regional slot is told what its own ground is living through");
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
        // The ceremonial town hall was re-asked once and the substantive answer took the slot.
        auto recorded = dump(store.query("SELECT intentions FROM turns WHERE turn=1")[0]);
        check(recorded.find("mining royalties") != std::string::npos &&
                  recorded.find("town hall") == std::string::npos,
              "A ceremonial independent decision is re-asked once");
        for (const auto mode : {"no_reserve", "slow_chronicle", "cancel"}) {
            Store boundedStore(":memory:"); boundedStore.create(w, false);
            std::mutex boundedMutex;
            TurnRunner *running = nullptr;
            std::string boundedId;
            int chronicles = 0, mandatory = 0;
            LocalProvider boundedProvider("http://127.0.0.1:8081", 2,
                [&](const auto &url, const auto &path, const auto &body, double limit) -> Json::Value {
                    if (path == "/v1/chat/completions") {
                        const auto in = parse(body["messages"][1]["content"].asString());
                        if (in.isMember("campaign_events")) {
                            ++chronicles;
                            check(limit <= .3, "Combined optional allowance caps transport");
                            if (std::string(mode) == "slow_chronicle") {
                                std::this_thread::sleep_for(std::chrono::duration<double>(limit + .01));
                                throw std::runtime_error("slow optional request");
                            }
                        } else if (!in.isMember("authoritative_events")) {
                            ++mandatory;
                            if (std::string(mode) == "cancel") {
                                std::lock_guard guard(boundedMutex);
                                running->cancel(boundedId);
                            }
                        } else throw std::runtime_error("narration unavailable");
                    }
                    return model(url, path, body, limit);
                });
            ReferencePreparer boundedReferences(boundedStore, reference, boundedMutex,
                [](auto &, auto &, auto &, double) -> Json::Value { throw std::runtime_error("offline"); });
            TurnRunner boundedRunner({boundedStore, reference, boundedProvider, boundedReferences, geography, adjacency, boundedMutex, 2, std::string(mode) == "no_reserve" ? 0.0 : 1.0});
            running = &boundedRunner;
            auto boundedPlan = boundedRunner.plan(w, orders);
            boundedId = boundedStore.submit(0, mode, "{}")["id"].asString();
            boundedRunner.run(boundedPlan, boundedId);
            boundedReferences.finish();
            const bool cancelled = std::string(mode) == "cancel";
            check(boundedStore.job(boundedId)["status"] == (cancelled ? "failed" : "complete"), "Optional failure cannot invalidate committed work; cancellation cannot commit");
            check(decode(boundedStore.campaign()).turn == (cancelled ? 0 : 1), "Atomic cancellation");
            check(mandatory > 0 && (std::string(mode) != "no_reserve" || chronicles == 0), "No reserve skips refresh and still runs decisions");
            if (!cancelled) check(boundedStore.query("SELECT fallback FROM narration")[0]["fallback"].asInt() == 1, "Narration failure retains authoritative summary");
            Json::Value timing;
            for (const auto &row : boundedStore.query("SELECT record FROM inference")) {
                auto entry = parse(row["record"].asString());
                if (entry["kind"] == "turn_timing") timing = entry;
            }
            double total = 0;
            for (auto key : {"context_preparation_ms", "queue_ms", "chronicles_ms", "decisions_ms", "adjudication_preparation_ms", "adjudication_ms", "simulation_ms", "commit_ms", "post_commit_or_failure_ms"}) total += timing[key].asDouble();
            check(std::abs(total - timing["submission_to_finished_ms"].asDouble()) < 10, "Stage timings reconcile with elapsed time");
        }
        // Cancelling a month interrupts the model request it is waiting on: the connection closes,
        // the month fails without committing, and the provider takes the next month at once.
#ifndef _WIN32
        {
            BlockingModel server;
            Store liveStore(":memory:");
            liveStore.create(w, false);
            std::mutex liveMutex;
            LocalProvider live("http://127.0.0.1:" + std::to_string(server.port), 30);
            ReferencePreparer liveReferences(liveStore, reference, liveMutex,
                [](auto &, auto &, auto &, double) -> Json::Value { throw std::runtime_error("offline"); });
            TurnRunner liveRunner({liveStore, reference, live, liveReferences, geography, adjacency, liveMutex, 60, 0});
            std::string liveId;
            TurnPlan livePlan;
            {
                std::lock_guard l(liveMutex);
                liveId = liveStore.submit(0, "live-cancel", "{}")["id"].asString();
                livePlan = liveRunner.plan(w, orders);
            }
            std::thread month([&] { liveRunner.run(livePlan, liveId); });
            check(server.waitFor([&] { return server.started == 1; }, 5), "The model request starts");
            const auto cancelled = std::chrono::steady_clock::now();
            {
                std::lock_guard l(liveMutex);
                liveRunner.cancel(liveId);
            }
            month.join();
            const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - cancelled).count();
            check(seconds < 1, "Cancel releases the month within a second, took " + std::to_string(seconds));
            check(server.waitFor([&] { return server.disconnected == 1; }, 1), "The model server sees the request dropped");
            auto job = liveStore.job(liveId);
            check(job["status"] == "failed" && job["error"] == "Cancelled by the player", "Cancelled job fails: " + dump(job));
            check(decode(liveStore.campaign()).turn == 0 && liveStore.query("SELECT turn FROM turns").empty(), "A cancelled month never commits");
            check(server.completions == 1, "No retry after cancellation");
            bool audited = false;
            for (auto &row : liveStore.query("SELECT record FROM inference WHERE job=?", {liveId}))
                audited = audited || parse(row["record"].asString())["status"] == "cancelled";
            check(audited, "The interrupted attempt keeps its audit");
            server.block = false;
            auto next = liveStore.submit(0, "live-after-cancel", "{}")["id"].asString();
            liveRunner.run(liveRunner.plan(w, orders), next);
            liveReferences.finish();
            check(liveStore.job(next)["status"] == "complete" && decode(liveStore.campaign()).turn == 1,
                  "The next month runs without a restart");
        }
        // Shutdown interrupts a running month the same way and leaves it for a new submission.
        {
            BlockingModel server;
            Store liveStore(":memory:");
            liveStore.create(w, false);
            std::mutex liveMutex;
            LocalProvider live("http://127.0.0.1:" + std::to_string(server.port), 30);
            ReferencePreparer liveReferences(liveStore, reference, liveMutex,
                [](auto &, auto &, auto &, double) -> Json::Value { throw std::runtime_error("offline"); });
            TurnRunner liveRunner({liveStore, reference, live, liveReferences, geography, adjacency, liveMutex, 60, 0});
            auto liveId = liveStore.submit(0, "live-shutdown", "{}")["id"].asString();
            auto livePlan = liveRunner.plan(w, orders);
            std::thread month([&] { liveRunner.run(livePlan, liveId); });
            check(server.waitFor([&] { return server.started == 1; }, 5), "The model request starts");
            {
                std::lock_guard l(liveMutex);
                liveRunner.interrupt();
            }
            month.join();
            liveReferences.finish();
            check(liveStore.job(liveId)["status"] == "interrupted" && decode(liveStore.campaign()).turn == 0,
                  "Shutdown interrupts the month before commit");
        }
#endif
        // Preparation or queueing that fails after the job exists ends it failed with the first
        // error; its key repeats that answer and a new key can run.
        {
            Store failing(":memory:");
            failing.create(w, false);
            auto first = failing.submit(0, "prepare-fails", "{}")["id"].asString();
            std::string error;
            try {
                startSubmitted(failing, first, [] { throw ValidationError("Reference snapshot unavailable"); });
            } catch (const std::exception &e) {
                error = e.what();
            }
            auto job = failing.job(first);
            check(error == "Reference snapshot unavailable" && job["status"] == "failed" &&
                      job["error"] == error, "Preparation failure fails the job: " + dump(job));
            check(failing.submit(0, "prepare-fails", "{}")["status"] == "failed", "Its key repeats the failure");
            check(decode(failing.campaign()).turn == 0, "No state advances");
            Worker stopped(false);
            stopped.finish();
            auto second = failing.submit(0, "queue-rejects", "{}")["id"].asString();
            try {
                startSubmitted(failing, second, [&] { stopped.add([] {}); });
                check(false, "Queue rejection propagates");
            } catch (const ValidationError &e) {
                check(std::string(e.what()) == "Work queue full; retry later", "Queue error kept");
            }
            check(failing.job(second)["status"] == "failed", "Queue rejection fails the job");
            auto third = failing.submit(0, "queued", "{}")["id"].asString();
            Worker queue(false);
            startSubmitted(failing, third, [&] { queue.add([] {}); });
            check(failing.job(third)["status"] == "running", "An accepted job stays with the worker");
        }
        std::cout << "Turn tests passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
