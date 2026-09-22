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
            R"({"r":{"name":"Local Region","kind":"adm1","owner":"PT","controller":"PT",)"
            R"("initial_affiliation":"PT"},"s":{"name":"Elsewhere","kind":"adm1","owner":"ES",)"
            R"("controller":"ES"},"bad":{"kind":"residual","controller":"FR"},)"
            R"("unknown":{"controller":"missing"}})");
        {
            auto v = w;
            std::map<std::string, int> near{{"ES", 3}};
            auto weighted = scheduleInitiatives(v, {}, near);
            check(weighted[0]["actor"] == "ES" && weighted == scheduleInitiatives(v, {}, near),
                  "Neighbour takes the first national slot, deterministically");
            v.activity["ES"] = 0;
            check(scheduleInitiatives(v, {}, near)[0]["actor"] != "ES", "Recent neighbour rests");
            v.activity = Json::objectValue;
            v.turn = 2;
            std::map<std::string, int> known{{"ES", 3}, {"FR", 2}, {"IT", 1}, {"DE", 1}};
            auto third = scheduleInitiatives(v, {}, known);
            check(third[0]["actor"] == "ES" && third[1]["actor"] == "NO",
                  "Every third month a distant nation takes the second slot");
            v.turn = 0;
            auto world = scheduleInitiatives(v, {}, near, {"DE"});
            check(world[0]["actor"] == "ES" && world[1]["actor"] == "DE",
                  "The second slot goes to a great power");
            v.turn = 2;
            check(scheduleInitiatives(v, {}, {{"ES", 3}, {"FR", 2}, {"IT", 1}},
                                      {"DE"})[1]["actor"] == "NO",
                  "A distant nation is neither close nor a great power");
            v.turn = 0;
            auto island = v;
            island.territories["r"]["kind"] = "single";
            for (int n = 0; n < 20; ++n) {
                island.campaignId = std::to_string(n);
                check(scheduleInitiatives(island)[2]["region"] == "s",
                      "Single-territory countries have no regional slot");
            }
            std::map<std::string, int> home{{"PT", 3}};
            for (int n = 0; n < 50; ++n) {
                v.campaignId = std::to_string(n);
                check(scheduleInitiatives(v, {}, home)[2]["actor"] == "PT",
                      "Regional slot prefers the player's regions");
            }
            // A front outranks a quiet region of a closer governor, and keeps the slot through the
            // cooldown that would rotate an equally quiet region away.
            auto war = v;
            war.conflicts["c1"]["status"] = "open";
            war.conflicts["c1"]["actors"].append("ES");
            war.conflicts["c1"]["actors"].append("FR");
            war.conflicts["c1"]["affected_regions"].append("s");
            for (int n = 0; n < 50; ++n) {
                war.campaignId = std::to_string(n);
                check(scheduleInitiatives(war, {}, home)[2]["region"] == "s",
                      "A front takes the regional slot over a quiet home region");
            }
            war.activity["s"] = 0;
            check(scheduleInitiatives(war, {}, home)[2]["region"] == "s",
                  "Stake outranks the regional cooldown");
            // An unanswered demand is a lesser stake, but still beats a quiet sibling.
            auto pressed = v;
            pressed.regionalPolitics["s"]["demands"][0]["status"] = "pending";
            for (int n = 0; n < 50; ++n) {
                pressed.campaignId = std::to_string(n);
                check(scheduleInitiatives(pressed, {}, home)[2]["region"] == "s",
                      "A pending demand takes the regional slot");
            }
            // Occupied land is a front by another name.
            auto held = v;
            held.territories["s"]["controller"] = "FR";
            for (int n = 0; n < 50; ++n) {
                held.campaignId = std::to_string(n);
                check(scheduleInitiatives(held, {}, home)[2]["region"] == "s",
                      "Occupied land takes the regional slot");
            }
        }
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
        auto regional =
            event("PT",
                  parse(R"([{"action":"regional_initiative","region":"r",)"
                        R"("agenda":"Improve civic representation","relations":"strained",)"
                        R"("demand":"Negotiate autonomy"}])"),
                  "r");
        Resolution base{w, {}};
        auto delta = compileOperations(w, regional, {});
        auto r = applyDelta(base, delta, {});
        check(r.state.regionalPolitics["r"]["demands"][0]["status"] == "pending" &&
                  r.state.activity["r"] == w.turn,
              "Regional pending demand and attribution");
        check(!r.state.actors.isMember("r"), "Regions are not sovereign actors");
        check(delta.events[0]["effects"].isNull() || delta.events[0]["effects"].empty() ||
                  delta.events[0]["effects"][0]["actor"] == "PT",
              "A regional initiative that files a demand may carry effects");
        // A ceremony that files no demand and leaves relations as they were changed nothing; the
        // model still likes to attach a national economic decline to it.
        auto ceremony =
            event("PT",
                  parse(R"([{"action":"regional_initiative","region":"r",)"
                        R"("agenda":"Celebrate local heritage","relations":"neutral"}])"),
                  "r");
        ceremony["events"][0]["effects"] =
            parse(R"([{"actor":"PT","economy":"decline","stability":"none"}])");
        check(!compileOperations(w, ceremony, {}).events[0].isMember("effects"),
              "An inert regional initiative cannot move a national economy");
        auto pressing = ceremony;
        pressing["events"][0]["operations"][0]["demand"] = "Hand over the mining royalties";
        check(compileOperations(w, pressing, {}).events[0]["effects"].size() == 1,
              "A regional initiative with a demand keeps its effects");
        auto souring = ceremony;
        souring["events"][0]["operations"][0]["relations"] = "strained";
        check(compileOperations(w, souring, {}).events[0]["effects"].size() == 1,
              "A regional initiative that sours relations keeps its effects");
        auto illegal = event(
            "PT",
            parse(R"([{"action":"create_actor","actor_id":"free","name":"Free","color":"#123456"},)"
                  R"({"action":"transfer_territory","region":"r","to":"free"}])"),
            "r");
        auto blocked = compileOperations(w, illegal, {});
        check(blocked.events[0]["dropped_operations"].size() == 2 && blocked.changes.size() == 1,
              "Region cannot secede by decree");
        for (auto op :
             {R"([{"action":"build","kind":"army","actor":"PT","name":"Army","region":"r"}])",
              R"([{"action":"add_commitment","actors":["PT","ES"],"text":"Treaty"}])",
              R"([{"action":"change_identity","actor":"PT","name":"New Republic",)"
              R"("change_type":"rename","reason":"Demand"}])"})
            check(compileOperations(w, event("PT", parse(op), "r"), {})
                          .events[0]["dropped_operations"]
                          .size() == 1,
                  "Unauthorized regional national operation");
        auto changed = r.state;
        changed.territories["r"]["controller"] = "ES";
        changed.turn++;
        auto response =
            parse(R"([{"action":"respond_regional","region":"r","demand_id":"placeholder",)"
                  R"("response":"accepted"}])");
        response[0]["demand_id"] = r.state.regionalPolitics["r"]["demands"][0]["id"];
        check(compileOperations(changed, event("PT", response), {})
                      .events[0]["dropped_operations"]
                      .size() == 1,
              "Former authority cannot respond");
        auto accepted =
            applyDelta({changed, {}}, compileOperations(changed, event("ES", response), {}), {});
        check(accepted.state.regionalPolitics["r"]["demands"][0]["status"] == "accepted" &&
                  accepted.state.regionalPolitics["r"]["agenda"] ==
                      r.state.regionalPolitics["r"]["agenda"],
              "Control change preserves agenda and current authority responds");
        auto design = parse(
            R"({"layout":"horizontal","palette":["#112233","#445566","#778899"],"emblem":"circle",)"
            R"("explanation":"Civic unity"})");
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
        auto rename =
            parse(R"([{"action":"change_identity","actor":"PT","name":"Atlantic Republic",)"
                  R"("change_type":"rename","reason":"Adjudicated constitutional settlement"}])");
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
        auto creation =
            event("PT", parse(R"([{"action":"create_actor","actor_id":"new","name":"New State",)"
                              R"("color":"#123456"},{"action":"transfer_territory","region":"r",)"
                              R"("to":"new"}])"));
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
                        R"({"results":{"bindings":[{"item":{"value":"http://www.wikidata.org/)"
                        R"(entity/Q123"}}]}})");
                if (host == "https://www.wikidata.org")
                    return parse(R"({"entities":{"Q123":{"sitelinks":{"enwiki":{"title":"Local )"
                                 R"(Region"}}}}})");
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
