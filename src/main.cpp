#include "localhistoria/history.h"
#include "localhistoria/providers.h"
#include "localhistoria/store.h"
#include <condition_variable>
#include <algorithm>
#include <cmath>
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
        std::string configPath, llamaFlagValue;
        bool llamaFlagGiven = false;
        const std::string prefix = "--llama-endpoint=";
        for (int i = 1; i < argc; i++) {
            std::string arg = argv[i];
            if (arg.rfind(prefix, 0) == 0) {
                llamaFlagValue = arg.substr(prefix.size());
                llamaFlagGiven = true;
            } else if (configPath.empty()) {
                configPath = arg;
            } else {
                throw std::runtime_error("Unexpected argument: " + arg);
            }
        }
        Json::Value config;
        if (!configPath.empty()) {
            std::ifstream f(configPath);
            if (!f)
                throw std::runtime_error("Cannot read config");
            std::string s((std::istreambuf_iterator<char>(f)), {});
            config = parse(s);
        }
        if (!config.isNull() && !config.isObject())
            throw ValidationError("Config must be an object");
        auto timeoutSeconds = config.get("timeout_seconds", 20).asDouble();
        if (timeoutSeconds <= 0 || timeoutSeconds > 60)
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
        Json::Value geography(Json::objectValue);
        for(auto &r:reference.query("SELECT id,geometry FROM regions"))geography[r["id"].asString()]=parse(r["geometry"].asString());
        std::string endpoint = llamaFlagGiven
                                   ? validateEndpoint(llamaFlagValue)
                                   : validateEndpoint(store.setting("llama_endpoint",
                                                                    "http://127.0.0.1:8081"));
        if (llamaFlagGiven)
            store.setSetting("llama_endpoint", endpoint);
        std::mutex mutex;
        LocalProvider provider(endpoint, timeoutSeconds);
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
                        auto port = std::to_string(config.get("port", 8080).asInt());
                        if ((host != "127.0.0.1:" + port && host != "localhost:" + port) ||
                            (!origin.empty() && origin != "http://" + host))
                            throw ValidationError("Use the local 127.0.0.1 or localhost origin");
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
                            if (path == "/api/v1/model-config")
                                fields = {{"llama_endpoint", "string"}};
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
                      for (auto &c : reference.query("SELECT code,name FROM countries ORDER BY code")) {
                          auto id=c["code"].asString();w.countries[id] = {};
                          w.actors[id]["name"]=c["name"];
                          unsigned hash=0;for(unsigned char ch:id)hash=hash*31+ch;
                          char color[8];snprintf(color,sizeof(color),"#%06x",0x405050+(hash*7919)%0x909090);
                          w.actors[id]["color"]=color;
                      }
                      for(auto &r:reference.query("SELECT id,name,affiliation,kind FROM regions")) {
                          auto &t=w.territories[r["id"].asString()];t["name"]=r["name"];t["initial_affiliation"]=r["affiliation"];t["owner"]=r["affiliation"];t["controller"]=r["affiliation"];t["kind"]=r["kind"];
                      }
                      for(auto &r:reference.query("SELECT * FROM region_interiors")) {auto &t=w.territories[r["id"].asString()];t["longitude"]=r["longitude"];t["latitude"]=r["latitude"];}
                      for(auto &c:reference.query("SELECT * FROM (SELECT c.*,r.affiliation,ROW_NUMBER() OVER(PARTITION BY r.affiliation ORDER BY population DESC,c.id) AS rank FROM region_cities c JOIN regions r ON r.id=c.region WHERE r.affiliation!='') WHERE rank<=3")) {
                          auto &e=w.entities[c["id"].asString()];for(auto k:{"name","region","latitude","longitude"})e[k]=c[k];e["actor"]=c["affiliation"];e["kind"]="city";e["status"]="active";e["origin_event"]="initial";
                      }
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
                  j["llama_endpoint"] = provider.endpointValue();
                  reply(cb, j);
              },
              {Get, Post});
        route("/api/v1/model-config",
              [&](auto, auto b, auto cb) {
                  if (!b["llama_endpoint"].isString())
                      throw ValidationError("llama_endpoint is required");
                  auto endpoint = validateEndpoint(b["llama_endpoint"].asString());
                  provider.setEndpoint(endpoint);
                  {
                      std::lock_guard l(mutex);
                      store.setSetting("llama_endpoint", endpoint);
                  }
                  Json::Value j;
                  j["llama_endpoint"] = endpoint;
                  reply(cb, j);
              },
              {Post});
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
                  if (rows.empty()) {
                      auto w=decode(store.campaign());if(!w.actors.isMember(code))throw ValidationError("Unknown country");
                      Json::Value j=w.actors[code];j["cities"]=Json::arrayValue;j["background"]["available"]=false;j["background"]["reason"]="Fictional campaign actor";reply(cb,j);return;
                  }
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
                      if (w.countries.contains(o.target)) actors.insert(o.target);
                      else if(w.territories.isMember(o.target)) {auto a=w.territories[o.target]["owner"].asString();if(!a.empty())actors.insert(a);}
                      else if(w.entities.isMember(o.target))actors.insert(w.entities[o.target]["actor"].asString());
                  for (auto &o : w.offers) {
                      actors.insert(o.substr(0, o.find(':')));
                      actors.insert(o.substr(o.find(':')+1));
                  }
                  // Ongoing disputes and commitments take priority; rotate neighbors deterministically.
                  for(auto collection:{w.conflicts,w.commitments})for(auto &v:collection)if(v["status"]=="open")for(auto &a:v["actors"])actors.insert(a.asString());
                  auto neighbors=reference.query("SELECT b FROM neighbors WHERE a=? ORDER BY b",{w.player});
                  for(Json::ArrayIndex n=0;n<neighbors.size()&&n<1;++n)actors.insert(neighbors[(n+w.turn)%neighbors.size()]["b"].asString());
                  if(actors.empty() && !w.countries.empty()) {
                      std::vector<std::string> rotation;for(auto &[a,c]:w.countries)if(a!=w.player)rotation.push_back(a);
                      for(size_t n=0;n<rotation.size()&&actors.empty();++n)actors.insert(rotation[(w.turn+n)%rotation.size()]);
                  }
                  actors.erase(w.player);
                  refs[w.player] = store.background(w.player);
                  for (auto &a : actors)
                      refs[a] = store.background(a);
                  std::map<std::string,CountryContext> contexts;
                  contexts[w.player]=store.campaignContext(w,w.player);
                  for(auto &a:actors)contexts[a]=store.campaignContext(w,a);
                  try {
                      worker.add([&, w, orders, id, actors, refs, contexts] {
                          bool committed = false;
                          try {
                              std::map<std::string, TurnOrders> intentions{{w.player, orders}};
                              for (auto &a : actors) {
                                  auto c = contexts.at(a);
                                  intentions[a] = provider.decide(w, c).orders;
                              }
                              auto result = resolve(w, intentions);
                              Json::Value adjudication;
                              adjudication["facts"]["turn"]=result.state.turn;
                              adjudication["facts"]["actors"]=Json::objectValue;
                              auto post=encode(result.state);
                              for(auto &[a,c]:contexts) {
                                  auto facts=c.facts;facts["state"]=post["countries"][a];
                                  for(auto k:{"territories","entities","available_targets","available_actions","objective"})facts.removeMember(k);
                                  adjudication["facts"]["actors"][a]=facts;
                                  adjudication["reference_only"][a]=c.reference;
                              }
                              adjudication["facts"]["territories"]=Json::objectValue;
                              adjudication["facts"]["entities"]=Json::objectValue;
                              // Only regions the intentions or open conflicts touch: a 4B model adjudicates far more reliably from a short, relevant prompt.
                              std::set<std::string> relevant;
                              for(auto &[a,o]:intentions)for(auto &order:o) {
                                  if(w.territories.isMember(order.target))relevant.insert(order.target);
                                  if(w.entities.isMember(order.target))relevant.insert(w.entities[order.target]["region"].asString());
                              }
                              for(auto &id:w.conflicts.getMemberNames())if(w.conflicts[id]["status"]=="open")for(auto &r:w.conflicts[id]["regions"])if(r.isString()&&w.territories.isMember(r.asString()))relevant.insert(r.asString());
                              for(auto &id:relevant) {
                                  auto t=w.territories[id];t.removeMember("longitude");t.removeMember("latitude");
                                  adjudication["facts"]["territories"][id]=t;
                              }
                              for(auto &id:w.entities.getMemberNames())if(relevant.contains(w.entities[id]["region"].asString())) {
                                  auto e=w.entities[id];e.removeMember("longitude");e.removeMember("latitude");
                                  adjudication["facts"]["entities"][id]=e;
                              }
                              // Readable sentences: raw order names such as "conflict" mislead a small model into requiring a prior conflict.
                              for(auto &[a,o]:intentions) {
                                  adjudication["intentions"][a]=Json::arrayValue;
                                  for(auto &order:o) {
                                      static const std::map<std::string,std::string> labels{{"construction","Construction"},{"movement","Move forces or entity"},{"conflict","Military action"},{"territorial_diplomacy","Territorial diplomacy"}};
                                      auto label=labels.contains(order.action)?labels.at(order.action):order.action;
                                      std::string target=order.target;
                                      if(w.territories.isMember(target))target=w.territories[target]["name"].asString()+" (region "+target+")";
                                      else if(w.entities.isMember(target))target=w.entities[target]["name"].asString()+" (entity "+target+")";
                                      adjudication["intentions"][a].append(label+(target.empty()?"":" targeting "+target)+(order.value.empty()?"":": "+order.value));
                                  }
                              }
                              auto delta=provider.adjudicate(result.state,adjudication);
                              result=applyDelta(result,delta,geography);
                              auto frozen=refs;frozen["delta"]=encode(delta);
                              frozen["contexts"]=adjudication;
                              Json::Value recorded;
                              for (auto &[a, o] : intentions)
                                  recorded[a] = encode(o);
                              {
                                  std::lock_guard l(mutex);
                                  store.recordInference(id,provider.takeAudit());
                                  store.commit(id, result, recorded, frozen, false);
                                  committed = true;
                              }
                              try {
                                  CountryContext narrationContext;
                                  {std::lock_guard l(mutex);narrationContext=store.campaignContext(result.state,result.state.player);}
                                  auto text = provider.narrate(result.events,narrationContext);
                                  std::lock_guard l(mutex);
                                  store.narrate(id, text, false);
                                  store.recordInference(id,provider.takeAudit());
                              } catch (...) { std::lock_guard l(mutex);store.recordInference(id,provider.takeAudit()); }
                          } catch (const std::exception &e) {
                              if (!committed) {
                                  std::lock_guard l(mutex);
                                  store.recordInference(id,provider.takeAudit());
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
                  auto c = store.campaignContext(w, w.player);
                  worker.add([&, w, c, b, id] {
                      Json::Value j;
                      j["id"] = id;
                      try {
                          auto p = provider.propose(w, c, b["text"].asString());
                          j["orders"] = encode(p.orders);
                          j["message"] = p.status;
                          j["expected_turn"] = w.turn;
                          j["campaign_id"] = w.campaignId;
                          j["status"] = "complete";
                      } catch (const std::exception &e) {
                          j["status"] = "failed";
                          j["error"] = e.what();
                      }
                      std::lock_guard l(mutex);
                      store.recordInference(id,provider.takeAudit());
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
        route("/api/v1/inference", [&](auto req,auto,auto cb) {
            std::lock_guard l(mutex);auto rows=store.query("SELECT record FROM inference WHERE job=? ORDER BY id",{req->getParameter("job")});Json::Value records(Json::arrayValue);for(auto &row:rows)records.append(parse(row["record"].asString()));reply(cb,records);
        },{Get});
        route("/api/v1/map", [&](auto,auto,auto cb) {
            std::lock_guard l(mutex);auto w=decode(store.campaign());Json::Value j;j["geometry_url"]="/regions.geojson";j["territories"]=w.territories;j["turn"]=w.turn;reply(cb,j);
        },{Get});
        route("/api/v1/entities", [&](auto req,auto,auto cb) {
            std::lock_guard l(mutex);auto w=decode(store.campaign());Json::Value j(Json::arrayValue);
            auto number=[&](const char *key,double fallback){auto p=req->getParameter(key);if(p.empty())return fallback;size_t end=0;double x=std::stod(p,&end);if(end!=p.size()||!std::isfinite(x))throw ValidationError("Invalid viewport");return x;};
            double west=number("west",-180),east=number("east",180),south=number("south",-90),north=number("north",90),zoom=number("zoom",2);
            if(west>east||south>north)throw ValidationError("Invalid viewport bounds");
            auto wanted=req->getParameter("id");
            for(auto &id:w.entities.getMemberNames()) {auto e=w.entities[id];double x=e["longitude"].asDouble(),y=e["latitude"].asDouble();
                if((!wanted.empty()&&id==wanted)||(wanted.empty()&&x>=west&&x<=east&&y>=south&&y<=north&&(zoom>=4||e["kind"]!="city"))) {e["id"]=id;j.append(e);if(j.size()>=200)break;}}
            reply(cb,j);
        },{Get});
        route("/api/v1/prepare", [&](auto,auto b,auto cb) {
            std::lock_guard l(mutex);auto w=decode(store.campaign());if(w.player.empty())throw ValidationError("Start a campaign first");
            std::set<std::string> actors{w.player};for(auto &r:reference.query("SELECT b FROM neighbors WHERE a=?",{w.player}))actors.insert(r["b"].asString());
            for(auto collection:{w.conflicts,w.commitments})for(auto &v:collection)for(auto &a:v["actors"])actors.insert(a.asString());
            if(b.isNull()){Json::Value coverage;for(auto &a:actors)coverage[a]=store.background(a);reply(cb,coverage);return;}
            auto id="prepare-"+std::to_string(++auxiliaryId);Json::Value j;j["id"]=id;j["status"]="running";auxiliary[id]=j;
            worker.add([&,actors,id]{for(auto &a:actors){auto result=fetchHistory(a);std::lock_guard l(mutex);store.cache(a,result);auxiliary[id]["coverage"][a]=result;}std::lock_guard l(mutex);auxiliary[id]["status"]="complete";});reply(cb,j,202);
        },{Get,Post});
        route("/api/v1/replay",
              [&](auto, auto, auto cb) {
                  std::lock_guard l(mutex);
                  Json::Value j;
                  j["campaign"] = store.campaign();
                  auto initial = store.query("SELECT state FROM initial_world WHERE id=1");
                  j["initial_state"] =
                      initial.empty() ? Json::Value() : parse(initial[0]["state"].asString());
                  j["geography"] = Json::objectValue;

                  for(auto &row:store.query("SELECT reference FROM turns")) {
                      auto refs=parse(row["reference"].asString());
                      for(auto &change:refs["delta"]["changes"])if(change["kind"]=="entity") {auto region=change["after"]["region"].asString();j["geography"][region]=geography[region];}
                  }
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
