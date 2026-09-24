#include "server/campaign_factory.h"
#include "server/server_internal.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
namespace lh::detail {
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
    if (!j["state"].isNull()) {
        auto w = decode(j["state"]);
        for (auto &month : j["journal"]) {
            month["changes"] = changeSummaries(s.store.monthChanges(month["turn"].asInt()), w);
            // Engine consequences never pass through a delta, so they are not in monthChanges;
            // the structural ones still belong in "What changed".
            for (const auto &event : month["events"]) {
                auto action = event["action"].asString();
                if (action != "front_advance" && action != "revolt" && action != "secession" &&
                    action != "regime_change")
                    continue;
                Json::Value change;
                change["kind"] = action == "front_advance" ? "territory" : "actor";
                change["id"] = action == "front_advance" ? event["region"] : event["actor"];
                change["summary"] = event["text"];
                month["changes"].append(change);
            }
        }
    }
    j["active_jobs"] = s.store.query("SELECT id,status FROM jobs WHERE status='running'");
    j["llama_endpoint"] = s.provider.endpointValue();
    if (b.isNull())
        replyCached(req, cb, j);
    else
        reply(cb, j);
}
void flag(Server &s, const HttpRequestPtr &req, Json::Value, const CB &cb) {
    std::lock_guard lock(s.mutex);
    const auto w = decode(s.store.campaign());
    auto a = req->getParameter("actor"), rev = req->getParameter("revision");
    if (req->getParameter("campaign") != w.campaignId || !w.hasActor(a))
        throw ValidationError("Unknown campaign flag");
    const auto &actor = w.actors[a];
    std::string svg;
    if (rev == "fallback")
        svg = makeFlag(Json::nullValue, w.campaignId + a, "legacy")["svg"].asString();
    else {
        if (!isSmallNumber(rev))
            throw ValidationError("Invalid flag revision");
        auto n = std::stoul(rev);
        if (!actor["identity_revisions"].isArray() || n >= actor["identity_revisions"].size())
            throw ValidationError("Unknown flag revision");
        const auto &r = actor["identity_revisions"][Json::ArrayIndex(n)];
        svg = r["svg"].asString();
        if (svg.empty()) {
            auto code = r["static_flag"].asString();
            if (!isCountryCode(code))
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
void modelConfig(Server &s, const HttpRequestPtr &, Json::Value b, const CB &cb) {
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
void countries(Server &s, const HttpRequestPtr &, Json::Value, const CB &cb) {
    std::lock_guard l(s.mutex);
    auto w = decode(s.store.campaign());
    auto rows = s.reference.query("SELECT code,name FROM countries ORDER BY name");
    if (!w.campaignId.empty()) {
        rows = Json::arrayValue;
        std::set<std::string> neighbors;
        for (auto &n : s.reference.query("SELECT b FROM neighbors WHERE a=?", {w.player}))
            neighbors.insert(n["b"].asString());
        for (auto &a : w.actors.getMemberNames()) {
            Json::Value c;
            c["code"] = a;
            c["name"] = w.actors[a]["name"];
            // Landless actors without a flag of their own are listed only when searched for.
            c["has_flag"] =
                isCountryCode(a) && std::filesystem::exists("web/flags/" + lowercase(a) + ".svg");
            // The campaign's current system, so a nation refounded during play lists what it is
            // now rather than what it was in January.
            auto government =
                static_cast<const Json::Value &>(w.actors)[a]["status"]["government"].asString();
            if (validGovernment(government))
                c["government"] = governmentLabel(government);
            if (neighbors.contains(a))
                c["neighbor"] = true;
            rows.append(c);
        }
    }
    reply(cb, rows);
}
void country(Server &s, const HttpRequestPtr &req, Json::Value, const CB &cb) {
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
void jobs(Server &s, const HttpRequestPtr &req, Json::Value, const CB &cb) {
    std::lock_guard l(s.mutex);
    auto id = req->getParameter("id");
    auto job = s.references.job(id);
    reply(cb, job.isNull() ? s.store.job(id) : job);
}
void turns(Server &s, const HttpRequestPtr &, Json::Value b, const CB &cb) {
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
    bool governmentBackfill = !hasGovernment(w);
    bool backfill = !hasStatus(w);
    if (backfill)
        seedStatus(w, s.statusSeed, false);
    if (militaryBackfill)
        seedMilitary(w, s.statusSeed);
    if (governmentBackfill)
        seedGovernment(w, s.statusSeed);
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
    // Answers to treaties offered last month, and to demands from regions the player governs;
    // each pending one at most once.
    std::set<std::string> answered;
    if (b["responses"].size() > 8)
        throw ValidationError("Too many proposal responses");
    for (auto &r : b["responses"]) {
        if (r.isObject() && r.isMember("demand")) {
            if (r.getMemberNames() != std::vector<std::string>{"accept", "demand", "region"} ||
                !r["accept"].isBool() || !r["demand"].isString() || !r["region"].isString())
                throw ValidationError("Invalid demand response");
            auto region = r["region"].asString(), id = r["demand"].asString();
            bool open = false;
            for (const auto &d : std::as_const(w.regionalPolitics)[region]["demands"])
                open = open || (d["id"] == id && d["status"] == "pending");
            if (!open || !w.territories.isMember(region) ||
                w.territories[region]["controller"] != w.player ||
                !answered.insert("demand:" + region + "/" + id).second)
                throw ValidationError("No pending demand " + id);
            continue;
        }
        if (!r.isObject() ||
            r.getMemberNames() != std::vector<std::string>{"accept", "proposal", "revision"} ||
            !r["accept"].isBool() || !r["proposal"].isString() || !r["revision"].isInt())
            throw ValidationError("Invalid proposal response");
        auto pid = r["proposal"].asString();
        if (!w.proposals.isMember(pid) || w.proposals[pid]["status"] != "pending" ||
            !isParty(w.proposals[pid], w.player) ||
            w.proposals[pid].get("revision", 1) != r["revision"] ||
            w.proposals[pid]["answers"].isMember(w.player) || !answered.insert(pid).second)
            throw ValidationError("No pending proposal " + pid);
    }
    auto job = s.store.submit(b["expected_turn"].asInt(), b["idempotency_key"].asString(), payload);
    auto id = job["id"].asString();
    startSubmitted(s.store, id, [&] {
        auto plan = s.turns.plan(w, orders);
        plan.narrate = b.get("narration", true).asBool();
        auto &refs = plan.refs;
        if (b.isMember("responses"))
            refs["proposal_responses"] = b["responses"];
        if (backfill)
            refs["status_seed"] = encode(s.statusSeed);
        if (militaryBackfill)
            refs["military_seed"] = encode(s.statusSeed);
        if (governmentBackfill)
            refs["government_seed"] = encode(s.statusSeed);
        s.references.prepare(w);
        s.worker.add([&s, plan, id] { s.turns.run(plan, id); });
    });
    reply(cb, s.store.job(id), 202);
}
void cancelTurn(Server &s, const HttpRequestPtr &, Json::Value b, const CB &cb) {
    std::lock_guard l(s.mutex);
    auto id = b.get("job", "").asString();
    auto job = s.store.job(id);
    if (job["status"] != "running")
        throw ValidationError("Month is not running");
    s.turns.cancel(id);
    reply(cb, job, 202);
}
void prepare(Server &s, const HttpRequestPtr &, Json::Value b, const CB &cb) {
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
void replay(Server &s, const HttpRequestPtr &, Json::Value, const CB &cb) {
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
        for (auto &[from, to] : s.adjacency)
            for (auto &region : to)
                j["adjacency"][from].append(region);
    }
    for (auto &t : j["turns"])
        if (t["reference"].asString().find("\"sea_route_revision\"") != std::string::npos) {
            j["sea_routes"] = encodeSeaRoutes(s.adjacency);
            break;
        }
    for (auto &t : j["turns"])
        for (auto k : {"state", "intentions", "events", "reference"})
            t[k] = parse(t[k].asString());
    reply(cb, j);
}
} // namespace lh::detail
