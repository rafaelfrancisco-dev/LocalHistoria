#pragma once
#include "providers.h"
namespace lh {
Json::Value fetchHistory(const std::string &code, Transport transport = httpJson);
}
