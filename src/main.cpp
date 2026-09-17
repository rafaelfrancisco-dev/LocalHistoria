#include "localhistoria/history.h"
#include "localhistoria/politics.h"
#include "localhistoria/status.h"
#include <ctime>
#include <regex>
#include "localhistoria/providers.h"
#include "localhistoria/store.h"
#include <condition_variable>
#include <algorithm>
#include <cctype>
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
    bool drain;
    std::thread thread;

  public:
    explicit Worker(bool drain = true)
        : drain(drain), thread([this] {
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
    ~Worker() { finish(); }
    void finish() {
        {
            std::lock_guard l(m);
            stop = true;
            if (!drain) tasks = {};
        }
        cv.notify_all();
        if (thread.joinable()) thread.join();
    }
    void add(std::function<void()> f) {
        std::lock_guard l(m);
        if (stop || tasks.size() >= 8)
            throw ValidationError("Work queue full; retry later");
        tasks.push(std::move(f));
        cv.notify_one();
    }
};
const size_t maxAiActors = 4;
struct Mentions {
    std::set<std::string> actors, regions;
};
static std::string lower(const std::string &s) {
    std::string o;
    for (unsigned char c : s) o += char(std::tolower(c));
    return o;
}
static bool containsWord(const std::string &text, const std::string &word) {
    for (auto at = text.find(word); at != std::string::npos; at = text.find(word, at + 1)) {
        auto boundary = [&](size_t i) { return i >= text.size() || !std::isalnum(static_cast<unsigned char>(text[i])); };
        if ((at == 0 || boundary(at - 1)) && boundary(at + word.size())) return true;
    }
    return false;
}
// Free text names nations and regions; resolve those names to IDs so the model sees the relevant map.
static Mentions mentioned(const WorldState &w, const std::string &actor, const TurnOrders &orders) {
    Mentions m;
    std::string text;
    for (auto &o : orders) {
        text += lower(o.text) + "\n";
        for (auto &t : o.targets) {
            if (w.hasActor(t)) m.actors.insert(t);
            else if (w.territories.isMember(t)) m.regions.insert(t);
            else if (w.entities.isMember(t)) m.regions.insert(w.entities[t]["region"].asString());
        }
    }
    for (auto &id : w.actors.getMemberNames()) {
        auto name = lower(w.actors[id]["name"].asString());
        if (name.size() >= 4 && containsWord(text, name)) m.actors.insert(id);
    }
    std::map<std::string, std::vector<std::string>> byName;
    for (auto &id : w.territories.getMemberNames()) {
        auto name = lower(w.territories[id]["name"].asString());
        if (name.size() >= 4 && w.territories[id]["kind"] != "residual") byName[name].push_back(id);
    }
    for (auto &[name, ids] : byName) {
        if (!containsWord(text, name)) continue;
        std::vector<std::string> preferred;
        for (auto &id : ids) {
            auto owner = w.territories[id]["owner"].asString(), controller = w.territories[id]["controller"].asString();
            if (owner == actor || controller == actor || m.actors.contains(owner) || m.actors.contains(controller)) preferred.push_back(id);
        }
        auto &chosen = preferred.empty() ? ids : preferred;
        if (chosen.size() <= 3) m.regions.insert(chosen.begin(), chosen.end());
    }
    for (auto &id : m.regions)
        if (w.territories.isMember(id)) {
            auto owner = w.territories[id]["owner"].asString();
            if (w.hasActor(owner)) m.actors.insert(owner);
        }
    m.actors.erase(actor);
    return m;
}
static Json::Value adjudicationInput(const WorldState &w, const std::map<std::string, CountryContext> &contexts,
                                     const std::map<std::string, TurnOrders> &intentions, const Adjacency &adjacency) {
    Json::Value input;
    input["facts"]["turn"] = w.turn;
    input["facts"]["actors"] = Json::objectValue;
    std::set<std::string> acting, relevant;
    for (auto &[a, o] : intentions) {
        acting.insert(a);
        auto m = mentioned(w, a, o);
        relevant.insert(m.regions.begin(), m.regions.end());
        // Nations an actor addresses without naming a region: the parts of them it borders, where an invasion or handover would begin.
        for (auto &target : m.actors)
            for (auto &id : w.territories.getMemberNames())
                if (w.territories[id]["controller"] == target && w.territories[id]["kind"] != "residual" && relevant.size() < 40 && reaches(w, adjacency, a, id))
                    relevant.insert(id);
    }
    for (auto &[a, c] : contexts) {
        auto facts = c.facts;
        facts.removeMember("territories");
        facts["past_events"] = facts["recent_events"];
        facts.removeMember("recent_events");
        input["facts"]["actors"][a] = facts;
        input["reference_only"][a] = c.reference;
        for (auto k : {"occupied_by_others", "occupying"})
            for (auto &id : c.facts[k].getMemberNames()) relevant.insert(id);
    }
    // Fronts of wars this month's actors fight; distant wars would crowd the prompt with irrelevant regions.
    for (auto &id : w.conflicts.getMemberNames()) {
        auto &c = w.conflicts[id];
        bool involved = false;
        for (auto &a : c["actors"]) involved = involved || acting.contains(a.asString());
        if (c["status"] == "open" && involved)
            for (auto &r : c["affected_regions"])
                if (r.isString() && relevant.size() < 60) relevant.insert(r.asString());
    }
    // Where the acting nations' armies and fleets stand, so movement has somewhere to go.
    for (auto &id : w.entities.getMemberNames()) {
        auto &e = w.entities[id];
        if (acting.contains(e["actor"].asString()) && (e["kind"] == "army" || e["kind"] == "fleet") && e["status"] == "active")
            relevant.insert(e["region"].asString());
    }
    input["facts"]["territories"] = Json::objectValue;
    input["facts"]["entities"] = Json::objectValue;
    for (auto &id : relevant) {
        if (!w.territories.isMember(id)) continue;
        auto t = w.territories[id];
        for (auto k : {"longitude", "latitude", "initial_affiliation", "kind", "gdp_usd", "occupation_gdp"}) t.removeMember(k);
        t["within_reach_of"] = Json::arrayValue;
        for (auto &a : acting)
            if (reaches(w, adjacency, a, id)) t["within_reach_of"].append(a);
        input["facts"]["territories"][id] = t;
    }
    for (auto &id : w.entities.getMemberNames())
        if (relevant.contains(w.entities[id]["region"].asString())) {
            auto e = w.entities[id];
            for (auto k : {"longitude", "latitude", "origin_event"}) e.removeMember(k);
            input["facts"]["entities"][id] = e;
        }
    // The player's actions come first so the model's bounded reasoning reaches them.
    input["intentions"] = Json::arrayValue;
    std::vector<std::string> order{w.player};
    for (auto &[a, o] : intentions)
        if (a != w.player) order.push_back(a);
    for (auto &a : order) {
        if (!intentions.contains(a)) continue;
        for (auto &action : intentions.at(a)) {
            std::string targets;
            for (auto &t : action.targets) {
                auto name = w.territories.isMember(t) ? w.territories[t]["name"].asString() + " (region " + t + ")"
                          : w.entities.isMember(t)    ? w.entities[t]["name"].asString() + " (entity " + t + ")"
                                                      : w.actors[t]["name"].asString() + " (actor " + t + ")";
                targets += (targets.empty() ? "" : ", ") + name;
            }
            Json::Value item;
            item["actor"] = a;
            item["action"] = action.text + (targets.empty() ? "" : " (targets: " + targets + ")");
            input["intentions"].append(item);
        }
    }
    return input;
}
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
        // Adjudication reasons before answering, which takes minutes on a small local model.
        auto timeoutSeconds = config.get("timeout_seconds", 240).asDouble();
        if (timeoutSeconds <= 0 || timeoutSeconds > 600)
            throw ValidationError("Timeout must be greater than zero and at most 600 seconds");
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
        Adjacency adjacency;
        for(auto &r:reference.query("SELECT a,b FROM region_neighbors"))adjacency[r["a"].asString()].insert(r["b"].asString());
        // National statistics, regional shares and the January 2026 wars and treaties.
        StatusSeed statusSeed;
        for(auto &r:reference.query("SELECT * FROM country_stats")){auto code=r["code"].asString();r.removeMember("code");statusSeed.countries[code]=r;}
        for(auto &r:reference.query("SELECT * FROM region_stats")){auto id=r["id"].asString();r.removeMember("id");statusSeed.regions[id]=r;}
        auto seedRows=reference.query("SELECT document FROM world_seed WHERE id=1");
        if(statusSeed.countries.empty()||seedRows.empty())throw std::runtime_error("Reference database lacks national statistics; run make data");
        statusSeed.world=parse(seedRows[0]["document"].asString());
        std::string endpoint = llamaFlagGiven
                                   ? validateEndpoint(llamaFlagValue)
                                   : validateEndpoint(store.setting("llama_endpoint",
                                                                    "http://127.0.0.1:8081"));
        if (llamaFlagGiven)
            store.setSetting("llama_endpoint", endpoint);
        std::mutex mutex;
        LocalProvider provider(endpoint, timeoutSeconds);
        const bool referenceNetwork=config.get("reference_network",true).asBool();
        Transport referenceTransport=[referenceNetwork](const std::string &host,const std::string &path,const Json::Value &body,double timeout){
            if(!referenceNetwork)throw std::runtime_error("Reference network disabled");
            return httpJson(host,path,body,timeout);
        };
        std::map<std::string, Json::Value> auxiliary;
        unsigned auxiliaryId = 0;
        std::set<std::string> preparing;
        std::map<std::string,std::time_t> retryAfter;
        Worker referenceWorker(false);
        Worker worker;
        // Called under mutex. One bounded queue, deduplicated, with failures backed off.
        auto prepareReferences = [&](const WorldState &w) {
            std::vector<std::string> wanted;
            for(auto &slot:scheduleInitiatives(w)){wanted.push_back(slot["actor"].asString());if(slot["region"]!="")wanted.push_back(slot["region"].asString());}
            wanted.push_back(w.player);
            for(auto &n:reference.query("SELECT b FROM neighbors WHERE a=? ORDER BY b",{w.player}))wanted.push_back(n["b"].asString());
            for(auto &key:wanted) {
                if(preparing.size()>=8)break;
                if(key.empty()||preparing.contains(key)||store.background(key).get("available",false).asBool()||retryAfter[key]>std::time(nullptr))continue;
                auto cached=store.background(key);
                if(cached["retrieved_at"].isIntegral()&&cached["retrieved_at"].asInt64()+3600>std::time(nullptr))continue;
                bool regional=w.territories.isMember(key);
                if(!regional&&reference.query("SELECT code FROM countries WHERE code=?",{key}).empty())continue;
                auto region=regional?w.territories[key]:Json::Value();
                preparing.insert(key);
                try { referenceWorker.add([&,key,region,regional]{
                    Json::Value result;
                    try{result=regional?fetchRegionalHistory(region["name"].asString(),region["initial_affiliation"].asString(),referenceTransport):fetchHistory(key,referenceTransport);}
                    catch(const std::exception &e){result["available"]=false;result["reason"]=e.what();}
                    if(!result.get("available",false).asBool())result["retrieved_at"]=Json::Int64(std::time(nullptr));
                    std::lock_guard lock(mutex);
                    if(result.get("available",false).asBool()||!store.background(key).get("available",false).asBool())store.cache(key,result);
                    if(!result.get("available",false).asBool())retryAfter[key]=std::time(nullptr)+3600;
                    preparing.erase(key);
                }); } catch(const ValidationError &) {preparing.erase(key);break;}
            }
        };
        {std::lock_guard lock(mutex);prepareReferences(decode(store.campaign()));}
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
                          auto id=c["code"].asString();
                          w.actors[id]["name"]=c["name"];w.actors[id]["static_flag"]=id;
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
                      if (!w.hasActor(w.player))
                          throw ValidationError("Country is not playable");
                      seedStatus(w, statusSeed, true);
                      w.campaignId = utils::getUuid();
                      store.create(w, b.get("confirm_replace", false).isBool() &&
                                          b.get("confirm_replace", false).asBool());
                  }
                  prepareReferences(decode(store.campaign()));
                  j["state"] = store.campaign();
                  if (!j["state"].isNull()) j["national_status"] = allNationalStatus(decode(j["state"]));
                  j["journal"] = store.journal();
                  j["active_jobs"] =
                      store.query("SELECT id,status FROM jobs WHERE status='running'");
                  j["llama_endpoint"] = provider.endpointValue();
                  reply(cb, j);
              },
              {Get, Post});
        route("/api/v1/flag", [&](auto req,auto,auto cb) {
            std::lock_guard lock(mutex);auto w=decode(store.campaign());auto a=req->getParameter("actor"),rev=req->getParameter("revision");
            if(req->getParameter("campaign")!=w.campaignId||!w.hasActor(a))throw ValidationError("Unknown campaign flag");
            auto actor=w.actors[a];std::string svg;
            if(rev=="fallback")svg=makeFlag(Json::nullValue,w.campaignId+a,"legacy")["svg"].asString();
            else {if(!std::regex_match(rev,std::regex("[0-9]{1,6}")))throw ValidationError("Invalid flag revision");auto n=std::stoul(rev);if(!actor["identity_revisions"].isArray()||n>=actor["identity_revisions"].size())throw ValidationError("Unknown flag revision");auto r=actor["identity_revisions"][Json::ArrayIndex(n)];svg=r["svg"].asString();if(svg.empty()){auto code=r["static_flag"].asString();if(!std::regex_match(code,std::regex("[A-Z]{2}")))throw ValidationError("No static flag");std::ifstream f("web/flags/"+lower(code)+".svg");svg=std::string(std::istreambuf_iterator<char>(f),{});}}
            auto response=HttpResponse::newHttpResponse();response->setBody(svg);response->addHeader("Content-Type","image/svg+xml");response->addHeader("Content-Security-Policy","default-src 'none'; sandbox");response->addHeader("X-Content-Type-Options","nosniff");response->addHeader("Cache-Control","private, max-age=31536000, immutable");cb(response);
        },{Get});
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
                  auto w=decode(store.campaign());auto rows=reference.query("SELECT code,name FROM countries ORDER BY name");
                  if(!w.campaignId.empty()){rows=Json::arrayValue;for(auto &a:w.actors.getMemberNames()){Json::Value c;c["code"]=a;c["name"]=w.actors[a]["name"];rows.append(c);}}reply(cb,rows);
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
                  auto j = rows[0];auto current=decode(store.campaign());if(current.hasActor(code)){j["name"]=current.actors[code]["name"];j["status"]=nationalStatus(current,code);}
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
                  // Saves from before national status gain it at this month; replay re-seeds from the frozen inputs.
                  bool backfill = !hasStatus(w);
                  if (backfill) seedStatus(w, statusSeed, false);
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
                  refs["status_version"] = statusVersion;
                  if (backfill) refs["status_seed"] = encode(statusSeed);
                  auto mentions=mentioned(w,w.player,orders);
                  // AI nations that act this month: those the player addresses first, then ongoing disputes and one rotating neighbor.
                  std::vector<std::string> candidates(mentions.actors.begin(),mentions.actors.end());
                  // Large blocs (NATO, the EU) would otherwise fill every month with the same alphabetical members.
                  auto engaged=[&](const Json::Value &v){return v["status"]=="open"&&std::find(v["actors"].begin(),v["actors"].end(),Json::Value(w.player))!=v["actors"].end();};
                  for(auto &v:w.conflicts)if(engaged(v))for(auto &a:v["actors"])candidates.push_back(a.asString());
                  for(auto &v:w.commitments)if(engaged(v)&&v["actors"].size()<=3)for(auto &a:v["actors"])candidates.push_back(a.asString());
                  auto neighbors=reference.query("SELECT b FROM neighbors WHERE a=? ORDER BY b",{w.player});
                  if(!neighbors.empty())candidates.push_back(neighbors[w.turn%neighbors.size()]["b"].asString());
                  if(neighbors.empty()&&w.actors.size()>1){auto names=w.actors.getMemberNames();candidates.push_back(names[w.turn%names.size()]);}
                  std::set<std::string> actors;
                  for(auto &a:candidates)if(a!=w.player&&w.hasActor(a)&&actors.size()<maxAiActors)actors.insert(a);
                  actors.erase(w.player);
                  auto allocations=scheduleInitiatives(w,actors);
                  auto prior=store.query("SELECT slots FROM monthly_allocations WHERE campaign=? AND month=?",{w.campaignId,std::to_string(w.turn+1)});
                  if(!prior.empty())allocations=parse(prior[0]["slots"].asString());
                  else store.execute("INSERT INTO monthly_allocations VALUES(?,?,?)",{w.campaignId,std::to_string(w.turn+1),dump(allocations)});
                  refs["allocations"]=allocations;
                  refs[w.player] = store.background(w.player);
                  for (auto &a : actors)
                      refs[a] = store.background(a);
                  std::map<std::string,CountryContext> contexts;
                  contexts[w.player]=store.campaignContext(w,w.player);
                  for(auto &a:actors)contexts[a]=store.campaignContext(w,a);
                  std::vector<CountryContext> independentContexts;
                  for(auto &slot:allocations) {
                      auto a=slot["actor"].asString(),r=slot["region"].asString();
                      auto c=store.campaignContext(w,a);contexts[a]=c;
                      c.facts["initiative_type"]=slot["initiative_type"];c.facts["action_count"]=1;
                      if(!r.empty()) {
                          // No national military inventory or player orders in a regional decision.
                          c.facts=Json::objectValue;c.facts["initiative_type"]=slot["initiative_type"];c.facts["action_count"]=1;c.facts["turn"]=w.turn;
                          c.facts["regional_identity"]=w.territories[r];c.facts["regional_identity"]["id"]=r;for(auto k:{"gdp_usd","occupation_gdp"})c.facts["regional_identity"].removeMember(k);
                          c.facts["governing_authority"]=w.actors[a];c.facts["governing_authority"].removeMember("identity_revisions");c.facts["governing_authority"]["status"]=nationalStatus(w,a);
                          c.facts["politics"]=regionalState(w,r);c.facts["related_history"]=Json::arrayValue;
                          for(auto &row:store.query("SELECT document FROM events WHERE id IN (SELECT event FROM event_links WHERE kind='regions' AND target=?) ORDER BY turn DESC LIMIT 12",{r}))c.facts["related_history"].append(parse(row["document"].asString()));
                          c.reference=store.background(r);refs[r]=c.reference;
                      }
                      refs[a]=store.background(a);independentContexts.push_back(c);
                  }
                  prepareReferences(w);
                  try {
                      worker.add([&, w, orders, id, actors, refs, contexts, allocations, independentContexts] {
                          bool committed = false;
                          try {
                              std::map<std::string, TurnOrders> intentions{{w.player, orders}};
                              for (auto &a : actors) {
                                  auto c = contexts.at(a);c.facts["initiative_type"]="reactive";c.facts["action_count"]=3;c.facts["player_actions"]=encode(orders);
                                  intentions[a] = provider.decide(w, c).orders;
                                  // Flushed as it lands so the client can show it before the month is adjudicated.
                                  {std::lock_guard l(mutex);store.recordInference(id,provider.takeAudit());}
                              }
                              Json::Value independent(Json::arrayValue);
                              for(Json::ArrayIndex n=0;n<allocations.size();++n){
                                  auto slot=allocations[n];auto a=slot["actor"].asString(),r=slot["region"].asString();
                                  auto decision=provider.decide(w,independentContexts[n]);
                                  {std::lock_guard l(mutex);store.recordInference(id,provider.takeAudit());}
                                  for(auto &o:decision.orders){slot["action"]=o.text;if(!r.empty()){o.targets.push_back(r);slot["regional_actor"]=r;}slot["actor_action_index"]=Json::UInt(intentions[a].size());intentions[a].push_back(o);independent.append(slot);}
                              }
                              auto result = resolve(w);
                              auto adjudication=adjudicationInput(result.state,contexts,intentions,adjacency);
                              // Attach trusted slot attribution by matching each appended action once.
                              for(auto &slot:independent){unsigned index=0;for(auto &i:adjudication["intentions"])if(i["actor"]==slot["actor"]&&index++==slot["actor_action_index"].asUInt()){i["initiative_type"]=slot["initiative_type"];if(slot.isMember("regional_actor"))i["regional_actor"]=slot["regional_actor"];break;}}
                              adjudication["facts"]["regional_politics"]=Json::objectValue;
                              for(auto &slot:allocations)if(slot["region"]!=""){auto r=slot["region"].asString();adjudication["facts"]["regional_politics"][r]=regionalState(w,r);}
                              auto delta=provider.adjudicate(result.state,adjudication,adjacency);
                              {std::lock_guard l(mutex);store.recordInference(id,provider.takeAudit());}
                              result=applyDelta(result,delta,geography);
                              result=applyMonthlyStatus(result,delta,adjacency);
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
                                  prepareReferences(result.state);
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
                  referenceWorker.add([&, code, id] {
                      auto result = fetchHistory(code,referenceTransport);
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
            std::lock_guard l(mutex);
            // "since" (a previously seen row id) lets a live log poll for only what is new.
            auto since=req->getParameter("since");if(since.empty())since="0";
            auto rows=store.query("SELECT id,record FROM inference WHERE job=? AND id>? ORDER BY id",{req->getParameter("job"),since});
            Json::Value records(Json::arrayValue);
            for(auto &row:rows){auto record=parse(row["record"].asString());record["id"]=row["id"];records.append(record);}
            reply(cb,records);
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
            for(auto collection:{w.conflicts,w.commitments})for(auto &v:collection)if(v["actors"].size()<=3&&std::find(v["actors"].begin(),v["actors"].end(),Json::Value(w.player))!=v["actors"].end())for(auto &a:v["actors"])actors.insert(a.asString());
            if(b.isNull()){Json::Value coverage;for(auto &a:actors)coverage[a]=store.background(a);reply(cb,coverage);return;}
            auto id="prepare-"+std::to_string(++auxiliaryId);Json::Value j;j["id"]=id;j["status"]="running";auxiliary[id]=j;
            referenceWorker.add([&,actors,id]{for(auto &a:actors){if(!std::regex_match(a,std::regex("[A-Z]{2}")))continue;auto result=fetchHistory(a,referenceTransport);std::lock_guard l(mutex);if(result["available"].asBool()||!store.background(a).get("available",false).asBool())store.cache(a,result);auxiliary[id]["coverage"][a]=result;}std::lock_guard l(mutex);auxiliary[id]["status"]="complete";});reply(cb,j,202);
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
                  // Monthly consequences follow region adjacency, so replay needs it once status exists.
                  bool status=false;for(auto &t:j["turns"])status=status||t["reference"].asString().find("\"status_version\"")!=std::string::npos;
                  if(status){j["adjacency"]=Json::objectValue;for(auto &[a,bs]:adjacency)for(auto &b:bs)j["adjacency"][a].append(b);}
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
        // Finish while captured callbacks and preparation state are still alive.
        worker.finish();
        referenceWorker.finish();
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
