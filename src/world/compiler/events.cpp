#include "world/compiler/operation_compiler.h"
#include <algorithm>
#include <cctype>
#include <regex>
namespace lh::detail {
namespace {
// Whether a description narrates an agreement: only used to add the record's verdict to it.
bool claimsAgreement(const std::string &text) {
    static const std::regex words(
        R"(\b(sign|signs|signed|signing|ratif\w*|treaty|treaties|pact|pacts|accord|accords|agreement|agreements|alliance)\b)",
        std::regex::icase);
    return std::regex_search(text, words);
}
// A description the 160-character cap cut mid-sentence ends in an ellipsis.
std::string closed(std::string text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.pop_back();
    if (!text.empty() && !text.ends_with("…") &&
        std::string(".!?\"')").find(text.back()) == std::string::npos)
        text += "…";
    return text;
}
std::string withVerdict(const std::string &text, const std::string &verdict) {
    return closed(text) + " " + verdict;
}
} // namespace
// A later event about an incident, or one caused by it, keeps its site on the map for another few
// months.
void OperationCompiler::renewEventSites(EventScope &s) {
    std::set<std::string> causes;
    for (auto &c : s.e["causes"])
        if (c.isString())
            causes.insert(c.asString());
    for (auto &eid : w.entities.getMemberNames()) {
        // Sites created this month are not in w yet and need no renewal; kind never changes, so
        // check it before copying.
        if (w.entities[eid].get("kind", "") != "event_site")
            continue;
        auto site = pending.current("entity", w.entities, eid);
        if (site["kind"] != "event_site" || site["status"] != "active" ||
            (!s.entities.contains(eid) && !causes.contains(site["origin_event"].asString())))
            continue;
        pending.touch("entity", eid, w.entities[eid], s.id,
                      "Renewed by a related event")["expires_turn"] = w.turn + eventMapMonths;
        s.entities.insert(eid);
    }
}
// Even narrative-only and failed attempts leave a persistent regional history.
void OperationCompiler::finishRegionalHistory(EventScope &s) {
    s.regions.insert(s.regional);
    auto p = pending.current("regional", w.regionalPolitics, s.regional);
    if (p.isNull())
        p = regionalState(w, s.regional);
    if (std::find(p["supporting_events"].begin(), p["supporting_events"].end(),
                  Json::Value(s.id)) == p["supporting_events"].end())
        p["supporting_events"].append(s.id);
    if (p["supporting_events"].size() > 12)
        p["supporting_events"].removeIndex(0, nullptr);
    pending.touch("regional", s.regional, w.regionalPolitics[s.regional], s.id,
                  "Regional political history") = p;
}
void OperationCompiler::buildEvent(EventScope &s) {
    auto &e = s.e;
    bool happened = s.blocked == "none";
    if (!s.breaches.empty() && happened)
        s.text += " This breached the commitment: " + w.commitments[s.breaches]["text"].asString();
    Json::Value event;
    event["id"] = s.id;
    event["actor"] = s.ev;
    if (!happened)
        event["blocked_by"] = s.blocked;
    if (!s.breaches.empty() && happened)
        event["breaches"] = s.breaches;
    event["text"] = consequential() ? closed(s.text.substr(0, 2000)) : s.text.substr(0, 2000);
    if (!s.dropped.empty())
        event["dropped_operations"] = s.dropped;
    // What players and later prompts are told about dropped operations: a flag, never the reasons.
    bool failed = happened && !s.seenOps.empty() && s.failedOps == int(s.seenOps.size());
    if (failed)
        event["failed"] = true;
    else if (happened && !s.dropped.empty())
        event["partial"] = true;
    event["background"] = e["background"];
    event["causes"] = e["causes"];
    for (auto [key, values] :
         std::initializer_list<std::pair<const char *, std::set<std::string> *>>{
             {"actors", &s.actors},
             {"regions", &s.regions},
             {"entities", &s.entities},
             {"conflicts", &s.conflicts},
             {"commitments", &s.commitments}}) {
        event[key] = Json::arrayValue;
        for (auto &v : *values)
            event[key].append(v);
    }
    for (auto k : {"initiative_type", "regional_actor"})
        if (e.isMember(k))
            event[k] = e[k];
    // Only something that happened has consequences.
    if (happened && (!realism() || !failed || s.e.get("outcome_kind", "none") != "none"))
        settleEffects(s, event);
    if (realism() && failed) {
        // get(): naming a missing member would insert a null "effects" the delta rejects.
        if (!event.get("effects", Json::arrayValue).empty() || event.isMember("effort")) {
            event.removeMember("failed");
            event["partial"] = true;
            event["text"] = "The independent " + s.e["outcome_kind"].asString() +
                            " proceeded; the proposed operations could not be carried out.";
        } else
            event["text"] = "The proposed action could not be carried out.";
    } else if (realism() && happened && !s.dropped.empty())
        event["text"] = "Only part of the proposed action was carried out; see accepted changes.";
    // Before version 2 an outcome the model called "none" lost its description; now it keeps it and
    // settleEffects gives it a bounded consequence.
    if (realism() && !consequential() && happened && !failed && s.successfulOps.empty() &&
        s.e.get("outcome_kind", "none") == "none" && !s.e.get("background", false).asBool())
        event["text"] = "No validated change resulted from the proposed action.";
    if (w.engine.get("event_text_version", 0).asInt() >= 1 && happened && !failed) {
        std::string factual;
        for (const auto &change : pending) {
            if (change.event != s.id || change.before == change.after || change.kind != "proposal") continue;
            const auto &p = change.after;
            const auto revision = p.get("revision", 1).asInt();
            const auto label = p["name"].asString() + " (proposal " + change.id + ", revision " + std::to_string(revision) + ")";
            if (!factual.empty()) factual += " ";
            if (change.before.isNull())
                factual += "Recorded " + label + (p["amends"].isString() ? " to amend agreement " + p["amends"].asString() : "") + "; awaiting required signatures.";
            else {
                for (const auto &party : p["answers"].getMemberNames())
                    if (p["answers"][party] != change.before["answers"][party])
                        factual += party + (p["answers"][party]["accept"] == true ? " signed " : " rejected ") + label + ". ";
                if (p["status"] == "accepted")
                    factual += "All required parties signed " + label + "; agreement " + p.get("amends", change.id).asString() + " took effect.";
                else factual += label + " is " + p["status"].asString() + ".";
            }
        }
        if (!factual.empty()) event["text"] = factual.substr(0, 2000);
        else if (s.successfulOps.empty() &&
                 (s.e["outcome_kind"] == "negotiation" ||
                  (consequential() && claimsAgreement(event["text"].asString()))))
            // Talks keep what the model said happened, with the record's verdict after it. So does
            // any outcome describing a signing without an operation that recorded one: a small model
            // labels a pact it never wrote a "policy" and narrates it as signed.
            event["text"] = consequential()
                                ? withVerdict(event["text"].asString(), "No agreement was recorded.")
                                : "Preliminary diplomatic talks took place; no treaty offer or "
                                  "agreement was recorded.";
    }
    event["identity_changes"] = Json::arrayValue;
    for (const auto &c : pending)
        if (c.kind == "actor" && c.event == s.id && c.after["flag_revision"].isUInt() &&
            c.after["flag_revision"] != c.before["flag_revision"]) {
            Json::Value link;
            link["actor"] = c.id;
            link["revision"] = c.after["flag_revision"];
            event["identity_changes"].append(link);
        }
    event["major"] = happened && (s.structural || event.isMember("breaches"));
    // A severe or strong effect declares a headline; the model no longer labels significance
    // itself.
    bool strong = false;
    for (auto &x : event.get("effects", Json::arrayValue))
        for (auto k : {"economy", "stability"})
            strong = strong || x[k] == "severe_decline" || x[k] == "strong_growth";
    // A programme the model rated strong is still news when it starts, though its benefit waits
    // for completion.
    if (event.isMember("effort"))
        for (const auto &x : s.e["effects"])
            if (x.isObject() && actorRef(x["actor"].asString()) == s.ev)
                strong =
                    strong || x["economy"] == "strong_growth" || x["stability"] == "strong_growth";
    if (happened && !event["major"].asBool() && strong)
        declared.push_back(events.size());
    events.append(event);
}
// Malformed or unknown effects are dropped, not fatal. The small model rewards aggression and
// punishes victims at random; the month's operations settle the sign.
void OperationCompiler::settleEffects(const EventScope &s, Json::Value &event) const {
    struct Effect {
        std::string actor, economy, stability;
        bool forced;
    };
    std::vector<Effect> effects;
    auto find = [&](const std::string &a) {
        return std::find_if(effects.begin(), effects.end(),
                            [&](const Effect &f) { return f.actor == a; });
    };
    // A regional initiative that filed no demand and left relations as they were changed nothing;
    // the model still likes to attach a national economic decline to it. A town hall about heritage
    // does not move a country's economy.
    bool inert = !s.regional.empty();
    if (inert)
        for (const auto &o : (realism() ? s.successfulOps : s.seenOps)) {
            if (!o["demand"].asString().empty() ||
                (o["relations"].isString() &&
                 o["relations"] != regionalState(w, s.regional)["relations"]))
                inert = false;
        }
    auto independent = s.e.get("outcome_kind", "none").asString();
    bool background = s.e.get("background", false).asBool();
    // Version 2 has no "none": an unlabelled outcome from an unconstrained provider is a policy.
    if (consequential() && !background && independent != "reform" &&
        independent != "investment" && independent != "negotiation" && independent != "protest" &&
        independent != "security")
        independent = "policy";
    // A consequence without a map change is felt, not decisive.
    auto mild = [](const std::string &x) { return stronger(weaker(x, "growth"), "decline"); };
    auto effortOpen = [&](const std::string &a) {
        return std::as_const(w.actors)[a]["status"]["efforts"].isMember(independent) ||
               efforts.contains(a + ":" + independent);
    };
    auto startEffort = [&](const std::string &a) {
        efforts.insert(a + ":" + independent);
        event["effort"] = independent;
    };
    // A world event is an unplanned development whose consequence is the point of it, bounded like
    // any other effect.
    // Security, policy and talks under version 2 are the act itself: when every operation it
    // proposed failed, its consequence is the failure, not an independent result.
    const bool failedAll = !s.seenOps.empty() && s.failedOps == int(s.seenOps.size());
    bool supportedIndependent =
        s.regional.empty() &&
        (independent == "reform" || independent == "investment" || independent == "protest" ||
         background || (independent == "negotiation" && !(consequential() && failedAll)) ||
         (consequential() && !failedAll && (independent == "security" || independent == "policy")));
    if (!inert && (!realism() || supportedIndependent) && s.e["effects"].isArray())
        for (auto &x : s.e["effects"]) {
            if (effects.size() >= 3 || !x.isObject() || !x["actor"].isString() ||
                !x["economy"].isString() || !x["stability"].isString())
                continue;
            auto a = actorRef(x["actor"].asString());
            auto economy = x["economy"].asString(), stability = x["stability"].asString();
            if (!known(a) || !validEffect(economy) || !validEffect(stability) ||
                (economy == "none" && stability == "none") || find(a) != effects.end())
                continue;
            if (realism()) {
                // An independently represented domestic or diplomatic result only affects its
                // author; operations supply their own deterministic consequences below.
                if (a != s.ev)
                    continue;
                // Talks read at home as progress or weakness; their economic worth waits for a
                // treaty.
                if (independent == "negotiation") {
                    economy = "none";
                    stability = consequential() ? mild(stability) : "none";
                }
                // A law, decree or deployment can cost an economy but not grow it within a month:
                // growth is what a reform or investment pays for as an effort. Labelled a policy,
                // a reform would otherwise earn its benefit at once and for nothing.
                if (independent == "security" || independent == "policy") {
                    economy = weaker(mild(economy), "none");
                    stability = mild(stability);
                }
                if (independent == "protest") {
                    economy = "none";
                    stability = "decline";
                }
                // A reform or investment is an effort the month step prices and pays for; its
                // benefit arrives once, on completion. Starting one may lift confidence for a
                // month; asking again continues the effort already under way and earns nothing.
                if ((independent == "reform" || independent == "investment") &&
                    s.successfulOps.empty() && (rank(economy) > 2 || rank(stability) > 2)) {
                    bool open = effortOpen(a);
                    if (rank(economy) > 2)
                        economy = "none";
                    if (open && rank(stability) > 2)
                        stability = "none";
                    if (!open)
                        startEffort(a);
                    if (economy == "none" && stability == "none")
                        continue;
                }
            }
            effects.push_back({a, economy, stability, false});
        }
    // A reform or investment the model named starts its effort even when it rated no effect,
    // unless it judged the attempt a setback for its author.
    if (realism() && !inert && supportedIndependent && s.successfulOps.empty() &&
        (independent == "reform" || independent == "investment") && !event.isMember("effort")) {
        auto worse = [](const Json::Value &v) {
            return validEffect(v.asString()) && rank(v.asString()) < 2;
        };
        bool setback = false;
        for (const auto &x : s.e.get("effects", Json::Value(Json::arrayValue)))
            setback = setback || (x.isObject() && actorRef(x["actor"].asString()) == s.ev &&
                                  (worse(x["economy"]) || worse(x["stability"])));
        if (!setback && !effortOpen(s.ev))
            startEffort(s.ev);
    }
    // Every outcome does something. When the model rated nothing usable for an act without
    // operations, its author feels the usual small consequence of that kind of act. A failed
    // attempt is not given one: its consequence is the failure.
    if (consequential() && !inert && supportedIndependent && !background && s.seenOps.empty() &&
        find(s.ev) == effects.end() && !event.isMember("effort")) {
        static const std::map<std::string, std::pair<const char *, const char *>> defaults{
            {"security", {"decline", "growth"}},
            {"policy", {"none", "growth"}},
            {"negotiation", {"none", "growth"}},
            {"protest", {"none", "decline"}}};
        if (auto d = defaults.find(independent); d != defaults.end()) {
            effects.push_back({s.ev, d->second.first, d->second.second, false});
            event["effect_source"] = "default";
        }
    }
    auto entry = [&](const std::string &a) -> Effect & {
        auto f = find(a);
        if (f == effects.end()) {
            effects.push_back({a, "none", "none", true});
            return effects.back();
        }
        f->forced = true;
        return *f;
    };
    for (auto &p : s.warParties) {
        auto &f = entry(p);
        f.economy = weaker(f.economy, "decline");
        if (p != s.ev)
            f.stability = weaker(f.stability, "decline");
    }
    // Under the realism rules trade is an ongoing relationship the month step values while it
    // lasts, not a reward for signing or a penalty for ending.
    for (auto &p : s.tradeParties) {
        if (realism())
            break;
        auto &f = entry(p);
        f.economy = stronger(f.economy, "growth");
    }
    for (auto &p : s.seizedOwners) {
        auto &f = entry(p);
        f.stability = weaker(f.stability, "decline");
    }
    for (auto &p : s.tradeLoss) {
        if (realism())
            break;
        auto &f = entry(p);
        f.economy = weaker(f.economy, "decline");
    }
    std::erase_if(effects,
                  [](const Effect &f) { return f.economy == "none" && f.stability == "none"; });
    // Forced entries survive the cap; among the model's own, the weightiest stay.
    auto weight = [](const Effect &f) {
        return std::abs(rank(f.economy) - 2) + std::abs(rank(f.stability) - 2);
    };
    std::stable_sort(effects.begin(), effects.end(), [&](const Effect &a, const Effect &b) {
        return a.forced != b.forced ? a.forced : weight(a) > weight(b);
    });
    if (effects.size() > 3)
        effects.resize(3);
    if (effects.empty())
        return;
    // A deployment, decree or round of talks that changed nothing on the map is felt slightly:
    // its rated step is recorded at half weight, so routine acts do not add up to a crisis.
    const bool slight = consequential() && s.successfulOps.empty() &&
                        (independent == "security" || independent == "policy" ||
                         independent == "negotiation");
    auto soften = [&](const Effect &f, const std::string &v) -> std::string {
        if (!slight || f.forced)
            return v;
        return v == "decline" ? "slight_decline" : v == "growth" ? "slight_growth" : v;
    };
    event["effects"] = Json::arrayValue;
    for (auto &f : effects) {
        Json::Value x;
        x["actor"] = f.actor;
        x["economy"] = soften(f, f.economy);
        x["stability"] = soften(f, f.stability);
        event["effects"].append(x);
    }
}
// Actors first so later territory and entity changes may reference them.
WorldDelta OperationCompiler::delta() const {
    WorldDelta d;
    d.events = events;
    for (auto kind :
         {"actor", "territory", "entity", "conflict", "commitment", "regional", "proposal"})
        for (auto &c : pending)
            if (c.kind == kind && c.before != c.after) {
                Json::Value j;
                j["kind"] = c.kind;
                j["id"] = c.id;
                j["before"] = c.before;
                j["after"] = c.after;
                j["event"] = c.event;
                j["rationale"] = c.rationale;
                d.changes.append(j);
            }
    return parseDelta(encode(d));
}
} // namespace lh::detail
