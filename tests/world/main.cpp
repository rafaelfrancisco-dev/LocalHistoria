#include "world_tests.h"
void check(bool x, const char *what) {
    if (!x)
        throw std::runtime_error(what);
}
Json::Value WorldTests::operation(const Json::Value &schema, const char *name) {
    for (auto &o : schema["properties"]["events"]["prefixItems"][0]["properties"]["operations"]
                         ["items"]["anyOf"]) {
        auto &d = schema["$defs"][o["$ref"].asString().substr(8)];
        if (d["properties"]["action"]["enum"][0] == name)
            return d;
    }
    return Json::Value();
}
WorldDelta WorldTests::compile(const char *json) {
    return compileOperations(opsBase.state, parse(json), adjacency);
}
bool WorldTests::infeasible(const WorldDelta &x, Json::ArrayIndex index) {
    return !x.events[index]["dropped_operations"].empty() &&
           x.events[index]["text"].asString().find("Could not happen") == std::string::npos;
}
bool WorldTests::changed(const WorldDelta &x, const char *kind, const char *id) {
    for (auto &c : x.changes)
        if (c["kind"] == kind && c["id"] == id)
            return true;
    return false;
}
Json::Value WorldTests::effectOf(const WorldDelta &x, const char *actor, Json::ArrayIndex index) {
    for (auto &f : x.events[index]["effects"])
        if (f["actor"] == actor)
            return f;
    return Json::Value();
}
int main() {
    try {
        WorldTests t;
        t.deltas();
        t.operations();
        t.movement();
        t.naming();
        t.treaties();
        t.events();
        std::cout << "World deltas: occupation, secession, entity placement, holes, construction "
                     "schedule, reach, pacing, annexation, conflicts, reload, replay, old history "
                     "and atomic rollback passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
