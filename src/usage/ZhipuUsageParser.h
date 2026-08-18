#pragma once

#include "usage/UsageModels.h"

#include <string_view>

namespace cqt
{

class ZhipuUsageParser
{
public:
    [[nodiscard]] static ZhipuUsageSnapshot Parse(std::string_view json, long long receivedAtUnixSeconds);
};

} // namespace cqt
