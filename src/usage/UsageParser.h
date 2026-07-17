#pragma once

#include "usage/UsageModels.h"

#include <string_view>

namespace cqt
{

class UsageParser
{
public:
    [[nodiscard]] static UsageSnapshot Parse(std::string_view json, long long receivedAtUnixSeconds);
};

} // namespace cqt
