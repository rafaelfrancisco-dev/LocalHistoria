#include "localhistoria/politics.h"
#include "localhistoria/status.h"
#include "world/world_internal.h"
#include <algorithm>
namespace lh {
using namespace detail;
static void collectEventIds(const Json::Value &v, std::set<std::string> &ids) {
    if (v.isObject()) {
        // Prompt inputs mix objects, arrays and prose; index only what is an object.
        if (v["shared"].isObject() && v["shared"]["events"].isObject())
            for (const auto &id : v["shared"]["events"].getMemberNames()) ids.insert(id);
        if (v["related_history"].isArray())
            for (auto &e : v["related_history"])
                if (e.isObject() && e["id"].isString())
                    ids.insert(e["id"].asString());
        for (auto &k : v.getMemberNames())
            collectEventIds(v[k], ids);
    } else if (v.isArray())
        for (auto &x : v)
            collectEventIds(x, ids);
}
std::string actionOpening(const std::string &text) {
    size_t at = 0;
    for (int words = 0; words < 6; ++words) {
        at = text.find(' ', text.find_first_not_of(' ', at));
        if (at == std::string::npos)
            return text;
    }
    return text.substr(0, at) + "…";
}
static Json::Value enumeration(const std::set<std::string> &values) {
    Json::Value j;
    j["type"] = "string";
    j["enum"] = Json::arrayValue;
    for (auto &v : values)
        j["enum"].append(v);
    return j;
}
Json::Value operationSchema(const WorldState &w, const Json::Value &facts,
                            const std::vector<std::pair<std::string, std::string>> &actions,
                            const Adjacency &adjacency, bool narrow) {
    std::set<std::string> actors, regions, entities, movable, conflicts, commitments, causes;
    if (facts["actors"].isObject())
        for (auto &a : facts["actors"].getMemberNames())
            if (w.hasActor(a))
                actors.insert(a);
    if (facts["territories"].isObject())
        for (auto &id : facts["territories"].getMemberNames())
            if (w.territories.isMember(id)) {
                regions.insert(id);
                for (auto k : {"owner", "controller"}) {
                    auto a = w.territories[id][k].asString();
                    if (w.hasActor(a))
                        actors.insert(a);
                }
            }
    const auto &entityFacts = facts["shared"]["entities"].isObject() ? facts["shared"]["entities"] : facts["entities"];
    if (entityFacts.isObject())
        for (auto &id : entityFacts.getMemberNames())
            if (w.entities.isMember(id) && (!facts["map_entities"].isArray() ||
                std::find(facts["map_entities"].begin(), facts["map_entities"].end(), Json::Value(id)) != facts["map_entities"].end())) {
                entities.insert(id);
                auto a = w.entities[id]["actor"].asString();
                if (w.hasActor(a))
                    actors.insert(a);
                // Only finished armies and fleets move; cities and ports offered as move targets
                // drew a small model away from occupy.
                auto &e = w.entities[id];
                if ((e["kind"] == "army" || e["kind"] == "fleet") && e["status"] == "active")
                    movable.insert(id);
            }
    if (w.hasActor(w.player))
        actors.insert(w.player);
    for (auto &id : w.conflicts.getMemberNames())
        if (isOpen(w.conflicts[id]))
            conflicts.insert(id);
    for (auto &id : w.commitments.getMemberNames())
        if (isOpen(w.commitments[id]))
            commitments.insert(id);
    collectEventIds(facts, causes);
    auto string = [](int max) {
        Json::Value j;
        j["type"] = "string";
        j["minLength"] = 1;
        j["maxLength"] = max;
        return j;
    };
    Json::Value actorId;
    actorId["type"] = "string";
    actorId["pattern"] = "^[A-Za-z0-9_-]{1,40}$";
    auto actor = enumeration(actors), region = enumeration(regions), entity = enumeration(entities);
    // Each operation is defined once and referenced from every outcome: inlined, 13 outcomes made a
    // 190 KB schema.
    Json::Value ops(Json::arrayValue), defs(Json::objectValue);
    auto ref = [](const std::string &name) {
        Json::Value j;
        j["$ref"] = "#/$defs/" + name;
        return j;
    };
    defs["flag_design"] = flagDesignSchema();
    // Defines an operation and returns its reference. `action` differs from `name` where one
    // operation has several narrowed forms, so each gets its own definition while the model still
    // writes the single action the compiler dispatches on.
    auto define = [&](const std::string &name, Json::Value properties,
                      const std::string &action = "") {
        Json::Value o;
        o["type"] = "object";
        o["additionalProperties"] = false;
        o["required"] = Json::arrayValue;
        o["required"].append("action");
        for (auto &k : properties.getMemberNames())
            o["required"].append(k);
        properties["action"]["type"] = "string";
        properties["action"]["enum"] = Json::arrayValue;
        properties["action"]["enum"].append(action.empty() ? name : action);
        o["properties"] = properties;
        defs["op_" + name] = o;
        return ref("op_" + name);
    };
    // Operations every actor may attempt, referenced from every outcome.
    auto op = [&](const char *name, Json::Value properties) {
        ops.append(define(name, properties));
    };
    Json::Value p;
    auto parties = parse(R"({"type":"array","minItems":2,"maxItems":6})");
    parties["items"] = actorId;
    p = Json::objectValue;
    p["actor_id"] = actorId;
    p["name"] = string(120);
    p["color"] = parse(R"({"type":"string","pattern":"^#[0-9a-fA-F]{6}$"})");
    p["flag_design"] = ref("flag_design");
    op("create_actor", p);
    p = Json::objectValue;
    p["actor"] = actor;
    p["name"] = string(120);
    p["reason"] = string(500);
    p["change_type"] = parse(R"({"type":"string","enum":["rename","refounding","correction"]})");
    // define() makes every property required, so a regime change is carried by an explicit
    // "none" the way blocked_by is. Only a refounding may name a system; the compiler checks
    // that again.
    p["government"] = Json::objectValue;
    p["government"]["type"] = "string";
    p["government"]["enum"] = Json::arrayValue;
    p["government"]["enum"].append("none");
    for (const auto &g : governments())
        p["government"]["enum"].append(g);
    p["flag_design"] = ref("flag_design");
    op("change_identity", p);
    if (!regions.empty()) {
        p = Json::objectValue;
        p["region"] = region;
        p["agenda"] = string(500);
        p["relations"] = parse(R"({"type":"string","enum":["neutral","cooperative","strained"]})");
        p["demand"] = parse(R"({"type":"string","maxLength":500})");
        op("regional_initiative", p);
        p = Json::objectValue;
        p["region"] = region;
        p["demand_id"] = string(100);
        p["response"] = parse(R"({"type":"string","enum":["accepted","rejected"]})");
        op("respond_regional", p);
    }
    // Kind leads name alphabetically, so the model decides what it is before naming it.
    p = Json::objectValue;
    p["actors"] = parties;
    p["intensity"] = parse(R"({"type":"string","enum":["low","medium","high"]})");
    p["kind"] =
        parse(R"({"type":"string","enum":["war","border_conflict","insurgency","civil_war"]})");
    p["name"] = string(120);
    p["text"] = string(500);
    op("open_conflict", p);
    // Terms are what signing changes at once; months how long a treaty lasts (0 = open-ended).
    Json::Value clauses(Json::arrayValue);
    auto clause = [&](const char *name, Json::Value properties) {
        Json::Value o;
        o["type"] = "object";
        o["additionalProperties"] = false;
        o["required"] = Json::arrayValue;
        o["required"].append("action");
        for (auto &k : properties.getMemberNames())
            o["required"].append(k);
        properties["action"]["type"] = "string";
        properties["action"]["enum"] = Json::arrayValue;
        properties["action"]["enum"].append(name);
        o["properties"] = properties;
        clauses.append(o);
    };
    if (!conflicts.empty()) {
        p = Json::objectValue;
        p["conflict"] = enumeration(conflicts);
        clause("end_conflict", p);
    }
    p = Json::objectValue;
    p["to"] = actorId;
    clause("grant_access", p);
    if (!regions.empty()) {
        p = Json::objectValue;
        p["region"] = region;
        p["to"] = actorId;
        clause("cede", p);
        p = Json::objectValue;
        p["region"] = region;
        p["by"] = actorId;
        clause("occupy", p);
        p = Json::objectValue;
        p["region"] = region;
        clause("withdraw", p);
    }
    auto terms = parse(R"({"type":"array","maxItems":4})");
    if (clauses.empty())
        terms["maxItems"] = 0;
    else
        terms["items"]["anyOf"] = clauses;
    auto months = parse(R"({"type":"integer","minimum":0,"maximum":120})");
    p = Json::objectValue;
    p["actors"] = parties;
    p["kind"] = parse(R"({"type":"string","enum":["alliance","defence_pact","non_aggression_pact",)"
                      R"("trade_agreement","ceasefire","peace_treaty","union","other"]})");
    p["months"] = months;
    p["name"] = string(120);
    p["terms"] = terms;
    p["text"] = string(500);
    op("add_commitment", p);
    if (!commitments.empty()) {
        p = Json::objectValue;
        p["commitment"] = enumeration(commitments);
        p["months"] = months;
        p["terms"] = terms;
        p["text"] = string(500);
        op("amend_commitment", p);
    }
    if (!regions.empty()) {
        // An event site marks where something happened and needs no control, so it is the one
        // build anyone may place anywhere. Everything else is narrowed per actor below.
        p = Json::objectValue;
        p["kind"] = parse(R"({"type":"string","enum":["event_site"]})");
        p["name"] = string(120);
        p["actor"] = actor;
        p["region"] = region;
        ops.append(define("build_site", p, "build"));
        p = Json::objectValue;
        p["region"] = region;
        // A preceding create_actor may introduce the recipient in this same response. The
        // compiler resolves that ID/name and rejects any recipient that was never created.
        p["to"] = actorId;
        op("transfer_territory", p);
        p = Json::objectValue;
        p["region"] = region;
        op("restore_control", p);
    }
    if (!entities.empty()) {
        if (!regions.empty() && !movable.empty()) {
            p = Json::objectValue;
            p["entity"] = enumeration(movable);
            p["region"] = region;
            op("move_entity", p);
        }
        p = Json::objectValue;
        p["entity"] = entity;
        p["status"] = parse(R"({"type":"string","enum":["active","inactive","destroyed"]})");
        op("set_entity_status", p);
    }
    if (!conflicts.empty()) {
        p = Json::objectValue;
        p["conflict"] = enumeration(conflicts);
        op("resolve_conflict", p);
    }
    if (!conflicts.empty()) {
        p = Json::objectValue;
        p["conflict"] = enumeration(conflicts);
        p["intensity"] = parse(R"({"type":"string","enum":["low","medium","high"]})");
        op("set_conflict_intensity", p);
    }
    if (!commitments.empty()) {
        p = Json::objectValue;
        p["commitment"] = enumeration(commitments);
        op("resolve_commitment", p);
    }
    Json::Value event;
    event["type"] = "object";
    event["additionalProperties"] = false;
    // jsoncpp sorts keys and llama.cpp grammars keep that order: description precedes operations,
    // action leads each operation.
    for (auto k : {"actor", "blocked_by", "causes", "description", "operations"})
        event["required"].append(k);
    // Denials must cite a real commitment, so a small model cannot invent prerequisites; physical
    // limits are checked by C++.
    event["properties"]["blocked_by"] = enumeration(commitments);
    event["properties"]["blocked_by"]["enum"].insert(0, "none");
    event["properties"]["actor"] = actor;
    event["properties"]["description"] = string(160);
    event["properties"]["causes"] = parse(R"({"type":"array","maxItems":4})");
    // Economic and stability consequences are qualitative; C++ turns them into bounded numbers.
    // Optional before event text version 2; since then every outcome rates at least one.
    auto effect = parse(
        R"({"type":"string","enum":["severe_decline","decline","none","growth","strong_growth"]})");
    defs["effect"] =
        parse(R"({"type":"object","additionalProperties":false,"required":["actor","economy",)"
              R"("stability"]})");
    defs["effect"]["properties"]["actor"] = actor;
    defs["effect"]["properties"]["economy"] = effect;
    defs["effect"]["properties"]["stability"] = effect;
    event["properties"]["effects"] = parse(R"({"type":"array","maxItems":3})");
    event["properties"]["effects"]["items"] = ref("effect");
    // Required under the realism rules: a small model never volunteers an optional field, and an
    // outcome without a map change keeps its text and effects only when it names what it was.
    // The key sorts after operations, so the model writes its operations first and classifies
    // what remains; placed before them, a "negotiation" label talked it out of writing the treaty.
    if (w.engine.get("lifecycle_version", 0).asInt() == 1) {
        event["properties"]["outcome_kind"] = parse(
            R"({"type":"string","enum":["reform","investment","negotiation","protest","none"]})");
        event["required"].append("outcome_kind");
        // Version 2: every outcome has a consequence the model rates, so "none" gives way to the
        // two kinds it used to hide (deployments and policing; laws, decrees and statements).
        if (w.engine.get("event_text_version", 0).asInt() >= 2) {
            event["properties"]["outcome_kind"]["enum"] = parse(
                R"(["reform","investment","negotiation","protest","security","policy"])");
            // Required alone, the list came back as one "none"/"none" entry in eight of nineteen
            // live outcomes, which left the consequence to a C++ default. An entry must move at
            // least one axis, so the model grounds the direction and size itself. Named so it sorts
            // after operations, like outcome_kind: rated first, it talked the model out of the
            // treaty and occupation operations (4 of 12 written against 7 of 12). The provider
            // renames it back to effects.
            auto moved = parse(
                R"({"type":"string","enum":["severe_decline","decline","growth","strong_growth"]})");
            Json::Value economyMoves = defs["effect"], stabilityMoves = defs["effect"];
            economyMoves["properties"]["economy"] = moved;
            stabilityMoves["properties"]["economy"] = parse(R"({"const":"none"})");
            stabilityMoves["properties"]["stability"] = moved;
            defs["rated_effect"]["anyOf"].append(economyMoves);
            defs["rated_effect"]["anyOf"].append(stabilityMoves);
            auto rated = parse(R"({"type":"array","minItems":1,"maxItems":3})");
            rated["items"] = ref("rated_effect");
            event["properties"].removeMember("effects");
            event["properties"]["outcome_effects"] = rated;
            event["required"].append("outcome_effects");
        }
    }
    if (causes.empty())
        event["properties"]["causes"]["maxItems"] = 0;
    else
        event["properties"]["causes"]["items"] = enumeration(causes);
    event["properties"]["operations"] = parse(R"({"type":"array","maxItems":5})");
    event["properties"]["operations"]["items"]["anyOf"] = ops;
    // Building and taking ground are narrowed to what each actor can physically do: it builds only
    // where it already governs, and occupies only what it can reach. A small model that means to
    // invade but reaches for the wrong verb used to have its operation dropped and the war stall;
    // now the grammar cannot express it. Nothing here reads the wording of the order.
    std::map<std::string, Json::Value> byActor;
    std::set<std::string> acting;
    for (const auto &[a, text] : actions)
        acting.insert(a);
    for (const auto &a : acting) {
        std::set<std::string> governs, within;
        for (auto &id : regions) {
            if (w.territories[id]["controller"] == a)
                governs.insert(id);
            else if (reaches(w, adjacency, a, id))
                within.insert(id);
        }
        Json::Value own(Json::arrayValue);
        if (!governs.empty()) {
            p = Json::objectValue;
            p["kind"] = parse(R"({"type":"string","enum":["city","port","base","army","fleet"]})");
            p["name"] = string(120);
            p["actor"] = Json::objectValue;
            p["actor"]["const"] = a;
            p["region"] = enumeration(governs);
            own.append(define("build_" + a, p, "build"));
        }
        if (!within.empty()) {
            p = Json::objectValue;
            p["region"] = enumeration(within);
            p["by"] = Json::objectValue;
            p["by"]["const"] = a;
            own.append(define("occupy_" + a, p, "occupy"));
        }
        if (!own.empty())
            byActor[a] = own;
    }
    Json::Value schema;
    schema["type"] = "object";
    schema["additionalProperties"] = false;
    schema["required"].append("events");
    schema["required"].append("world_events");
    // One outcome per action, in order, opening with that action's first words: without them a
    // small model repeats one outcome for all of a nation's actions. The whole text kept it on
    // track too, but cost dozens of generated tokens per event.
    auto &outcomes = schema["properties"]["events"];
    outcomes["type"] = "array";
    outcomes["prefixItems"] = Json::arrayValue;
    for (auto &[a, text] :
         std::vector(actions.begin(), actions.begin() + std::min<size_t>(actions.size(), 37))) {
        auto item = event;
        item["required"].insert(0, "action");
        item["properties"]["action"]["const"] = actionOpening(text);
        item["properties"]["actor"] = Json::objectValue;
        item["properties"]["actor"]["const"] = a;
        // Only a commitment the actor is party to can hold it back; otherwise a small model cites
        // any treaty it sees.
        std::set<std::string> binding;
        for (auto &id : commitments)
            for (auto &party : w.commitments[id]["actors"])
                if (party == a)
                    binding.insert(id);
        item["properties"]["blocked_by"] = enumeration(binding);
        item["properties"]["blocked_by"]["enum"].insert(0, "none");
        if (byActor.contains(a))
            for (auto &own : byActor.at(a))
                item["properties"]["operations"]["items"]["anyOf"].append(own);
        if (narrow) {
            // Authority is stable for existing mobile formations and treaty membership. Do not
            // strength-filter occupation or transit: earlier operations can supply access, fleets
            // or ground. Existing hostile entity status choices remain available for warfare.
            auto &choices = item["properties"]["operations"]["items"]["anyOf"];
            Json::Value allowed(Json::arrayValue);
            for (const auto &choice : choices) {
                auto definition = choice["$ref"].asString();
                definition = definition.substr(definition.find_last_of('/') + 1);
                const auto operation = defs[definition]["properties"]["action"]["enum"][0].asString();
                const char *field = operation == "move_entity" ? "entity" :
                    (operation == "amend_commitment" || operation == "resolve_commitment") ? "commitment" :
                    (operation == "resolve_conflict" || operation == "set_conflict_intensity") ? "conflict" : nullptr;
                if (!field) { allowed.append(choice); continue; }
                std::set<std::string> ids;
                for (const auto &id : defs[definition]["properties"][field]["enum"]) {
                    auto value = id.asString();
                    if (operation == "move_entity" ? w.entities[value]["actor"] == a :
                        isParty(std::string(field) == "commitment" ? w.commitments[value] : w.conflicts[value], a)) ids.insert(value);
                }
                if (ids.empty()) continue;
                auto projected = defs[definition];
                projected["properties"][field] = enumeration(ids);
                auto name = definition + "_authority_" + a;
                defs[name] = projected;
                allowed.append(ref(name));
            }
            choices = allowed;
        }
        outcomes["prefixItems"].append(item);
    }
    outcomes["minItems"] = outcomes["maxItems"] = Json::UInt(outcomes["prefixItems"].size());
    // World events are unplanned developments: narrative only, never decisions of the player's
    // nation, so they cannot invent construction, treaties or conquests that nobody ordered.
    auto development = event;
    development["required"] = Json::arrayValue;
    for (auto k : {"actor", "causes", "description", "effects"})
        development["required"].append(k);
    // A development has no operations to write first, so it keeps the plain name.
    if (development["properties"].isMember("outcome_effects")) {
        development["properties"]["effects"] = development["properties"]["outcome_effects"];
        development["properties"].removeMember("outcome_effects");
    }
    // A development is kept only when it changes something, so it must name at least one effect.
    development["properties"]["effects"]["minItems"] = 1;
    development["properties"].removeMember("operations");
    development["properties"].removeMember("outcome_kind");
    development["properties"].removeMember("blocked_by");
    std::set<std::string> others(actors);
    others.erase(w.player);
    if (others.empty())
        others = actors;
    development["properties"]["actor"] = enumeration(others);
    schema["properties"]["world_events"]["type"] = "array";
    schema["properties"]["world_events"]["maxItems"] = 2;
    schema["properties"]["world_events"]["items"] = development;
    schema["$defs"] = defs;
    return schema;
}
} // namespace lh
