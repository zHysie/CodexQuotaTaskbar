#pragma once

#include "settings/Settings.h"
#include "ui/TaskbarRenderer.h"
#include "usage/UsageModels.h"

namespace cqt
{

[[nodiscard]] TaskbarRenderModel BuildTaskbarRenderModel(
    const AppState& state,
    const SettingsData& settings);

} // namespace cqt
