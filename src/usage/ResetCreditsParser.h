#pragma once

#include "usage/UsageModels.h"

#include <string_view>

namespace cqt
{

class ResetCreditsParser
{
public:
    [[nodiscard]] static ResetCreditsSnapshot Parse(std::string_view json, long long receivedAtUnixSeconds);
};

} // namespace cqt
