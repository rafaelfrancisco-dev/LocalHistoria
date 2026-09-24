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
            // A restive region is where the month's one regional voice belongs.
            auto restive = v;
            restive.territories["s"]["unrest"] = 58.0;
            for (int n = 0; n < 50; ++n) {
                restive.campaignId = std::to_string(n);
                check(scheduleInitiatives(restive, {}, home)[2]["region"] == "s",
                      "A restive region takes the regional slot");
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
        // Plan 28 C: a demand has a deadline and is granted, refused or lapses.
        {
            auto v = w;
            v.engine["lifecycle_version"] = 1;
            v.engine["event_text_version"] = 2;
            v.turn = 5;
            auto raised = applyDelta({v, {}}, compileOperations(v, regional, {}), {}).state;
            const auto demand = raised.regionalPolitics["r"]["demands"][0];
            check(demand["answer_by_turn"] == 8,
                  "A demand in the player's region must be answered within three months");
            raised.regionalPolitics["r"]["relations"] = "neutral";
            auto month = [&](WorldState s, int turn, const Json::Value &responses) {
                s.turn = turn;
                auto d = compileOperations(s, event("PT", parse("[]")), {}, responses);
                return std::make_pair(applyDelta({s, {}}, d, {}).state, d);
            };
            auto quiet = Json::Value(Json::arrayValue);
            check(month(raised, 7, quiet).first.regionalPolitics["r"]["demands"][0]["status"] ==
                      "pending",
                  "A demand waits until its deadline");
            auto [lapsed, lapseDelta] = month(raised, 8, quiet);
            check(lapsed.regionalPolitics["r"]["demands"][0]["status"] == "expired" &&
                      lapsed.regionalPolitics["r"]["demands"][0]["lapse_reason"] == "unanswered" &&
                      lapsed.regionalPolitics["r"]["relations"] == "strained" &&
                      lapseDelta.events[0]["text"] ==
                          "Local Region's demand lapsed unanswered: Negotiate autonomy.",
                  "An unanswered demand lapses at its deadline as a refusal");
            auto grant = parse(R"([{"demand":"","region":"r","accept":true}])");
            grant[0]["demand"] = demand["id"];
            auto [granted, grantDelta] = month(raised, 6, grant);
            check(granted.regionalPolitics["r"]["demands"][0]["status"] == "accepted" &&
                      granted.regionalPolitics["r"]["relations"] == "cooperative" &&
                      grantDelta.events[0]["text"] ==
                          "PT granted Local Region's demand: Negotiate autonomy.",
                  "Granting a demand eases relations one step");
            grant[0]["accept"] = false;
            check(month(raised, 6, grant).first.regionalPolitics["r"]["relations"] == "strained",
                  "Refusing a demand sours relations one step");
            auto taken = raised;
            taken.territories["r"]["controller"] = "ES";
            auto handed = month(taken, 6, quiet).first.regionalPolitics["r"];
            check(handed["demands"][0]["status"] == "expired" &&
                      handed["demands"][0]["lapse_reason"] == "authority_changed" &&
                      handed["relations"] == "neutral",
                  "A demand to an authority that lost the region lapses without penalty");
            rejects([&] { month(taken, 6, grant); });
            auto stale = grant;
            stale[0]["demand"] = "t1-unknown";
            rejects([&] { month(raised, 6, stale); });
            auto abroad = event("ES",
                                parse(R"([{"action":"regional_initiative","region":"s",)"
                                      R"("agenda":"Local rights","relations":"neutral",)"
                                      R"("demand":"Language rights"}])"),
                                "s");
            check(applyDelta({v, {}}, compileOperations(v, abroad, {}), {})
                          .state.regionalPolitics["s"]["demands"][0]["answer_by_turn"] == 11,
                  "Another nation's region waits six months for an answer");
        }
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
        // A refounding is the one thing the model may write into a nation's status, and only
        // there: everything else about it still has to survive the delta unchanged.
        auto governed = refounded;
        governed.actors["PT"]["status"] =
            parse(R"({"government":"parliamentary_republic","government_since":1976,)"
                  R"("stability":70.0,"baseline_stability":70.0})");
        auto regime = rename;
        regime[0]["name"] = "Federal Atlantic Union";
        regime[0]["government"] = "military_junta";
        auto seizedPower =
            applyDelta({governed, {}}, compileOperations(governed, event("PT", regime), {}), {});
        check(seizedPower.state.actors["PT"]["status"]["government"] == "military_junta" &&
                  seizedPower.state.actors["PT"]["status"]["government_since"].asInt() == 2026 &&
                  seizedPower.state.actors["PT"]["status"]["stability"].asDouble() == 70.0,
              "A refounding sets the political system and nothing else in status");
        auto spelling = regime;
        spelling[0]["change_type"] = "correction";
        spelling[0]["name"] = "Atlantic republic";
        bool refused = false;
        try {
            compileOperations(governed, event("PT", spelling), {});
        } catch (...) {
            refused = true;
        }
        auto corrections = compileOperations(governed, event("PT", spelling), {});
        check(refused || corrections.events[0]["failed"].asBool() ||
                  applyDelta({governed, {}}, corrections, {})
                          .state.actors["PT"]["status"]["government"] == "parliamentary_republic",
              "A spelling correction is not a revolution");
        // The widened equality in checkActorChange is the whole security boundary: a refounding
        // carries government and government_since across, and every other status key must still
        // compare equal. Forge one onto an otherwise valid refounding and the delta must fail.
        auto forged = compileOperations(governed, event("PT", regime), {});
        for (auto &c : forged.changes)
            if (c["kind"] == "actor" && c["id"] == "PT")
                c["after"]["status"]["stability"] = 99.0;
        rejects([&] { applyDelta({governed, {}}, forged, {}); });
        auto smuggled = compileOperations(governed, event("PT", regime), {});
        for (auto &c : smuggled.changes)
            if (c["kind"] == "actor" && c["id"] == "PT")
                c["after"]["status"]["military"] = parse(R"({"active":9000000})");
        rejects([&] { applyDelta({governed, {}}, smuggled, {}); });

        auto invented = regime;
        invented[0]["government"] = "democracy";
        auto nonsense = compileOperations(governed, event("PT", invented), {});
        check(applyDelta({governed, {}}, nonsense, {}).state.actors["PT"]["status"]["government"] ==
                  "parliamentary_republic",
              "An unknown political system is refused");

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
        // Plan 26 J: urgent reactions cannot be crowded out by name mentions, a finite backlog of
        // urgent nations is served in turn, and a proposal's recipient answers.
        {
            WorldState u;
            u.player = "PL";
            u.turn = 10;
            u.engine["lifecycle_version"] = 1;
            for (auto a :
                 {"PL", "EN", "M1", "M2", "M3", "M4", "U1", "U2", "U3", "U4", "U5", "U6", "TR"})
                u.actors[a]["name"] = a;
            u.conflicts["war"] = parse(R"({"kind":"war","status":"open","actors":["PL","EN"],
                "started_turn":2,"hostile_pairs":[{"actors":["EN","PL"],"status":"active"}],
                "postures":{"PL":"high","EN":"high"}})");
            std::set<std::string> named{"M1", "M2", "M3", "M4"};
            std::vector<std::string> linked{"M1", "M2", "M3", "M4", "EN"};
            std::map<std::string, int> served;
            for (int month = 11; month < 17; ++month) {
                auto chosen =
                    selectReactive(reactiveCandidates(u, linked, named), served, month, 4);
                check(std::find(chosen.begin(), chosen.end(), "EN") != chosen.end(),
                      "Name mentions never crowd out the nation fighting the player");
                for (auto &a : chosen)
                    served[a] = month;
            }
            // Six nations in crisis, four places a month: all six are served within two months.
            auto crises = u;
            crises.conflicts.clear();
            std::vector<std::string> urgent{"U1", "U2", "U3", "U4", "U5", "U6"};
            for (auto &a : urgent)
                crises.actors[a]["status"]["stability"] = 10;
            std::map<std::string, int> last;
            std::set<std::string> reached;
            for (int month = 11; month < 13; ++month)
                for (auto &a :
                     selectReactive(reactiveCandidates(crises, urgent, {}), last, month, 4)) {
                    last[a] = month;
                    reached.insert(a);
                }
            check(reached.size() == 6, "A finite urgent backlog is served within bounded months");
            // A proposal's recipient owes an answer and outranks a war and every mention.
            auto offered = u;
            offered.proposals["k1"] = parse(R"({"status":"pending","from":"PL","proposed_turn":9,
                "actors":["PL","TR"],"answers":{"PL":{"accept":true}}})");
            auto chosen = selectReactive(reactiveCandidates(offered, linked, named), {}, 11, 4);
            check(chosen.front() == "TR" && chosen[1] == "EN",
                  "A pending treaty answer is served first, then the enemy");
            check(selectReactive(reactiveCandidates(offered, linked, named), {}, 11, 4) == chosen,
                  "Selection is deterministic");
        }
        std::cout << "Independent scheduler, regional authority/history, identities, safe flags, "
                     "rollback, reload, reference mocks and fair reactions passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
