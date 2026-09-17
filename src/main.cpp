#include "localhistoria/history.h"
#include "localhistoria/providers.h"
#include "localhistoria/store.h"
#include <chrono>
#include <condition_variable>
#include <drogon/drogon.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <queue>
#include <sys/file.h>
#include <thread>
#include <unistd.h>
using namespace lh;
using namespace drogon;
class Worker {
    std::mutex m;
    std::condition_variable cv;
    std::queue<std::function<void()>> tasks;
    bool stop = false;
    std::thread thread;

  public:
    Worker()
        : thread([this] {
              for (;;) {
                  std::function<void()> f;
                  {
                      std::unique_lock l(m);
                      cv.wait(l, [&] { return stop || !tasks.empty(); });
                      if (stop && tasks.empty())
                          return;
                      f = std::move(tasks.front());
                      tasks.pop();
                  }
                  try {
                      f();
                  } catch (const std::exception &e) {
                      LOG_ERROR << e.what();
                  }
              }
          }) {}
    ~Worker() {
        {
            std::lock_guard l(m);
            stop = true;
        }
        cv.notify_all();
        thread.join();
    }
    void add(std::function<void()> f) {
        std::lock_guard l(m);
        if (tasks.size() >= 8)
            throw ValidationError("Work queue full; retry later");
        tasks.push(std::move(f));
        cv.notify_one();
    }
};
int main(int argc, char **argv) {
    try {
        Json::Value config;
        if (argc > 1) {
            std::ifstream f(argv[1]);
            if (!f)
                throw std::runtime_error("Cannot read config");
            std::string s((std::istreambuf_iterator<char>(f)), {});
            config = parse(s);
        }
        if (!config.isNull() && !config.isObject())
            throw ValidationError("Config must be an object");
        auto modelEndpoint = config.get("llama_endpoint", "http://127.0.0.1:8081").asString();
        if (modelEndpoint.rfind("http://127.0.0.1:", 0) != 0 ||
            modelEndpoint.find_first_not_of("0123456789", 17) != std::string::npos)
            throw ValidationError("Model endpoint must be http://127.0.0.1:PORT");
        auto timeout = config.get("timeout_seconds", 20).asDouble();
        if (timeout <= 0 || timeout > 60)
            throw ValidationError("Timeout must be greater than zero and at most 60 seconds");
        std::string save = config.get("save_path", "saves/campaign.sqlite").asString();
        auto parent = std::filesystem::path(save).parent_path();
        if (!parent.empty())
            std::filesystem::create_directories(parent);
        int lockfd = open((save + ".lock").c_str(), O_CREAT | O_RDWR, 0600);
        if (lockfd < 0 || flock(lockfd, LOCK_EX | LOCK_NB) != 0)
            throw std::runtime_error("Save is already open by another server");
        Store store(save), reference("data/world.sqlite", true);
        store.recover();
        std::mutex mutex;
        LocalProvider provider(
            config.get("ai_enabled", false).asBool()
                ? config.get("llama_endpoint", "http://127.0.0.1:8081").asString()
                : "",
            config.get("timeout_seconds", 20).asDouble());
        std::map<std::string, Json::Value> auxiliary;
        unsigned auxiliaryId = 0;
        Worker worker;
        auto reply = [](auto cb, Json::Value j, int status = 200) {
            auto r = HttpResponse::newHttpJsonResponse(j);
            r->setStatusCode(static_cast<HttpStatusCode>(status));
            r->addHeader("Cache-Control", "no-store");
            r->addHeader("X-Content-Type-Options", "nosniff");
            cb(r);
        };
        using CB = std::function<void(const HttpResponsePtr &)>;
        auto route = [&](const std::string &path, auto handler, std::vector<HttpMethod> methods) {
            std::vector<internal::HttpConstraint> constraints;
            for (auto m : methods)
                constraints.emplace_back(m);
            std::function<void(const HttpRequestPtr &, CB &&)> endpoint =
                [&, handler, path](const HttpRequestPtr &req, CB &&cb) {
                    try {
                        auto host = req->getHeader("host");
                        auto origin = req->getHeader("origin");
                        if (host !=
                                "127.0.0.1:" + std::to_string(config.get("port", 8080).asInt()) ||
                            (!origin.empty() && origin != "http://" + host))
                            throw ValidationError("Use the local 127.0.0.1 origin");
                        Json::Value body;
                        if (req->method() == Post) {
                            if (req->getHeader("content-type").find("application/json") != 0)
                                throw ValidationError("Content-Type must be application/json");
                            body = parse(std::string(req->body()));
                            if (!body.isObject())
                                throw ValidationError("Expected JSON object");
                            std::map<std::string, std::string> fields;
                            if (path == "/api/v1/campaign")
                                fields = {{"country", "string"}, {"confirm_replace", "bool"}};
                            if (path == "/api/v1/turns")
                                fields = {{"campaign_id", "string"},
                                          {"expected_turn", "int"},
                                          {"idempotency_key", "string"},
                                          {"orders", "array"}};
                            if (path == "/api/v1/proposals")
                                fields = {{"campaign_id", "string"},
                                          {"expected_turn", "int"},
                                          {"text", "string"}};
                            if (path == "/api/v1/history")
                                fields = {{"code", "string"}};
                            for (auto &key : body.getMemberNames()) {
                                if (!fields.contains(key))
                                    throw ValidationError("Unknown request field: " + key);
                                auto type = fields.at(key);
                                auto &v = body[key];
                                if ((type == "string" && !v.isString()) ||
                                    (type == "bool" && !v.isBool()) ||
                                    (type == "int" && !v.isInt()) ||
                                    (type == "array" && !v.isArray()))
                                    throw ValidationError("Wrong type for field: " + key);
                            }
                        }
                        handler(req, body, cb);
                    } catch (const ValidationError &e) {
                        Json::Value j;
                        j["error"]["code"] = "validation";
                        j["error"]["message"] = e.what();
                        reply(cb, j, 409);
                    } catch (const std::exception &e) {
                        LOG_ERROR << e.what();
                        Json::Value j;
                        j["error"]["code"] = "internal";
                        j["error"]["message"] = "Operation failed; state was not partially applied";
                        reply(cb, j, 500);
                    }
                };
            app().registerHandler(path, std::move(endpoint), constraints);
        };
        route("/api/v1/campaign",
              [&](auto, auto b, auto cb) {
                  std::lock_guard l(mutex);
                  Json::Value j;
                  if (!b.isNull()) {
                      if (!b["country"].isString())
                          throw ValidationError("country is required");
                      WorldState w;
                      for (auto &c : reference.query("SELECT code FROM countries ORDER BY code"))
                          w.countries[c["code"].asString()] = {};
                      w.player = b["country"].asString();
                      if (!w.countries.contains(w.player))
                          throw ValidationError("Country is not playable");
                      w.campaignId = utils::getUuid();
                      store.create(w, b.get("confirm_replace", false).isBool() &&
                                          b.get("confirm_replace", false).asBool());
                  }
                  j["state"] = store.campaign();
                  j["journal"] = store.journal();
                  j["active_jobs"] =
                      store.query("SELECT id,status FROM jobs WHERE status='running'");
                  j["ai_mode"] = provider.enabled() ? "Local model with scripted fallback"
                                                    : "Scripted AI (offline)";
                  reply(cb, j);
              },
              {Get, Post});
        route("/api/v1/countries",
              [&](auto, auto, auto cb) {
                  std::lock_guard l(mutex);
                  reply(cb, reference.query("SELECT code,name FROM countries ORDER BY name"));
              },
              {Get});
        route("/api/v1/country",
              [&](auto req, auto, auto cb) {
                  std::lock_guard l(mutex);
                  auto code = req->getParameter("code");
                  auto rows = reference.query("SELECT * FROM countries WHERE code=?", {code});
                  if (rows.empty())
                      throw ValidationError("Unknown country");
                  auto j = rows[0];
                  j["cities"] = reference.query("SELECT name,population,city_wikidata FROM cities "
                                                "WHERE code=? ORDER BY population DESC LIMIT 15",
                                                {code});
                  j["background"] = store.background(code);
                  reply(cb, j);
              },
              {Get});
        route("/api/v1/jobs",
              [&](auto req, auto, auto cb) {
                  std::lock_guard l(mutex);
                  auto id = req->getParameter("id");
                  reply(cb, auxiliary.contains(id) ? auxiliary.at(id) : store.job(id));
              },
              {Get});
        route("/api/v1/turns",
              [&](auto, auto b, auto cb) {
                  std::lock_guard l(mutex);
                  if (!b["expected_turn"].isInt() || !b["idempotency_key"].isString() ||
                      b["idempotency_key"].asString().size() < 8 ||
                      b["idempotency_key"].asString().size() > 128)
                      throw ValidationError(
                          "Expected integer turn and 8–128 character idempotency key");
                  auto w = decode(store.campaign());
                  if (b["campaign_id"] != w.campaignId || w.campaignId.empty())
                      throw ValidationError("Stale campaign");
                  auto orders = parseOrders(b["orders"]);
                  auto payload = dump(b);
                  auto existing = store.query("SELECT id,payload FROM jobs WHERE key=?",
                                              {b["idempotency_key"].asString()});
                  if (!existing.empty()) {
                      if (existing[0]["payload"].asString() != payload)
                          throw ValidationError("Idempotency key payload mismatch");
                      reply(cb, store.job(existing[0]["id"].asString()), 202);
                      return;
                  }
                  validate(w, w.player, orders);
                  auto job = store.submit(b["expected_turn"].asInt(),
                                          b["idempotency_key"].asString(), payload);
                  auto id = job["id"].asString();
                  Json::Value refs(Json::objectValue);
                  std::set<std::string> actors;
                  for (auto &o : orders)
                      if (!o.target.empty())
                          actors.insert(o.target);
                  for (auto &o : w.offers) {
                      actors.insert(o.substr(0, 2));
                      actors.insert(o.substr(3));
                  }
                  actors.erase(w.player);
                  refs[w.player] = store.background(w.player);
                  for (auto &a : actors)
                      refs[a] = store.background(a);
                  auto recent = store.journal();
                  if (recent.size() > 2)
                      recent.resize(2);
                  try {
                      worker.add([&, w, orders, id, actors, refs, recent] {
                          bool committed = false;
                          try {
                              std::map<std::string, TurnOrders> intentions{{w.player, orders}};
                              bool fallback = false;
                              auto deadline =
                                  std::chrono::steady_clock::now() + std::chrono::seconds(60);
                              unsigned modelCalls = 0;
                              for (auto &a : actors) {
                                  auto c = context(w, a, recent, refs[a]);
                                  Decision d;
                                  if (provider.enabled() &&
                                      (modelCalls >= 8 ||
                                       std::chrono::steady_clock::now() +
                                               std::chrono::duration<double>(timeout) >
                                           deadline)) {
                                      d = {scripted(w, a), true,
                                           "Scripted fallback: turn inference budget"};
                                  } else {
                                      d = provider.decide(w, c);
                                      ++modelCalls;
                                  }
                                  intentions[a] = d.orders;
                                  fallback |= d.fallback;
                              }
                              auto result = resolve(w, intentions);
                              Json::Value recorded;
                              for (auto &[a, o] : intentions)
                                  recorded[a] = encode(o);
                              {
                                  std::lock_guard l(mutex);
                                  store.commit(id, result, recorded, refs, false, fallback);
                                  committed = true;
                              }
                              if (provider.enabled()) {
                                  try {
                                      auto text = provider.narrate(result.events);
                                      std::lock_guard l(mutex);
                                      store.narrate(id, text, false);
                                  } catch (...) { /* Factual summary already committed. */
                                  }
                              }
                          } catch (const std::exception &e) {
                              if (!committed) {
                                  std::lock_guard l(mutex);
                                  store.execute(
                                      "UPDATE jobs SET status='failed',error=? WHERE id=?",
                                      {e.what(), id});
                              }
                          }
                      });
                  } catch (...) {
                      store.execute("UPDATE jobs SET status='failed',error='Queue full' WHERE id=?",
                                    {id});
                      throw;
                  }
                  reply(cb, store.job(id), 202);
              },
              {Post});
        route("/api/v1/proposals",
              [&](auto, auto b, auto cb) {
                  std::lock_guard l(mutex);
                  auto w = decode(store.campaign());
                  if (w.player.empty() || b["campaign_id"] != w.campaignId ||
                      !b["expected_turn"].isInt() || b["expected_turn"].asInt() != w.turn)
                      throw ValidationError("Load the current campaign before proposing orders");
                  if (!b["text"].isString() || b["text"].asString().empty() ||
                      b["text"].asString().size() > 1000)
                      throw ValidationError("Provide 1–1000 bytes of instructions");
                  if (auxiliary.size() >= 128) {
                      for (auto it = auxiliary.begin(); it != auxiliary.end();) {
                          if (it->second["status"] != "running")
                              it = auxiliary.erase(it);
                          else
                              ++it;
                      }
                  }
                  auto id = "proposal-" + std::to_string(++auxiliaryId);
                  auto c = context(w, w.player, {}, {});
                  worker.add([&, w, c, b, id] {
                      Json::Value j;
                      j["id"] = id;
                      try {
                          auto p = provider.propose(w, c, b["text"].asString());
                          j["orders"] = encode(p.orders);
                          j["fallback"] = p.fallback;
                          j["message"] = p.status;
                          j["expected_turn"] = w.turn;
                          j["campaign_id"] = w.campaignId;
                          j["status"] = "complete";
                      } catch (const std::exception &e) {
                          j["status"] = "failed";
                          j["error"] = e.what();
                      }
                      std::lock_guard l(mutex);
                      auxiliary[id] = j;
                  });
                  Json::Value j;
                  j["id"] = id;
                  j["status"] = "running";
                  auxiliary[id] = j;
                  reply(cb, j, 202);
              },
              {Post});
        route("/api/v1/history",
              [&](auto req, auto b, auto cb) {
                  std::lock_guard l(mutex);
                  auto code = b.isNull() ? req->getParameter("code") : b.get("code", "").asString();
                  if (reference.query("SELECT code FROM countries WHERE code=?", {code}).empty())
                      throw ValidationError("Unknown country");
                  if (b.isNull()) {
                      reply(cb, store.background(code));
                      return;
                  }
                  if (auxiliary.size() >= 128) {
                      for (auto it = auxiliary.begin(); it != auxiliary.end();) {
                          if (it->second["status"] != "running")
                              it = auxiliary.erase(it);
                          else
                              ++it;
                      }
                  }
                  auto id = "history-" + std::to_string(++auxiliaryId);
                  worker.add([&, code, id] {
                      auto result = fetchHistory(code);
                      std::lock_guard l(mutex);
                      if (result["available"].asBool() || store.background(code).isNull())
                          store.cache(code, result);
                      Json::Value j;
                      j["id"] = id;
                      j["status"] = "complete";
                      j["result"] = result;
                      auxiliary[id] = j;
                  });
                  Json::Value j;
                  j["id"] = id;
                  j["status"] = "running";
                  auxiliary[id] = j;
                  reply(cb, j, 202);
              },
              {Get, Post});
        route("/api/v1/replay",
              [&](auto, auto, auto cb) {
                  std::lock_guard l(mutex);
                  Json::Value j;
                  j["campaign"] = store.campaign();
                  auto initial = store.query("SELECT state FROM initial_world WHERE id=1");
                  j["initial_state"] =
                      initial.empty() ? Json::Value() : parse(initial[0]["state"].asString());
                  j["turns"] = store.query("SELECT * FROM turns ORDER BY turn");
                  for (auto &t : j["turns"])
                      for (auto k : {"state", "intentions", "events", "reference"})
                          t[k] = parse(t[k].asString());
                  reply(cb, j);
              },
              {Get});
        app()
            .setLogLevel(trantor::Logger::kWarn)
            .setClientMaxBodySize(32768)
            .setDocumentRoot("web")
            .setFileTypes({"html", "js", "css", "svg", "png", "ico", "geojson"})
            .addListener("127.0.0.1", config.get("port", 8080).asInt())
            .setThreadNum(2);
        std::cout << "LocalHistoria: http://127.0.0.1:" << config.get("port", 8080).asInt()
                  << " (fictional sandbox)\n";
        app().run();
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
