#pragma once

#include "usage/CodexAuthReader.h"
#include "usage/UsageModels.h"
#include "usage/ZhipuAuthReader.h"

#include <string>

namespace cqt
{

[[nodiscard]] std::wstring BuildTooltipText(
    const AppState& state,
    const AuthSearchPaths& authPaths,
    const ZhipuAuthSearchPaths& zhipuAuthPaths,
    long long nowUnixSeconds);

} // namespace cqt
