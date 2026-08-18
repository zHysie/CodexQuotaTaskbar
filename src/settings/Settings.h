#pragma once

#include "usage/UsageModels.h"

#include <filesystem>
#include <string>

namespace cqt
{

enum class LayoutMode { Vertical, Horizontal };
enum class ColorMode { QuotaAware, System, White, Black };

inline constexpr int kCurrentSettingsSchemaVersion = 3;

struct SettingsData
{
    int schemaVersion = kCurrentSettingsSchemaVersion;
    int refreshIntervalSeconds = 180;
    LayoutMode layout = LayoutMode::Vertical;
    bool showFiveHour = true;
    bool showWeekly = true;
    bool showSingleQuotaLabel = true;
    ColorMode colorMode = ColorMode::QuotaAware;
    QuotaProvider activeProvider = QuotaProvider::Codex;
};

[[nodiscard]] constexpr bool CanToggleSingleQuotaLabel(const SettingsData& settings) noexcept
{
    return settings.showFiveHour != settings.showWeekly;
}

[[nodiscard]] constexpr bool ToggleSingleQuotaLabel(SettingsData& settings) noexcept
{
    if (!CanToggleSingleQuotaLabel(settings)) return false;
    settings.showSingleQuotaLabel = !settings.showSingleQuotaLabel;
    return true;
}

class Settings
{
public:
    [[nodiscard]] static std::filesystem::path DefaultPath();
    [[nodiscard]] static SettingsData Load(const std::filesystem::path& path);
    [[nodiscard]] static bool Save(const std::filesystem::path& path, const SettingsData& settings,
                                   std::wstring& error);
    [[nodiscard]] static SettingsData Normalize(SettingsData settings);
};

} // namespace cqt
