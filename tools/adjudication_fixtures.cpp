// Closed, deterministic, disposable full-world fixtures for plan 34. No inference or user save.
#include "server/campaign_factory.h"
#include "localhistoria/lifecycle.h"
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace lh;
namespace fs = std::filesystem;
static void write(const fs::path &p, const Json::Value &v) { std::ofstream(p) << dump(v); }
int main(int argc, char **argv) {
    try {
        if (argc != 2) throw std::runtime_error("Usage: adjudication_fixtures NEW_DIRECTORY (run from repository root)");
        fs::path root = fs::absolute(argv[1]);
        auto protectedRoot = fs::weakly_canonical("saves");
        auto relative = fs::weakly_canonical(root).lexically_relative(protectedRoot);
        if (!relative.empty() && *relative.begin() != "..") throw std::runtime_error("Fixtures must not use saves/");
        if (fs::exists(root)) throw std::runtime_error("Destination must not exist");
        fs::create_directories(root);
        Store reference("data/world.sqlite", true);
        auto seed = loadStatusSeed(reference);
        auto adjacency = loadAdjacency(reference);
        auto geography = loadGeography(reference);
        Json::Value manifest;
        manifest["scenarios"] = Json::arrayValue;
        write(root / "references.json", Json::Value(Json::objectValue));
        for (const std::string name : {"peace", "invasion", "access_ceasefire", "multiparty", "regional", "older"}) {
            auto w = newCampaign(reference, seed, "PT");
            w.campaignId = "plan34-" + name;
            if (name == "invasion") {
                // Synthetic force advantage isolates feasible intent grounding, not historical balance.
                w.actors["PT"]["status"]["military"]["active"] = 2500000;
                w.actors["PT"]["status"]["military"]["aircraft"] = 1000;
            }
            Json::Value orders(Json::arrayValue), checks;
            auto order = [&](const std::string &text) { Json::Value m; m["orders"][0]["text"] = text; orders.append(m); };
            std::string region;
            for (const auto &id : w.territories.getMemberNames())
                if (w.territories[id]["owner"] == "PT" && w.territories[id]["kind"] == "ADM1") { region = id; break; }
            {
                Store store((root / (name + ".sqlite")).string());
                store.create(w, false);
                auto step = [&](Json::Value events) {
                    auto refs = lifecycleReference(w);
                    refs["sea_route_revision"] = adjacency.seaRevision;
                    auto base = beginTurn(w, refs);
                    Json::Value response; response["events"] = events;
                    auto delta = compileOperations(base.state, response, adjacency);
                    refs["delta"] = encode(delta);
                    auto result = finishTurn(base, delta, adjacency, geography, refs);
                    auto job = store.submit(w.turn, "fixture-" + std::to_string(w.turn), "{}");
                    store.commit(job["id"].asString(), result, Json::Value(Json::objectValue), refs);
                    w = result.state;
                };
                auto event = [](const std::string &actor, const std::string &text) {
                    Json::Value e; e["actor"] = actor; e["description"] = text; e["blocked_by"] = "none";
                    e["causes"] = e["operations"] = Json::arrayValue; return e;
                };
                if (name == "multiparty") {
                    auto e = event("ES", "Offer a three-party trade agreement.");
                    e["operations"] = parse(R"([{"action":"add_commitment","actors":["PT","ES","FR"],"name":"Three-party Trade Accord","text":"Cooperate in trade for six months","kind":"trade_agreement","months":6,"terms":[]}])");
                    Json::Value events(Json::arrayValue); events.append(e); step(events);
                    if (w.proposals.size() != 1) throw std::runtime_error("Multiparty fixture did not create exactly one proposal");
                    auto id = w.proposals.getMemberNames()[0]; checks["proposal"] = id;
                    order("Support the Three-party Trade Accord with Spain and France.");
                    orders[0]["responses"][0] = parse(R"({"revision":1,"accept":true})");
                    orders[0]["responses"][0]["proposal"] = id;
                    order("Maintain the trade policy agreed with Spain and France.");
                } else if (name == "regional") {
                    auto e = event("PT", "The region requests infrastructure funding.");
                    e["regional_actor"] = region; e["initiative_type"] = "independent_regional";
                    auto op = parse(R"({"action":"regional_initiative","agenda":"Local infrastructure","relations":"cooperative","demand":"Fund regional infrastructure","demand_kind":"regional_project","project_benefit":"infrastructure"})");
                    op["region"] = region; e["operations"].append(op);
                    Json::Value events(Json::arrayValue); events.append(e); step(events);
                    auto demand = w.regionalPolitics[region]["demands"][0];
                    if (demand["id"].isNull()) throw std::runtime_error("Regional fixture lacks demand");
                    checks["region"] = region; checks["demand"] = demand["id"];
                    order("Fund infrastructure investment in " + w.territories[region]["name"].asString() + " (region " + region + ") to fulfill its infrastructure demand.");
                    orders[0]["responses"][0]["demand"] = demand["id"];
                    orders[0]["responses"][0]["region"] = region;
                    orders[0]["responses"][0]["revision"] = demand["revision"];
                    orders[0]["responses"][0]["accept"] = true;
                    order("Continue funding the infrastructure project in " + w.territories[region]["name"].asString() + " (region " + region + ").");
                } else if (name == "access_ceasefire") {
                    auto e = event("PT", "A synthetic border war begins.");
                    e["operations"] = parse(R"([{"action":"open_conflict","actors":["PT","ES"],"kind":"war","intensity":"low","name":"Fixture Border War","text":"Border dispute"}])");
                    Json::Value events(Json::arrayValue); events.append(e); step(events);
                    order("Offer Spain a six-month ceasefire treaty ending our border war; record it as an offer awaiting signatures.");
                    order("Offer Spain a six-month treaty granting Portuguese forces military access to Spanish territory, subject to Spain's explicit signature.");
                } else if (name == "invasion") {
                    order("Invade Spain and seize reachable frontier territory within our army's capacity.");
                    order("Continue the invasion of Spain, advancing only as far as military capacity and supply permit.");
                } else {
                    if (name == "older") {
                        for (int month = 0; month < 36; ++month) {
                            Json::Value events(Json::arrayValue);
                            for (auto actor : {"PT", "ES", "FR", "DE", "IT"}) {
                                auto e = event(actor, "Parliament reviews public administration in fixture month " + std::to_string(month + 1) + ".");
                                e["outcome_kind"] = "policy"; events.append(e);
                            }
                            step(events);
                        }
                    }
                    order("Fund a national education reform to improve public services.");
                    order("Continue funding the national education reform to improve public services.");
                }
                store.sql("PRAGMA wal_checkpoint(TRUNCATE)");
            }
            write(root/(name + "-orders.json"), orders);
            Json::Value s; s["name"] = name; s["starting_save"] = name + ".sqlite";
            s["schedule"] = name + "-orders.json"; s["reference_cache"] = "references.json"; s["checks"] = checks;
            s["required_outcomes"].append(name == "peace" || name == "older" ? "Correct public_services reform project started or continued" : name == "invasion" ? "Feasible capacity-bounded invasion and war recorded" : name == "access_ceasefire" ? "Ceasefire and access offers exist in authoritative proposals" : name == "multiparty" ? "Exact revision answers; accepted only with all three signatures" : "Accepted regional demand tied to funded infrastructure, never prose-only fulfillment");
            s["permitted_alternatives"].append(name == "multiparty" ? "AI rejection or pending missing signature" : name == "invasion" ? "Partial advance within capacity" : name == "regional" ? "Accepted but still being funded" : "Feasible partial funding or a pending unsigned offer; no invented completion");
            manifest["scenarios"].append(s);
            std::cout << name << ": turn " << w.turn << std::endl;
        }
        write(root/"scenarios.json", manifest);
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
