#include "status_tests.h"
#include <cmath>
namespace {
// A two-nation world: a capital and two provinces each, all adjacent within their country.
Resolution world(const char *government) {
    WorldState w;
    w.player = "AA";
    w.campaignId = "unrest";
    w.actors = parse(R"({"AA":{"name":"Aland","color":"#111111"},)"
                     R"("BB":{"name":"Bland","color":"#222222"}})");
    w.territories = parse(R"({
      "a1":{"name":"Capital","owner":"AA","controller":"AA","initial_affiliation":"AA","kind":"ADM1"},
      "a2":{"name":"Northmark","owner":"AA","controller":"AA","initial_affiliation":"AA","kind":"ADM1"},
      "a3":{"name":"Southmark","owner":"AA","controller":"AA","initial_affiliation":"AA","kind":"ADM1"},
      "b1":{"name":"Bland Capital","owner":"BB","controller":"BB","initial_affiliation":"BB","kind":"ADM1"}})");
    StatusSeed seed;
    seed.countries = parse(R"({
      "AA":{"population":20000000,"gdp_usd":200000000000,"gdp_growth":0,"pop_growth":0,
            "stability":60,"currency":"USD","symbol":"US$","usd_rate":1,"estimated":0},
      "BB":{"population":5000000,"gdp_usd":50000000000,"gdp_growth":0,"pop_growth":0,
            "stability":60,"currency":"USD","symbol":"US$","usd_rate":1,"estimated":0}})");
    seed.countries["AA"]["government"] = government;
    seed.countries["BB"]["government"] = "presidential_republic";
    seed.regions = parse(R"({"a1":{"population":10000000,"gdp_usd":100000000000},
      "a2":{"population":5000000,"gdp_usd":50000000000},
      "a3":{"population":5000000,"gdp_usd":50000000000},
      "b1":{"population":5000000,"gdp_usd":50000000000}})");
    seed.military = parse(R"({"AA":{"active":80000,"reserve":40000,"ships":0,"aircraft":0},
      "BB":{"active":20000,"reserve":10000,"ships":0,"aircraft":0}})");
    seedStatus(w, seed, false);
    seedMilitary(w, seed);
    return Resolution{w, {}};
}
const Adjacency adjacency{
    {"a1", {"a2", "a3"}}, {"a2", {"a1", "a3"}}, {"a3", {"a1", "a2"}}, {"b1", {}}};
// A grievance the campaign produced: the province is at odds with the capital and has two demands
// the capital has never answered.
void aggrieve(WorldState &w, const char *region) {
    w.regionalPolitics[region] =
        parse(R"({"agenda":"Home rule","relations":"strained","supporting_events":[],"demands":[
          {"id":"t0-a","text":"Devolve the budget","status":"pending"},
          {"id":"t0-b","text":"Recognise the language","status":"pending"}]})");
}
// Stability falls because the nation is failing, which is the only thing that opens the gate.
void depress(WorldState &w, const char *actor, double to) {
    w.actors[actor]["status"]["stability"] = to;
    w.actors[actor]["status"]["baseline_stability"] = to;
}
Resolution months(Resolution r, int n, bool keepDown = true) {
    for (int m = 0; m < n; ++m) {
        r.state.turn++;
        if (keepDown)
            depress(r.state, "AA", 30);
        r = applyMonthlyStatus(r, {}, adjacency);
    }
    return r;
}
double unrestOf(const Resolution &r, const char *region) {
    return r.state.territories[region]["unrest"].asDouble();
}
// A Resolution carries its events forward, so anything counted over a run has to start from where
// that run began.
int since(const Resolution &r, size_t from, const char *action) {
    int n = 0;
    for (size_t i = from; i < r.events.size(); ++i)
        n += r.events[i].action == action;
    return n;
}
} // namespace

void unrestTests() {
    // The gate: grievance alone is not enough, and a fall alone is not enough.
    {
        auto steady = world("presidential_republic");
        aggrieve(steady.state, "a2");
        auto after = months(steady, 12, false);
        check(unrestOf(after, "a2") == 0,
              "A nation at its seeded stability never accrues unrest, however aggrieved a region");
        auto quiet = world("presidential_republic");
        auto fallen = months(quiet, 12);
        check(unrestOf(fallen, "a2") == 0 && unrestOf(fallen, "a1") == 0,
              "A fall with no regional grievance accrues nothing");
    }

    // Both together accrue, and the nation's own system decides how fast.
    double democratic = 0, closed = 0;
    {
        auto open = world("parliamentary_republic");
        aggrieve(open.state, "a2");
        democratic = unrestOf(months(open, 12), "a2");
        auto shut = world("one_party_state");
        aggrieve(shut.state, "a2");
        closed = unrestOf(months(shut, 12), "a2");
        check(democratic > 0 && closed > democratic * 2,
              "A one-party state stores what a republic vents: " + std::to_string(democratic) +
                  " vs " + std::to_string(closed));
        // And it cools faster once the grievance is answered.
        auto calmed = world("parliamentary_republic");
        aggrieve(calmed.state, "a2");
        auto boiling = months(calmed, 12);
        boiling.state.regionalPolitics["a2"]["relations"] = "neutral";
        for (auto &d : boiling.state.regionalPolitics["a2"]["demands"])
            d["status"] = "accepted";
        auto settled = months(boiling, 6);
        check(unrestOf(settled, "a2") < unrestOf(boiling, "a2") / 2,
              "Answering the grievance calms the region");
    }

    // Rally, don't revolt: a front does not turn a region against its own defender, but occupation
    // by someone else does turn it against the occupier. This is the Ukraine regression.
    {
        auto invaded = world("semi_presidential_republic");
        invaded.state.conflicts["c0-1"] =
            parse(R"({"name":"Border War","kind":"war","actors":["AA","BB"],"status":"open",)"
                  R"("text":"War","started_turn":0,"set_intensity":"high","intensity":"high",)"
                  R"("affected_regions":["a2"],"theatre":["a2"],"casualties":0,"displaced":0})");
        auto fought = months(invaded, 12);
        check(unrestOf(fought, "a2") == 0,
              "A war front raises no unrest against the nation defending it");
        auto occupied = world("semi_presidential_republic");
        occupied.state.territories["a2"]["controller"] = "BB";
        depress(occupied.state, "BB", 30);
        auto held = occupied;
        for (int m = 0; m < 12; ++m) {
            held.state.turn++;
            depress(held.state, "BB", 30);
            held = applyMonthlyStatus(held, {}, adjacency);
        }
        check(unrestOf(held, "a2") > 0 && held.state.actors["BB"]["status"].isObject(),
              "Occupied ground resents its occupier");
    }

    // The chain: sustained unrest raises rebels, the rebels take ground, and holding it long
    // enough makes them a state.
    {
        auto boiling = world("military_junta");
        aggrieve(boiling.state, "a2");
        aggrieve(boiling.state, "a3");
        auto r = boiling;
        auto opened = r.events.size();
        int revolted = 0;
        for (int m = 0; m < 60 && !revolted; ++m) {
            r.state.turn++;
            depress(r.state, "AA", 25);
            r = applyMonthlyStatus(r, {}, adjacency);
            if (since(r, opened, "revolt"))
                revolted = r.state.turn;
        }
        check(revolted > 0, "Sustained unrest under a junta eventually raises rebels");
        std::string rebel, conflict;
        for (const auto &a : r.state.actors.getMemberNames())
            if (r.state.actors[a]["rebellion"].isObject()) {
                rebel = a;
                conflict = r.state.actors[a]["rebellion"]["conflict"].asString();
            }
        check(!rebel.empty() && r.state.conflicts.isMember(conflict),
              "The rebellion has an actor and a conflict");
        const auto &c = r.state.conflicts[conflict];
        check((c["kind"] == "insurgency" || c["kind"] == "civil_war") && c["actors"].size() == 2 &&
                  c["status"] == "open" && !c["name"].asString().empty() &&
                  validIntensity(c["set_intensity"].asString()),
              "The conflict is well formed: " + dump(c));
        check(r.state.actors[rebel]["status"]["government"] == "transitional_government" &&
                  isHexColor(r.state.actors[rebel]["color"].asString()) &&
                  r.state.actors[rebel]["identity_revisions"].isArray() &&
                  r.state.actors[rebel]["flag_revision"].asUInt() ==
                      r.state.actors[rebel]["identity_revisions"].size() - 1,
              "The rebel actor is well formed: " + dump(r.state.actors[rebel]));
        // Personnel are conserved: the rebels came out of the army, not out of nothing.
        auto rebels = r.state.actors[rebel]["status"]["military"]["active"].asInt64();
        check(rebels >= 1500 &&
                  r.state.actors["AA"]["status"]["military"]["active"].asInt64() + rebels == 80000,
              "Defecting personnel leave the army they came from");
        // Only ever one rebellion a month, campaign-wide, even with two regions past the line.
        check(since(r, opened, "revolt") == 1, "At most one rebellion a month");

        // Ground, then a state. The army is stripped so it cannot garrison what it has lost.
        r.state.actors["AA"]["status"]["military"]["active"] = 2000;
        auto fighting = r.events.size();
        int seceded = 0;
        for (int m = 0; m < 40 && !seceded; ++m) {
            r.state.turn++;
            depress(r.state, "AA", 20);
            r = applyMonthlyStatus(r, {}, adjacency);
            if (since(r, fighting, "secession"))
                seceded = r.state.turn;
        }
        check(since(r, fighting, "front_advance") > 0,
              "A rebellion the state cannot garrison takes ground");
        check(seceded > 0, "Ground held long enough becomes independence");
        bool owns = false;
        for (const auto &id : r.state.territories.getMemberNames())
            owns = owns || r.state.territories[id]["owner"] == rebel;
        check(owns, "A breakaway state owns its land, not merely controls it");
        check(r.state.actors[rebel]["rebellion"]["state"].asBool() &&
                  r.state.actors[rebel]["identity_revisions"].size() >= 2,
              "Independence is declared once, with its own identity revision");
        auto settled = r.events.size();
        check(since(months(r, 6), settled, "secession") == 0,
              "A state does not declare independence twice");
    }

    // Regime collapse: a government that cannot put down a civil war stops being that government.
    {
        auto failing = world("dominant_party_state");
        failing.state.conflicts["c0-1"] =
            parse(R"({"name":"Civil War in Aland","kind":"civil_war","actors":["AA","BB"],)"
                  R"("status":"open","text":"War","started_turn":0,"set_intensity":"low",)"
                  R"("intensity":"low","affected_regions":[],"theatre":[],"casualties":0,)"
                  R"("displaced":0})");
        auto r = failing;
        for (int m = 0; m < 8; ++m) {
            r.state.turn++;
            depress(r.state, "AA", 5);
            r.state.actors["AA"]["status"]["stability"] = 5;
            r = applyMonthlyStatus(r, {}, adjacency);
        }
        check(r.state.actors["AA"]["status"]["government"] == "transitional_government",
              "A collapsing government is replaced by a transitional one");
        check(since(r, 0, "regime_change") == 1, "The collapse is reported, once");
    }

    // Version gating and determinism.
    {
        auto boiling = world("one_party_state");
        aggrieve(boiling.state, "a2");
        auto live = months(boiling, 12);
        auto legacy = boiling;
        for (int m = 0; m < 12; ++m) {
            legacy.state.turn++;
            depress(legacy.state, "AA", 30);
            legacy = applyMonthlyStatus(legacy, {}, adjacency, unrestVersion - 1);
        }
        check(unrestOf(live, "a2") > 0 && !legacy.state.territories["a2"].isMember("unrest"),
              "Turns recorded before this rule gain no unrest at all");
        check(encode(months(boiling, 12).state) == encode(live.state) &&
                  encode(months(boiling, 12).events) == encode(live.events),
              "Unrest is deterministic");
    }

    // The political system itself: every value classifies, and the labels are readable.
    check(governments().size() == 12 && validGovernment("military_junta") &&
              !validGovernment("democracy") &&
              governmentLabel("one_party_state") == "One-party state",
          "The political-system vocabulary is closed");
    check(regime("one_party_state").repression > regime("parliamentary_republic").repression &&
              regime("parliamentary_republic").vent > regime("military_junta").vent &&
              !regime("uninhabited_territory").inhabited,
          "Regimes differ in the direction they should");
    check(unrestDescription(0) == "calm" && unrestDescription(90) == "in open rebellion",
          "Unrest bands are named");
}
