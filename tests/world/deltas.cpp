#include "world_tests.h"
void WorldTests::deltas() {
    w.player = "old-state";
    w.campaignId = "fixture";
    w.actors =
        parse(R"({"old-state":{"name":"Old","color":"#123456"},"neighbor":{"name":"Neighbor",)"
              R"("color":"#654321"}})");
    w.territories =
        parse(R"({"west":{"name":"West","initial_affiliation":"old-state","owner":"old-state",)"
              R"("controller":"old-state"},"east":{"name":"East",)"
              R"("initial_affiliation":"old-state","owner":"old-state",)"
              R"("controller":"old-state"}})");
    g = parse(
        R"({"type":"Polygon","coordinates":[[[0,0],[10,0],[10,10],[0,10],[0,0]],[[4,4],[6,4],)"
        R"([6,6],[4,6],[4,4]]]})");
    geo["west"] = g;
    geo["east"] = g;
    check(containsPoint(g, 1, 1));
    check(!containsPoint(g, 5, 5));
    check(!containsPoint(g, 20, 20));
    base = resolve(w);
    d.events =
        parse(R"([{"id":"t1-change","actor":"old-state","actors":["old-state"],"regions":["west"],)"
              R"("entities":[],"causes":[],"text":"West changes hands.","background":false}])");
    auto change = [&](std::string kind, std::string id, Json::Value before, Json::Value after) {
        Json::Value c;
        c["kind"] = kind;
        c["id"] = id;
        c["before"] = before;
        c["after"] = after;
        c["event"] = "t1-change";
        c["rationale"] = "Fixture outcome";
        d.changes.append(c);
    };
    auto west = w.territories["west"];
    west["controller"] = "neighbor";
    change("territory", "west", w.territories["west"], west);
    auto occupied = applyDelta(base, d, geo);
    check(occupied.state.territories["west"]["owner"] == "old-state");
    check(occupied.state.territories["west"]["controller"] == "neighbor");
    d.changes[0]["after"]["owner"] = "neighbor";
    auto transferred = applyDelta(base, d, geo);
    check(transferred.state.territories["east"]["owner"] == "old-state");
    auto saved = d;
    d.changes.append(d.changes[0]);
    rejects([&] { applyDelta(base, d, geo); });
    d = saved;
    d.changes[0]["before"]["owner"] = "neighbor";
    rejects([&] { applyDelta(base, d, geo); });
    d = saved;
    d.changes = Json::arrayValue;
    change("actor", "new-state", Json::nullValue,
           parse(R"({"name":"New State","color":"#abcdef"})"));
    west["owner"] = "new-state";
    west["controller"] = "new-state";
    change("territory", "west", w.territories["west"], west);
    auto split = applyDelta(base, d, geo);
    check(split.state.hasActor("new-state") &&
          split.state.territories["west"]["owner"] == "new-state");
    auto entity =
        parse(R"({"name":"Harbor","kind":"port","actor":"old-state","region":"east","longitude":1,)"
              R"("latitude":1,"status":"active","origin_event":"t1-change"})");
    change("entity", "port-1", Json::nullValue, entity);
    auto built = applyDelta(base, d, geo);
    check(built.state.entities.isMember("port-1"));
    saved = d;
    d.changes[2]["after"]["longitude"] = 5;
    d.changes[2]["after"]["latitude"] = 5;
    rejects([&] { applyDelta(base, d, geo); });
    d = saved;
    saved = d;
    d.changes[2]["after"]["status"] = "under_construction";
    rejects([&] { applyDelta(base, d, geo); });
    d.changes[2]["after"]["completes_turn"] = 4;
    applyDelta(base, d, geo);
    d = saved;
    check(encode(decode(encode(built.state))) == encode(built.state));
    check(encode(applyDelta(base, parseDelta(encode(d)), geo).state) == encode(built.state));
    auto moveBase = resolve(built.state);
    WorldDelta move;
    move.events = d.events;
    move.events[0]["id"] = "t2-move";
    auto moved = built.state.entities["port-1"];
    moved["longitude"] = 2;
    Json::Value movement;
    movement["kind"] = "entity";
    movement["id"] = "port-1";
    movement["before"] = built.state.entities["port-1"];
    movement["after"] = moved;
    movement["event"] = "t2-move";
    movement["rationale"] = "Relocation";
    move.changes.append(movement);
    check(applyDelta(moveBase, move, geo).state.entities["port-1"]["longitude"].asInt() == 2);
    Store store(":memory:");
    store.create(w, false);
    auto job = store.submit(0, "key", "{}");
    Json::Value refs;
    refs["delta"] = encode(d);
    store.commit(job["id"].asString(), built, {}, refs);
    auto c = store.campaignContext(built.state, w.player);
    check(c.facts["related_history"].size() == 1);
    auto current = built.state;
    for (int n = 0; n < 30; ++n) {
        auto next = resolve(current);
        auto j = store.submit(current.turn, "key-" + std::to_string(n), "{}");
        store.commit(j["id"].asString(), next, {}, {});
        current = next.state;
    }
    check(store.campaignContext(current, w.player).facts["related_history"][0]["id"] ==
          "t1-change");
    auto previous = store.campaign();
    auto j = store.submit(current.turn, "bad-causes", "{}");
    auto next = resolve(current);
    WorldDelta bad;
    bad.events = d.events;
    bad.events[0]["id"] = "t32-bad";
    bad.events[0]["causes"].append("missing");
    refs["delta"] = encode(bad);
    rejects([&] { store.commit(j["id"].asString(), next, {}, refs); });
    check(store.campaign() == previous);
}
