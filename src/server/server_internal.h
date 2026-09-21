#pragma once
#include "localhistoria/politics.h"
#include "server/server.h"
#include <drogon/drogon.h>
namespace lh::detail {
using namespace drogon;
using CB = std::function<void(const HttpResponsePtr &)>;
using Handler = void (*)(Server &, const HttpRequestPtr &, Json::Value, const CB &);
// Up to six decimal digits: a revision or a query-string number.
bool isSmallNumber(const std::string &s);
void reply(const CB &cb, const Json::Value &j, int status = 200);
// Revalidated JSON: an unchanged response costs a 304 instead of the full body.
void replyCached(const HttpRequestPtr &req, const CB &cb, const Json::Value &j);
// A month's recorded changes in words, for the journal's "What changed"; names are current ones.
Json::Value changeSummaries(const Json::Value &changes, const WorldState &w);
// Route handlers: one per endpoint.
void campaign(Server &s, const HttpRequestPtr &req, Json::Value b, const CB &cb);
void flag(Server &s, const HttpRequestPtr &req, Json::Value, const CB &cb);
void modelConfig(Server &s, const HttpRequestPtr &, Json::Value b, const CB &cb);
void countries(Server &s, const HttpRequestPtr &, Json::Value, const CB &cb);
void country(Server &s, const HttpRequestPtr &req, Json::Value, const CB &cb);
void jobs(Server &s, const HttpRequestPtr &req, Json::Value, const CB &cb);
void turns(Server &s, const HttpRequestPtr &, Json::Value b, const CB &cb);
void cancelTurn(Server &s, const HttpRequestPtr &, Json::Value b, const CB &cb);
void history(Server &s, const HttpRequestPtr &req, Json::Value b, const CB &cb);
void inference(Server &s, const HttpRequestPtr &req, Json::Value, const CB &cb);
void map(Server &s, const HttpRequestPtr &, Json::Value, const CB &cb);
void entities(Server &s, const HttpRequestPtr &req, Json::Value, const CB &cb);
void events(Server &s, const HttpRequestPtr &req, Json::Value, const CB &cb);
void prepare(Server &s, const HttpRequestPtr &, Json::Value b, const CB &cb);
void replay(Server &s, const HttpRequestPtr &, Json::Value, const CB &cb);
} // namespace lh::detail
