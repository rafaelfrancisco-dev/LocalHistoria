#pragma once
#include "localhistoria/references.h"
#include "localhistoria/status.h"
#include "localhistoria/turn.h"
#include "worker.h"
namespace lh {
// What the routes share. Handlers lock the mutex around every store and reference access.
struct Server {
    const Json::Value &config;
    Store &store, &reference;
    LocalProvider &provider;
    std::mutex &mutex;
    const Json::Value &geography;
    const Adjacency &adjacency;
    // Content version of web/regions.topojson, for its cacheable URL.
    std::string geometryVersion;
    const StatusSeed &statusSeed;
    ReferencePreparer &references;
    TurnRunner &turns;
    // Serializes turns; one bounded queue.
    Worker &worker;
};
void registerRoutes(Server &);
// Static files, caching headers and the loopback listener.
void configureApp(const Json::Value &config);
} // namespace lh
