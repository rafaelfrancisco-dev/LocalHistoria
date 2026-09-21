#include "server/campaign_factory.h"
#include "server/server.h"
#include <drogon/drogon.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#ifdef _WIN32
#include <io.h>
#include <share.h>
#else
#include <sys/file.h>
#include <unistd.h>
#endif
using namespace lh;
using namespace drogon;
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
        // A month makes up to about a dozen calls; the budget bounds them together.
        auto budgetSeconds = config.get("turn_budget_seconds", 2 * timeoutSeconds).asDouble();
        if (budgetSeconds <= 0 || budgetSeconds > 1800)
            throw ValidationError("Turn budget must be greater than zero and at most 1800 seconds");
        std::string save = config.get("save_path", "saves/campaign.sqlite").asString();
        auto parent = std::filesystem::path(save).parent_path();
        if (!parent.empty())
            std::filesystem::create_directories(parent);
#ifdef _WIN32
        // Windows enforces the exclusive share mode at open time; there is no separate flock step,
        // and the lock releases when this process exits and the descriptor closes.
        int lockfd = -1;
        _sopen_s(&lockfd, (save + ".lock").c_str(), _O_CREAT | _O_RDWR, _SH_DENYRW,
                 _S_IREAD | _S_IWRITE);
        if (lockfd < 0)
            throw std::runtime_error("Save is already open by another server");
#else
        // Never closed: the flock is held for the life of the process and released when it exits.
        int lockfd = open((save + ".lock").c_str(), O_CREAT | O_RDWR, 0600);
        if (lockfd < 0 || flock(lockfd, LOCK_EX | LOCK_NB) != 0)
            throw std::runtime_error("Save is already open by another server");
#endif
        Store store(save), reference("data/world.sqlite", true);
        store.recover();
        auto geography = loadGeography(reference);
        // Display geometry is immutable per build, so its URL carries a content version the browser
        // may cache forever.
        std::string geometryVersion;
        {
            std::ifstream in("web/regions.topojson", std::ios::binary);
            std::string bytes((std::istreambuf_iterator<char>(in)), {});
            geometryVersion = utils::getSha256(bytes).substr(0, 16);
        }
        auto adjacency = loadAdjacency(reference);
        auto statusSeed = loadStatusSeed(reference);
        std::string endpoint =
            llamaFlagGiven
                ? validateEndpoint(llamaFlagValue)
                : validateEndpoint(store.setting("llama_endpoint", "http://127.0.0.1:8081"));
        if (llamaFlagGiven)
            store.setSetting("llama_endpoint", endpoint);
        std::mutex mutex;
        LocalProvider provider(endpoint, timeoutSeconds);
        const bool referenceNetwork = config.get("reference_network", true).asBool();
        Transport referenceTransport = [referenceNetwork](const std::string &host,
                                                          const std::string &path,
                                                          const Json::Value &body, double timeout) {
            if (!referenceNetwork)
                throw std::runtime_error("Reference network disabled");
            return httpJson(host, path, body, timeout);
        };
        ReferencePreparer references(store, reference, mutex, referenceTransport);
        // Shutdown drops a queued month; Store::recover() marks one that was running interrupted on
        // the next start.
        Worker worker(false);
        TurnRunner turns(
            {store, reference, provider, references, geography, adjacency, mutex, budgetSeconds});
        {
            std::lock_guard lock(mutex);
            references.prepare(decode(store.campaign()));
        }
        Server server{config,    store,           reference,  provider,   mutex, geography,
                      adjacency, geometryVersion, statusSeed, references, turns, worker};
        registerRoutes(server);
        configureApp(config);
        std::cout << "LocalHistoria: http://127.0.0.1:" << config.get("port", 8080).asInt()
                  << " (fictional sandbox)\n";
        app().run();
        // Finish while captured callbacks and preparation state are still alive.
        worker.finish();
        references.finish();
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
