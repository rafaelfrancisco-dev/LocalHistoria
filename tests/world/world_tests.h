#pragma once
#include "localhistoria/store.h"
#include "localhistoria/world.h"
#include <iostream>
using namespace lh;
void check(bool x, const char *what = "Assertion failed");
template <class F> void rejects(F f) {
    bool bad = false;
    try {
        f();
    } catch (const ValidationError &) {
        bad = true;
    }
    check(bad, "Expected rejection");
}
// The world delta suite, in its original order. Each method is one group of sections; members are
// what later groups build on.
class WorldTests {
  public:
    void deltas();
    void operations();
    void movement();
    void naming();
    void treaties();
    void events();

  private:
    static Json::Value operation(const Json::Value &schema, const char *name);
    WorldDelta compile(const char *json);
    static bool infeasible(const WorldDelta &x, Json::ArrayIndex index = 0);
    static bool changed(const WorldDelta &x, const char *kind, const char *id);
    static Json::Value effectOf(const WorldDelta &x, const char *actor, Json::ArrayIndex index = 0);

    // Fixture world and delta.
    WorldState w;
    Json::Value g, geo;
    Resolution base;
    WorldDelta d;
    // Operation compilation.
    Adjacency adjacency;
    Resolution opsBase, war, treaty, namedWorld;
    Json::Value facts, schema, ops, none;
    WorldDelta split2, compiled, orphan, reform, named, trade, offer, ended;
};
