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
    const bool proposals = c.facts["treaty_proposals"].isObject() &&
                           !c.facts["treaty_proposals"].empty() &&
                           c.facts["initiative_type"] != "independent_regional";
    if (proposals) {
        schema["properties"]["responses"] = parse(R"({"type":"array","maxItems":8,"items":{
          "type":"object","additionalProperties":false,"required":["proposal","revision","accept"],
          "properties":{"proposal":{"type":"string"},"revision":{"type":"integer","minimum":1},
          "accept":{"type":"boolean"}}}})");
    }
    Json::Value answers(Json::arrayValue);
    int count = c.facts.get("action_count", 3).asInt();
    schema["properties"]["actions"]["maxItems"] = count;
    bool independent = c.facts.get("initiative_type", "reactive") != "reactive";
    if (independent)
        schema["properties"]["actions"]["minItems"] = count;
    static const std::string instruction =
        "You represent the actor specified by initiative_type in this fictional monthly sandbox. "
        "Obey action_count. Answer pending treaty_proposals with responses containing the exact "
        "proposal ID, revision, and accept boolean; prose conveys no signature. For "
        "independent_regional you represent only regional_identity, not its "
        "governing nation: act on what is actually contested between the region and its capital or "
        "its neighbours, and on what region_situation shows it is living through - revenue and "
        "resources, autonomy, borders, displaced people, garrisons, war spillover, or a demand the "
        "capital has left unanswered; never national military orders, treaties or armed rebellion. "
        "An independent initiative must be something that changes this actor's position, its "
        "relations or its territory, never a ceremony, festival, heritage or cultural declaration, "
        "awareness campaign, town hall or symbolic announcement. For independent initiatives act "
        "from your own agenda, geography and history. Write concise actions (one sentence each) "
        "it plausibly takes this month, given its facts, commitments, conflicts and recent events. "
        "Each action must be achievable "
        "within a month or be a realistic step toward a longer goal, and fit the nation's status: "
        "its population, economy (GDP in real currency), stability and wars limit what it can "
        "afford and attempt. Its government bounds its politics: a monarchy, junta or one-party "
        "state holds no competitive election. "
        "A war that has cost heavily and taken no ground for months is a "
        "reason to seek terms - a ceasefire, a peace treaty, a withdrawal or outside mediation - "
        "not only to escalate or to protest; propose terms the nation could actually live with. "
        "recent_events already happened: do not repeat them; "
        "follow up, change course or respond to new developments. Refer to nations and regions by "
        "name.";
    auto ask = [&](const std::string &extra) {
        auto j = completion(input, schema, instruction + extra, 0, 0.7, "decide", c.actor);
        if (!j.isObject() || !j["actions"].isArray())
            throw ValidationError("Invalid decision shape");
        for (auto &key : j.getMemberNames())
            if (key != "actions" && !(proposals && key == "responses"))
                throw ValidationError("Invalid decision field");
        answers = Json::arrayValue;
        std::set<std::string> seen;
        if (j.isMember("responses") && (!j["responses"].isArray() || j["responses"].size() > 8))
            throw ValidationError("Invalid proposal answers");
        for (auto answer : j["responses"]) {
            auto id = answer["proposal"].asString();
            const auto &p = c.facts["treaty_proposals"][id];
            if (!p.isObject() || answer["revision"] != p["revision"] ||
                !answer["accept"].isBool() || !seen.insert(id).second)
                throw ValidationError("Invalid proposal answer revision");
            answer["actor"] = c.actor;
            answers.append(answer);
        }
        auto o = parseOrders(j["actions"]);
        validate(w, c.actor, o);
        if (o.size() > size_t(count) || (independent && o.size() != size_t(count)))
            throw ValidationError("Decision action count mismatch");
        return o;
    };
    auto o = ask("");
    // An independent slot has one action a month; a ceremony spends it on nothing. Re-ask once,
    // then take whatever comes back: a worse month is not a broken one.
    if (independent && !o.empty() &&
        std::all_of(o.begin(), o.end(), [](const Order &x) { return ceremonial(x.text); }))
        try {
            o = ask(" The previous answer was a symbolic gesture. Write instead what this actor "
                    "does about something at stake for it this month.");
        } catch (const ValidationError &) {
        }
    return {o, "Local model", answers};
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
    auto schema = operationSchema(w, input["facts"], actions, adjacency);
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
        "high-intensity war has little to spare. Elections, opposition parties and legislatures "
        "exist only where a nation's government has them. "
        "independent_outcome identifies a separate reform, investment, negotiation or protest; "
        "effects require that supported independent result. Operations derive consequences in C++. "
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
        "kind, and resolve_conflict only when the war has actually ended. A conflict whose front "
        "reads as a stalemate is a plausible reason for a party to open talks or accept terms. "
        "Treaties become proposals requiring explicit signatures to their revision. Names and "
        "prose never authorize signatures; nations only build for themselves. "
        "Treaties: months is how long one lasts (0 = open-ended); terms are what signing changes "
        "at once: end_conflict (a peace treaty ends that war), cede (its owner hands a region to "
        "another party), occupy (its owner lets another party hold a region while the treaty "
        "lasts), withdraw (an occupier hands a region back), grant_access (the other parties "
        "let one party base and move forces on their land); a ceasefire pauses its war while in "
        "force; trade and economic treaties usually have no terms. amend_commitment renews a "
        "treaty (months from now; 0 keeps its end date) or revises its text and terms, with every "
        "party's agreement. Acting against a treaty partner breaches the treaty. "
        "Regional intentions are attributed by regional_actor: only regional_initiative is "
        "permitted, recording agenda, relations and an optional pending demand. They cannot "
        "authorize national actions. National actors may explicitly respond_regional with "
        "demand_id and accepted/rejected; pending demands never imply consent. Independence "
        "requires explicit national agreement or an established relevant campaign conflict; never "
        "autonomous armed rebellion by a region; sustained unrest raises its own rebels "
        "without being asked. change_identity requires actor, name, change_type (rename or "
        "refounding), reason, government and flag_design. Only substantive national renames "
        "or explicit constitutional/regime refoundings qualify; elections, border changes "
        "and alignments retain flags. government names the new political system on a "
        "refounding, and is \"none\" otherwise. create_actor also proposes flag_design (layout "
        "horizontal/vertical/cross/diagonal, three hex palette colors, emblem none/circle/diamond, "
        "explanation). Use conservative campaign-grounded symbolism, never invented historical "
        "claims. "
        "Operations: build (starts constructing a city, port, base, army or fleet in a region the "
        "actor already controls, which takes months to finish; or marks an event_site where a "
        "notable incident happened, such as a disaster, summit, protest or battle, which appears "
        "at once and fades after three months unless later events involve it). "
        "To take ground use occupy, never build: a nation cannot build in land it does not hold. "
        "Each nation's offensive lists the regions it could take this month and how many; "
        "when an order aims beyond those regions, resolve its feasible first step by occupying "
        "a listed region, leaving the distant destination and regime change for later months. "
        "A superior force ordered forward should gain reachable ground unless the supplied "
        "facts give a specific obstacle; do not invent stronger defenders or failed logistics. "
        "within_reach_of gives its real strength in each, and a nation marked too weak cannot "
        "occupy: describe a failed, aborted or symbolic action. Where one side is decisively "
        "stronger the front also moves on its own, so an outcome may describe fighting with no "
        "operation at all; set_conflict_intensity is how a party presses or eases that tempo. "
        "Losses are computed by the game; do not state numbers. "
        "occupy (military control of a bordering region, owner unchanged), restore_control, "
        "transfer_territory (the owner cedes a region, or an occupier annexes a region it held "
        "before this month), "
        "move_entity (an army moves one neighboring region it may enter, or sails with a fleet "
        "in its region; a fleet sails up to 1,500 km of sea to a coast), "
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
    // Reachable offensive options also deserve reasoning, regardless of the order's vocabulary.
    // Otherwise "drive to the capital" bypasses the thinking used for "invade" and the small
    // model ignores the same strength facts. Keep the existing bounded budget.
    unsigned weighed = 0;
    for (auto &[actor, text] : actions)
        weighed +=
            consequential(text) || !input["facts"]["actors"][actor]["offensive"]["regions"].empty();
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
    // The history the model was shown this month: anything it re-reports as a new background
    // development is the past, not a development.
    std::set<std::string> priorEvents;
    for (const auto &a : input["facts"]["actors"].getMemberNames())
        for (const auto &past : input["facts"]["actors"][a]["related_history"])
            if (past["id"].isString())
                priorEvents.insert(past["id"].asString());
    return compileOperations(w, response, adjacency, responses, priorEvents);
}
} // namespace lh
