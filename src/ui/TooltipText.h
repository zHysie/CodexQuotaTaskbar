#pragma once

#include "usage/CodexAuthReader.h"
#include "usage/UsageModels.h"

#include <string>

namespace cqt
{

[[nodiscard]] std::wstring BuildTooltipText(
    const AppState& state,
    const AuthSearchPaths& authPaths,
    long long nowUnixSeconds);

} // namespace cqt
