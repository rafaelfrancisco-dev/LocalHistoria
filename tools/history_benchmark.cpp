// Disposable long-campaign fixtures and event-log/context read timings (plan 33).
//   history_benchmark make <save> <months>       writes a synthetic campaign of that length
//   history_benchmark measure <save> [repeats]    times reads; commits extra months, so use a copy
// The world is real-sized (3,420 regions, 190 nations) and each month holds ~23 journal items with
// real link shapes: structured events with places and causes, front advances, war tolls, unrest
// and construction.
#include "localhistoria/store.h"
#include "localhistoria/world.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <random>
#include <sys/resource.h>
using namespace lh;
namespace {
const int nations = 190, regionsEach = 18, openWars = 4;
const std::vector<std::string> words{
    "border",  "harvest", "reform",   "treaty",   "army",     "port",   "election", "strike",
    "protest", "bridge",  "railway",  "tariff",   "drought",  "flood",  "minister", "parliament",
    "summit",  "fleet",   "garrison", "pipeline", "currency", "famine", "refugees", "embargo",
    "loan",    "school",  "hospital", "mine",     "factory",  "canal",  "militia",  "envoy"};
WorldState world() {
    WorldState w;
    w.player = "A0";
    w.campaignId = "history-benchmark";
    for (int n = 0; n < nations; ++n) {
        auto a = "A" + std::to_string(n);
        w.actors[a]["name"] = "Nation " + std::to_string(n);
        w.actors[a]["color"] = "#336699";
        for (int k = 0; k < regionsEach; ++k) {
            auto id = a + "r" + std::to_string(k);
            auto &t = w.territories[id];
            t["name"] = "Region " + id;
            t["owner"] = t["controller"] = t["initial_affiliation"] = a;
            t["kind"] = "ADM1";
            t["latitude"] = -60.0 + (n % 20) * 6 + k * 0.2;
            t["longitude"] = -170.0 + (n / 20) * 34 + k * 0.3;
            t["population"] = Json::Int64(100000 + ((n * 7919 + k * 104729) % 9000000));
            t["gdp_usd"] = Json::Int64(1000000000LL * (1 + (n + k) % 50));
            t["stability"] = 60;
            t["unrest"] = 0.1 * (k % 5);
        }
    }
    for (int e = 0; e < 400; ++e) {
        auto id = "e" + std::to_string(e);
        auto region = "A" + std::to_string(e % nations) + "r" + std::to_string(e % regionsEach);
        auto &x = w.entities[id];
        x["kind"] = e % 3 ? "base" : "city";
        x["name"] = "Site " + id;
        x["owner"] = w.territories[region]["owner"];
        x["region"] = region;
        x["latitude"] = w.territories[region]["latitude"].asDouble() + 0.05;
        x["longitude"] = w.territories[region]["longitude"];
    }
    for (int c = 0; c < 12; ++c) {
        auto id = "c" + std::to_string(c);
        auto a = "A" + std::to_string(2 * c + 1), b = "A" + std::to_string(2 * c + 2);
        auto &x = w.conflicts[id];
        x["actors"].append(a);
        x["actors"].append(b);
        x["status"] = c < openWars ? "open" : "ended";
        for (auto side : {a, b})
            for (int k = 0; k < 3; ++k)
                x["affected_regions"].append(side + "r" + std::to_string(k));
    }
    return w;
}
// One synthetic month on top of the saved campaign, committed like a real turn.
void month(Store &store, std::mt19937 &rng, int &eventCount) {
    auto w = decode(store.campaign());
    auto pick = [&](int n) { return int(rng() % unsigned(n)); };
    auto text = [&] {
        std::string s;
        for (int n = 8 + pick(12); n > 0; --n)
            s += (s.empty() ? "" : " ") + words[pick(int(words.size()))];
        if (pick(500) == 0)
            s += " zephyr";
        return s;
    };
    Resolution r;
    r.state = w;
    r.state.turn = w.turn + 1;
    Json::Value refs;
    refs["delta"]["events"] = Json::arrayValue;
    auto recent = store.query("SELECT id FROM events WHERE turn>? ORDER BY rowid DESC LIMIT 200",
                              {std::to_string(w.turn - 24)});
    for (int k = 0; k < 12; ++k) {
        // The player acts most; a few dozen nations make most of the rest of the news.
        auto actor = "A" + std::to_string(k < 3 ? 0 : pick(4) ? pick(40) : pick(nations));
        auto id = "t" + std::to_string(r.state.turn) + "-e" + std::to_string(k);
        Json::Value d;
        d["id"] = id;
        d["actor"] = actor;
        d["description"] = text();
        d["blocked_by"] = pick(30) ? "none" : "infeasible";
        d["background"] = k >= 10;
        d["major"] = pick(4) == 0;
        d["actors"].append(actor);
        if (pick(3) == 0)
            d["actors"].append("A" + std::to_string(pick(nations)));
        d["regions"] = Json::arrayValue;
        for (int n = pick(3); n > 0; --n)
            d["regions"].append(actor + "r" + std::to_string(pick(regionsEach)));
        d["entities"] = Json::arrayValue;
        if (pick(5) == 0)
            d["entities"].append("e" + std::to_string(pick(400)));
        d["causes"] = Json::arrayValue;
        if (!recent.empty() && pick(5) == 0)
            d["causes"].append(recent[pick(int(recent.size()))]["id"]);
        d["conflicts"] = Json::arrayValue;
        if (pick(6) == 0)
            d["conflicts"].append("c" + std::to_string(pick(12)));
        d["effects"].append(parse(R"({"economy":"growth","stability":"none"})"));
        d["effects"][0]["actor"] = actor;
        if (k == 9)
            d["regional_actor"] = "Committee of " + actor;
        refs["delta"]["events"].append(d);
        if (d["blocked_by"] != "infeasible")
            r.events.push_back({actor, "event", id, d["description"].asString()});
    }
    for (int c = 0; c < openWars; ++c) {
        auto id = "c" + std::to_string(c);
        const auto &war = w.conflicts[id];
        r.events.push_back({war["actors"][0].asString(), "war_toll", id, "War toll: " + text()});
        if (c < 2)
            r.events.push_back({war["actors"][0].asString(), "front_advance", id,
                                "The front moves: " + text(),
                                war["affected_regions"][3 + pick(3)].asString()});
    }
    for (int n = 0; n < 3; ++n) {
        auto a = "A" + std::to_string(pick(nations));
        r.events.push_back({a, n ? "unrest" : "revolt", a, "Unrest: " + text(),
                            a + "r" + std::to_string(pick(regionsEach))});
    }
    for (int n = 0; n < 2; ++n) {
        auto e = "e" + std::to_string(pick(400));
        r.events.push_back(
            {w.entities[e]["owner"].asString(), "completion", e, "Built: " + text()});
    }
    eventCount += int(r.events.size());
    auto key = "bench-" + std::to_string(r.state.turn);
    store.commit(store.submit(w.turn, key, "{}")["id"].asString(), r, Json::objectValue, refs);
}
double ms(std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}
double percentile(std::vector<double> v, double p) {
    std::sort(v.begin(), v.end());
    return v[std::min(v.size() - 1, size_t(p * double(v.size() - 1) + 0.5))];
}
Json::Int64 rssBytes() {
    rusage u{};
    getrusage(RUSAGE_SELF, &u);
#ifdef __APPLE__
    return u.ru_maxrss;
#else
    return Json::Int64(u.ru_maxrss) * 1024;
#endif
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc < 3)
            throw std::runtime_error(
                "Usage: history_benchmark make|measure <save> [months|repeats]");
        std::string mode = argv[1], path = argv[2];
        auto now = [] { return std::chrono::steady_clock::now(); };
        std::mt19937 rng(33);
        if (mode == "make") {
            const int months = argc > 3 ? std::stoi(argv[3]) : 12;
            Store store(path);
            store.create(world(), true);
            int events = 0;
            auto start = now();
            for (int n = 0; n < months; ++n)
                month(store, rng, events);
            Json::Value row;
            row["months"] = months;
            row["journal_items"] = events;
            row["make_seconds"] = ms(start, now()) / 1000;
            std::cout << dump(row) << '\n';
            return 0;
        }
        if (mode != "measure")
            throw std::runtime_error("Unknown mode " + mode);
        const int repeats = argc > 3 ? std::stoi(argv[3]) : 30;
        Json::Value report;
        auto opened = now();
        Store store(path);
        report["open_ms"] = ms(opened, now());
        auto decodeStart = now();
        auto w = decode(store.campaign());
        report["decode_campaign_ms"] = ms(decodeStart, now());
        report["months"] = w.turn;
        auto filters = parse(R"({
            "first_page": {},
            "player_page": {"scope":"actor","actor":"A0"},
            "sparse_actor_page": {"scope":"actor","actor":"A150"},
            "war_page": {"categories":["war"]},
            "major_page": {"major":true},
            "search_common": {"q":"harvest"},
            "search_rare": {"q":"zephyr"},
            "map": {"on_map":true,"limit":500},
            "oldest_page": {"before_turn":3}
        })");
        auto measure = [&](const std::string &name, const std::function<Json::Value()> &read) {
            std::vector<double> times;
            Json::Value result, out;
            for (int n = 0; n < repeats; ++n) {
                auto before = store.counters;
                auto start = now();
                result = read();
                times.push_back(ms(start, now()));
                out["statements"] = Json::Int64(store.counters.statements - before.statements);
                out["text_bytes"] = Json::Int64(store.counters.textBytes - before.textBytes);
            }
            out["p50_ms"] = percentile(times, 0.5);
            out["p95_ms"] = percentile(times, 0.95);
            out["max_ms"] = *std::max_element(times.begin(), times.end());
            out["results"] = result.isObject() && result.isMember("events")
                                 ? result["events"].size()
                                 : result.size();
            report["reads"][name] = out;
        };
        // The first read after opening: the old log built every month here.
        auto before = store.counters;
        auto cold = now();
        store.eventLog(w, filters["first_page"]);
        report["cold_first_page_ms"] = ms(cold, now());
        report["cold_first_page_statements"] =
            Json::Int64(store.counters.statements - before.statements);
        report["cold_first_page_text_bytes"] =
            Json::Int64(store.counters.textBytes - before.textBytes);
        for (const auto &name : filters.getMemberNames())
            measure(name, [&] { return store.eventLog(w, filters[name]); });
        auto second = filters["first_page"];
        second["before_turn"] = store.eventLog(w, second)["next_before_turn"];
        measure("second_page", [&] { return store.eventLog(w, second); });
        measure("journal", [&] { return store.journal(); });
        measure("chronicle_events", [&] { return store.chronicleEvents("A0", w.turn - 12); });
        measure("related_history", [&] {
            try {
                return store.campaignContext(w, "A0").facts["related_history"];
            } catch (const std::exception &e) {
                return Json::Value(e.what());
            }
        });
        // A handler holds the store mutex while it decodes the campaign and reads the log.
        std::vector<double> held;
        for (int n = 0; n < repeats; ++n) {
            auto start = now();
            store.eventLog(decode(store.campaign()), filters["first_page"]);
            held.push_back(ms(start, now()));
        }
        report["handler_hold_p50_ms"] = percentile(held, 0.5);
        report["handler_hold_p95_ms"] = percentile(held, 0.95);
        // The first read after each new month.
        std::vector<double> afterCommit, commits;
        int events = 0;
        for (int n = 0; n < 5; ++n) {
            auto start = now();
            month(store, rng, events);
            commits.push_back(ms(start, now()));
            w = decode(store.campaign());
            start = now();
            store.eventLog(w, filters["first_page"]);
            afterCommit.push_back(ms(start, now()));
        }
        report["commit_p50_ms"] = percentile(commits, 0.5);
        report["first_read_after_commit_p50_ms"] = percentile(afterCommit, 0.5);
        report["first_read_after_commit_max_ms"] =
            *std::max_element(afterCommit.begin(), afterCommit.end());
        report["max_rss_mb"] = double(rssBytes()) / 1048576;
        std::cout << dump(report) << '\n';
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
