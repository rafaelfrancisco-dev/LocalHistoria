#include "localhistoria/history.h"
#include "localhistoria/politics.h"
#include "localhistoria/store.h"
#include "localhistoria/world.h"
#include <iostream>
using namespace lh;
void check(bool b, const char *s) {
    if (!b)
        throw std::runtime_error(s);
}
template <class F> void rejects(F f) {
    bool rejected = false;
    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected, "Expected rejection");
}
Json::Value event(const std::string &actor, Json::Value ops, const std::string &region = "") {
    Json::Value e;
    e["actor"] = actor;
    e["blocked_by"] = "none";
    e["description"] = "Political development";
    e["causes"] = Json::arrayValue;
    e["operations"] = ops;
    if (!region.empty()) {
        e["regional_actor"] = region;
        e["initiative_type"] = "independent_regional";
    } else
        e["initiative_type"] = "independent_national";
    Json::Value r;
    r["events"].append(e);
    return r;
}
int main() {
    try {
        WorldState w;
        w.player = "PT";
        w.campaignId = "politics";
        for (auto a : {"PT", "ES", "FR", "IT", "DE", "NO"}) {
            w.actors[a]["name"] = a;
            w.actors[a]["color"] = "#123456";
            w.actors[a]["static_flag"] = a;
        }
        w.territories = parse(
            R"({"r":{"name":"Local Region","kind":"adm1","owner":"PT","controller":"PT","initial_affiliation":"PT"},"s":{"name":"Elsewhere","kind":"adm1","owner":"ES","controller":"ES"},"bad":{"kind":"residual","controller":"FR"},"unknown":{"controller":"missing"}})");
        auto slots = scheduleInitiatives(w, {"ES"});
        check(slots == scheduleInitiatives(w, {"ES"}) && slots.size() == 3,
              "Deterministic three slots");
        check(slots[0]["actor"] != "PT" && slots[0]["actor"] != slots[1]["actor"] &&
                  slots[0]["actor"] != "ES" && slots[1]["actor"] != "ES",
              "Distinct nonplayer, avoid reactive");
        check(slots[2]["region"] == "r" || slots[2]["region"] == "s",
              "Eligible regional selection");
        for (unsigned i = 0; i < 2; ++i)
            w.activity[slots[i]["actor"].asString()] = 1;
        w.turn = 1;
        auto next = scheduleInitiatives(w);
        for (unsigned i = 0; i < 2; ++i)
            check(next[i]["actor"] != slots[0]["actor"] && next[i]["actor"] != slots[1]["actor"],
                  "Two month cooldown preference");
        auto one = w;
        one.actors = Json::objectValue;
        one.actors["PT"] = w.actors["PT"];
        check(scheduleInitiatives(one).size() == 1, "Small pool bounded");
        one.territories = Json::objectValue;
        check(scheduleInitiatives(one).empty(), "Empty pool");
        int domestic = 0;
        for (int n = 0; n < 200; ++n) {
            w.campaignId = std::to_string(n);
            if (scheduleInitiatives(w)[2]["actor"] == "PT")
                ++domestic;
        }
        check(domestic > 60 && domestic < 140, "Country balanced regional sampling");
        for (int n = 0; n < 100; ++n)
            w.territories["extra" + std::to_string(n)] = w.territories["s"];
        int domesticLarge = 0;
        for (int n = 0; n < 200; ++n) {
            w.campaignId = std::to_string(n);
            if (scheduleInitiatives(w)[2]["actor"] == "PT")
                ++domesticLarge;
        }
        check(domestic == domesticLarge, "Region count cannot bias authority selection");
        w.territories.removeMember("unknown");
        w.territories["bad"]["owner"] = "FR";
        auto regional = event(
            "PT",
            parse(
                R"([{"action":"regional_initiative","region":"r","agenda":"Improve civic representation","relations":"strained","demand":"Negotiate autonomy"}])"),
            "r");
        Resolution base{w, {}};
        auto delta = compileOperations(w, regional, {});
        auto r = applyDelta(base, delta, {});
        check(r.state.regionalPolitics["r"]["demands"][0]["status"] == "pending" &&
                  r.state.activity["r"] == w.turn,
              "Regional pending demand and attribution");
        check(!r.state.actors.isMember("r"), "Regions are not sovereign actors");
        auto illegal = event(
            "PT",
            parse(
                R"([{"action":"create_actor","actor_id":"free","name":"Free","color":"#123456"},{"action":"transfer_territory","region":"r","to":"free"}])"),
            "r");
        auto blocked = compileOperations(w, illegal, {});
        check(blocked.events[0]["blocked_by"] == "infeasible", "Region cannot secede by decree");
        for (
            auto op :
            {R"([{"action":"build","kind":"army","actor":"PT","name":"Army","region":"r"}])",
             R"([{"action":"add_commitment","actors":["PT","ES"],"text":"Treaty"}])",
             R"([{"action":"change_identity","actor":"PT","name":"New Republic","change_type":"rename","reason":"Demand"}])"})
            check(compileOperations(w, event("PT", parse(op), "r"), {}).events[0]["blocked_by"] ==
                      "infeasible",
                  "Unauthorized regional national operation");
        auto changed = r.state;
        changed.territories["r"]["controller"] = "ES";
        changed.turn++;
        auto response = parse(
            R"([{"action":"respond_regional","region":"r","demand_id":"placeholder","response":"accepted"}])");
        response[0]["demand_id"] = r.state.regionalPolitics["r"]["demands"][0]["id"];
        check(compileOperations(changed, event("PT", response), {}).events[0]["blocked_by"] ==
                  "infeasible",
              "Former authority cannot respond");
        auto accepted =
            applyDelta({changed, {}}, compileOperations(changed, event("ES", response), {}), {});
        check(accepted.state.regionalPolitics["r"]["demands"][0]["status"] == "accepted" &&
                  accepted.state.regionalPolitics["r"]["agenda"] ==
                      r.state.regionalPolitics["r"]["agenda"],
              "Control change preserves agenda and current authority responds");
        auto design = parse(
            R"({"layout":"horizontal","palette":["#112233","#445566","#778899"],"emblem":"circle","explanation":"Civic unity"})");
        auto f = makeFlag(design, "seed", "event");
        check(f["svg"] == renderFlag(design) && f == makeFlag(design, "seed", "event"),
              "Deterministic renderer");
        auto distinct = makeFlag(design, "seed", "event2", f["svg"].asString());
        check(distinct["svg"] != f["svg"], "Replacement differs");
        for (auto key : {"layout", "emblem"}) {
            auto bad = design;
            bad[key] = "<script>alert(1)</script>";
            rejects([&] { renderFlag(bad); });
            check(makeFlag(bad, "seed", "event")["svg"].asString().find("script") ==
                      std::string::npos,
                  "Safe fallback");
        }
        auto bad = design;
        bad["palette"][0] = "url(https://evil)";
        rejects([&] { renderFlag(bad); });
        bad = design;
        bad["svg"] = "<svg/>";
        rejects([&] { renderFlag(bad); });
        w.commitments["treaty"] =
            parse(R"({"actors":["PT","ES"],"status":"open","text":"Cooperation"})");
        auto rename = parse(
            R"([{"action":"change_identity","actor":"PT","name":"Atlantic Republic","change_type":"rename","reason":"Adjudicated constitutional settlement"}])");
        rename[0]["flag_design"] = design;
        auto identityDelta = compileOperations(w, event("PT", rename), {});
        auto renamed = applyDelta({w, {}}, identityDelta, {});
        check(renamed.state.actors.isMember("PT") &&
                  renamed.state.actors["PT"]["name"] == "Atlantic Republic" &&
                  renamed.state.commitments == w.commitments &&
                  renamed.state.territories == w.territories,
              "Stable identity links");
        check(renamed.state.actors["PT"]["identity_revisions"][0]["name"] == "PT" &&
                  renamed.state.actors["PT"]["flag_revision"].asUInt() == 1,
              "Historical static flag retained");
        auto refounded = renamed.state;
        refounded.turn++;
        rename[0]["change_type"] = "refounding";
        auto refounding =
            applyDelta({refounded, {}}, compileOperations(refounded, event("PT", rename), {}), {});
        check(refounding.state.actors["PT"]["flag_revision"].asUInt() == 2 &&
                  refounding.state.actors["PT"]["identity_revisions"][1]["svg"] !=
                      refounding.state.actors["PT"]["identity_revisions"][2]["svg"],
              "Refounding distinct flag");
        auto quiet = applyDelta(
            {refounded, {}},
            compileOperations(refounded, event("PT", Json::Value(Json::arrayValue)), {}), {});
        check(quiet.state.actors == refounded.actors, "Ordinary events retain flags");
        auto correction = rename;
        correction[0]["change_type"] = "correction";
        correction[0]["name"] = "Atlantic republic";
        auto corrected = applyDelta({refounded, {}},
                                    compileOperations(refounded, event("PT", correction), {}), {});
        check(corrected.state.actors["PT"]["flag_revision"].asUInt() == 1,
              "Spelling correction retains flag");
        auto creation = event(
            "PT",
            parse(
                R"([{"action":"create_actor","actor_id":"new","name":"New State","color":"#123456"},{"action":"transfer_territory","region":"r","to":"new"}])"));
        auto created = applyDelta({w, {}}, compileOperations(w, creation, {}), {});
        check(created.state.actors["new-state"]["flag_revision"].asUInt() == 0 &&
                  created.state.territories["r"]["owner"] == "new-state",
              "New state fallback flag and land");
        auto legacy = encode(w);
        legacy.removeMember("activity");
        legacy.removeMember("regional_politics");
        check(decode(legacy).regionalPolitics.empty(), "Legacy decoding");
        check(encode(decode(encode(refounding.state))) == encode(refounding.state),
              "Reload identity revisions");
        auto tampered = identityDelta;
        for (auto &c : tampered.changes)
            if (c["kind"] == "actor")
                c["after"]["identity_revisions"][1]["svg"] = "<script/>";
        rejects([&] { applyDelta({w, {}}, tampered, {}); });
        Store store(":memory:");
        w.turn = 0;
        store.create(w, false);
        auto job = store.submit(0, "identity-rollback", "{}");
        auto resolved = resolve(w);
        auto dd = compileOperations(resolved.state, event("PT", rename), {});
        auto final = applyDelta(resolved, dd, {});
        Json::Value refs;
        refs["delta"] = encode(dd);
        rejects([&] { store.commit(job["id"].asString(), final, {}, refs, true); });
        check(store.campaign() == encode(w), "Atomic identity rollback");
        store.commit(job["id"].asString(), final, {}, refs);
        check(store.campaign() == encode(final.state), "Atomic identity persistence");
        auto ambiguous = fetchRegionalHistory("Local Region", "PT", [](auto, auto, auto, double) {
            return parse(R"({"results":{"bindings":[{},{}]}})");
        });
        check(!ambiguous["available"].asBool(), "Ambiguous reference unavailable");
        auto outage =
            fetchRegionalHistory("Local Region", "PT", [](auto, auto, auto, double) -> Json::Value {
                throw std::runtime_error("offline");
            });
        check(!outage["available"].asBool(), "Reference outage tolerated");
        auto reference =
            fetchRegionalHistory("Local Region", "PT", [](auto host, auto path, auto, double) {
                if (host == "https://query.wikidata.org")
                    return parse(
                        R"({"results":{"bindings":[{"item":{"value":"http://www.wikidata.org/entity/Q123"}}]}})");
                if (host == "https://www.wikidata.org")
                    return parse(
                        R"({"entities":{"Q123":{"sitelinks":{"enwiki":{"title":"Local Region"}}}}})");
                return parse(
                    R"({"latest":{"id":42},"html":"<p>A verified regional reference.</p>"})");
            });
        check(reference["available"].asBool() && reference["regional_wikidata"] == "Q123" &&
                  reference["revision"] == 42,
              "Verified regional reference cached provenance");
        std::cout << "Independent scheduler, regional authority/history, identities, safe flags, "
                     "rollback, reload and reference mocks passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
