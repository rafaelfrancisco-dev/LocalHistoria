#include "server.h"
#include "campaign_factory.h"
#include "localhistoria/politics.h"
#include <algorithm>
#include <cmath>
#include <drogon/drogon.h>
#include <fstream>
#include <regex>
namespace lh {
using namespace drogon;
namespace {
using CB = std::function<void(const HttpResponsePtr &)>;
using Handler = void (*)(Server &, const HttpRequestPtr &, Json::Value, const CB &);
void reply(const CB &cb, const Json::Value &j, int status = 200) {
    auto r = HttpResponse::newHttpJsonResponse(j);
    r->setStatusCode(static_cast<HttpStatusCode>(status));
    r->addHeader("Cache-Control", "no-store");
    r->addHeader("X-Content-Type-Options", "nosniff");
    cb(r);
}
// Revalidated JSON: an unchanged response costs a 304 instead of the full body.
void replyCached(const HttpRequestPtr &req, const CB &cb, const Json::Value &j) {
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    auto body = Json::writeString(writer, j), etag = "\"" + utils::getMd5(body) + "\"";
    auto r = HttpResponse::newHttpResponse();
    if (req->getHeader("if-none-match") == etag)
        r->setStatusCode(k304NotModified);
    else {
        r->setBody(body);
        r->setContentTypeCode(CT_APPLICATION_JSON);
    }
    r->addHeader("ETag", etag);
    r->addHeader("Cache-Control", "no-cache");
    r->addHeader("X-Content-Type-Options", "nosniff");
    cb(r);
}
// Mutation bodies accept only their known fields, each with its JSON type.
void validateBody(const std::string &path, const Json::Value &body) {
    std::map<std::string, std::string> fields;
    if (path == "/api/v1/campaign")
        fields = {{"country", "string"}, {"confirm_replace", "bool"}};
    if (path == "/api/v1/turns")
        fields = {{"campaign_id", "string"},
                  {"expected_turn", "int"},
                  {"idempotency_key", "string"},
                  {"orders", "array"},
                  {"responses", "array"}};
    if (path == "/api/v1/model-config")
        fields = {{"llama_endpoint", "string"}};
    if (path == "/api/v1/history")
        fields = {{"code", "string"}};
    for (auto &key : body.getMemberNames()) {
        if (!fields.contains(key))
            throw ValidationError("Unknown request field: " + key);
        auto type = fields.at(key);
        auto &v = body[key];
        if ((type == "string" && !v.isString()) || (type == "bool" && !v.isBool()) ||
            (type == "int" && !v.isInt()) || (type == "array" && !v.isArray()))
            throw ValidationError("Wrong type for field: " + key);
    }
}
void route(Server &s, const std::string &path, Handler handler, std::vector<HttpMethod> methods) {
    std::vector<internal::HttpConstraint> constraints;
    for (auto m : methods)
        constraints.emplace_back(m);
    std::function<void(const HttpRequestPtr &, CB &&)> endpoint =
        [&s, handler, path](const HttpRequestPtr &req, CB &&cb) {
            try {
                auto host = req->getHeader("host");
                auto origin = req->getHeader("origin");
                auto port = std::to_string(s.config.get("port", 8080).asInt());
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
                    validateBody(path, body);
                }
                handler(s, req, body, cb);
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
}
void campaign(Server &s, const HttpRequestPtr &req, Json::Value b, const CB &cb) {
    std::lock_guard l(s.mutex);
    Json::Value j;
    if (!b.isNull()) {
        if (!b["country"].isString())
            throw ValidationError("country is required");
        auto w = newCampaign(s.reference, s.statusSeed, b["country"].asString());
        s.store.create(w, b.get("confirm_replace", false).isBool() &&
                              b.get("confirm_replace", false).asBool());
    }
    s.references.prepare(decode(s.store.campaign()));
    j["state"] = s.store.campaign();
    if (!j["state"].isNull())
        j["national_status"] = allNationalStatus(decode(j["state"]));
    j["journal"] = s.store.journal();
    j["active_jobs"] = s.store.query("SELECT id,status FROM jobs WHERE status='running'");
    j["llama_endpoint"] = s.provider.endpointValue();
    if (b.isNull())
        replyCached(req, cb, j);
    else
        reply(cb, j);
}
void flag(Server &s, const HttpRequestPtr &req, Json::Value b, const CB &cb) {
    std::lock_guard lock(s.mutex);
    auto w = decode(s.store.campaign());
    auto a = req->getParameter("actor"), rev = req->getParameter("revision");
    if (req->getParameter("campaign") != w.campaignId || !w.hasActor(a))
        throw ValidationError("Unknown campaign flag");
    auto actor = w.actors[a];
    std::string svg;
    if (rev == "fallback")
        svg = makeFlag(Json::nullValue, w.campaignId + a, "legacy")["svg"].asString();
    else {
        if (!std::regex_match(rev, std::regex("[0-9]{1,6}")))
            throw ValidationError("Invalid flag revision");
        auto n = std::stoul(rev);
        if (!actor["identity_revisions"].isArray() || n >= actor["identity_revisions"].size())
            throw ValidationError("Unknown flag revision");
        auto r = actor["identity_revisions"][Json::ArrayIndex(n)];
        svg = r["svg"].asString();
        if (svg.empty()) {
            auto code = r["static_flag"].asString();
            if (!std::regex_match(code, std::regex("[A-Z]{2}")))
                throw ValidationError("No static flag");
            std::ifstream f("web/flags/" + lowercase(code) + ".svg");
            svg = std::string(std::istreambuf_iterator<char>(f), {});
        }
    }
    auto response = HttpResponse::newHttpResponse();
    response->setBody(svg);
    response->addHeader("Content-Type", "image/svg+xml");
    response->addHeader("Content-Security-Policy", "default-src 'none'; sandbox");
    response->addHeader("X-Content-Type-Options", "nosniff");
    response->addHeader("Cache-Control", "private, max-age=31536000, immutable");
    cb(response);
}
void modelConfig(Server &s, const HttpRequestPtr &req, Json::Value b, const CB &cb) {
    if (!b["llama_endpoint"].isString())
        throw ValidationError("llama_endpoint is required");
    auto endpoint = validateEndpoint(b["llama_endpoint"].asString());
    s.provider.setEndpoint(endpoint);
    {
        std::lock_guard l(s.mutex);
        s.store.setSetting("llama_endpoint", endpoint);
    }
    Json::Value j;
    j["llama_endpoint"] = endpoint;
    reply(cb, j);
}
void countries(Server &s, const HttpRequestPtr &req, Json::Value b, const CB &cb) {
    std::lock_guard l(s.mutex);
    auto w = decode(s.store.campaign());
    auto rows = s.reference.query("SELECT code,name FROM countries ORDER BY name");
    if (!w.campaignId.empty()) {
        rows = Json::arrayValue;
        for (auto &a : w.actors.getMemberNames()) {
            Json::Value c;
            c["code"] = a;
            c["name"] = w.actors[a]["name"];
            rows.append(c);
        }
    }
    reply(cb, rows);
}
void country(Server &s, const HttpRequestPtr &req, Json::Value b, const CB &cb) {
    std::lock_guard l(s.mutex);
    auto code = req->getParameter("code");
    auto rows = s.reference.query("SELECT * FROM countries WHERE code=?", {code});
    if (rows.empty()) {
        auto w = decode(s.store.campaign());
        if (!w.actors.isMember(code))
            throw ValidationError("Unknown country");
        Json::Value j = w.actors[code];
        j["cities"] = Json::arrayValue;
        j["background"]["available"] = false;
        j["background"]["reason"] = "Fictional campaign actor";
        reply(cb, j);
        return;
    }
    auto j = rows[0];
    auto current = decode(s.store.campaign());
    if (current.hasActor(code)) {
        j["name"] = current.actors[code]["name"];
        j["status"] = nationalStatus(current, code);
    }
    j["cities"] = s.reference.query("SELECT name,population,city_wikidata FROM cities "
                                    "WHERE code=? ORDER BY population DESC LIMIT 15",
                                    {code});
    j["background"] = s.store.background(code);
    reply(cb, j);
}
void jobs(Server &s, const HttpRequestPtr &req, Json::Value b, const CB &cb) {
    std::lock_guard l(s.mutex);
    auto id = req->getParameter("id");
    auto job = s.references.job(id);
    reply(cb, job.isNull() ? s.store.job(id) : job);
}
void turns(Server &s, const HttpRequestPtr &req, Json::Value b, const CB &cb) {
    std::lock_guard l(s.mutex);
    if (!b["expected_turn"].isInt() || !b["idempotency_key"].isString() ||
        b["idempotency_key"].asString().size() < 8 || b["idempotency_key"].asString().size() > 128)
        throw ValidationError("Expected integer turn and 8–128 character idempotency key");
    auto w = decode(s.store.campaign());
    if (b["campaign_id"] != w.campaignId || w.campaignId.empty())
        throw ValidationError("Stale campaign");
    // Saves from before national status gain it at this month; replay re-seeds from the
    // frozen inputs.
    bool militaryBackfill = !hasMilitary(w);
    bool backfill = !hasStatus(w);
    if (backfill)
        seedStatus(w, s.statusSeed, false);
    if (militaryBackfill)
        seedMilitary(w, s.statusSeed);
    auto orders = parseOrders(b["orders"]);
    auto payload = dump(b);
    auto existing =
        s.store.query("SELECT id,payload FROM jobs WHERE key=?", {b["idempotency_key"].asString()});
    if (!existing.empty()) {
        if (existing[0]["payload"].asString() != payload)
            throw ValidationError("Idempotency key payload mismatch");
        reply(cb, s.store.job(existing[0]["id"].asString()), 202);
        return;
    }
    validate(w, w.player, orders);
    // Answers to treaties offered last month; each pending proposal to the player at
    // most once.
    std::set<std::string> answered;
    if (b["responses"].size() > 8)
        throw ValidationError("Too many proposal responses");
    for (auto &r : b["responses"]) {
        if (!r.isObject() || r.getMemberNames() != std::vector<std::string>{"accept", "proposal"} ||
            !r["accept"].isBool() || !r["proposal"].isString())
            throw ValidationError("Invalid proposal response");
        auto pid = r["proposal"].asString();
        if (!w.proposals.isMember(pid) || w.proposals[pid]["status"] != "pending" ||
            !answered.insert(pid).second)
            throw ValidationError("No pending proposal " + pid);
    }
    auto job = s.store.submit(b["expected_turn"].asInt(), b["idempotency_key"].asString(), payload);
    auto id = job["id"].asString();
    auto plan = s.turns.plan(w, orders);
    auto &refs = plan.refs;
    refs["status_version"] = statusVersion;
    if (b.isMember("responses"))
        refs["proposal_responses"] = b["responses"];
    if (backfill)
        refs["status_seed"] = encode(s.statusSeed);
    if (militaryBackfill)
        refs["military_seed"] = encode(s.statusSeed);
    s.references.prepare(w);
    try {
        s.worker.add([&s, plan, id] { s.turns.run(plan, id); });
    } catch (...) {
        s.store.execute("UPDATE jobs SET status='failed',error='Queue full' WHERE id=?", {id});
        throw;
    }
    reply(cb, s.store.job(id), 202);
}
void history(Server &s, const HttpRequestPtr &req, Json::Value b, const CB &cb) {
    std::lock_guard l(s.mutex);
    auto code = b.isNull() ? req->getParameter("code") : b.get("code", "").asString();
    if (s.reference.query("SELECT code FROM countries WHERE code=?", {code}).empty())
        throw ValidationError("Unknown country");
    if (b.isNull()) {
        reply(cb, s.store.background(code));
        return;
    }
    reply(cb, s.references.refresh(code), 202);
}
void inference(Server &s, const HttpRequestPtr &req, Json::Value b, const CB &cb) {
    std::lock_guard l(s.mutex);
    // "since" (a previously seen row id) lets a live log poll for only what is new.
    auto since = req->getParameter("since");
    if (since.empty())
        since = "0";
    auto rows = s.store.query("SELECT id,record FROM inference WHERE job=? AND id>? ORDER BY id",
                              {req->getParameter("job"), since});
    Json::Value records(Json::arrayValue);
    for (auto &row : rows) {
        auto record = parse(row["record"].asString());
        record["id"] = row["id"];
        records.append(record);
    }
    reply(cb, records);
}
void map(Server &s, const HttpRequestPtr &req, Json::Value b, const CB &cb) {
    std::lock_guard l(s.mutex);
    auto w = decode(s.store.campaign());
    Json::Value j;
    j["geometry_url"] = "/regions.topojson?v=" + s.geometryVersion;
    j["territories"] = w.territories;
    j["turn"] = w.turn;
    reply(cb, j);
}
void entities(Server &s, const HttpRequestPtr &req, Json::Value b, const CB &cb) {
    std::lock_guard l(s.mutex);
    auto w = decode(s.store.campaign());
    Json::Value j(Json::arrayValue);
    auto number = [&](const char *key, double fallback) {
        auto p = req->getParameter(key);
        if (p.empty())
            return fallback;
        size_t end = 0;
        double x = std::stod(p, &end);
        if (end != p.size() || !std::isfinite(x))
            throw ValidationError("Invalid viewport");
        return x;
    };
    double west = number("west", -180), east = number("east", 180), south = number("south", -90),
           north = number("north", 90), zoom = number("zoom", 2);
    if (west > east || south > north)
        throw ValidationError("Invalid viewport bounds");
    auto wanted = req->getParameter("id");
    // Destroyed and inactive entities, such as faded event sites, leave the map
    // unless asked for.
    bool inactive = req->getParameter("include_inactive") == "1";
    for (auto &id : w.entities.getMemberNames()) {
        auto e = w.entities[id];
        double x = e["longitude"].asDouble(), y = e["latitude"].asDouble();
        if (wanted.empty() && !inactive &&
            (e["status"] == "destroyed" || e["status"] == "inactive"))
            continue;
        if ((!wanted.empty() && id == wanted) ||
            (wanted.empty() && x >= west && x <= east && y >= south && y <= north &&
             (zoom >= 4 || e["kind"] != "city"))) {
            e["id"] = id;
            j.append(e);
            if (j.size() >= 200)
                break;
        }
    }
    reply(cb, j);
}
void events(Server &s, const HttpRequestPtr &req, Json::Value b, const CB &cb) {
    std::lock_guard l(s.mutex);
    auto w = decode(s.store.campaign());
    Json::Value filter;
    auto scope = req->getParameter("scope");
    if (scope.empty())
        scope = "all";
    if (scope != "all" && scope != "actor")
        throw ValidationError("scope must be all or actor");
    filter["scope"] = scope;
    if (scope == "actor") {
        auto a = req->getParameter("actor");
        if (!w.hasActor(a))
            throw ValidationError("Unknown actor");
        filter["actor"] = a;
    }
    static const std::set<std::string> known{"player", "nation", "regional",
                                             "world",  "war",    "construction"};
    filter["categories"] = Json::arrayValue;
    for (auto categories = req->getParameter("category"); !categories.empty();) {
        auto comma = categories.find(',');
        auto c = categories.substr(0, comma);
        if (!known.contains(c))
            throw ValidationError("Unknown event category: " + c);
        filter["categories"].append(c);
        categories = comma == std::string::npos ? "" : categories.substr(comma + 1);
    }
    auto q = req->getParameter("q");
    if (q.size() > 200)
        throw ValidationError("Search is too long");
    filter["q"] = q;
    filter["on_map"] = req->getParameter("on_map") == "1";
    filter["major"] = req->getParameter("major") == "1";
    auto integer = [&](const char *key, int fallback, int low, int high) {
        auto p = req->getParameter(key);
        if (p.empty())
            return fallback;
        static const std::regex digits("[0-9]{1,6}");
        if (!std::regex_match(p, digits))
            throw ValidationError(std::string("Invalid ") + key);
        return std::clamp(std::stoi(p), low, high);
    };
    filter["before_turn"] = integer("before_turn", w.turn + 1, 0, w.turn + 1);
    filter["limit"] = integer("limit", filter["on_map"].asBool() ? 500 : 50, 1,
                              filter["on_map"].asBool() ? 500 : 100);
    auto j = w.campaignId.empty() ? Json::Value() : s.store.eventLog(w, filter);
    if (j.isNull()) {
        j["events"] = Json::arrayValue;
        j["more"] = false;
    }
    j["turn"] = w.turn;
    reply(cb, j);
}
void prepare(Server &s, const HttpRequestPtr &req, Json::Value b, const CB &cb) {
    std::lock_guard l(s.mutex);
    auto w = decode(s.store.campaign());
    if (w.player.empty())
        throw ValidationError("Start a campaign first");
    auto actors = s.references.engagedActors(w);
    if (b.isNull()) {
        reply(cb, s.references.coverage(actors));
        return;
    }
    reply(cb, s.references.prepareAll(actors), 202);
}
void replay(Server &s, const HttpRequestPtr &req, Json::Value b, const CB &cb) {
    std::lock_guard l(s.mutex);
    Json::Value j;
    j["campaign"] = s.store.campaign();
    auto initial = s.store.query("SELECT state FROM initial_world WHERE id=1");
    j["initial_state"] = initial.empty() ? Json::Value() : parse(initial[0]["state"].asString());
    j["geography"] = Json::objectValue;

    for (auto &row : s.store.query("SELECT reference FROM turns")) {
        auto refs = parse(row["reference"].asString());
        for (auto &change : refs["delta"]["changes"])
            if (change["kind"] == "entity") {
                auto region = change["after"]["region"].asString();
                j["geography"][region] = s.geography[region];
            }
    }
    j["turns"] = s.store.query("SELECT * FROM turns ORDER BY turn");
    // Monthly consequences follow region s.adjacency, so replay needs it once status
    // exists.
    bool status = false;
    for (auto &t : j["turns"])
        status =
            status || t["reference"].asString().find("\"status_version\"") != std::string::npos;
    if (status) {
        j["adjacency"] = Json::objectValue;
        for (auto &[a, bs] : s.adjacency)
            for (auto &b : bs)
                j["adjacency"][a].append(b);
    }
    for (auto &t : j["turns"])
        for (auto k : {"state", "intentions", "events", "reference"})
            t[k] = parse(t[k].asString());
    reply(cb, j);
}
} // namespace
void registerRoutes(Server &s) {
    route(s, "/api/v1/campaign", campaign, {Get, Post});
    route(s, "/api/v1/flag", flag, {Get});
    route(s, "/api/v1/model-config", modelConfig, {Post});
    route(s, "/api/v1/countries", countries, {Get});
    route(s, "/api/v1/country", country, {Get});
    route(s, "/api/v1/jobs", jobs, {Get});
    route(s, "/api/v1/turns", turns, {Post});
    route(s, "/api/v1/history", history, {Get, Post});
    route(s, "/api/v1/inference", inference, {Get});
    route(s, "/api/v1/map", map, {Get});
    route(s, "/api/v1/entities", entities, {Get});
    route(s, "/api/v1/events", events, {Get});
    route(s, "/api/v1/prepare", prepare, {Get, Post});
    route(s, "/api/v1/replay", replay, {Get});
}
void configureApp(const Json::Value &config) {
    app()
        .setLogLevel(trantor::Logger::kWarn)
        .setClientMaxBodySize(32768)
        .setDocumentRoot("web")
        .setFileTypes({"html", "js", "css", "svg", "png", "ico", "geojson", "topojson"})
        .registerCustomExtensionMime("geojson", "application/geo+json")
        .registerCustomExtensionMime("topojson", "application/json")
        // Serve the pre-compressed web/*.gz next to each file when the browser accepts gzip.
        .setGzipStatic(true)
        .registerPreSendingAdvice([](const HttpRequestPtr &req, const HttpResponsePtr &r) {
            auto &path = req->path();
            if (path.starts_with("/api/"))
                return;
            bool versioned = !req->getParameter("v").empty() &&
                             (path.ends_with(".topojson") || path.ends_with(".geojson"));
            r->addHeader("Cache-Control",
                         versioned ? "public, max-age=31536000, immutable" : "no-cache");
        })
        .addListener("127.0.0.1", config.get("port", 8080).asInt())
        .setThreadNum(2);
}
} // namespace lh
