#include "server/server_internal.h"
#include <algorithm>
namespace lh {
namespace detail {
bool isSmallNumber(const std::string &s) {
    return !s.empty() && s.size() <= 6 &&
           std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}
void reply(const CB &cb, const Json::Value &j, int status) {
    auto r = HttpResponse::newHttpJsonResponse(j);
    r->setStatusCode(static_cast<HttpStatusCode>(status));
    r->addHeader("Cache-Control", "no-store");
    r->addHeader("X-Content-Type-Options", "nosniff");
    cb(r);
}
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
} // namespace detail
using namespace detail;
namespace {
// Mutation bodies accept only their known fields, each with its JSON type.
void validateBody(const std::string &path, const Json::Value &body) {
    std::map<std::string, std::string> fields;
    if (path == "/api/v1/campaign")
        fields = {{"country", "string"}, {"confirm_replace", "bool"}};
    if (path == "/api/v1/turns")
        fields = {{"campaign_id", "string"},     {"expected_turn", "int"},
                  {"idempotency_key", "string"}, {"narration", "bool"},
                  {"orders", "array"},           {"responses", "array"}};
    if (path == "/api/v1/turns/cancel")
        fields = {{"job", "string"}};
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
} // namespace
void registerRoutes(Server &s) {
    route(s, "/api/v1/campaign", campaign, {Get, Post});
    route(s, "/api/v1/flag", flag, {Get});
    route(s, "/api/v1/model-config", modelConfig, {Post});
    route(s, "/api/v1/countries", countries, {Get});
    route(s, "/api/v1/country", country, {Get});
    route(s, "/api/v1/jobs", jobs, {Get});
    route(s, "/api/v1/turns", turns, {Post});
    route(s, "/api/v1/turns/cancel", cancelTurn, {Post});
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
