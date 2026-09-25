#include "localhistoria/domestic.h"
#include "localhistoria/providers.h"
#include "localhistoria/turn.h"
#include <algorithm>
#include <exception>
namespace lh {
namespace {
struct ValidateAnswer {
    InferenceRequest &request;
    int exceptions = std::uncaught_exceptions();
    Json::ArrayIndex first = request.audit.size();
    ~ValidateAnswer() {
        if (request.audit.size() <= first) return;
        auto &last = request.audit[request.audit.size() - 1];
        if (last["status"] == "answered")
            last["status"] = std::uncaught_exceptions() > exceptions ? "validation_failed" : "success";
    }
};
}

bool clearlyDomestic(const WorldState &w, const std::string &actor, const std::string &text) {
    const auto lower = lowercase(text);
    // Only a narrow, explicit domestic action class is admitted. Everything ambiguous retains
    // reasoning, as do named foreign/territorial targets and military paraphrases.
    bool domestic = false;
    for (const auto *verb : {"reform", "invest", "investment", "research"})
        domestic = domestic || containsWord(lower, verb);
    bool subject = false;
    for (const auto *noun : {"education", "schools", "healthcare", "hospitals", "science", "tax", "pensions", "courts", "sanitation"})
        subject = subject || containsWord(lower, noun);
    if (!domestic || !subject || consequential(text)) return false;
    for (const auto *cue : {"foreign", "border", "military", "army", "navy", "fleet", "troops", "weapons", "defence", "defense", "security", "capital", "front", "advance", "drive", "push", "liberate", "recapture", "allies", "treaty", "agreement", "offer", "sign", "occupy", "territory", "missile", "drone", "nuclear", "mobilize", "mobilise", "strike", "war", "aid", "with", "against", "targets:"})
        if (containsWord(lower, cue)) return false;
    auto targets = mentioned(w, actor, {{text, {}}});
    return targets.actors.empty() && targets.regions.empty();
}
std::set<std::string> treatyOffer(const WorldState &w, const std::string &actor,
                                  const std::string &text) {
    const auto lower = lowercase(text);
    auto any = [&](std::initializer_list<const char *> words) {
        return std::any_of(words.begin(), words.end(),
                           [&](const char *word) { return containsWord(lower, word); });
    };
    // Only an order that makes an offer: answering someone else's offer is not one.
    if (!any({"offer", "offers", "offering", "propose", "proposes", "proposing", "proposal"}) ||
        !any({"treaty", "treaties", "ceasefire", "cease-fire", "armistice", "truce", "pact",
              "alliance", "agreement", "accord", "access", "peace", "deal"}) ||
        any({"accept", "accepts", "reject", "rejects", "decline", "declines", "refuse", "refuses",
             "answer", "respond", "ignore"}))
        return {};
    return mentioned(w, actor, {{text, {}}}).actors;
}
void narrowIntentions(const WorldState &w, Json::Value &schema, const Json::Value &intentions,
                      bool offers) {
    auto &defs = schema["$defs"];
    auto &items = schema["properties"]["events"]["prefixItems"];
    auto ref = [](const std::string &name) {
        Json::Value j;
        j["$ref"] = "#/$defs/" + name;
        return j;
    };
    if (offers)
        // Keys generate alphabetically: as "clauses", treaty terms are drafted before the kind.
        // The provider renames them back to terms.
        for (const auto &name : defs.getMemberNames()) {
            auto &properties = defs[name]["properties"];
            const auto action = properties["action"]["enum"][0];
            if ((action == "add_commitment" || action == "amend_commitment") &&
                properties.isMember("terms")) {
                properties["clauses"] = properties["terms"];
                properties.removeMember("terms");
            }
        }
    for (Json::ArrayIndex n = 0; n < intentions.size() && n < items.size(); ++n) {
        const auto &intent = intentions[n];
        if (intent["regional_actor"].isString())
            continue;
        const auto actor = intent["actor"].asString(), text = intent["action"].asString();
        auto &item = items[n];
        auto parties = offers ? treatyOffer(w, actor, text) : std::set<std::string>{};
        if (!parties.empty() && defs.isMember("op_add_commitment")) {
            // A clear offer records one: the 4B otherwise describes it and closes an empty
            // operations array. Parties are the offering nation and those it names. The 4B takes
            // the first enum value, which had it grant access to the counterparty; the offering
            // nation comes first.
            Json::Value names(Json::arrayValue);
            names.append(actor);
            for (const auto &p : parties)
                names.append(p);
            auto offer = defs["op_add_commitment"];
            auto &actors = offer["properties"]["actors"];
            actors["items"] = Json::objectValue;
            actors["items"]["type"] = "string";
            actors["items"]["enum"] = names;
            actors["maxItems"] = std::min<Json::UInt>(names.size(), 6);
            for (auto &clause : offer["properties"]["clauses"]["items"]["anyOf"])
                if (clause["properties"]["action"]["enum"][0] == "grant_access")
                    clause["properties"]["to"] = actors["items"];
            auto name = "op_add_commitment_offer_" + std::to_string(n);
            defs[name] = offer;
            auto &operations = item["properties"]["operations"];
            operations["items"] = ref(name);
            // One treaty holds several clauses; a second slot drew a duplicate offer.
            operations["minItems"] = 1;
            operations["maxItems"] = 1;
        } else if (distinctProjects(w) && item["properties"].isMember("project") &&
                   defs.isMember("domestic_project") && clearlyDomestic(w, actor, text)) {
            // A clear reform funds a project: null was the 4B's first and easiest answer. Its id
            // is new ("") or one of this nation's own records, never an invented one.
            auto project = defs["domestic_project"];
            auto &id = project["properties"]["id"];
            id = Json::objectValue;
            id["type"] = "string";
            id["enum"].append("");
            for (const auto &known :
                 std::as_const(w.actors)[actor]["status"]["projects"].getMemberNames())
                id["enum"].append(known);
            auto name = "domestic_project_" + std::to_string(n);
            defs[name] = project;
            item["properties"]["project"] = ref(name);
        }
    }
}
Decision LocalProvider::decide(const WorldState &w, const CountryContext &c) {
    InferenceRequest request;
    return decide(w, c, request);
}
Decision LocalProvider::decide(const WorldState &w, const CountryContext &c, InferenceRequest &request) {
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
    // Every offered proposal must be answered, true to sign or false to decline, keyed by its ID:
    // a small model otherwise writes "we accept" as an action and never signs. The revision
    // answered is the one these facts show.
    if (proposals) {
        auto &r = schema["properties"]["responses"];
        r["type"] = "object";
        r["additionalProperties"] = false;
        for (const auto &id : c.facts["treaty_proposals"].getMemberNames()) {
            r["properties"][id]["type"] = "boolean";
            r["required"].append(id);
        }
        schema["required"].append("responses");
    }
    Json::Value answers(Json::arrayValue);
    int count = c.facts.get("action_count", 3).asInt();
    schema["properties"]["actions"]["maxItems"] = count;
    bool independent = c.facts.get("initiative_type", "reactive") != "reactive";
    if (independent)
        schema["properties"]["actions"]["minItems"] = count;
    static const std::string instruction =
        "You represent the actor specified by initiative_type in this fictional monthly sandbox. "
        "Obey action_count. Answer every pending treaty_proposals entry in responses by its ID: "
        "true signs its terms, false declines; prose conveys no signature. For "
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
        ValidateAnswer validation{request};
        auto j = completion(input, schema, instruction + extra, 0, 0.7, "decide", c.actor, request);
        if (!j.isObject() || !j["actions"].isArray())
            throw ValidationError("Invalid decision shape");
        for (auto &key : j.getMemberNames())
            if (key != "actions" && !(proposals && key == "responses"))
                throw ValidationError("Invalid decision field");
        answers = Json::arrayValue;
        const auto responses = j.get("responses", Json::Value(Json::objectValue));
        if (!responses.isObject() || (proposals && responses.getMemberNames() != c.facts["treaty_proposals"].getMemberNames()))
            throw ValidationError("Invalid proposal answers");
        for (const auto &id : responses.getMemberNames()) {
            const auto &p = c.facts["treaty_proposals"][id];
            if (!p.isObject() || !responses[id].isBool())
                throw ValidationError("Invalid proposal answer");
            Json::Value answer;
            answer["proposal"] = id;
            answer["revision"] = p["revision"];
            answer["accept"] = responses[id];
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
    const auto firstAnswers = answers;
    if (independent && !o.empty() &&
        std::all_of(o.begin(), o.end(), [](const Order &x) { return ceremonial(x.text); }))
        try {
            request.retryReason = "ceremonial_independent_action";
            o = ask(" The previous answer was a symbolic gesture. Write instead what this actor "
                    "does about something at stake for it this month.");
        } catch (const ValidationError &) {
            answers = firstAnswers;
        }
    return {o, "Local model", answers};
}
std::string LocalProvider::chronicle(const Json::Value &input, const std::string &actor, InferenceRequest *provided) {
    InferenceRequest local;
    auto &request = provided ? *provided : local;
    ValidateAnswer validation{request};
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
        0, 0.3, "chronicle", actor, request);
    if (!j.isObject() || j.getMemberNames() != std::vector<std::string>{"summary"} ||
        !j["summary"].isString() || j["summary"].asString().empty() ||
        j["summary"].asString().size() > 2400)
        throw ValidationError("Invalid chronicle");
    return j["summary"].asString();
}
std::string LocalProvider::narrate(const std::vector<ResolvedEvent> &e) { return narrate(e, {}); }
std::string LocalProvider::narrate(const std::vector<ResolvedEvent> &e, const CountryContext &c, InferenceRequest *provided) {
    InferenceRequest local;
    auto &request = provided ? *provided : local;
    ValidateAnswer validation{request};
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
        0, 0.7, "narrate", c.actor, request);
    if (!j.isObject() || j.getMemberNames() != std::vector<std::string>{"text"} ||
        !j["text"].isString() || j["text"].asString().size() > 2000 || j["text"].asString().empty())
        throw ValidationError("Invalid narration");
    return j["text"].asString();
}
WorldDelta LocalProvider::adjudicate(const WorldState &w, const Json::Value &input,
                                     const Adjacency &adjacency, const Json::Value &responses, InferenceRequest *provided) {
    InferenceRequest local;
    auto &request = provided ? *provided : local;
    ValidateAnswer validation{request};
    std::vector<std::pair<std::string, std::string>> actions;
    for (auto &i : input["intentions"])
        actions.emplace_back(i["actor"].asString(), i["action"].asString());
    auto schema = operationSchema(w, input["facts"], actions, adjacency, request.experiment == "schema");
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
    narrowIntentions(w, schema, input["intentions"], request.experiment == "offers");
    if (request.experiment == "operations") {
        // An empty array is the 4B's default even after describing a treaty offer; an explicit
        // "none" makes it choose. The entry is removed before compilation.
        schema["$defs"]["op_none"] = parse(R"({"type":"object","additionalProperties":false,"required":["action"],"properties":{"action":{"type":"string","enum":["none"]}}})");
        for (auto &item : schema["properties"]["events"]["prefixItems"]) {
            auto &operations = item["properties"]["operations"];
            if (!operations["items"].isMember("anyOf"))
                continue;
            Json::Value none;
            none["$ref"] = "#/$defs/op_none";
            operations["items"]["anyOf"].append(none);
            operations["minItems"] = 1;
        }
    }
    static const std::string head =
        "You are the game master of a fictional monthly strategy sandbox. Adjudicate this month's "
        "actions (intentions) against the facts. Context version 2 shares event, entity, "
        "conflict and agreement records in facts.shared dictionaries keyed by stable IDs; "
        "actor lists reference those records and retain actor-specific relevance. "
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
        "preliminary talks) are valid events with no operations; describe their plausible effect. "
        "A concrete recorded treaty offer requires add_commitment or amend_commitment. "
        "Discussion alone is valid negotiation but creates no offer or agreement. Never describe "
        "an offer as signed or in force before the supplied revision has every required signature. "
        "Ground every outcome in each nation's status: population, GDP (real currency), stability, "
        "wars and treaties. A small or poor economy cannot fund what a large one can; unstable or "
        "war-weary nations face unrest, desertion and failed reforms; a nation fighting a "
        "high-intensity war has little to spare. Elections, opposition parties and legislatures "
        "exist only where a nation's government has them. ";
    // Event text version 2 replaces the sentence rather than adding one: the prompt is crowdable.
    static const std::string outcomes =
        "outcome_kind names what the outcome itself is when it is not an operation: a "
        "reform, an investment, a negotiation (preliminary talks, diplomacy) or a protest; none only "
        "if nothing happened. Operations derive consequences in C++. "
        "effects: economic and stability consequences of the event, only for nations it clearly "
        "affects; never invent numbers, the game computes them and corrects wars, seizures and "
        "treaties. Omit effects unless something changes; a world_event must list at least one. ";
    static const std::string consequences =
        "outcome_kind names what the outcome itself is when it is not an operation: a "
        "reform, an investment, a negotiation (preliminary talks, diplomacy), a protest, security "
        "(deployments, policing, martial law) or policy (laws, decrees, appointments, statements). "
        "Operations derive consequences in C++. "
        "outcome_effects (effects in world_events): economic and stability consequences of the "
        "event, only for nations it clearly affects; never invent numbers, the game computes them "
        "and corrects wars, seizures and treaties. Every outcome has a consequence: rate it from "
        "the facts and history, a minor act a minor effect. ";
    static const std::string tail =
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
        "permitted, recording agenda, relations and an optional pending demand; demand_kind is "
        "political (talks only), regional_project (funded project_benefit work there) or "
        "independence. They cannot "
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
            consequential(text) || (!input["facts"]["actors"][actor]["offensive"]["regions"].empty() &&
                !(request.experiment == "reasoning" && clearlyDomestic(w, actor, text)));
    unsigned budget = weighed ? std::clamp<unsigned>(256 + 128 * weighed, 384, 768) : 0;
    const auto instruction =
        head + (w.engine.get("event_text_version", 0).asInt() >= 2 ? consequences : outcomes) + tail +
        (w.engine.get("project_version", 0) == 1 ?
        " Domestic projects: set project=null except for funded reform/investment. For a new project "
        "supply action=start, id=empty string, a short objective, target=your actor ID or a region you own and control, "
        "and sector: administration (governance, taxation, justice) or public_services (education, health, welfare) "
        "for reforms (stability), infrastructure/production for investments (output). These are the only simulated benefits; do not claim unsupported "
        "objectives achieved. To continue or cancel use action=continue/cancel and the exact project id and unchanged terms in facts (sector=its benefit). "
        "Same target/category/benefit continues existing work regardless of title. Completed work "
        "cannot restart. A domestic project needs no build operation or event_site; project is its authoritative record. Cancellation retains paid work without refund; explicit continuation resumes it. Funding and paid completion are determined by C++; descriptions cannot "
        "complete projects. Repeated acts may only maintain a stance: outcome_effects may be empty "
        "or none/none; consequences are bounded and diminish with repetition. " : "");
    const auto grounding = request.experiment == "grounding" ?
        " Intent grounding examples: an explicit offer of a treaty -> add_commitment with its "
        "parties, kind, duration and terms, still awaiting exact signatures. Merely discussing or "
        "mentioning a treaty -> no proposal. Military access offer -> add_commitment with grant_access "
        "terms, not open_conflict. Fund education reform -> project={action:start,id:empty,objective:education,"
        "target:acting actor ID,sector:public_services}; operations may be empty. Continuing work -> "
        "the existing project's exact id and terms. If infeasible, record the feasible partial outcome "
        "or explain the obstacle; never invent consent or successful completion. " : "";
    const auto explicitOps = request.experiment == "operations" ?
        " operations lists at least one entry: each concrete offer, construction, movement or "
        "seizure is its operation; use {action:none} only when nothing is recorded. " : "";
    const auto prompt = instruction + grounding + explicitOps + (request.experiment == "compact" ?
        " For mechanical operations keep description to a short action label; do not repeat treaty "
        "parties, duration, terms or results already encoded by operations. Keep authored names and "
        "interpretive outcomes. The compiler describes recorded proposals and their signatures." : "");
    Json::Value response;
    try {
        response = completion(input, schema, prompt, budget, 0.6, "adjudicate", "", request);
    } catch (const ValidationError &) {
        if (!budget || *request.cancelled || std::chrono::steady_clock::now() >= request.deadline)
            throw;
        request.retryReason = "reasoning_answer_invalid";
        // A reasoning run can exhaust its budget before answering; retry once without thinking.
        response = completion(input, schema, prompt, 0, 0.6, "adjudicate", "", request);
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
        if (event["operations"].isArray()) {
            Json::Value kept(Json::arrayValue);
            for (const auto &o : event["operations"])
                if (o["action"] != "none")
                    kept.append(o);
            event["operations"] = kept;
            // The offers experiment names treaty terms "clauses" so they generate before the kind.
            for (auto &o : event["operations"])
                if (o.isObject() && o.isMember("clauses")) {
                    if (o.isMember("terms"))
                        throw ValidationError("Treaty carries both clauses and terms");
                    o["terms"] = o["clauses"];
                    o.removeMember("clauses");
                }
        }
        for (auto k : {"initiative_type", "regional_actor"}) {
            event.removeMember(k);
            if (intent.isMember(k))
                event[k] = intent[k];
        }
        // Event text version 2 names the rating outcome_effects so the model writes it after its
        // operations; the compiler reads effects.
        if (event.isMember("outcome_effects")) {
            event["effects"] = event["outcome_effects"];
            event.removeMember("outcome_effects");
        }
    }
    // The history the model was shown this month: anything it re-reports as a new background
    // development is the past, not a development.
    std::set<std::string> priorEvents;
    for (const auto &id : input["facts"]["shared"]["events"].getMemberNames()) priorEvents.insert(id);
    for (const auto &a : input["facts"]["actors"].getMemberNames())
        for (const auto &past : input["facts"]["actors"][a]["related_history"])
            if (past.isObject() && past["id"].isString())
                priorEvents.insert(past["id"].asString());
    const auto compiling = std::chrono::steady_clock::now();
    try {
        auto delta = compileOperations(w, response, adjacency, responses, priorEvents);
        request.audit[request.audit.size() - 1]["compilation_ms"] = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - compiling).count();
        return delta;
    } catch (...) {
        request.audit[request.audit.size() - 1]["compilation_ms"] = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - compiling).count();
        throw;
    }
}
} // namespace lh
