#pragma once
#include <json/json.h>
#include <string>
// Member names shared across the saved world and the model's operations. Reads go through const
// references: the non-const Json::Value::operator[] inserts a null member when the key is missing.
namespace lh {
namespace key {
inline constexpr const char *owner = "owner", *controller = "controller", *status = "status",
                            *actors = "actors", *kind = "kind", *region = "region", *name = "name";
} // namespace key
// A conflict or commitment still in force.
inline bool isOpen(const Json::Value &v) { return v[key::status] == "open"; }
// Whether an actor is among a conflict's, commitment's or proposal's parties.
inline bool isParty(const Json::Value &v, const std::string &a) {
    for (const auto &x : v[key::actors])
        if (x == a)
            return true;
    return false;
}
inline std::string controllerOf(const Json::Value &territory) {
    return territory[key::controller].asString();
}
} // namespace lh
