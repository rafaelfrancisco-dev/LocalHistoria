#pragma once
#include "localhistoria/world.h"
namespace lh::detail {
inline void require(bool b, const char *message) {
    if (!b)
        throw ValidationError(message);
}
} // namespace lh::detail
