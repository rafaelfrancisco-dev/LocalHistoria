#include "world/compiler/operation_compiler.h"
#include <algorithm>
namespace lh::detail {
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
    event["text"] = s.text.substr(0, 2000);
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
    if (happened && (!realism() || !failed || s.e.get("independent_outcome", "none") != "none"))
        settleEffects(s, event);
    if (realism() && failed) {
        // get(): naming a missing member would insert a null "effects" the delta rejects.
        if (!event.get("effects", Json::arrayValue).empty() || event.isMember("effort")) {
            event.removeMember("failed");
            event["partial"] = true;
            event["text"] = "The independent " + s.e["independent_outcome"].asString() +
                            " proceeded; the proposed operations could not be carried out.";
        } else
            event["text"] = "The proposed action could not be carried out.";
    } else if (realism() && happened && !s.dropped.empty())
        event["text"] = "Only part of the proposed action was carried out; see accepted changes.";
    if (realism() && happened && !failed && s.successfulOps.empty() &&
        s.e.get("independent_outcome", "none") == "none") {
        event["text"] = "No validated change resulted from the proposed action.";
        event["major"] = false;
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
    auto independent = s.e.get("independent_outcome", "none").asString();
    bool supportedIndependent =
        s.regional.empty() && (independent == "reform" || independent == "investment" ||
                               independent == "negotiation" || independent == "protest");
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
                if (independent == "negotiation")
                    economy = stability = "none";
                if (independent == "protest") {
                    economy = "none";
                    stability = "decline";
                }
                // A reform or investment is an effort the month step prices and pays for; its
                // benefit arrives once, on completion. Starting one may lift confidence for a
                // month; asking again continues the effort already under way and earns nothing.
                if ((independent == "reform" || independent == "investment") &&
                    (rank(economy) > 2 || rank(stability) > 2)) {
                    auto key = a + ":" + independent;
                    bool open =
                        std::as_const(w.actors)[a]["status"]["efforts"].isMember(independent) ||
                        efforts.contains(key);
                    if (rank(economy) > 2)
                        economy = "none";
                    if (open && rank(stability) > 2)
                        stability = "none";
                    if (!open) {
                        efforts.insert(key);
                        event["effort"] = independent;
                    }
                    if (economy == "none" && stability == "none")
                        continue;
                }
            }
            effects.push_back({a, economy, stability, false});
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
    event["effects"] = Json::arrayValue;
    for (auto &f : effects) {
        Json::Value x;
        x["actor"] = f.actor;
        x["economy"] = f.economy;
        x["stability"] = f.stability;
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
