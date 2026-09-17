#pragma once
#include "providers.h"
namespace lh {
Json::Value fetchRegionalHistory(const std::string &name, const std::string &country, Transport transport = httpJson);
Json::Value fetchHistory(const std::string &code, Transport transport = httpJson);
}
