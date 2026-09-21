#include "localhistoria/providers.h"
#include <algorithm>
namespace lh {
Decision LocalProvider::decide(const WorldState &w, const CountryContext &c) {
    Json::Value input;
    input["facts"] = c.facts;
    input["reference_only"] = briefReference(c.reference, 1200, 2400);
    auto schema =
        parse(R"({"type":"object","additionalProperties":false,"required":["actions"],)"
              R"("properties":{"actions":{"type":"array","maxItems":3,"items":{"type":"object",)"
              R"("additionalProperties":false,"required":["text"],)"
              R"("properties":{"text":{"type":"string","minLength":1,"maxLength":300}}}}}})");
    int count = c.facts.get("action_count", 3).asInt();
    schema["properties"]["actions"]["maxItems"] = count;
    if (c.facts.get("initiative_type", "reactive") != "reactive")
        schema["properties"]["actions"]["minItems"] = count;
    auto j = completion(
        input, schema,
        "You represent the actor specified by initiative_type in this fictional monthly sandbox. "
        "Obey action_count. For independent_regional you represent only regional_identity, not its "
        "governing nation: propose civic initiatives, negotiations or autonomy demands; never "
        "national military orders, treaties or armed rebellion. For independent initiatives act "
        "from your own agenda, geography and history. Write concise actions (one sentence each) "
        "it plausibly takes this month, given its facts, commitments, conflicts and recent events. "
        "Each action must be achievable "
        "within a month or be a realistic step toward a longer goal, and fit the nation's status: "
        "its population, economy (GDP in real currency), stability and wars limit what it can "
        "afford and attempt. recent_events already happened: do not repeat them; "
        "follow up, change course or respond to new developments. Refer to nations and regions by "
        "name.",
        0, 0.7, "decide", c.actor);
    if (!j.isObject() || j.getMemberNames() != std::vector<std::string>{"actions"} ||
        !j["actions"].isArray())
        throw ValidationError("Invalid decision shape");
    auto o = parseOrders(j["actions"]);
    validate(w, c.actor, o);
    if (o.size() > size_t(count) ||
        (c.facts.get("initiative_type", "reactive") != "reactive" && o.size() != size_t(count)))
        throw ValidationError("Decision action count mismatch");
    return {o, "Local model"};
}
std::string LocalProvider::chronicle(const Json::Value &input, const std::string &actor) {
    auto j = completion(
        input,
        parse(R"({"type":"object","additionalProperties":false,"required":["summary"],)"
              R"("properties":{"summary":{"type":"string","minLength":1,"maxLength":2400}}})"),
        "Write this nation's history up to the month in facts.date, in at most 250 words, oldest "
        "to newest. "
        "Start from previous_chronicle when given, otherwise condense the real history in "
        "reference_only into a few sentences. "
        "Give more space to recent years and to campaign_events since January 2026, which happened "
        "in this fictional campaign and override real history. "
        "Name the wars, treaties, policies and unresolved tensions that shape the nation's choices "
        "now. Do not invent events beyond these inputs. Write years, not month counts.",
        0, 0.3, "chronicle", actor);
    if (!j.isObject() || j.getMemberNames() != std::vector<std::string>{"summary"} ||
        !j["summary"].isString() || j["summary"].asString().empty() ||
        j["summary"].asString().size() > 2400)
        throw ValidationError("Invalid chronicle");
    return j["summary"].asString();
}
std::string LocalProvider::narrate(const std::vector<ResolvedEvent> &e) { return narrate(e, {}); }
std::string LocalProvider::narrate(const std::vector<ResolvedEvent> &e, const CountryContext &c) {
    Json::Value in;
    in["authoritative_events"] = encode(e);
    in["facts"] = c.facts;
    for (auto k : {"territories", "occupied_by_others", "occupying", "entities"})
        in["facts"].removeMember(k);
    auto j = completion(
        in,
        parse(R"({"type":"object","additionalProperties":false,"required":["text"],)"
              R"("properties":{"text":{"type":"string","maxLength":2000}}})"),
        "Write a brief fictional monthly journal strictly reflecting these events. Do not invent "
        "outcomes.",
        0, 0.7, "narrate", c.actor);
    if (!j.isObject() || j.getMemberNames() != std::vector<std::string>{"text"} ||
        !j["text"].isString() || j["text"].asString().size() > 2000 || j["text"].asString().empty())
        throw ValidationError("Invalid narration");
    return j["text"].asString();
}
Json::Value LocalProvider::takeAudit() {
    std::lock_guard lock(inference);
    auto result = audit;
    audit = Json::arrayValue;
    return result;
}
WorldDelta LocalProvider::adjudicate(const WorldState &w, const Json::Value &input,
                                     const Adjacency &adjacency, const Json::Value &responses) {
    std::vector<std::pair<std::string, std::string>> actions;
    for (auto &i : input["intentions"])
        actions.emplace_back(i["actor"].asString(), i["action"].asString());
    auto schema = operationSchema(w, input["facts"], actions);
    for (Json::ArrayIndex n = 0; n < input["intentions"].size(); ++n)
        if (input["intentions"][n]["regional_actor"].isString()) {
            auto &item = schema["properties"]["events"]["prefixItems"][n];
            auto regionalOp = schema["$defs"].get("op_regional_initiative", Json::nullValue);
            if (regionalOp.isNull())
                throw ValidationError("Regional context lacks its territory");
            regionalOp["properties"]["region"] = Json::objectValue;
            regionalOp["properties"]["region"]["const"] = input["intentions"][n]["regional_actor"];
            item["properties"]["operations"]["items"] = regionalOp;
            item["properties"]["operations"]["minItems"] = 1;
            item["properties"]["operations"]["maxItems"] = 1;
        }
    static const std::string instruction =
        "You are the game master of a fictional monthly strategy sandbox. Adjudicate this month's "
        "actions (intentions) against the facts. "
        "Return events: exactly one per action, in the given order, each opening with that "
        "action's first words; describe what actually happened to that action this month. "
        "Then world_events: only unplanned developments historians would record, involving other "
        "listed nations (an earthquake, flood or epidemic, a revolt or mass protest, a coup, an "
        "election result, a major attack), at most two and usually none; they change nothing on "
        "the map. "
        "History (past_events, related_history) already happened in earlier months: never repeat "
        "or re-adjudicate it; use it only as context and for causes. "
        "Each event has the acting actor, blocked_by (none, or the ID of an open commitment that "
        "prevents the action, in which case it has no operations; a nation that knowingly breaks a "
        "treaty acts anyway), causes (earlier event IDs from related_history, or []), "
        "a factual past-tense description of what happened (one sentence, under 160 characters), "
        "and then the operations that make the map match that description. "
        "Realism: a turn is one month. Judge what can realistically be achieved in a month given "
        "the facts, history and ongoing efforts. "
        "Large goals such as conquering a country, winning a war, founding a city or transforming "
        "an economy only advance by a step: describe partial progress, never complete them at "
        "once. "
        "Actions without map effects (reforms, investment, research, trade, speeches, "
        "negotiations) are valid events with no operations; describe their plausible effect. "
        "Ground every outcome in each nation's status: population, GDP (real currency), stability, "
        "wars and treaties. A small or poor economy cannot fund what a large one can; unstable or "
        "war-weary nations face unrest, desertion and failed reforms; a nation fighting a "
        "high-intensity war has little to spare. "
        "effects: economic and stability consequences of the event, only for nations it clearly "
        "affects; never invent numbers, the game computes them and corrects wars, seizures and "
        "treaties. Omit effects unless something changes; a world_event must list at least one. "
        "Name every new war and treaty the way historians and newspapers would: specific, never "
        "generic. Wars after places, peoples or causes (Second Iberian War, Galician War, Douro "
        "Border Conflict, Rif Insurgency); treaties after the signing city or purpose (Treaty of "
        "Porto, Lisbon Defence Pact, Madrid Trade Accord). Choose kind (war, border_conflict, "
        "insurgency, civil_war; alliance, defence_pact, non_aggression_pact, trade_agreement, "
        "ceasefire, peace_treaty, union, other) and a war's starting intensity (low, medium, "
        "high). "
        "set_conflict_intensity lets a party escalate (a major offensive) or scale down (a lull, "
        "withdrawal) an existing war; a ceasefire or peace treaty is add_commitment with that "
        "kind, and resolve_conflict only when the war has actually ended. "
        "Only a party can open, escalate, end or sign; a treaty needs every party's own action "
        "this month to mention the partner; nations only build for themselves. "
        "Treaties: months is how long one lasts (0 = open-ended); terms are what signing changes "
        "at once: end_conflict (a peace treaty ends that war), cede (its owner hands a region to "
        "another party), occupy (its owner lets another party hold a region while the treaty "
        "lasts), withdraw (an occupier hands a region back); a ceasefire pauses its war while in "
        "force; trade and economic treaties usually have no terms. amend_commitment renews a "
        "treaty (months from now; 0 keeps its end date) or revises its text and terms, with every "
        "party's agreement. Acting against a treaty partner breaches the treaty. "
        "Regional intentions are attributed by regional_actor: only regional_initiative is "
        "permitted, recording agenda, relations and an optional pending demand. They cannot "
        "authorize national actions. National actors may explicitly respond_regional with "
        "demand_id and accepted/rejected; pending demands never imply consent. Independence "
        "requires explicit national agreement or an established relevant campaign conflict; never "
        "autonomous armed rebellion. change_identity requires actor, name, change_type (rename or "
        "refounding), reason, and flag_design. Only substantive national renames or explicit "
        "constitutional/regime refoundings qualify; elections, border changes and alignments "
        "retain flags. create_actor also proposes flag_design (layout "
        "horizontal/vertical/cross/diagonal, three hex palette colors, emblem none/circle/diamond, "
        "explanation). Use conservative campaign-grounded symbolism, never invented historical "
        "claims. "
        "Operations: build (starts constructing a city, port, base, army or fleet in a region the "
        "actor controls, which takes months to finish; or marks an event_site where a notable "
        "incident happened, such as a disaster, summit, protest or battle, which appears at once "
        "and fades after three months unless later events involve it), "
        "within_reach_of gives each nation's real strength here; a nation marked too weak cannot "
        "occupy: describe a failed, aborted or symbolic action. Losses are computed by the game; "
        "do not state numbers. "
        "occupy (military control of a bordering region, owner unchanged), restore_control, "
        "transfer_territory (the owner cedes a region, or an occupier annexes a region it held "
        "before this month), "
        "move_entity (an army moves one neighboring region, a fleet up to 1,500 km), "
        "set_entity_status (never to finish construction: the game completes it at "
        "completes_turn), create_actor (a new state; the owner transfers it a region in the same "
        "event), "
        "open_conflict, resolve_conflict, add_commitment (treaties, alliances, agreements), "
        "resolve_commitment (only when explicitly ended). "
        "Decisions within an actor's own authority over its own land take effect at once. "
        "Examples: build a port in R -> build port. Raise an army in R -> build army (only in its "
        "own regions). Send troops to occupy or attack a bordering region R -> occupy R by the "
        "actor + open_conflict unless already at war. Invade neighbor X -> occupy one or two of "
        "its regions within reach + open_conflict unless already at war. "
        "Grant R independence as NAME (by its owner) -> create_actor + transfer_territory to it. "
        "Sign an alliance with X -> add_commitment. Reform education -> no operations.";
    // Reasoning is worth its generation time only for actions that can change wars, land or
    // treaties; a month of reforms, talks and trade is answered directly. Generation is the slowest
    // part of a month, so keep it tight.
    unsigned weighed = 0;
    for (auto &[actor, text] : actions)
        weighed += consequential(text);
    unsigned budget = weighed ? std::clamp<unsigned>(256 + 128 * weighed, 384, 768) : 0;
    Json::Value response;
    try {
        response = completion(input, schema, instruction, budget, 0.6, "adjudicate");
    } catch (const ValidationError &) {
        if (!budget)
            throw;
        // A reasoning run can exhaust its budget before answering; retry once without thinking.
        response = completion(input, schema, instruction, 0, 0.6, "adjudicate");
    }
    if (!response["events"].isArray() || response["events"].size() != actions.size())
        throw ValidationError("Every intention requires an outcome");
    for (Json::ArrayIndex i = 0; i < response["events"].size(); ++i) {
        auto &event = response["events"][i];
        auto &intent = input["intentions"][i];
        if (event["actor"] != intent["actor"] ||
            (event.isMember("action") &&
             event["action"] != actionOpening(intent["action"].asString())))
            throw ValidationError("Outcome attribution mismatch");
        if (intent["regional_actor"].isString() &&
            (event["operations"].size() != 1 ||
             event["operations"][0]["action"] != "regional_initiative" ||
             event["operations"][0]["region"] != intent["regional_actor"]))
            throw ValidationError("Regional outcome requires its bounded agenda operation");
        for (auto k : {"initiative_type", "regional_actor"}) {
            event.removeMember(k);
            if (intent.isMember(k))
                event[k] = intent[k];
        }
    }
    return compileOperations(w, response, adjacency, responses);
}
} // namespace lh
